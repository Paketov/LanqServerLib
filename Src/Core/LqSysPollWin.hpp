/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSysPoll... - Multiplatform abstracted event folower
* This part of server support:
*   +Windows native events objects.
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
    LqArr3Init(&Dest->EvntFdArr);

    Dest->EventObjectIndex = INTPTR_MAX;
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
    if(Dest->EvntFdArr.Count > ((intptr_t)0)) {
        for(i = 0, m = Dest->EvntFdArr.Count; i < m; i++) {
            if(LqArr3At_1(&Dest->EvntFdArr, HANDLE, i) != (HANDLE)LqArr3At_2(&Dest->EvntFdArr, LqEvntFd*, i)->Fd)
                LqFileClose((int)LqArr3At_1(&Dest->EvntFdArr, HANDLE, i)); //close only events
        }
    }
    LqArr2Uninit(&Dest->ConnArr);
    LqArr3Uninit(&Dest->EvntFdArr);
}

/*
* Adding to follow all type of WinNT objects
*/
bool LqSysPollAddHdr(LqSysPoll* Dest, LqClientHdr* Client) {
    LARGE_INTEGER *ppl, pl;
    LqEvntFd* EvntData;
    int Event;
    char tt;
    u_long InternalBufLen;
    static char Buf1;
    static char Buf2;
    size_t InsertIndex;
    NTSTATUS Stat;
    LqEvntFlag NewFlags = _LqEvntGetFlagForUpdate(Client);
    if(NewFlags & _LQEVNT_FLAG_CONN) {
        LqArr2PushBack(&Dest->ConnArr, LqConn*, InsertIndex, NULL);
        LqArr2At(&Dest->ConnArr, LqConn*, InsertIndex) = (LqConn*)Client;
        WSAAsyncSelect(Client->Fd, (HWND)Dest->WinHandle, WM_USER + InsertIndex, LqEvntSystemEventByConnFlag(NewFlags));
        if(NewFlags & LQEVNT_FLAG_RD) {
            InternalBufLen = 0UL;
            ioctlsocket(Client->Fd, FIONREAD, &InternalBufLen);
            if(InternalBufLen > 0UL)
                PostMessage((HWND)Dest->WinHandle, WM_USER + InsertIndex, Client->Fd, FD_READ);
        }
        if(NewFlags & LQEVNT_FLAG_WR) {
            if(send(Client->Fd, &tt, 0, 0) >= 0)
                PostMessage((HWND)Dest->WinHandle, WM_USER + InsertIndex, Client->Fd, FD_WRITE);
        }
    } else {
        ppl = NULL;
        EvntData = (LqEvntFd*)Client;
        Event = LqEventCreate(LQ_O_NOINHERIT);
        if(NewFlags & LQEVNT_FLAG_RD) {
            ppl = NULL;
lblAgain:
            EvntData->__Reserved1.Status = STATUS_PENDING;
            switch(Stat = NtReadFile((HANDLE)EvntData->Fd, (HANDLE)Event, NULL, NULL, (PIO_STATUS_BLOCK)&EvntData->__Reserved1, &Buf1, 0, ppl, NULL)) {
                case STATUS_MORE_PROCESSING_REQUIRED:
                case STATUS_SUCCESS:
                case STATUS_PIPE_BROKEN:
                case STATUS_PIPE_CLOSING:
                    EvntData->__Reserved1.Status = Stat;
                    break;
                case STATUS_PENDING:
                    break;
                case STATUS_INVALID_PARAMETER:
                    if(ppl == NULL) {
                        ppl = &pl;
                        pl.QuadPart = LqFileTell(EvntData->Fd);
                        goto lblAgain;
                    }
                    goto lblErr;
                case STATUS_OBJECT_TYPE_MISMATCH:
                    /*If object is event, then follow them*/
                    EvntData->__Reserved1.Status = STATUS_OBJECT_TYPE_MISMATCH;
                    if(WaitForSingleObject((HANDLE)EvntData->Fd, 0) != WAIT_FAILED) {
                        LqFileClose(Event);
                        Event = EvntData->Fd;
                        break;
                    }
lblErr:
                default:
                    LqFileClose(Event);
                    return false;
            }
        } else {
            EvntData->__Reserved1.Status = STATUS_NOT_SUPPORTED;
        }
        if(NewFlags & LQEVNT_FLAG_WR) {
            ppl = NULL;
lblAgain2:
            EvntData->__Reserved2.Status = STATUS_PENDING;
            switch(Stat =
                (Event == EvntData->Fd) ?
                STATUS_OBJECT_TYPE_MISMATCH :
                NtWriteFile((HANDLE)EvntData->Fd, (HANDLE)Event, NULL, NULL, (PIO_STATUS_BLOCK)&EvntData->__Reserved2, &Buf2, 0, ppl, NULL)
            ) {
                case STATUS_MORE_PROCESSING_REQUIRED:
                case STATUS_SUCCESS:
                case STATUS_PIPE_BROKEN:
                case STATUS_PIPE_CLOSING:
                    EvntData->__Reserved2.Status = Stat;
                    break;
                case STATUS_PENDING:
                    break;
                case STATUS_INVALID_PARAMETER:
                    if(ppl == NULL) {
                        ppl = &pl;
                        pl.QuadPart = LqFileTell(EvntData->Fd);
                        goto lblAgain2;
                    }
                    goto lblErr2;
                case STATUS_OBJECT_TYPE_MISMATCH:
                    EvntData->__Reserved2.Status = STATUS_OBJECT_TYPE_MISMATCH;
                    if(WaitForSingleObject((HANDLE)EvntData->Fd, 0) != WAIT_FAILED) {
                        if(Event != EvntData->Fd) {
                            LqFileClose(Event); Event = EvntData->Fd;
                        }
                        break;
                    }
lblErr2:
                default:
                    if(Event != EvntData->Fd)
                        LqFileClose(Event);
                    return false;
            }
        } else {
            EvntData->__Reserved2.Status = STATUS_NOT_SUPPORTED;
        }
        if(!(NewFlags & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RD)) && (NewFlags & (LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP))) {
            Dest->IsHaveOnlyHup = (uintptr_t)1;
            if(EvntData->Fd != Event)
                LqEventReset(Event);
        }
        LqArr3PushBack(&Dest->EvntFdArr, HANDLE, LqClientHdr*);

        LqArr3Back_1(&Dest->EvntFdArr, HANDLE) = (HANDLE)Event;
        LqArr3Back_2(&Dest->EvntFdArr, LqEvntFd*) = EvntData;
    }
    Dest->CommonCount++;
    return true;
}

LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events) {
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events) {
    LqEvntFlag Flag;
    MSG Msg;
    LqEvntFd* Fd;
    HANDLE Event;
    LqEvntFlag ResFlag;
    intptr_t i, m;
    static const LARGE_INTEGER TimeWait = {0};
    LARGE_INTEGER *ppl, pl;
    UINT ConnIndex;
    LqConn* Conn;
    LqEvntFlag r;
    IO_STATUS_BLOCK StatusBlock;
    static char Buf1;
    NTSTATUS Stat;
    WORD EventNum;
    WORD Error;

    if(Events->EventObjectIndex < Events->EvntFdArr.Count) {
        for(i = Events->EventObjectIndex + ((intptr_t)1), m = Events->EvntFdArr.Count; i < m; i++) {
            if((Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, i)) == NULL)
                continue;
            Event = LqArr3At_1(&Events->EvntFdArr, HANDLE, i);
            ResFlag = ((LqEvntFlag)0);
            Flag = LqClientGetFlags(Fd);
            if(Flag & LQEVNT_FLAG_RD) {
                switch(Fd->__Reserved1.Status) {
                    case STATUS_MORE_PROCESSING_REQUIRED:
                    case STATUS_SUCCESS:
                        ResFlag |= LQEVNT_FLAG_RD;
                        break;
                    case STATUS_OBJECT_TYPE_MISMATCH:
                        switch(NtWaitForSingleObject(Event, FALSE, (PLARGE_INTEGER)&TimeWait)) {
                            case STATUS_SUCCESS:
                                ResFlag |= LQEVNT_FLAG_RD;
                            case STATUS_TIMEOUT:
                            case STATUS_ALERTED:
                                break;
                            default:
                                ResFlag |= LQEVNT_FLAG_ERR; break;
                        }
                        break;
                    case STATUS_PIPE_BROKEN:
                    case STATUS_PENDING:
                    case STATUS_NOT_SUPPORTED:
                    case STATUS_PIPE_CLOSING:
                        break;
                    default:
                        ResFlag |= LQEVNT_FLAG_ERR;
                }
            }
            if(Flag & LQEVNT_FLAG_WR) {
                switch(Fd->__Reserved2.Status) {
                    case STATUS_MORE_PROCESSING_REQUIRED:
                    case STATUS_SUCCESS:
                        ResFlag |= LQEVNT_FLAG_WR;
                        break;
                    case STATUS_OBJECT_TYPE_MISMATCH:
                        switch(NtWaitForSingleObject(Event, FALSE, (PLARGE_INTEGER)&TimeWait)) {
                            case STATUS_SUCCESS:
                                ResFlag |= LQEVNT_FLAG_WR;
                            case STATUS_TIMEOUT:
                            case STATUS_ALERTED:
                                break;
                            default:
                                ResFlag |= LQEVNT_FLAG_ERR; break;
                        }
                        break;
                    case STATUS_PIPE_BROKEN:
                    case STATUS_PENDING:
                    case STATUS_NOT_SUPPORTED:
                    case STATUS_PIPE_CLOSING:
                        break;
                    default:
                        ResFlag |= LQEVNT_FLAG_ERR;
                }
            }
            if(Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_HUP)) {
                if(!(Flag & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RD))) {
                    Events->IsHaveOnlyHup = ((uintptr_t)1);
                    ppl = NULL; 
lblAgain2:
                    StatusBlock.Information = ((ULONG_PTR)0);
                    StatusBlock.Status = ((NTSTATUS)0);
                    switch(Stat = NtWriteFile((HANDLE)Fd->Fd, Event, NULL, NULL, &StatusBlock, &Buf1, 0, ppl, NULL)) {
                        case STATUS_PIPE_BROKEN:
                        case STATUS_PIPE_CLOSING:
                            ResFlag |= LQEVNT_FLAG_HUP;
                            break;
                        case STATUS_MORE_PROCESSING_REQUIRED:
                        case STATUS_SUCCESS:
                            break;
                        case STATUS_PENDING:
                            NtCancelIoFile((HANDLE)Fd->Fd, &StatusBlock);
                            break;
                        case STATUS_INVALID_PARAMETER:
                            if(ppl == NULL) {
                                ppl = &pl;
                                pl.QuadPart = LqFileTell(Fd->Fd);
                                goto lblAgain2;
                            }
                        default:
                            ResFlag |= LQEVNT_FLAG_ERR;
                            break;
                    }
                    LqEventReset((int)Event);
                } else if(
                    (Fd->__Reserved1.Status == STATUS_PIPE_BROKEN) ||
                    (Fd->__Reserved1.Status == STATUS_PIPE_CLOSING) || 
                    (Fd->__Reserved2.Status == STATUS_PIPE_BROKEN) ||
                    (Fd->__Reserved2.Status == STATUS_PIPE_CLOSING)
                ) {
                    ResFlag |= LQEVNT_FLAG_HUP;
                }
            }
            if(ResFlag == ((LqEvntFlag)0))
                continue;
            Events->EventObjectIndex = i;
            return ResFlag;
        }
        Events->EventObjectIndex = INTPTR_MAX;
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
    return 0;
}

