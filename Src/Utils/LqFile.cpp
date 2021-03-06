/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFile... - File layer between os and server.
*/

#include "LqOs.h"
#include "LqDef.hpp"
#include "LqFile.h"
#include "LqStr.h"
#include "LqErr.h"
#include "LqConn.h"


#include <string.h>
#include <signal.h>


#if defined(LQPLATFORM_WINDOWS)

#include <Windows.h>
#include <Winternl.h> //For LqPollCheck
#include <Psapi.h>
#include <ntstatus.h>
#include <vector>
#include "LqCp.h"
#include "LqAlloc.hpp"
#include "LqAtm.hpp"


extern "C" __kernel_entry NTSTATUS NTAPI NtReadFile(
    _In_     HANDLE           FileHandle,
    _In_opt_ HANDLE           Event,
    _In_opt_ PIO_APC_ROUTINE  ApcRoutine,
    _In_opt_ PVOID            ApcContext,
    _Out_    PIO_STATUS_BLOCK IoStatusBlock,
    _Out_    PVOID            Buffer,
    _In_     ULONG            Length,
    _In_opt_ PLARGE_INTEGER   ByteOffset,
    _In_opt_ PULONG           Key
);


extern "C" __kernel_entry NTSTATUS NTAPI NtWriteFile(
    _In_     HANDLE           FileHandle,
    _In_opt_ HANDLE           Event,
    _In_opt_ PIO_APC_ROUTINE  ApcRoutine,
    _In_opt_ PVOID            ApcContext,
    _Out_    PIO_STATUS_BLOCK IoStatusBlock,
    _In_     PVOID            Buffer,
    _In_     ULONG            Length,
    _In_opt_ PLARGE_INTEGER   ByteOffset,
    _In_opt_ PULONG           Key
);

extern "C" NTSYSAPI NTSTATUS NTAPI NtCancelIoFile(
    _In_ HANDLE               FileHandle,
    _Out_ PIO_STATUS_BLOCK    IoStatusBlock
);
#pragma pack(push,8)
typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;
#pragma pack(pop)
extern "C" __kernel_entry NTSTATUS NTAPI NtQueryInformationFile(
    __in HANDLE FileHandle,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __out_bcount(Length) PVOID FileInformation,
    __in ULONG Length,
    __in FILE_INFORMATION_CLASS FileInformationClass
);

extern "C" __kernel_entry NTSTATUS NTAPI NtSetInformationFile(
    __in HANDLE FileHandle,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __in_bcount(Length) PVOID FileInformation,
    __in ULONG Length,
    __in FILE_INFORMATION_CLASS FileInformationClass
);

extern "C" __kernel_entry NTSTATUS NTAPI NtNotifyChangeDirectoryFile(
    __in HANDLE FileHandle,
    __in_opt HANDLE Event,
    __in_opt PIO_APC_ROUTINE ApcRoutine,
    __in_opt PVOID ApcContext,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __out_bcount(Length) PVOID Buffer,
    __in ULONG Length,
    __in ULONG CompletionFilter,
    __in BOOLEAN WatchTree
);

typedef enum _EVENT_TYPE {
    NotificationEvent,
    SynchronizationEvent
} EVENT_TYPE, *PEVENT_TYPE;

extern "C" __kernel_entry NTSTATUS NTAPI NtCreateEvent(
    OUT PHANDLE             EventHandle,
    IN ACCESS_MASK          DesiredAccess,
    IN POBJECT_ATTRIBUTES   ObjectAttributes OPTIONAL,
    IN EVENT_TYPE           EventType,
    IN BOOLEAN              InitialState
);


extern "C" __kernel_entry NTSTATUS NTAPI NtSetEvent(
    IN HANDLE EventHandle,
    OUT PLONG PreviousState OPTIONAL
);

extern "C" __kernel_entry NTSTATUS NTAPI NtResetEvent(
    IN HANDLE               EventHandle,
    OUT PLONG               PreviousState OPTIONAL
);


extern "C" __kernel_entry NTSTATUS NTAPI NtLockFile(
    _In_     HANDLE           FileHandle,
    _In_opt_ HANDLE           Event,
    _In_opt_ PIO_APC_ROUTINE  ApcRoutine,
    _In_opt_ PVOID            ApcContext,
    _Out_    PIO_STATUS_BLOCK IoStatusBlock,
    _In_     PLARGE_INTEGER   ByteOffset,
    _In_     PLARGE_INTEGER   Length,
    _In_     ULONG            Key,
    _In_     BOOLEAN          FailImmediately,
    _In_     BOOLEAN          ExclusiveLock
);

extern "C" __kernel_entry NTSTATUS NTAPI NtUnlockFile(
    _In_  HANDLE           FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_  PLARGE_INTEGER   ByteOffset,
    _In_  PLARGE_INTEGER   Length,
    _In_  ULONG            Key
);

extern "C" __kernel_entry NTSTATUS NTAPI NtClearEvent(IN HANDLE EventHandle);


//NtQueryObject

int _LqFileConvertNameToWcs(const char* Name, wchar_t* DestBuf, size_t DestBufSize) {
    if((DestBufSize > 5) && (((Name[0] >= 'a') && (Name[0] <= 'z')) || ((Name[0] >= 'A') && (Name[0] <= 'Z')) && (Name[1] == ':') && (Name[2] == '\\'))) {
        memcpy(DestBuf, L"\\\\?\\", sizeof(L"\\\\?\\"));
        DestBufSize -= 4;
        auto l = LqCpConvertToWcs(Name, DestBuf + 4, DestBufSize);
        if(l < 0)
            return l;
        return l + 4;
    } else {
        return LqCpConvertToWcs(Name, DestBuf, DestBufSize);
    }
}

