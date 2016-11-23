/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower
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
#include <Winternl.h>
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


typedef enum _WAIT_TYPE
{
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

//#define LQCONN_FLAG_RD_AGAIN _LQEVNT_FLAG_RESERVED_1
//#define LQCONN_FLAG_WR_AGAIN _LQEVNT_FLAG_RESERVED_2


#define LqEvntSystemEventByConnEvents(Client)               \
    (((Client->Flag & LQEVNT_FLAG_RD)        ? FD_READ : 0)  | \
    ((Client->Flag & LQEVNT_FLAG_WR)         ? FD_WRITE : 0) |\
    ((Client->Flag & LQEVNT_FLAG_ACCEPT)    ? FD_ACCEPT : 0)  | \
    ((Client->Flag & LQEVNT_FLAG_CONNECT)    ? FD_CONNECT : 0) |\
    ((Client->Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) ? FD_CLOSE: 0))

#define IsRdAgain(Client)  ((Client->Flag & (LQCONN_FLAG_RD_AGAIN | LQEVNT_FLAG_RD)) == (LQCONN_FLAG_RD_AGAIN | LQEVNT_FLAG_RD))
#define IsWrAgain(Client)  ((Client->Flag & (LQCONN_FLAG_WR_AGAIN | LQEVNT_FLAG_WR)) == (LQCONN_FLAG_WR_AGAIN | LQEVNT_FLAG_WR))
#define IsAgain(Client)    (IsRdAgain(Client) || IsWrAgain(Client))

static LRESULT CALLBACK WindowProcedure(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(window, msg, wp, lp);
}

static struct _wnd_class
{
    WNDCLASSEX* wc;
    _wnd_class()
    {
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
    ~_wnd_class()
    {
        UnregisterClass(wc->lpszClassName, 0);
    }
} wnd_class;

bool LqEvntInit(LqEvnt* Dest)
{
    LqArr2Init(&Dest->ConnArr);
    LqArr3Init(&Dest->EvntFdArr);

    Dest->EventObjectIndex = 0xffffff;
    Dest->ConnIndex = 0;
    Dest->DeepLoop = 0;
    Dest->IsRemoved = false;
    Dest->IsRemovedEvntFd = false;
    Dest->CommonCount = 0;
    Dest->WinHandle = (uintptr_t)-1;
    return true;
}

bool LqEvntThreadInit(LqEvnt* Dest)
{
    auto hSockWnd = CreateWindowEx(0, TEXT("SockClass"), TEXT(""), WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, 0, 0);
    if(hSockWnd == NULL)
        return false;
    Dest->WinHandle = (uintptr_t)hSockWnd;
    auto Conns = &LqArr2At(&Dest->ConnArr, LqConn*, 0);
    for(intptr_t i = 0, m = Dest->ConnArr.AllocCount; i < m; i++)
    {
        if(Conns[i] != nullptr)
        {
            WSAAsyncSelect(Conns[i]->Fd, hSockWnd, WM_USER + i, LqEvntSystemEventByConnEvents(Conns[i]));
            if(Conns[i]->Flag & LQEVNT_FLAG_RD)
            {
                u_long res = -1;
                ioctlsocket(Conns[i]->Fd, FIONREAD, &res);
                if(res > 0)
                    PostMessage((HWND)Dest->WinHandle, WM_USER + i, Conns[i]->Fd, FD_READ);
            }
            if(Conns[i]->Flag & LQEVNT_FLAG_WR)
            {
                char t;
                if(send(Conns[i]->Fd, &t, 0, 0) >= 0)
                    PostMessage((HWND)Dest->WinHandle, WM_USER + i, Conns[i]->Fd, FD_WRITE);
            }
        }
    }
    return true;
}

void LqEvntThreadUninit(LqEvnt* Dest)
{
    CloseWindow((HWND)Dest->WinHandle);
    Dest->WinHandle = (uintptr_t)-1;
}

void LqEvntUninit(LqEvnt* Dest)
{
    if(Dest->EvntFdArr.Count > 0)
    {
        for(size_t i = 0, m = Dest->EvntFdArr.Count; i < m; i++)
        {
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
bool LqEvntAddHdr(LqEvnt* Dest, LqEvntHdr* Client)
{
    if(LqEvntIsConn(Client))
    {
        size_t InsertIndex;
        LqArr2PushBack(&Dest->ConnArr, LqConn*, InsertIndex, nullptr);
		LqArr2At(&Dest->ConnArr, LqConn*, InsertIndex) = (LqConn*)Client;

        WSAAsyncSelect(Client->Fd, (HWND)Dest->WinHandle, WM_USER + InsertIndex, LqEvntSystemEventByConnEvents(Client));
       
        if(Client->Flag & LQEVNT_FLAG_RD)
        {
            u_long res = -1;
            ioctlsocket(Client->Fd, FIONREAD, &res);
            if(res > 0)
                PostMessage((HWND)Dest->WinHandle, WM_USER + InsertIndex, Client->Fd, FD_READ);
        }
        if(Client->Flag & LQEVNT_FLAG_WR)
        {
            char t;
            if(send(Client->Fd, &t, 0, 0) >= 0)
                PostMessage((HWND)Dest->WinHandle, WM_USER + InsertIndex, Client->Fd, FD_WRITE);
        }
        Client->Flag &= ~_LQEVNT_FLAG_SYNC;
    } else
    {
        LARGE_INTEGER *ppl = nullptr, pl;
        auto EvntData = (LqEvntFd*)Client;
        int Event = LqFileEventCreate(LQ_O_NOINHERIT);
        if(EvntData->Flag & LQEVNT_FLAG_RD)
        {
            static char Buf;
            ppl = nullptr;
lblAgain:
            EvntData->__Reserved1.Status = STATUS_PENDING;
            switch(auto Stat = NtReadFile((HANDLE)EvntData->Fd, (HANDLE)Event, NULL, NULL, (PIO_STATUS_BLOCK)&EvntData->__Reserved1, &Buf, 0, ppl, NULL))
            {
                case STATUS_MORE_PROCESSING_REQUIRED:
                case STATUS_SUCCESS:
                case STATUS_PIPE_BROKEN:
                    EvntData->__Reserved1.Status = Stat;
                    break;
                case STATUS_PENDING:
                    break;
                case STATUS_INVALID_PARAMETER:
                    if(ppl == nullptr)
                    {
                        ppl = &pl;
                        pl.QuadPart = LqFileTell(EvntData->Fd);
                        goto lblAgain;
                    }
                    goto lblErr;
                case STATUS_OBJECT_TYPE_MISMATCH:
                    /*If object is event, then follow them*/
                    EvntData->__Reserved1.Status = STATUS_OBJECT_TYPE_MISMATCH;
                    if(WaitForSingleObject((HANDLE)EvntData->Fd, 0) != WAIT_FAILED)
                    {
                        LqFileClose(Event);
                        Event = EvntData->Fd;
                        break;
                    }
lblErr:
                default:
                    LqFileClose(Event);
                    return false;
            }
        } else
        {
            EvntData->__Reserved1.Status = STATUS_NOT_SUPPORTED;
        }
        if(EvntData->Flag & LQEVNT_FLAG_WR)
        {
            static char Buf;
            ppl = nullptr;
lblAgain2:
            EvntData->__Reserved2.Status = STATUS_PENDING;
            switch(auto Stat =
                (Event == EvntData->Fd) ?
                   STATUS_OBJECT_TYPE_MISMATCH :
                   NtWriteFile((HANDLE)EvntData->Fd, (HANDLE)Event, NULL, NULL, (PIO_STATUS_BLOCK)&EvntData->__Reserved2, &Buf, 0, ppl, NULL)
                   )
            {
                case STATUS_MORE_PROCESSING_REQUIRED:
                case STATUS_SUCCESS:
                case STATUS_PIPE_BROKEN:
                    EvntData->__Reserved2.Status = Stat;
                    break;
                case STATUS_PENDING:
                    break;
                case STATUS_INVALID_PARAMETER:
                    if(ppl == nullptr)
                    {
                        ppl = &pl;
                        pl.QuadPart = LqFileTell(EvntData->Fd);
                        goto lblAgain2;
                    }
                    goto lblErr2;
                case STATUS_OBJECT_TYPE_MISMATCH:
                    EvntData->__Reserved2.Status = STATUS_OBJECT_TYPE_MISMATCH;
                    if(WaitForSingleObject((HANDLE)EvntData->Fd, 0) != WAIT_FAILED)
                    {
                        if(Event != EvntData->Fd)
                            LqFileClose(Event), Event = EvntData->Fd;
                        break;
                    }
lblErr2:
                default:
                    if(Event != EvntData->Fd)
                        LqFileClose(Event);
                    return false;
            }
        } else
        {
            EvntData->__Reserved2.Status = STATUS_NOT_SUPPORTED;
        }
		EvntData->Flag &= ~_LQEVNT_FLAG_SYNC;
        LqArr3PushBack(&Dest->EvntFdArr, HANDLE, LqEvntHdr*);

        LqArr3Back_1(&Dest->EvntFdArr, HANDLE) = (HANDLE)Event;
        LqArr3Back_2(&Dest->EvntFdArr, LqEvntFd*) = EvntData;
    }
    Dest->CommonCount++;
    return true;
}

LqEvntFlag __LqEvntEnumEventBegin(LqEvnt* Events)
{
    Events->DeepLoop++;
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqEvnt* Events)
{
    if(Events->EventObjectIndex < Events->EvntFdArr.Count)
    {
        for(intptr_t i = Events->EventObjectIndex + 1, m = Events->EvntFdArr.Count; i < m; i++)
        {
            auto Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, i);
            if(Fd == nullptr)
                continue;
            auto Event = LqArr3At_1(&Events->EvntFdArr, HANDLE, i);
            LqEvntFlag r = 0;
            static const LARGE_INTEGER TimeWait = {0};
            if(Fd->Flag & LQEVNT_FLAG_RD)
            {
                switch(Fd->__Reserved1.Status)
                {
                    case STATUS_MORE_PROCESSING_REQUIRED:
                    case STATUS_SUCCESS:
                        r |= LQEVNT_FLAG_RD;
                        break;
                    case STATUS_OBJECT_TYPE_MISMATCH:
                        switch(NtWaitForSingleObject(Event, FALSE, (PLARGE_INTEGER)&TimeWait))
                        {
                            case STATUS_SUCCESS:
                                r |= LQEVNT_FLAG_RD;
                            case STATUS_TIMEOUT:
                            case STATUS_ALERTED:
                                break;
                            default:
                                r |= LQEVNT_FLAG_ERR; break;
                        }
                        break;
                    case STATUS_PIPE_BROKEN:
                    case STATUS_PENDING:
                        break;
                    default:
                        r |= LQEVNT_FLAG_ERR;
                }
            }
            if(Fd->Flag & LQEVNT_FLAG_WR)
            {
                switch(Fd->__Reserved2.Status)
                {
                    case STATUS_MORE_PROCESSING_REQUIRED:
                    case STATUS_SUCCESS:
                        r |= LQEVNT_FLAG_WR;
                        break;
                    case STATUS_OBJECT_TYPE_MISMATCH:
                        switch(NtWaitForSingleObject(Event, FALSE, (PLARGE_INTEGER)&TimeWait))
                        {
                            case STATUS_SUCCESS:
                                r |= LQEVNT_FLAG_WR;
                            case STATUS_TIMEOUT:
                            case STATUS_ALERTED:
                                break;
                            default:
                                r |= LQEVNT_FLAG_ERR; break;
                        }
                        break;
                    case STATUS_PIPE_BROKEN:
                    case STATUS_PENDING:
                        break;
                    default:
                        r |= LQEVNT_FLAG_ERR;
                }
            }
            if((Fd->Flag & LQEVNT_FLAG_HUP) && ((Fd->__Reserved1.Status == STATUS_PIPE_BROKEN) || (Fd->__Reserved2.Status == STATUS_PIPE_BROKEN)))
                r |= LQEVNT_FLAG_HUP;
            if(r == 0)
                continue;
            Events->EventObjectIndex = i;
            return r;
        }
        Events->EventObjectIndex = 0xffffff;
    }
    MSG Msg;
    while(PeekMessage(&Msg, (HWND)Events->WinHandle, WM_USER, WM_USER + 0xffff, PM_REMOVE) == TRUE)
    {
        auto ConnIndex = Msg.message - WM_USER;
        LqConn* Conn;
        if((ConnIndex >= Events->ConnArr.AllocCount) ||
            ((Conn = LqArr2At(&Events->ConnArr, LqConn*, ConnIndex)) == nullptr) ||
           (Conn->Fd != Msg.wParam))
            continue;

        auto Event = WSAGETSELECTEVENT(Msg.lParam);
        auto Error = WSAGETSELECTERROR(Msg.lParam);
        LqEvntFlag r = 0;
        switch(Event)
        {
            case FD_READ: r = LQEVNT_FLAG_RD; break;
            case FD_WRITE: r = LQEVNT_FLAG_WR; break;
            case FD_CONNECT: r = LQEVNT_FLAG_CONNECT; break;
            case FD_ACCEPT: r = LQEVNT_FLAG_ACCEPT; break;
            case FD_CLOSE: r = (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP); break;
        }
        if(Error != 0)
            r |= LQEVNT_FLAG_ERR;
        if(r != 0)
        {
            Events->ConnIndex = ConnIndex;
            return r;
        }
    }
    return 0;
}

void LqEvntRemoveCurrent(LqEvnt* Events)
{
    if(Events->EventObjectIndex < Events->EvntFdArr.Count)
    {
        auto Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex);
		LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex) = nullptr;
        auto EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Events->EventObjectIndex);
        if((HANDLE)Fd->Fd != EventObject)
        {
            if(Fd->__Reserved1.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
            if(Fd->__Reserved2.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
            LqFileClose((int)EventObject);
        }
        Fd->Flag &= ~_LQEVNT_FLAG_SYNC;
        Events->IsRemovedEvntFd = true;
    } else
    {
        auto Conn = LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
		LqArr2RemoveAt(&Events->ConnArr, LqConn*, Events->ConnIndex, nullptr);
        Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
        WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, 0, 0);
        Events->IsRemoved = true;
    }
    Events->CommonCount--;
}

void __LqEvntRestructAfterRemoves(LqEvnt* Events)
{
    if((--Events->DeepLoop) > 0)
        return;
    if(Events->IsRemoved)
    {
        Events->IsRemoved = false;
        LqArr2AlignAfterRemove(&Events->ConnArr, LqConn*, nullptr);
    }
    if(Events->IsRemovedEvntFd)
    {
        Events->IsRemovedEvntFd = false;
        auto Fds = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, 0);
        auto EventObjects = &LqArr3At_1(&Events->EvntFdArr, HANDLE, 0);
        auto Count = Events->EvntFdArr.Count;
        for(register intptr_t i = 0; i < Count;)
        {
            if(Fds[i] == nullptr)
            {
                Fds[i] = Fds[--Count];
                EventObjects[i] = EventObjects[Count];
            } else
            {
                i++;
            }
        }
        Events->EvntFdArr.Count = Count;
        LqArr3AlignAfterRemove(&Events->EvntFdArr, HANDLE, LqEvntFd*);
    }
}

LqEvntHdr* LqEvntGetHdrByCurrent(LqEvnt* Events)
{
    if(Events->EventObjectIndex < Events->EvntFdArr.Count)
        return (LqEvntHdr*)LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex);
    return (LqEvntHdr*)LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
}