void LqSysPollRemoveCurrent(LqSysPoll* Events) {
    LqConn* Conn;
    LqEvntFd* Fd;
    HANDLE EventObject;
    if(Events->EventObjectIndex < Events->EvntFdArr.Count) {
        Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex);
        EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Events->EventObjectIndex);
        LqArr3RemoveAt(&Events->EvntFdArr, HANDLE, LqEvntFd*, Events->EventObjectIndex, NULL);
        if((HANDLE)Fd->Fd != EventObject) {
            if(Fd->__Reserved1.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
            if(Fd->__Reserved2.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
            LqFileClose((int)EventObject);
        }
        _LqEvntGetFlagForUpdate(Fd);
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
    LqArr3AlignAfterRemove(&Events->EvntFdArr, HANDLE, LqEvntFd*, nullptr);
}

LqClientHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    if(Events->EventObjectIndex < Events->EvntFdArr.Count)
        return (LqClientHdr*)LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex);
    return (LqClientHdr*)LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
}

void LqSysPollUnuseCurrent(LqSysPoll* Events) {
    u_long res;
    LqConn* Conn;
    LqEvntFd* Fd;
    HANDLE EventObject;
    LARGE_INTEGER *ppl, pl;
    NTSTATUS Stat;
    static char Buf1;
    static char Buf2;
    char t;
    LqEvntFlag Flag;

    if(Events->EventObjectIndex >= Events->EvntFdArr.Count) {
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

    Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex);
    EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Events->EventObjectIndex);
    if((HANDLE)Fd->Fd != EventObject) {
        Flag = LqClientGetFlags(Fd);
        ppl = NULL;
        if((Flag & LQEVNT_FLAG_RD) && (Fd->__Reserved1.Status != STATUS_PENDING)) {
            LqEventReset((int)EventObject);
            ppl = NULL;
lblAgain:
            Fd->__Reserved1.Status = STATUS_PENDING;
            Stat = NtReadFile((HANDLE)Fd->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved1, &Buf1, 0, ppl, NULL);
            if((Stat == STATUS_INVALID_PARAMETER) && (ppl == NULL)) {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd->Fd);
                goto lblAgain;
            } else if(Stat != STATUS_PENDING) {
                Fd->__Reserved1.Status = Stat;
            }
        }
        if((Flag & LQEVNT_FLAG_WR) && (Fd->__Reserved2.Status != STATUS_PENDING)) {
            if(!(Flag & LQEVNT_FLAG_RD))
                LqEventReset((int)EventObject);
            ppl = NULL;
lblAgain2:
            Stat = NtWriteFile((HANDLE)Fd->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved2, &Buf2, 0, ppl, NULL);
            if((Stat == STATUS_INVALID_PARAMETER) && (ppl == NULL)) {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd->Fd);
                goto lblAgain2;
            } else if(Stat != STATUS_PENDING) {
                Fd->__Reserved2.Status = Stat;
            }
        }
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

