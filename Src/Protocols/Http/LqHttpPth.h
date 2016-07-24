/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpPth... - Functions for working with path and domens.
*/


#ifndef __LANQ_HTTP_PTH_HPP_HAS_INCLUDED__
#define __LANQ_HTTP_PTH_HPP_HAS_INCLUDED__


#include "LqHttp.h"
#include "LqOs.h"

LQ_EXTERN_C_BEGIN


#define LQHTTPPTH_MAX_DOMEN_NAME 1024

#if defined(LANQBUILD)
void LqHttpPthRecognize(LqHttpConn* c);

void LqHttpPthAssign(LqHttpPth* Pth);
bool LqHttpPthRelease(LqHttpPth* Pth);
#endif




/*
* Is @Atz == nullptr, then remove athorization
*/
LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthDirSetAtz(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 WebDomen, const char* lqain lqautf8 WebPath, LqHttpAtz* lqain Atz, bool IsReplace);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthFileSetAtz(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 WebDomen, const char* lqain lqautf8 WebPath, LqHttpAtz* lqain Atz, bool IsReplace);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthDirSetPerm(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 WebDomen, const char* lqain lqautf8 WebPath, uint8_t Permissions);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthFileSetPerm(LqHttpProtoBase* lqain Reg, const char* lqain lqautf8 WebDomen, const char* lqain lqautf8 WebPath, uint8_t Permissions);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterFile
(
	LqHttpProtoBase* lqaio Reg,
	LqHttpMdl* lqain lqaopt RegModule,
	const char* lqain lqautf8 WebDomen,
	const char* lqain lqautf8 WebPath,
	const char* lqain lqautf8 RealPath,
	uint8_t Permissions,
	LqHttpAtz* lqain lqaopt Autoriz,
	uintptr_t ModuleData
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterDir
(
	LqHttpProtoBase* lqaio Reg, 
	LqHttpMdl* lqain lqaopt RegModule,
	const char* lqain lqautf8 WebDomen,
	const char* lqain lqautf8 WebPath,
	const char* lqain lqautf8 RealPath,
	bool IsIncludeSubdirs,
	uint8_t Permissions,
	LqHttpAtz* lqain lqaopt Autoriz,
	uintptr_t ModuleData
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterExecFile
(
	LqHttpProtoBase* lqaio Reg,
	LqHttpMdl* lqain lqaopt RegModule,
	const char* lqain lqautf8 WebDomen,
	const char* lqain lqautf8 WebPath,
	LqHttpEvntHandlerFn ExecQueryProc,
	uint8_t Permissions,
	LqHttpAtz* lqain lqaopt Autoriz,
	uintptr_t ModuleData
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterExecDir
(
	LqHttpProtoBase* lqaio Reg,
	LqHttpMdl* lqain lqaopt RegModule,
	const char* lqain lqautf8 WebDomen,
	const char* lqain lqautf8 WebPath,
	bool IsIncludeSubdirs,
	LqHttpEvntHandlerFn ExecQueryProc,
	uint8_t Permissions,
	LqHttpAtz* lqain lqaopt Autoriz,
	uintptr_t ModuleData
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterFileRedirection
(
	LqHttpProtoBase* lqaio Reg,
	LqHttpMdl* lqain lqaopt RegModule,
	const char* lqain lqautf8 WebDomen,
	const char* lqain lqautf8 WebPath,
	const char* lqain lqautf8 Location,
	short ResponseStatus,
	uint8_t Permissions,
	LqHttpAtz* lqain lqaopt Autoriz,
	uintptr_t ModuleData
);

/*
* Includes subdirs automatically
*/
LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterDirRedirection
(
	LqHttpProtoBase* lqaio Reg,
	LqHttpMdl* lqain lqaopt RegModule,
	const char* lqain lqautf8 WebDomen,
	const char* lqain lqautf8 WebPath,
	const char* lqain lqautf8 Location,
	short ResponseStatus,
	uint8_t Permissions,
	LqHttpAtz* lqain lqaopt Autoriz,
	uintptr_t ModuleData
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthCopyFile
(
	LqHttpProtoBase* lqaio Reg,
	const char* lqain lqautf8 WebDomenDest, 
	const char* lqain lqautf8 WebDomenSource,
	const char* lqain lqautf8 WebPath
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthCopyDir
(
	LqHttpProtoBase* lqaio Reg, 
	const char* lqain lqautf8 WebDomenDest, 
	const char* lqain lqautf8 WebDomenSource, 
	const char* lqain lqautf8 WebPath
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthUnregisterFile
(
	LqHttpProtoBase* lqaio Reg, 
	const char* lqain lqautf8 WebDomen, 
	const char* lqain lqautf8 WebPath
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthUnregisterDir
(
	LqHttpProtoBase* lqaio Reg, 
	const char* lqain lqautf8 WebDomen, 
	const char* lqain lqautf8 WebPath
);
/*
*	Register "naked" path
*/
LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthRegister
(
	LqHttpProtoBase* lqaio Reg, 
	const char* lqain lqautf8 Domain, 
	LqHttpPth* lqain Path
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpPthEnm
(
	LqHttpProtoBase* lqain Reg,
	const char* lqain lqautf8 Domain,
	char* lqaio lqautf8 WebPath, size_t PathLen,
	int* lqaout lqaopt Type,
	char* lqaout lqaopt lqautf8 RealPath, size_t RealPathLen,
	LqHttpEvntHandlerFn* lqaout lqaopt EventFunction,
	uintptr_t* lqaout lqaopt ModuleOwner,
	char* lqaout lqaopt lqautf8 ModuleName, size_t ModuleNameSize,
	uintptr_t* lqaout lqaopt ModuleData,
	uint8_t* lqaout lqaopt Permissions,
	LqHttpAtz** lqaout lqaopt lqamrelease AccessUserList
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpPthInfo
(
	LqHttpProtoBase* lqain Reg,
	const char* lqain lqautf8 Domain,
	const char* lqain lqautf8 WebPath,
	bool IsDir,
	int* lqaout lqaopt Type,
	char* lqaout lqaopt lqautf8 RealPath, size_t RealPathLen,
	LqHttpEvntHandlerFn* lqaout lqaopt EventFunction,
	uintptr_t* lqaout lqaopt ModuleOwner,
	char* lqaout lqaopt lqautf8 ModuleName, size_t ModuleNameSize,
	uintptr_t* lqaout lqaopt ModuleData,
	uint8_t* Permissions,
	LqHttpAtz** lqaout lqaopt lqamrelease AccessUserList
);

LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthDmnCreate
(
	LqHttpProtoBase* lqaio Reg,
	const char* lqain lqautf8 WebDomen
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpPthDmnEnm
(
	LqHttpProtoBase* lqain Reg, 
	char* lqaio lqautf8 WebDomen, 
	size_t WebDomenLen
);
LQ_IMPORTEXPORT LqHttpPthResultEnm LQ_CALL LqHttpPthDmnDelete
(
	LqHttpProtoBase* lqaio Reg, 
	const char* lqain lqautf8 WebDomen
);

LQ_EXTERN_C_END

#endif