void LqEvntUnuseCurrent(LqEvnt* Events)
{
    if(Events->EventObjectIndex >= Events->EvntFdArr.Count)
    {
        auto Conn = LqArr2At(&Events->ConnArr, LqConn*, Events->ConnIndex);
        if(Conn->Flag & LQEVNT_FLAG_RD)
        {
            u_long res = -1;
            ioctlsocket(Conn->Fd, FIONREAD, &res);
            if(res > 0)
                PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_READ);
        }
        if(Conn->Flag & LQEVNT_FLAG_WR)
        {
            char t;
            if(send(Conn->Fd, &t, 0, 0) >= 0)
                PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_WRITE);
        }
        return;
    }

    auto Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EventObjectIndex);
    auto EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Events->EventObjectIndex);
    if((HANDLE)Fd->Fd != EventObject)
    {
        LARGE_INTEGER *ppl = nullptr, pl;
        NTSTATUS Stat;
        if((Fd->Flag & LQEVNT_FLAG_RD) && (Fd->__Reserved1.Status != STATUS_PENDING))
        {
            LqFileEventReset((int)EventObject);
            static char Buf;
            ppl = nullptr;
lblAgain:
            Fd->__Reserved1.Status = STATUS_PENDING;
            NTSTATUS Stat = NtReadFile((HANDLE)Fd->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved1, &Buf, 0, ppl, NULL);
            if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
            {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd->Fd);
                goto lblAgain;
            } else if(Stat != STATUS_PENDING)
            {
                Fd->__Reserved1.Status = Stat;
            }
        }
        if((Fd->Flag & LQEVNT_FLAG_WR) && (Fd->__Reserved2.Status != STATUS_PENDING))
        {
            if(!(Fd->Flag & LQEVNT_FLAG_RD))
                LqFileEventReset((int)EventObject);
            static char Buf;
            ppl = nullptr;
lblAgain2:
            NTSTATUS Stat = NtWriteFile((HANDLE)Fd->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved2, &Buf, 0, ppl, NULL);
            if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
            {
                ppl = &pl;
                pl.QuadPart = LqFileTell(Fd->Fd);
                goto lblAgain2;
            } else if(Stat != STATUS_PENDING)
            {
                Fd->__Reserved2.Status = Stat;
            }
        }
    }
}
static bool LqEvntSetMaskConn(LqEvnt* Events, size_t Index)
{
    auto Conn = LqArr2At(&Events->ConnArr, LqConn*, Index);
    auto Res = WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, Index + WM_USER, LqEvntSystemEventByConnEvents(Conn)) != SOCKET_ERROR;
    if(Conn->Flag & LQEVNT_FLAG_RD)
    {
        u_long res = -1;
        ioctlsocket(Conn->Fd, FIONREAD, &res);
        if(res > 0)
            PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_READ);
    }
    if(Conn->Flag & LQEVNT_FLAG_WR)
    {
        char t;
        if(send(Conn->Fd, &t, 0, 0) >= 0)
            PostMessage((HWND)Events->WinHandle, WM_USER + Events->ConnIndex, Conn->Fd, FD_WRITE);
    }
    Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
    return Res;
}

