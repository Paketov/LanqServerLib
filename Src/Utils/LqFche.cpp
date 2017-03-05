
#include "LqFche.h"
#include "LqAlloc.hpp"
#include "LqTbl.hpp"
#include "LqLock.hpp"
#include "LqDef.hpp"
#include "LqFile.h"
#include "LqAtm.hpp"


#undef max

static const float __FloatMax = 9999e+32f * 9999e+32f * 9999e+32f;

#define LqFcheTblRemove(Tbl, Elem) LqTblRowRemove(Tbl, Elem, TblNext, _LqFcheIndexByVal, false, __LqCachedFile)
#define LqFcheTblSearch(Tbl, Key, Res) LqTblRowSearch(Tbl, Key, Res, TblNext, _LqFcheIndexByKey, _LqFcheCmpKeyVal, __LqCachedFile)
#define LqFcheTblInsert(Tbl, NewVal) LqTblRowInsert(Tbl, NewVal, TblNext, _LqFcheIndexByVal, __LqCachedFile)


static void _LqFcheRemoveFromList(__LqCachedFile* CachFile);
static void _LqFcheAssignCachedFile(__LqCachedFile* f);

static intptr_t _FcheCloseProc(LqFbuf* Context) {
	__LqCachedFile* VirtFile = (__LqCachedFile*)Context->UserData;
	LqFche* Owner = VirtFile->Owner;
	size_t Expected = VirtFile->CountRef;
	for(; !LqAtmCmpXchg(VirtFile->CountRef, Expected, Expected - 1); Expected = VirtFile->CountRef);
	if(Expected == 1) {
		LqAtmLkWr(Owner->Locker);
		_LqFcheRemoveFromList(VirtFile);
		if(VirtFile->Path != NULL)
			free(VirtFile->Path);
		if(VirtFile->File.Flags != 0)
			LqFbuf_close(&VirtFile->File);
		LqFastAlloc::Delete(VirtFile);
		Expected = Owner->FilePtrs;
		for(; !LqAtmCmpXchg(Owner->FilePtrs, Expected, Expected - 1); Expected = Owner->FilePtrs);
		if(Expected <= 1) {
			LqTblRowUninit(&Owner->Table);
			LqFastAlloc::Delete(Owner);
		} else {
			LqAtmUlkWr(Owner->Locker);
		}
	}
	return 0;
}

static intptr_t _EmptySeekProc(LqFbuf*, int64_t, int) {
	return -1;
}

static intptr_t _EmptyReadProc(LqFbuf* Context, char*, size_t) {
	Context->Flags |= LQFBUF_READ_ERROR;
	return 0;
}

static intptr_t _EmptyWriteProc(LqFbuf* Context, char*, size_t) {
	Context->Flags |= LQFBUF_WRITE_ERROR;
	return 0;
}

static bool _FcheCopyProc(LqFbuf* Dest, LqFbuf* Source) {
	__LqCachedFile* VirtFile = (__LqCachedFile*)Source->UserData;
	Dest->UserData = VirtFile;
	_LqFcheAssignCachedFile(VirtFile);
	return true;
}

static LqFbufCookie _FcheCookie = {
	_EmptyReadProc,
	_EmptyWriteProc,
	_EmptySeekProc,
	_FcheCopyProc,
	_FcheCloseProc
};

static inline size_t _LqFcheIndexByVal(__LqCachedFile* Val, size_t MaxCount) {
	return Val->PathHash % MaxCount;
}

static size_t _LqFcheIndexByKey(const char* Key, size_t MaxCount) {
	size_t h = 0;
	for(const char* k = Key; *k != '\0'; k++)
		h = 31 * h + *k;
	return h % MaxCount;
}

static inline bool _LqFcheCmpKeyVal(const char* Key, __LqCachedFile* Val) {
	return LqStrSame(Key, Val->Path);
}

