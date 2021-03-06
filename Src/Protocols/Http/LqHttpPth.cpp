/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpPth... - Functions for working with path and domens.
*/

#include "LqOs.h"
#include "LqErr.h"
#include "LqConn.h"
#include "LqStrSwitch.h"
#include "LqAlloc.hpp"
#include "LqAtm.hpp"
#include "LqTime.h"
#include "LqStr.hpp"
#include "LqCrypt.h"

#include "LqHttpPth.hpp"
#include "LqHttp.hpp"
#include "LqHttpAtz.h"
#include "LqHttpMdl.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

LqHttpDmn EmptyDmn;
LqHttpPth EmptyPth;
LqHttpAtz EmptyAtz;

static const char __init = ([] {
	/* Init empty domain */
	static char EmptyStr[5] = "";
	EmptyDmn.CountPointers = 5;
	EmptyDmn.Name = EmptyStr;
	EmptyDmn.NameHash = 0;

	EmptyAtz.AuthType = LQHTTPATZ_TYPE_NONE;
	EmptyAtz.CountAuthoriz = 0;
	EmptyAtz.CountPointers = 6;
	LqAtmLkInit(EmptyAtz.Locker);
	EmptyAtz.Realm = EmptyStr;

	LqObPtrInit<LqHttpAtz>(EmptyPth.Atz, &EmptyAtz, EmptyPth.AtzPtrLk);
	EmptyPth.CountPointers = 5;
	EmptyPth.WebPath = EmptyStr;
	EmptyPth.Atz = &EmptyAtz;

	return char(0);
})();

#define LqCheckedFree(MemReg) (((MemReg) != nullptr)? free(MemReg): void())

static LqHttpPthResultEnm LqHttpPthRegisterNative(LqHttpDmnTbl& Dmns, const char* WebDomen, LqHttpPthPtr& Path);

static LqHttpPthPtr LqHttpPthCreate
(
	const char* WebPath,
	uint8_t Type,
	void* Data,
	uint8_t DefaultAccess,
	LqHttpAtz* Authoriz,
	LqHttpMdl* RegModule
);

template<typename T>
static inline T StringHash(const char * Str) {
	T h = 0;
	for(const char* k = Str; *k != '\0'; k++)
		h = 31 * h + *k;
	return h;
}

uint16_t LqHttpPthCmp::IndexByKey(const LqHttpPthPtr& CurPth, uint16_t MaxIndex) { return CurPth->WebPathHash % MaxIndex; }
uint16_t LqHttpPthCmp::IndexByKey(const char* Key, uint16_t MaxIndex) { return StringHash<uint32_t>(Key) % MaxIndex; }
bool LqHttpPthCmp::Cmp(const LqHttpPthPtr& CurPth, const LqHttpPthPtr& Key) {
	return (Key == CurPth.Get()) || ((Key->WebPathHash == CurPth->WebPathHash) && LqStrSame(Key->WebPath, CurPth->WebPath));
}

bool LqHttpPthCmp::Cmp(const LqHttpPthPtr& CurPth, const char* Key) { return LqStrSame(Key, CurPth->WebPath); }
uint16_t LqHttpDmn::IndexByKey(const LqHttpDmnPtr& CurPth, uint16_t MaxIndex) { return CurPth->NameHash % MaxIndex; }

uint16_t LqHttpDmn::IndexByKey(const char* Key, uint16_t MaxIndex) {
	uint32_t h = 0;
	for(const char* k = Key; *k != '\0';)
		h = 31 * h + ((*k >= 'a') && (*k <= 'z')) ? (uint32_t)*(k++) : LqStrUtf8ToLowerChar(&k, -1);
	return h % MaxIndex;
}

bool LqHttpDmn::Cmp(const LqHttpDmnPtr& CurPth, const LqHttpDmnPtr& Key) { return LqStrUtf8CmpCase(CurPth->Name, Key->Name); }
bool LqHttpDmn::Cmp(const LqHttpDmnPtr& CurPth, const char* Key) { return LqStrUtf8CmpCase(CurPth->Name, Key); }

LqHttpDmnPtr LqHttpDmnAlloc(const char* NewName) {
	auto NewDomen = LqFastAlloc::New<LqHttpDmn>();
	char HostBuf[LQHTTPPTH_MAX_DOMEN_NAME];
	LqStrUtf8ToLower(HostBuf, sizeof(HostBuf) - 1, NewName, -1);

	auto l = LqStrLen(HostBuf);
	auto NameDomain = (char*)malloc(l + 1);
	if(NameDomain == nullptr)
		throw "LqHttpDmnAlloc(): Not alloc memory\n";
	LqStrUtf8ToLower(NameDomain, l + 1, HostBuf, -1);
	uint32_t h = 0;
	for(const char* k = NewName; *k != '\0';)
		h = 31 * h + ((*k >= 'a') && (*k <= 'z')) ? (uint32_t)*(k++) : LqStrUtf8ToLowerChar(&k, -1);
	NewDomen->NameHash = h;
	NewDomen->Name = NameDomain;
	NewDomen->CountPointers = 0;
	return NewDomen;
}

