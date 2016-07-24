/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpMdl... - Work with modules.
*
*		+-------------------------------------------------+
*		|					LqHttpProto					  |
*		+-------------------------------------------------+
*			|		|						!
*			|		 \						!
*			/		  \					+=========+
*		   \/		  \/				|  Domen  |
*		+--------+   +--------+		  / +=========+
*		| Module |   | Module |		 /
*		+--------+   +--------+		/
*		   |	|			|	   /
*		   |	|			|	  /
*		   |	\			|	 /
*		   /	 \			/	/
*		  \/     \/		   \/  \/
*		+----+	+----+     +----+
*		|Path|	|Path|     |Path|
*		+----+  +----+     +----+
*
*/

#include "LqHttpMdl.h"
#include "LqLock.hpp"
#include "LqHttp.h"
#include "LqLib.h"
#include "LqHttpPth.hpp"
#include "LqHttp.hpp"
#include "LqStrSwitch.h"
#include "LqErr.h"
#include "LqAtm.hpp"
#include "LqHttpMdlHandlers.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

thread_local volatile size_t lkpl = 0;

static void LqHttpMdlPathUnregister(LqHttpPathListHdr* l);

inline static void LqHttpMdlLockWrite(LqHttpMdl* Module) { if(lkpl == 0) LqAtmLkWr(Module->PathListLocker); }
inline static void LqHttpMdlUnlockWrite(LqHttpMdl* Module) { if(lkpl == 0) LqAtmUlkWr(Module->PathListLocker); }
inline static void LqHttpMdlListLockWrite(LqHttpProtoBase* Reg) { if(lkpl == 0) ((LqHttpProto*)Reg)->ModuleListLocker.LockWrite(); }
inline static void LqHttpMdlListUnlockWrite(LqHttpProtoBase* Reg) { if(lkpl == 0) ((LqHttpProto*)Reg)->ModuleListLocker.UnlockWrite(); }
inline static void LqHttpMdlListLockRead(LqHttpProtoBase* Reg) { if(lkpl == 0) ((LqHttpProto*)Reg)->ModuleListLocker.LockRead(); }
inline static void LqHttpMdlListUnlockRead(LqHttpProtoBase* Reg) { if(lkpl == 0) ((LqHttpProto*)Reg)->ModuleListLocker.UnlockRead(); }
inline static void LqHttpMdlUnlockForThisThread() { lkpl++; }
inline static void LqHttpMdlLockForThisThread() { lkpl--; }


static void LqHttpMdlModuleRemoveFromReg(LqHttpMdl* Module)
{
	LqHttpMdlListLockWrite(Module->Proto);
	Module->Next->Prev = Module->Prev;
	Module->Prev->Next = Module->Next;
	Module->Proto->CountModules--;
	LqHttpMdlListUnlockWrite(Module->Proto);
	//====
	if(auto HandleModule = Module->FreeModuleNotifyProc(Module))
		LqLibFree(HandleModule);
}

void LqHttpMdlPathFree(LqHttpPth* Pth)
{
	auto Module = Pth->ParentModule;
	Module->DeletePathProc(Pth);
	LqHttpMdlPathUnregister((LqHttpPathListHdr*)Pth);
	if(Module->IsFree && (Module->CountPointers == 0))
	{
		LqHttpMdlModuleRemoveFromReg(Module);
	}
}

void LqHttpMdlPathRegister(LqHttpMdl* Module, LqHttpPathListHdr* l)
{
	LqHttpMdlLockWrite(Module);
	l->Prev = Module->StartPathList.Prev;
	Module->StartPathList.Prev = l;
	l->Prev->Next = l;
	l->Next = &Module->StartPathList;
	Module->CountPointers++;
	LqHttpMdlUnlockWrite(Module);
}

static void LqHttpMdlPathUnregister(LqHttpPathListHdr* l)
{
	auto m = l->Path.ParentModule;
	LqHttpMdlLockWrite(m);
	l->Next->Prev = l->Prev;
	l->Prev->Next = l->Next;
	m->CountPointers--;
	LqHttpMdlUnlockWrite(m);
}


