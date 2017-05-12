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

#ifndef LQ_EVNT
#error "Only use in LqEvnt.cpp !"
#endif


#include <Windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include "LqFile.h"
#include "LqTime.hpp"

#include "LqAlloc.hpp"

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

extern "C" __kernel_entry NTSTATUS NTAPI NtQueryInformationFile(
    __in HANDLE FileHandle,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __out_bcount(Length) PVOID FileInformation,
    __in ULONG Length,
    __in __FILE_INFORMATION_CLASS FileInformationClass
);

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

#define LqEvntSystemEventByConnFlag(EvntFlags)            \
    ((((EvntFlags) & LQEVNT_FLAG_RD)        ? FD_READ : 0)  |\
    (((EvntFlags) & LQEVNT_FLAG_WR)         ? FD_WRITE : 0) |\
    (((EvntFlags) & LQEVNT_FLAG_ACCEPT)     ? FD_ACCEPT : 0)  |\
    (((EvntFlags) & LQEVNT_FLAG_CONNECT)    ? FD_CONNECT : 0) |\
    (((EvntFlags) & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) ? FD_CLOSE: 0))

#define IsRdAgain(Client)  ((LqClientGetFlags(Client) & (LQCONN_FLAG_RD_AGAIN | LQEVNT_FLAG_RD)) == (LQCONN_FLAG_RD_AGAIN | LQEVNT_FLAG_RD))
#define IsWrAgain(Client)  ((LqClientGetFlags(Client) & (LQCONN_FLAG_WR_AGAIN | LQEVNT_FLAG_WR)) == (LQCONN_FLAG_WR_AGAIN | LQEVNT_FLAG_WR))
#define IsAgain(Client)    (IsRdAgain(Client) || IsWrAgain(Client))

static LARGE_INTEGER ZeroTimeWait = {0};
static char AsyncBuf;

static LRESULT CALLBACK WindowProcedure(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(window, msg, wp, lp);
}

static struct _wnd_class {
    WNDCLASSEX* wc;
    _wnd_class() {
        static WNDCLASSEX wndclass =
        {
            sizeof(WNDCLASSEX), CS_DBLCLKS, WindowProcedure,
            0, 0, 0, 0,
            0, 0,
            0, TEXT("SockClass"), 0
        };
        RegisterClassEx(&wndclass);
        wc = &wndclass;
    }
    ~_wnd_class() {
        UnregisterClass(wc->lpszClassName, 0);
    }
} wnd_class;

bool LqSysPollInit(LqSysPoll* Dest) {
    LqArr2Init(&Dest->ConnArr);
    LqArr3Init(&Dest->EvntArr);

    Dest->EventObjectIndex = LQ_NUMERIC_MAX(intptr_t);
    Dest->ConnIndex = -((intptr_t)1);
    Dest->CommonCount = 0;
    Dest->WinHandle = (uintptr_t)-((intptr_t)1);
    Dest->IsHaveOnlyHup = (uintptr_t)0;
    return true;
}

bool LqSysPollThreadInit(LqSysPoll* Dest) {
    char t;
    HWND hSockWnd;
    u_long InternalBufLen;
    intptr_t i, m;
    LqConn** Conns;
    LqEvntFlag NewFlags;
    hSockWnd = CreateWindowEx(0, TEXT("SockClass"), TEXT(""), WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, 0, 0);
    if(hSockWnd == NULL)
        return false;
    Dest->WinHandle = (uintptr_t)hSockWnd;
    Conns = &LqArr2At(&Dest->ConnArr, LqConn*, 0);
    for(i = (intptr_t)0, m = Dest->ConnArr.AllocCount; i < m; i++) {
        if(Conns[i] != NULL) {
            NewFlags = LqClientGetFlags(Conns[i]);
            WSAAsyncSelect(Conns[i]->Fd, hSockWnd, WM_USER + i, LqEvntSystemEventByConnFlag(NewFlags));
            if(NewFlags & LQEVNT_FLAG_RD) {
                InternalBufLen = 0UL;
                ioctlsocket(Conns[i]->Fd, FIONREAD, &InternalBufLen);
                if(InternalBufLen > 0UL)
                    PostMessage((HWND)Dest->WinHandle, WM_USER + i, Conns[i]->Fd, FD_READ);
            }
            if(NewFlags & LQEVNT_FLAG_WR) {
                if(send(Conns[i]->Fd, &t, 0, 0) >= 0)
                    PostMessage((HWND)Dest->WinHandle, WM_USER + i, Conns[i]->Fd, FD_WRITE);
            }
        }
    }
    return true;
}

