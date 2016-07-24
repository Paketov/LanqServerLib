#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFileChe - File cache for optimized send content.
*   While cache is load file in RAM another thread's not locket.
*/

#include "LqOs.h"
#include "LqAlloc.hpp"
#include "LqHashTable.hpp"
#include "LqLock.hpp"
#include "LqDef.hpp"
#include "LqTime.h"
#include "LqFile.h"
#include "LqStr.h"

#include <stdint.h>
#undef max

#if defined(_DEBUG)
#define CHECK_RATING_LIST CheckRatingList()
#else
#define CHECK_RATING_LIST
#endif


/*
CACHE_INFO must have methods:
void Cache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime)
void Uncache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime)
*/

#pragma pack(push)
#pragma pack(LQCACHE_ALIGN_FAST)

struct DefaultCacheData
{
	void Cache(const char* Path, void* Buf, size_t SizeBuf, LqTimeSec LastModifTime, const LqFileStat* Stat) const {}
	void Recache(const char* Path, void* Buf, size_t SizeBuf, LqTimeSec LastModifTime, const LqFileStat* Stat) const {}
	void Uncache(const char* Path, void* Buf, size_t SizeBuf, LqTimeSec LastModifTime) const {}
};


template<typename CachedHdr = DefaultCacheData>
class LqFileChe
{
public:
	typedef enum
	{
		OK,
		NOT_HAVE_FILE,
		NOT_ALLOC_MEM,
		FILE_OUT_OF_MAX_MEM_BOUND,
		FILE_NOT_HAVE_IN_CACHE,
		FILE_HAS_BEEN_RESIZED,
		NOT_FULL_READ_FROM_FILE
	} StatEnm;

	struct CachedFile;
private:

	typedef LqHashTable<CachedFile, size_t>			TableType;

	struct FileDescriptor
	{
		int	   Descriptor;

		inline FileDescriptor(): Descriptor(-1) {}
		inline ~FileDescriptor() 
		{ 
			if(Descriptor != -1) 
				LqFileClose(Descriptor);
		}

		bool Open(const char* Name, LqFileStat* s)
		{
			if(Descriptor != -1) 
				LqFileClose(Descriptor);
			if(((Descriptor = LqFileOpen(Name, LQ_O_RD | LQ_O_BIN, 0)) == -1) || (LqFileGetStatByFd(Descriptor, s) != 0))
				return false;
			return true;
		}
	};
public:
	struct CachedFile: public CachedHdr
	{
	private:
		friend LqFileChe;
		friend TableType;
		friend LqFastAlloc;
		friend CachedHdr;

		CachedFile()
		{
			CountReaded = 1;
			Path = nullptr;
			Buf = nullptr;
		}

		~CachedFile()
		{
			if(Buf != nullptr)
				___free(Buf);
			if(Path != nullptr) 
				free(Path);
		}

		bool SetKey(const char* Name)
		{
			Path = LqStrDuplicate(Name);
			size_t h = 0;
			for(const char* k = Path; *k != '\0'; k++)
				h = 31 * h + *k;
			PathHash = h;
			return true;
		}

		static size_t IndexByKey(const char* Key, size_t MaxCount)
		{
			size_t h = 0;
			for(const char* k = Key; *k != '\0'; k++)
				h = 31 * h + *k;
			return h % MaxCount;
		}
		static size_t IndexByKey(CachedFile* Key, size_t MaxCount) { return Key->PathHash % MaxCount; }
		bool CmpKey(CachedFile* Key) const { return (Key->PathHash == PathHash) && (LqStrSame(Path, Key->Path)); }
		size_t IndexInBound(size_t MaxCount) const { return PathHash % MaxCount; }
		bool CmpKey(const char* Key) const { return LqStrSame(Key, Path); }

		void RatingUp(LqFileChe* Cache)
		{
			auto n = Next;
			if(n != &Cache->RatingList)
			{
				auto p = Prev;
				n->Prev = p;
				Next = n->Next;
				Next->Prev = this;
				n->Next = this;
				Prev = n;
				p->Next = n;
			}
			if(Cache->CachedListEnd == &Cache->RatingList)
				Cache->CachedListEnd = this;
			else if(Cache->CachedListEnd == this)
				Cache->CachedListEnd = Prev;
		}

		void RemoveFromList(LqFileChe* Cache)
		{
			Next->Prev = Prev;
			Prev->Next = Next;
			if(Cache->CachedListEnd == this)
				Cache->CachedListEnd = Next;
			if(Buf == nullptr)
				Cache->CountInUncached--;
			else
				Cache->CurSize -= (SizeFile + sizeof(CachedFile));
		}