static void _LqFcheRemoveFromList(__LqCachedFile* CachFile) {
	CachFile->Next->Prev = CachFile->Prev;
	CachFile->Prev->Next = CachFile->Next;
	if(CachFile->Owner->CachedListEnd == CachFile)
		CachFile->Owner->CachedListEnd = CachFile->Next;
	if(CachFile->File.Flags == 0)
		CachFile->Owner->CountInUncached--;
	else
		CachFile->Owner->CurSize -= (CachFile->SizeFile + sizeof(__LqCachedFile));
}

static bool _LqFcheRemoveFile(__LqCachedFile* CachFile) {
	LqFche* Owner = CachFile->Owner;
	LqFcheTblRemove(&CachFile->Owner->Table, CachFile);
	size_t Expected = CachFile->CountRef;
	for(; !LqAtmCmpXchg(CachFile->CountRef, Expected, Expected - 1); Expected = CachFile->CountRef);
	if(Expected == 1) {
		_LqFcheRemoveFromList(CachFile);
		if(CachFile->Path != NULL)
			free(CachFile->Path);
		if(CachFile->File.Flags != 0)
			LqFbuf_close(&CachFile->File);
		LqFastAlloc::Delete(CachFile);
		LqAtmIntrlkDec(Owner->FilePtrs);
		return true;
	}
	return false;
}

static inline void _LqFcheAssignCachedFile(__LqCachedFile* f) {
	LqAtmIntrlkInc(f->CountRef);
}

static bool _LqFcheWaitCachedFile(__LqCachedFile* f) {
	if(f->CachingLocker == 0) {
		_LqFcheAssignCachedFile(f);
		LqAtmUlkWr(f->Owner->Locker);
		LqAtmLkWr(f->CachingLocker);
		LqAtmLkWr(f->Owner->Locker);
		LqAtmUlkWr(f->CachingLocker);
		_LqFcheRemoveFile(f);
		return true;
	}
	return false;
}

static void _LqFcheAddInList(__LqCachedFile* Cached) {
	Cached->Next = Cached->Owner->RatingList.Next;
	Cached->Next->Prev = Cached->Owner->RatingList.Next = Cached;
	Cached->Prev = &Cached->Owner->RatingList;
	Cached->Owner->CountInUncached++;
}

static __LqCachedFile* _LqFcheSearchFile(LqFche* Che, const char* Name) {
	__LqCachedFile* Res;
#if defined(LQPLATFORM_WINDOWS)
	size_t l;
	char* SearchStr;
	char Buf[256];
	if((Name[0] == '\\') && (Name[1] == '\\') && (Name[2] == '?') && (Name[3] == '\\'))
		Name += 4;
	l = LqStrLen(Name);
	if(l < 230) {
		LqStrUtf8ToLower(Buf, sizeof(Buf), Name, -1);
		LqFcheTblSearch(&Che->Table, Buf, Res);
		return Res;
	}//// !!!!!!!!!!!!! C:ddd
	if((SearchStr = (char*)malloc(l + 70)) == NULL) {
		LqFcheTblSearch(&Che->Table, Name, Res);
		return Res;
	}
	LqStrUtf8ToLower(SearchStr, l + 60, Name, -1);
	LqFcheTblSearch(&Che->Table, SearchStr, Res);
	free(SearchStr);
	return Res;
#else
	LqFcheTblSearch(&Che->Table, Name, Res);
	return Res;
#endif
}