static bool LqEvntSetMaskEventFd(LqEvnt* Events, size_t Index)
{
    auto h = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Index);
    auto EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Index);
    if((HANDLE)h->Fd != EventObject)
    {
        bool IsCleared = false;
        LARGE_INTEGER *ppl = nullptr, pl;
        if(h->Flag & LQEVNT_FLAG_RD)
        {
            if(h->__Reserved1.Status == STATUS_NOT_SUPPORTED)
            {
                static char Buf;
                LqFileEventReset((int)EventObject);
                IsCleared = true;
                ppl = nullptr;
lblAgain:
                h->__Reserved1.Status = STATUS_PENDING;
                NTSTATUS Stat = NtReadFile((HANDLE)h->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&h->__Reserved1, &Buf, 0, ppl, NULL);
                if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
                {
                    ppl = &pl;
                    pl.QuadPart = LqFileTell(h->Fd);
                    goto lblAgain;
                } else if(Stat != STATUS_PENDING)
                {
                    h->__Reserved1.Status = Stat;
                }
            }
        } else
        {
            if((h->__Reserved1.Status != STATUS_NOT_SUPPORTED) && (h->__Reserved1.Status != STATUS_PENDING))
                NtCancelIoFile((HANDLE)h->Fd, (PIO_STATUS_BLOCK)&h->__Reserved1);
            h->__Reserved1.Status = STATUS_NOT_SUPPORTED;
        }

        if(h->Flag & LQEVNT_FLAG_WR)
        {
            if(h->__Reserved2.Status == STATUS_NOT_SUPPORTED)
            {
                static char Buf;
                if(!IsCleared)
                    LqFileEventReset((int)EventObject);
                ppl = nullptr;
lblAgain2:
                h->__Reserved2.Status = STATUS_PENDING;
                NTSTATUS Stat = NtWriteFile((HANDLE)h->Fd, EventObject, NULL, NULL, (PIO_STATUS_BLOCK)&h->__Reserved2, &Buf, 0, ppl, NULL);
                if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
                {
                    ppl = &pl;
                    pl.QuadPart = LqFileTell(h->Fd);
                    goto lblAgain2;
                } else if(Stat != STATUS_PENDING)
                {
                    h->__Reserved2.Status = Stat;
                }
            }
        } else
        {
            if((h->__Reserved2.Status != STATUS_NOT_SUPPORTED) && (h->__Reserved2.Status != STATUS_PENDING))
                NtCancelIoFile((HANDLE)h->Fd, (PIO_STATUS_BLOCK)&h->__Reserved2);
            h->__Reserved2.Status = STATUS_NOT_SUPPORTED;
        }
    }
    return true;
}