static int LqFileOpenFlagsToCreateFileFlags(int openFlags) {
    switch(openFlags & (LQ_O_CREATE | LQ_O_TRUNC | LQ_O_EXCL)) {
        case 0:
        case LQ_O_EXCL:
            return OPEN_EXISTING;
        case LQ_O_CREATE:
            return OPEN_ALWAYS;
        case LQ_O_TRUNC:
        case LQ_O_TRUNC | LQ_O_EXCL:
            return TRUNCATE_EXISTING;
        case LQ_O_CREATE | LQ_O_TRUNC:
            return CREATE_ALWAYS;
        case LQ_O_CREATE | LQ_O_EXCL:
        case LQ_O_CREATE | LQ_O_TRUNC | LQ_O_EXCL:
            return CREATE_NEW;
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqFileOpen(const char *FileName, uint32_t Flags, int Access) {
    HANDLE              ResultHandle;
    SECURITY_ATTRIBUTES InheritAttr = {sizeof(SECURITY_ATTRIBUTES), NULL, (Flags & LQ_O_NOINHERIT) ? FALSE : TRUE};
    wchar_t Name[LQ_MAX_PATH];

    _LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);
    if((
        ResultHandle = CreateFileW(
            Name,
            (Flags & LQ_O_RDWR) ? (GENERIC_WRITE | GENERIC_READ) :
            ((Flags & LQ_O_WR) ? GENERIC_WRITE : GENERIC_READ),
            (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
            &InheritAttr,
            LqFileOpenFlagsToCreateFileFlags(Flags),
            FILE_ATTRIBUTE_NORMAL |
            ((Flags & LQ_O_RND) ? FILE_FLAG_RANDOM_ACCESS : 0) |
            ((Flags & LQ_O_SEQ) ? FILE_FLAG_SEQUENTIAL_SCAN : 0) |
            ((Flags & LQ_O_SHORT_LIVED /*_O_SHORT_LIVED*/) ? FILE_ATTRIBUTE_TEMPORARY : 0) |
            ((Flags & LQ_O_TMP) ? FILE_FLAG_DELETE_ON_CLOSE : 0) |
            ((Flags & LQ_O_DSYNC) ? FILE_FLAG_WRITE_THROUGH : 0) |
            ((Flags & LQ_O_NONBLOCK) ? FILE_FLAG_OVERLAPPED : 0),
            NULL
        )
       ) == INVALID_HANDLE_VALUE)
        return -1;
    //if(
    //  ((fd = _open_osfhandle((long)h, (Flags & LQ_O_APND) ? O_APPEND : 0)) < 0) ||
    //  (Flags & (LQ_O_TXT | LQ_O_BIN) && (_setmode(fd, ((Flags & LQ_O_TXT) ? O_TEXT : 0) | ((Flags & LQ_O_BIN) ? O_BINARY : 0)) < 0))
    //  ) CloseHandle(h);
    return (int)ResultHandle;
}

LQ_EXTERN_C bool LQ_CALL LqFileIsExist(const char* FileName) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    wchar_t Name[LQ_MAX_PATH];
    _LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);
    return GetFileAttributesExW(Name, GetFileExInfoStandard, &info) == TRUE;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileGetPath(int Fd, char* DestBuf, intptr_t SizeBuf) {
    wchar_t Name[LQ_MAX_PATH];
    if(GetFinalPathNameByHandleW((HANDLE)Fd, Name, LQ_MAX_PATH, 0) == 0)
        return -1;
    return LqCpConvertFromWcs(Name, DestBuf, SizeBuf);
}

static inline LqTimeSec FileTimeToTimeSec(const FILETIME* ft) {
    ULARGE_INTEGER ull;
    ull.LowPart = ft->dwLowDateTime;
    ull.HighPart = ft->dwHighDateTime;
    return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

LQ_EXTERN_C int LQ_CALL LqFileGetStat(const char* FileName, LqFileStat* StatDest) {
    WIN32_FILE_ATTRIBUTE_DATA   info;
    wchar_t Name[LQ_MAX_PATH];

    _LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);
    if(GetFileAttributesExW(Name, GetFileExInfoStandard, &info) == FALSE)
        return -1;
    StatDest->CreateTime = FileTimeToTimeSec(&info.ftCreationTime);
    StatDest->AccessTime = FileTimeToTimeSec(&info.ftLastAccessTime);
    StatDest->ModifTime = FileTimeToTimeSec(&info.ftLastWriteTime);
    StatDest->RefCount = 0;
    StatDest->Size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    StatDest->DevId = 0;
    StatDest->Gid = 0;
    StatDest->Uid = 0;
    StatDest->Id = 0;
    if(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ARCHIVE))
        StatDest->Type = LQ_F_REG;
    else if(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        StatDest->Type = LQ_F_DIR;
    else
        StatDest->Type = LQ_F_OTHER;

    if(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        StatDest->Access = 0444;
    else
        StatDest->Access = 0666;
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqFileGetStatByFd(int Fd, LqFileStat* StatDest) {
    BY_HANDLE_FILE_INFORMATION info;
    if(GetFileInformationByHandle((HANDLE)Fd, &info) != TRUE)
        return -1;

    //StatDest->RefCount = info.nNumberOfLinks;
    //StatDest->DevId = info.dwVolumeSerialNumber;
    //{
    //  FILE_ID_INFO fid = {0};
    //  GetFileInformationByHandleEx((HANDLE)Fd, FileIdInfo, &fid, sizeof(fid));
    //  unsigned int h = 0;
    //  for(int i = 0; i < 16; i++) h = 31 * h + fid.FileId.Identifier[i];
    //  StatDest->Id = h;
    //}

    StatDest->CreateTime = FileTimeToTimeSec(&info.ftCreationTime);
    StatDest->AccessTime = FileTimeToTimeSec(&info.ftLastAccessTime);
    StatDest->ModifTime = FileTimeToTimeSec(&info.ftLastWriteTime);
    StatDest->RefCount = 0;
    StatDest->Size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    StatDest->DevId = 0;
    StatDest->Gid = 0;
    StatDest->Uid = 0;
    StatDest->Id = 0;
    if(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ARCHIVE))
        StatDest->Type = LQ_F_REG;
    else if(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        StatDest->Type = LQ_F_DIR;
    else
        StatDest->Type = LQ_F_OTHER;

    if(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        StatDest->Access = 0444;
    else
        StatDest->Access = 0666;
    return 0;
}

LQ_EXTERN_C LQ_NO_INLINE LqFileSz LQ_CALL LqFileTell(int Fd) {
    IO_STATUS_BLOCK iosb;
    long long CurPos;
    NTSTATUS Stat;
    if((Stat = NtQueryInformationFile((HANDLE)Fd, &iosb, &CurPos, sizeof(CurPos), (FILE_INFORMATION_CLASS)14)) != STATUS_SUCCESS) {
        SetLastError(RtlNtStatusToDosError(Stat));
        return -((LqFileSz)1);
    }
    return CurPos;
}

LQ_EXTERN_C LQ_NO_INLINE LqFileSz LQ_CALL LqFileSeek(int Fd, LqFileSz Offset, int Flag) {
    IO_STATUS_BLOCK iosb;
    NTSTATUS Stat;
    long long NewOffset;
    DWORD MoveMethod;
    FILE_STANDARD_INFORMATION fsinfo; /*FileStandardInformation*/

    NewOffset = Offset;
    switch(Flag) {
        case LQ_SEEK_END: {
            if((Stat = NtQueryInformationFile((HANDLE)Fd, &iosb, &fsinfo, sizeof(fsinfo), (FILE_INFORMATION_CLASS)5)) != STATUS_SUCCESS) {
                SetLastError(RtlNtStatusToDosError(Stat));
                return -((LqFileSz)1);
            }
            NewOffset = fsinfo.EndOfFile.QuadPart - NewOffset;
            break;
        }
        case LQ_SEEK_SET: break;
        default:
        case LQ_SEEK_CUR: {
            auto CurPos = LqFileTell(Fd);
            if(CurPos == -((LqFileSz)1))
                return -((LqFileSz)1);
            NewOffset += CurPos;
        }
        break;
    }
    if(NewOffset < 0) {
        SetLastError(ERROR_NEGATIVE_SEEK);
        return -((LqFileSz)1);
    }
    if((Stat = NtSetInformationFile((HANDLE)Fd, &iosb, &NewOffset, sizeof(NewOffset), (FILE_INFORMATION_CLASS)14)) != STATUS_SUCCESS) {
        SetLastError(RtlNtStatusToDosError(Stat));
        return -((LqFileSz)1);
    }
    return NewOffset;
}

LQ_EXTERN_C int LQ_CALL LqFileFlush(int Fd) {
    return (FlushFileBuffers((HANDLE)Fd) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL __LqStdErrFileNo() {
    HANDLE Res = GetStdHandle(STD_ERROR_HANDLE);
    if(Res == INVALID_HANDLE_VALUE)
        return -1;
    return (int)Res;
}

LQ_EXTERN_C int LQ_CALL __LqStdOutFileNo() {
    HANDLE Res = GetStdHandle(STD_OUTPUT_HANDLE);
    if(Res == INVALID_HANDLE_VALUE)
        return -1;
    return (int)Res;
}

LQ_EXTERN_C int LQ_CALL __LqStdInFileNo() {
    HANDLE Res = GetStdHandle(STD_INPUT_HANDLE);
    if(Res == INVALID_HANDLE_VALUE)
        return -1;
    return (int)Res;
}

LQ_EXTERN_C int LQ_CALL LqFileClose(int Fd) {
    return (NtClose((HANDLE)Fd) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C LQ_NO_INLINE int LQ_CALL LqFileEof(int Fd) {
    LARGE_INTEGER li = {0};
    if(GetFileSizeEx((HANDLE)Fd, &li) == FALSE)
        return -1;
    return LqFileTell(Fd) >= li.QuadPart;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileRead(int Fd, void* DestBuf, intptr_t SizeBuf) {
    /*
    * Use NtReadFile in this because, native function more faster and more flexible
    */
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER pl, *ppl = NULL;
    NTSTATUS Stat;
lblAgain:
    switch(Stat = NtReadFile((HANDLE)Fd, NULL, NULL, NULL, &iosb, (PVOID)DestBuf, SizeBuf, ppl, NULL)) {
        case STATUS_PENDING:
            NtCancelIoFile((HANDLE)Fd, &iosb);
            SetLastError(ERROR_IO_PENDING);
            break;
        case STATUS_SUCCESS: return iosb.Information;
        case STATUS_END_OF_FILE: SetLastError(ERROR_HANDLE_EOF); return 0;
        case STATUS_INVALID_PARAMETER:
            if(ppl == NULL) {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd);
                goto lblAgain;
            }
        default: SetLastError(RtlNtStatusToDosError(Stat));
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileWrite(int Fd, const void* SourceBuf, intptr_t SizeBuf) {
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER pl, *ppl = NULL;
    NTSTATUS Stat;

lblAgain:
    switch(Stat = NtWriteFile((HANDLE)Fd, NULL, NULL, NULL, &iosb, (PVOID)SourceBuf, SizeBuf, ppl, NULL)) {
        case STATUS_PENDING:
            NtCancelIoFile((HANDLE)Fd, &iosb);
            SetLastError(ERROR_IO_PENDING);
            break;
        case STATUS_SUCCESS: return iosb.Information;
        case STATUS_END_OF_FILE: SetLastError(ERROR_HANDLE_EOF); break;//For improve performance
        case STATUS_INVALID_PARAMETER:
            if(ppl == NULL) {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd);
                goto lblAgain;
            }
        default: SetLastError(RtlNtStatusToDosError(Stat));
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileReadBlocked(int Fd, void* lqaout DestBuf, intptr_t SizeBuf) {
    /*
    * Use NtReadFile in this because, native function more faster and more flexible
    */
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER pl, *ppl = NULL;
    NTSTATUS Stat;
lblAgain:
    switch(Stat = NtReadFile((HANDLE)Fd, NULL, NULL, NULL, &iosb, (PVOID)DestBuf, SizeBuf, ppl, NULL)) {
        case STATUS_PENDING:
            Stat = NtWaitForSingleObject((HANDLE)Fd, FALSE, NULL);
            if(NT_SUCCESS(Stat))
                Stat = iosb.Status;
            if(NT_SUCCESS(Stat))
                return iosb.Information;
            SetLastError(RtlNtStatusToDosError(Stat));
            break;
        case STATUS_SUCCESS: return iosb.Information;
        case STATUS_END_OF_FILE: SetLastError(ERROR_HANDLE_EOF); return 0;
        case STATUS_INVALID_PARAMETER:
            if(ppl == NULL) {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd);
                goto lblAgain;
            }
        default: SetLastError(RtlNtStatusToDosError(Stat));
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileWriteBlocked(int Fd, const void* lqain SourceBuf, intptr_t SizeBuf) {
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER pl, *ppl = NULL;
    NTSTATUS Stat;

lblAgain:
    switch(Stat = NtWriteFile((HANDLE)Fd, NULL, NULL, NULL, &iosb, (PVOID)SourceBuf, SizeBuf, ppl, NULL)) {
        case STATUS_PENDING:
            Stat = NtWaitForSingleObject((HANDLE)Fd, FALSE, NULL);
            if(NT_SUCCESS(Stat))
                Stat = iosb.Status;
            if(NT_SUCCESS(Stat))
                return iosb.Information;
            SetLastError(RtlNtStatusToDosError(Stat));
            break;
        case STATUS_SUCCESS: return iosb.Information;
        case STATUS_END_OF_FILE: SetLastError(ERROR_HANDLE_EOF); break;//For improve performance
        case STATUS_INVALID_PARAMETER:
            if(ppl == NULL) {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd);
                goto lblAgain;
            }
        default: SetLastError(RtlNtStatusToDosError(Stat));
    }
    return -((intptr_t)1);
}


LQ_EXTERN_C intptr_t LQ_CALL LqFileReadAsync(int Fd, void* DestBuf, intptr_t SizeBuf, LqFileSz Offset, int EventFd, LqAsync* Target) {
    LARGE_INTEGER pl;
    NTSTATUS Stat;

    pl.QuadPart = Offset;
    Target->Status = STATUS_PENDING;
    switch(Stat = NtReadFile((HANDLE)Fd, (HANDLE)EventFd, NULL, NULL, (PIO_STATUS_BLOCK)Target, (PVOID)DestBuf, SizeBuf, &pl, NULL)) {
        case STATUS_PENDING:
            SetLastError(ERROR_IO_PENDING);
        case STATUS_SUCCESS:
            return 0;
        default: SetLastError(RtlNtStatusToDosError(Stat));
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileWriteAsync(int Fd, const void* SourceBuf, intptr_t SizeBuf, LqFileSz Offset, int EventFd, LqAsync* Target) {
    LARGE_INTEGER pl;
    NTSTATUS Stat;

    pl.QuadPart = Offset;
    Target->Status = STATUS_PENDING;
    switch(Stat = NtWriteFile((HANDLE)Fd, (HANDLE)EventFd, NULL, NULL, (PIO_STATUS_BLOCK)Target, (PVOID)SourceBuf, SizeBuf, &pl, NULL)) {
        case STATUS_PENDING:
            SetLastError(ERROR_IO_PENDING);
        case STATUS_SUCCESS:
            return 0;
        default: SetLastError(RtlNtStatusToDosError(Stat));
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C int LQ_CALL LqFileAsyncCancel(int Fd, LqAsync* Target) {
    NTSTATUS Stat;
    if((Stat = NtCancelIoFile((HANDLE)Fd, (PIO_STATUS_BLOCK)Target)) == STATUS_SUCCESS)
        return 0;
    SetLastError(RtlNtStatusToDosError(Stat));
    return -1;
}

LQ_EXTERN_C int LQ_CALL LqFileAsyncStat(LqAsync* Target, intptr_t* LenWritten) {
    switch(Target->Status) {
        case STATUS_SUCCESS:
            *LenWritten = Target->Information;
            return 0;
        case STATUS_PENDING:
            return EINPROGRESS;
    }
    SetLastError(RtlNtStatusToDosError(Target->Status));
    return lq_errno;
}

LQ_EXTERN_C int LQ_CALL LqFileSetLock(int Fd, LqFileSz StartOffset, LqFileSz Len, int LockFlags) {
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER LocStart, LocLen;
    NTSTATUS Stat;

    LocStart.QuadPart = StartOffset;
    LocLen.QuadPart = Len;
    if(LockFlags & LQ_FLOCK_UNLOCK) {
        switch(auto Stat = NtUnlockFile((HANDLE)Fd, &iosb, &LocStart, &LocLen, NULL)) {
            case STATUS_PENDING:
                if(LockFlags & LQ_FLOCK_WAIT) {
                    NtWaitForSingleObject((HANDLE)Fd, FALSE, NULL);
                    return 0;
                } else {
                    NtCancelIoFile((HANDLE)Fd, &iosb);
                    SetLastError(ERROR_IO_PENDING);
                }
                break;
            case STATUS_SUCCESS:
                return 0;
            default:
                SetLastError(RtlNtStatusToDosError(Stat));
        }
    } else {
        switch(Stat = NtLockFile(
            (HANDLE)Fd,
            NULL,
            NULL,
            NULL,
            &iosb,
            &LocStart,
            &LocLen,
            NULL,
            (LockFlags & LQ_FLOCK_WAIT) ? TRUE : FALSE,
            ((LockFlags & LQ_FLOCK_WR) ? TRUE : FALSE))
            ) {
            case STATUS_PENDING:
                if(LockFlags & LQ_FLOCK_WAIT) {
                    NtWaitForSingleObject((HANDLE)Fd, FALSE, NULL);
                    return 0;
                } else {
                    NtCancelIoFile((HANDLE)Fd, &iosb);
                    SetLastError(ERROR_IO_PENDING);
                }
                break;
            case STATUS_SUCCESS:
                return 0;
            default:
                SetLastError(RtlNtStatusToDosError(Stat));
        }
    }

    return -1;
}

LQ_EXTERN_C int LQ_CALL LqFileMakeDir(const char* NewDirName, int Access) {
    wchar_t Name[LQ_MAX_PATH];
    _LqFileConvertNameToWcs(NewDirName, Name, LQ_MAX_PATH);
    return CreateDirectoryW(Name, NULL) == TRUE;
}

LQ_EXTERN_C int LQ_CALL LqFileRemoveDir(const char* NewDirName) {
    wchar_t Name[LQ_MAX_PATH];
    _LqFileConvertNameToWcs(NewDirName, Name, LQ_MAX_PATH);
    return RemoveDirectoryW(Name) == TRUE;
}

LQ_EXTERN_C int LQ_CALL LqFileMove(const char* OldName, const char* NewName) {
    wchar_t Old[LQ_MAX_PATH];
    wchar_t New[LQ_MAX_PATH];
    _LqFileConvertNameToWcs(OldName, Old, LQ_MAX_PATH);
    _LqFileConvertNameToWcs(NewName, New, LQ_MAX_PATH);
    return (MoveFileW(Old, New) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqFileRemove(const char* FileName) {
    wchar_t Name[LQ_MAX_PATH];
    _LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);
    return (DeleteFileW(Name) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileRealPath(const char* Source, char* Dest, intptr_t DestLen) {
    wchar_t Name[LQ_MAX_PATH];
    wchar_t New[LQ_MAX_PATH];
    DWORD Ret;
    _LqFileConvertNameToWcs(Source, Name, LQ_MAX_PATH);
    Ret = GetFullPathNameW(Name, LQ_MAX_PATH - 1, New, NULL);
    if(!((Source[0] == '\\') && (Source[1] == '\\') && (Source[2] == '?') && (Source[3] == '\\')) &&
        (New[0] == L'\\') && (New[1] == L'\\') && (New[2] == L'?') && (New[3] == L'\\')) {
        LqCpConvertFromWcs(New + 4, Dest, DestLen);
    } else {
        LqCpConvertFromWcs(New, Dest, DestLen);
    }
    return (Ret == 0) ? -((intptr_t)1) : Ret;
}


LQ_EXTERN_C int LQ_CALL LqTermPairCreate(int* MasterFd, int* SlaveFd, int MasterFlags, int SlaveFlags) {
    static uint64_t TermSerialNumber = 0;
    char PipeNameBuffer[MAX_PATH];
    DWORD dwError;
    int Mfd;
    int Sfd;
    auto Proc = (uint64_t)GetCurrentProcessId();
    uint64_t CurSerNum, t;
    do {
        t = TermSerialNumber;
        CurSerNum = TermSerialNumber + 1;
    } while(!LqAtmCmpXchg(TermSerialNumber, t, CurSerNum));

    LqFbuf_snprintf(
        PipeNameBuffer,
        sizeof(PipeNameBuffer),
        "\\\\.\\Pipe\\TermPair_%u%u_%u%u",
        (unsigned)(Proc >> 32),
        (unsigned)(Proc & 0xffffffff),
        (unsigned)(CurSerNum >> 32),
        (unsigned)(CurSerNum & 0xffffffff)
    );
    Mfd = LqPipeCreateNamed(PipeNameBuffer, (MasterFlags & (LQ_O_NONBLOCK | LQ_O_NOINHERIT)) | LQ_O_BIN | LQ_O_RDWR);
    if(Mfd == -1)
        return -1;
    Sfd = LqFileOpen(PipeNameBuffer, (SlaveFlags & (LQ_O_NONBLOCK | LQ_O_NOINHERIT)) | LQ_O_RDWR | LQ_O_EXCL, 0);
    if(Sfd == -1) {
        dwError = GetLastError();
        LqFileClose(Mfd);
        SetLastError(dwError);
        return -1;
    }
    *MasterFd = Mfd;
    *SlaveFd = Sfd;
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqPipeCreate(int* lpReadPipe, int* lpWritePipe, uint32_t FlagsRead, uint32_t FlagsWrite) {
    static uint64_t PipeSerialNumber = 0;

    char PipeNameBuffer[MAX_PATH];
    auto Proc = (uint64_t)GetCurrentProcessId();
    uint64_t CurSerNum, t;
    do {
        t = PipeSerialNumber;
        CurSerNum = PipeSerialNumber + 1;
    } while(!LqAtmCmpXchg(PipeSerialNumber, t, CurSerNum));

    LqFbuf_snprintf(
        PipeNameBuffer,
        sizeof(PipeNameBuffer),
        "\\\\.\\Pipe\\AnonPipe_%u%u_%u%u",
        (unsigned)(Proc >> 32),
        (unsigned)(Proc & 0xffffffff),
        (unsigned)(CurSerNum >> 32),
        (unsigned)(CurSerNum & 0xffffffff));
    int ReadPipeHandle = LqPipeCreateNamed(PipeNameBuffer, (FlagsRead & (LQ_O_NONBLOCK | LQ_O_BIN | LQ_O_TXT | LQ_O_NOINHERIT)) | LQ_O_RD);
    if(ReadPipeHandle == -1)
        return -1;
    int WritePipeHandle = LqFileOpen(PipeNameBuffer, (FlagsWrite & (LQ_O_NONBLOCK | LQ_O_BIN | LQ_O_TXT | LQ_O_NOINHERIT)) | LQ_O_WR | LQ_O_EXCL, 0);
    if(WritePipeHandle == -1) {
        DWORD dwError = GetLastError();
        LqFileClose(ReadPipeHandle);
        SetLastError(dwError);
        return -1;
    }
    *lpReadPipe = ReadPipeHandle;
    *lpWritePipe = WritePipeHandle;
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqPipeCreateRw(int* Pipe1, int* Pipe2, uint32_t Flags1, uint32_t Flags2) {
    static uint64_t PipeSerialNumber = 0;

    char PipeNameBuffer[MAX_PATH];
    auto Proc = (uint64_t)GetCurrentProcessId();
    uint64_t CurSerNum, t;
    do {
        t = PipeSerialNumber;
        CurSerNum = PipeSerialNumber + 1;
    } while(!LqAtmCmpXchg(PipeSerialNumber, t, CurSerNum));
    LqFbuf_snprintf(
        PipeNameBuffer,
        sizeof(PipeNameBuffer),
        "\\\\.\\Pipe\\AnonPipeRw_%u%u_%u%u",
        (unsigned)(Proc >> 32),
        (unsigned)(Proc & 0xffffffff),
        (unsigned)(CurSerNum >> 32),
        (unsigned)(CurSerNum & 0xffffffff)
    );
    int p1 = LqPipeCreateNamed(PipeNameBuffer, (Flags1 & (LQ_O_NONBLOCK | LQ_O_BIN | LQ_O_TXT | LQ_O_NOINHERIT)) | LQ_O_RDWR);
    if(p1 == -1)
        return -1;
    int p2 = LqFileOpen(PipeNameBuffer, (Flags2 & (LQ_O_NONBLOCK | LQ_O_BIN | LQ_O_TXT | LQ_O_NOINHERIT)) | LQ_O_RDWR | LQ_O_EXCL, 0);
    if(p2 == -1) {
        DWORD dwError = GetLastError();
        LqFileClose(p1);
        SetLastError(dwError);
        return -1;
    }
    *Pipe1 = p1;
    *Pipe2 = p2;
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqPipeCreateNamed(const char* NameOfPipe, uint32_t Flags) {
    static const size_t nSize = 4096;
    wchar_t PipeNameBuffer[LQ_MAX_PATH];
    LqCpConvertToWcs(NameOfPipe, PipeNameBuffer, MAX_PATH);
    SECURITY_ATTRIBUTES InheritAttr = {sizeof(SECURITY_ATTRIBUTES), NULL, (Flags & LQ_O_NOINHERIT) ? FALSE : TRUE};
    HANDLE PipeHandle = CreateNamedPipeW(
        PipeNameBuffer,
        ((Flags & LQ_O_RDWR) ? PIPE_ACCESS_DUPLEX : ((Flags & LQ_O_WR) ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND)) |
        ((LQ_O_NONBLOCK & Flags) ? FILE_FLAG_OVERLAPPED : 0),

        ((LQ_O_TXT & Flags) ? (PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE) : 0) |
        ((LQ_O_BIN & Flags) ? (PIPE_TYPE_BYTE | PIPE_READMODE_BYTE) : 0) |
        /*((LQ_O_NONBLOCK & Flags) ? PIPE_NOWAIT : */PIPE_WAIT/*)*/,
        1,
        nSize,
        nSize,
        120 * 1000,
        &InheritAttr
    );
    if(PipeHandle == INVALID_HANDLE_VALUE)
        return -1;
    return (int)PipeHandle;
}

LQ_EXTERN_C bool LQ_CALL LqDescrIsTerminal(int Fd) {
    DWORD Mode;
    return GetConsoleMode((HANDLE)Fd, &Mode) == TRUE;
}


/*------------------------------------------
* File descriptors
*/


LQ_EXTERN_C int LQ_CALL LqDescrDup(int Descriptor, int InheritFlag) {
    HANDLE h;
    auto PrHandle = GetCurrentProcess();
    if(DuplicateHandle(PrHandle, (HANDLE)Descriptor, PrHandle, &h, 0, (InheritFlag & LQ_O_NOINHERIT) ? FALSE : TRUE, DUPLICATE_SAME_ACCESS) == FALSE)
        return -1;
    return (int)h;
}

LQ_EXTERN_C int LQ_CALL LqDescrSetInherit(int Descriptor, int IsInherit) {
    return (SetHandleInformation((HANDLE)Descriptor, HANDLE_FLAG_INHERIT, IsInherit) == TRUE) ? 0 : -1;
}

static LqLocker<uintptr_t> _LqFileDescrDupToStd_Locker;

LQ_EXTERN_C int LQ_CALL LqDescrDupToStd(int Descriptor, int StdNo) {
    int Ret = 0;
    HANDLE h, p;
    HANDLE PrHandle = GetCurrentProcess();
    if(DuplicateHandle(PrHandle, (HANDLE)Descriptor, PrHandle, &h, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE)
        return -1;
    _LqFileDescrDupToStd_Locker.LockWriteYield();
    if(StdNo == STDIN_FILENO) {
        if(SetStdHandleEx(STD_INPUT_HANDLE, h, &p) == 0)
            Ret = -1;
    } else if(StdNo == STDOUT_FILENO) {
        if(SetStdHandleEx(STD_OUTPUT_HANDLE, h, &p) == 0)
            Ret = -1;
    } else if(StdNo == STDERR_FILENO) {
        if(SetStdHandleEx(STD_ERROR_HANDLE, h, &p) == 0)
            Ret = -1;
    } else {
        CloseHandle(h);
        lq_errno_set(EINVAL);
        Ret = -1;
    }
    if(Ret != -1)
        CloseHandle(p);
    _LqFileDescrDupToStd_Locker.UnlockWrite();
    return Ret;
}

/*------------------------------------------
* Shared Memory
*/

LQ_EXTERN_C int LQ_CALL LqSharedCreate(int key, size_t Size, int DscrFlags, int UserAccess) {
    HANDLE hMap;
    char Name[30];
    SECURITY_ATTRIBUTES InheritAttr = {sizeof(SECURITY_ATTRIBUTES), NULL, (DscrFlags & LQ_O_NOINHERIT) ? FALSE : TRUE};
    LqFbuf_snprintf(Name, sizeof(Name), "SharedM%05X", key);
    hMap = CreateFileMappingA(INVALID_HANDLE_VALUE,
                              &InheritAttr,
                              PAGE_READWRITE,
                              0,
                              Size,
                              Name);
    return (hMap == 0) ? -1 : (int)hMap;
}

LQ_EXTERN_C int LQ_CALL LqSharedOpen(int key, size_t Size, int DscrFlags, int UserAccess) {
    HANDLE hMap;
    char Name[30];
    LqFbuf_snprintf(Name, sizeof(Name), "SharedM%05X", key);
    hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, (DscrFlags & LQ_O_NOINHERIT) ? FALSE : TRUE, Name);
    return (hMap == 0) ? -1 : (int)hMap;
}

LQ_EXTERN_C void* LQ_CALL LqSharedAt(int shmid, void* BaseAddress) {
    return MapViewOfFileEx((HANDLE)shmid, FILE_MAP_ALL_ACCESS, 0, 0, 0, BaseAddress);
}

LQ_EXTERN_C int LQ_CALL LqSharedUnmap(void *addr) {
    return (UnmapViewOfFile(addr) == 0) ? -1 : 0;
}

LQ_EXTERN_C int LQ_CALL LqSharedClose(int shmid) {
    return LqFileClose(shmid);
}


/*------------------------------------------
* Process
*/

LQ_EXTERN_C int LQ_CALL LqProcessCreate
(
    const char* FileName,
    char* const Argv[],
    char* const Envp[],
    const char* WorkingDir,
    int StdIn,
    int StdOut,
    int StdErr,
    int* EventKill,
    bool IsOwnerGroup
) {
    STARTUPINFOW siStartInfo = {sizeof(STARTUPINFOW), 0};
    PROCESS_INFORMATION processInfo = {0};
    LqString16 CommandLine, Environ;
    wchar_t Buf[LQ_MAX_PATH];

    siStartInfo.hStdInput = (HANDLE)((StdIn == -1) ? LQ_STDIN : StdIn);
    siStartInfo.hStdError = (HANDLE)((StdErr == -1) ? LQ_STDERR : StdErr);
    siStartInfo.hStdOutput = (HANDLE)((StdOut == -1) ? LQ_STDOUT : StdOut);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    LqCpConvertToWcs(FileName, Buf, LQ_MAX_PATH);

    if(Buf[0] != L'"')
        CommandLine = L"\"";
    CommandLine.append(Buf);
    if(Buf[0] != L'"')
        CommandLine.append(L"\"");
    if(Argv != NULL)
        for(size_t i = 0; Argv[i] != NULL; i++) {
            CommandLine.append(1, L' ');
            LqCpConvertToWcs(Argv[i], Buf, LQ_MAX_PATH);
            CommandLine.append(Buf);
        }

    if(Envp != NULL)
        for(size_t i = 0; Envp[i] != NULL; i++) {
            LqCpConvertToWcs(Envp[i], Buf, LQ_MAX_PATH);
            Environ.append(Buf);
            Environ.append(1, L'\0');
        }
    Environ.append(2, L'\0\0');

    if(WorkingDir != NULL) {
        LqCpConvertToWcs(WorkingDir, Buf, LQ_MAX_PATH);
    }

    if(CreateProcessW
    (
       NULL,
       (LPWSTR)CommandLine.c_str(),
       NULL,
       NULL,
       TRUE,
       CREATE_UNICODE_ENVIRONMENT | (IsOwnerGroup ? CREATE_NEW_PROCESS_GROUP : 0),
       (Envp != NULL) ? (LPVOID)Environ.c_str() : NULL,
       (WorkingDir != NULL) ? Buf : NULL,
       &siStartInfo,
       &processInfo
       ) == FALSE
       ) {
        return -1;
    }

    if(EventKill == NULL) {
        NtClose(processInfo.hProcess);
    } else {
        LqDescrSetInherit((int)processInfo.hProcess, 1);
        *EventKill = (int)processInfo.hProcess;
    }
    NtClose(processInfo.hThread);
    return processInfo.dwProcessId;
}

LQ_EXTERN_C int LQ_CALL LqProcessKill(int Pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, Pid);
    if(h != NULL) {
        TerminateProcess(h, 0);
        NtClose(h);
        return 0;
    }
    return -1;
}

LQ_EXTERN_C int LQ_CALL LqProcessId() {
    return GetCurrentProcessId();
}

LQ_EXTERN_C int LQ_CALL LqProcessParentId() {
    PROCESS_BASIC_INFORMATION BasicInfo;
    ULONG ulSize = 0;
    if(NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), &ulSize) != STATUS_SUCCESS)
        return -1;
    return (int)BasicInfo.Reserved3;
}

LQ_EXTERN_C intptr_t LQ_CALL LqProcessGetName(int Pid, char* DestBuf, intptr_t SizeBuf) {
    DWORD Ret;
    HANDLE Handle;
    wchar_t Name[LQ_MAX_PATH];

    Handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, Pid);
    if(Handle == NULL)
        return -((intptr_t)1);

    Ret = GetModuleFileNameExW(Handle, NULL, Name, LQ_MAX_PATH - 1);
    NtClose(Handle);
    if(Ret <= 0)
        return -((intptr_t)1);
    return LqCpConvertFromWcs(Name, DestBuf, SizeBuf);
}

LQ_EXTERN_C int LQ_CALL LqFileSetCurDir(const char* NewDir) {
    wchar_t Name[LQ_MAX_PATH];
    LqCpConvertToWcs(NewDir, Name, LQ_MAX_PATH);
    return (SetCurrentDirectoryW(Name) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqFileGetCurDir(char* DirBuf, size_t LenBuf) {
    wchar_t Name[LQ_MAX_PATH];
    if(GetCurrentDirectoryW(LQ_MAX_PATH - 1, Name) <= 0)
        return -1;
    LqCpConvertFromWcs(Name, DirBuf, LenBuf);
    return 0;
}

/*------------------------------------------
* Event
*/

LQ_EXTERN_C int LQ_CALL LqEventCreate(int InheritFlags) {
    OBJECT_ATTRIBUTES Attr;
    HANDLE h;
    NTSTATUS Stat;

    InitializeObjectAttributes(&Attr, NULL, (InheritFlags & LQ_O_NOINHERIT) ? 0 : OBJ_INHERIT, NULL, NULL);
    Stat = NtCreateEvent(&h, EVENT_ALL_ACCESS, &Attr, NotificationEvent, FALSE);
    if(!NT_SUCCESS(Stat)) {
        SetLastError(RtlNtStatusToDosError(Stat));
        return -1;
    }
    return (int)h;
}

LQ_EXTERN_C int LQ_CALL LqEventSet(int FileEvent) {
    return (NtSetEvent((HANDLE)FileEvent, NULL) == STATUS_SUCCESS) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqEventReset(int FileEvent) {
    LONG PrevVal = 0;
    if(NtResetEvent((HANDLE)FileEvent, &PrevVal) != STATUS_SUCCESS)
        return -1;
    return PrevVal ? 1 : 0;
}

/*------------------------------------------
* Timer
*/

LQ_EXTERN_C int LQ_CALL LqTimerCreate(int InheritFlags) {
    HANDLE h;
    SECURITY_ATTRIBUTES InheritAttr = {sizeof(SECURITY_ATTRIBUTES), NULL, (InheritFlags & LQ_O_NOINHERIT) ? FALSE : TRUE};

    h = CreateWaitableTimerW(&InheritAttr, TRUE, NULL);
    if(h == NULL)
        return -1;
    return (int)h;
}

LQ_EXTERN_C int LQ_CALL LqTimerSet(int TimerFd, LqTimeMillisec Time) {
    LARGE_INTEGER li;
    li.QuadPart = -((LONGLONG)(Time * 10000LL));
    return (SetWaitableTimer((HANDLE)TimerFd, &li, 0, NULL, NULL, FALSE) == TRUE) ? 0 : -1;
}


/*------------------------------------------
* FileEnm
*/

LQ_EXTERN_C int LQ_CALL LqFileEnmStart(LqFileEnm* Enm, const char* Dir, char* DestName, size_t NameLen, uint8_t* Type) {
    wchar_t DirName[LQ_MAX_PATH];
    WIN32_FIND_DATAW Fdata = {0};
    HANDLE Hndl;
    int l;

    l = _LqFileConvertNameToWcs(Dir, DirName, LQ_MAX_PATH - 4);
    if(l < 0)
        return -1;

    if(DirName[l - 2] != L'*') {
        if(DirName[l - 2] != L'\\') {
            DirName[l - 1] = L'\\';
            l++;
        }
        DirName[l - 1] = L'*';
        DirName[l] = L'\0';
    }
    Hndl = FindFirstFileW(DirName, &Fdata);
    if(Hndl == INVALID_HANDLE_VALUE)
        return -1;
    Enm->Hndl = (uintptr_t)Hndl;
    LqCpConvertFromWcs(Fdata.cFileName, DestName, NameLen);
    if(Type != NULL) {
        if(Fdata.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ARCHIVE))
            *Type = LQ_F_REG;
        else if(Fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            *Type = LQ_F_DIR;
        else
            *Type = LQ_F_OTHER;
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqFileEnmNext(LqFileEnm* Enm, char* DestName, size_t NameLen, uint8_t* Type) {
    WIN32_FIND_DATAW Fdata = {0};


    if(FindNextFileW((HANDLE)Enm->Hndl, &Fdata) == FALSE) {
        FindClose((HANDLE)Enm->Hndl);
        Enm->Hndl = (uintptr_t)INVALID_HANDLE_VALUE;
        return -1;
    }
    LqCpConvertFromWcs(Fdata.cFileName, DestName, NameLen);
    if(Type != NULL) {
        if(Fdata.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ARCHIVE))
            *Type = LQ_F_REG;
        else if(Fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            *Type = LQ_F_DIR;
        else
            *Type = LQ_F_OTHER;
    }
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFileEnmBreak(LqFileEnm* Enm) {
    if(Enm->Hndl != (uintptr_t)INVALID_HANDLE_VALUE)
        FindClose((HANDLE)Enm->Hndl);
}

#pragma comment(lib, "ntdll.lib")

//#define  IsUseDynamicEvnt

/*
* Version of unix poll for Windows.
* You must send to function only native descriptors (Not CRT).
* Supports:
*   +Read/Write events
*   +Supports types of files (sockets, pipes, console)
*   +Event objects (when have signal simulate POLLIN and POLLOUT)
*   -In widows only can be use with LQ_O_NONBLOCK creation parametr
*/
#define LqEvntSystemEventByConnFlag(EvntFlags)        \
    ((((EvntFlags) & LQ_POLLOUT)  ? FD_WRITE : 0)    |\
    (((EvntFlags) & LQ_POLLIN)    ? FD_READ : 0)     |\
    (((EvntFlags) & LQ_POLLIN)    ? FD_ACCEPT : 0)   |\
    (((EvntFlags) & LQ_POLLOUT)   ? FD_CONNECT : 0)  |\
    (((EvntFlags) & LQ_POLLHUP)   ? FD_CLOSE: 0))

#define LqConnFlagBySysEvent(EvntFlags)               \
    ((((EvntFlags) & FD_READ)     ? LQ_POLLIN : 0)   |\
    (((EvntFlags) & FD_WRITE)     ? LQ_POLLOUT : 0)  |\
    (((EvntFlags) & FD_ACCEPT)    ? LQ_POLLOUT : 0)  |\
    (((EvntFlags) & FD_CONNECT)   ? LQ_POLLIN : 0)   |\
    (((EvntFlags) & FD_CLOSE)     ? LQ_POLLHUP: 0))

#define LQ_POLL_TYPE_EVENT 0
#define LQ_POLL_TYPE_PIPE 1
#define LQ_POLL_TYPE_SOCKET 2
#define LQ_POLL_TYPE_TERMINAL 3
#define LQ_POLL_TYPE_DRIVER 4

typedef struct __FILE_PIPE_LOCAL_INFORMATION {
    ULONG NamedPipeType;
    ULONG NamedPipeConfiguration;
    ULONG MaximumInstances;
    ULONG CurrentInstances;
    ULONG InboundQuota;
    ULONG ReadDataAvailable;
    ULONG OutboundQuota;
    ULONG WriteQuotaAvailable;
    ULONG NamedPipeState;
    ULONG NamedPipeEnd;
} __FILE_PIPE_LOCAL_INFORMATION;


static DWORD CheckAllEvents(const HANDLE* EventObjs, const intptr_t EventsCount) {
    intptr_t StartIndex = ((intptr_t)0);
    intptr_t Count;
    DWORD Status;
    while(true) {
        Count = EventsCount - StartIndex;
        if(Count >= ((intptr_t)MAXIMUM_WAIT_OBJECTS))
            Count = ((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1);
        Status = WaitForMultipleObjects(Count, EventObjs + StartIndex, FALSE, 0);
        if((Status >= WAIT_OBJECT_0) && (Status < (WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS))) {
            return Status;
        } else if(Status != WAIT_TIMEOUT) {
            return Status;
        }
        StartIndex += Count;
        if(StartIndex >= ((intptr_t)EventsCount))
            return Status;
    }
}
static char LqPollCheckBuf;
LQ_EXTERN_C int LQ_CALL LqPollCheck(LqPoll* Fds, size_t CountFds, LqTimeMillisec TimeoutMillisec) {
    bool HavePipeOrTerminal = false;
    bool HaveEvents = false;
    LARGE_INTEGER li, *pli;
    IO_STATUS_BLOCK StatusBlock;
    static IO_STATUS_BLOCK ReadStatusBlock;
    WSANETWORKEVENTS NetEvnts;
    DWORD num_read;
    INPUT_RECORD ir;
    NTSTATUS Status;
    DWORD WaitRes;
    __FILE_PIPE_LOCAL_INFORMATION PipeInfo;
    int CountEvents = 0;
    HANDLE* Handles;
    uint8_t* Types;
    LqTimeMillisec CurWaitTime, CurWaitTime2, WaitTime;
    if(CountFds == 0) {
        Sleep(TimeoutMillisec);
        return 0;
    }
    Handles = (HANDLE*)LqMemAlloc(CountFds * sizeof(HANDLE) + CountFds * sizeof(uint8_t));
    Types = (uint8_t*)(Handles + CountFds);
    for(size_t i = 0; i < CountFds; i++) {
        Fds[i].revents = 0;
        if(LqDescrIsSocket(Fds[i].fd)) {
            Types[i] = LQ_POLL_TYPE_SOCKET;
            Handles[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
            WSAEventSelect(Fds[i].fd, Handles[i], LqEvntSystemEventByConnFlag(Fds[i].events));
            WSAEnumNetworkEvents(Fds[i].fd, Handles[i], &NetEvnts);
            if(NetEvnts.lNetworkEvents != 0) {
                Fds[i].revents = LqConnFlagBySysEvent(NetEvnts.lNetworkEvents);
                CountEvents++;
                CloseHandle(Handles[i]);
                Handles[i] = (HANDLE)Fds[i].fd;
            }
        } else if(LqDescrIsTerminal(Fds[i].fd)) {
            Types[i] = LQ_POLL_TYPE_TERMINAL;
            HavePipeOrTerminal = true;
            if(Fds[i].events & LQ_POLLIN) {
                while(true) {
                    if(!PeekConsoleInputW((HANDLE)Fds[i].fd, &ir, 1, &num_read)) {
                        Fds[i].revents = LQ_POLLERR;
                        break;
                    }
                    if(num_read <= 0)
                        break;
                    if(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                        Fds[i].revents = LQ_POLLIN;
                        break;
                    } else {
                        ReadConsoleInputW((HANDLE)Fds[i].fd, &ir, 1, &num_read);
                    }
                }
            }
            Fds[i].revents |= (Fds[i].events & LQ_POLLOUT);
            if(Fds[i].revents != 0)
                CountEvents++;
            if(CountEvents > 0)
                Handles[i] = (HANDLE)Fds[i].fd;
            else
                Handles[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        } else if(NtCancelIoFile((HANDLE)Fds[i].fd, &StatusBlock) == STATUS_OBJECT_TYPE_MISMATCH) { /* Is Event */
            Types[i] = LQ_POLL_TYPE_EVENT;
            if(Fds[i].events & (LQ_POLLIN | LQ_POLLOUT)) {
                Handles[i] = (HANDLE)Fds[i].fd;
                switch(WaitForSingleObject((HANDLE)Fds[i].fd, 0)) {
                    case WAIT_OBJECT_0:
                        Fds[i].revents = Fds[i].events & (LQ_POLLIN | LQ_POLLOUT);
                        break;
                    case WAIT_TIMEOUT:
                        break;
                    default:
                        Fds[i].revents = LQ_POLLERR;
                        break;
                }
            }
            if(Fds[i].revents != 0)
                CountEvents++;
            if((CountEvents > 0) || (Fds[i].events & (LQ_POLLIN | LQ_POLLOUT)))
                Handles[i] = (HANDLE)Fds[i].fd;
            else
                Handles[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        } else if(
            (Status = NtQueryInformationFile((HANDLE)Fds[i].fd, &StatusBlock, &PipeInfo, sizeof(PipeInfo), (FILE_INFORMATION_CLASS)24)) !=
            STATUS_INVALID_PARAMETER
            ) { /* Is pipe */
            Types[i] = LQ_POLL_TYPE_PIPE;
            HavePipeOrTerminal = true;

            if(Status != STATUS_SUCCESS) {
                Fds[i].revents |= LQ_POLLERR;
            } else {
                if((Fds[i].events & LQ_POLLOUT) && (PipeInfo.WriteQuotaAvailable > 0))
                    Fds[i].revents |= LQ_POLLOUT;
                if((Fds[i].events & LQ_POLLIN) && (PipeInfo.ReadDataAvailable > 0))
                    Fds[i].revents |= LQ_POLLIN;
                if((Fds[i].events & LQ_POLLHUP) && (PipeInfo.NamedPipeState != 3))
                    Fds[i].revents |= LQ_POLLHUP;
            }
            if(Fds[i].revents != 0)
                CountEvents++;
            if(CountEvents > 0)
                Handles[i] = (HANDLE)Fds[i].fd;
            else
                Handles[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        } else {
            Types[i] = LQ_POLL_TYPE_DRIVER;
            Fds[i].revents = 0;
            if(Fds[i].events & LQ_POLLOUT) {
                Fds[i].revents |= LQ_POLLERR;
            } else {
                Handles[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
                if(Fds[i].events & (LQ_POLLIN | LQ_POLLHUP)) {
                    pli = NULL;
lblAgain:
                    switch(NtReadFile((HANDLE)Fds[i].fd, Handles[i], NULL, NULL, &ReadStatusBlock, &LqPollCheckBuf, 0, pli, NULL)) {
                        case STATUS_SUCCESS:
                            if(Fds[i].events & LQ_POLLIN)
                                Fds[i].revents |= LQ_POLLIN;
                            else
                                ResetEvent(Handles[i]);
                            break;
                        case STATUS_PENDING:
                            if(!(Fds[i].events & LQ_POLLIN)) {
                                CancelIo((HANDLE)Fds[i].fd);
                                ResetEvent(Handles[i]);
                                HavePipeOrTerminal = true;
                            }
                            break;
                        case STATUS_PIPE_BROKEN:
                        case STATUS_PIPE_CLOSING:
                            if(Fds[i].events & LQ_POLLHUP)
                                Fds[i].revents |= LQ_POLLHUP;
                            ResetEvent(Handles[i]);
                            break;
                        case STATUS_INVALID_PARAMETER:
                            if(pli == NULL) {
                                pli = &li;
                                NtQueryInformationFile((HANDLE)Fds[i].fd, &ReadStatusBlock, &li.QuadPart, sizeof(li.QuadPart), (FILE_INFORMATION_CLASS)14);
                                goto lblAgain;
                            }
                        default:
                            Fds[i].revents |= LQEVNT_FLAG_ERR;
                    }
                }
            }
            if(Fds[i].revents != 0)
                CountEvents++;
            if(CountEvents > 0) {
                CancelIo((HANDLE)Fds[i].fd);
                CloseHandle(Handles[i]);
                Handles[i] = (HANDLE)Fds[i].fd;
            }
        }
    }

    if(CountEvents > 0)
        goto lblOut;

    WaitTime = TimeoutMillisec;
lblAgainCheck:

    if(HavePipeOrTerminal)
        CurWaitTime2 = CurWaitTime = LQ_POLLCHECK_WAIT_WHEN_HAVE_PIPE_OR_TERMINAL;
    else
        CurWaitTime2 = CurWaitTime = WaitTime;

    if(CountFds >= MAXIMUM_WAIT_OBJECTS) {
        while(true) {
            WaitRes = CheckAllEvents(Handles, CountFds);
            if(((WaitRes >= WAIT_OBJECT_0) && (WaitRes < (WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS))) || (WaitRes != WAIT_TIMEOUT))
                break;
            CurWaitTime -= ((LqTimeMillisec)LQ_POLLCHECK_WAIT_WHEN_GR_MAXIMUM_WAIT_OBJECTS);
            WaitRes = WaitForMultipleObjects(MAXIMUM_WAIT_OBJECTS - 1, Handles, FALSE, lq_min(((LqTimeMillisec)LQ_POLLCHECK_WAIT_WHEN_GR_MAXIMUM_WAIT_OBJECTS), CurWaitTime));
            if(((WaitRes >= WAIT_OBJECT_0) && (WaitRes < (WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS))) || (WaitRes != WAIT_TIMEOUT))
                break;
            if(CurWaitTime <= ((LqTimeMillisec)0))
                break;
        }
    } else {
        WaitForMultipleObjects(CountFds, Handles, FALSE, CurWaitTime);
    }
    WaitTime -= CurWaitTime2;
    for(size_t i = 0; i < CountFds; i++) {
        switch(Types[i]) {
            case LQ_POLL_TYPE_EVENT:
                switch(WaitForSingleObject(Handles[i], 0)) {
                    case WAIT_OBJECT_0:
                        Fds[i].revents = Fds[i].events & (LQ_POLLIN | LQ_POLLOUT);
                        break;
                    case WAIT_TIMEOUT:
                        break;
                    default:
                        Fds[i].revents = LQ_POLLERR;
                        break;
                }
                break;
            case LQ_POLL_TYPE_SOCKET:
                NetEvnts.lNetworkEvents = 0;
                WSAEnumNetworkEvents(Fds[i].fd, Handles[i], &NetEvnts);
                if(NetEvnts.lNetworkEvents != 0)
                    Fds[i].revents = LqConnFlagBySysEvent(NetEvnts.lNetworkEvents);
                break;
            case LQ_POLL_TYPE_PIPE:
                Status = NtQueryInformationFile((HANDLE)Fds[i].fd, &StatusBlock, &PipeInfo, sizeof(PipeInfo), (FILE_INFORMATION_CLASS)24);
                if(Status != STATUS_SUCCESS) {
                    Fds[i].revents |= LQ_POLLERR;
                } else {
                    if((Fds[i].events & LQ_POLLOUT) && (PipeInfo.WriteQuotaAvailable > 0))
                        Fds[i].revents |= LQ_POLLOUT;
                    if((Fds[i].events & LQ_POLLIN) && (PipeInfo.ReadDataAvailable > 0))
                        Fds[i].revents |= LQ_POLLIN;
                    if((Fds[i].events & LQ_POLLHUP) && (PipeInfo.NamedPipeState != 3))
                        Fds[i].revents |= LQ_POLLHUP;
                }
                break;
            case LQ_POLL_TYPE_TERMINAL:
                if(Fds[i].events & LQ_POLLIN) {
                    while(true) {
                        if(!PeekConsoleInputW((HANDLE)Fds[i].fd, &ir, 1, &num_read)) {
                            Fds[i].revents = LQ_POLLERR;
                            break;
                        }
                        if(num_read <= 0)
                            break;
                        if(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                            Fds[i].revents = LQ_POLLIN;
                            break;
                        } else {
                            ReadConsoleInputW((HANDLE)Fds[i].fd, &ir, 1, &num_read);
                        }
                    }
                }
                Fds[i].revents |= (Fds[i].events & LQ_POLLOUT);
                break;
            case LQ_POLL_TYPE_DRIVER:
                switch(((Fds[i].events & LQ_POLLHUP) && !(Fds[i].events & LQ_POLLIN)) ? WAIT_OBJECT_0 : WaitForSingleObject(Handles[i], 0)) {
                    case WAIT_OBJECT_0:
                        pli = NULL;
lblAgain2:
                        switch(NtReadFile((HANDLE)Fds[i].fd, Handles[i], NULL, NULL, &ReadStatusBlock, &LqPollCheckBuf, 0, pli, NULL)) {
                            case STATUS_SUCCESS:
                                if(Fds[i].events & LQ_POLLIN)
                                    Fds[i].revents |= LQ_POLLIN;
                                else
                                    ResetEvent(Handles[i]);
                                break;
                            case STATUS_PENDING:
                                if(!(Fds[i].events & LQ_POLLIN)) {
                                    CancelIo((HANDLE)Fds[i].fd);
                                    ResetEvent(Handles[i]);
                                }
                                break;
                            case STATUS_PIPE_BROKEN:
                            case STATUS_PIPE_CLOSING:
                                if(Fds[i].events & LQ_POLLHUP)
                                    Fds[i].revents |= LQ_POLLHUP;
                                ResetEvent(Handles[i]);
                                break;
                            case STATUS_INVALID_PARAMETER:
                                if(pli == NULL) {
                                    pli = &li;
                                    NtQueryInformationFile((HANDLE)Fds[i].fd, &ReadStatusBlock, &li.QuadPart, sizeof(li.QuadPart), (FILE_INFORMATION_CLASS)14);
                                    goto lblAgain2;
                                }
                            default:
                                Fds[i].revents |= LQEVNT_FLAG_ERR;
                        }
                        break;
                    case WAIT_TIMEOUT:
                        break;
                    default:
                        Fds[i].revents = LQ_POLLERR;
                        break;
                }
                break;
        }
        if(Fds[i].revents != 0)
            CountEvents++;
    }
    if(CountEvents > 0)
        goto lblOut;
    if(WaitTime > ((LqTimeMillisec)0))
        goto lblAgainCheck;
lblOut:
    for(size_t i = 0; i < CountFds; i++) {
        if(Types[i] == LQ_POLL_TYPE_DRIVER)
            CancelIo((HANDLE)Fds[i].fd);
        if((HANDLE)Fds[i].fd != Handles[i])
            CloseHandle(Handles[i]);
    }
    LqMemFree(Handles);
    return CountEvents;
}

/*------------------------------------------
* LqFilePathEvnt
*/

LQ_EXTERN_C int LQ_CALL LqFilePathEvntCreate(LqFilePathEvnt* Evnt, const char* DirOrFile, uint8_t FollowFlag) {
    wchar_t Wdir[LQ_MAX_PATH];
    DWORD NotifyFilter = 0;
    HANDLE DirHandle;
    HANDLE EventHandle;
    static char Buf;
    NTSTATUS Stat;

    Evnt->Fd = -1;
    LqCpConvertToWcs(DirOrFile, Wdir, LQ_MAX_PATH - 1);
    if(FollowFlag & LQDIREVNT_ADDED)
        NotifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
    if(FollowFlag & LQDIREVNT_MOD)
        NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
    if(FollowFlag & (LQDIREVNT_MOVE_TO | LQDIREVNT_MOVE_FROM))
        NotifyFilter |= (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
    if(FollowFlag & LQDIREVNT_RM)
        NotifyFilter |= (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);

    DirHandle = CreateFileW(
        Wdir,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    if(DirHandle == INVALID_HANDLE_VALUE)
        return -1;
    EventHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if(EventHandle == NULL) {
        CloseHandle(DirHandle);
        return -1;
    }

    Evnt->_Data.Buffer = (char*)malloc(1024 * 16);
    if(Evnt->_Data.Buffer == NULL) {
        CloseHandle(DirHandle);
        return -1;
    }
    Evnt->_Data.BufferSize = 1024 * 16;

    Evnt->_Data.IsSubtree = (FollowFlag & LQDIREVNT_SUBTREE) ? true : false;

    Evnt->_Data.IoStatusBlock.Status = STATUS_PENDING;
    Stat = NtNotifyChangeDirectoryFile(
        DirHandle,
        EventHandle,
        NULL,
        NULL,
        (PIO_STATUS_BLOCK)&Evnt->_Data.IoStatusBlock,
        Evnt->_Data.Buffer,
        Evnt->_Data.BufferSize,
        NotifyFilter,
        (FollowFlag & LQDIREVNT_SUBTREE) ? TRUE : FALSE
    );
    SetLastError(RtlNtStatusToDosError(Stat));

    switch(Stat) {
        case STATUS_NOTIFY_ENUM_DIR:
        case STATUS_SUCCESS:
            Evnt->_Data.IoStatusBlock.Status = Stat;
        case STATUS_PENDING:
            Evnt->Fd = (int)EventHandle;
            Evnt->_Data.DirFd = (int)DirHandle;
            Evnt->_Data.NotifyFilter = NotifyFilter;
            Evnt->_Data.DirName = LqStrDuplicate(DirOrFile);
            Evnt->_Data.DirNameLen = LqStrLen(DirOrFile);
            return 0;
    }
    Evnt->_Data.IoStatusBlock.Status = Stat;
    NtClose(EventHandle);
    NtClose(DirHandle);
    free(Evnt->_Data.Buffer);
    return -1;
}

LQ_EXTERN_C void LQ_CALL LqFilePathEvntFreeEnum(LqFilePathEvntEnm** Dest) {
    LqFilePathEvntEnm* i, *j;

    if(Dest == NULL)
        return;
    for(i = *Dest; i != NULL; ) {
        j = i;
        i = i->Next;
        free(j);
    }
    *Dest = NULL;
}

LQ_EXTERN_C int LQ_CALL LqFilePathEvntDoEnum(LqFilePathEvnt* Evnt, LqFilePathEvntEnm** Dest) {
    char* Buf;
    LqFilePathEvntEnm* NewList;
    DWORD Offset;
    char FileName[LQ_MAX_PATH];
    FILE_NOTIFY_INFORMATION* Info;
    uint8_t RetFlag;
    WCHAR t;
    size_t NewSize;
    LqFilePathEvntEnm* Val;
    size_t Count;
    NTSTATUS Stat;

    LqFilePathEvntFreeEnum(Dest);

    switch(Evnt->_Data.IoStatusBlock.Status) {
        case STATUS_PENDING:
            return 0;
        case STATUS_SUCCESS:
        case STATUS_NOTIFY_ENUM_DIR:
            break;
        default:
            SetLastError(RtlNtStatusToDosError(Evnt->_Data.IoStatusBlock.Status));
            return -1;
    }

    Buf = Evnt->_Data.Buffer;

    NewList = NULL;
    if(Dest != NULL)
        *Dest = NULL;
    Offset = 0;
    while(true) {
        Info = (FILE_NOTIFY_INFORMATION*)(Buf + Offset);
        switch(Info->Action) {
            case FILE_ACTION_ADDED: RetFlag = LQDIREVNT_ADDED; break;
            case FILE_ACTION_REMOVED: RetFlag = LQDIREVNT_RM; break;
            case FILE_ACTION_MODIFIED: RetFlag = LQDIREVNT_MOD; break;
            case FILE_ACTION_RENAMED_OLD_NAME: RetFlag = LQDIREVNT_MOVE_FROM; break;
            case FILE_ACTION_RENAMED_NEW_NAME: RetFlag = LQDIREVNT_MOVE_TO; break;
        }

        t = Info->FileName[Info->FileNameLength / 2];
        Info->FileName[Info->FileNameLength / 2] = L'\0';
        LqCpConvertFromWcs(Info->FileName, FileName, LQ_MAX_PATH - 1);
        Info->FileName[Info->FileNameLength / 2] = t;

        NewSize = LqStrLen(FileName) + sizeof(LqFilePathEvntEnm) + Evnt->_Data.DirNameLen + 2;
        Val = (LqFilePathEvntEnm*)malloc(NewSize);
        Count = LqStrCopy(Val->Name, Evnt->_Data.DirName);

        if(Val->Name[0] != 0) {
            char Sep[2] = {LQ_PATH_SEPARATOR, 0};
            if(Val->Name[Count] != LQ_PATH_SEPARATOR)
                LqStrCat(Val->Name, Sep);
        }
        LqStrCat(Val->Name, FileName);
        Val->Flag = RetFlag;

        Val->Next = NewList;
        NewList = Val;
        Offset += Info->NextEntryOffset;
        if(Info->NextEntryOffset == 0)
            break;
    }

    if(Dest != NULL)
        *Dest = NewList;
    else
        LqFilePathEvntFreeEnum(&NewList);
    Evnt->_Data.IoStatusBlock.Status = STATUS_PENDING;

    Stat = NtNotifyChangeDirectoryFile(
        (HANDLE)Evnt->_Data.DirFd,
        (HANDLE)Evnt->Fd,
        NULL,
        NULL,
        (PIO_STATUS_BLOCK)&Evnt->_Data.IoStatusBlock,
        Evnt->_Data.Buffer,
        Evnt->_Data.BufferSize,
        Evnt->_Data.NotifyFilter,
        Evnt->_Data.IsSubtree
    );

    SetLastError(RtlNtStatusToDosError(Stat));

    switch(Stat) {
        case STATUS_NOTIFY_ENUM_DIR:
        case STATUS_SUCCESS:
            Evnt->_Data.IoStatusBlock.Status = Stat;
        case STATUS_PENDING:
            return 0;
    }
    Evnt->_Data.IoStatusBlock.Status = Stat;
    return -1;
}

LQ_EXTERN_C int LQ_CALL LqFilePathEvntGetName(LqFilePathEvnt* Evnt, char* DestName, size_t DestNameSize) {
    return LqStrCopyMax(DestName, Evnt->_Data.DirName, DestNameSize);
}

LQ_EXTERN_C void LQ_CALL LqFilePathEvntFree(LqFilePathEvnt* Evnt) {
    if(Evnt->Fd != -1) {
        NtClose((HANDLE)Evnt->Fd);
        NtClose((HANDLE)Evnt->_Data.DirFd);
        if(Evnt->_Data.DirName != NULL)
            free(Evnt->_Data.DirName);
        if(Evnt->_Data.Buffer != NULL)
            free(Evnt->_Data.Buffer);
    }
}

LQ_EXTERN_C int LQ_CALL LqEnvSet(const char* Name, const char* Value) {
    wchar_t NameW[32768];
    wchar_t ValueW[32768];
    LqCpConvertToWcs(Name, NameW, 32767);
    if(Value != NULL)
        LqCpConvertToWcs(Value, ValueW, 32767);
    return (SetEnvironmentVariableW(NameW, (Value != NULL) ? ValueW : NULL) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqEnvGet(const char* Name, char* Value, size_t ValueBufLen) {
    wchar_t NameW[32768];
    wchar_t ValueW[32768];
    LqCpConvertToWcs(Name, NameW, 32767);

    if(GetEnvironmentVariableW(NameW, ValueW, 32767) == 0)
        return -1;
    return LqCpConvertFromWcs(ValueW, Value, ValueBufLen);
}

LQ_EXTERN_C int LQ_CALL LqEnvGetAll(char* Buf, size_t BufLen) {
    LPWCH Wch = GetEnvironmentStringsW();
    if(Wch == NULL)
        return -1;
    if(BufLen <= 4) {
        FreeEnvironmentStringsW(Wch);
        lq_errno_set(EINVAL);
        return -1;
    }
    char* m = Buf + BufLen - 1, *s = Buf;
    int j = 0;
    auto FreeWch = Wch;
    for(; *Wch != L'\0';) {
        auto l = m - s;
        auto k = LqCpConvertFromWcs(Wch, s, l);
        if((k + 1) >= l)
            break;
        s += k;
        while(*(Wch++) != L'\0');
        j++;
    }
    FreeEnvironmentStringsW(FreeWch);
    *s = '\0';
    return j;
}
LQ_EXTERN_C bool LQ_CALL LqMutexCreate(LqMutex* Dest) {
    return (Dest->m = CreateMutexW(NULL, FALSE, NULL)) != NULL;
}
LQ_EXTERN_C bool LQ_CALL LqMutexTryLock(LqMutex* Dest) {
    return WaitForSingleObject(Dest->m, 0) == WAIT_OBJECT_0;
}
LQ_EXTERN_C bool LQ_CALL LqMutexLock(LqMutex* Dest) {
    return WaitForSingleObject(Dest->m, INFINITE) == WAIT_OBJECT_0;
}
LQ_EXTERN_C bool LQ_CALL LqMutexUnlock(LqMutex* lqaio Dest) {
    return ReleaseMutex(Dest->m) == TRUE;
}
LQ_EXTERN_C bool LQ_CALL LqMutexClose(LqMutex* lqain Dest) {
    return CloseHandle(Dest->m);
}

LQ_EXTERN_C int LQ_CALL LqThreadId() {
    return GetCurrentThreadId();
}

LQ_EXTERN_C void LQ_CALL LqThreadYield() {
    Sleep(0);
}

LQ_EXTERN_C int LQ_CALL LqThreadConcurrency() {
    SYSTEM_INFO sysinfo = {0};
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
}

LQ_EXTERN_C void LQ_CALL LqSleep(LqTimeMillisec Millisec) {
    Sleep(Millisec);
}

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#else


#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/socket.h> 

#include <sys/wait.h>

#include <sys/eventfd.h>
#include <sys/inotify.h>

#include <vector>
#include "LqAtm.hpp"
#include "LqLock.hpp"

#ifndef O_SHORT_LIVED
#define O_SHORT_LIVED 0
#endif

#ifndef O_DSYNC
#define O_DSYNC 0
#endif

#ifndef O_TEMPORARY
#define O_TEMPORARY 0
#endif // O_TEMPORARY

#ifndef O_BINARY
#define O_BINARY 0
#endif // O_TEMPORARY

#ifndef O_TEXT
#define O_TEXT 0
#endif // O_TEXT

#ifndef O_NOINHERIT
#define O_NOINHERIT 0
#endif

#ifndef O_SEQUENTIAL
#define O_SEQUENTIAL 0
#endif

#ifndef O_RANDOM
#define O_RANDOM 0
#endif

#ifndef S_IFIFO
#define S_IFIFO 0xffff
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

LQ_EXTERN_C intptr_t LQ_CALL LqFileGetPath(int Fd, char* DestBuf, intptr_t SizeBuf) {
    char PathToFd[64];
    LqFbuf_snprintf(PathToFd, sizeof(PathToFd), "/proc/self/fd/%i", Fd);
    return readlink(PathToFd, DestBuf, SizeBuf);
}

LQ_EXTERN_C int LQ_CALL LqFileOpen(const char *FileName, uint32_t Flags, int Access) {
    int DecodedFlags =
        ((Flags & LQ_O_RDWR) ? O_RDWR : ((Flags & LQ_O_WR) ? O_WRONLY : O_RDONLY)) |
        ((Flags & LQ_O_CREATE) ? O_CREAT : 0) |
        ((Flags & LQ_O_TMP) ? O_TEMPORARY : 0) |
        ((Flags & LQ_O_BIN) ? O_BINARY : 0) |
        ((Flags & LQ_O_TXT) ? O_TEXT : 0) |
        ((Flags & LQ_O_APND) ? O_APPEND : 0) |
        ((Flags & LQ_O_TRUNC) ? O_TRUNC : 0) |
        ((Flags & LQ_O_NOINHERIT) ? O_NOINHERIT : 0) |
        ((Flags & LQ_O_NOINHERIT) ? O_CLOEXEC : 0) |
        ((Flags & LQ_O_SEQ) ? O_SEQUENTIAL : 0) |
        ((Flags & LQ_O_RND) ? O_RANDOM : 0) |
        ((Flags & LQ_O_EXCL) ? O_EXCL : 0) |
        ((Flags & LQ_O_DSYNC) ? O_DSYNC : 0) |
        ((Flags & LQ_O_SHORT_LIVED) ? O_SHORT_LIVED : 0) |
        ((Flags & LQ_O_NONBLOCK) ? O_NONBLOCK : 0);
    return open(FileName, DecodedFlags, Access);
}

LQ_EXTERN_C bool LQ_CALL LqFileIsExist(const char* FileName) {
    return access(FileName, F_OK) == 0;
}

LQ_EXTERN_C int LQ_CALL LqFileGetStat(const char* FileName, LqFileStat* StatDest) {
    struct stat64 s;
    if(stat64(FileName, &s) == -1)
        return -1;

    StatDest->Size = s.st_size;
    StatDest->CreateTime = s.st_ctime;
    StatDest->AccessTime = s.st_atime;
    StatDest->ModifTime = s.st_mtime;
    StatDest->Gid = s.st_gid;
    StatDest->Uid = s.st_uid;
    StatDest->Id = s.st_ino;
    StatDest->DevId = s.st_dev;
    StatDest->RefCount = s.st_nlink;
    StatDest->Access = s.st_mode & 0777;
    if(S_ISDIR(s.st_mode))
        StatDest->Type = LQ_F_DIR;
    else if(S_ISREG(s.st_mode))
        StatDest->Type = LQ_F_REG;
    else
        StatDest->Type = LQ_F_OTHER;
    return 0;
}


LQ_EXTERN_C int LQ_CALL LqFileGetStatByFd(int Fd, LqFileStat* StatDest) {
    struct stat64 s;
    if(fstat64(Fd, &s) == -1)
        return -1;
    StatDest->Size = s.st_size;
    StatDest->CreateTime = s.st_ctime;
    StatDest->AccessTime = s.st_atime;
    StatDest->ModifTime = s.st_mtime;
    StatDest->Gid = s.st_gid;
    StatDest->Uid = s.st_uid;
    StatDest->Id = s.st_ino;
    StatDest->DevId = s.st_dev;
    StatDest->RefCount = s.st_nlink;
    StatDest->Access = s.st_mode & 0777;
    StatDest->Type = S_IFREG;
    if(S_ISDIR(s.st_mode))
        StatDest->Type = LQ_F_DIR;
    else if(S_ISREG(s.st_mode))
        StatDest->Type = LQ_F_REG;
    else
        StatDest->Type = LQ_F_OTHER;
    return 0;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFileTell(int Fd) {
    return lseek64(Fd, 0, SEEK_CUR);
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFileSeek(int Fd, LqFileSz Offset, int Flag) {
    return lseek64(Fd, Offset, Flag);
}

LQ_EXTERN_C int LQ_CALL LqFileMakeDir(const char* NewDirName, int Access) {
    return mkdir(NewDirName, Access);
}

LQ_EXTERN_C int LQ_CALL LqFileRemoveDir(const char* NewDirName) {
    return rmdir(NewDirName);
}

LQ_EXTERN_C int LQ_CALL LqFileMove(const char* OldName, const char* NewName) {
    return rename(OldName, NewName);
}

LQ_EXTERN_C int LQ_CALL LqFileRemove(const char* FileName) {
    return unlink(FileName);
}

LQ_EXTERN_C int LQ_CALL LqFileClose(int Fd) {
    return close(Fd);
}

LQ_EXTERN_C int LQ_CALL LqFileEof(int Fd) {
    LqFileStat Fs = {0};
    if(LqFileGetStatByFd(Fd, &Fs) == -1)
        return -1;
    return LqFileTell(Fd) >= Fs.Size;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileRead(int Fd, void* DestBuf, intptr_t SizeBuf) {
    int Res = read(Fd, DestBuf, SizeBuf);
    if(Res == -1)
        return -((intptr_t)1);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileWrite(int Fd, const void* SourceBuf, intptr_t SizeBuf) {
    int Res = write(Fd, SourceBuf, SizeBuf);
    if(Res == -1)
        return -((intptr_t)1);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileReadBlocked(int Fd, void* lqaout DestBuf, intptr_t SizeBuf) {
    int Flags = fcntl(Fd, F_GETFL, 0);
    fcntl(Fd, F_SETFL, Flags | ~O_NONBLOCK);
    int Res = read(Fd, DestBuf, SizeBuf);
    fcntl(Fd, F_SETFL, Flags);
    if(Res == -1)
        return -((intptr_t)1);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileWriteBlocked(int Fd, const void* lqain SourceBuf, intptr_t SizeBuf) {
    int Flags = fcntl(Fd, F_GETFL, 0);
    fcntl(Fd, F_SETFL, Flags | ~O_NONBLOCK);
    int Res = write(Fd, SourceBuf, SizeBuf);
    fcntl(Fd, F_SETFL, Flags);
    if(Res == -1)
        return -((intptr_t)1);
    return Res;
}


#ifndef LQ_ASYNC_IO_NOT_HAVE
#define IO_SIGNAL SIGUSR1

static void __LqAioSigHandler(int sig, siginfo_t *si, void *ucontext) {
    if(si->si_code == SI_ASYNCIO) {
        LqAsync* Target = (LqAsync*)si->si_value.sival_ptr;
        if(Target->IsNonBlock)
            fcntl(Target->cb.aio_fildes, F_SETFL, fcntl(Target->cb.aio_fildes, F_GETFL, 0) | O_NONBLOCK);
        LqEventSet(Target->EvFd);
    }
}

#endif

static int InitSignal() {
#ifdef LQ_ASYNC_IO_NOT_HAVE
    return ENOSYS;
#else
    static int _LqAioInit = 0;
    int t = 0;
    if(LqAtmCmpXchg(_LqAioInit, t, 1)) {
        struct sigaction Act = {0};
        Act.sa_sigaction = __LqAioSigHandler;
        Act.sa_flags = SA_RESTART | SA_SIGINFO;
        sigaction(IO_SIGNAL, &Act, NULL);
        return 0;
    }
    return -1;
#endif
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileReadAsync(int Fd, void* DestBuf, intptr_t SizeBuf, LqFileSz Offset, int EventFd, LqAsync* Target) {
#ifdef LQ_ASYNC_IO_NOT_HAVE
    return ENOSYS;
#else
    InitSignal();
    Target->EvFd = EventFd;
    Target->cb.aio_fildes = Fd;
    Target->cb.aio_offset = Offset;
    Target->cb.aio_buf = DestBuf;
    Target->cb.aio_nbytes = SizeBuf;
    Target->cb.aio_reqprio = 0;
    Target->cb.aio_lio_opcode = LIO_READ;
    Target->cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    Target->cb.aio_sigevent.sigev_signo = IO_SIGNAL;
    Target->cb.aio_sigevent.sigev_value.sival_ptr = Target;
    auto Flags = fcntl(Fd, F_GETFL, 0);
    if(Flags & O_NONBLOCK) {
        Target->IsNonBlock = 1;
        fcntl(Fd, F_SETFL, Flags & ~O_NONBLOCK);
    } else {
        Target->IsNonBlock = 0;
    }
    auto Ret = aio_read(&Target->cb);
    if((Ret == -1) && (Target->IsNonBlock))
        fcntl(Fd, F_SETFL, Flags | O_NONBLOCK);
    return Ret;
#endif
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileWriteAsync(int Fd, const void* SourceBuf, intptr_t SizeBuf, LqFileSz Offset, int EventFd, LqAsync* Target) {
#ifdef LQ_ASYNC_IO_NOT_HAVE
    return ENOSYS;
#else
    InitSignal();

    Target->EvFd = EventFd;
    Target->cb.aio_fildes = Fd;
    Target->cb.aio_offset = Offset;
    Target->cb.aio_buf = (void*)SourceBuf;
    Target->cb.aio_nbytes = SizeBuf;
    Target->cb.aio_reqprio = 0;
    Target->cb.aio_lio_opcode = LIO_WRITE;
    Target->cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    Target->cb.aio_sigevent.sigev_signo = IO_SIGNAL;
    Target->cb.aio_sigevent.sigev_value.sival_ptr = Target;
    auto Flags = fcntl(Fd, F_GETFL, 0);
    if(Flags & O_NONBLOCK) {
        Target->IsNonBlock = 1;
        fcntl(Fd, F_SETFL, Flags & ~O_NONBLOCK);
    } else {
        Target->IsNonBlock = 0;
    }
    auto Ret = aio_write(&Target->cb);
    if((Ret == -1) && (Target->IsNonBlock))
        fcntl(Fd, F_SETFL, Flags | O_NONBLOCK);
    return Ret;
#endif
}

LQ_EXTERN_C int LQ_CALL LqFileAsyncCancel(int Fd, LqAsync* Target) {
#ifdef LQ_ASYNC_IO_NOT_HAVE
    return ENOSYS;
#else
    switch(aio_cancel(Fd, &Target->cb)) {
        case AIO_CANCELED:
        case AIO_ALLDONE:
            if(Target->IsNonBlock)
                fcntl(Target->cb.aio_fildes, F_SETFL, fcntl(Target->cb.aio_fildes, F_GETFL, 0) | O_NONBLOCK);
            return 0;
        case AIO_NOTCANCELED:
            return -1;
        case -1:
        default:
            return -1;
    }
#endif
}


LQ_EXTERN_C int LQ_CALL LqFileAsyncStat(LqAsync* Target, intptr_t* LenWritten) {
#ifdef LQ_ASYNC_IO_NOT_HAVE
    return ENOSYS;
#else
    switch(auto Stat = aio_error(&Target->cb)) {
        case 0:
            *LenWritten = aio_return(&Target->cb);
            return 0;
        default:
            lq_errno_set(Stat);
            return Stat;
    }
#endif
}

LQ_EXTERN_C int LQ_CALL LqFileSetLock(int Fd, LqFileSz StartOffset, LqFileSz Len, int LockFlags) {
    struct flock lck = {0};
    lck.l_whence = SEEK_SET;
    lck.l_start = StartOffset;
    lck.l_len = Len;
    lck.l_pid = getpid();
    lck.l_type = (LockFlags & LQ_FLOCK_UNLOCK) ? F_UNLCK : ((LockFlags & LQ_FLOCK_WR) ? F_WRLCK : F_RDLCK);
    return fcntl(Fd, (LockFlags & LQ_FLOCK_WAIT) ? F_SETLKW : F_SETLK, &lck);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFileRealPath(const char* Source, char* Dest, intptr_t DestLen) {
    char Buf[PATH_MAX];
    auto Ret = realpath(Source, Buf);
    if(Ret == NULL)
        return -1;
    auto Len = LqStrCopyMax(Dest, Ret, DestLen);
    if(Ret != Buf)
        free(Ret);
    return Len;
}

LQ_EXTERN_C int LQ_CALL LqFileFlush(int Fd) {
    return fsync(Fd);
}

/*
* In unix all as standart poll
*/
LQ_EXTERN_C int LQ_CALL LqPollCheck(LqPoll* Fds, size_t CountFds, LqTimeMillisec TimeoutMillisec) {
    return poll(Fds, CountFds, TimeoutMillisec);
}

/*------------------------------------------
* Pipes
*/

LQ_EXTERN_C int LQ_CALL LqPipeCreate(int* lpReadPipe, int* lpWritePipe, uint32_t FlagsRead, uint32_t FlagsWrite) {
    int PipeFd[2];
    if(pipe(PipeFd) == -1)
        return -1;
    if(FlagsRead & LQ_O_NONBLOCK)
        fcntl(PipeFd[0], F_SETFL, fcntl(PipeFd[0], F_GETFL, 0) | O_NONBLOCK);
    if(FlagsRead & LQ_O_NOINHERIT)
        fcntl(PipeFd[0], F_SETFD, fcntl(PipeFd[0], F_GETFD) | FD_CLOEXEC);

    if(FlagsWrite & LQ_O_NONBLOCK)
        fcntl(PipeFd[1], F_SETFL, fcntl(PipeFd[1], F_GETFL, 0) | O_NONBLOCK);
    if(FlagsWrite & LQ_O_NOINHERIT)
        fcntl(PipeFd[1], F_SETFD, fcntl(PipeFd[1], F_GETFD) | FD_CLOEXEC);
    *lpReadPipe = PipeFd[0];
    *lpWritePipe = PipeFd[1];
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqPipeCreateNamed(const char* NameOfPipe, uint32_t Flags) {
    if(mkfifo(NameOfPipe, 0) == -1)
        return -1;
    return LqFileOpen(NameOfPipe, (Flags & (~LQ_O_CREATE)) | LQ_O_TMP, 0);
}

LQ_EXTERN_C int LQ_CALL LqPipeCreateRw(int* Pipe1, int* Pipe2, uint32_t Flags1, uint32_t Flags2) {
    int SocketFd[2];
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, SocketFd) < 0)
        return -1;
    if(Flags1 & LQ_O_NONBLOCK)
        fcntl(SocketFd[0], F_SETFL, fcntl(SocketFd[0], F_GETFL, 0) | O_NONBLOCK);
    if(Flags1 & LQ_O_NOINHERIT)
        fcntl(SocketFd[0], F_SETFD, fcntl(SocketFd[0], F_GETFD) | FD_CLOEXEC);

    if(Flags2 & LQ_O_NONBLOCK)
        fcntl(SocketFd[1], F_SETFL, fcntl(SocketFd[1], F_GETFL, 0) | O_NONBLOCK);
    if(Flags2 & LQ_O_NOINHERIT)
        fcntl(SocketFd[1], F_SETFD, fcntl(SocketFd[1], F_GETFD) | FD_CLOEXEC);
    *Pipe1 = SocketFd[0];
    *Pipe2 = SocketFd[1];
    return 0;
}

/*------------------------------------------
* Descriptors
*/

LQ_EXTERN_C int LQ_CALL LqDescrDup(int Descriptor, int InheritFlag) {
    int h = dup(Descriptor);
    if(h == -1)
        return -1;
    if(LQ_O_NOINHERIT & InheritFlag)
        LqDescrSetInherit(h, 0);
    return h;
}

LQ_EXTERN_C int LQ_CALL LqDescrSetInherit(int Descriptor, int IsInherit) {
    auto Val = fcntl(Descriptor, F_GETFD);
    if(Val < 0)
        return -1;
    return fcntl(Descriptor, F_SETFD, (IsInherit) ? (Val & ~(FD_CLOEXEC)) : (Val | FD_CLOEXEC));
}

LQ_EXTERN_C int LQ_CALL LqDescrDupToStd(int Descriptor, int StdNo) {
    auto Res = dup2(Descriptor, StdNo);
    return lq_min(Res, 0);
}

/*------------------------------------------
* Process
*/


LQ_EXTERN_C int LQ_CALL LqProcessCreate
(
    const char* FileName,
    char* const Argv[],
    char* const Envp[],
    const char* WorkingDir,
    int StdIn,
    int StdOut,
    int StdErr,
    int* EventKill,
    bool IsOwnerGroup
) {
    int EventParent = -1, EventChild = -1;
    pid_t Pid;
    int TestPipe[2];
    int64_t Err = 0;

    static volatile int _init = ([] {
        struct sigaction Act = {0};
        Act.sa_handler = [](int Sig) {
            int Stat;
            int Pid = waitpid(-1, &Stat, 0);
        };
        sigemptyset(&Act.sa_mask);
        sigaddset(&Act.sa_mask, SIGCHLD);

        sigaction(SIGCHLD, &Act, NULL);
        return 0;
    })();

    if(EventKill != NULL) {
        if(LqPipeCreate(&EventParent, &EventChild, LQ_O_NONBLOCK | LQ_O_BIN, LQ_O_NONBLOCK) == -1)
            goto lblErr;
    }

    pipe(TestPipe);
    fcntl(TestPipe[1], F_SETFD, fcntl(TestPipe[1], F_GETFD) | FD_CLOEXEC);
    Pid = fork();
    if(Pid == 0) {
        auto ThisPid = getpid();
        close(TestPipe[0]);
        lq_errno_set(0);
        setsid();
        if(IsOwnerGroup)
            setpgid(ThisPid, ThisPid);


        if(StdIn != -1) {
            if(LqDescrDupToStd(StdIn, STDIN_FILENO) == -1) {
                Err = lq_errno;
                write(TestPipe[1], &Err, sizeof(Err));
                return -1;
            }
        }
        if(StdOut != -1) {
            if(LqDescrDupToStd(StdOut, STDOUT_FILENO) == -1) {
                Err = lq_errno;
                write(TestPipe[1], &Err, sizeof(Err));
                return -1;
            }
        }
        if(StdErr != -1) {
            if(LqDescrDupToStd(StdErr, STDERR_FILENO) == -1) {
                Err = lq_errno;
                write(TestPipe[1], &Err, sizeof(Err));
                return -1;
            }
        }
        if(EventParent != -1)
            LqFileClose(EventParent);

        if(WorkingDir != NULL)
            chdir(WorkingDir);

        std::vector<char*> Argv2;
        Argv2.push_back(FileName);
        if(Argv != NULL) {
            for(size_t i = 0; Argv[i] != NULL; i++)
                Argv2.push_back(Argv[i]);
        }
        Argv2.push_back(NULL);

        int r = (Envp == NULL) ? execvp(FileName, Argv2.data()) : execve(FileName, Argv2.data(), Envp);
        Err = lq_errno;
        write(TestPipe[1], &Err, sizeof(Err));
        exit(r);
    } else if(Pid > 0) {
        close(TestPipe[1]);
        if(read(TestPipe[0], &Err, sizeof(Err)) > 0) // Wait error from forket process
            goto lblErr2;
        //If not have error
        close(TestPipe[0]);
        if(EventChild != -1) {
            LqFileClose(EventChild);
            *EventKill = EventParent;
        }
    } else {
        close(TestPipe[1]);
lblErr2:
        close(TestPipe[0]);
        if(EventParent != -1) {
            LqFileClose(EventParent);
            LqFileClose(EventChild);
        }
lblErr:
        if(Err != 0)
            lq_errno_set(Err);
        return -1;
    }
    return Pid;
}

LQ_EXTERN_C int LQ_CALL LqProcessKill(int Pid) {
    return kill(Pid, SIGTERM);
}


LQ_EXTERN_C int LQ_CALL LqProcessId() {
    return getpid();
}

LQ_EXTERN_C int LQ_CALL LqProcessParentId() {
    return getppid();
}

LQ_EXTERN_C intptr_t LQ_CALL LqProcessGetName(int Pid, char* DestBuf, intptr_t SizeBuf) {
    char Buf[64];
    LqFbuf_snprintf(Buf, sizeof(Buf), "/proc/%i/exe", Pid);
    return readlink(Buf, DestBuf, SizeBuf);
}

LQ_EXTERN_C int LQ_CALL LqFileSetCurDir(const char* NewDir) {
    return chdir(NewDir);
}

LQ_EXTERN_C int LQ_CALL LqFileGetCurDir(char* DirBuf, size_t LenBuf) {
    return (getcwd(DirBuf, LenBuf) != NULL) ? 0 : -1;
}


/*------------------------------------------
* FileEnm
*/

LQ_EXTERN_C int LQ_CALL LqFileEnmStart(LqFileEnm* Enm, const char* Dir, char* DestName, size_t NameLen, uint8_t* Type) {
    auto Hndl = opendir(Dir);
    if(Hndl == NULL)
        return -1;

    auto Entry = readdir(Hndl);
    if(Entry == NULL) {
        closedir(Hndl);
        Enm->Hndl = 0;
        return -1;
    }
    Enm->Hndl = (uintptr_t)Hndl;
    LqStrCopyMax(DestName, Entry->d_name, NameLen);
#if !defined(_DIRENT_HAVE_D_TYPE)
    Enm->Internal = LqStrDuplicate(Dir);
#endif
    if(Type != NULL) {
#if defined(_DIRENT_HAVE_D_TYPE)
        if(Entry->d_type == DT_DIR)
            *Type = LQ_F_DIR;
        else if(Entry->d_type == DT_REG)
            *Type = LQ_F_REG;
        else
            *Type = LQ_F_OTHER;
#else
        LqFileStat Stat;
        LqString FullPath = Enm->Internal;
        if(FullPath[FullPath.length() - 1] != '/')
            FullPath += "/";
        FullPath += Entry->d_name;
        if(LqFileGetStat(FullPath.c_str(), &Stat) == -1) {
            closedir(Hndl);
            free(Enm->Internal);
            Enm->Hndl = 0;
            return -1;
        }
        *Type = Stat.Type;
#endif
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqFileEnmNext(LqFileEnm* Enm, char* DestName, size_t NameLen, uint8_t* Type) {
    auto Entry = readdir((DIR*)Enm->Hndl);
    if(Entry == NULL) {
        closedir((DIR*)Enm->Hndl);
        Enm->Hndl = 0;
#if !defined(_DIRENT_HAVE_D_TYPE)
        free(Enm->Internal);
#endif
        return -1;
    }
    LqStrCopyMax(DestName, Entry->d_name, NameLen);
    if(Type != NULL) {
#if defined(_DIRENT_HAVE_D_TYPE)
        if(Entry->d_type == DT_DIR)
            *Type = LQ_F_DIR;
        else if(Entry->d_type == DT_REG)
            *Type = LQ_F_REG;
        else
            *Type = LQ_F_OTHER;
#else
        LqFileStat Stat;
        LqString FullPath = Enm->Internal;
        if(FullPath[FullPath.length() - 1] != '/')
            FullPath += "/";
        FullPath += Entry->d_name;
        if(LqFileGetStat(FullPath.c_str(), &Stat) == -1) {
            closedir((DIR*)Enm->Hndl);
            free(Enm->Internal);
            Enm->Hndl = 0;
            return -1;
        }
        *Type = Stat.Type;
#endif
    }
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFileEnmBreak(LqFileEnm* Enm) {
    if(Enm->Hndl != 0) {
        closedir((DIR*)Enm->Hndl);
#if !defined(_DIRENT_HAVE_D_TYPE)
        free(Enm->Internal);
#endif
    }
}


#if defined(LQPLATFORM_LINUX) || defined(LQPLATFORM_ANDROID)

#include <unistd.h>
#include <sys/syscall.h>

#if !defined(SYS_timerfd_create) || !defined(SYS_timerfd_settime)
# if defined(__i386__)
#  define SYS_timerfd_create 322
#  define SYS_timerfd_settime 325
# elif defined(__x86_64__)
#  define SYS_timerfd_create 283
#  define SYS_timerfd_settime 286
# elif defined(__arm__)
#  define SYS_timerfd_create 350
#  define SYS_timerfd_settime 353
# elif defined(__aarch64__) //arm64
#  define SYS_timerfd_create 85
#  define SYS_timerfd_settime 86
# else
#  error "no timerfd"
# endif
#endif

#if !defined(SYS_eventfd)
# if defined(__i386__)
#  define SYS_eventfd 323
# elif defined(__x86_64__)
#  define SYS_eventfd 284
# elif defined(__arm__)
#  define SYS_eventfd 351
# else
#  error "no eventfd"
# endif
#endif

#if !defined(SYS_shmget) || !defined(SYS_shmdt) || !defined(SYS_shmat) || !defined(SYS_shmctl)
# if defined(__i386__)
#  define SYS_shmget 29
#  define SYS_shmdt 67
#  define SYS_shmat 30
#  define SYS_shmctl 31

# elif defined(__x86_64__)
#  define SYS_shmget 29
#  define SYS_shmdt 67
#  define SYS_shmat 30
#  define SYS_shmctl 31

# elif defined(__arm__)
#  define SYS_shmget 307
#  define SYS_shmdt 306
#  define SYS_shmat 305
#  define SYS_shmctl 308

# else
#  error "no shared mem"
# endif
#endif


/*------------------------------------------
* Event
*/

LQ_EXTERN_C int LQ_CALL LqEventCreate(int InheritFlag) {
    int Res = syscall(SYS_eventfd, (unsigned int)0, (int)0);
    if(Res == -1)
        return -1;
    fcntl(Res, F_SETFL, fcntl(Res, F_GETFL, 0) | O_NONBLOCK);
    if(InheritFlag & LQ_O_NOINHERIT)
        fcntl(Res, F_SETFD, fcntl(Res, F_GETFD) | FD_CLOEXEC);
    return Res;
}

/*------------------------------------------
* Timer
*/
/*In some libraries not have timerfd defenitions (as in old bionic)*/
LQ_EXTERN_C int LQ_CALL LqTimerCreate(int InheritFlags) {
    int Res = syscall(SYS_timerfd_create, (int)CLOCK_MONOTONIC, (int)0);
    if(Res == -1)
        return -1;
    fcntl(Res, F_SETFL, fcntl(Res, F_GETFL, 0) | O_NONBLOCK);
    if(InheritFlags & LQ_O_NOINHERIT)
        fcntl(Res, F_SETFD, fcntl(Res, F_GETFD) | FD_CLOEXEC);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqTimerSet(int TimerFd, LqTimeMillisec Time) {
    uint64_t val = 0;
    int r;
    do
        r = read(TimerFd, &val, sizeof(val));
    while(r > 0);
    itimerspec Ts;
    Ts.it_value.tv_sec = Ts.it_interval.tv_sec = (time_t)(Time / 1000);
    Ts.it_value.tv_nsec = Ts.it_interval.tv_nsec = (long int)((Time % 1000) * 1000 * 1000);
    return syscall(SYS_timerfd_settime, TimerFd, (int)0, &Ts, (itimerspec*)NULL);
}

/*------------------------------------------
* Shared Memory
*/
#define IPC_RMID 0
#define IPC_CREAT 00001000

LQ_EXTERN_C int LQ_CALL LqSharedCreate(int key, size_t Size, int DscrFlags, int UserAccess) {
    int Res = syscall(SYS_shmget, key, (int)Size, (int)(UserAccess | IPC_CREAT));
    if(Res == -1)
        return -1;
    if(DscrFlags & LQ_O_NOINHERIT)
        LqDescrSetInherit(Res, 0);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSharedOpen(int key, size_t Size, int DscrFlags, int UserAccess) {
    int Res = syscall(SYS_shmget, key, (int)Size, (int)UserAccess);
    if(Res == -1)
        return -1;
    if(DscrFlags & LQ_O_NOINHERIT)
        LqDescrSetInherit(Res, 0);
    return Res;
}

LQ_EXTERN_C void* LQ_CALL LqSharedAt(int shmid, void* BaseAddress) {
    return syscall(SYS_shmat, shmid, (char*)BaseAddress, (int)0);
}

LQ_EXTERN_C int LQ_CALL LqSharedUnmap(void *addr) {
    return syscall(SYS_shmdt, addr);
}

LQ_EXTERN_C int LQ_CALL LqSharedClose(int shmid) {
    return syscall(SYS_shmctl, shmid, (int)IPC_RMID, (void*)NULL);
}

#else
#include <sys/timerfd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/*------------------------------------------
* Event
*/

LQ_EXTERN_C int LQ_CALL LqEventCreate(int InheritFlag) {
    int Res = eventfd(0, 0);
    if(Res == -1)
        return -1;
    fcntl(Res, F_SETFL, fcntl(Res, F_GETFL, 0) | O_NONBLOCK);
    if(InheritFlags & LQ_O_NOINHERIT)
        fcntl(Res, F_SETFD, fcntl(Res, F_GETFD) | FD_CLOEXEC);
    return Res;
}

/*------------------------------------------
* Timer
*/
LQ_EXTERN_C int LQ_CALL LqTimerCreate(int InheritFlags) {
    int Res = timerfd_create(CLOCK_MONOTONIC, 0);
    if(Res == -1)
        return -1;
    fcntl(Res, F_SETFL, fcntl(Res, F_GETFL, 0) | O_NONBLOCK);
    if(InheritFlags & LQ_O_NOINHERIT)
        fcntl(Res, F_SETFD, fcntl(Res, F_GETFD) | FD_CLOEXEC);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqTimerSet(int TimerFd, LqTimeMillisec Time) {
    uint64_t val = 0;
    int r;
    do
        r = read(TimerFd, &val, sizeof(val));
    while(r > 0);
    itimerspec Ts;
    Ts.it_value.tv_sec = Ts.it_interval.tv_sec = (time_t)(Time / 1000);
    Ts.it_value.tv_nsec = Ts.it_interval.tv_nsec = (long int)((Time % 1000) * 1000 * 1000);
    return timerfd_settime(TimerFd, 0, &Ts, NULL);
}

/*------------------------------------------
* Shared Memory
*/

LQ_EXTERN_C int LQ_CALL LqSharedCreate(int key, size_t Size, int DscrFlags, int UserAccess) {
    int Res = shmget(key, Size, UserAccess | IPC_CREAT);
    if(Res == -1)
        return -1;
    if(DscrFlags & LQ_O_NOINHERIT)
        LqDescrSetInherit(Res, 0);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSharedOpen(int key, size_t Size, int DscrFlags, int UserAccess) {
    int Res = shmget(key, Size, UserAccess);
    if(Res == -1)
        return -1;
    if(DscrFlags & LQ_O_NOINHERIT)
        LqDescrSetInherit(Res, 0);
    return Res;
}

LQ_EXTERN_C void* LQ_CALL LqSharedAt(int shmid, void* BaseAddress) {
    return shmat(shmid, BaseAddress, 0);
}

LQ_EXTERN_C int LQ_CALL LqSharedUnmap(void *addr) {
    return shmdt(addr);
}

LQ_EXTERN_C int LQ_CALL LqSharedClose(int shmid) {
    return shmctl(shmid, IPC_RMID, NULL);
}

#endif

LQ_EXTERN_C int LQ_CALL LqEventReset(int FileEvent) {
    eventfd_t r[20];
    return (read(FileEvent, &r, sizeof(r)) > 0) ? 1 : 0;
}

LQ_EXTERN_C int LQ_CALL LqEventSet(int FileEvent) {
    eventfd_t r = 1;
    return (write(FileEvent, &r, sizeof(r)) > 0) ? 0 : -1;
}



/*------------------------------------------
* inotify directory event
*/

static int InsertDir(LqFilePathEvnt* Evnt, int Pwd, const char* Name);
static int InsertDir(LqFilePathEvnt* Evnt, int Pwd, const char* Name, const char* FullName);

static void RcrvDirAdd(LqFilePathEvnt* Evnt, int Pwd, const char* TargetDir) {
    LqFileEnm i;
    char Buf[LQ_MAX_PATH];
    uint8_t Type;
    for(int r = LqFileEnmStart(&i, TargetDir, Buf, sizeof(Buf) - 1, &Type); r != -1; r = LqFileEnmNext(&i, Buf, sizeof(Buf) - 1, &Type)) {
        if(LqStrSame("..", Buf) || LqStrSame(".", Buf) || (Type != LQ_F_DIR))
            continue;
        LqString FullDirName = TargetDir;
        if((FullDirName.length() <= 0) || (FullDirName[FullDirName.length() - 1] != '/'))
            FullDirName += "/";
        FullDirName += Buf;
        int NewWd;
        if((NewWd = InsertDir(Evnt, Pwd, Buf, FullDirName.c_str())) == -1)
            continue;
        RcrvDirAdd(Evnt, NewWd, FullDirName.c_str());
    }
}


static void DirByWd(LqFilePathEvnt* Evnt, int Wd, LqString& DestPath) {
    if(Evnt->_Data.Subdirs[Wd].Pwd == -1) {
        DestPath += Evnt->_Data.Subdirs[Wd].Name;
        return;
    }
    DirByWd(Evnt, Evnt->_Data.Subdirs[Wd].Pwd, DestPath);
    DestPath += "/";
    DestPath += Evnt->_Data.Subdirs[Wd].Name;
}


static int InsertDir(LqFilePathEvnt* Evnt, int Pwd, const char* Name) {
    LqString FullPath;
    DirByWd(Evnt, Pwd, FullPath);
    if(FullPath[FullPath.length() - 1] != '/')
        FullPath += "/";
    FullPath += Name;
    int NewWd;
    if((NewWd = InsertDir(Evnt, Pwd, Name, FullPath.c_str())) == -1)
        return -1;
    RcrvDirAdd(Evnt, Pwd, FullPath.c_str());
    return NewWd;
}

static bool RcrvDirAdd(LqFilePathEvnt* Evnt) {
    LqString Buf;
    const char* Pth = Evnt->_Data.Name;
    auto l = LqStrLen(Evnt->_Data.Name);
    if(Evnt->_Data.Name[l - 1] != '/') {
        Buf = Evnt->_Data.Name;
        Buf += "/";
        Pth = Buf.c_str();
    }
    RcrvDirAdd(Evnt, Evnt->_Data.Wd, Pth);
    return true;
}

static int InsertDir(LqFilePathEvnt* Evnt, int Pwd, const char* Name, const char* FullName) {
    auto MaskEx = Evnt->_Data.Mask;
    if(Evnt->_Data.IsSubtree)
        MaskEx |= (((Pwd == -1) ? 0 : IN_DELETE_SELF) | IN_MOVED_FROM | IN_CREATE);
    auto Wd = inotify_add_watch(Evnt->Fd, FullName, MaskEx);
    if(Wd == -1)
        return -1;
    if(Wd >= Evnt->_Data.SubdirsCount) {
        Evnt->_Data.Subdirs = (LqFilePathEvnt::Subdir*)realloc(Evnt->_Data.Subdirs, sizeof(LqFilePathEvnt::Subdir) * (Wd + 1));
        Evnt->_Data.SubdirsCount = Wd + 1;
        for(int i = Evnt->_Data.Max + 1; i <= Wd; i++) {
            Evnt->_Data.Subdirs[i].Pwd = -1;
            Evnt->_Data.Subdirs[i].Name = NULL;
        }
        Evnt->_Data.Max = Wd;
    }
    Evnt->_Data.Subdirs[Wd].Pwd = Pwd;
    Evnt->_Data.Subdirs[Wd].Name = LqStrDuplicate(Name);
    return Wd;
}


static void RemoveByWd(LqFilePathEvnt* Evnt, int Wd) {
    free(Evnt->_Data.Subdirs[Wd].Name);
    Evnt->_Data.Subdirs[Wd].Name = NULL;
    Evnt->_Data.Subdirs[Wd].Pwd = -1;
    inotify_rm_watch(Evnt->Fd, Wd);
    if(Wd == Evnt->_Data.Max) {
        int i;
        for(i = Evnt->_Data.SubdirsCount - 1; i >= 0; i--) {
            if(Evnt->_Data.Subdirs[i].Name != NULL)
                break;
        }
        Evnt->_Data.Max = i;
        i++;
        Evnt->_Data.Subdirs = (LqFilePathEvnt::Subdir*)realloc(Evnt->_Data.Subdirs, sizeof(LqFilePathEvnt::Subdir) * i);
        Evnt->_Data.SubdirsCount = i;
    }
}

static void RemoveByPwdAndName(LqFilePathEvnt* Evnt, int Pwd, const char* Name) {
    for(int i = 0; i < Evnt->_Data.SubdirsCount; i++) {
        if((Evnt->_Data.Subdirs[i].Pwd == Pwd) && LqStrSame(Evnt->_Data.Subdirs[i].Name, Name)) {
            RemoveByWd(Evnt, i);
            return;
        }
    }
}

LQ_EXTERN_C int LQ_CALL LqFilePathEvntCreate(LqFilePathEvnt* Evnt, const char* DirOrFile, uint8_t FollowFlag) {
    uint32_t NotifyFilter = 0;
    bool IsWatchSubtree = false;
    if(FollowFlag & LQDIREVNT_ADDED)
        NotifyFilter |= IN_CREATE;
    if(FollowFlag & LQDIREVNT_MOD)
        NotifyFilter |= IN_CLOSE_WRITE;
    if(FollowFlag & LQDIREVNT_MOVE_TO)
        NotifyFilter |= IN_MOVED_TO;
    if(FollowFlag & LQDIREVNT_MOVE_FROM)
        NotifyFilter |= IN_MOVED_FROM;
    if(FollowFlag & LQDIREVNT_RM)
        NotifyFilter |= IN_DELETE;
    int Ifd = inotify_init();
    if(Ifd == -1)
        return -1;
    fcntl(Ifd, F_SETFL, fcntl(Ifd, F_GETFL, 0) | O_NONBLOCK);
    int Wd = inotify_add_watch(Ifd, DirOrFile, NotifyFilter | ((FollowFlag & LQDIREVNT_SUBTREE) ? IN_CREATE | IN_MOVED_FROM : 0));
    if(Wd == -1) {
        close(Ifd);
        return -1;
    }

    Evnt->Fd = Ifd;
    Evnt->_Data.Max = -1;
    Evnt->_Data.Mask = NotifyFilter;
    Evnt->_Data.IsSubtree = FollowFlag & LQDIREVNT_SUBTREE;
    Evnt->_Data.Wd = Wd;

    InsertDir(Evnt, -1, DirOrFile, DirOrFile);
    Evnt->_Data.Name = LqStrDuplicate(DirOrFile);

    if(FollowFlag & LQDIREVNT_SUBTREE)
        RcrvDirAdd(Evnt);
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFilePathEvntFreeEnum(LqFilePathEvntEnm** Dest) {
    if(Dest == NULL)
        return;
    for(auto i = *Dest; i != NULL; ) {
        auto j = i;
        i = i->Next;
        free(j);
    }
    *Dest = NULL;
}

LQ_EXTERN_C int LQ_CALL LqFilePathEvntDoEnum(LqFilePathEvnt* Evnt, LqFilePathEvntEnm** Dest) {
    LqFilePathEvntFreeEnum(Dest);
    char Buf[32768];

    LqFilePathEvntEnm* NewList = *Dest = NULL;
    int Count = 0;

    int Readed = read(Evnt->Fd, Buf, sizeof(Buf));
    if(Readed <= 0) {
        if(LQERR_IS_WOULD_BLOCK)
            return 0;

        return -1;
    }

    for(int Off = 0; Off < Readed; ) {
        auto Info = (struct inotify_event *)(Buf + Off);
        uint8_t Flag = 0;
        if(Info->mask & IN_CREATE) {
            Flag |= (Evnt->_Data.Mask & IN_CREATE) ? LQDIREVNT_ADDED : 0;
            if(Evnt->_Data.IsSubtree && (Info->mask & IN_ISDIR))
                InsertDir(Evnt, Info->wd, Info->name);
        }
        if(Info->mask & IN_CLOSE_WRITE)
            Flag |= LQDIREVNT_MOD;
        if(Info->mask & IN_MOVED_TO)
            Flag |= LQDIREVNT_MOVE_TO;

        if(Info->mask & IN_MOVED_FROM) {
            Flag |= (Evnt->_Data.Mask & IN_MOVED_FROM) ? LQDIREVNT_MOVE_FROM : 0;
            if(Evnt->_Data.IsSubtree && (Info->mask & IN_ISDIR))
                RemoveByPwdAndName(Evnt, Info->wd, Info->name);
        }
        if(Info->mask & IN_DELETE)
            Flag |= LQDIREVNT_RM;
        if((Info->mask & IN_DELETE_SELF) && Evnt->_Data.IsSubtree)
            RemoveByWd(Evnt, Info->wd);

        if(Flag != 0) {
            LqString FullPath;
            DirByWd(Evnt, Info->wd, FullPath);
            if(FullPath[FullPath.length() - 1] != '/')
                FullPath += "/";
            FullPath += Info->name;

            size_t NewSize = sizeof(LqFilePathEvntEnm) + FullPath.length() + 2;
            auto Val = (LqFilePathEvntEnm*)malloc(NewSize);
            LqStrCopy(Val->Name, FullPath.c_str());
            Val->Flag = Flag;
            Val->Next = NewList;
            NewList = Val;
            Count++;
        }
        Off += (sizeof(struct inotify_event) + Info->len);
    }
    *Dest = NewList;
    return Count;
}

LQ_IMPORTEXPORT int LQ_CALL LqFilePathEvntGetName(LqFilePathEvnt* Evnt, char* DestName, size_t DestNameSize) {
    return LqStrCopyMax(DestName, Evnt->_Data.Name, DestNameSize);
}


LQ_EXTERN_C void LQ_CALL LqFilePathEvntFree(LqFilePathEvnt* Evnt) {
    if(Evnt->Fd != -1) {
        if(Evnt->_Data.Name != NULL)
            free(Evnt->_Data.Name);
        for(int i = 0; i < Evnt->_Data.SubdirsCount; i++) {
            if(Evnt->_Data.Subdirs[i].Name != NULL)
                free(Evnt->_Data.Subdirs[i].Name);
        }
        if(Evnt->Fd != -1)
            close(Evnt->Fd);
    }
}

LQ_EXTERN_C int LQ_CALL LqTermPairCreate(int* MasterFd, int* SlaveFd, int MasterFlags, int SlaveFlags) {
    int   ptm = -1, pts = -1;
    char *tty = 0;
    if((ptm = open("/dev/ptmx", (O_RDWR | O_NOCTTY) | ((MasterFlags & LQ_O_NONBLOCK) ? O_NONBLOCK : 0) | ((MasterFlags & LQ_O_NOINHERIT) ? O_CLOEXEC : 0))) == -1)
        return -1;
    tty = ptsname(ptm);
    if(
        (grantpt(ptm) == -1) ||
        (unlockpt(ptm) == -1) ||
        ((pts = open(tty, (O_RDWR | O_NOCTTY) | ((SlaveFlags & LQ_O_NONBLOCK) ? O_NONBLOCK : 0) | ((SlaveFlags & LQ_O_NOINHERIT) ? O_CLOEXEC : 0))) == -1)
        ) {
        close(ptm);
        return -1;
    }

    *MasterFd = ptm;
    *SlaveFd = pts;
    return 0;
}


LQ_EXTERN_C bool LQ_CALL LqDescrIsTerminal(int Fd) {
    return isatty(Fd) == 1;
}

static LqLocker<uintptr_t> EnvLocker;

LQ_EXTERN_C int LQ_CALL LqEnvSet(const char* Name, const char* Value) {
    EnvLocker.LockWriteYield();
    auto Ret = (Value != NULL) ? setenv(Name, Value, 1) : unsetenv(Name);
    EnvLocker.UnlockWrite();
    return Ret;
}

LQ_EXTERN_C int LQ_CALL LqEnvGet(const char* Name, char* Value, size_t ValueBufLen) {
    EnvLocker.LockReadYield();
    auto Ret = getenv(Name);
    int Len;
    if(Ret != NULL)
        Len = LqStrCopyMax(Value, Ret, ValueBufLen);
    else
        Len = -1;
    EnvLocker.UnlockRead();
    return Len;
}

LQ_EXTERN_C int LQ_CALL LqEnvGetAll(char* Buf, size_t BufLen) {
    EnvLocker.LockReadYield();
    auto Envs = environ;
    if(Envs == NULL) {
        EnvLocker.UnlockRead();
        return -1;
    }
    if(BufLen <= 4) {
        EnvLocker.UnlockRead();
        lq_errno_set(EINVAL);
        return -1;
    }
    char* m = Buf + BufLen - 1;
    char* s = Buf;
    int j = 0;
    for(int i = 0; Envs[i] != '\0'; i++) {
        auto l = m - s;
        auto k = LqStrCopyMax(s, Envs[i], l);
        if((k + 1) >= l)
            break;
        s += (k + 1);
        j++;
    }
    EnvLocker.UnlockRead();
    *s = '\0';
    return j;
}

LQ_EXTERN_C bool LQ_CALL LqMutexCreate(LqMutex* Dest) {
    return pthread_mutex_init(&Dest->m, NULL) == 0;
}
LQ_EXTERN_C bool LQ_CALL LqMutexLock(LqMutex* Dest) {
    return pthread_mutex_lock(&Dest->m) == 0;
}
LQ_EXTERN_C bool LQ_CALL LqMutexTryLock(LqMutex* Dest) {
    return pthread_mutex_trylock(&Dest->m) == 0;
}
LQ_EXTERN_C bool LQ_CALL LqMutexUnlock(LqMutex* Dest) {
    return pthread_mutex_unlock(&Dest->m) == 0;
}
LQ_EXTERN_C bool LQ_CALL LqMutexClose(LqMutex* Dest) {
    pthread_mutex_unlock(&Dest->m);
    return pthread_mutex_destroy(&Dest->m) == 0;
}

LQ_EXTERN_C int LQ_CALL LqThreadId() {
    return pthread_self();
}

#if defined(LQPLATFORM_FREEBSD) || defined(LQPLATFORM_APPLE)

LQ_EXTERN_C int LQ_CALL LqThreadConcurrency() {
    int mib[4];
    int numCPU;
    size_t len = sizeof(numCPU);
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;
    sysctl(mib, 2, &numCPU, &len, NULL, 0);
    if(numCPU < 1) {
        mib[1] = HW_NCPU;
        sysctl(mib, 2, &numCPU, &len, NULL, 0);
        if(numCPU < 1)
            numCPU = 1;
    }
    return numCPU;
}

#elif defined(LQPLATFORM_LINUX) || defined(LQPLATFORM_ANDROID)

LQ_EXTERN_C int LQ_CALL LqThreadConcurrency() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

#endif

LQ_EXTERN_C void LQ_CALL LqThreadYield() {
    usleep(0);
}

LQ_EXTERN_C void LQ_CALL LqSleep(LqTimeMillisec Millisec) {
    usleep(Millisec * ((LqTimeMillisec)1000));
}

#endif

LQ_EXTERN_C bool LQ_CALL LqFileDirIsRoot(const char* DirOrFile) {
    if(LQ_PATH_SEPARATOR == '/') {
        return DirOrFile[0] == '/';
    } else {
        if(((((DirOrFile[0] >= 'a') && (DirOrFile[0] <= 'z')) || ((DirOrFile[0] >= 'A') && (DirOrFile[0] <= 'Z'))) && (DirOrFile[1] == ':')) ||
            ((DirOrFile[0] == '\\') && (DirOrFile[1] == '\\') && (DirOrFile[2] == '?') && (DirOrFile[3] == '\\'))) {
            return true;
        }
    }
    return false;
}

LQ_EXTERN_C int LQ_CALL LqFileMakeSubdirs(const char* NewSubdirsDirName, int Access) {

    size_t DirPos = 0;
    char c;
    char Name[LQ_MAX_PATH];
    char* Sep = Name;
    LqFileStat s;
    LqStrCopyMax(Name, NewSubdirsDirName, sizeof(Name));
    int RetStat = 1;
    while(true) {
        if((Sep = LqStrChr(Sep, LQ_PATH_SEPARATOR)) == NULL)
            break;
        Sep++;
        c = *Sep;
        *Sep = '\0';
        s.Type = 0;
        if(LqFileGetStat(Name, &s) == 0) {
            *Sep = c;
            if(s.Type == LQ_F_DIR)
                continue;
            return -1;
        } else {
            if(!LqFileMakeDir(Name, Access)) {
                *Sep = c;
                return -1;
            }
            RetStat = 0;
            *Sep = c;
        }
    }
    return RetStat;
}

LQ_EXTERN_C short LQ_CALL LqPollCheckSingle(int Fd, short Events, LqTimeMillisec TimeoutMillisec) {
    LqPoll Poll;
    Poll.fd = Fd;
    Poll.events = Events;
    Poll.revents = 0;
    if(LqPollCheck(&Poll, 1, TimeoutMillisec) == 1)
        return Poll.revents;
    return 0;
}

LQ_EXTERN_C bool LQ_CALL LqDescrIsSocket(int Fd) {
    int val;
    socklen_t len = sizeof(val);
    return getsockopt(Fd, SOL_SOCKET, SO_ACCEPTCONN, (char*)&val, &len) != -1;
}