static __LqCachedFile* _LqFcheInsertFile(LqFche* Che, const char* Name) {
	__LqCachedFile* NewFileReg;
	size_t Hash;
#if defined(LQPLATFORM_WINDOWS)
	char Buf[256];
	char* SearchStr;
	size_t l;
	if((Name[0] == '\\') && (Name[1] == '\\') && (Name[2] == '?') && (Name[3] == '\\'))
		Name += 4;
	l = LqStrLen(Name);
	if(l < 230) {
		LqStrUtf8ToLower(Buf, sizeof(Buf), Name, -1);
		if((NewFileReg = LqFastAlloc::New<__LqCachedFile>()) != NULL) {
			NewFileReg->CountReaded = 1;
			LqAtmLkInit(NewFileReg->CachingLocker);
			NewFileReg->File.Flags = 0;
			NewFileReg->Path = LqStrDuplicate(Buf);
			NewFileReg->Owner = Che;
			LqAtmIntrlkInc(Che->FilePtrs);
			Hash = 0;
			for(const char* k = NewFileReg->Path; *k != '\0'; k++)
				Hash = 31 * Hash + *k;
			NewFileReg->PathHash = Hash;
			LqFcheTblInsert(&Che->Table, NewFileReg);
			_LqFcheAddInList(NewFileReg);
		}
		return NewFileReg;
	}
	if((SearchStr = (char*)malloc(l + 70)) == NULL)
		return NULL;
	LqStrUtf8ToLower(SearchStr, l + 60, Name, -1);
	if((NewFileReg = LqFastAlloc::New<__LqCachedFile>()) != NULL) {
		NewFileReg->CountReaded = 1;
		LqAtmLkInit(NewFileReg->CachingLocker);
		NewFileReg->File.Flags = 0;
		NewFileReg->Owner = Che;
		LqAtmIntrlkInc(Che->FilePtrs);
		NewFileReg->Path = LqStrDuplicate(SearchStr);
		Hash = 0;
		for(const char* k = NewFileReg->Path; *k != '\0'; k++)
			Hash = 31 * Hash + *k;
		NewFileReg->PathHash = Hash;
		LqFcheTblInsert(&Che->Table, NewFileReg);
		_LqFcheAddInList(NewFileReg);
	}
	free(SearchStr);
	return NewFileReg;
#else
	if((NewFileReg = LqFastAlloc::New<__LqCachedFile>()) != NULL) {
		NewFileReg->CountReaded = 1;
		LqAtmLkInit(NewFileReg->CachingLocker);
		NewFileReg->File.Flags = 0;
		NewFileReg->Path = LqStrDuplicate(Name);
		NewFileReg->Owner = Che;
		LqAtmIntrlkInc(Che->FilePtrs);
		Hash = 0;
		for(const char* k = NewFileReg->Path; *k != '\0'; k++)
			Hash = 31 * Hash + *k;
		NewFileReg->PathHash = Hash;
		LqFcheTblInsert(&Che->Table, NewFileReg);
		_LqFcheAddInList(NewFileReg);
	}
	return NewFileReg;
#endif
}

static float _LqFcheCountReadPerSec(__LqCachedFile* Cached, LqTimeMillisec CurTimeMillisec) {
	if(Cached->Path == NULL)
		return __FloatMax;
	double TimePassed = (double)(CurTimeMillisec - Cached->LastTestTime);
	return (TimePassed < 100.0) ? __FloatMax : (Cached->CountReaded / TimePassed * 1000.0);
}

static bool _LqFcheAddFile(LqFche* Che, const char* Path, LqTimeMillisec CurTime, LqFileStat* s) {
	__LqCachedFile* NewFileReg;
	if(LqFileGetStat(Path, s) != 0)
		return false;
	if((s->Size > Che->MaxSizeFile) || (s->Size >= Che->MaxSizeBuff))
		return false;
	if(Che->CountInUncached >= Che->MaxInPreparedList) {
		if(((Che->RatingList.Next->LastTestTime + 1000) >= CurTime) ||
			(_LqFcheCountReadPerSec(Che->RatingList.Next, CurTime) >= Che->PerSecReadAdding))
			return false;
		_LqFcheRemoveFile(Che->RatingList.Next);
		NewFileReg = _LqFcheInsertFile(Che, Path);
	} else {
		NewFileReg = _LqFcheInsertFile(Che, Path);
	}
	if(NewFileReg == NULL)
		return false;
	NewFileReg->SizeFile = s->Size;
	NewFileReg->LastModifTime = s->ModifTime;
	NewFileReg->LastTestTime = CurTime;
	NewFileReg->CountRef = 1;
	return true;
}