bool LqEvntSetMaskByCurrent(LqEvnt* Events)
{
    if(Events->EventObjectIndex < Events->EvntFdArr.Count)
        return LqEvntSetMaskEventFd(Events, Events->EventObjectIndex);
    return LqEvntSetMaskConn(Events, Events->ConnIndex);
}

int LqEvntUpdateAllMask(LqEvnt* Events, void* UserData, void(*DelProc)(void*, LqEvntHdr*), bool IsRestruct)
{
    Events->DeepLoop++;
    for(register auto i = &LqArr2At(&Events->ConnArr, LqConn*, 0), m = i + Events->ConnArr.AllocCount; i < m; i++)
        if((*i != nullptr) && ((*i)->Flag & _LQEVNT_FLAG_SYNC))
        {
            auto Index = ((uintptr_t)i - (uintptr_t)&LqArr2At(&Events->ConnArr, LqConn*, 0)) / sizeof(LqConn*);
            if((*i)->Flag & LQEVNT_FLAG_END)
            {
                auto Conn = *i;
				LqArr2RemoveAt(&Events->ConnArr, LqConn*, Index, nullptr);
                WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, 0, 0);
                Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
                Events->IsRemoved = true;
				Events->CommonCount--;
                DelProc(UserData, (LqEvntHdr*)Conn);
                /* Update fast indexes */
                i = &LqArr2At(&Events->ConnArr, LqConn*, Index);
                m = &LqArr2At(&Events->ConnArr, LqConn*, Events->ConnArr.AllocCount);
            } else
            {
                LqEvntSetMaskConn(Events, Index);
            }
        }
	
    for(register auto i = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, 0), m = i + Events->EvntFdArr.Count; i < m; i++)
        if((*i != nullptr) && ((*i)->Flag & _LQEVNT_FLAG_SYNC))
        {
            auto Index = ((uintptr_t)i - (uintptr_t)&LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, 0)) / sizeof(LqEvntFd*);
            if((*i)->Flag & LQEVNT_FLAG_END)
            {
                auto Fd = *i;
                *i = nullptr;
                Fd->Flag &= ~_LQEVNT_FLAG_SYNC;
                auto EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Index);
                if((HANDLE)Fd->Fd != EventObject)
                {
                    if(Fd->__Reserved1.Status == STATUS_PENDING)
                        NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
                    if(Fd->__Reserved2.Status == STATUS_PENDING)
                        NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
                    LqFileClose((int)EventObject);
                }
                Events->IsRemovedEvntFd = true;
				Events->CommonCount--;
                DelProc(UserData, (LqEvntHdr*)Fd);
                /* Update fast indexes */
                i = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Index);
                m = &LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Events->EvntFdArr.Count);
            } else
            {
                LqEvntSetMaskEventFd(Events, Index);
            }
        }
    if(IsRestruct)
        __LqEvntRestructAfterRemoves(Events);
    else
        Events->DeepLoop--;
    return 1;
}

