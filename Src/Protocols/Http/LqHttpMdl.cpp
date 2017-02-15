/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpMdl... - Work with modules.
*
*               +-------------------------------------------------+
*               |                       LqHttpProto               |
*               +-------------------------------------------------+
*                       |       |                    !
*                       |        \                   !
*                       /         \             +=========+
*                      \/         \/            |  Domen  |
*               +--------+   +--------+       / +=========+
*               | Module |   | Module |      /
*               +--------+   +--------+     /
*                  |    |       |          /
*                  |    |       |         /
*                  |    \       |        /
*                  /     \     /        /
*                 \/     \/  \/        \/
*               +----+  +----+     +----+
*               |Path|  |Path|     |Path|
*               +----+  +----+     +----+
*
*/

#include "LqConn.h"
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
#include "LqPtdArr.hpp"
#include "LqHttpPth.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"


void __LqHttpMdlDelete(LqHttpMdl* Val) {
    if(auto HandleModule = Val->FreeNotifyProc(Val))
        LqLibFree(HandleModule);
}

void LqHttpMdlPathFree(LqHttpPth* Pth) {
    auto Module = Pth->ParentModule;
    auto& PathsArr = *(LqHttpPthArr*)Module->_Paths;
    Module->DeletePathProc(Pth);
    PathsArr.remove_by_val(Pth);
    LqObPtrDereference<LqHttpMdl, __LqHttpMdlDelete>(Module);
}

void LqHttpMdlPathRegister(LqHttpMdl* Module, LqHttpPth* l) {
    auto& PathsArr = *(LqHttpPthArr*)Module->_Paths;
    PathsArr.push_back_uniq(l);
    LqObPtrReference(Module);
}

static void _LqHttpMdlDeletePathsFromFs(LqHttpMdl* Module, bool IsFreeRelations) {
    LqObPtrReference(Module);
    auto& PathsArr = *(LqHttpPthArr*)Module->_Paths;
    auto& Dmns = ((LqHttpData*)((LqHttp*)Module->HttpAcceptor)->UserData)->Dmns;
    PathsArr.clear();
    for(auto d = Dmns.begin(); !d.is_end(); ++d) {
        (*d)->Pths.remove_mult_by_compare_fn([&](LqHttpPthPtr& Ptr) {
            return Ptr->ParentModule == Module;
        });
    }
    LqObPtrDereference<LqHttpMdl, __LqHttpMdlDelete>(Module);
}