static bool LqEvntSetMaskEventFd(LqSysPoll* Events, intptr_t Index, LqEvntFlag NewFlag) {
    LARGE_INTEGER *ppl, pl;
    static char Buf1;
    static char Buf2;
    NTSTATUS Stat;
    bool IsCleared;
    LqEvntFd* h = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Index);
    HANDLE EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Index);
    if((HANDLE)h->Fd != EventObject) {
        IsCleared = false;
        ppl = NULL;
        if(NewFlag & LQEVNT_FLAG_RD) {
            if(h->__Reserved1.Status == STATUS_NOT_SUPPORTED) {
                LqEventReset((int)EventObject);
                IsCleared = true;
                ppl = NULL;
lblAgain:
                h->__Reserved1.Status = STATUS_PENDING;
                Stat = NtReadFile((HANDLE)h->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&h->__Reserved1, &Buf1, 0, ppl, NULL);
                if((Stat == STATUS_INVALID_PARAMETER) && (ppl == NULL)) {
                    ppl = &pl;
                    pl.QuadPart = LqFileTell(h->Fd);
                    goto lblAgain;
                } else if(Stat != STATUS_PENDING) {
                    h->__Reserved1.Status = Stat;
                }
            }
        } else {
            if((h->__Reserved1.Status != STATUS_NOT_SUPPORTED) && (h->__Reserved1.Status != STATUS_PENDING))
                NtCancelIoFile((HANDLE)h->Fd, (PIO_STATUS_BLOCK)&h->__Reserved1);
            h->__Reserved1.Status = STATUS_NOT_SUPPORTED;
        }

        if(NewFlag & LQEVNT_FLAG_WR) {
            if(h->__Reserved2.Status == STATUS_NOT_SUPPORTED) {
                if(!IsCleared)
                    LqEventReset((int)EventObject);
                ppl = NULL;
lblAgain2:
                h->__Reserved2.Status = STATUS_PENDING;
                Stat = NtWriteFile((HANDLE)h->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&h->__Reserved2, &Buf2, 0, ppl, NULL);
                if((Stat == STATUS_INVALID_PARAMETER) && (ppl == NULL)) {
                    ppl = &pl;
                    pl.QuadPart = LqFileTell(h->Fd);
                    goto lblAgain2;
                } else if(Stat != STATUS_PENDING) {
                    h->__Reserved2.Status = Stat;
                }
            }
        } else {
            if((h->__Reserved2.Status != STATUS_NOT_SUPPORTED) && (h->__Reserved2.Status != STATUS_PENDING))
                NtCancelIoFile((HANDLE)h->Fd, (PIO_STATUS_BLOCK)&h->__Reserved2);
            h->__Reserved2.Status = STATUS_NOT_SUPPORTED;
        }
    }
    if(!(NewFlag & (LQEVNT_FLAG_WR | LQEVNT_FLAG_RD)) && (NewFlag & (LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP))) {
        Events->IsHaveOnlyHup = (uintptr_t)1;
        if((HANDLE)h->Fd != EventObject)
            LqEventReset((int)EventObject);
    }
    return true;
}