bool __LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator)
{
    Events->DeepLoop++;
    Interator->Index = -1;
	Interator->IsEnumConn = true;
    return __LqEvntEnumNext(Events, Interator);
}

bool __LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator)
{
    register intptr_t Index = Interator->Index + 1;
	if(Interator->IsEnumConn)
	{
		for(auto m = Events->ConnArr.AllocCount; Index < m; Index++)
			if(LqArr2At(&Events->ConnArr, LqConn*, Index) != nullptr)
			{
				Interator->Index = Index;
				return true;
			}
		Index = 0;
		Interator->IsEnumConn = false;
	}
    for(auto m = Events->EvntFdArr.Count; Index < m; Index++)
        if(LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Index) != nullptr)
        {
            Interator->Index = Index;
            return true;
        }
    return false;
}

LqEvntHdr* LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
    LqEvntHdr* Ret;
    if(Interator->IsEnumConn)  
	{
        auto Conn = LqArr2At(&Events->ConnArr, LqConn*, Interator->Index);
		LqArr2RemoveAt(&Events->ConnArr, LqConn*, Interator->Index, nullptr);

        WSAAsyncSelect(Conn->Fd, (HWND)Events->WinHandle, 0, 0);
        Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
		Events->IsRemoved = true;
        Ret = (LqEvntHdr*)Conn;
    }else
    {
        auto Fd = LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Interator->Index);
        LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Interator->Index) = nullptr;

        auto EventObject = LqArr3At_1(&Events->EvntFdArr, HANDLE, Interator->Index);

        Fd->Flag &= ~_LQEVNT_FLAG_SYNC;
        if((HANDLE)Fd->Fd != EventObject)
        {
            if(Fd->__Reserved1.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
            if(Fd->__Reserved2.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
            LqFileClose((int)EventObject);
        }
        Events->IsRemovedEvntFd = true;
        Ret = (LqEvntHdr*)Fd;
    }
    Events->CommonCount--;
    return Ret;
}

