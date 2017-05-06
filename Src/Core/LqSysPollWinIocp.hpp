/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSysPoll... - Multiplatform abstracted event folower
* This part of server support:
*   +Windows window message. (one thread processing all message for multiple threads)
*   +Windows internal async iocp poll. (most powerfull method in windows)
*   +linux epoll.
*   +kevent for BSD like systems.(*But not yet implemented)
*   +poll for others unix systems.
*
*/

/*
    Most powerfull method in windows
        This method used internall AFD API. In some versions windows may not work.
    + Supports the processing of a large number of sockets (More then 1024 sockets (Ngnix not can))
    + Full independence of threads. This method not use another threads for processing kernell message(SysPollWin used another thread for all message).
        This method uses a separate completion port for each thread (as epoll in linux, but only requires reenable for each kernel message).

    - Some windows versions, maybe, cannot support this method. (Tested on win8 - work fine)
    - For console support only read mode.
*/


#ifndef LQ_EVNT
#error "Only use in LqEvnt.cpp !"
#endif
#include <winsock2.h>
#include <mswsock.h>
#include <Windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <stdint.h>

#include "LqFile.h"
#include "LqTime.hpp"
#include "LqAlloc.hpp"

#define LQ_AFD_POLL_TYPE_PIPE       ((uint8_t)0)
#define LQ_AFD_POLL_TYPE_EVENT      ((uint8_t)1)
#define LQ_AFD_POLL_TYPE_TERMINAL   ((uint8_t)2)

/* Values from AFD driver */

typedef enum __FILE_INFORMATION_CLASS {
    // end_wdm
    _FileDirectoryInformation = 1,
    _FileFullDirectoryInformation,   // 2
    _FileBothDirectoryInformation,   // 3
    _FileBasicInformation,           // 4  wdm
    _FileStandardInformation,        // 5  wdm
    _FileInternalInformation,        // 6
    _FileEaInformation,              // 7
    _FileAccessInformation,          // 8
    _FileNameInformation,            // 9
    _FileRenameInformation,          // 10
    _FileLinkInformation,            // 11
    _FileNamesInformation,           // 12
    _FileDispositionInformation,     // 13
    _FilePositionInformation,        // 14 wdm
    _FileFullEaInformation,          // 15
    _FileModeInformation,            // 16
    _FileAlignmentInformation,       // 17
    _FileAllInformation,             // 18
    _FileAllocationInformation,      // 19
    _FileEndOfFileInformation,       // 20 wdm
    _FileAlternateNameInformation,   // 21
    _FileStreamInformation,          // 22
    _FilePipeInformation,            // 23
    _FilePipeLocalInformation,       // 24
    _FilePipeRemoteInformation,      // 25
    _FileMailslotQueryInformation,   // 26
    _FileMailslotSetInformation,     // 27
    _FileCompressionInformation,     // 28
    _FileObjectIdInformation,        // 29
    _FileCompletionInformation,      // 30
    _FileMoveClusterInformation,     // 31
    _FileQuotaInformation,           // 32
    _FileReparsePointInformation,    // 33
    _FileNetworkOpenInformation,     // 34
    _FileAttributeTagInformation,    // 35
    _FileTrackingInformation,        // 36
    _FileIdBothDirectoryInformation, // 37
    _FileIdFullDirectoryInformation, // 38
    _FileValidDataLengthInformation, // 39
    _FileShortNameInformation,       // 40
    _FileReplaceCompletionInformation = 56 /* Since version  Win 8.1*/
    // begin_wdm
} __FILE_INFORMATION_CLASS;

#define AFD_POLL_RECEIVE_BIT            0
#define AFD_POLL_RECEIVE                (1 << AFD_POLL_RECEIVE_BIT)
#define AFD_POLL_RECEIVE_EXPEDITED_BIT  1
#define AFD_POLL_RECEIVE_EXPEDITED      (1 << AFD_POLL_RECEIVE_EXPEDITED_BIT)
#define AFD_POLL_SEND_BIT               2
#define AFD_POLL_SEND                   (1 << AFD_POLL_SEND_BIT)
#define AFD_POLL_DISCONNECT_BIT         3
#define AFD_POLL_DISCONNECT             (1 << AFD_POLL_DISCONNECT_BIT)
#define AFD_POLL_ABORT_BIT              4
#define AFD_POLL_ABORT                  (1 << AFD_POLL_ABORT_BIT)
#define AFD_POLL_LOCAL_CLOSE_BIT        5
#define AFD_POLL_LOCAL_CLOSE            (1 << AFD_POLL_LOCAL_CLOSE_BIT)
#define AFD_POLL_CONNECT_BIT            6
#define AFD_POLL_CONNECT                (1 << AFD_POLL_CONNECT_BIT)
#define AFD_POLL_ACCEPT_BIT             7
#define AFD_POLL_ACCEPT                 (1 << AFD_POLL_ACCEPT_BIT)
#define AFD_POLL_CONNECT_FAIL_BIT       8
#define AFD_POLL_CONNECT_FAIL           (1 << AFD_POLL_CONNECT_FAIL_BIT)
#define AFD_POLL_QOS_BIT                9
#define AFD_POLL_QOS                    (1 << AFD_POLL_QOS_BIT)
#define AFD_POLL_GROUP_QOS_BIT          10
#define AFD_POLL_GROUP_QOS              (1 << AFD_POLL_GROUP_QOS_BIT)
#define AFD_NUM_POLL_EVENTS             11
#define AFD_POLL_ALL                    ((1 << AFD_NUM_POLL_EVENTS) - 1)
#define AFD_POLL_ERR_BIT                12
#define AFD_POLL_ERR                    (1 << AFD_POLL_ERR_BIT)

#define AFD_POLL                    9

#define FSCTL_AFD_BASE FILE_DEVICE_NETWORK
#define _AFD_CONTROL_CODE(operation, method) ((FSCTL_AFD_BASE) << 12 | (operation << 2) | method)
#define IOCTL_AFD_POLL                    _AFD_CONTROL_CODE( AFD_POLL, METHOD_BUFFERED )


typedef struct _FILE_COMPLETION_INFORMATION {
    HANDLE Port;
    PVOID Key;
} FILE_COMPLETION_INFORMATION, *PFILE_COMPLETION_INFORMATION;

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

#define IO_COMPLETION_QUERY_STATE   0x0001
#define IO_COMPLETION_MODIFY_STATE  0x0002  // winnt
#define IO_COMPLETION_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0x3) // winnt

