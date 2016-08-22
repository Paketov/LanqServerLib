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

#include "LqHttpPth.hpp"
#include "LqHttp.hpp"
#include "LqHttpAtz.h"
#include "LqHttpMdl.h"
#include "LqHttpConn.h"


#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#define LqCheckedFree(MemReg) (((MemReg) != nullptr)? free(MemReg): void())

static LqHttpPthResultEnm LqHttpPthRegisterNative(LqHttpFileSystem& fs, const char* WebDomen, LqHttpPth* Path);
static LqHttpPthResultEnm LqHttpPthRegisterNative(LqHttpDomainPaths* WebDomen, LqHttpPth* Path);

static LqHttpPth* LqHttpPthCreate
(
    const char* WebPath,
    uint8_t Type,
    void* Data,
    uint8_t DefaultAccess,
    LqHttpAtz* Authoriz,
    LqHttpMdl* RegModule
);

template<typename T>
static inline T StringHash(const char * Str)
{
    T h = 0;
    for(const char* k = Str; *k != '\0'; k++) h = 31 * h + *k;
    return h;
}

bool LqHttpDomainPaths::Element::SetKey(const LqHttpPth* NewKey) { p = (decltype(p))NewKey; LqHttpPthAssign(p);  return true; }
size_t LqHttpDomainPaths::Element::IndexByKey(const LqHttpPth* Key, size_t MaxCount) { return Key->WebPathHash % MaxCount; }
size_t LqHttpDomainPaths::Element::IndexByKey(const char* Key, size_t MaxCount) { return StringHash<size_t>(Key) % MaxCount; }
size_t LqHttpDomainPaths::Element::IndexInBound(size_t MaxCount) const { return p->WebPathHash % MaxCount; }
bool LqHttpDomainPaths::Element::CmpKey(const LqHttpPth* Key) const { return (Key == p) || ((Key->WebPathHash == p->WebPathHash) && LqStrSame(Key->WebPath, p->WebPath)); }
bool LqHttpDomainPaths::Element::CmpKey(const char* Key) const { return LqStrSame(Key, p->WebPath); }

bool LqHttpDomainPaths::SetKey(const char* Name)
{
    auto l = LqStrLen(Name);
    NameDomain = (char*)malloc(l + 1);
    LqStrUtf8ToLower(NameDomain, l + 1, Name, -1);
    NameHash = StringHash<decltype(NameHash)>(Name);
    return true;
}
size_t LqHttpDomainPaths::IndexByKey(const char* Key, size_t MaxCount)
{
    decltype(NameHash) h = 0;
    for(const char* k = Key; *k != '\0';)
        h = 31 * h + ((*k >= 'a') && (*k <= 'z')) ? (uint32_t)*(k++) : LqStrUtf8ToLowerChar(&k, -1);
    return h % MaxCount;
}
size_t LqHttpDomainPaths::IndexInBound(size_t MaxCount) const { return NameHash % MaxCount; }
bool LqHttpDomainPaths::CmpKey(const char* Key) const
{
    return LqStrUtf8CmpCase(Key, NameDomain);
}
LqHttpDomainPaths::LqHttpDomainPaths(): NameDomain(nullptr), NameHash(0) {}
LqHttpDomainPaths::~LqHttpDomainPaths() { if(NameDomain != nullptr) free(NameDomain); }

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterFile
(
    LqHttpProtoBase* Reg,
    LqHttpMdl* RegModule,
    const char* WebDomen,
    const char* WebPath,
    const char* RealPath,
    uint8_t Permissions,
    LqHttpAtz* Autoriz,
    uintptr_t ModuleData
)
{
    auto r = LqHttpPthCreate
    (
        WebPath,
        LQHTTPPTH_TYPE_FILE,
        (char*)RealPath,
        Permissions,
        Autoriz,
        (RegModule == nullptr) ? &Reg->StartModule : RegModule
    );
    if(r == nullptr)
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;
    r->ModuleData = ModuleData;
    auto t = LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, WebDomen, r);
    LqHttpPthRelease(r);
    return t;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterDir
(
    LqHttpProtoBase* Reg,
    LqHttpMdl* RegModule,
    const char* WebDomen,
    const char* WebPath,
    const char* RealPath,
    bool IsIncludeSubdirs,
    uint8_t Permissions,
    LqHttpAtz* Autoriz,
    uintptr_t ModuleData
)
{
    auto l = LqStrLen(WebPath);
    if((l == 0) || (WebPath[l - 1] != '/'))
        return LQHTTPPTH_RES_NOT_DIR;
    auto r = LqHttpPthCreate
    (
        WebPath,
        LQHTTPPTH_TYPE_DIR | ((IsIncludeSubdirs) ? LQHTTPPTH_FLAG_SUBDIR : 0),
        (char*)RealPath,
        Permissions,
        Autoriz,
        (RegModule == nullptr) ? &Reg->StartModule : RegModule
    );
    if(r == nullptr)
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;
    r->ModuleData = ModuleData;
    auto t = LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, WebDomen, r);
    LqHttpPthRelease(r);
    return t;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterExecFile
(
    LqHttpProtoBase* Reg,
    LqHttpMdl* RegModule,
    const char* WebDomen,
    const char* WebPath,
    LqHttpEvntHandlerFn ExecQueryProc,
    uint8_t Permissions,
    LqHttpAtz* Autoriz,
    uintptr_t ModuleData
)
{
    auto r = LqHttpPthCreate
    (
        WebPath,
        LQHTTPPTH_TYPE_EXEC_FILE,
        (void*)ExecQueryProc,
        Permissions,
        Autoriz,
        (RegModule == nullptr) ? &Reg->StartModule : RegModule
    );
    if(r == nullptr)
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;
    r->ModuleData = ModuleData;
    auto t = LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, WebDomen, r);
    LqHttpPthRelease(r);
    return t;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterExecDir
(
    LqHttpProtoBase* Reg,
    LqHttpMdl* RegModule,
    const char* WebDomen,
    const char* WebPath,
    bool IsIncludeSubdirs,
    LqHttpEvntHandlerFn ExecQueryProc,
    uint8_t Permissions,
    LqHttpAtz* Autoriz,
    uintptr_t ModuleData
)
{
    auto l = LqStrLen(WebPath);
    if((l == 0) || (WebPath[l - 1] != '/'))
        return LQHTTPPTH_RES_NOT_DIR;

    auto r = LqHttpPthCreate
    (
        WebPath,
        LQHTTPPTH_TYPE_EXEC_DIR | ((IsIncludeSubdirs) ? LQHTTPPTH_FLAG_SUBDIR : 0),
        (void*)ExecQueryProc,
        Permissions,
        Autoriz,
        (RegModule == nullptr) ? &Reg->StartModule : RegModule
    );
    if(r == nullptr)
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;
    r->ModuleData = ModuleData;
    auto t = LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, WebDomen, r);
    LqHttpPthRelease(r);
    return t;
}


LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterFileRedirection
(
    LqHttpProtoBase* Reg,
    LqHttpMdl* RegModule,
    const char* WebDomen,
    const char* WebPath,
    const char* Location,
    short ResponseStatus,
    uint8_t Permissions,
    LqHttpAtz* Autoriz,
    uintptr_t ModuleData
)
{
    auto r = LqHttpPthCreate
    (
        WebPath,
        LQHTTPPTH_TYPE_FILE_REDIRECTION,
        nullptr,
        Permissions,
        Autoriz,
        (RegModule == nullptr) ? &Reg->StartModule : RegModule
    );
    if(r == nullptr)
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;

    r->ModuleData = ModuleData;
    r->StatusCode = ResponseStatus;
    auto t = LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, WebDomen, r);
    LqHttpPthRelease(r);
    return t;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegisterDirRedirection
(
    LqHttpProtoBase* Reg,
    LqHttpMdl* RegModule,
    const char* WebDomen,
    const char* WebPath,
    const char* Location,
    short ResponseStatus,
    uint8_t Permissions,
    LqHttpAtz* Autoriz,
    uintptr_t ModuleData
)
{
    auto l = LqStrLen(WebPath);
    if((l == 0) || (WebPath[l - 1] != '/'))
        return LQHTTPPTH_RES_NOT_DIR;
    auto r = LqHttpPthCreate
    (
        WebPath,
        LQHTTPPTH_TYPE_DIR_REDIRECTION | LQHTTPPTH_FLAG_SUBDIR,
        (void*)Location,
        Permissions,
        Autoriz,
        (RegModule == nullptr) ? &Reg->StartModule : RegModule
    );
    if(r == nullptr)
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;
    r->ModuleData = ModuleData;
    r->StatusCode = ResponseStatus;
    auto t = LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, WebDomen, r);
    LqHttpPthRelease(r);
    return t;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthCopyFile
(
    LqHttpProtoBase* Reg,
    const char* WebDomenDest,
    const char* WebDomenSource,
    const char* WebPath
)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    auto r = LQHTTPPTH_RES_OK;
    FileSystem.l.LockWriteYield();
    LQ_BREAK_BLOCK_BEGIN
        auto DmnSrc = FileSystem.t.Search(WebDomenSource);
    auto DmnDest = FileSystem.t.Search(WebDomenDest);
    if((DmnSrc == nullptr) || (DmnDest == nullptr))
    {
        r = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
        break;
    }
    auto Pth = DmnSrc->t.Search(WebPath);
    if(Pth == nullptr)
    {
        r = LQHTTPPTH_RES_NOT_HAVE_PATH;
        break;
    }
    r = LqHttpPthRegisterNative(DmnDest, Pth->p);
    LQ_BREAK_BLOCK_END
        FileSystem.l.UnlockWrite();
    return r;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthCopyDir
(
    LqHttpProtoBase* Reg,
    const char* WebDomenDest,
    const char* WebDomenSource,
    const char* WebPath
)
{
    LqString TempPathStr = WebPath;
    TempPathStr.append("?", 1);
    return LqHttpPthCopyFile(Reg, WebDomenDest, WebDomenSource, TempPathStr.c_str());
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthUnregisterFile
(
    LqHttpProtoBase* Reg,
    const char* WebDomen,
    const char* WebPath
)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    auto r = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
    FileSystem.l.LockWriteYield();
    LQ_BREAK_BLOCK_BEGIN
        auto Dmn = FileSystem.t.Search(WebDomen);
    if(Dmn == nullptr)
    {
        r = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
        break;
    }
    auto Pth = Dmn->t.Search(WebPath);
    if(Pth == nullptr)
    {
        r = LQHTTPPTH_RES_NOT_HAVE_PATH;
        break;
    }
    Pth->p->ParentModule->UnregisterPathFromDomenProc(Pth->p, WebDomen);

    auto OldTableElement = Dmn->t.RemoveRetPointer(WebPath);
    LqHttpPthRelease(OldTableElement->p);
    OldTableElement->p = nullptr;
    Dmn->t.DeleteRetPointer(OldTableElement);

    r = LQHTTPPTH_RES_OK;
    if((size_t)(Dmn->t.Count() * 1.7f) < Dmn->t.AllocCount())
        Dmn->t.ResizeAfterRemove();
    LQ_BREAK_BLOCK_END
        FileSystem.l.UnlockWrite();
    return r;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthUnregisterDir
(
    LqHttpProtoBase* Reg,
    const char* WebDomen,
    const char* WebPath
)
{
    LqString TempPathStr = WebPath;
    TempPathStr.append("?", 1);
    return LqHttpPthUnregisterFile(Reg, WebDomen, TempPathStr.c_str());
}


static LqHttpPthResultEnm LqHttpPthFileSetUnsetAtz
(
    LqHttpProtoBase* Reg,
    const char* WebDomen,
    const char* WebPath,
    LqHttpAtz* Atz,
    bool IsReplaceAtz,
    bool IsSetAtz,
    uint8_t Perm,
    bool IsSetPerm
)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    auto r = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
    LqHttpPth* ReleasePth = nullptr;
    FileSystem.l.LockWriteYield();
    LQ_BREAK_BLOCK_BEGIN
        auto Dmn = FileSystem.t.Search(WebDomen);
    if(Dmn == nullptr)
    {
        r = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
        break;
    }
    auto Pth = Dmn->t.Search(WebPath);
    if(Pth == nullptr)
    {
        r = LQHTTPPTH_RES_NOT_HAVE_PATH;
        break;
    }
    auto RmPath = Pth->p;
    if(IsSetAtz)
    {
        if(Atz == nullptr)
        {
            if(RmPath->Atz == nullptr)
            {
                r = LQHTTPPTH_RES_NOT_HAVE_ATZ;
                break;
            }
        } else
        {
            if(!IsReplaceAtz && (RmPath->Atz != nullptr))
            {
                r = LQHTTPPTH_RES_ALREADY_HAVE_ATZ;
                break;
            }
        }
    }
    void* Data = nullptr;
    switch(RmPath->Type & LQHTTPPTH_TYPE_SEP)
    {
        case LQHTTPPTH_TYPE_DIR:
        case LQHTTPPTH_TYPE_FILE:
            Data = RmPath->RealPath;
            break;
        case LQHTTPPTH_TYPE_DIR_REDIRECTION:
        case LQHTTPPTH_TYPE_FILE_REDIRECTION:
            Data = RmPath->Location;
            break;
        case LQHTTPPTH_TYPE_EXEC_DIR:
        case LQHTTPPTH_TYPE_EXEC_FILE:
            Data = (void*)RmPath->ExecQueryProc;
            break;
    }
    auto ResPath = LqHttpPthCreate(WebPath, RmPath->Type, Data, (IsSetPerm) ? Perm : RmPath->Permissions, (IsSetAtz) ? Atz : RmPath->Atz, RmPath->ParentModule);
    if(ResPath == nullptr)
    {
        r = LQHTTPPTH_RES_NOT_ALLOC_MEM;
        break;
    }
    ResPath->ModuleData = RmPath->ModuleData;
    switch(RmPath->Type & LQHTTPPTH_TYPE_SEP)
    {
        case LQHTTPPTH_TYPE_DIR_REDIRECTION:
        case LQHTTPPTH_TYPE_FILE_REDIRECTION:
            ResPath->StatusCode = RmPath->StatusCode;
            break;
    }

    /* Remove old path from main table*/
    auto OldTableElement = Dmn->t.RemoveRetPointer(RmPath);
    ReleasePth = OldTableElement->p;
    OldTableElement->p = nullptr;
    Dmn->t.DeleteRetPointer(OldTableElement);
    /* Register new path in table*/
    ResPath->Type |= LQHTTPPTH_FLAG_CHILD; /* Not call module proc*/
    r = LqHttpPthRegisterNative(Dmn, ResPath);
    ResPath->Type &= ~LQHTTPPTH_FLAG_CHILD;
    LQ_BREAK_BLOCK_END
    FileSystem.l.UnlockWrite();
    LqHttpPthRelease(ReleasePth);//Should remove over safe block
    return r;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDirSetAtz(LqHttpProtoBase* Reg, const char* WebDomen, const char* WebPath, LqHttpAtz* Atz, bool IsReplace)
{
    LqString TempPathStr = WebPath;
    TempPathStr.append("?", 1);
    return LqHttpPthFileSetUnsetAtz(Reg, WebDomen, TempPathStr.c_str(), Atz, IsReplace, true, 0, false);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthFileSetAtz(LqHttpProtoBase* Reg, const char* WebDomen, const char* WebPath, LqHttpAtz* Atz, bool IsReplace)
{
    return LqHttpPthFileSetUnsetAtz(Reg, WebDomen, WebPath, Atz, IsReplace, true, 0, false);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDirSetPerm(LqHttpProtoBase* Reg, const char* WebDomen, const char* WebPath, uint8_t Permissions)
{
    LqString TempPathStr = WebPath;
    TempPathStr.append("?", 1);
    return LqHttpPthFileSetUnsetAtz(Reg, WebDomen, TempPathStr.c_str(), nullptr, false, false, Permissions, true);
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthFileSetPerm(LqHttpProtoBase* Reg, const char* WebDomen, const char* WebPath, uint8_t Permissions)
{
    return LqHttpPthFileSetUnsetAtz(Reg, WebDomen, WebPath, nullptr, false, false, Permissions, true);
}

/*
* Create domen
*/
LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDmnCreate(LqHttpProtoBase* Reg, const char* WebDomen)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    auto r = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
    FileSystem.l.LockWriteYield();
    LQ_BREAK_BLOCK_BEGIN
        if(FileSystem.t.Search(WebDomen) != nullptr)
        {
            r = LQHTTPPTH_RES_ALREADY_HAVE;
            break;
        }
    if(FileSystem.t.IsFull())
        FileSystem.t.ResizeBeforeInsert((FileSystem.t.Count() < 3) ? 3 : (size_t)(FileSystem.t.Count() * 1.61803398875f));
    char HostBuf[LQHTTPPTH_MAX_DOMEN_NAME];
    if(auto r = LqStrUtf8ToLower(HostBuf, sizeof(HostBuf) - 1, WebDomen, -1))
    {
        if(r >= (HostBuf + sizeof(HostBuf)))
        {
            FileSystem.l.UnlockWrite();
            return LQHTTPPTH_RES_DOMEN_NAME_OVERFLOW;
        }
        *r = '\0';
    } else
    {
        FileSystem.l.UnlockWrite();
        return LQHTTPPTH_RES_INVALID_NAME;
    }
    r = (FileSystem.t.Insert(HostBuf) != nullptr) ? LQHTTPPTH_RES_OK : LQHTTPPTH_RES_NOT_ALLOC_MEM;
    LQ_BREAK_BLOCK_END
        FileSystem.l.UnlockWrite();
    return r;
}

LQ_EXTERN_C bool LQ_CALL LqHttpPthDmnEnm(LqHttpProtoBase* Reg, char* WebDomen, size_t WebDomenLen)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    FileSystem.l.LockReadYield();
    if(auto i = ((WebDomen[0] == '\0') ? FileSystem.t.GetStartCell() : FileSystem.t.GetNextCellByKey(WebDomen)))
    {
        LqStrCopyMax(WebDomen, i->NameDomain, WebDomenLen);
        FileSystem.l.UnlockRead();
        return true;
    }
    FileSystem.l.UnlockRead();
    return false;
}


LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthDmnDelete(LqHttpProtoBase* Reg, const char* WebDomen)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    FileSystem.l.LockWriteYield();
    auto d = FileSystem.t.Search(WebDomen);
    struct LocalFunctions
    {
        static bool EnmDomain(void* DomainName, LqHttpDomainPaths::Element* e)
        {
            e->p->ParentModule->UnregisterPathFromDomenProc(e->p, (const char*)DomainName);
            LqHttpPthRelease(e->p);
            return true;
        }
    };

    if(d != nullptr)
    {
        d->t.EnumDelete(LocalFunctions::EnmDomain, (void*)WebDomen);
        FileSystem.t.Remove(WebDomen);
        if((size_t)(FileSystem.t.Count() * 1.7f) < FileSystem.t.AllocCount())
            FileSystem.t.ResizeAfterRemove();
    }
    FileSystem.l.UnlockWrite();
    return (d == nullptr) ? LQHTTPPTH_RES_NOT_HAVE_DOMEN : LQHTTPPTH_RES_OK;
}

/*
* Note!
* Argument @Pth - not thread safe!
* if @Pth use of multiple thread, then  copy this string to temp local buffer before call.
* This not doing in func for improve performance.
*/
static LqHttpPth* LqHttpPthGetByAddressSubdirCheck
(
    LqHttpProtoBase* Reg,
    const char* Domain,
    char* Path,
    uint* DeepSubDirs
)
{
    uint k = 0;
    char c, c2;
    LqHttpDomainPaths::Element* Pth = nullptr;
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    FileSystem.l.LockReadYield();
    LQ_BREAK_BLOCK_BEGIN
    auto Dmn = FileSystem.t.Search(Domain);
    if(Dmn == nullptr)
    {
        if(Reg->UseDefaultDmn)
            Dmn = FileSystem.t.Search("*");
        else
            break;
    }
    Pth = Dmn->t.Search(Path);
    if(Pth != nullptr)
        break;
    for(int l = LqStrLen(Path) - 1; l >= 0; l--)
        if(Path[l] == '/')
        {
            k++;
            c = Path[l + 1];
            Path[l + 1] = '?';
            c2 = Path[l + 2];
            Path[l + 2] = '\0';
            Pth = Dmn->t.Search(Path);
            Path[l + 1] = c;
            Path[l + 2] = c2;
            if(Pth != nullptr)
            {
                if((k > 1) && !(Pth->p->Type & LQHTTPPTH_FLAG_SUBDIR))
                    continue;
                break;
            }
        }
    LQ_BREAK_BLOCK_END
    if(Pth != nullptr)
    {
        LqHttpPthAssign(Pth->p);
        FileSystem.l.UnlockRead();
        *DeepSubDirs = k;
        return Pth->p;
    } else
    {
        FileSystem.l.UnlockRead();
        return nullptr;
    }
}

static LqHttpPth* LqHttpPthGetByAddress(LqHttpProtoBase* Reg, const char* Domain, char* Path)
{
    char c, c2;
    LqHttpDomainPaths::Element* Pth = nullptr;
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    FileSystem.l.LockReadYield();
    LQ_BREAK_BLOCK_BEGIN
    auto Dmn = FileSystem.t.Search(Domain);
    if(Dmn == nullptr)
    {
        if(Reg->UseDefaultDmn)
            Dmn = FileSystem.t.Search("*");
        else
            break;
    }
    Pth = Dmn->t.Search(Path);
    if(Pth != nullptr)
        break;
    for(int l = LqStrLen(Path) - 1; l >= 0; l--)
        if(Path[l] == '/')
        {
            c = Path[l + 1];
            c2 = Path[l + 2];
            Path[l + 1] = '?';
            Path[l + 2] = '\0';
            Pth = Dmn->t.Search(Path);
            Path[l + 1] = c;
            Path[l + 2] = c2;
            if(Pth != nullptr)
                break;
        }
    LQ_BREAK_BLOCK_END
    if(Pth != nullptr)
    {
        LqHttpPthAssign(Pth->p); //Occupy   path struct
        FileSystem.l.UnlockRead();
        return Pth->p;
    } else
    {
        FileSystem.l.UnlockRead();
        return nullptr;
    }
}


/*
* Enumerate web paths for domen
*/
LQ_EXTERN_C bool LQ_CALL LqHttpPthEnm
(
    LqHttpProtoBase* Reg,
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
)
{
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    FileSystem.l.LockRead();
    auto r = FileSystem.t.Search(Domain);
    if(r == nullptr)
    {
        FileSystem.l.UnlockRead();
        return false;
    }
    if(auto i = ((WebPath[0] == '\0') ? r->t.GetStartCell() : r->t.GetNextCellByKey((char*)WebPath)))
    {
        const LqHttpPth* Pth = i->p;
        auto l = LqStrLen(WebPath);
        LqStrCopyMax(WebPath, Pth->WebPath, PathLen);
        if(Type != nullptr)
            *Type = Pth->Type;
        switch(Pth->Type & LQHTTPPTH_TYPE_SEP)
        {
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
        if(ModuleOwner != nullptr)
        {
            if(Pth->ParentModule != nullptr)
                *ModuleOwner = Pth->ParentModule->Handle;
            else
                *ModuleOwner = 0;
        }
        if(ModuleName != nullptr)
        {
            ModuleName[0] = '\0';
            if(Pth->ParentModule != nullptr)
                LqStrCopyMax(ModuleName, Pth->ParentModule->Name, ModuleNameSize);
        }
        if(ModuleData != nullptr)
            *ModuleData = Pth->ModuleData;
        if(Permissions != nullptr)
            *Permissions = Pth->Permissions;
        if(AccessUserList != nullptr)
        {
            if(*AccessUserList = Pth->Atz)
                LqHttpAtzAssign(Pth->Atz);
        }
        FileSystem.l.UnlockRead();
        return true;
    }
    FileSystem.l.UnlockRead();
    return false;
}


/*
* Enumerate web paths for domen
*/
LQ_EXTERN_C bool LQ_CALL LqHttpPthInfo
(
    LqHttpProtoBase* Reg,
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
)
{
    auto l = LqStrLen(WebPath);
    LqString WebPathBuf;
    if(IsDir && ((l < 1) || (WebPath[l - 1] != '?')))
    {
        WebPathBuf = WebPath;
        WebPathBuf.append(1, '?');
        WebPath = WebPathBuf.c_str();
    }
    LqHttpDomainPaths::Element* p = nullptr;
    auto& FileSystem = ((LqHttpProto*)Reg)->FileSystem;
    FileSystem.l.LockReadYield();
    auto Dmn = FileSystem.t.Search(Domain);
    if(Dmn == nullptr)
    {
        FileSystem.l.UnlockRead();
        return false;
    }
    const LqHttpPth* Pth;
    if(auto v = Dmn->t.Search(WebPath))
    {
        Pth = v->p;
    } else
    {
        FileSystem.l.UnlockRead();
        return false;
    }
    if(Type != nullptr)
        *Type = Pth->Type;
    switch(Pth->Type & LQHTTPPTH_TYPE_SEP)
    {
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
    if(ModuleOwner != nullptr)
    {
        if(Pth->ParentModule != nullptr)
            *ModuleOwner = Pth->ParentModule->Handle;
        else
            *ModuleOwner = 0;
    }
    if(ModuleName != nullptr)
    {
        ModuleName[0] = '\0';
        if(Pth->ParentModule != nullptr)
            LqStrCopyMax(ModuleName, Pth->ParentModule->Name, ModuleNameSize);
    }
    if(ModuleData != nullptr)
        *ModuleData = Pth->ModuleData;
    if(Permissions != nullptr)
        *Permissions = Pth->Permissions;
    if(AccessUserList != nullptr)
    {
        if(*AccessUserList = Pth->Atz)
            LqHttpAtzAssign(Pth->Atz);
    }
    FileSystem.l.UnlockRead();
    return true;
}

LQ_EXTERN_C LqHttpPthResultEnm LQ_CALL LqHttpPthRegister(LqHttpProtoBase* Reg, const char* Domain, LqHttpPth* Path)
{
    return LqHttpPthRegisterNative(((LqHttpProto*)Reg)->FileSystem, Domain, Path);
}

static LqHttpPthResultEnm LqHttpPthRegisterNative(LqHttpFileSystem& FileSystem, const char* WebDomen, LqHttpPth* Path)
{
    LqHttpPthResultEnm Result = LQHTTPPTH_RES_NOT_HAVE_DOMEN;
    FileSystem.l.LockWrite();
    auto Dmn = FileSystem.t.Search(WebDomen);
    if(Dmn != nullptr)
        Result = LqHttpPthRegisterNative(Dmn, Path);
    FileSystem.l.UnlockWrite();
    return Result;
}

static LqHttpPthResultEnm LqHttpPthRegisterNative(LqHttpDomainPaths* WebDomen, LqHttpPth* Path)
{
    if(WebDomen->t.IsFull())
        WebDomen->t.ResizeBeforeInsert((WebDomen->t.Count() < 3) ? 3 : (size_t)(WebDomen->t.Count() * 1.61803398875f));
    if(!(Path->Type & LQHTTPPTH_FLAG_CHILD) && !Path->ParentModule->RegisterPathInDomenProc(Path, WebDomen->NameDomain))
        return LQHTTPPTH_RES_MODULE_REJECT;
    auto j = WebDomen->t.Insert(Path);
    if(j != nullptr)
    {
        if(j->p != Path)
            return LQHTTPPTH_RES_ALREADY_HAVE;
        else
            return LQHTTPPTH_RES_OK;
    } else
        return LQHTTPPTH_RES_NOT_ALLOC_MEM;

    return LQHTTPPTH_RES_OK;
}

void LqHttpPthAssign(LqHttpPth* Pth)
{
    if(Pth == nullptr)
        return;
    LqAtmIntrlkInc(Pth->CountPointers);
}

bool LqHttpPthRelease(LqHttpPth* Pth)
{
    if(Pth == nullptr)
        return false;
    LqAtmIntrlkDec(Pth->CountPointers);
    if(Pth->CountPointers == 0)
    {
        if(Pth->Type & LQHTTPPTH_FLAG_CHILD)
            LqHttpPthRelease(Pth->Parent);
        else
            LqHttpMdlPathFree(Pth);
        LqHttpAtzRelease(Pth->Atz);
        switch(Pth->Type & LQHTTPPTH_TYPE_SEP)
        {
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
        if(Pth->Type & LQHTTPPTH_FLAG_CHILD)
            LqFastAlloc::Delete(Pth);
        else
            LqFastAlloc::Delete((LqHttpPathListHdr*)Pth);
        return true;
    }
    return false;
}

static LqHttpPth* LqHttpPthCreate
(
    const char* WebPath,
    uint8_t Type,
    void* Data,
    uint8_t Permissions,
    LqHttpAtz* Authoriz,
    LqHttpMdl* RegModule
)
{
    LqHttpPth* ResultPth;
    if(RegModule == nullptr)
    {
        ResultPth = LqFastAlloc::New<LqHttpPth>();
        if(ResultPth == nullptr)
            return nullptr;
        Type |= LQHTTPPTH_FLAG_CHILD;
    } else
    {
        auto l = LqFastAlloc::New<LqHttpPathListHdr>();
        if(l == nullptr)
            return nullptr;
        LqHttpMdlPathRegister(RegModule, l);
        ResultPth = &l->Path;
    }
    memset(ResultPth, 0, sizeof(*ResultPth));
    ResultPth->CountPointers = 1;
    ResultPth->Type = Type;
    ResultPth->ParentModule = RegModule;
    if(WebPath != nullptr)
    {
        switch(Type & LQHTTPPTH_TYPE_SEP)
        {
            case LQHTTPPTH_TYPE_DIR:
            case LQHTTPPTH_TYPE_EXEC_DIR:
            case LQHTTPPTH_TYPE_DIR_REDIRECTION:
            {
                auto l = LqStrLen(WebPath);
                if((ResultPth->WebPath = (char*)malloc(l + 3)) != nullptr)
                {
                    memcpy(ResultPth->WebPath, WebPath, l);
                    if(ResultPth->WebPath[l - 1] == '?')
                    {
                        ResultPth->WebPath[l] = '\0';
                    } else
                    {
                        ResultPth->WebPath[l] = '?';
                        ResultPth->WebPath[l + 1] = '\0';
                    }
                }
            }
            break;
            case LQHTTPPTH_TYPE_EXEC_FILE:
            case LQHTTPPTH_TYPE_FILE:
            case LQHTTPPTH_TYPE_FILE_REDIRECTION:
            {
                ResultPth->WebPath = LqStrDuplicate(WebPath);
            }
            break;
        }
        if(ResultPth->WebPath == nullptr)
        {
            LqHttpPthRelease(ResultPth);
            return nullptr;
        }
        ResultPth->WebPathHash = StringHash<decltype(ResultPth->WebPathHash)>(ResultPth->WebPath);
    }
    switch(Type & LQHTTPPTH_TYPE_SEP)
    {
        case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
        {
            if(Data != nullptr)
            {
                ResultPth->RealPath = LqStrDuplicate((const char*)Data);
                if(ResultPth->RealPath == nullptr)
                {
                    LqHttpPthRelease(ResultPth);
                    return nullptr;
                }
            } else
            {
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
            if(Data != nullptr)
            {
                ResultPth->Location = LqStrDuplicate((const char*)Data);
                if(ResultPth->Location == nullptr)
                {
                    LqHttpPthRelease(ResultPth);
                    return nullptr;
                }
            } else
            {
                ResultPth->Location = nullptr;
            }
        }
        break;
    }

    ResultPth->Atz = Authoriz;
    LqHttpAtzAssign(Authoriz);
    ResultPth->Permissions = Permissions;
    if(RegModule != nullptr)
        RegModule->CreatePathProc(ResultPth);
    return ResultPth;
}

static LqHttpPth* LqHttpPthGetFileByDir(LqHttpConn* c, uint CountSubDirs)
{
    auto Pth = c->Pth;
    auto Query = &c->Query;
    auto RealPathLen = LqStrLen(Pth->RealPath);
    int i = Query->PathLen;
    int Deep = 0;

    for(uint k = 0; i >= 0; i--)
        if(Query->Path[i] == '/')
        {
            if(Query->Path[i + 1] == '.')
            {
                Deep = -1;
                break;
            } else
                Deep++;
            if(++k == CountSubDirs)
                break;
        }
    if((Deep < 0) || (Query->PathLen == ++i))
        return nullptr;
    auto NewPth = LqHttpPthCreate(nullptr, LQHTTPPTH_TYPE_FILE, nullptr, Pth->Permissions, Pth->Atz, nullptr);
    if(NewPth == nullptr)
        return nullptr;
    if((NewPth->RealPath = (char*)malloc(RealPathLen + (Query->PathLen - i) + 3)) == nullptr)
    {
        LqHttpPthRelease(NewPth);
        return nullptr;
    }
    char* s = Pth->RealPath, *d = NewPth->RealPath;
    for(; *s; s++, d++)
        *d = *s;
    if((s > Pth->RealPath) && (*(s - 1) != LQ_PATH_SEPARATOR))
        *d = LQ_PATH_SEPARATOR, d++;
    s = Query->Path + i;
    auto m = s + (Query->PathLen - i);
    if(LQ_PATH_SEPARATOR != '/')
    {
        for(; s < m; s++, d++)
            *d = (*s == '/') ? '\\' : *s;
    } else
    {
        for(; s < m; s++, d++)
            *d = *s;
    }
    *d = '\0';
    return NewPth;
}

static LqHttpPth* LqHttpPthGetFileRedirectionByDirRedirection(LqHttpConn* c, uint CountSubDirs)
{
    auto Pth = c->Pth;
    auto Query = &c->Query;
    auto LocationLen = LqStrLen(Pth->Location);
    int Deep = 0;
    int i = Query->PathLen;

    for(uint k = 0; i >= 0; i--)
        if(Query->Path[i] == '/')
        {
            if(Query->Path[i + 1] == '.')
            {
                Deep = -1;
                break;
            } else
                Deep++;
            if(++k == CountSubDirs)
                break;
        }

    if((Deep < 0) || (Query->PathLen == ++i))
        return nullptr;
    auto NewPth = LqHttpPthCreate(nullptr, LQHTTPPTH_TYPE_FILE_REDIRECTION, nullptr, Pth->Permissions, Pth->Atz, nullptr);
    if(NewPth == nullptr)
        return nullptr;
    if((NewPth->Location = (char*)malloc(LocationLen + (Query->PathLen - i) + 3)) == nullptr)
    {
        LqHttpPthRelease(NewPth);
        return nullptr;
    }
    NewPth->StatusCode = Pth->StatusCode;
    LqStrCopy(NewPth->Location, Pth->Location);
    if((LocationLen == 0) || (Pth->Location[LocationLen - 1] != '/'))
        strcat(NewPth->Location, "/");
    strncat(NewPth->Location, Query->Path + i, Query->PathLen - i);
    return NewPth;
}

void LqHttpPthRecognize(LqHttpConn* c)
{
    auto Query = &c->Query;
    auto Proto = LqHttpGetReg(c);
    auto CountSubDirs = 0U;
    if(Query->Host != nullptr)
    {
        char t2 = Query->Path[Query->PathLen];
        Query->Path[Query->PathLen] = '\0';
        char t1 = Query->Host[Query->HostLen];
        Query->Host[Query->HostLen] = '\0';
        c->Pth = LqHttpPthGetByAddressSubdirCheck(&Proto->Base, Query->Host, Query->Path, &CountSubDirs);
        Query->Path[Query->PathLen] = t2;
        Query->Host[Query->HostLen] = t1;
    } else
    {
        char t = Query->Path[Query->PathLen];
        Query->Path[Query->PathLen] = '\0';
        c->Pth = LqHttpPthGetByAddressSubdirCheck(&Proto->Base, "*", Query->Path, &CountSubDirs);
        Query->Path[Query->PathLen] = t;
    }
    if(c->Pth != nullptr)
    {
        switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP)
        {
            case LQHTTPPTH_TYPE_DIR:
            {
                auto Pth = LqHttpPthGetFileByDir(c, CountSubDirs);
                if(Pth != nullptr)
                {
                    Pth->Parent = c->Pth;
                    c->Pth = Pth;
                    Pth->Type |= LQHTTPPTH_FLAG_CHILD;
                } else
                {
                    LqHttpConnPthRemove(c);
                }
            }
            break;
            case LQHTTPPTH_TYPE_DIR_REDIRECTION:
            {
                auto Pth = LqHttpPthGetFileRedirectionByDirRedirection(c, CountSubDirs);
                if(Pth != nullptr)
                {
                    Pth->Parent = c->Pth;
                    c->Pth = Pth;
                    Pth->Type |= LQHTTPPTH_FLAG_CHILD;
                } else
                {
                    LqHttpConnPthRemove(c);
                }
            }
            break;
        }
    }
}

static void CalcFileMD5(int FileDescriptor, LqMd5* HashDest)
{
    LqMd5Ctx md5;
    LqMd5Init(&md5);
    char Buf[1024];
    int Readed;
    while((Readed = LqFileRead(FileDescriptor, Buf, sizeof(Buf))) > 0)
        LqMd5Update(&md5, Buf, Readed);
    LqMd5Update(&md5, Buf, Readed);
    LqMd5Final((unsigned char*)HashDest, &md5);
}