static bool _LqFcheUpdateStat(LqFche* Cache, __LqCachedFile* Cached, LqTimeMillisec CurTimeMillisec) {
	if((Cached->LastTestTime + Cache->PeriodUpdateStatMillisec) < CurTimeMillisec) {
		Cached->LastTestTime = CurTimeMillisec;
		Cached->CountReaded = 1;
		return true;
	}
	Cached->CountReaded++;
	return false;
}

static bool _LqFcheClearPlaceForFirstInUncached(LqFche* Cache, ullong CurTime) {
	__LqCachedFile *c, *i, *t;
	intptr_t CommonDelSize = Cache->MaxSizeBuff - Cache->CurSize;
	__LqCachedFile * CachFile = Cache->CachedListEnd->Prev;
	float CachFileReadPerSec = _LqFcheCountReadPerSec(CachFile, CurTime);

	c = Cache->CachedListEnd;
	for(; c != &Cache->RatingList; c = c->Next) {
		if(CachFile->SizeFile <= CommonDelSize)
			break;
		if((c->CountRef <= 1) && (_LqFcheCountReadPerSec(c, CurTime) < CachFileReadPerSec))
			CommonDelSize += c->SizeFile;
		else
			break;
	}
	if(CachFile->SizeFile <= CommonDelSize) {
		for(i = Cache->CachedListEnd; i != c; i = t) {
			if((i->CountRef <= 1) && (_LqFcheCountReadPerSec(i, CurTime) < CachFileReadPerSec)) {
				t = i->Next;
				_LqFcheRemoveFile(i);
			} else {
				t = i->Next;
			}
		}
	} else {
		return false;
	}
	return true;
}

static void _LqFcheRatingUp(LqFche* Cache, __LqCachedFile* CachFile) {
	__LqCachedFile *n, *p;
	n = CachFile->Next;
	if(n != &Cache->RatingList) {
		p = CachFile->Prev;
		n->Prev = p;
		CachFile->Next = n->Next;
		CachFile->Next->Prev = CachFile;
		n->Next = CachFile;
		CachFile->Prev = n;
		p->Next = n;
	}
	if(Cache->CachedListEnd == &Cache->RatingList)
		Cache->CachedListEnd = CachFile;
	else if(Cache->CachedListEnd == CachFile)
		Cache->CachedListEnd = CachFile->Prev;
}