void _LqHttpDmnDelete(LqHttpDmn* Dmn) {
	for(auto& i : Dmn->Pths)
		i->ParentModule->UnregisterPathFromDomenProc(i.Get(), Dmn->Name);
	free(Dmn->Name);
	LqFastAlloc::Delete(Dmn);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterFile
(
	LqHttp* Http,
	LqHttpMdl* RegModule,
	const char* WebDomen,
	const char* WebPath,
	const char* RealPath,
	uint8_t Permissions,
	LqHttpAtz* Autoriz,
	uintptr_t ModuleData
) {
	auto NewPath = LqHttpPthCreate
	(
		WebPath,
		LQHTTPPTH_TYPE_FILE,
		(char*)RealPath,
		Permissions,
		Autoriz,
		(RegModule == NULL) ? &LqHttpGetHttpData(Http)->StartModule : RegModule
	);
	NewPath->ModuleData = ModuleData;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, WebDomen, NewPath);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterDir
(
	LqHttp* Http,
	LqHttpMdl* RegModule,
	const char* WebDomen,
	const char* WebPath,
	const char* RealPath,
	bool IsIncludeSubdirs,
	uint8_t Permissions,
	LqHttpAtz* Autoriz,
	uintptr_t ModuleData
) {
	auto l = LqStrLen(WebPath);
	if((l == 0) || (WebPath[l - 1] != '/'))
		return LQHTTPPTH_RES_NOT_DIR;
	auto NewPath = LqHttpPthCreate
	(
		WebPath,
		LQHTTPPTH_TYPE_DIR | ((IsIncludeSubdirs) ? LQHTTPPTH_FLAG_SUBDIR : 0),
		(char*)RealPath,
		Permissions,
		Autoriz,
		(RegModule == NULL) ? &LqHttpGetHttpData(Http)->StartModule : RegModule
	);
	NewPath->ModuleData = ModuleData;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, WebDomen, NewPath);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterExecFile
(
	LqHttp* Http,
	LqHttpMdl* RegModule,
	const char* WebDomen,
	const char* WebPath,
	LqHttpEvntHandlerFn ExecQueryProc,
	uint8_t Permissions,
	LqHttpAtz* Autoriz,
	uintptr_t ModuleData
) {
	auto NewPath = LqHttpPthCreate
	(
		WebPath,
		LQHTTPPTH_TYPE_EXEC_FILE,
		(void*)ExecQueryProc,
		Permissions,
		Autoriz,
		(RegModule == NULL) ? &LqHttpGetHttpData(Http)->StartModule : RegModule
	);
	NewPath->ModuleData = ModuleData;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, WebDomen, NewPath);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterExecDir
(
	LqHttp* Http,
	LqHttpMdl* RegModule,
	const char* WebDomen,
	const char* WebPath,
	bool IsIncludeSubdirs,
	LqHttpEvntHandlerFn ExecQueryProc,
	uint8_t Permissions,
	LqHttpAtz* Autoriz,
	uintptr_t ModuleData
) {
	auto l = LqStrLen(WebPath);
	if((l == 0) || (WebPath[l - 1] != '/'))
		return LQHTTPPTH_RES_NOT_DIR;

	auto NewPath = LqHttpPthCreate
	(
		WebPath,
		LQHTTPPTH_TYPE_EXEC_DIR | ((IsIncludeSubdirs) ? LQHTTPPTH_FLAG_SUBDIR : 0),
		(void*)ExecQueryProc,
		Permissions,
		Autoriz,
		(RegModule == NULL) ? &LqHttpGetHttpData(Http)->StartModule : RegModule
	);
	NewPath->ModuleData = ModuleData;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, WebDomen, NewPath);
}


LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterFileRedirection
(
	LqHttp* Http,
	LqHttpMdl* RegModule,
	const char* WebDomen,
	const char* WebPath,
	const char* Location,
	short ResponseStatus,
	uint8_t Permissions,
	LqHttpAtz* Autoriz,
	uintptr_t ModuleData
) {
	auto NewPath = LqHttpPthCreate
	(
		WebPath,
		LQHTTPPTH_TYPE_FILE_REDIRECTION,
		(void*)Location,
		Permissions,
		Autoriz,
		(RegModule == nullptr) ? &LqHttpGetHttpData(Http)->StartModule : RegModule
	);
	NewPath->ModuleData = ModuleData;
	NewPath->StatusCode = ResponseStatus;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, WebDomen, NewPath);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterDirRedirection
(
	LqHttp* Http,
	LqHttpMdl* RegModule,
	const char* WebDomen,
	const char* WebPath,
	const char* Location,
	short ResponseStatus,
	uint8_t Permissions,
	LqHttpAtz* Autoriz,
	uintptr_t ModuleData
) {
	auto l = LqStrLen(WebPath);
	if((l == 0) || (WebPath[l - 1] != '/'))
		return LQHTTPPTH_RES_NOT_DIR;
	auto NewPath = LqHttpPthCreate
	(
		WebPath,
		LQHTTPPTH_TYPE_DIR_REDIRECTION | LQHTTPPTH_FLAG_SUBDIR,
		(void*)Location,
		Permissions,
		Autoriz,
		(RegModule == NULL) ? &LqHttpGetHttpData(Http)->StartModule : RegModule
	);
	NewPath->ModuleData = ModuleData;
	NewPath->StatusCode = ResponseStatus;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, WebDomen, NewPath);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthCopyFile
(
	LqHttp* Http,
	const char* WebDomenDest,
	const char* WebDomenSource,
	const char* WebPath
) {
	auto Dmns = LqHttpGetHttpData(Http)->Dmns;
	auto DmnSourceInterator = Dmns.search(WebDomenSource);
	auto DmnDestInterator = Dmns.search(WebDomenDest);
	if(DmnSourceInterator.is_end() || DmnDestInterator.is_end())
		return LQHTTPPTH_RES_NOT_HAVE_DOMEN;
	auto PthInterator = (*DmnSourceInterator)->Pths.search(WebPath);
	if(PthInterator.is_end())
		return LQHTTPPTH_RES_NOT_HAVE_PATH;
	switch((*DmnDestInterator)->Pths.push_back_uniq(*PthInterator)) {
		case -1: return LQHTTPPTH_RES_NOT_ALLOC_MEM;
		case 0: return LQHTTPPTH_RES_ALREADY_HAVE;
	}
	return LQHTTPPTH_RES_OK;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthCopyDir
(
	LqHttp* Http,
	const char* WebDomenDest,
	const char* WebDomenSource,
	const char* WebPath
) {
	LqString TempPathStr = WebPath;
	TempPathStr.append("?", 1);
	return LqHttpPthCopyFile(Http, WebDomenDest, WebDomenSource, TempPathStr.c_str());
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthUnregisterFile
(
	LqHttp* Http,
	const char* WebDomen,
	const char* WebPath
) {
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	const auto DmnInterator = Dmns.search(WebDomen);
	if(DmnInterator.is_end())
		return LQHTTPPTH_RES_NOT_HAVE_DOMEN;
	LqHttpPthPtr Pth = &EmptyPth;
	if((*DmnInterator)->Pths.remove_by_val(WebPath, &Pth)) {
		Pth->ParentModule->UnregisterPathFromDomenProc(Pth.Get(), WebDomen);
		return LQHTTPPTH_RES_OK;
	}
	return LQHTTPPTH_RES_NOT_HAVE_PATH;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthUnregisterDir
(
	LqHttp* Http,
	const char* WebDomen,
	const char* WebPath
) {
	LqString TempPathStr = WebPath;
	TempPathStr.append("?", 1);
	return LqHttpPthUnregisterFile(Http, WebDomen, TempPathStr.c_str());
}


static LqHttpPthResultEnm LqHttpPthFileSetUnsetAtz
(
	LqHttp* Http,
	const char* WebDomen,
	const char* WebPath,
	LqHttpAtz* Atz,
	bool IsReplaceAtz,
	bool IsSetAtz,
	uint8_t Perm,
	bool IsSetPerm
) {
	auto Dmns = LqHttpGetHttpData(Http)->Dmns;
	auto DmnInterator = Dmns.search(WebDomen);
	if(DmnInterator.is_end())
		return LQHTTPPTH_RES_NOT_HAVE_DOMEN;

	auto PthIntertor = (*DmnInterator)->Pths.search(WebPath);
	if(PthIntertor.is_end())
		return LQHTTPPTH_RES_NOT_HAVE_PATH;

	LqHttpPthPtr Pth = *PthIntertor;
	if(IsSetAtz) {
		auto PrevVal = LqObPtrNewStart(Pth->Atz, Pth->AtzPtrLk);
		if((!IsReplaceAtz) && (PrevVal != nullptr)) {
			LqObPtrNewFin<LqHttpAtz, _LqHttpAtzDelete>(Pth->Atz, PrevVal, Pth->AtzPtrLk);
			return LQHTTPPTH_RES_ALREADY_HAVE_ATZ;
		}
		if((Atz == nullptr) && (PrevVal == nullptr)) {
			LqObPtrNewFin<LqHttpAtz, _LqHttpAtzDelete>(Pth->Atz, PrevVal, Pth->AtzPtrLk);
			return LQHTTPPTH_RES_NOT_HAVE_ATZ;
		}
		LqObPtrNewFin<LqHttpAtz, _LqHttpAtzDelete>(Pth->Atz, Atz, Pth->AtzPtrLk);
	}
	if(IsSetPerm)
		Pth->Permissions = Perm;
	return LQHTTPPTH_RES_OK;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDirSetAtz(LqHttp* Http, const char* WebDomen, const char* WebPath, LqHttpAtz* Atz, bool IsReplace) {
	LqString TempPathStr = WebPath;
	TempPathStr.append("?", 1);
	return LqHttpPthFileSetUnsetAtz(Http, WebDomen, TempPathStr.c_str(), Atz, IsReplace, true, 0, false);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthFileSetAtz(LqHttp* Http, const char* WebDomen, const char* WebPath, LqHttpAtz* Atz, bool IsReplace) {
	return LqHttpPthFileSetUnsetAtz(Http, WebDomen, WebPath, Atz, IsReplace, true, 0, false);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDirSetPerm(LqHttp* Http, const char* WebDomen, const char* WebPath, uint8_t Permissions) {
	LqString TempPathStr = WebPath;
	TempPathStr.append("?", 1);
	return LqHttpPthFileSetUnsetAtz(Http, WebDomen, TempPathStr.c_str(), nullptr, false, false, Permissions, true);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthFileSetPerm(LqHttp* Http, const char* WebDomen, const char* WebPath, uint8_t Permissions) {
	return LqHttpPthFileSetUnsetAtz(Http, WebDomen, WebPath, nullptr, false, false, Permissions, true);
}

/*
* Create domen
*/
LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDmnCreate(LqHttp* Http, const char* WebDomen) {
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	auto DmnPtr = LqHttpDmnAlloc(WebDomen);
	auto Res = Dmns.push_back_uniq(DmnPtr);
	if(Res == -1)
		return LQHTTPPTH_RES_NOT_ALLOC_MEM;
	if(Res == 0)
		return LQHTTPPTH_RES_ALREADY_HAVE;
	return LQHTTPPTH_RES_OK;
}

LQ_EXTERN_C bool LQ_CALL LqHttpPthDmnEnm(LqHttp* Http, char* WebDomen, size_t WebDomenLen) {
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	LqHttpDmnTbl::interator Dmn;
	if(WebDomen[0] == '\0') {
		Dmn = Dmns.begin();
	} else {
		Dmn = Dmns.search(WebDomen);
		if(Dmn.is_end())
			return false;
		++Dmn;
	}
	if(Dmn.is_end())
		return false;
	LqStrCopyMax(WebDomen, (*Dmn)->Name, WebDomenLen);
	return true;
}


LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDmnDelete(LqHttp* Http, const char* WebDomen) {
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	return (Dmns.remove_by_val(WebDomen)) ? LQHTTPPTH_RES_OK : LQHTTPPTH_RES_NOT_HAVE_DOMEN;
}

/*
* Note!
* Argument @Pth - not thread safe!
* if @Pth use of multiple thread, then  copy this string to temp local buffer before call.
* This not doing in func for improve performance.
*/
static LqHttpPth* LqHttpPthGetByAddressSubdirCheck
(
	LqHttp* Http,
	const char* Domain,
	char* Path,
	int* DeepSubDirs
) {
	int k = 0;
	char c, c2;
	LqHttpDmnPtr DefaultDmn = &EmptyDmn, Dmn = &EmptyDmn;
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;

	{
		auto Interator = Dmns.search(Domain);
		if(!Interator.is_end())
			Dmn = *Interator;
	}
	if(LqHttpGetHttpData(Http)->UseDefaultDmn) {
		auto Interator = Dmns.search("*");
		if(!Interator.is_end())
			DefaultDmn = *Interator;
	}
	auto Pth = Dmn->Pths.search(Path);
	if(Pth.is_end())
		Pth = DefaultDmn->Pths.search(Path);
	if(!Pth.is_end()) {
		LqHttpPthAssign(Pth->Get());
		*DeepSubDirs = k;
		return Pth->Get();
	}
	for(intptr_t l = LqStrLen(Path) - 1; l >= 0; l--)
		if(Path[l] == '/') {
			k++;
			c = Path[l + 1];
			Path[l + 1] = '?';
			c2 = Path[l + 2];
			Path[l + 2] = '\0';
			Pth = Dmn->Pths.search(Path);
			if(Pth.is_end())
				Pth = DefaultDmn->Pths.search(Path);
			Path[l + 1] = c;
			Path[l + 2] = c2;
			if(!Pth.is_end()) {
				if((k > 1) && !((*Pth)->Type & LQHTTPPTH_FLAG_SUBDIR)) {
					Pth.set_end();
				} else {
					LqHttpPthAssign(Pth->Get());
					*DeepSubDirs = k;
					return Pth->Get();
				}
			}
		}
	return nullptr;
}

static LqHttpPth* LqHttpPthGetByAddress(LqHttp* Http, const char* Domain, char* Path) {
	char c, c2;
	LqHttpDmnPtr DefaultDmn = &EmptyDmn, Dmn = &EmptyDmn;
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	{
		auto Interator = Dmns.search(Domain);
		if(!Interator.is_end())
			Dmn = *Interator;
	}
	if(LqHttpGetHttpData(Http)->UseDefaultDmn) {
		auto Interator = Dmns.search("*");
		if(!Interator.is_end())
			DefaultDmn = *Interator;
	}
	auto Pth = Dmn->Pths.search(Path);
	if(Pth.is_end())
		Pth = DefaultDmn->Pths.search(Path);
	if(!Pth.is_end()) {
		LqHttpPthAssign(Pth->Get());
		return Pth->Get();
	}
	for(intptr_t l = LqStrLen(Path) - 1; l >= 0; l--)
		if(Path[l] == '/') {
			c = Path[l + 1];
			c2 = Path[l + 2];
			Path[l + 1] = '?';
			Path[l + 2] = '\0';
			if((Pth = Dmn->Pths.search(Path)).is_end())
				Pth = DefaultDmn->Pths.search(Path);
			Path[l + 1] = c;
			Path[l + 2] = c2;
			if(!Pth.is_end()) {
				LqHttpPthAssign(Pth->Get());
				return Pth->Get();
			}
		}
	return nullptr;
}


/*
* Enumerate web paths for domen
*/
LQ_EXTERN_C bool LQ_CALL LqHttpPthEnm
(
	LqHttp* Http,
	const char* Domain,
	char* WebPath, size_t PathLen,
	int* Type,
	char* RealPath, size_t RealPathLen,
	LqHttpEvntHandlerFn* EventFunction,
	uintptr_t* ModuleOwner,
	char* ModuleName, size_t ModuleNameSize,
	uintptr_t* ModuleData,
	uint8_t* Permissions,
	LqHttpAtz** AccessUserList
) {
	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	auto r = Dmns.search(Domain);
	if(r.is_end())
		return false;
	LqHttpPthTbl::interator i;
	if(WebPath[0] == '\0') {
		i = (*r)->Pths.begin();
	} else {
		i = (*r)->Pths.search(WebPath);
		i++;
	}
	if(!i.is_end()) {
		const LqHttpPth* Pth = i->Get();
		auto l = LqStrLen(WebPath);
		LqStrCopyMax(WebPath, Pth->WebPath, PathLen);
		if(Type != nullptr)
			*Type = Pth->Type;
		switch(Pth->Type & LQHTTPPTH_TYPE_SEP) {
			case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
				if(RealPath != nullptr)
					LqStrCopyMax(RealPath, Pth->RealPath, RealPathLen);
				break;
			case LQHTTPPTH_TYPE_FILE_REDIRECTION: case LQHTTPPTH_TYPE_DIR_REDIRECTION:
				if(RealPath != nullptr)
					LqStrCopyMax(RealPath, Pth->Location, RealPathLen);
				break;
			case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
				if(EventFunction != nullptr)
					*EventFunction = Pth->ExecQueryProc;
				break;
		}
		if(ModuleOwner != nullptr) {
			if(Pth->ParentModule != nullptr)
				*ModuleOwner = Pth->ParentModule->Handle;
			else
				*ModuleOwner = 0;
		}
		if(ModuleName != nullptr) {
			ModuleName[0] = '\0';
			if(Pth->ParentModule != nullptr)
				LqStrCopyMax(ModuleName, Pth->ParentModule->Name, ModuleNameSize);
		}
		if(ModuleData != nullptr)
			*ModuleData = Pth->ModuleData;
		if(Permissions != nullptr)
			*Permissions = Pth->Permissions;
		if(AccessUserList != nullptr) {
			if(*AccessUserList = Pth->Atz)
				LqHttpAtzAssign(Pth->Atz);
		}
		return true;
	}
	return false;
}


/*
* Enumerate web paths for domen
*/
LQ_EXTERN_C bool LQ_CALL LqHttpPthInfo
(
	LqHttp* Http,
	const char* Domain,
	const char* WebPath,
	bool IsDir,
	int* Type,
	char* RealPath, size_t RealPathLen,
	LqHttpEvntHandlerFn* EventFunction,
	uintptr_t* ModuleOwner,
	char* ModuleName, size_t ModuleNameSize,
	uintptr_t* ModuleData,
	uint8_t* Permissions,
	LqHttpAtz** AccessUserList
) {
	auto l = LqStrLen(WebPath);
	LqString WebPathBuf;
	if(IsDir && ((l < 1) || (WebPath[l - 1] != '?'))) {
		WebPathBuf = WebPath;
		WebPathBuf.append(1, '?');
		WebPath = WebPathBuf.c_str();
	}

	auto& Dmns = LqHttpGetHttpData(Http)->Dmns;
	auto Dmn = Dmns.search(Domain);
	if(Dmn.is_end())
		return false;

	auto Pth = (*Dmn)->Pths.search(WebPath);
	if(Pth.is_end())
		return false;

	if(Type != nullptr)
		*Type = (*Pth)->Type;
	switch((*Pth)->Type & LQHTTPPTH_TYPE_SEP) {
		case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
			if(RealPath != nullptr)
				LqStrCopyMax(RealPath, (*Pth)->RealPath, RealPathLen);
			break;
		case LQHTTPPTH_TYPE_FILE_REDIRECTION: case LQHTTPPTH_TYPE_DIR_REDIRECTION:
			if(RealPath != nullptr)
				LqStrCopyMax(RealPath, (*Pth)->Location, RealPathLen);
			break;
		case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
			if(EventFunction != nullptr)
				*EventFunction = (*Pth)->ExecQueryProc;
			break;
	}
	if(ModuleOwner != nullptr) {
		if((*Pth)->ParentModule != nullptr)
			*ModuleOwner = (*Pth)->ParentModule->Handle;
		else
			*ModuleOwner = 0;
	}
	if(ModuleName != nullptr) {
		ModuleName[0] = '\0';
		if((*Pth)->ParentModule != nullptr)
			LqStrCopyMax(ModuleName, (*Pth)->ParentModule->Name, ModuleNameSize);
	}
	if(ModuleData != nullptr)
		*ModuleData = (*Pth)->ModuleData;
	if(Permissions != nullptr)
		*Permissions = (*Pth)->Permissions;
	if(AccessUserList != nullptr) {
		if(*AccessUserList = (*Pth)->Atz)
			LqHttpAtzAssign((*Pth)->Atz);
	}
	return true;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegister(LqHttp* Http, const char* Domain, LqHttpPth* Path) {
	LqHttpPthPtr Pth = Path;
	return LqHttpPthRegisterNative(LqHttpGetHttpData(Http)->Dmns, Domain, Pth);
}

static LqHttpPthResultEnm LqHttpPthRegisterNative(LqHttpDmnTbl& Dmns, const char* WebDomen, LqHttpPthPtr& Path) {
	if(Path == &EmptyPth)
		return LQHTTPPTH_RES_NOT_ALLOC_MEM;
	if(!(Path->Type & LQHTTPPTH_FLAG_CHILD) && !Path->ParentModule->RegisterPathInDomenProc(Path.Get(), WebDomen))
		return LQHTTPPTH_RES_MODULE_REJECT;
	LqHttpPthResultEnm Result = LQHTTPPTH_RES_NOT_HAVE_DOMEN;

	auto DmnInter = Dmns.search(WebDomen);
	if(DmnInter.is_end())
		return LQHTTPPTH_RES_NOT_HAVE_DOMEN;
	switch((*DmnInter)->Pths.push_back_uniq(Path)) {
		case -1: return LQHTTPPTH_RES_NOT_ALLOC_MEM;
		case 0: return LQHTTPPTH_RES_ALREADY_HAVE;
	}
	return LQHTTPPTH_RES_OK;
}

void LqHttpPthAssign(LqHttpPth* Pth) {
	if(Pth == nullptr)
		return;
	LqObPtrReference(Pth);
}

void _LqHttpPthDelete(LqHttpPth* Pth) {
	if(Pth->Type & LQHTTPPTH_FLAG_CHILD)
		LqHttpPthRelease(Pth->Parent);
	else
		LqHttpMdlPathFree(Pth);

	LqHttpAtzRelease(Pth->Atz);
	switch(Pth->Type & LQHTTPPTH_TYPE_SEP) {
		case LQHTTPPTH_TYPE_DIR:
		case LQHTTPPTH_TYPE_FILE:
			LqCheckedFree(Pth->RealPath);
			break;
		case LQHTTPPTH_TYPE_DIR_REDIRECTION:
		case LQHTTPPTH_TYPE_FILE_REDIRECTION:
			LqCheckedFree(Pth->Location);
			break;
	}
	LqCheckedFree(Pth->WebPath);
	LqFastAlloc::Delete(Pth);
}

/* Dereference HTTP path */
bool LqHttpPthRelease(LqHttpPth* Pth) {
	if(Pth == nullptr)
		return false;
	return LqObPtrDereference<LqHttpPth, _LqHttpPthDelete>(Pth);
}

static LqHttpPthPtr LqHttpPthCreate
(
	const char* WebPath,
	uint8_t Type,
	void* Data,
	uint8_t Permissions,
	LqHttpAtz* Authoriz,
	LqHttpMdl* RegModule
) {
	LqHttpPth* ResultPth = LqFastAlloc::New<LqHttpPth>();
	if(ResultPth == nullptr)
		return &EmptyPth;
	memset(ResultPth, 0, sizeof(*ResultPth));
	ResultPth->ParentModule = RegModule;
	if(RegModule == nullptr)
		Type |= LQHTTPPTH_FLAG_CHILD;
	else
		LqHttpMdlPathRegister(RegModule, ResultPth);

	ResultPth->Type = Type;
	if(WebPath != nullptr) {
		switch(Type & LQHTTPPTH_TYPE_SEP) {
			case LQHTTPPTH_TYPE_DIR:
			case LQHTTPPTH_TYPE_EXEC_DIR:
			case LQHTTPPTH_TYPE_DIR_REDIRECTION:
			{
				auto l = LqStrLen(WebPath);
				if((ResultPth->WebPath = (char*)malloc(l + 3)) != nullptr) {
					memcpy(ResultPth->WebPath, WebPath, l);
					if(ResultPth->WebPath[l - 1] == '?') {
						ResultPth->WebPath[l] = '\0';
					} else {
						ResultPth->WebPath[l] = '?';
						ResultPth->WebPath[l + 1] = '\0';
					}
				}
			}
			break;
			case LQHTTPPTH_TYPE_EXEC_FILE:
			case LQHTTPPTH_TYPE_FILE:
			case LQHTTPPTH_TYPE_FILE_REDIRECTION:
				ResultPth->WebPath = LqStrDuplicate(WebPath);
				break;
		}
		if(ResultPth->WebPath == nullptr) {
			LqHttpPthRelease(ResultPth);
			return &EmptyPth;
		}
		ResultPth->WebPathHash = StringHash<decltype(ResultPth->WebPathHash)>(ResultPth->WebPath);
	}
	switch(Type & LQHTTPPTH_TYPE_SEP) {
		case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
		{
			if(Data != nullptr) {
				size_t CurLen = (LQ_MAX_PATH + 1024) * sizeof(char);
				char* Buf = (char*)malloc(CurLen);
				if((Buf == nullptr) || (LqFileRealPath((const char*)Data, Buf, CurLen - 1) == -1)) {
					LqHttpPthRelease(ResultPth);
					return &EmptyPth;
				}
				CurLen = LqStrLen(Buf) + 2;
				ResultPth->RealPath = (char*)realloc(Buf, CurLen);
			} else {
				ResultPth->RealPath = nullptr;
			}
		}
		break;
		case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
		{
			if(Data != nullptr)
				ResultPth->ExecQueryProc = (decltype(ResultPth->ExecQueryProc))Data;
			else
				ResultPth->ExecQueryProc = nullptr;
		}
		break;
		case LQHTTPPTH_TYPE_FILE_REDIRECTION: case LQHTTPPTH_TYPE_DIR_REDIRECTION:
		{
			if(Data != nullptr) {
				ResultPth->Location = LqStrDuplicate((const char*)Data);
				if(ResultPth->Location == nullptr) {
					LqHttpPthRelease(ResultPth);
					return &EmptyPth;
				}
			} else {
				ResultPth->Location = nullptr;
			}
		}
		break;
	}

	LqObPtrInit(ResultPth->Atz, Authoriz, ResultPth->AtzPtrLk);
	ResultPth->Permissions = Permissions;
	if(RegModule != nullptr)
		RegModule->CreatePathProc(ResultPth);
	return ResultPth;
}

static LqHttpPthPtr LqHttpPthGetFileByDir(LqHttpConn* HttpConn, int CountSubDirs) {
	LqHttpConnData* HttpConnData;
	LqHttpPth* Pth;
	LqHttpRcvHdrs* Query;
	size_t RealPathLen;
	int i, Deep = 0, n;
	char* s, *m, *d;

	HttpConnData = LqHttpConnGetData(HttpConn);
	Pth = HttpConnData->Pth;
	Query = HttpConnData->RcvHdr;
	RealPathLen = LqStrLen(Pth->RealPath);
	n = i = LqStrLen(Query->Path);

	for(int k = 0; i >= 0; i--)
		if(Query->Path[i] == '/') {
			if(Query->Path[i + 1] == '.') {
				Deep = -1;
				break;
			} else
				Deep++;
			if(++k == CountSubDirs)
				break;
		}
	if((Deep < 0) || (n == ++i))
		return &EmptyPth;
	LqHttpPthPtr NewPth = LqHttpPthCreate(nullptr, LQHTTPPTH_TYPE_FILE, nullptr, Pth->Permissions, Pth->Atz, nullptr);
	if(NewPth == nullptr)
		return &EmptyPth;
	if((NewPth->RealPath = (char*)malloc(RealPathLen + (n - i) + 3)) == NULL)
		return &EmptyPth;
	s = Pth->RealPath;
	d = NewPth->RealPath;
	for(; *s; s++, d++)
		*d = *s;
	if((s > Pth->RealPath) && (*(s - 1) != LQ_PATH_SEPARATOR))
		*d = LQ_PATH_SEPARATOR, d++;
	s = Query->Path + i;
	m = s + (n - i);
	if(LQ_PATH_SEPARATOR != '/')
	{
		for(; s < m; s++, d++)
			*d = (*s == '/') ? '\\' : *s;
	} else {
		for(; s < m; s++, d++)
			*d = *s;
	}
	*d = '\0';
	return NewPth;
}

static LqHttpPthPtr LqHttpPthGetFileRedirectionByDirRedirection(LqHttpConn* HttpConn, int CountSubDirs) {
	LqHttpConnData* HttpConnData;
	LqHttpRcvHdrs* Query;
	LqHttpPth* Pth;
	int i, n, Deep = 0;
	size_t LocationLen;
	HttpConnData = LqHttpConnGetData(HttpConn);
	Pth = HttpConnData->Pth;
	Query = HttpConnData->RcvHdr;
	LocationLen = LqStrLen(Pth->Location);
	n = i = LqStrLen(Query->Path);

	for(int k = 0; i >= 0; i--)
		if(Query->Path[i] == '/') {
			if(Query->Path[i + 1] == '.') {
				Deep = -1;
				break;
			} else
				Deep++;
			if(++k == CountSubDirs)
				break;
		}

	if((Deep < 0) || (n == ++i))
		return &EmptyPth;
	LqHttpPthPtr NewPth = LqHttpPthCreate(nullptr, LQHTTPPTH_TYPE_FILE_REDIRECTION, nullptr, Pth->Permissions, Pth->Atz, nullptr);
	if(NewPth == &EmptyPth)
		return &EmptyPth;
	if((NewPth->Location = (char*)malloc(LocationLen + (n - i) + 3)) == nullptr)
		return &EmptyPth;
	NewPth->StatusCode = Pth->StatusCode;
	LqStrCopy(NewPth->Location, Pth->Location);
	if((LocationLen == 0) || (Pth->Location[LocationLen - 1] != '/'))
		strcat(NewPth->Location, "/");
	strncat(NewPth->Location, Query->Path + i, n - i);
	return NewPth;
}

void LqHttpConnPthRemove(LqHttpConn* HttpConn) {
	LqHttpConnData* HttpConnData;
	HttpConnData = LqHttpConnGetData(HttpConn);
	if(HttpConnData->Pth != NULL) {
		LqHttpPthRelease(HttpConnData->Pth);
		HttpConnData->Pth = NULL;
	}
}


void LqHttpPthRecognize(LqHttpConn* HttpConn) {
	LqHttpConnData* HttpConnData;
	LqHttpRcvHdrs* Query;
	LqHttpData* HttpData;
	LqHttp* Http;
	int CountSubDirs;

	HttpConnData = LqHttpConnGetData(HttpConn);
	HttpData = LqHttpConnGetHttpData(HttpConn);
	Http = LqHttpConnGetHttp(HttpConn);

	Query = HttpConnData->RcvHdr;
	CountSubDirs = 0;
	if(Query->Host != NULL) {
		HttpConnData->Pth = LqHttpPthGetByAddressSubdirCheck(Http, Query->Host, Query->Path, &CountSubDirs);
	} else {
		HttpConnData->Pth = LqHttpPthGetByAddressSubdirCheck(Http, "*", Query->Path, &CountSubDirs);
	}
	if(HttpConnData->Pth != NULL) {
		switch(HttpConnData->Pth->Type & LQHTTPPTH_TYPE_SEP) {
			case LQHTTPPTH_TYPE_DIR:
			{
				LqHttpPthPtr Pth = LqHttpPthGetFileByDir(HttpConn, CountSubDirs);
				if(Pth != &EmptyPth) {
					Pth->Parent = HttpConnData->Pth;
					HttpConnData->Pth = Pth.Get();
					LqHttpPthAssign(HttpConnData->Pth);
					Pth->Type |= LQHTTPPTH_FLAG_CHILD;
				} else {
					LqHttpConnPthRemove(HttpConn);
				}
			}
			break;
			case LQHTTPPTH_TYPE_DIR_REDIRECTION:
			{
				LqHttpPthPtr Pth = LqHttpPthGetFileRedirectionByDirRedirection(HttpConn, CountSubDirs);
				if(Pth != &EmptyPth) {
					Pth->Parent = HttpConnData->Pth;
					HttpConnData->Pth = Pth.Get();
					LqHttpPthAssign(HttpConnData->Pth);
					Pth->Type |= LQHTTPPTH_FLAG_CHILD;
				} else {
					LqHttpConnPthRemove(HttpConn);
				}
			}
			break;
		}
	}
}

static void CalcFileMD5(int FileDescriptor, void* HashDest) {
	LqCryptHash md5;
	LqCryptHashOpen(&md5, "md5");
	char Buf[1024];
	intptr_t Readed;
	while((Readed = LqFileRead(FileDescriptor, Buf, sizeof(Buf))) > 0)
		LqCryptHashUpdate(&md5, Buf, Readed);
	LqCryptHashFinal(&md5, HashDest);
}