void LqSysPollThreadUninit(LqSysPoll* Dest) {
    DestroyWindow((HWND)Dest->WinHandle);
    Dest->WinHandle = (uintptr_t)-1;
}

void LqSysPollUninit(LqSysPoll* Dest) {
    intptr_t i, m;
    if(Dest->EvntArr.Count > ((intptr_t)0)) {
        for(i = 0, m = Dest->EvntArr.Count; i < m; i++) {
            if(LqArr3At_1(&Dest->EvntArr, HANDLE, i) != (HANDLE)LqArr3At_2(&Dest->EvntArr, LqEvntFd*, i)->Fd)
                LqFileClose((int)LqArr3At_1(&Dest->EvntArr, HANDLE, i)); //close only events
        }
    }
    LqArr2Uninit(&Dest->ConnArr);
    LqArr3Uninit(&Dest->EvntArr);
}

inline static intptr_t AddIocpElement(LqSysPoll* Events, LqConn* NewVal) {
    intptr_t InsertIndex;
    LqArr2PushBack(&(Events)->ConnArr, LqConn*, InsertIndex, NULL);
    LqArr2At(&(Events)->ConnArr, LqConn*, InsertIndex) = NewVal;
    return InsertIndex;
}

inline static void AddKernelEvent(LqSysPoll* Events, LqEvntFd* NewPtr, HANDLE NewHandleVal) {
    LqArr3PushBack(&(Events)->EvntArr, HANDLE, LqEvntFd*);
    LqArr3Back_1(&(Events)->EvntArr, HANDLE) = NewHandleVal;
    LqArr3Back_2(&(Events)->EvntArr, LqEvntFd*) = NewPtr;
}

#define RemoveKernelEvent(Events, Index) LqArr3RemoveAt(&(Events)->EvntArr, HANDLE, LqEvntFd*, (Index), NULL);

#define HandleKernelEventAt(Events, Index) LqArr3At_1(&(Events)->EvntArr, HANDLE, (Index))
#define PtrKernelEventAt(Events, Index) LqArr3At_2(&(Events)->EvntArr, LqEvntFd*, (Index))
#define ConnAt(Events, Index) LqArr2At(&(Events)->ConnArr, LqConn*, (Index))

#define LQ_AFD_POLL_TYPE_PIPE       ((uint8_t)0)
#define LQ_AFD_POLL_TYPE_EVENT      ((uint8_t)1)
#define LQ_AFD_POLL_TYPE_TERMINAL   ((uint8_t)2)

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
    char tt;
    u_long InternalBufLen;
    IO_STATUS_BLOCK ioStatusBlock;
    LqEvntFlag NewFlags = _LqEvntGetFlagForUpdate(Client);

    if(NewFlags & _LQEVNT_FLAG_CONN) {
        LqArr2PushBack(&Events->ConnArr, LqConn*, InsertIndex, NULL);
        LqArr2At(&Events->ConnArr, LqConn*, InsertIndex) = (LqConn*)Client;
        WSAAsyncSelect(Client->Fd, (HWND)Events->WinHandle, WM_USER + InsertIndex, LqEvntSystemEventByConnFlag(NewFlags));
        if(NewFlags & LQEVNT_FLAG_RD) {
            InternalBufLen = 0UL;
            ioctlsocket(Client->Fd, FIONREAD, &InternalBufLen);
            if(InternalBufLen > 0UL)
                PostMessage((HWND)Events->WinHandle, WM_USER + InsertIndex, Client->Fd, FD_READ);
        }
        if(NewFlags & LQEVNT_FLAG_WR) {
            if(send(Client->Fd, &tt, 0, 0) >= 0)
                PostMessage((HWND)Events->WinHandle, WM_USER + InsertIndex, Client->Fd, FD_WRITE);
        }
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
    PVOID KeyContext;
    unsigned long SysEvents;
    DWORD nbuffer, num_read;
    INPUT_RECORD ir;
    MSG Msg;
    UINT ConnIndex;
    WORD EventNum;
    WORD Error;
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
        Events->EventObjectIndex = LQ_NUMERIC_MAX(intptr_t);
    }
    while(PeekMessage(&Msg, (HWND)Events->WinHandle, WM_USER, WM_USER + 0xffff, PM_REMOVE) == TRUE) {
        ConnIndex = Msg.message - WM_USER;
        if((ConnIndex >= Events->ConnArr.AllocCount) ||
            ((Conn = LqArr2At(&Events->ConnArr, LqConn*, ConnIndex)) == NULL) ||
           (Conn->Fd != Msg.wParam))
            continue;

        EventNum = WSAGETSELECTEVENT(Msg.lParam);
        Error = WSAGETSELECTERROR(Msg.lParam);
        r = (LqEvntFlag)0;
        switch(EventNum) {
            case FD_READ: r = LQEVNT_FLAG_RD; break;
            case FD_WRITE: r = LQEVNT_FLAG_WR; break;
            case FD_CONNECT: r = LQEVNT_FLAG_CONNECT; break;
            case FD_ACCEPT: r = LQEVNT_FLAG_ACCEPT; break;
            case FD_CLOSE: r = (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP); break;
        }
        if(Error != 0)
            r |= LQEVNT_FLAG_ERR;
        if(r != (LqEvntFlag)0) {
            Events->ConnIndex = ConnIndex;
            return r;
        }
    }
    Events->ConnIndex = LQ_NUMERIC_MAX(intptr_t);
    return 0;
}
void LqSysPollRemoveCurrent(LqSysPoll* Events) {
    LqEvntFd* EvntData;
    HANDLE EventHandle;
    LqConn* Conn;
    if(Events->EventObjectIndex < Events->EvntArr.Count) {
        EvntData = PtrKernelEventAt(Events, Events->EventObjectIndex);
        EventHandle = HandleKernelEventAt(Events, Events->EventObjectIndex);
        RemoveKernelEvent(Events, Events->EventObjectIndex);
        if(EvntData->__Reserved1.Status == STATUS_PENDING)
            NtCancelIoFile((HANDLE)EvntData->Fd, (PIO_STATUS_BLOCK)&EvntData->__Reserved1);
        if(((HANDLE)EvntData->Fd) != EventHandle)
            NtClose(EventHandle);
    } else {
        Conn = LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
        LqArr2RemoveAt(&Events->ConnArr, LqConn*, Events->ConnIndex, NULL);
        _LqEvntGetFlagForUpdate(Conn);
        WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, 0, 0);
    }
    Events->CommonCount--;
}

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events) {
    LqArr2AlignAfterRemove(&Events->ConnArr, LqConn*, nullptr);
    LqArr3AlignAfterRemove(&Events->EvntArr, HANDLE, LqEvntFd*, nullptr);

}

LqClientHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    if(Events->EventObjectIndex < Events->EvntArr.Count)
        return (LqClientHdr*)LqArr3At_2(&Events->EvntArr, LqEvntFd*, Events->EventObjectIndex);
    return (LqClientHdr*)LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
}

void LqSysPollUnuseCurrent(LqSysPoll* Events) {
    u_long res;
    LqConn* Conn;
    LqEvntFd* Fd;
    char t;
    LqEvntFlag Flag;
    if(Events->EventObjectIndex >= Events->EvntArr.Count) {
        Conn = LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
        Flag = LqClientGetFlags(Conn);
        if(Flag & LQEVNT_FLAG_RD) {
            res = 0UL;
            ioctlsocket(Conn->Fd, FIONREAD, &res);
            if(res > 0UL)
                PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_READ);
        }
        if(Flag & LQEVNT_FLAG_WR) {
            if(send(Conn->Fd, &t, 0, 0) >= 0)
                PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_WRITE);
        }
        return;
    }
    Fd = PtrKernelEventAt(Events, Events->EventObjectIndex);
    switch(Fd->_Type) {
        case LQ_AFD_POLL_TYPE_PIPE:
            PipeProcessAsyncSelect(Events, Events->EventObjectIndex);
            break;
        case LQ_AFD_POLL_TYPE_TERMINAL:
            TerminalProcessAsyncSelect(Events, Events->EventObjectIndex);
            break;
    }
}