		bool UpdateInfo(LqFileStat* s)
		{
			if(LqFileGetStat(Path, s) != 0) 
				return false;
			SizeFile = s->Size;
			LastModifTime = s->ModifTime;
			return true;
		}

		void AddInList(LqFileChe* Cache)
		{
			Next = Cache->RatingList.Next;
			Next->Prev = Cache->RatingList.Next = this;
			Prev = &Cache->RatingList;
			Cache->CountInUncached++;
		}

		StatEnm DoCache(FileDescriptor* File, LqFileStat* Stat)
		{
			if((Buf = ___malloc((SizeFile == 0) ? 1 : SizeFile)) == nullptr) 
				return StatEnm::NOT_ALLOC_MEM;
			if(SizeFile > 0)
			{
				int Readed = LqFileRead(File->Descriptor, Buf, SizeFile);
				if(Readed < SizeFile) 
					return StatEnm::NOT_FULL_READ_FROM_FILE;
			}
			CachedHdr::Cache(Path, Buf, SizeFile, LastModifTime, Stat);
			return StatEnm::OK;
		}

		StatEnm Recache(FileDescriptor* File, LqFileStat* Stat)
		{
			void* NewPlace = ___realloc(Buf, (SizeFile == 0) ? 1 : SizeFile);
			if(NewPlace == nullptr) 
				return StatEnm::NOT_ALLOC_MEM;
			Buf = NewPlace;
			if(SizeFile > 0)
			{
				int Readed = LqFileRead(File->Descriptor, Buf, SizeFile);
				if(Readed < SizeFile) 
					return StatEnm::NOT_FULL_READ_FROM_FILE;
			}
			CachedHdr::Recache(Path, Buf, SizeFile, LastModifTime, Stat);
			return StatEnm::OK;
		}

		bool UpdateStat(LqTimeMillisec CurTimeMillisec, LqFileChe* Cache)
		{
			if((LastTestTime + Cache->PeriodUpdateStatMillisec) < CurTimeMillisec)
			{
				LastTestTime = CurTimeMillisec;
				CountReaded = 1;
				return true;
			}
			CountReaded++;
			return false;
		}

		CachedFile*			Next;
		CachedFile*			Prev;
		std::atomic<size_t>	CountRef;
		intptr_t			CountReaded;
		LqTimeMillisec		LastTestTime;
	public:
		float CountReadPerSec(LqTimeMillisec CurTimeMillisec) const
		{
			if(Path == nullptr) 
				return std::numeric_limits<float>::max();
			double TimePassed = (double)(CurTimeMillisec - LastTestTime);
			return (TimePassed < 100.0) ? std::numeric_limits<float>::max() : (CountReaded / TimePassed * 1000.0);
		}
		char*				Path;
		size_t				PathHash;
		void*				Buf;
		size_t				SizeFile;
		LqTimeSec			LastModifTime;
	private:
		LqLocker<uint8_t>	CachingLocker;
	};

private:

	CachedFile*			CachedListEnd;
	CachedFile			RatingList;
	TableType			Table;
	float				PerSecReadAdding;
	size_t				MaxInPreparedList;
	size_t				MaxSizeBuff;
	size_t				CurSize;
	size_t				MaxSizeFile;
	size_t				CountInUncached;
	LqTimeMillisec		PeriodUpdateStatMillisec;
	LqLocker<uintptr_t>	Locker;

	template<typename T>
	CachedFile* InsertInTable(T arg)
	{
		if(Table.IsFull()) Table.ResizeBeforeInsert((Table.Count() < 3) ? 3 : (typename TableType::IndexType)(Table.Count() * 1.61803398875f));
		return Table.Insert(arg);
	}

	template<typename T>
	void RemoveFromTable(T arg)
	{
		if(Table.RemoveRow(arg) != nullptr)
		{
			if((typename TableType::IndexType)(Table.Count() * 1.7f) < Table.AllocCount())
				Table.ResizeAfterRemove();
		}
	}