typedef enum _IO_COMPLETION_INFORMATION_CLASS {
    IoCompletionBasicInformation
} IO_COMPLETION_INFORMATION_CLASS;

typedef struct _IO_COMPLETION_BASIC_INFORMATION {
    LONG Depth;
} IO_COMPLETION_BASIC_INFORMATION, *PIO_COMPLETION_BASIC_INFORMATION;

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


typedef enum _WAIT_TYPE {
    WaitAll,
    WaitAny
} WAIT_TYPE;

extern "C" NTSYSAPI NTSTATUS NTAPI NtWaitForMultipleObjects(
    __in ULONG Count,
    __in_ecount(Count) HANDLE Handles[],
    __in WAIT_TYPE WaitType,
    __in BOOLEAN Alertable,
    __in_opt PLARGE_INTEGER Timeout
);

extern "C" __kernel_entry NTSTATUS NTAPI NtClearEvent(IN HANDLE EventHandle);

extern "C" NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(
    __in HANDLE FileHandle,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __in_bcount(Length) PVOID FileInformation,
    __in ULONG Length,
    __in __FILE_INFORMATION_CLASS FileInformationClass
);

extern "C" NTSYSAPI NTSTATUS NTAPI NtCreateIoCompletion(
    __out PHANDLE IoCompletionHandle,
    __in ACCESS_MASK DesiredAccess,
    __in_opt POBJECT_ATTRIBUTES ObjectAttributes,
    __in ULONG Count OPTIONAL
);

extern "C" NTSYSAPI NTSTATUS NTAPI NtSetIoCompletion(
    __in HANDLE IoCompletionHandle,
    __in PVOID KeyContext,
    __in_opt PVOID ApcContext,
    __in NTSTATUS IoStatus,
    __in ULONG_PTR IoStatusInformation
);

extern "C" NTSYSAPI NTSTATUS NTAPI NtRemoveIoCompletion(
    __in HANDLE IoCompletionHandle,
    __out PVOID *KeyContext,
    __out PVOID *ApcContext,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __in_opt PLARGE_INTEGER Timeout
);