static bool _LqFcheBeforeRead(__LqCachedFile* CachFile, LqTimeMillisec CurTime, LqFileStat* s) {
	int Fd;
	LqFche* Cache = CachFile->Owner;
	float CachFileReadPerSec = _LqFcheCountReadPerSec(CachFile, CurTime);
	if(CachFile->File.Flags == 0) {
		if((CachFile->Next != Cache->CachedListEnd) ||
			((CachFile->SizeFile + Cache->CurSize + sizeof(__LqCachedFile)) >= Cache->MaxSizeBuff) &&
		   ((_LqFcheCountReadPerSec(CachFile->Next, CurTime) >= CachFileReadPerSec) ||
		   (CachFileReadPerSec == __FloatMax)) ||
		   (CachFile->Next->CachingLocker == 0))
			return true;

		LqAtmLkWr(CachFile->CachingLocker);
		LqAtmUlkWr(Cache->Locker); //Unlock all cache for longer time descriptor opening.

		if(((Fd = LqFileOpen(CachFile->Path, LQ_O_RD | LQ_O_BIN | LQ_O_SEQ, 0)) == -1) || (LqFileGetStatByFd(Fd, s) != 0)) {
			if(Fd != -1)
				LqFileClose(Fd);
			LqAtmLkWr(Cache->Locker);
			LqAtmUlkWr(CachFile->CachingLocker);
			_LqFcheRemoveFile(CachFile);
			return false;
		}

		LqAtmLkWr(Cache->Locker);   //Again lock for internal processing.
		CachFile->LastModifTime = s->ModifTime;
		if(((CachFile->SizeFile = s->Size) + Cache->CurSize + sizeof(__LqCachedFile)) >= Cache->MaxSizeBuff) {
			if(!_LqFcheClearPlaceForFirstInUncached(Cache, CurTime)) {
				LqAtmUlkWr(CachFile->CachingLocker);
				LqFileClose(Fd);
				return true;
			}
		}
		_LqFcheRatingUp(Cache, CachFile);
		Cache->CurSize += (CachFile->SizeFile + sizeof(__LqCachedFile));
		Cache->CountInUncached--;
		LqAtmUlkWr(Cache->Locker);  //Unlock all cache for longer time reading.

		LqFbuf_fdopen(&CachFile->File, LQFBUF_FAST_LK, Fd, 0, 0, 32750);
		if(LqFbuf_peek(&CachFile->File, NULL, CachFile->SizeFile) < CachFile->SizeFile) {
			LqFbuf_close(&CachFile->File);
			CachFile->File.Flags = 0;
			LqAtmLkWr(Cache->Locker);
			LqAtmUlkWr(CachFile->CachingLocker);
			_LqFcheRemoveFile(CachFile);
			return false;
		}
		LqFbuf_make_ptr(&CachFile->File);
		LqFbuf_set_ptr_cookie(&CachFile->File, CachFile, &_FcheCookie);
		LqAtmLkWr(Cache->Locker); //Again lock for internal processing.
		LqAtmUlkWr(CachFile->CachingLocker);
		return true;
	}
	if(!(CachFile->Next->CachingLocker == 0) && (_LqFcheCountReadPerSec(CachFile->Next, CurTime) < CachFileReadPerSec))
		_LqFcheRatingUp(Cache, CachFile);
	return true;
}


static bool _LqFcheRecache(__LqCachedFile* CachFile, int *Fd) {
	LqFbuf_close(&CachFile->File);
	LqFbuf_fdopen(&CachFile->File, LQFBUF_FAST_LK, *Fd, 0, 0, 32750);
	*Fd = -1;
	if(LqFbuf_peek(&CachFile->File, NULL, CachFile->SizeFile) < CachFile->SizeFile) {
		LqFbuf_close(&CachFile->File);
		return false;
	}
	LqFbuf_make_ptr(&CachFile->File);
	LqFbuf_set_ptr_cookie(&CachFile->File, CachFile, &_FcheCookie);
	return true;
}

static bool _LqFcheUpdateFile(__LqCachedFile* CachFile, LqFileStat* s) {
	intptr_t Diffr;
	LqFche* Che = CachFile->Owner;
	if(CachFile->File.Flags == 0) {
		if(LqFileGetStat(CachFile->Path, s) != 0) {
			_LqFcheRemoveFile(CachFile);
		} else {
			CachFile->SizeFile = s->Size;
			CachFile->LastModifTime = s->ModifTime;
		}
	} else {
		LqAtmLkWr(CachFile->CachingLocker); //Lock only file
		LqAtmUlkWr(Che->Locker); //Unlock all cache for longer time descriptor opening.
		int Fd;
		if(((Fd = LqFileOpen(CachFile->Path, LQ_O_RD | LQ_O_BIN | LQ_O_SEQ, 0)) == -1) || (LqFileGetStatByFd(Fd, s) != 0)) {
			if(Fd != -1)
				LqFileClose(Fd);
			LqAtmLkWr(Che->Locker);
			LqAtmUlkWr(CachFile->CachingLocker);
			_LqFcheRemoveFile(CachFile);
		} else if((CachFile->LastModifTime != s->ModifTime) || (CachFile->SizeFile != s->Size)) {
			Diffr = s->Size - CachFile->SizeFile;
			CachFile->SizeFile = s->Size;
			CachFile->LastModifTime = s->ModifTime;

			if(((Diffr + Che->CurSize) >= Che->MaxSizeBuff) || (s->Size > Che->MaxSizeFile) || !((CachFile->CountRef == 1) && _LqFcheRecache(CachFile, &Fd))) {
				if(Fd != -1)
					LqFileClose(Fd);
				LqAtmLkWr(Che->Locker); //Lock again for internal operations.
				LqAtmUlkWr(CachFile->CachingLocker);
				_LqFcheRemoveFile(CachFile);
			} else {
				LqAtmLkWr(Che->Locker);
				Che->CurSize += Diffr;
				LqAtmUlkWr(CachFile->CachingLocker);
				return true;
			}
		} else {
			LqFileClose(Fd);
			LqAtmLkWr(Che->Locker); //Lock again for internal operations.
			LqAtmUlkWr(CachFile->CachingLocker);
			return true;
		}
	}
	return false;
}