static void LqHttpMdlFree_Native(LqHttpMdl* Module)
{
	auto& FileSystem = ((LqHttpProto*)Module->Proto)->FileSystem;
	LqHttpMdlLockWrite(Module);
	Module->CountPointers++;
	FileSystem.l.LockWriteYield();
	LqHttpMdlUnlockForThisThread();

	for(auto i = Module->StartPathList.Next; i != &Module->StartPathList; )
	{
		auto t = i->Next;
		FileSystem.t.EnumValues
		(
			[](void* UserData, LqHttpDomainPaths* Element)
			{
				auto r = true;
				LqHttpPth* WebPath = (LqHttpPth*)UserData;
				auto v = Element->t.Search(WebPath);
				if((v != nullptr) && (v->p == WebPath))
				{
					auto v = Element->t.RemoveRetPointer(WebPath);
					r = !LqHttpPthRelease(v->p);
					v->p = nullptr;
					Element->t.DeleteRetPointer(v);
				}
				return r;
			},
			&i->Path
		);
		i = t;
	}
	LqHttpMdlLockForThisThread();
	FileSystem.t.EnumValues
	(
		[](LqHttpDomainPaths* Element)
		{
			if((size_t)(Element->t.Count() * 1.7f) < Element->t.AllocCount())
				Element->t.ResizeAfterRemove();
			return true;
		}
	);
	FileSystem.l.UnlockWrite();
	Module->CountPointers--;
}


static void LqHttpMdlEnmFree(LqHttpMdl* Module)
{
	bool Expected = false;
	if(LqAtmCmpXchg(Module->IsFree, Expected, true))
	{
		LqHttpMdlFree_Native(Module);
		if(Module->CountPointers == 0)
		{
			Module->Next->Prev = Module->Prev;
			Module->Prev->Next = Module->Next;
			((LqHttpProto*)Module->Proto)->Base.CountModules--;
			if(auto HandleModule = Module->FreeModuleNotifyProc(Module))
				LqLibFree(HandleModule);
		} else
		{
			LqHttpMdlUnlockWrite(Module);
		}
	}
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlFreeByName(LqHttpProtoBase* Reg, const char* NameModule, bool IsAll)
{
	LqHttpMdlListLockWrite(Reg);
	auto StartMdl = &Reg->StartModule;
	int Res = -1;

	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		if(LqStrSame(i->Name, NameModule))
		{
			Res = 1;
			LqHttpMdlEnmFree(i);
			if(!IsAll)
				break;
		}
	}
	LqHttpMdlListUnlockWrite(Reg);
	return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlFreeAll(LqHttpProtoBase* Reg)
{
	LqHttpMdlListLockWrite(Reg);
	auto StartMdl = &Reg->StartModule;
	int Res = 0;

	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		Res++;
		LqHttpMdlEnmFree(i);
	}
	LqHttpMdlListUnlockWrite(Reg);
	return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlFreeByHandle(LqHttpProtoBase* Reg, uintptr_t Handle)
{
	LqHttpMdlListLockWrite(Reg);
	auto StartMdl = &Reg->StartModule;
	int Res = -1;

	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		if(i->Handle == Handle)
		{
			Res = 1;
			LqHttpMdlEnmFree(i);
			break;
		}
	}
	LqHttpMdlListUnlockWrite(Reg);
	return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpMdlSendCommandByHandle(LqHttpProtoBase* Reg, uintptr_t Handle, const char* Command, void* Data)
{
	LqHttpMdlListLockWrite(Reg);
	auto StartMdl = &Reg->StartModule;
	LqHttpMdl *Mdl = nullptr;
	bool Res = false;
	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		if(i->Handle == Handle)
		{
			Mdl = i;
			break;
		}
	}
	if(Mdl != nullptr)
	{
		LqHttpMdlUnlockForThisThread();
		Mdl->ReciveCommandProc(Mdl, Command, Data);
		LqHttpMdlLockForThisThread();
		Res = true;
	}
	LqHttpMdlListUnlockWrite(Reg);
	return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpMdlSendCommandByName(LqHttpProtoBase* Reg, const char* Name, const char* Command, void* Data)
{
	LqHttpMdlListLockWrite(Reg);
	auto StartMdl = &Reg->StartModule;
	LqHttpMdl *Mdl = nullptr;
	bool Res = false;
	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		if(LqStrSame(i->Name, Name))
		{
			Mdl = i;
			break;
		}
	}
	if(Mdl != nullptr)
	{
		LqHttpMdlUnlockForThisThread();
		Mdl->ReciveCommandProc(Mdl, Command, Data);
		LqHttpMdlLockForThisThread();
		Res = true;
	}
	LqHttpMdlListUnlockWrite(Reg);
	return Res;
}


