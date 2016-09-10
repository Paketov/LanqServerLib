/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqLib... - Use for load or unlod library from address space program.
*/

#ifndef __LQ_LIB_H_HAS_INCLUDED__
#define __LQ_LIB_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqDef.h"

#include <stdint.h>


LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT uintptr_t LQ_CALL LqLibLoad(const char* lqain lqacp ModuleName);
LQ_IMPORTEXPORT bool LQ_CALL LqLibFree(uintptr_t ModuleHandle);
LQ_IMPORTEXPORT uintptr_t LQ_CALL LqLibGetHandleByAddr(const void* Address);
LQ_IMPORTEXPORT int LQ_CALL LqLibGetPathByHandle(uintptr_t Handle, char* lqaout lqacp DestName, size_t DestSize);
LQ_IMPORTEXPORT void* LQ_CALL LqLibGetProc(uintptr_t ModuleHandle, const char* lqain ProcName);

LQ_EXTERN_C_END


#endif