LqEvntHdr* LqEvntGetHdrByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
    if(Interator->IsEnumConn)
        return (LqEvntHdr*)LqArr2At(&Events->ConnArr, LqConn*, Interator->Index);
    return (LqEvntHdr*)LqArr3At_2(&Events->EvntFdArr, LqEvntFd*, Interator->Index);
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime)
{
    if(Events->EvntFdArr.Count >= MAXIMUM_WAIT_OBJECTS)
    {
        /*On windows, when count event object greater then 64 is necessary check in parts :(*/
        intptr_t StartIndex = 0;
        intptr_t Count = MAXIMUM_WAIT_OBJECTS - 1;
        auto StartTime = LqTimeGetLocMillisec();
        for(;;)
        {
            auto Stat = MsgWaitForMultipleObjectsEx(Count, ((HANDLE*)Events->EvntFdArr.Data) + StartIndex, LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS, QS_POSTMESSAGE, 0);
            if(Stat != STATUS_TIMEOUT)
            {
                auto Index = Stat - STATUS_WAIT_0;
                if((Index >= STATUS_WAIT_0) && (Index < Count))
                {
                    Events->EventObjectIndex = Index - 1 + StartIndex;
                    return 1;
                }
				Events->EventObjectIndex = 0xffffff;
                if(Index == Count)
                    return 1;
                //Have Error
                return -1;
            }
            if((LqTimeGetLocMillisec() - StartTime) >= WaitTime)
            {
                //Is timeout
                Events->EventObjectIndex = 0xffffff;
                return 0;
            }
            StartIndex += Count;
            Count = Events->EvntFdArr.Count - StartIndex;
            if(Count > (MAXIMUM_WAIT_OBJECTS - 1))
            {
                Count = (MAXIMUM_WAIT_OBJECTS - 1);
            } else if(Count <= 0)
            {
                Count = (MAXIMUM_WAIT_OBJECTS - 1);
                StartIndex = 0;
            }
        }
    }
    auto Stat = MsgWaitForMultipleObjectsEx(Events->EvntFdArr.Count, (HANDLE*)Events->EvntFdArr.Data, WaitTime, QS_POSTMESSAGE, 0);
    if(Stat == STATUS_TIMEOUT)
    {
        Events->EventObjectIndex = 0xffffff;
        return 0;
    }
    intptr_t Index = Stat - STATUS_WAIT_0;
    if((Index >= STATUS_WAIT_0) && (Index < Events->EvntFdArr.Count))
    {
        Events->EventObjectIndex = Index - 1;
        return 1;
    }
	Events->EventObjectIndex = 0xffffff;
    if(Index == Events->EvntFdArr.Count)
        return 1;
    //When have error
    return -1;
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->CommonCount;
}