LQ_EXTERN_C int LQ_CALL LqHttpMdlIsHave(LqHttpProtoBase* Reg, uintptr_t Handle)
{
	LqHttpMdlListLockRead(Reg);
	auto StartMdl = &Reg->StartModule;
	int Res = -1;

	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		if(i->Handle == Handle)
		{
			Res = 1;
			break;
		}
	}
	LqHttpMdlListUnlockRead(Reg);
	return Res;
}


LQ_EXTERN_C bool LQ_CALL LqHttpMdlEnm(LqHttpProtoBase* Reg, uintptr_t* ModuleHandle, char* Name, size_t NameLen, bool* IsFree)
{
	LqHttpMdlListLockRead(Reg);
	auto StartMdl = &Reg->StartModule;
	int Res = -1;
	if((*ModuleHandle == 0) && (StartMdl->Next != StartMdl))
	{
		*ModuleHandle = StartMdl->Next->Handle;
		if(Name != nullptr)
			LqStrCopyMax(Name, StartMdl->Next->Name, NameLen);
		if(IsFree != nullptr)
			*IsFree = StartMdl->Next->IsFree;
		LqHttpMdlListUnlockRead(Reg);
		return true;
	}
	for(auto i = StartMdl; i->Next != StartMdl; i = i->Next)
	{
		if(i->Handle == *ModuleHandle)
		{
			*ModuleHandle = i->Next->Handle;
			if(Name != nullptr)
				LqStrCopyMax(Name, i->Next->Name, NameLen);
			if(IsFree != nullptr)
				*IsFree = i->Next->IsFree;
			LqHttpMdlListUnlockRead(Reg);
			return true;
		}
	}
	LqHttpMdlListUnlockRead(Reg);
	return false;
}


LQ_EXTERN_C bool LQ_CALL LqHttpMdlFree(LqHttpMdl* Module)
{
	if(&Module->Proto->StartModule == Module)
		return false;
	bool Expected = false;
	if(LqAtmCmpXchg(Module->IsFree, Expected, true))
	{
		LqHttpMdlFree_Native(Module);
		if(Module->CountPointers == 0)
			LqHttpMdlModuleRemoveFromReg(Module);
		else
			LqHttpMdlUnlockWrite(Module);
		return true;
	}
	return false;
}