extern "C" NTSYSAPI NTSTATUS NTAPI NtQueryIoCompletion(
    __in HANDLE IoCompletionHandle,
    __in IO_COMPLETION_INFORMATION_CLASS IoCompletionInformationClass,
    __out_bcount(IoCompletionInformationLength) PVOID IoCompletionInformation,
    __in ULONG IoCompletionInformationLength,
    __out_opt PULONG ReturnLength
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
extern "C" __kernel_entry NTSTATUS NTAPI NtClearEvent(
    __in HANDLE EventHandle
);

extern "C" __kernel_entry NTSTATUS NTAPI NtSetEvent(
    IN HANDLE EventHandle,
    OUT PLONG PreviousState OPTIONAL
);

extern "C" __kernel_entry NTSTATUS NTAPI NtQueryInformationFile(
    __in HANDLE FileHandle,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __out_bcount(Length) PVOID FileInformation,
    __in ULONG Length,
    __in __FILE_INFORMATION_CLASS FileInformationClass
);

typedef enum _EVENT_INFORMATION_CLASS {
    EventBasicInformation
} EVENT_INFORMATION_CLASS;

extern "C" __kernel_entry NTSTATUS NTAPI NtQueryEvent(
    __in HANDLE EventHandle,
    __in EVENT_INFORMATION_CLASS EventInformationClass,
    __out_bcount(EventInformationLength) PVOID EventInformation,
    __in ULONG EventInformationLength,
    __out_opt PULONG ReturnLength
);


#define LqEvntSystemEventByConnFlag(EvntFlags)            \
    ((((EvntFlags) & LQEVNT_FLAG_RD)        ? AFD_POLL_RECEIVE : 0)  |\
    (((EvntFlags) & LQEVNT_FLAG_WR)         ? AFD_POLL_SEND : 0) |\
    (((EvntFlags) & LQEVNT_FLAG_ACCEPT)     ? AFD_POLL_ACCEPT : 0)  |\
    (((EvntFlags) & LQEVNT_FLAG_CONNECT)    ? (AFD_POLL_CONNECT | AFD_POLL_CONNECT_FAIL) : 0) |\
    (((EvntFlags) & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) ? (AFD_POLL_DISCONNECT | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE): 0))

typedef struct _AFD_POLL_HANDLE_INFO {
    void* Handle;
    unsigned long Events;
    long Status;
} AFD_POLL_HANDLE_INFO, *PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
    union {
        struct {
            unsigned long LowPart;
            long HighPart;
        };
        long long QuadPart;
    } Timeout;
    unsigned long  NumberOfHandles;
    unsigned long  Exclusive;
    AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, *PAFD_POLL_INFO;

typedef struct _POLL_CONTEXT_BLOCK {
    void* SocketInfo;
    DWORD AsyncSelectSerialNumber;
    IO_STATUS_BLOCK IoStatus;
    AFD_POLL_INFO PollInfo;
    intptr_t Index;
} POLL_CONTEXT_BLOCK, *PPOLL_CONTEXT_BLOCK;

static LARGE_INTEGER ZeroTimeWait = {0};
static char AsyncBuf;

static bool SetCompletionInfo(HANDLE DestHandle, HANDLE IocpHandle, PVOID Key) {
    FILE_COMPLETION_INFORMATION completionInfo;
    IO_STATUS_BLOCK ioStatusBlock;
    completionInfo.Port = IocpHandle;
    completionInfo.Key = Key;
    NTSTATUS Status = NtSetInformationFile(
        DestHandle,
        &ioStatusBlock,
        &completionInfo,
        sizeof(completionInfo),
        (IocpHandle != NULL) ? _FileCompletionInformation : _FileReplaceCompletionInformation
    );
    if(Status != STATUS_SUCCESS) {
        SetLastError(RtlNtStatusToDosError(Status));
    }
    return NT_SUCCESS(Status);
}

bool LqSysPollInit(LqSysPoll* Events) {
    HANDLE Handle = NULL;
    UNICODE_STRING afdName;
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK ioStatusBlock;
    NTSTATUS Status;


    LqArr2Init(&Events->IocpArr);
    LqArr3Init(&Events->EvntArr);

    Events->EventObjectIndex = INTPTR_MAX;
    Events->ConnIndex = INTPTR_MAX;
    Events->CommonCount = 0;
    Events->IsHaveOnlyHup = (uintptr_t)0;
    Events->EnumCalledCount = (intptr_t)0;
    Events->PollBlock = NULL;
    Status = NtCreateIoCompletion(
        &Handle,
        IO_COMPLETION_ALL_ACCESS,
        NULL,
        1
    );
    Events->IocpHandle = (void*)Handle;
    RtlInitUnicodeString(&afdName, L"\\Device\\Afd\\AsyncSelectHlp");
    InitializeObjectAttributes(
        &objectAttributes,
        &afdName,
        OBJ_INHERIT | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
    );
    Status = NtCreateFile(
        &Handle,
        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
        &objectAttributes,
        &ioStatusBlock,
        NULL,
        0L,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        0,
        NULL,
        0
    );
    Events->AfdAsyncHandle = Handle;
    SetCompletionInfo(Handle, (HANDLE)Events->IocpHandle, NULL);
    return true;
}


void LqSysPollUninit(LqSysPoll* Dest) {
    NTSTATUS Status;
    PVOID KeyContext;
    IO_STATUS_BLOCK ioStatusBlock;
    PPOLL_CONTEXT_BLOCK PollBlock;


    LqArr2Uninit(&Dest->IocpArr);
    LqArr3Uninit(&Dest->EvntArr);
    Dest->EvntArr.Data = NULL;
    Dest->EvntArr.Data2 = NULL;
    Dest->IocpArr.Data = NULL;

    while(true) {
        Status = NtRemoveIoCompletion(
            Dest->IocpHandle,
            &KeyContext,
            (PVOID*)&PollBlock,
            &ioStatusBlock,
            &ZeroTimeWait
        );
        if(Status == STATUS_SUCCESS)
            LqFastAlloc::Delete(PollBlock);
        else
            break;
    }
    NtClose(Dest->AfdAsyncHandle);
    while(true) {
        Status = NtRemoveIoCompletion(
            Dest->IocpHandle,
            &KeyContext,
            (PVOID*)&PollBlock,
            &ioStatusBlock,
            &ZeroTimeWait
        );
        if(Status == STATUS_SUCCESS)
            LqFastAlloc::Delete(PollBlock);
        else
            break;
    }
    NtClose(Dest->IocpHandle);
    if(Dest->PollBlock != NULL)
        LqFastAlloc::Delete((PLARGE_INTEGER)Dest->PollBlock);
}

inline static intptr_t AddIocpElement(LqSysPoll* Events, LqConn* NewVal) {
    intptr_t InsertIndex;
    LqArr2PushBack(&(Events)->IocpArr, LqConn*, InsertIndex, NULL);
    LqArr2At(&(Events)->IocpArr, LqConn*, InsertIndex) = NewVal;
    return InsertIndex;
}

inline static void AddKernelEvent(LqSysPoll* Events, LqEvntFd* NewPtr, HANDLE NewHandleVal) {
    LqArr3PushBack(&(Events)->EvntArr, HANDLE, LqEvntFd*);
    LqArr3Back_1(&(Events)->EvntArr, HANDLE) = NewHandleVal;
    LqArr3Back_2(&(Events)->EvntArr, LqEvntFd*) = NewPtr;
}

#define RemoveKernelEvent(Events, Index) LqArr3RemoveAt(&(Events)->EvntArr, HANDLE, LqEvntFd*, (Index), NULL);
#define RemoveIocpElement(Events, Index) LqArr2RemoveAt(&(Events)->IocpArr, LqConn*, (Index), NULL);

#define HandleKernelEventAt(Events, Index) LqArr3At_1(&(Events)->EvntArr, HANDLE, (Index))
#define PtrKernelEventAt(Events, Index) LqArr3At_2(&(Events)->EvntArr, LqEvntFd*, (Index))
#define IocpAt(Events, Index) LqArr2At(&(Events)->IocpArr, LqConn*, (Index))


static void SockProcessAsyncSelect(LqSysPoll* Events, const intptr_t Index) {
    NTSTATUS status;
    PPOLL_CONTEXT_BLOCK Context;
    LqConn* Conn = IocpAt(Events, Index);
    if(Conn->_EmptyPollInfo != NULL) {
        Context = (PPOLL_CONTEXT_BLOCK)Conn->_EmptyPollInfo;
        Conn->_EmptyPollInfo = NULL;
    } else {
        Context = LqFastAlloc::New<POLL_CONTEXT_BLOCK>();
        Context->PollInfo.Timeout.HighPart = 0x7FFFFFFF;
        Context->PollInfo.Timeout.LowPart = 0xFFFFFFFF;
        Context->PollInfo.NumberOfHandles = 1;
        Context->PollInfo.Exclusive = TRUE;
        Context->Index = Index;
        Context->SocketInfo = Conn;
    }

    Context->PollInfo.Handles[0].Handle = (HANDLE)Conn->Fd;
    Context->PollInfo.Handles[0].Events = Conn->_AsyncPollEvents; //LqEvntSystemEventByConnFlag(NewFlags);
    Context->IoStatus.Status = Context->PollInfo.Handles[0].Status = STATUS_PENDING;
    Conn->_AsyncSelectSerialNumber++;
    Context->AsyncSelectSerialNumber = Conn->_AsyncSelectSerialNumber;
    if(Conn->_AsyncPollEvents == 0) {
        Context->SocketInfo = NULL;
        Context->Index = INTPTR_MAX;
    }
    status = NtDeviceIoControlFile(
        Events->AfdAsyncHandle,
        NULL,
        NULL,
        Context,
        &Context->IoStatus,
        IOCTL_AFD_POLL,
        &Context->PollInfo,
        sizeof(Context->PollInfo),
        &Context->PollInfo,
        sizeof(Context->PollInfo)
    );
    if(NT_ERROR(status)) {
        Context->PollInfo.Handles[0].Events = 0;
        Context->PollInfo.Handles[0].Status = status;
        if(NtSetIoCompletion(Events->IocpHandle, NULL, Context, STATUS_SUCCESS, sizeof(Context->PollInfo)) != STATUS_SUCCESS) {
            LqFastAlloc::Delete(Context);
        }
    }
}

static void SockUpdateAsyncSelect(LqSysPoll* Events, const intptr_t Index, const LqEvntFlag NewFlags) {
    LqConn* Conn = IocpAt(Events, Index);
    Conn->_AsyncPollEvents = LqEvntSystemEventByConnFlag(NewFlags);
    SockProcessAsyncSelect(Events, Index);
}

static void PipeProcessAsyncSelect(LqSysPoll* Events, const intptr_t Index) {
    LARGE_INTEGER *ppl, pl;
    NTSTATUS Status;
    IO_STATUS_BLOCK ioStatusBlock;
    __FILE_PIPE_LOCAL_INFORMATION PipeInfo;
    LqEvntFd* Fd = PtrKernelEventAt(Events, Index);
    HANDLE EventHandle = HandleKernelEventAt(Events, Index);
    const LqEvntFlag Flag = Fd->_AsyncPollEvents;
    LqEvntFlag ResFlag = (LqEvntFlag)0;
    if((Flag & LQEVNT_FLAG_WR) || ((Flag & (LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP)) && !(Flag & LQEVNT_FLAG_RD))) {
        Status = NtQueryInformationFile((HANDLE)Fd->Fd, &ioStatusBlock, &PipeInfo, sizeof(PipeInfo), _FilePipeLocalInformation);
        if(Status != STATUS_SUCCESS) {
            ResFlag |= LQEVNT_FLAG_ERR;
        } else {
            if((Flag & LQEVNT_FLAG_WR) && (PipeInfo.WriteQuotaAvailable > 0))
                ResFlag |= LQEVNT_FLAG_WR;
            if((Flag & LQEVNT_FLAG_RD) && (PipeInfo.ReadDataAvailable > 0))
                ResFlag |= LQEVNT_FLAG_RD;
            if((Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) && (PipeInfo.NamedPipeState != 3))
                ResFlag |= (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
        }
        if(ResFlag != ((LqEvntFlag)0)) {
            NtSetEvent(EventHandle, NULL);
        } else {
            NtClearEvent(EventHandle);
            Events->IsHaveOnlyHup = (uintptr_t)1;
        }
    } else {
        if(Flag & LQEVNT_FLAG_RD) {
            if(Fd->__Reserved1.Status != STATUS_PENDING) {
                ppl = NULL;
lblAgain:
                Fd->__Reserved1.Status = STATUS_PENDING;
                switch(Status = NtReadFile((HANDLE)Fd->Fd, EventHandle, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved1, &AsyncBuf, 0, ppl, NULL)) {
                    case STATUS_SUCCESS:
                        Fd->__Reserved1.Status = Status;
                        ResFlag |= LQEVNT_FLAG_RD;
                        break;
                    case STATUS_PENDING:
                        break;
                    case STATUS_PIPE_BROKEN:
                    case STATUS_PIPE_CLOSING:
                        Fd->__Reserved1.Status = Status;
                        if(Flag & (LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP))
                            ResFlag |= (Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP));
                        else
                            NtClearEvent(EventHandle);
                        break;
                    case STATUS_INVALID_PARAMETER:
                        if(ppl == NULL) {
                            ppl = &pl;
                            NtQueryInformationFile((HANDLE)Fd->Fd, &ioStatusBlock, &pl.QuadPart, sizeof(pl.QuadPart), _FilePositionInformation);
                            goto lblAgain;
                        }
                    default:
                        Fd->__Reserved1.Status = Status;
                        ResFlag |= LQEVNT_FLAG_ERR;
                }
            }
        } else {
            NtClearEvent(EventHandle);
        }
    }
    Fd->_ResAsyncPollEvents = ResFlag;
}

static void PipeUpdateAsyncSelect(LqSysPoll* Events, const intptr_t Index, const LqEvntFlag NewFlags) {
    LqEvntFd* Fd = PtrKernelEventAt(Events, Index);
    Fd->_AsyncPollEvents = NewFlags;
    if(Fd->__Reserved1.Status == STATUS_PENDING) {
        NtCancelIoFile(((HANDLE)Fd->Fd), (PIO_STATUS_BLOCK)&Fd->__Reserved1);
        Fd->__Reserved1.Status = STATUS_NOT_SUPPORTED;
    }
    PipeProcessAsyncSelect(Events, Index);
}

static void TerminalProcessAsyncSelect(LqSysPoll* Events, const intptr_t Index) {
    DWORD num_read;
    INPUT_RECORD ir;
    LqEvntFd* Fd = PtrKernelEventAt(Events, Index);
    HANDLE EventHandle = HandleKernelEventAt(Events, Index);
    const LqEvntFlag Flag = Fd->_AsyncPollEvents;
    LqEvntFlag ResFlag = (LqEvntFlag)0;

    if(Flag & LQEVNT_FLAG_RD) {
        while(true) {
            if(!PeekConsoleInputW((HANDLE)Fd->Fd, &ir, 1, &num_read)) {
                ResFlag |= LQEVNT_FLAG_ERR;
                break;
            }
            if(num_read <= 0)
                break;
            if(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                ResFlag |= LQEVNT_FLAG_RD;
                break;
            } else {
                ReadConsoleInputW((HANDLE)Fd->Fd, &ir, 1, &num_read);
            }
        }
    }
    ResFlag |= (Flag & LQEVNT_FLAG_WR);
    if(ResFlag != 0) {
        NtSetEvent(EventHandle, NULL);
    } else {
        NtClearEvent(EventHandle);
        Events->IsHaveOnlyHup = (uintptr_t)1;
    }
    Fd->_ResAsyncPollEvents = ResFlag;
}

static void TerminalUpdateAsyncSelect(LqSysPoll* Events, const intptr_t Index, const LqEvntFlag NewFlags) {
    LqEvntFd* Fd = PtrKernelEventAt(Events, Index);
    Fd->_AsyncPollEvents = NewFlags;
    TerminalProcessAsyncSelect(Events, Index);
}


/*
* Adding to follow all type of WinNT objects
*/
bool LqSysPollAddHdr(LqSysPoll* Events, LqClientHdr* Client) {
    LqEvntFd* EvntData;
    LqConn* Conn;
    intptr_t InsertIndex;
    HANDLE EventHandle;
    DWORD Mode;
    IO_STATUS_BLOCK ioStatusBlock;
    LqEvntFlag NewFlags = _LqEvntGetFlagForUpdate(Client);

    if(NewFlags & _LQEVNT_FLAG_CONN) {
        Conn = (LqConn*)Client;
        Conn->_AsyncSelectSerialNumber = 0;
        Conn->_EmptyPollInfo = NULL;
        InsertIndex = AddIocpElement(Events, Conn);
        SockUpdateAsyncSelect(Events, InsertIndex, NewFlags);
    } else {
        EvntData = (LqEvntFd*)Client;

        if(NtCancelIoFile((HANDLE)EvntData->Fd, &ioStatusBlock) == STATUS_OBJECT_TYPE_MISMATCH) { /*Test if descriptor not readable/writable*/
            if(NewFlags & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RD))
                EventHandle = (HANDLE)EvntData->Fd;
            else
                NtCreateEvent(&EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
            AddKernelEvent(Events, EvntData, EventHandle);
            EvntData->__Reserved1.Status = STATUS_OBJECT_TYPE_MISMATCH;
            EvntData->_AsyncPollEvents = NewFlags;
            EvntData->_Type = LQ_AFD_POLL_TYPE_EVENT;
        } else if(GetConsoleMode((HANDLE)EvntData->Fd, &Mode)) {
            NtCreateEvent(&EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
            AddKernelEvent(Events, EvntData, EventHandle);
            EvntData->__Reserved1.Status = STATUS_OBJECT_TYPE_MISMATCH;
            EvntData->_Type = LQ_AFD_POLL_TYPE_TERMINAL;
            TerminalUpdateAsyncSelect(Events, Events->EvntArr.Count - 1, NewFlags);
        } else {
            NtCreateEvent(&EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
            AddKernelEvent(Events, EvntData, EventHandle);
            EvntData->__Reserved1.Status = STATUS_NOT_SUPPORTED;
            EvntData->_Type = LQ_AFD_POLL_TYPE_PIPE;
            PipeUpdateAsyncSelect(Events, Events->EvntArr.Count - 1, NewFlags);
        }
    }
    Events->CommonCount++;
    return true;
}

LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events) {
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events) {
    LqEvntFlag Flag;
    LqEvntFd* EvntData;
    LqConn* Conn;
    intptr_t i, m;
    LqEvntFlag r;
    IO_STATUS_BLOCK ioStatusBlock;
    __FILE_PIPE_LOCAL_INFORMATION PipeInfo;
    NTSTATUS Stat;
    HANDLE Handle;
    PPOLL_CONTEXT_BLOCK PollBlock;
    PVOID KeyContext;
    unsigned long SysEvents;
    DWORD nbuffer, num_read;
    INPUT_RECORD ir;

    if(Events->EventObjectIndex < Events->EvntArr.Count) {
        for(i = Events->EventObjectIndex + ((intptr_t)1), m = Events->EvntArr.Count; i < m; i++) {
            if((EvntData = PtrKernelEventAt(Events, i)) == NULL)
                continue;
            switch(EvntData->_Type) {
                case LQ_AFD_POLL_TYPE_EVENT:
                    switch(NtWaitForSingleObject(HandleKernelEventAt(Events, i), FALSE, &ZeroTimeWait)) {
                        case STATUS_SUCCESS:
                            Events->EventObjectIndex = i;
                            return (EvntData->_AsyncPollEvents & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR));
                        case STATUS_TIMEOUT:
                        case STATUS_ALERTED:
                            break;
                        default:
                            Events->EventObjectIndex = i;
                            return LQEVNT_FLAG_ERR;
                    }
                    break;
                case LQ_AFD_POLL_TYPE_PIPE:
                    if(EvntData->_ResAsyncPollEvents != ((LqEvntFlag)0)) {
                        Events->EventObjectIndex = i;
                        return EvntData->_ResAsyncPollEvents;
                    }
                    Flag = EvntData->_AsyncPollEvents;
                    r = ((LqEvntFlag)0);
                    switch(EvntData->__Reserved1.Status) {
                        case STATUS_NOT_SUPPORTED:
                            if(Flag & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP)) {
                                Stat = NtQueryInformationFile((HANDLE)EvntData->Fd, &ioStatusBlock, &PipeInfo, sizeof(PipeInfo), _FilePipeLocalInformation);
                                if(Stat != STATUS_SUCCESS) {
                                    r |= LQEVNT_FLAG_ERR;
                                    break;
                                }
                                if((Flag & LQEVNT_FLAG_WR) && (PipeInfo.WriteQuotaAvailable > 0))
                                    r |= LQEVNT_FLAG_WR;
                                if((Flag & LQEVNT_FLAG_RD) && (PipeInfo.ReadDataAvailable > 0))
                                    r |= LQEVNT_FLAG_RD;
                                if((Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) && (PipeInfo.NamedPipeState != 3))
                                    r |= (Flag & (LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP));
                                if(r == 0)
                                    Events->IsHaveOnlyHup = (uintptr_t)1;
                            }
                            break;
                        case STATUS_PIPE_BROKEN:
                        case STATUS_PIPE_CLOSING:
                            r |= (Flag & (LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP));
                            break;
                        case STATUS_SUCCESS:
                            r |= LQEVNT_FLAG_RD;
                            break;
                        case STATUS_PENDING:
                        case STATUS_OBJECT_TYPE_MISMATCH:
                            break;
                        default:
                            r |= LQEVNT_FLAG_ERR;
                            break;
                    }
                    if(r == ((LqEvntFlag)0))
                        continue;
                    Events->EventObjectIndex = i;
                    return r;
                case LQ_AFD_POLL_TYPE_TERMINAL:
                    if(EvntData->_ResAsyncPollEvents != ((LqEvntFlag)0)) {
                        Events->EventObjectIndex = i;
                        return EvntData->_ResAsyncPollEvents;
                    }
                    Flag = EvntData->_AsyncPollEvents;
                    r = ((LqEvntFlag)0);
                    if(Flag & LQEVNT_FLAG_RD) {
                        while(true) {
                            if(!PeekConsoleInputW((HANDLE)EvntData->Fd, &ir, 1, &num_read)) {
                                r |= LQEVNT_FLAG_ERR;
                                break;
                            }
                            if(num_read <= 0)
                                break;
                            if(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                                r |= LQEVNT_FLAG_RD;
                                break;
                            } else {
                                ReadConsoleInputW((HANDLE)EvntData->Fd, &ir, 1, &num_read);
                            }
                        }
                    }
                    r |= (Flag & LQEVNT_FLAG_WR);
                    if(r == ((LqEvntFlag)0)) {
                        if(Flag & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RD))
                            Events->IsHaveOnlyHup = (uintptr_t)1;
                        continue;
                    }
                    Events->EventObjectIndex = i;
                    return r;
            }
        }
        Events->EventObjectIndex = INTPTR_MAX;
    }

    if(Events->EnumCalledCount < ((intptr_t)70)) {/* To prevent an infinite loop */
        Events->EnumCalledCount++;
        if(Events->PollBlock != NULL) {
            PollBlock = (PPOLL_CONTEXT_BLOCK)Events->PollBlock;
            Events->PollBlock = NULL;
            goto lblProcessBlock;
        }
        while(true) {
            Stat = NtRemoveIoCompletion(
                Events->IocpHandle,
                &KeyContext,
                (PVOID*)&PollBlock,
                &ioStatusBlock,
                &ZeroTimeWait
            );
            if(Stat == STATUS_SUCCESS) {
                /* Validate completion message */
lblProcessBlock:
                if(
                    (PollBlock->Index >= Events->IocpArr.AllocCount) ||
                    ((Conn = IocpAt(Events, PollBlock->Index)) == NULL) ||
                    (Conn != PollBlock->SocketInfo) ||
                    (Conn->_AsyncSelectSerialNumber != PollBlock->AsyncSelectSerialNumber)
                    ) {
                    LqFastAlloc::Delete(PollBlock);
                    continue;
                }

                Flag = LqClientGetFlags(Conn);
                SysEvents = PollBlock->PollInfo.Handles[0].Events;
                r = (LqEvntFlag)0;
                if(SysEvents & AFD_POLL_RECEIVE) r |= LQEVNT_FLAG_RD;
                if(SysEvents & AFD_POLL_SEND) r |= LQEVNT_FLAG_WR;
                if(SysEvents & AFD_POLL_ACCEPT) r |= LQEVNT_FLAG_ACCEPT;
                if(SysEvents & (AFD_POLL_CONNECT | AFD_POLL_CONNECT_FAIL)) r |= LQEVNT_FLAG_CONNECT;
                if(SysEvents & (AFD_POLL_DISCONNECT | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE))r |= (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
                if(PollBlock->PollInfo.Handles[0].Status != STATUS_SUCCESS) r |= LQEVNT_FLAG_ERR;
                r &= ((Flag & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) | LQEVNT_FLAG_ERR);
                if(Conn->_EmptyPollInfo == NULL)
                    Conn->_EmptyPollInfo = PollBlock;
                else
                    LqFastAlloc::Delete(PollBlock);
                if(r == 0)
                    continue;
                Events->ConnIndex = PollBlock->Index;
                return r;
            } else {
                SetLastError(RtlNtStatusToDosError(Stat));
                break;
            }
        }
    }
    Events->ConnIndex = INTPTR_MAX;
    return 0;
}

