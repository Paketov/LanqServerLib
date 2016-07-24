/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqLib... - Use for load or unlod library from address space program.
*/


#include "LqLib.h"
#include "LqCp.h"
#include "LqFile.h"


#if defined(_MSC_VER)
#include <Windows.h>
static void LqLibConvertNameToWcs(const char* Name, wchar_t* DestBuf, size_t DestBufSize)
{
	if(((Name[0] >= 'a') && (Name[0] <= 'z')) || ((Name[0] >= 'A') && (Name[0] <= 'Z')) && (Name[1] == ':') && (Name[2] == '/'))
	{
		memcpy(DestBuf, L"\\\\?\\", sizeof(L"\\\\?\\"));
		DestBufSize -= 4;
		LqCpConvertToWcs(Name, DestBuf + 4, DestBufSize);
	} else
	{
		LqCpConvertToWcs(Name, DestBuf, DestBufSize);
	}
}

#else
#include <dlfcn.h>
#endif

LQ_EXTERN_C uintptr_t LQ_CALL LqLibLoad(const char* ModuleName)
{
#if defined(_MSC_VER)
	wchar_t Name[LQ_MAX_PATH];
	LqLibConvertNameToWcs(ModuleName, Name, sizeof(Name));
	return (uintptr_t)LoadLibraryW(Name);
#else
	return (uintptr_t)dlopen(ModuleName, RTLD_LAZY);
#endif
}

LQ_EXTERN_C uintptr_t LQ_CALL LqLibGetHandleByAddr(void* Address)
{
#if defined(_MSC_VER)
	HMODULE Handle = 0;
	if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (wchar_t*)Address, &Handle) == FALSE)
		return 0;
	return (uintptr_t)Handle;
#else
	Dl_info Info = {0};
	if(dladdr(Address, &Info) != 0)
		return 0;
	return (intptr_t)Info.dli_fbase;
#endif
}

LQ_EXTERN_C bool LQ_CALL LqLibFree(uintptr_t ModuleHandle)
{
#if defined(_MSC_VER)
	return FreeLibrary((HMODULE)ModuleHandle) == TRUE;
#else
	return dlclose((void*)ModuleHandle) == 0;
#endif
}


LQ_EXTERN_C void* LQ_CALL LqLibGetProc(uintptr_t ModuleHandle, const char* ProcName)
{
#if defined(_MSC_VER)
	return GetProcAddress((HMODULE)ModuleHandle, ProcName);
#else
	return dlsym((void*)ModuleHandle, ProcName);
#endif
}