LQ_EXTERN_C void LQ_CALL LqHttpMdlInit(LqHttpProtoBase* Reg, LqHttpMdl* Module, const char* Name, uintptr_t Handle)
{
	Module->CountPointers = 0;
	LqAtmLkInit(Module->PathListLocker);

	struct Procs
	{
		static uintptr_t LQ_CALL FreeModuleNotifyProc(LqHttpMdl* Module) { return Module->Handle; }
		static void LQ_CALL DelCreatePathProc(LqHttpPth*) {}
		static bool LQ_CALL RegisterPathInDomenProc(LqHttpPth*, const char*) { return true; }
		static void LQ_CALL UnregisterPathFromDomenProc(LqHttpPth* , const char*) {}
		static void LQ_CALL ReciveCommandProc(LqHttpMdl*, const char*, void*) {};
	};

	Module->Handle = Handle;
	Module->FreeModuleNotifyProc = Procs::FreeModuleNotifyProc;
	Module->CreatePathProc = Module->DeletePathProc = Procs::DelCreatePathProc;
	Module->RegisterPathInDomenProc = Procs::RegisterPathInDomenProc;
	Module->UnregisterPathFromDomenProc = Procs::UnregisterPathFromDomenProc;
	Module->GetCacheInfoProc = LqHttpMdlHandlersCacheInfo;
	Module->GetMimeProc = LqHttpMdlHandlersMime;
	Module->RspErrorProc = LqHttpMdlHandlersError;
	Module->ServerNameProc = LqHttpMdlHandlersServerName;
	Module->GetActEvntHandlerProc = LqHttpMdlHandlersGetMethod;
	Module->ReciveCommandProc = Procs::ReciveCommandProc;
	Module->AllowProc = LqHttpMdlHandlersAllowMethods;
	Module->NonceProc = LqHttpMdlHandlersNonce;
	Module->ResponseRedirectionProc = LqHttpMdlHandlersResponseRedirection;
	Module->RspStatusProc = LqHttpMdlHandlersStatus;
	Module->IsFree = false;
	Module->Proto = Reg;
	Module->UserData = 0;
	Module->StartPathList.Next = Module->StartPathList.Prev = &Module->StartPathList;
	if(Name != nullptr)
		Module->Name = LqStrDuplicate(Name);
	LqHttpProto* HttpProto = (LqHttpProto*)Reg;

	//Adding module in reg list
	LqHttpMdlListLockWrite(Reg);
	if(Module == &Reg->StartModule)
	{
		Module->Next = Module->Prev = Module;
	} else
	{
		Module->Next = Reg->StartModule.Next;
		Module->Next->Prev = Module;
		Reg->StartModule.Next = Module;
		Module->Prev = &Reg->StartModule;
	}
	Reg->CountModules++;
	LqHttpMdlListUnlockWrite(Reg);
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlGetNameByHandle(LqHttpProtoBase* Reg, uintptr_t Handle, char* NameDest, size_t NameDestSize)
{
	LqHttpMdlListLockRead(Reg);
	auto StartMdl = &Reg->StartModule;
	int Res = -1;

	for(auto i = StartMdl->Next, t = i->Next; i != StartMdl; i = t)
	{
		t = i->Next;
		if(i->Handle == Handle)
		{
			LqStrCopyMax(NameDest, i->Name, NameDestSize);
			Res = 1;
			break;
		}
	}
	LqHttpMdlListUnlockRead(Reg);
	return Res;

}

LQ_EXTERN_C LqHttpMdlLoadEnm LQ_CALL LqHttpMdlLoad(LqHttpProtoBase* Reg, const char* PathToLib, void* UserData, uintptr_t* Handle)
{
	auto LibHandle = LqLibLoad(PathToLib);
	if(LibHandle == 0)
		return LQHTTPMDL_LOAD_FAIL;
	if(LqHttpMdlIsHave(Reg, LibHandle) > -1)
		return LQHTTPMDL_LOAD_ALREADY_HAVE;
	auto MuduleProc = (LqHttpModuleRegistratorProc)LqLibGetProc(LibHandle, LQ_MOD_REGISTARTOR_NAME);
	if(MuduleProc == NULL)
	{
		LqLibFree(LibHandle);
		return LQHTTPMDL_LOAD_PROC_NOT_FOUND;
	}
	auto r = MuduleProc(Reg, (uintptr_t)LibHandle, PathToLib, UserData);
	switch(r)
	{
		case LQHTTPMDL_REG_FREE_LIB:
			LqLibFree(LibHandle);
			return LQHTTPMDL_LOAD_INDEPENDENTLY_UNLOADED;
	}

	if(Handle != nullptr)
		*Handle = LibHandle;

	return LQHTTPMDL_LOAD_OK;
}