void LqSysPollRemoveCurrent(LqSysPoll* Events) {
    LqEvntFd* EvntData;
    HANDLE EventHandle;
    if(Events->EventObjectIndex < Events->EvntArr.Count) {
        EvntData = PtrKernelEventAt(Events, Events->EventObjectIndex);
        EventHandle = HandleKernelEventAt(Events, Events->EventObjectIndex);
        RemoveKernelEvent(Events, Events->EventObjectIndex);
        if(EvntData->__Reserved1.Status == STATUS_PENDING)
            NtCancelIoFile((HANDLE)EvntData->Fd, (PIO_STATUS_BLOCK)&EvntData->__Reserved1);
        if(((HANDLE)EvntData->Fd) != EventHandle)
            NtClose(EventHandle);
    } else {
        SockUpdateAsyncSelect(Events, Events->ConnIndex, 0);
        RemoveIocpElement(Events, Events->ConnIndex);
    }
    Events->CommonCount--;
}

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events) {
    LqArr2AlignAfterRemove(&Events->IocpArr, LqConn*, NULL);
    LqArr3AlignAfterRemove(&Events->EvntArr, HANDLE, LqEvntFd*, NULL);
}

LqClientHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    if(Events->EventObjectIndex < Events->EvntArr.Count)
        return (LqClientHdr*)PtrKernelEventAt(Events, Events->EventObjectIndex);
    return (LqClientHdr*)IocpAt(Events, Events->ConnIndex);
}