	bool AddFile(const char* Path, LqTimeMillisec CurTime, LqFileStat* s)
	{
		if(LqFileGetStat(Path, s) != 0) 
			return false;
		if((s->Size > MaxSizeFile) || (s->Size >= MaxSizeBuff)) 
			return false;
		decltype(InsertFile(Path)) NewFileReg;
		if(CountInUncached >= MaxInPreparedList)
		{
			if(
				((RatingList.Next->LastTestTime + 1000) >= CurTime) ||
				(RatingList.Next->CountReadPerSec(CurTime) >= PerSecReadAdding)
				)
				return false;
			RemoveFile(RatingList.Next);
			NewFileReg = InsertFile(Path);
		} else
		{
			NewFileReg = InsertFile(Path);
		}
		if(NewFileReg == nullptr) 
			return false;
		NewFileReg->SizeFile = s->Size;
		NewFileReg->LastModifTime = s->ModifTime;
		NewFileReg->LastTestTime = CurTime;
		NewFileReg->CountRef = 1;
		CHECK_RATING_LIST;
		return true;
	}

	bool BeforeRead(CachedFile* CachFile, LqTimeMillisec CurTime, LqFileStat* s)
	{
		float CachFileReadPerSec = CachFile->CountReadPerSec(CurTime);
		if(CachFile->Buf == nullptr)
		{
			if(
				(CachFile->Next != CachedListEnd) ||
				((CachFile->SizeFile + CurSize + sizeof(CachedFile)) >= MaxSizeBuff) &&
				((CachFile->Next->CountReadPerSec(CurTime) >= CachFileReadPerSec) ||
				(CachFileReadPerSec == std::numeric_limits<float>::max())) ||
				CachFile->Next->CachingLocker.IsLockWrite()
				)
				return true;

			CachFile->CachingLocker.LockWrite();
			Locker.UnlockWrite(); //Unlock all cache for longer time descriptor opening.

			FileDescriptor File;
			if(!File.Open(CachFile->Path, s))
			{

				Locker.LockWriteYield();
				CachFile->CachingLocker.UnlockWrite();
				RemoveFile(CachFile);
				return false;
			}

			Locker.LockWriteYield();   //Again lock for internal processing.
			CachFile->LastModifTime = s->ModifTime;
			if(((CachFile->SizeFile = s->Size) + CurSize + sizeof(CachedFile)) >= MaxSizeBuff)
			{
				if(!ClearPlaceForFirstInUncached(CurTime))
				{
					CachFile->CachingLocker.UnlockWrite();
					return true;
				}
			}
			CachFile->RatingUp(this);
			CurSize += (CachFile->SizeFile + sizeof(CachedFile));
			CountInUncached--;
			Locker.UnlockWrite();  //Unlock all cache for longer time reading.

			if(CachFile->DoCache(&File, s) != StatEnm::OK)
			{
				Locker.LockWriteYield();
				CachFile->CachingLocker.UnlockWrite();
				RemoveFile(CachFile);
				return false;
			}
			Locker.LockWriteYield(); //Again lock for internal processing.
			CachFile->CachingLocker.UnlockWrite();
			CHECK_RATING_LIST;
			return true;
		}
		if(!CachFile->Next->CachingLocker.IsLockWrite() && (CachFile->Next->CountReadPerSec(CurTime) < CachFileReadPerSec))
			CachFile->RatingUp(this);
		CHECK_RATING_LIST;
		return true;
	}

	bool ClearPlaceForFirstInUncached(ullong CurTime)
	{
		intptr_t CommonDelSize = MaxSizeBuff - CurSize;
		CachedFile * CachFile = CachedListEnd->Prev;
		float CachFileReadPerSec = CachFile->CountReadPerSec(CurTime);

		auto c = CachedListEnd;
		for(; c != &RatingList; c = c->Next)
		{
			if(CachFile->SizeFile <= CommonDelSize)
				break;
			if((c->CountRef <= 1) && (c->CountReadPerSec(CurTime) < CachFileReadPerSec))
				CommonDelSize += c->SizeFile;
			else
				break;
		}
		if(CachFile->SizeFile <= CommonDelSize)
		{
			for(CachedFile* i = CachedListEnd, *t; i != c; i = t)
			{
				if((i->CountRef <= 1) && (i->CountReadPerSec(CurTime) < CachFileReadPerSec))
				{
					t = i->Next;
					RemoveFile(i);
				} else
				{
					t = i->Next;
				}
			}
		} else
		{
			return false;
		}
		return true;
	}

	bool RemoveFile(CachedFile* CachFile)
	{
		RemoveFromTable(CachFile);
		if(CachFile->CountRef <= 1)
		{
			CachFile->CachedHdr::Uncache(CachFile->Path, CachFile->Buf, CachFile->SizeFile, CachFile->LastModifTime);
			CachFile->RemoveFromList(this);
			LqFastAlloc::Delete(TableType::GetCellByElement(CachFile));
			return true;
		} else
			CachFile->CountRef--;
		return false;
	}

