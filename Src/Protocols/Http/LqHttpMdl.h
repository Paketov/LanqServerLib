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

#define LqHttpMdlGetByPth(Path) (((Path)->Type & LQHTTPPTH_FLAG_CHILD) ? ((Path)->Parent->ParentModule) : (Path)->ParentModule)
#define LqHttpMdlGetByConn(Conn) (((Conn)->Pth == nullptr)? (&LqHttpProtoGetByConn(Conn)->StartModule): (LqHttpMdlGetByPth(Conn->Pth)))

#ifdef LANQBUILD
void LqHttpMdlPathFree(LqHttpPth* Pth);
void LqHttpMdlPathRegister(LqHttpMdl* Module, LqHttpPth* l);
#endif


typedef enum LqHttpMdlLoadEnm
{
    LQHTTPMDL_LOAD_OK,
    LQHTTPMDL_LOAD_FAIL,
    LQHTTPMDL_LOAD_PROC_NOT_FOUND,
    LQHTTPMDL_LOAD_ALREADY_HAVE,
    LQHTTPMDL_LOAD_INDEPENDENTLY_UNLOADED
} LqHttpMdlLoadEnm;

typedef enum LqHttpMdlRegistratorEnm
{
    LQHTTPMDL_REG_OK,
    LQHTTPMDL_REG_FREE_LIB
} LqHttpMdlRegistratorEnm;


#define LQ_MOD_REGISTARTOR_NAME "LqHttpMdlRegistrator"
typedef LqHttpMdlRegistratorEnm(LQ_CALL* LqHttpModuleRegistratorProc)(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData);

LQ_IMPORTEXPORT LqHttpMdlLoadEnm LQ_CALL LqHttpMdlLoad(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 PathToLib, void* lqain lqaopt UserData, uintptr_t* lqaout lqaopt Handle);
//LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFree(LqHttpMdl* lqain Module);
/*
* Use for registrate module in http proto reg.
*   If module is static linking, then generate not even number.
*/
LQ_IMPORTEXPORT void LQ_CALL LqHttpMdlInit(LqHttpProtoBase* lqain Reg, LqHttpMdl* lqain Module, const char* lqain Name, uintptr_t Handle);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFreeByName(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 NameModule, bool IsAll);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFreeAll(LqHttpProtoBase* Reg);
LQ_IMPORTEXPORT void LQ_CALL LqHttpMdlFreeMain(LqHttpProtoBase* Reg);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlFreeByHandle(LqHttpProtoBase* lqain Reg, uintptr_t Handle);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlGetNameByHandle(LqHttpProtoBase* lqain Reg, uintptr_t Handle, char* lqaout lqautf8 NameDest, size_t NameDestSize);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlIsHave(LqHttpProtoBase* lqain Reg, uintptr_t Handle);

/* Return true - is command sended*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlSendCommandByHandle(LqHttpProtoBase* lqain Reg, uintptr_t Handle, const char* lqain lqautf8 Command, void* lqaio Data);
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlSendCommandByName(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 Name, const char* lqain lqautf8 Command, void* lqaio Data);
/*
* For start enum handles set ModuleHandle = 0
* Is termination return -1
*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpMdlEnm(LqHttpProtoBase* lqain Reg, uintptr_t* lqaio ModuleHandle, char* lqaout lqaopt Name, size_t NameLen, bool* lqaout lqaopt IsFree);

LQ_EXTERN_C_END

#endif