static void LqHttpMdlEnmFree(LqHttpMdl* Module) {
    Module->IsFree = true;
    Module->BeforeFreeNotifyProc(Module);
    _LqHttpMdlDeletePathsFromFs(Module, true);
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlFreeByName(LqHttp* Http, const char* NameModule, bool IsAll) {
    auto HttpData = ((LqHttpData*)(Http)->UserData);
    int Res = 0;
    for(auto& i : HttpData->Modules) {
        if(LqStrSame(i->Name, NameModule)) {
            Res++;
            LqHttpMdlEnmFree(i.Get());
			HttpData->Modules.remove_by_val(i);
            if(!IsAll)
                break;
        }
    }
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlFreeAll(LqHttp* Http) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    LqPtdArr<LqHttpMdlPtr> CurList;
    CurList.swap(HttpData->Modules);
    int Res = 0;
    for(auto& i : CurList) {
        Res++;
        LqHttpMdlEnmFree(i.Get());
		HttpData->Modules.remove_by_val(i);
    }
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqHttpMdlFreeMain(LqHttp* Http) {
    LqHttpMdlEnmFree(&((LqHttpData*)(Http)->UserData)->StartModule);
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlFreeByHandle(LqHttp* Http, uintptr_t Handle) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    for(auto& i : HttpData->Modules) {
        if(i->Handle == Handle) {
            LqHttpMdlEnmFree(i.Get());
			HttpData->Modules.remove_by_val(i);
            return 1;
        }
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlSendCommandByHandle(LqHttp* Http, uintptr_t Handle, const char* Command, void* Data) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    for(auto& i : HttpData->Modules) {
        if(i->Handle == Handle) {
            i->ReciveCommandProc(i.Get(), Command, Data);
            return 1;
        }
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlSendCommandByName(LqHttp* Http, const char* Name, const char* Command, void* Data) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    for(auto& i : HttpData->Modules) {
        if(LqStrSame(i->Name, Name)) {
            i->ReciveCommandProc(i.Get(), Command, Data);
            return 1;
        }
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlIsHave(LqHttp* Http, uintptr_t Handle) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    for(auto& i : HttpData->Modules) {
        if(i->Handle == Handle)
            return 1;
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlEnm(LqHttp* Http, uintptr_t* ModuleHandle, char* Name, size_t NameLen, bool* IsFree) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    if(*ModuleHandle == 0) {
        auto i = HttpData->Modules.begin();
        if(i.is_end())
            return -1;
        if((*i)->Handle == 0) {
            ++i;
            if(i.is_end())
                return -1;
        }
        *ModuleHandle = (*i)->Handle;
        if(Name != nullptr)
            LqStrCopyMax(Name, (*i)->Name, NameLen);
        if(IsFree != nullptr)
            *IsFree = (*i)->IsFree;
        return 0;
    }
    for(auto i = HttpData->Modules.begin(); !i.is_end(); i++) {
        if(((*i)->Handle != 0) && ((*i)->Handle == *ModuleHandle) && !(i += 1).is_end()) {
            *ModuleHandle = (*i)->Handle;
            if(Name != nullptr)
                LqStrCopyMax(Name, (*i)->Name, NameLen);
            if(IsFree != nullptr)
                *IsFree = (*i)->IsFree;
            return 0;
        }
    }
    return -1;
}

LQ_EXTERN_C void LQ_CALL LqHttpMdlInit(LqHttp* HttpAcceptor, LqHttpMdl* Module, const char* Name, uintptr_t Handle) {
    Module->CountPointers = 0;

    struct Procs {
        static uintptr_t LQ_CALL FreeNotifyProc(LqHttpMdl* Module) { return Module->Handle; }
        static void LQ_CALL BeforeFreeNotifyProc(LqHttpMdl* Module) {}
        static void LQ_CALL DelCreatePathProc(LqHttpPth*) {}
        static bool LQ_CALL RegisterPathInDomenProc(LqHttpPth*, const char*) { return true; }
        static void LQ_CALL UnregisterPathFromDomenProc(LqHttpPth*, const char*) {}
        static void LQ_CALL ReciveCommandProc(LqHttpMdl*, const char*, void*) {};
    };

    //Module->IsFree = false;
    //Module->Handle = Handle;
    //Module->FreeNotifyProc = Procs::FreeNotifyProc;
    //Module->BeforeFreeNotifyProc = Procs::BeforeFreeNotifyProc;
    //Module->CreatePathProc = Module->DeletePathProc = Procs::DelCreatePathProc;
    //Module->RegisterPathInDomenProc = Procs::RegisterPathInDomenProc;
    //Module->UnregisterPathFromDomenProc = Procs::UnregisterPathFromDomenProc;
    //Module->GetCacheInfoProc = LqHttpMdlHandlersCacheInfo;
    //Module->GetMimeProc = LqHttpMdlHandlersMime;
    Module->RspErrorProc = LqHttpMdlHandlersError;
    //Module->ServerNameProc = LqHttpMdlHandlersServerName;
    //Module->GetActEvntHandlerProc = LqHttpMdlHandlersGetMethod;
    //Module->ReciveCommandProc = Procs::ReciveCommandProc;
    //Module->AllowProc = LqHttpMdlHandlersAllowMethods;
    //Module->NonceProc = LqHttpMdlHandlersNonce;
    //Module->ResponseRedirectionProc = LqHttpMdlHandlersResponseRedirection;
    //Module->RspStatusProc = LqHttpMdlHandlersStatus;
    Module->HttpAcceptor = HttpAcceptor;
    Module->UserData = 0;
    new(&Module->_Paths[0]) LqHttpPthArr();
    if(Name != NULL)
        Module->Name = LqStrDuplicate(Name);
	auto HttpData = ((LqHttpData*)(HttpAcceptor)->UserData);
	HttpData->Modules.push_back(Module);
}

LQ_EXTERN_C int LQ_CALL LqHttpMdlGetNameByHandle(LqHttp* Http, uintptr_t Handle, char* NameDest, size_t NameDestSize) {
	auto HttpData = ((LqHttpData*)(Http)->UserData);
    for(auto& i : HttpData->Modules) {
        if(i->Handle == Handle) {
            LqStrCopyMax(NameDest, i->Name, NameDestSize);
            return 1;
        }
    }
    return 0;
}

LQ_EXTERN_C LqHttpMdlLoadEnm LQ_CALL LqHttpMdlLoad(LqHttp* Http, const char* PathToLib, void* UserData, uintptr_t* Handle) {
    auto LibHandle = LqLibLoad(PathToLib);
    if(LibHandle == 0)
        return LQHTTPMDL_LOAD_FAIL;
    if(LqHttpMdlIsHave(Http, LibHandle) > 0)
        return LQHTTPMDL_LOAD_ALREADY_HAVE;
    auto MuduleProc = (LqHttpModuleRegistratorProc)LqLibGetProc(LibHandle, LQ_MOD_REGISTARTOR_NAME);
    if(MuduleProc == NULL) {
        LqLibFree(LibHandle);
        return LQHTTPMDL_LOAD_PROC_NOT_FOUND;
    }
    auto r = MuduleProc(Http, (uintptr_t)LibHandle, PathToLib, UserData);
    switch(r) {
        case LQHTTPMDL_REG_FREE_LIB:
            LqLibFree(LibHandle);
            return LQHTTPMDL_LOAD_INDEPENDENTLY_UNLOADED;
    }

    if(Handle != nullptr)
        *Handle = LibHandle;

    return LQHTTPMDL_LOAD_OK;
}