	CachedFile* SearchFile(const char* Name)
	{
#ifdef _MSC_VER
		if((Name[0] == '\\') && (Name[1] == '\\') && (Name[2] == '?') && (Name[3] == '\\'))
			Name += 4;
		size_t l = LqStrLen(Name);
		if(l < 256)
		{
			char Buf[256];
			LqStrCopyMax(Buf, Name, sizeof(Buf));
			strlwr(Buf);
			return Table.Search(Buf);
		}//// !!!!!!!!!!!!! C:ddd
		char* SearchStr = LqStrDuplicate(Name);
		if(SearchStr == nullptr) 
			return Table.Search(Name);
		strlwr(SearchStr);
		auto r = Table.Search(SearchStr);
		free(SearchStr);
		return r;
#else
		return Table.Search(Name);
#endif
	}

	CachedFile* InsertFile(const char* Name)
	{
#ifdef _MSC_VER
		if((Name[0] == '\\') && (Name[1] == '\\') && (Name[2] == '?') && (Name[3] == '\\'))
			Name += 4;
		size_t l = LqStrLen(Name);
		if(l < 256)
		{
			char Buf[256];
			LqStrCopyMax(Buf, Name, sizeof(Buf));
			strlwr(Buf);
			auto NewFileReg = InsertInTable(Buf);
			if(NewFileReg != nullptr) 
				NewFileReg->AddInList(this);
			return NewFileReg;
		}
		char* SearchStr = LqStrDuplicate(Name);
		if(SearchStr == nullptr) 
			return nullptr;
		strlwr(SearchStr);
		auto NewFileReg = InsertInTable(SearchStr);
		if(NewFileReg != nullptr) 
			NewFileReg->AddInList(this);
		free(SearchStr);
		return NewFileReg;
#else
		auto NewFileReg = InsertInTable(Name);
		if(NewFileReg != nullptr) 
			NewFileReg->AddInList(this);
		return NewFileReg;
#endif
	}

	bool UpdateFile(CachedFile* CachFile, LqFileStat* s)
	{
		if(CachFile->Buf == nullptr)
		{
			if(!CachFile->UpdateInfo(s))
				RemoveFile(CachFile);
		} else
		{
			CachFile->CachingLocker.LockWrite(); //Lock only file
			Locker.UnlockWrite(); //Unlock all cache for longer time descriptor opening.
			FileDescriptor File;
			if(!File.Open(CachFile->Path, s))
			{
				Locker.LockWriteYield();
				CachFile->CachingLocker.UnlockWrite();
				RemoveFile(CachFile);
			} else if((CachFile->LastModifTime != s->ModifTime) || (CachFile->SizeFile != s->Size))
			{
				intptr_t Diffr = s->Size - CachFile->SizeFile;
				CachFile->SizeFile = s->Size;
				CachFile->LastModifTime = s->ModifTime;

				if(((Diffr + CurSize) >= MaxSizeBuff) || (s->Size > MaxSizeFile) || !((CachFile->CountRef == 1) && (CachFile->Recache(&File, s) == StatEnm::OK)))
				{
					Locker.LockWriteYield(); //Lock again for internal operations.
					CachFile->CachingLocker.UnlockWrite();
					RemoveFile(CachFile);
				} else
				{
					Locker.LockWriteYield();
					CurSize += Diffr;
					CachFile->CachingLocker.UnlockWrite();
					return true;
				}
			} else
			{
				Locker.LockWriteYield(); //Lock again for internal operations.
				CachFile->CachingLocker.UnlockWrite();
				return true;
			}
		}
		return false;
	}

	bool CheckRatingList()
	{
		for(auto i = CachedListEnd; i != &RatingList; i = i->Next)
		{
			if(i->Buf == nullptr)
			{
				throw "Rating list has been corrupted!";
			}
		}
		for(auto i = CachedListEnd->Prev; i != &RatingList; i = i->Prev)
		{
			if(i->Buf != nullptr)
			{
				throw "Rating list has been corrupted!";
			}
		}
		return true;
	}

	inline void AssignCachedFile(CachedFile* f) { f->CountRef++; }