size_t _LqFcheLqFcheClear(LqFche* Che) {
	size_t CountFree = 0;
	__LqCachedFile* i, *t;
	for(i = Che->RatingList.Next; i != &Che->RatingList; ) {
		t = i->Next;
		if(_LqFcheWaitCachedFile(i)) {
			i = Che->RatingList.Next;
			continue;
		}
		if(_LqFcheRemoveFile(i))
			CountFree++;
		i = t;
	}
	return CountFree;
}


LQ_EXTERN_C LqFche* LQ_CALL LqFcheCreate() {
	LqFche* Che = LqFastAlloc::New<LqFche>();
	Che->CachedListEnd = &Che->RatingList;
	Che->CurSize = 0;
	Che->MaxSizeBuff = 1024 * 1024 * 100; //100 mb
	Che->MaxInPreparedList = 30;
	Che->CountInUncached = 0;
	Che->PeriodUpdateStatMillisec = 5 * 1000; //5 sec
	Che->PerSecReadAdding = 1.f;
	Che->MaxSizeFile = 70 * 1024 * 1024; // 70mb
	Che->RatingList.SizeFile = 0;
	Che->RatingList.CountReaded = 1;
	Che->RatingList.PathHash = 0;
	Che->RatingList.LastTestTime = LqTimeGetMaxMillisec();
	Che->RatingList.Prev = Che->RatingList.Next = &Che->RatingList;
	Che->RatingList.CountRef = 1;
	Che->RatingList.Owner = Che;
	Che->CountPtrs = 1;
	Che->FilePtrs = 0;
	LqTblRowInit(&Che->Table);
	LqAtmLkInit(Che->Locker);
	LqAtmLkInit(Che->RatingList.CachingLocker);
	return Che;
}

LQ_EXTERN_C LqFche* LQ_CALL LqFcheCopy(LqFche* Che) {
	LqAtmIntrlkInc(Che->CountPtrs);
	return Che;
}

LQ_EXTERN_C void LQ_CALL LqFcheDelete(LqFche* RmSrc) {
	size_t Expected = RmSrc->CountPtrs;
	for(; !LqAtmCmpXchg(RmSrc->CountPtrs, Expected, Expected - 1); Expected = RmSrc->CountPtrs);
	if(Expected == 1) {
		LqAtmLkWr(RmSrc->Locker);
		_LqFcheLqFcheClear(RmSrc);
		if(RmSrc->FilePtrs == 0) {
			LqTblRowUninit(&RmSrc->Table);
			LqFastAlloc::Delete(RmSrc);
			return;
		}
		LqAtmUlkWr(RmSrc->Locker);
	}
}

LQ_EXTERN_C size_t LQ_CALL LqFcheClear(LqFche* Che) {
	size_t Res;
	LqAtmLkWr(Che->Locker);
	Res = _LqFcheLqFcheClear(Che);
	LqAtmUlkWr(Che->Locker);
	return Res;
}