void LqSysPollUnuseCurrent(LqSysPoll* Events) {
    LqEvntFd* Fd;
    if(Events->EventObjectIndex < Events->EvntArr.Count) {
        Fd = PtrKernelEventAt(Events, Events->EventObjectIndex);
        switch(Fd->_Type) {
            case LQ_AFD_POLL_TYPE_PIPE:
                PipeProcessAsyncSelect(Events, Events->EventObjectIndex);
                break;
            case LQ_AFD_POLL_TYPE_TERMINAL:
                TerminalProcessAsyncSelect(Events, Events->EventObjectIndex);
                break;
        }
    } else {
        SockProcessAsyncSelect(Events, Events->ConnIndex);
    }
}

inline static bool LqEvntSetMaskIocp(LqSysPoll* Events, const intptr_t Index, const LqEvntFlag NewFlags) {
    SockUpdateAsyncSelect(Events, Index, NewFlags);
    return true;
}

static bool LqEvntSetMaskFd(LqSysPoll* Events, const intptr_t Index, const LqEvntFlag NewFlags) {
    LqEvntFd* _EvntData = PtrKernelEventAt(Events, Index);
    switch(_EvntData->_Type) {
        case LQ_AFD_POLL_TYPE_PIPE:
            PipeUpdateAsyncSelect(Events, Index, NewFlags);
            break;
        case LQ_AFD_POLL_TYPE_TERMINAL:
            TerminalUpdateAsyncSelect(Events, Index, NewFlags);
            break;
        case LQ_AFD_POLL_TYPE_EVENT:
        {
            HANDLE EventHandle = HandleKernelEventAt(Events, Index);
            if(NewFlags & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RD)) {
                if(((HANDLE)_EvntData->Fd) != EventHandle) {
                    HandleKernelEventAt(Events, Index) = (HANDLE)_EvntData->Fd;
                    NtClose(EventHandle);
                }
            } else {
                if(((HANDLE)_EvntData->Fd) == EventHandle) {
                    NtCreateEvent(&EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
                    HandleKernelEventAt(Events, Index) = EventHandle;
                }
            }
            _EvntData->_AsyncPollEvents = NewFlags;
        }
        break;
    }
    return true;
}

