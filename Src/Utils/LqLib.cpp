/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqLib... - Use for load or unload library.
*/


#include "LqLib.h"
#include "LqCp.h"
#include "LqFile.h"
#include "LqStr.hpp"
#include "LqLock.hpp"
#include "LqAlloc.hpp"

#if defined(LQPLATFORM_WINDOWS)
# include <Windows.h>

int _LqFileConvertNameToWcs(const char* Name, wchar_t* DestBuf, size_t DestBufSize);

#else
# define _GNU_SOURCE
# include <dlfcn.h>
#endif

LQ_EXTERN_C uintptr_t LQ_CALL LqLibLoad(const char* ModuleName) {
#if defined(LQPLATFORM_WINDOWS)
    wchar_t Name[LQ_MAX_PATH];
    _LqFileConvertNameToWcs(ModuleName, Name, LQ_MAX_PATH);
    auto PrevErrVal = SetErrorMode(SEM_FAILCRITICALERRORS);
    auto ResHandle = (uintptr_t)LoadLibraryW(Name);
    SetErrorMode(PrevErrVal);
    return ResHandle;
#else
    return (uintptr_t)dlopen(ModuleName, RTLD_LAZY);
#endif
}

LQ_EXTERN_C uintptr_t LQ_CALL LqLibGetHandleByAddr(const void* Address) {
#if defined(LQPLATFORM_WINDOWS)
    HMODULE Handle = 0;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (wchar_t*)Address, &Handle) == FALSE)
        return 0;
    return (uintptr_t)Handle;
#else
    Dl_info Info = {0};
    if(dladdr((void*)Address, &Info) != 0)
        return 0;
    return (intptr_t)Info.dli_fbase;
#endif
}


LQ_EXTERN_C int LQ_CALL LqLibGetPathByHandle(uintptr_t Handle, char* DestName, size_t DestSize) {
#if defined(LQPLATFORM_WINDOWS)
    wchar_t Name[LQ_MAX_PATH];
    if(GetModuleFileNameW((HMODULE)Handle, Name, LQ_MAX_PATH) == 0)
        return -1;

    return LqCpConvertFromWcs(Name, DestName, DestSize);
#else
    Dl_info Info = {0};
    if(dladdr((void*)Handle, &Info) != 0)
        return -1;
    return LqStrCopyMax(DestName, Info.dli_fname, DestSize);
#endif
}

LQ_EXTERN_C int LQ_CALL LqLibFree(uintptr_t ModuleHandle) {
#if defined(LQPLATFORM_WINDOWS)
    return (FreeLibrary((HMODULE)ModuleHandle) == TRUE) ? 0 : -1;
#else
    return (dlclose((void*)ModuleHandle) == 0) ? 0 : -1;
#endif
}


LQ_EXTERN_C void* LQ_CALL LqLibGetProc(uintptr_t ModuleHandle, const char* ProcName) {
#if defined(LQPLATFORM_WINDOWS)
    return GetProcAddress((HMODULE)ModuleHandle, ProcName);
#else
    return dlsym((void*)ModuleHandle, ProcName);
#endif
}

typedef struct LqLibSaveElem {
    int16_t Deep;
    uintptr_t Handle;
} LqLibSaveElem;

static LqLibSaveElem* LibSaveElemArr = NULL;
static uintptr_t LibSaveElemArrCount = 0;
static LqLocker<uintptr_t> LibSaveElemArrLocker;


LQ_EXTERN_C void LQ_CALL LqLibSaveEnter(uintptr_t ModuleHandle) {
    uintptr_t i;
lblAgain:
    LibSaveElemArrLocker.LockWriteYield();
    for(i = 0; i < LibSaveElemArrCount; i++) {
        if(LibSaveElemArr[i].Handle == ModuleHandle) {
            if(LibSaveElemArr[i].Deep >= ((int16_t)0)) {
                LibSaveElemArr[i].Deep++;
                LibSaveElemArrLocker.UnlockWrite();
                return;
            } else {
                LibSaveElemArrLocker.UnlockWrite();
                LqThreadYield();
                goto lblAgain;
            }
        }
    }
    LibSaveElemArr = (LqLibSaveElem*)LqMemRealloc(LibSaveElemArr, (LibSaveElemArrCount + 1) * sizeof(LqLibSaveElem));
    LibSaveElemArr[LibSaveElemArrCount].Deep = 1;
    LibSaveElemArr[LibSaveElemArrCount].Handle = ModuleHandle;
    LibSaveElemArrCount++;
    LibSaveElemArrLocker.UnlockWrite();
}


LQ_EXTERN_C void LQ_CALL LqLibSaveOut(uintptr_t ModuleHandle) {
    uintptr_t i;
    int* InvalidAddr = NULL;
    LibSaveElemArrLocker.LockWriteYield();
    for(i = 0; i < LibSaveElemArrCount; i++) {
        if(LibSaveElemArr[i].Handle == ModuleHandle) {
            LibSaveElemArr[i].Deep--;
            if(LibSaveElemArr[i].Deep == 0) {
                LibSaveElemArrCount--;
                memcpy(LibSaveElemArr + i, LibSaveElemArr + LibSaveElemArrCount, sizeof(LqLibSaveElem));
                LibSaveElemArr = (LqLibSaveElem*)LqMemRealloc(LibSaveElemArr, LibSaveElemArrCount * sizeof(LqLibSaveElem));
            }
            LibSaveElemArrLocker.UnlockWrite();
            return;
        }
    }
    *InvalidAddr = 0;
}

LQ_EXTERN_C int LQ_CALL LqLibFreeSave(uintptr_t ModuleHandle) {
    uintptr_t i;
    int Res;
lblAgain:
    LibSaveElemArrLocker.LockWriteYield();
    for(i = 0; i < LibSaveElemArrCount; i++) {
        if(LibSaveElemArr[i].Handle == ModuleHandle) {
            LibSaveElemArrLocker.UnlockWrite();
            LqThreadYield();
            goto lblAgain;
        }
    }
    LibSaveElemArr = (LqLibSaveElem*)LqMemRealloc(LibSaveElemArr, (LibSaveElemArrCount + 1) * sizeof(LqLibSaveElem));
    LibSaveElemArr[LibSaveElemArrCount].Deep = -((int16_t)1);
    LibSaveElemArr[LibSaveElemArrCount].Handle = ModuleHandle;
    LibSaveElemArrCount++;
    LibSaveElemArrLocker.UnlockWrite();

    Res = LqLibFree(ModuleHandle);

    LibSaveElemArrLocker.LockWriteYield();
    for(i = 0; i < LibSaveElemArrCount; i++) {
        if(LibSaveElemArr[i].Handle == ModuleHandle) {
            LibSaveElemArrCount--;
            memcpy(LibSaveElemArr + i, LibSaveElemArr + LibSaveElemArrCount, sizeof(LqLibSaveElem));
            LibSaveElemArr = (LqLibSaveElem*)LqMemRealloc(LibSaveElemArr, LibSaveElemArrCount * sizeof(LqLibSaveElem));
            break;
        }
    }
    LibSaveElemArrLocker.UnlockWrite();
    return Res;
}