LQ_EXTERN_C bool LQ_CALL LqFcheRead(LqFche* Che, const char* Path, LqFbuf* Fbuf) {
	LqFileStat Stat;
	LqTimeMillisec CurTime;
	__LqCachedFile* VirtFile;

	LqAtmLkWr(Che->Locker);
	VirtFile = _LqFcheSearchFile(Che, Path);
	CurTime = LqTimeGetLocMillisec();
	if(VirtFile == NULL) {
		_LqFcheAddFile(Che, Path, CurTime, &Stat);
	} else {
		_LqFcheUpdateStat(Che, VirtFile, CurTime);
		VirtFile->CountReaded++;
		if(VirtFile->CachingLocker == 0) {
			LqAtmUlkWr(Che->Locker);
			return false;
		}
		if(_LqFcheBeforeRead(VirtFile, CurTime, &Stat)) {
			if(VirtFile->File.Flags != 0) {
				LqFbuf_copy(Fbuf, &VirtFile->File);
				LqAtmUlkWr(Che->Locker);
				return true;
			}
		}
	}
	LqAtmUlkWr(Che->Locker);
	return false;
}

LQ_EXTERN_C bool LQ_CALL LqFcheUpdateAndRead(LqFche* Che, const char* Path, LqFbuf* Fbuf) {
	LqFileStat Stat;
	__LqCachedFile* VirtFile;
	LqTimeMillisec CurTime;
	bool UpdateStatRes;
	float CachFileReadPerSec;

	LqAtmLkWr(Che->Locker);
	VirtFile = _LqFcheSearchFile(Che, Path);
	CurTime = LqTimeGetLocMillisec();
	if(VirtFile == NULL) {
		_LqFcheAddFile(Che, Path, CurTime, &Stat);
	} else {
		UpdateStatRes = _LqFcheUpdateStat(Che, VirtFile, CurTime);
		VirtFile->CountReaded++;
		if(VirtFile->CachingLocker == 0) {
			LqAtmUlkWr(Che->Locker);
			return false;
		}
		if(UpdateStatRes && (VirtFile->File.Flags != 0)) {
			if(!_LqFcheUpdateFile(VirtFile, &Stat)) {
				LqAtmUlkWr(Che->Locker);
				return false;
			}
			CachFileReadPerSec = _LqFcheCountReadPerSec(VirtFile, CurTime);
			if(!(VirtFile->Next->CachingLocker != 0) && (_LqFcheCountReadPerSec(VirtFile->Next, CurTime) < CachFileReadPerSec))
				_LqFcheRatingUp(Che, VirtFile);
			LqFbuf_copy(Fbuf, &VirtFile->File);
			LqAtmUlkWr(Che->Locker);
			return true;
		} else if(_LqFcheBeforeRead(VirtFile, CurTime, &Stat)) {
			if(VirtFile->File.Flags != 0) {
				LqFbuf_copy(Fbuf, &VirtFile->File);
				LqAtmUlkWr(Che->Locker);
				return true;
			}
		}
	}
	LqAtmUlkWr(Che->Locker);
	return false;
}

LQ_EXTERN_C void LQ_CALL LqFcheUpdateAllCache(LqFche* Che) {
	LqFileStat LocalStat;
	__LqCachedFile* i, *t;
	LqAtmLkWr(Che->Locker);
	for(i = Che->RatingList.Next; i != &Che->RatingList; ) {
		t = i->Next;
		if(i->CachingLocker != 0)
			_LqFcheUpdateFile(i, &LocalStat);
		i = t;
	}
	LqAtmUlkWr(Che->Locker);
}

LQ_EXTERN_C int LQ_CALL LqFcheUpdate(LqFche* Che, const char* Path) {
	LqFileStat Stat;
	__LqCachedFile* f;
	int r = -1;

	LqAtmLkWr(Che->Locker);
	if(f = _LqFcheSearchFile(Che, Path)) {
		if(f->CachingLocker != 0)
			r = (_LqFcheUpdateFile(f, &Stat)) ? 0 : 1;
		else
			r = 0;
	}
	LqAtmUlkWr(Che->Locker);
	return r;
}