	bool WaitCachedFile(CachedFile* f)
	{
		if(f->CachingLocker.IsLockWrite())
		{
			AssignCachedFile(f);
			Locker.UnlockWrite();
			f->CachingLocker.LockWriteYield();
			Locker.LockWriteYield();
			f->CachingLocker.UnlockWrite();
			RemoveFile(f);
			return true;
		}
		return false;
	}
public:

	LqFileChe()
	{
		CachedListEnd = &RatingList;
		CurSize = 0;
		MaxSizeBuff = 1024 * 1024 * 100; //100 mb
		MaxInPreparedList = 30;
		CountInUncached = 0;
		PeriodUpdateStatMillisec = 5 * 1000; //5 sec
		PerSecReadAdding = 1.f;
		MaxSizeFile = 70 * 1024 * 1024; // 70mb
		RatingList.SizeFile = 0;
		RatingList.CountReaded = 1;
		RatingList.PathHash = 0;
		RatingList.LastTestTime = LqTimeGetMaxMillisec();
		RatingList.Prev = RatingList.Next = &RatingList;
	}

	~LqFileChe()
	{
		Clear();
		if(!((RatingList.Prev == RatingList.Next) && (RatingList.Prev == &RatingList)))
			throw "class LqFileChe: Cache have file with ex. ref!";
	}

	inline size_t GetMaxSizeFile() const { return MaxSizeFile; }
	inline void SetMaxSizeFile(size_t NewSize)
	{
		Locker.LockWriteYield();
		if(MaxSizeFile >= NewSize)
		{
			MaxSizeFile = NewSize;
			Locker.UnlockWrite();
			return;
		}
		for(auto i = CachedListEnd; i != &RatingList; )
		{
			auto t = i->Next;
			if(i->SizeFile > NewSize)
				RemoveFile(i);
			i = t;
		}
		MaxSizeFile = NewSize;
		Locker.UnlockWrite();
	}
	size_t GetEmployedSize() const { return CurSize; }
	size_t GetMaxSize() const { return MaxSizeBuff; }
	void SetMaxSize(size_t NewSize)
	{
		Locker.LockWriteYield();
		if(NewSize >= MaxSizeBuff)
		{
			MaxSizeBuff = NewSize;
			Locker.UnlockWrite();
			return;
		}
		for(auto i = CachedListEnd; i != &RatingList; )
		{
			if(CurSize <= NewSize) break;
			auto t = i->Next;
			RemoveFile(i);
			i = t;
		}
		MaxSizeBuff = NewSize;
		Locker.UnlockWrite();
	}

	LqTimeMillisec GetPeriodUpdateStat() const { return PeriodUpdateStatMillisec; }
	void SetPeriodUpdateStat(LqTimeMillisec NewPeriodMillisec)
	{
		Locker.LockWriteYield();
		PeriodUpdateStatMillisec = NewPeriodMillisec;
		Locker.UnlockWrite();
	}

	float GetPerSecReadAdding() const { return PerSecReadAdding; }
	void SetPerSecReadAdding(float NewPeriod) { PerSecReadAdding = NewPeriod; }

	size_t GetMaxCountOfPrepared() const { return MaxInPreparedList; }
	void SetMaxCountOfPrepared(size_t NewPeriod) { MaxInPreparedList = NewPeriod; }

	/*
	*Get pointer on cached file.
	* @Path: Target file path.
	* @Stat: Used for optimise. If you need file statistics, transfer pointer.
	* @return: pointer on target cached file.
	*/
	CachedFile* Read(const char* Path, LqFileStat* Stat = nullptr)
	{
		Locker.LockWriteYield();
		auto r = SearchFile(Path);
		auto CurTime = LqTimeGetLocMillisec();
		LqFileStat LocalStat;
		if(Stat == nullptr) 
			Stat = &LocalStat;
		if(r == nullptr)
		{
			AddFile(Path, CurTime, Stat);
		} else
		{
			r->UpdateStat(CurTime, this);
			r->CountReaded++;
			if(r->CachingLocker.IsLockWrite())
			{
				Locker.UnlockWrite();
				return nullptr;
			}
			if(BeforeRead(r, CurTime, Stat))
			{
				if(r->Buf != nullptr)
				{
					AssignCachedFile(r);
					Locker.UnlockWrite();
					return r;
				}
			}
		}
		Locker.UnlockWrite();
		return nullptr;
	}