bool LqSysPollSetMaskByCurrent(LqSysPoll* Events) {
    LqEvntFlag NewFlags;
    if(Events->EventObjectIndex < Events->EvntFdArr.Count) {
        NewFlags = _LqEvntGetFlagForUpdate(LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex));
        return LqEvntSetMaskEventFd(Events, Events->EventObjectIndex, NewFlags);
    }
    NewFlags = _LqEvntGetFlagForUpdate(LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex));
    return LqEvntSetMaskConn(Events, Events->ConnIndex, NewFlags);
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*)) {
    LqEvntInterator Iter;
    uintptr_t Index;
    LqEvntFlag NewFlags;
    for(register auto i = &LqArr2At(&Events->ConnArr, LqConn*, 0), m = i + Events->ConnArr.AllocCount; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            Index = (((uintptr_t)i) - ((uintptr_t)&LqArr2At(&Events->ConnArr, LqConn*, 0))) / sizeof(LqConn*);
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                Iter.Index = Index;
                Iter.IsEnumConn = true;
                DelProc(UserData, &Iter);
                /* Update fast indexes */
                i = &LqArr2At(&Events->ConnArr, LqConn*, Index);
                m = &LqArr2At(&Events->ConnArr, LqConn*, Events->ConnArr.AllocCount);
            } else {
                LqEvntSetMaskConn(Events, Index, NewFlags);
            }
        }
    for(register auto i = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, 0), m = i + Events->EvntFdArr.Count; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            Index = (((uintptr_t)i) - ((uintptr_t)&LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, 0))) / sizeof(LqEvntFd*);
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                Iter.Index = Index;
                Iter.IsEnumConn = false;
                DelProc(UserData, &Iter);
                /* Update fast indexes */
                i = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Index);
                m = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EvntFdArr.Count);
            } else {
                LqEvntSetMaskEventFd(Events, Index, NewFlags);
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
            if(LqArr2At(&Events->ConnArr, LqConn*, Index) != NULL) {
                Interator->Index = Index;
                return true;
            }
        Index = ((intptr_t)0);
        Interator->IsEnumConn = false;
    }
    for(m = Events->EvntFdArr.Count; Index < m; Index++)
        if(LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Index) != NULL) {
            Interator->Index = Index;
            return true;
        }
    return false;
}

LqClientHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    LqClientHdr* Ret;
    LqConn* Conn;
    HANDLE EventObject;
    LqEvntFd* Fd;
    if(Interator->IsEnumConn) {
        Conn = LqArr2At(&Events->ConnArr, LqConn*, Interator->Index);
        LqArr2RemoveAt(&Events->ConnArr, LqConn*, Interator->Index, NULL);
        WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, 0, 0);
        _LqEvntGetFlagForUpdate(Conn);
        Ret = (LqClientHdr*)Conn;
    } else {
        EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Interator->Index);
        Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Interator->Index);
        Ret = (LqClientHdr*)Fd;
        LqArr3RemoveAt(&Events->EvntFdArr, HANDLE, LqEvntFd*, Interator->Index, NULL);
        _LqEvntGetFlagForUpdate(Fd);
        if((HANDLE)Fd->Fd != EventObject) {
            if(Fd->__Reserved1.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
            if(Fd->__Reserved2.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
            LqFileClose((int)EventObject);
        }
    }
    Events->CommonCount--;
    return Ret;
}

LqClientHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    if(Interator->IsEnumConn)
        return (LqClientHdr*)LqArr2At(&Events->ConnArr, LqConn*, Interator->Index);
    return (LqClientHdr*)LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Interator->Index);
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    DWORD Stat;
    intptr_t Index;
    intptr_t Count;
    intptr_t StartIndex;
    LqTimeMillisec StartTime;
    if(Events->EvntFdArr.Count >= MAXIMUM_WAIT_OBJECTS) {
        /*On windows, when count event object greater then 64 is necessary check in parts :(*/
        StartIndex = ((intptr_t)0);
        Count = ((intptr_t)MAXIMUM_WAIT_OBJECTS) - ((intptr_t)1);
        StartTime = LqTimeGetLocMillisec();
        for(;;) {
            Stat = MsgWaitForMultipleObjectsEx(Count, ((HANDLE*)Events->EvntFdArr.Data) + StartIndex, LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS, QS_POSTMESSAGE, 0);
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
                Events->EventObjectIndex = INTPTR_MAX;
                if(Index == Count)
                    return 1;
                /* Have Error */
                return -1;
            }
            if((LqTimeGetLocMillisec() - StartTime) >= WaitTime) {
                /* Is timeout */
                Events->EventObjectIndex = INTPTR_MAX;
                return 0;
            }
            StartIndex += Count;
            Count = Events->EvntFdArr.Count - StartIndex;
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
    Stat = MsgWaitForMultipleObjectsEx(Events->EvntFdArr.Count, (HANDLE*)Events->EvntFdArr.Data, WaitTime, QS_POSTMESSAGE, 0);
    if(Events->IsHaveOnlyHup == ((uintptr_t)1)) {
        Events->IsHaveOnlyHup = ((uintptr_t)0);
        Events->EventObjectIndex = -((intptr_t)1);
        return 1;
    }
    if(Stat == STATUS_TIMEOUT) {
        Events->EventObjectIndex = INTPTR_MAX;
        return 0;
    }
    Index = ((intptr_t)Stat) - ((intptr_t)STATUS_WAIT_0);
    if((Index >= ((intptr_t)STATUS_WAIT_0)) && (Index < Events->EvntFdArr.Count)) {
        Events->EventObjectIndex = Index - ((intptr_t)1);
        return 1;
    }
    Events->EventObjectIndex = INTPTR_MAX;
    if(Index == Events->EvntFdArr.Count)
        return 1;
    /* When have error */
    return -1;
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