LQ_EXTERN_C bool LQ_CALL LqFcheUncache(LqFche* Che, const char* Path) {
	__LqCachedFile *VirtFile;
	bool IsFree = false;
	LqAtmLkWr(Che->Locker);
	if(VirtFile = _LqFcheSearchFile(Che, Path)) {
		_LqFcheWaitCachedFile(VirtFile);
		IsFree = _LqFcheRemoveFile(VirtFile);
	}
	LqAtmUlkWr(Che->Locker);
	return IsFree;
}


LQ_EXTERN_C size_t LQ_CALL LqFcheGetMaxSizeFile(LqFche* Che) { 
	return Che->MaxSizeFile;
}

LQ_EXTERN_C void LQ_CALL LqFcheSetMaxSizeFile(LqFche* Che, size_t NewSize) {
	__LqCachedFile *i, *t;
	LqAtmLkWr(Che->Locker);
	if(Che->MaxSizeFile >= NewSize) {
		Che->MaxSizeFile = NewSize;
		LqAtmUlkWr(Che->Locker);
		return;
	}
	for(i = Che->CachedListEnd; i != &Che->RatingList; ) {
		t = i->Next;
		if(i->SizeFile > NewSize)
			_LqFcheRemoveFile(i);
		i = t;
	}
	Che->MaxSizeFile = NewSize;
	LqAtmUlkWr(Che->Locker);
}

LQ_EXTERN_C size_t LQ_CALL LqFcheGetEmployedSize(LqFche* Che) {
	return Che->CurSize; 
}

LQ_EXTERN_C size_t LQ_CALL LqFcheGetMaxSize(LqFche* Che) {
	return Che->MaxSizeBuff;
}

LQ_EXTERN_C void LQ_CALL LqFcheSetMaxSize(LqFche* Che, size_t NewSize) {
	__LqCachedFile *i, *t;
	LqAtmLkWr(Che->Locker);
	if(NewSize >= Che->MaxSizeBuff) {
		Che->MaxSizeBuff = NewSize;
		LqAtmUlkWr(Che->Locker);
		return;
	}
	for(i = Che->CachedListEnd; i != &Che->RatingList; ) {
		if(Che->CurSize <= NewSize) 
			break;
		t = i->Next;
		_LqFcheRemoveFile(i);
		i = t;
	}
	Che->MaxSizeBuff = NewSize;
	LqAtmUlkWr(Che->Locker);
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqFcheGetPeriodUpdateStat(LqFche* Che) {
	return Che->PeriodUpdateStatMillisec; 
}

LQ_EXTERN_C void LQ_CALL LqFcheSetPeriodUpdateStat(LqFche* Che, LqTimeMillisec NewPeriodMillisec) {
	LqAtmLkWr(Che->Locker);
	Che->PeriodUpdateStatMillisec = NewPeriodMillisec;
	LqAtmUlkWr(Che->Locker);
}

LQ_EXTERN_C float LQ_CALL LqFcheGetPerSecReadAdding(LqFche* Che) {
	return Che->PerSecReadAdding;
}

LQ_EXTERN_C void LQ_CALL LqFcheSetPerSecReadAdding(LqFche* Che, float NewPeriod){
	LqAtmLkWr(Che->Locker);
	Che->PerSecReadAdding = NewPeriod;
	LqAtmUlkWr(Che->Locker);
}

LQ_EXTERN_C size_t LQ_CALL LqFcheGetMaxCountOfPrepared(LqFche* Che) {
	return Che->MaxInPreparedList; 
}

LQ_EXTERN_C void LQ_CALL LqFcheSetMaxCountOfPrepared(LqFche* Che, size_t NewPeriod) {
	LqAtmLkWr(Che->Locker);
	Che->MaxInPreparedList = NewPeriod;
	LqAtmUlkWr(Che->Locker);
}



#define __METHOD_DECLS__
#include "LqAlloc.hpp"