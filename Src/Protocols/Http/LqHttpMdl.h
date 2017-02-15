/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpMdl... - Work with modules.
*/


#ifndef __LQ_HTTP_MDL_H_HAS_INCLUDED__
#define __LQ_HTTP_MDL_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqHttp.h"

LQ_EXTERN_C_BEGIN

#ifdef LANQBUILD
void LqHttpMdlPathFree(LqHttpPth* Pth);
void LqHttpMdlPathRegister(LqHttpMdl* Module, LqHttpPth* l);
#endif


typedef enum LqHttpMdlLoadEnm {
    LQHTTPMDL_LOAD_OK,
    LQHTTPMDL_LOAD_FAIL,
    LQHTTPMDL_LOAD_PROC_NOT_FOUND,
    LQHTTPMDL_LOAD_ALREADY_HAVE,
    LQHTTPMDL_LOAD_INDEPENDENTLY_UNLOADED
} LqHttpMdlLoadEnm;

typedef enum LqHttpMdlRegistratorEnm {
    LQHTTPMDL_REG_OK,
    LQHTTPMDL_REG_FREE_LIB
} LqHttpMdlRegistratorEnm;


#define LQ_MOD_REGISTARTOR_NAME "LqHttpMdlRegistrator"
typedef LqHttpMdlRegistratorEnm(LQ_CALL* LqHttpModuleRegistratorProc)(LqHttp* Http, uintptr_t ModuleHandle, const char* LibPath, void* UserData);

LQ_IMPORTEXPORT LqHttpMdlLoadEnm LQ_CALL LqHttpMdlLoad(LqHttp* lqain Http, const char* lqain lqautf8 PathToLib, void* lqain lqaopt UserData, uintptr_t* lqaout lqaopt Handle);
//LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFree(LqHttpMdl* lqain Module);
/*
* Use for registrate module in http proto reg.
*   If module is static linking, then generate not even number.
*/
LQ_IMPORTEXPORT void LQ_CALL LqHttpMdlInit(LqHttp* lqain Http, LqHttpMdl* lqain Module, const char* lqain Name, uintptr_t Handle);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFreeByName(LqHttp* lqain Http, const char* lqain lqautf8 NameModule, bool IsAll);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFreeAll(LqHttp* Http);
LQ_IMPORTEXPORT void LQ_CALL LqHttpMdlFreeMain(LqHttp* Http);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFreeByHandle(LqHttp* lqain Http, uintptr_t Handle);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlGetNameByHandle(LqHttp* lqain Http, uintptr_t Handle, char* lqaout lqautf8 NameDest, size_t NameDestSize);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlIsHave(LqHttp* lqain Http, uintptr_t Handle);

/* Return true - is command sended*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlSendCommandByHandle(LqHttp* lqain Http, uintptr_t Handle, const char* lqain lqautf8 Command, void* lqaio Data);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlSendCommandByName(LqHttp* lqain Http, const char* lqain lqautf8 Name, const char* lqain lqautf8 Command, void* lqaio Data);
/*
* For start enum handles set ModuleHandle = 0
* Is termination return -1
*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlEnm(LqHttp* lqain Http, uintptr_t* lqaio ModuleHandle, char* lqaout lqaopt Name, size_t NameLen, bool* lqaout lqaopt IsFree);

LQ_EXTERN_C_END

#endif