bool LqSysPollSetMaskByCurrent(LqSysPoll* Events) {
    LqEvntFlag NewFlags;
    if(Events->EventObjectIndex < Events->EvntArr.Count) {
        NewFlags = _LqEvntGetFlagForUpdate(PtrKernelEventAt(Events, Events->EventObjectIndex));
        return LqEvntSetMaskFd(Events, Events->EventObjectIndex, NewFlags);
    }
    NewFlags = _LqEvntGetFlagForUpdate(IocpAt(Events, Events->ConnIndex));
    return LqEvntSetMaskIocp(Events, Events->ConnIndex, NewFlags);
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*)) {
    LqEvntInterator Iter;
    uintptr_t Index;
    LqEvntFlag NewFlags;
    for(register auto i = &IocpAt(Events, 0), m = i + Events->IocpArr.AllocCount; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            Index = (((uintptr_t)i) - ((uintptr_t)&IocpAt(Events, 0))) / sizeof(LqConn*);
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                Iter.IsEnumConn = true;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                /* Update fast indexes */
                i = &IocpAt(Events, Index);
                m = &IocpAt(Events, Events->IocpArr.AllocCount);
            } else {
                LqEvntSetMaskIocp(Events, Index, NewFlags);
            }
        }
    for(register auto i = &PtrKernelEventAt(Events, 0), m = i + Events->EvntArr.Count; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            Index = (((uintptr_t)i) - ((uintptr_t)&PtrKernelEventAt(Events, 0))) / sizeof(LqEvntFd*);
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                Iter.IsEnumConn = false;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                /* Update fast indexes */
                i = &PtrKernelEventAt(Events, Index);
                m = &PtrKernelEventAt(Events, Events->EvntArr.Count);
            } else {
                LqEvntSetMaskFd(Events, Index, NewFlags);
            }
        }
    return 1;
}