	/*
	*Update file and get pointer on his.
	* @Path: Target file path.
	* @Stat: Used for optimise. If you need file statistics, transfer pointer.
	* @return: pointer on target cached file.
	*/
	CachedFile* UpdateAndRead(const char* Path, LqFileStat* Stat = nullptr)
	{
		Locker.LockWriteYield();
		auto r = SearchFile(Path);
		auto CurTime = LqTimeGetLocMillisec();
		LqFileStat LocalStat;
		if(Stat == nullptr) 
			Stat = &LocalStat;
		if(r == nullptr)
		{
			AddFile(Path, CurTime, Stat);
		} else
		{
			auto UpdateStatRes = r->UpdateStat(CurTime, this);
			r->CountReaded++;
			if(r->CachingLocker.IsLockWrite())
			{
				Locker.UnlockWrite();
				return nullptr;
			}
			if(UpdateStatRes && (r->Buf != nullptr))
			{
				if(!UpdateFile(r, Stat))
				{
					Locker.UnlockWrite();
					return nullptr;
				}
				float CachFileReadPerSec = r->CountReadPerSec(CurTime);
				if(!r->Next->CachingLocker.IsLockWrite() && (r->Next->CountReadPerSec(CurTime) < CachFileReadPerSec))
					r->RatingUp(this);
				AssignCachedFile(r);
				Locker.UnlockWrite();
				return r;
			} else if(BeforeRead(r, CurTime, Stat))
			{
				if(r->Buf != nullptr)
				{
					AssignCachedFile(r);
					Locker.UnlockWrite();
					return r;
				}
			}
		}
		Locker.UnlockWrite();
		return nullptr;
	}

	/*
	*Decrements count ref on cached file.
	* @FileInRam: pointer on target file.
	* @return: true - is file removed from RAM, false - is only decrement count ref.
	*/
	bool Release(CachedFile* FileInRam)
	{
		if(FileInRam->CountRef <= 1)
		{
			Locker.LockWriteYield();
			FileInRam->CachedHdr::Uncache(FileInRam->Path, FileInRam->Buf, FileInRam->SizeFile, FileInRam->LastModifTime);
			FileInRam->RemoveFromList(this);
			LqFastAlloc::Delete(TableType::GetCellByElement(FileInRam));
			Locker.UnlockWrite();
			return true;
		} else
		{
			FileInRam->CountRef--;
		}
		return false;
	}

	/*
	*Upadetes all cached files.
	*/
	void UpdateAllCache()
	{
		LqFileStat LocalStat;
		Locker.LockWriteYield();
		for(auto i = RatingList.Next; i != &RatingList; )
		{
			auto t = i->Next;
			if(!i->CachingLocker.IsLockWrite())
				UpdateFile(i, &LocalStat);
			i = t;
		}
		CHECK_RATING_LIST;
		Locker.UnlockWrite();
	}

	/*
	*Updates only one cached file.
	* @Path: Target file path.
	* @Stat: Used for optimise. If you need file statistics, transfer pointer.
	* @return: -1 - if file not found, 0 - file updated, 1 - file removed.
	*/
	int Update(const char* Path, LqFileStat* Stat = nullptr)
	{
		LqFileStat LocalStat;
		if(Stat == nullptr)
			Stat = &LocalStat;
		Locker.LockWriteYield();
		int r = -1;
		if(auto f = SearchFile(Path))
		{
			if(!f->CachingLocker.IsLockWrite())
				r = (UpdateFile(f, Stat)) ? 0 : 1;
			else
				r = 0;
		}
		CHECK_RATING_LIST;
		Locker.UnlockWrite();
		return r;
	}

	/*
	*Removes all file from RAM. If file have external refs, not remove.
	* @return: count removed file from RAM.
	*/
	size_t Clear()
	{
		Locker.LockWriteYield();
		size_t CountFree = 0;
		for(auto i = RatingList.Next; i != &RatingList; )
		{
			auto t = i->Next;
			if(WaitCachedFile(i))
			{
				i = RatingList.Next;
				continue;
			}
			if(RemoveFile(i))
				CountFree++;
			i = t;
		}
		CHECK_RATING_LIST;
		Locker.UnlockWrite();
		return CountFree;
	}

	/*
	*Removes target file from RAM.
	* @Path: Target file path
	* @return: true - is file removed from RAM, false - is file not found or file have references.
	*/
	bool Uncache(const char* Path)
	{
		Locker.LockWriteYield();
		bool IsFree = false;
		if(auto f = SearchFile(Path))
		{
			WaitCachedFile(f);
			IsFree = RemoveFile(f);
		}
		CHECK_RATING_LIST;
		Locker.UnlockWrite();
		return IsFree;
	}
};


#pragma pack(pop)