static bool LqEvntSetMaskConn(LqSysPoll* Events, intptr_t Index, LqEvntFlag NewFlags) {
    u_long InternalBufLen;
    char t;
    LqConn* Conn;
    bool Res;

    Conn = LqArr2At(&Events->ConnArr, LqConn*, Index);
    Res = WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, Index + WM_USER, LqEvntSystemEventByConnFlag(NewFlags)) != SOCKET_ERROR;
    if(NewFlags & LQEVNT_FLAG_RD) {
        InternalBufLen = 0UL;
        ioctlsocket(Conn->Fd, FIONREAD, &InternalBufLen);
        if(InternalBufLen > 0UL)
            PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_READ);
    }
    if(NewFlags & LQEVNT_FLAG_WR) {
        if(send(Conn->Fd, &t, 0, 0) >= 0)
            PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_WRITE);
    }
    return Res;
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
    NewFlags = _LqEvntGetFlagForUpdate(LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex));
    return LqEvntSetMaskConn(Events, Events->ConnIndex, NewFlags);
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*)) {
    LqEvntInterator Iter;
    uintptr_t Index;
    LqEvntFlag NewFlags;
    for(register auto i = &ConnAt(Events, 0), m = i + Events->ConnArr.AllocCount; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            Index = (((uintptr_t)i) - ((uintptr_t)&ConnAt(Events, 0))) / sizeof(LqConn*);
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                Iter.IsEnumConn = true;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                /* Update fast indexes */
                i = &ConnAt(Events, Index);
                m = &ConnAt(Events, Events->ConnArr.AllocCount);
            } else {
                LqEvntSetMaskConn(Events, Index, NewFlags);
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
        for(m = Events->ConnArr.AllocCount; Index < m; Index++)
            if(ConnAt(Events, Index) != NULL) {
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
    LqConn* Conn;
    if(Interator->IsEnumConn) {
        Conn = LqArr2At(&Events->ConnArr, LqConn*, Interator->Index);
        LqArr2RemoveAt(&Events->ConnArr, LqConn*, Interator->Index, NULL);
        WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, 0, 0);
        _LqEvntGetFlagForUpdate(Conn);
        Ret = (LqClientHdr*)Conn;
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
        return (LqClientHdr*)ConnAt(Events, Interator->Index);
    return (LqClientHdr*)PtrKernelEventAt(Events, Interator->Index);
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    DWORD Stat;
    intptr_t Index;
    intptr_t Count;
    intptr_t StartIndex;
    LqTimeMillisec StartTime;
    if(Events->EvntArr.Count >= MAXIMUM_WAIT_OBJECTS) {
        /*On windows, when count event object greater then 64 is necessary check in parts :(*/
        StartIndex = ((intptr_t)0);
        Count = ((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1);
        StartTime = LqTimeGetLocMillisec();
        for(;;) {
            Stat = MsgWaitForMultipleObjectsEx(Count, ((HANDLE*)Events->EvntArr.Data) + StartIndex, LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS, QS_POSTMESSAGE, 0);
            if(Events->IsHaveOnlyHup == ((uintptr_t)1)) {
                Events->IsHaveOnlyHup = ((uintptr_t)0);
                Events->EventObjectIndex = -((intptr_t)1);
                return 1;
            }
            if(Stat != STATUS_TIMEOUT) {
                Index = Stat - STATUS_WAIT_0;
                if((Index >= STATUS_WAIT_0) && (Index < Count)) {
                    Events->EventObjectIndex = ((intptr_t)Index) - ((intptr_t)1) + StartIndex;
                    return 1;
                }
                Events->EventObjectIndex = LQ_NUMERIC_MAX(intptr_t);
                if(Index == Count)
                    return 1;
                /* Have Error */
                return -1;
            }
            if((LqTimeGetLocMillisec() - StartTime) >= WaitTime) {
                /* Is timeout */
                Events->EventObjectIndex = LQ_NUMERIC_MAX(intptr_t);
                return 0;
            }
            StartIndex += Count;
            Count = Events->EvntArr.Count - StartIndex;
            if(Count > (((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1))) {
                Count = (((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1));
            } else if(Count <= ((intptr_t)0)) {
                Count = (((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1));
                StartIndex = ((intptr_t)0);
            }
        }
    }
    if((Events->IsHaveOnlyHup == ((uintptr_t)1)) && (WaitTime > LQ_WINEVNT_WAIT_WHEN_HAVE_ONLY_HUP_OBJ))
        WaitTime = LQ_WINEVNT_WAIT_WHEN_HAVE_ONLY_HUP_OBJ;
    Stat = MsgWaitForMultipleObjectsEx(Events->EvntArr.Count, (HANDLE*)Events->EvntArr.Data, WaitTime, QS_POSTMESSAGE, 0);
    if(Events->IsHaveOnlyHup == ((uintptr_t)1)) {
        Events->IsHaveOnlyHup = ((uintptr_t)0);
        Events->EventObjectIndex = -((intptr_t)1);
        return 1;
    }
    if(Stat == STATUS_TIMEOUT) {
        Events->EventObjectIndex = LQ_NUMERIC_MAX(intptr_t);
        return 0;
    }
    Index = ((intptr_t)Stat) - ((intptr_t)STATUS_WAIT_0);
    if((Index >= ((intptr_t)STATUS_WAIT_0)) && (Index < Events->EvntArr.Count)) {
        Events->EventObjectIndex = Index - ((intptr_t)1);
        return 1;
    }
    Events->EventObjectIndex = LQ_NUMERIC_MAX(intptr_t);
    if(Index == Events->EvntArr.Count)
        return 1;
    /* When have error */
    return -1;
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