bool __LqSysPollEnumBegin(LqSysPoll* Events, LqEvntInterator* Interator) {
    Interator->Index = -((intptr_t)1);
    Interator->IsEnumConn = true;
    return __LqSysPollEnumNext(Events, Interator);
}

bool __LqSysPollEnumNext(LqSysPoll* Events, LqEvntInterator* Interator) {
    register intptr_t Index = Interator->Index + ((intptr_t)1);
    register intptr_t m;
    if(Interator->IsEnumConn) {
        for(m = Events->IocpArr.AllocCount; Index < m; Index++)
            if(IocpAt(Events, Index) != NULL) {
                Interator->Index = Index;
                return true;
            }
        Index = ((intptr_t)0);
        Interator->IsEnumConn = false;
    }
    for(m = Events->EvntArr.Count; Index < m; Index++)
        if(PtrKernelEventAt(Events, Index) != NULL) {
            Interator->Index = Index;
            return true;
        }
    return false;
}

LqClientHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    LqClientHdr* Ret;
    LqEvntFd* Fd;
    HANDLE EventObject;
    if(Interator->IsEnumConn) {
        SockUpdateAsyncSelect(Events, Interator->Index, 0);
        Ret = (LqClientHdr*)IocpAt(Events, Interator->Index);
        RemoveIocpElement(Events, Interator->Index);
        _LqEvntGetFlagForUpdate(Ret);
    } else {
        Fd = PtrKernelEventAt(Events, Interator->Index);
        EventObject = HandleKernelEventAt(Events, Interator->Index);
        RemoveKernelEvent(Events, Interator->Index);
        if(Fd->__Reserved1.Status == STATUS_PENDING)
            NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
        if(((HANDLE)Fd->Fd) != EventObject)
            NtClose(EventObject);
        _LqEvntGetFlagForUpdate(Fd);
        Ret = (LqClientHdr*)Fd;
    }
    Events->CommonCount--;
    return Ret;
}

LqClientHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    if(Interator->IsEnumConn)
        return (LqClientHdr*)IocpAt(Events, Interator->Index);
    return (LqClientHdr*)PtrKernelEventAt(Events, Interator->Index);
}

static PLARGE_INTEGER BaseFormatTimeOut(PLARGE_INTEGER Timeout, LqTimeMillisec Milliseconds) {
    if(Milliseconds >= ((LqTimeMillisec)INFINITE)) return NULL;
    if(Milliseconds < ((LqTimeMillisec)5)) return &ZeroTimeWait;
    Timeout->QuadPart = Milliseconds * -10000LL;
    return Timeout;
}

static int CheckNEventsAndIocp(LqSysPoll* Events, const HANDLE* EventObjs, const intptr_t EventsCount) {
    NTSTATUS Status;
    IO_COMPLETION_BASIC_INFORMATION QueueInfo;
    ULONG ResultLen;
    Status = NtWaitForMultipleObjects(EventsCount, (HANDLE*)EventObjs, WaitAny, FALSE, &ZeroTimeWait);
    if((Status >= STATUS_WAIT_0) && (Status < (STATUS_WAIT_0 + MAXIMUM_WAIT_OBJECTS))) {
        return Status - STATUS_WAIT_0;
    } else if(Status == STATUS_TIMEOUT) {
        QueueInfo.Depth = 0;
        NtQueryIoCompletion(Events->IocpHandle, IoCompletionBasicInformation, &QueueInfo, sizeof(QueueInfo), &ResultLen);
        if(QueueInfo.Depth > 0)
            return -2;
        return -3;
    } else {
        SetLastError(RtlNtStatusToDosError(Status));
        return -1;
    }
}

static int CheckAllEventsAndIocp(LqSysPoll* Events, const HANDLE* EventObjs, const intptr_t EventsCount) {
    intptr_t StartIndex = ((intptr_t)0);
    intptr_t Count;
    int Status;
    while(true) {
        Count = EventsCount - StartIndex;
        if(Count >= ((intptr_t)MAXIMUM_WAIT_OBJECTS))
            Count = ((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1);
        Status = CheckNEventsAndIocp(Events, EventObjs + StartIndex, Count);
        if(Status >= 0)
            return StartIndex + Status;
        if((Status == -2) || (Status == -1))
            return Status;
        StartIndex += Count;
        if(StartIndex >= ((intptr_t)EventsCount))
            return -3;
    }
}

static int CheckAllEvents(const HANDLE* EventObjs, const intptr_t EventsCount) {
    intptr_t StartIndex = ((intptr_t)0);
    intptr_t Count;
    NTSTATUS Status;
    while(true) {
        Count = EventsCount - StartIndex;
        if(Count >= ((intptr_t)MAXIMUM_WAIT_OBJECTS))
            Count = ((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1);
        Status = NtWaitForMultipleObjects(Count, (HANDLE*)(EventObjs + StartIndex), WaitAny, FALSE, &ZeroTimeWait);
        if((Status >= STATUS_WAIT_0) && (Status < (STATUS_WAIT_0 + MAXIMUM_WAIT_OBJECTS))) {
            return StartIndex + Status - STATUS_WAIT_0;
        } else if(Status != STATUS_TIMEOUT) {
            SetLastError(RtlNtStatusToDosError(Status));
            return -1;
        }
        StartIndex += Count;
        if(StartIndex >= ((intptr_t)EventsCount))
            return -3;
    }
}

static int WaitEvents(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    LARGE_INTEGER liWaitTime, *pliWaitTime;
    NTSTATUS Status;
    IO_COMPLETION_BASIC_INFORMATION QueueInfo;
    ULONG ResultLen;
    PVOID KeyContext, ApcContext;
    IO_STATUS_BLOCK StatusBlock;
    int Result;
    const HANDLE* EventObjs = &HandleKernelEventAt(Events, 0);
    size_t EventsCount = Events->EvntArr.Count;

    if(Events->CommonCount <= EventsCount) { /* Is have only event objects */
        if(EventsCount >= MAXIMUM_WAIT_OBJECTS) {
            while(true) {
                Result = CheckAllEvents(EventObjs, EventsCount);
                if(Result > -3)
                    return Result;
                WaitTime -= ((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS);
                pliWaitTime = BaseFormatTimeOut(&liWaitTime, lq_min(((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS), WaitTime));
                Status = NtWaitForMultipleObjects(MAXIMUM_WAIT_OBJECTS - 1, (HANDLE*)EventObjs, WaitAny, FALSE, pliWaitTime);
                if((Status >= STATUS_WAIT_0) && (Status < (STATUS_WAIT_0 + MAXIMUM_WAIT_OBJECTS)))
                    return Status - STATUS_WAIT_0;
                if(Status != STATUS_TIMEOUT) {
                    SetLastError(RtlNtStatusToDosError(Status));
                    return -1;
                }
                if(WaitTime <= ((LqTimeMillisec)0))
                    return -3;
            }
        } else {
            pliWaitTime = BaseFormatTimeOut(&liWaitTime, WaitTime);
            Status = NtWaitForMultipleObjects(EventsCount, (HANDLE*)EventObjs, WaitAny, FALSE, pliWaitTime);
            if((Status >= STATUS_WAIT_0) && (Status < (STATUS_WAIT_0 + MAXIMUM_WAIT_OBJECTS))) {
                return Status - STATUS_WAIT_0;
            } else if(Status == STATUS_TIMEOUT) {
                return -3;
            } else {
                SetLastError(RtlNtStatusToDosError(Status));
                return -1;
            }
        }
    } else if(EventsCount == 0) {
        QueueInfo.Depth = 0;
        /* Fast check queue */
        NtQueryIoCompletion(Events->IocpHandle, IoCompletionBasicInformation, &QueueInfo, sizeof(QueueInfo), &ResultLen);
        if(QueueInfo.Depth > 0)
            return -2;
        if(WaitTime < ((LqTimeMillisec)5)) /* If time less then minimum thread quant, then just check length queue*/
            return -3;
        pliWaitTime = BaseFormatTimeOut(&liWaitTime, WaitTime);
        switch(Status = NtRemoveIoCompletion(Events->IocpHandle, &KeyContext, &ApcContext, &StatusBlock, pliWaitTime)) {
            case STATUS_SUCCESS:
                Events->PollBlock = ApcContext;
                return -2;
            case STATUS_TIMEOUT:
                return -3;
            default:
                SetLastError(RtlNtStatusToDosError(Status));
                return -1;
        }
    } else {
        if(EventsCount >= MAXIMUM_WAIT_OBJECTS) {
            while(true) {
                Result = CheckAllEventsAndIocp(Events, EventObjs, EventsCount);
                if(Result > -3)
                    return Result;
                WaitTime -= ((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS);
                pliWaitTime = BaseFormatTimeOut(&liWaitTime, lq_min(((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS), WaitTime));
                switch(Status = NtRemoveIoCompletion(Events->IocpHandle, &KeyContext, &ApcContext, &StatusBlock, pliWaitTime)) {
                    case STATUS_SUCCESS:
                        Events->PollBlock = ApcContext;
                        return -2;
                    case STATUS_TIMEOUT:
                        if(WaitTime > ((LqTimeMillisec)0))
                            continue;
                        return -3;
                    default:
                        SetLastError(RtlNtStatusToDosError(Status));
                        return -1;
                }
            }
        } else {
            while(true) {
                Status = NtWaitForMultipleObjects(EventsCount, (HANDLE*)EventObjs, WaitAny, FALSE, &ZeroTimeWait);
                if((Status >= STATUS_WAIT_0) && (Status < (STATUS_WAIT_0 + MAXIMUM_WAIT_OBJECTS))) {
                    return Status - STATUS_WAIT_0;
                } else if(Status != STATUS_TIMEOUT) {
                    SetLastError(RtlNtStatusToDosError(Status));
                    return -1;
                }
                WaitTime -= ((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_HAVE_IOCP_AND_EVNTOBJ);
                pliWaitTime = BaseFormatTimeOut(&liWaitTime, lq_min(((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_HAVE_IOCP_AND_EVNTOBJ), WaitTime));
                switch(Status = NtRemoveIoCompletion(Events->IocpHandle, &KeyContext, &ApcContext, &StatusBlock, pliWaitTime)) {
                    case STATUS_SUCCESS:
                        Events->PollBlock = ApcContext;
                        return -2;
                    case STATUS_TIMEOUT:
                        if(WaitTime > ((LqTimeMillisec)0))
                            continue;
                        return -3;
                    default:
                        SetLastError(RtlNtStatusToDosError(Status));
                        return -1;
                }
            }
        }
    }
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    int Stat;
    Events->EnumCalledCount = 0;
    Events->ConnIndex = INTPTR_MAX;
    Events->EventObjectIndex = INTPTR_MAX;
    if(Events->PollBlock != NULL)
        return -2;
    if((Events->IsHaveOnlyHup == ((uintptr_t)1)) && (WaitTime > ((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_HAVE_ONLY_HUP_OBJ)))
        WaitTime = ((LqTimeMillisec)LQ_WINEVNT_WAIT_WHEN_HAVE_ONLY_HUP_OBJ);
    Stat = WaitEvents(Events, WaitTime);
    if(Events->IsHaveOnlyHup == ((uintptr_t)1)) {
        Events->IsHaveOnlyHup = ((uintptr_t)0);
        Events->EventObjectIndex = -((intptr_t)1);
        return 1;
    }
    if(Stat == -3)
        return 0;
    if(Stat == -2)
        return 1;
    if(Stat >= 0) {
        Events->EventObjectIndex = ((intptr_t)Stat - (intptr_t)1);
        return 1;
    }
    return -1;
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

