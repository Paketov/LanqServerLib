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

#define LQCONN_FLAG_RD_AGAIN _LQEVNT_FLAG_RESERVED_1
#define LQCONN_FLAG_WR_AGAIN _LQEVNT_FLAG_RESERVED_2


#define LqEvntSystemEventByConnEvents(Client)                           \
    (((Client->Flag & LQEVNT_FLAG_RD)            ? (FD_ACCEPT | FD_READ) : 0)  | \
    ((Client->Flag & LQEVNT_FLAG_WR)            ? (FD_WRITE | FD_CONNECT) : 0) |\
    ((Client->Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) ? FD_CLOSE: 0))

#define IsRdAgain(Client)  ((Client->Flag & (LQCONN_FLAG_RD_AGAIN | LQEVNT_FLAG_RD)) == (LQCONN_FLAG_RD_AGAIN | LQEVNT_FLAG_RD))
#define IsWrAgain(Client)  ((Client->Flag & (LQCONN_FLAG_WR_AGAIN | LQEVNT_FLAG_WR)) == (LQCONN_FLAG_WR_AGAIN | LQEVNT_FLAG_WR))
#define IsAgain(Client)    (IsRdAgain(Client) || IsWrAgain(Client))

bool LqEvntInit(LqEvnt* Dest)
{
    Dest->EventArr = nullptr;
    Dest->ClientArr = nullptr;
    Dest->EventArr = (HANDLE*)___malloc(sizeof(HANDLE));
    if(Dest->EventArr == nullptr)
        return false;
    Dest->ClientArr = (LqEvntHdr**)___malloc(sizeof(LqEvntHdr*));
    if(Dest->ClientArr == nullptr)
        return false;

    Dest->AllocCount = Dest->Count = 1;
	Dest->SignalFd = LqFileEventCreate(LQ_O_NOINHERIT);
    if(Dest->SignalFd == NULL)
    {
		___free(Dest->EventArr);
        Dest->EventArr = nullptr;
		___free(Dest->ClientArr);
        Dest->ClientArr = nullptr;
        return false;
    }
	
    Dest->EventArr[0] = (HANDLE)Dest->SignalFd;
    Dest->ClientArr[0] = nullptr;
    Dest->EventEnumIndex = 0;
	Dest->DeepLoop = 0;
	Dest->IsRemoved = 0;
    return true;
}

void LqEvntUninit(LqEvnt* Dest)
{
    if(Dest->EventArr != nullptr)
    {
        LqFileClose((int)Dest->EventArr[0]);
        for(size_t i = 1, m = Dest->Count; i < m; i++)
        {
            if(Dest->EventArr[i] != (HANDLE)Dest->ClientArr[i]->Fd)
                LqFileClose((int)Dest->EventArr[i]); //close only events
        }
		___free(Dest->EventArr);
    }
    if(Dest->ClientArr != nullptr)
		___free(Dest->ClientArr);
}

/*
* Adding to follow all type of WinNT objects
*/
bool LqEvntAddHdr(LqEvnt* Dest, LqEvntHdr* Client)
{
    int Event = -1;
    if(LqEvntIsConn(Client))
    {
        if((Event = (int)WSACreateEvent()) == 0)
            return false;
        if(WSAEventSelect(Client->Fd, (HANDLE)Event, LqEvntSystemEventByConnEvents(Client)) == SOCKET_ERROR)
            goto lblErrOut;
        if(IsAgain(Client))
            SetEvent(Client);

    } else
    {
        LARGE_INTEGER *ppl = nullptr, pl;
        auto EvntData = (LqEvntFd*)Client;
        Event = LqFileEventCreate(LQ_O_NOINHERIT);
        if(Client->Flag & LQEVNT_FLAG_RD)
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
        if(Client->Flag & LQEVNT_FLAG_WR)
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
    }

    Client->Flag &= ~_LQEVNT_FLAG_SYNC;
    if(Dest->Count >= Dest->AllocCount)
    {
        size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))Dest->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;
        auto NewEventArr = (HANDLE*)___realloc(Dest->EventArr, sizeof(HANDLE) * NewSize);
        if(NewEventArr == nullptr)
            goto lblErrOut;
        Dest->EventArr = NewEventArr;
        auto ClientArr = (LqEvntHdr**)___realloc(Dest->ClientArr, sizeof(LqEvntHdr*) * NewSize);
        if(ClientArr == nullptr)
            goto lblErrOut;
        Dest->ClientArr = ClientArr;
        Dest->AllocCount = NewSize;
    }
    Dest->EventArr[Dest->Count] = (HANDLE)Event;
    Dest->ClientArr[Dest->Count] = Client;
    Dest->Count++;
    return true;
lblErrOut:
    if(Event != Client->Fd)
        LqFileClose(Event);
    return false;
}

LqEvntFlag LqEvntEnumEventBegin(LqEvnt* Events)
{
    Events->EventEnumIndex = 0;
	Events->DeepLoop++;
    return LqEvntEnumEventNext(Events);
}

LqEvntFlag LqEvntEnumEventNext(LqEvnt* Events)
{
    WSANETWORKEVENTS e = {0};
    for(int i = Events->EventEnumIndex + 1, m = Events->Count; i < m; i++)
    {
        auto h = Events->ClientArr[i];
		if(h == nullptr)
			continue;
        if(LqEvntIsConn(h))
        {
            //Check socket connection
            WSAEnumNetworkEvents(h->Fd, Events->EventArr[i], &e);
            if((e.lNetworkEvents != 0) || IsAgain(h) || (h->Flag & LQEVNT_FLAG_END))
            {
                Events->EventEnumIndex = i;
                LqEvntFlag r = 0;
                if((e.lNetworkEvents & (FD_ACCEPT | FD_READ)) || IsRdAgain(h))
                {
                    if(((e.lNetworkEvents & FD_ACCEPT) && (e.iErrorCode[FD_ACCEPT_BIT] != 0)) ||
                        ((e.lNetworkEvents & FD_READ) && (e.iErrorCode[FD_READ_BIT] != 0)))
                        r |= LQEVNT_FLAG_ERR;
                    r |= LQEVNT_FLAG_RD;
                }
                if((e.lNetworkEvents & (FD_WRITE | FD_CONNECT)) || IsWrAgain(h))
                {
                    if(((e.lNetworkEvents & FD_WRITE) && (e.iErrorCode[FD_WRITE_BIT] != 0)) ||
                        ((e.lNetworkEvents & FD_CONNECT) && (e.iErrorCode[FD_CONNECT_BIT] != 0)))
                        r |= LQEVNT_FLAG_ERR;
                    r |= LQEVNT_FLAG_WR;
                }
                if(e.lNetworkEvents & FD_CLOSE)
                {
                    if(e.iErrorCode[FD_CLOSE_BIT] != 0)
                        r |= LQEVNT_FLAG_ERR;
                    r |= (h->Flag & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP));
                }
                if(h->Flag & LQEVNT_FLAG_END)
                    r |= LQEVNT_FLAG_END;

                h->Flag &= ~(LQCONN_FLAG_WR_AGAIN | LQCONN_FLAG_RD_AGAIN);
                return r;
            }
        } else
        {
            auto Fd = (LqEvntFd*)h;
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
                        switch(NtWaitForSingleObject((HANDLE)Events->EventArr[i], FALSE, (PLARGE_INTEGER)&TimeWait))
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
                        switch(NtWaitForSingleObject((HANDLE)Events->EventArr[i], FALSE, (PLARGE_INTEGER)&TimeWait))
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
            if(h->Flag & LQEVNT_FLAG_END)
                r |= LQEVNT_FLAG_END;
            if(r == 0)
                continue;
            Events->EventEnumIndex = i;
            return r;
        }
    }
    return 0;
}

void LqEvntRemoveCurrent(LqEvnt* Events)
{
	Events->ClientArr[Events->EventEnumIndex]->Flag &= ~_LQEVNT_FLAG_SYNC;
	if(LqEvntIsConn(Events->ClientArr[Events->EventEnumIndex]))
	{
		LqFileClose((int)Events->EventArr[Events->EventEnumIndex]);
	} else
	{
		auto Fd = (LqEvntFd*)Events->ClientArr[Events->EventEnumIndex];
		if((HANDLE)Fd->Fd != Events->EventArr[Events->EventEnumIndex])
		{
			if(Fd->__Reserved1.Status == STATUS_PENDING)
				NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
			if(Fd->__Reserved2.Status == STATUS_PENDING)
				NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
			LqFileClose((int)Events->EventArr[Events->EventEnumIndex]);
		}
	}
	Events->ClientArr[Events->EventEnumIndex] = nullptr;
	Events->IsRemoved = 1;
}

void LqEvntRestructAfterRemoves(LqEvnt* Events)
{
    Events->DeepLoop--;
    if((Events->DeepLoop > 0) || (Events->IsRemoved == 0))
        return;
    Events->IsRemoved = 0;

    register auto AllClients = Events->ClientArr;
    register auto AllEvents = Events->EventArr;
    for(register size_t i = 1; i < Events->Count; )
    {
        if(AllClients[i] == nullptr)
        {
            Events->Count--;
            AllClients[i] = AllClients[Events->Count];
            AllEvents[i] = AllEvents[Events->Count];
        } else
        {
            i++;
        }
    }

    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
    {
        size_t NewCount = lq_max(Events->Count, 1);
        Events->EventArr = (HANDLE*)___realloc(Events->EventArr, NewCount * sizeof(HANDLE));
        Events->ClientArr = (LqEvntHdr**)___realloc(Events->ClientArr, NewCount * sizeof(LqEvntHdr*));
        Events->AllocCount = NewCount;
    }
}

LqEvntHdr* LqEvntGetHdrByCurrent(LqEvnt* Events)
{
    return Events->ClientArr[Events->EventEnumIndex];
}

void LqEvntUnuseCurrent(LqEvnt* Events)
{
    auto c = Events->ClientArr[Events->EventEnumIndex];
    if(LqEvntIsConn(c))
    {
        if(c->Flag & LQEVNT_FLAG_RD)
        {
            u_long res = -1;
            ioctlsocket(c->Fd, FIONREAD, &res);
            if(res > 0)
                c->Flag |= LQCONN_FLAG_RD_AGAIN;
        }
        if(c->Flag & LQEVNT_FLAG_WR)
        {
            char t;
            if(send(c->Fd, &t, 0, 0) >= 0)
                c->Flag |= LQCONN_FLAG_WR_AGAIN;
        }
        if(c->Flag & (LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN))
            LqFileEventSet((int)Events->EventArr[Events->EventEnumIndex]);
    } else
    {
        if((HANDLE)c->Fd != Events->EventArr[Events->EventEnumIndex])
        {
            auto Fd = (LqEvntFd*)c;
            auto Event = Events->EventArr[Events->EventEnumIndex];
            LARGE_INTEGER *ppl = nullptr, pl;
            NTSTATUS Stat;
            if((Fd->Flag & LQEVNT_FLAG_RD) && (Fd->__Reserved1.Status != STATUS_PENDING))
            {
                LqFileEventReset((int)Event);
                static char Buf;
                ppl = nullptr;
lblAgain:
                Fd->__Reserved1.Status = STATUS_PENDING;
                NTSTATUS Stat = NtReadFile((HANDLE)c->Fd, Event, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved1, &Buf, 0, ppl, NULL);
                if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
                {
                    ppl = &pl;
                    pl.QuadPart = LqFileTell(c->Fd);
                    goto lblAgain;
                } else if(Stat != STATUS_PENDING)
                {
                    Fd->__Reserved1.Status = Stat;
                }
            }
            if((Fd->Flag & LQEVNT_FLAG_WR) && (Fd->__Reserved2.Status != STATUS_PENDING))
            {
                if(!(Fd->Flag & LQEVNT_FLAG_RD))
                    LqFileEventReset((int)Event);
                static char Buf;
                ppl = nullptr;
lblAgain2:
                NTSTATUS Stat = NtWriteFile((HANDLE)c->Fd, Event, NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved2, &Buf, 0, ppl, NULL);
                if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
                {
                    ppl = &pl;
                    pl.QuadPart = LqFileTell(c->Fd);
                    goto lblAgain2;
                } else if(Stat != STATUS_PENDING)
                {
                    Fd->__Reserved2.Status = Stat;
                }
            }
        }
    }

}

static bool LqEvntSetMask(LqEvnt* Events, size_t Index)
{
    auto h = Events->ClientArr[Index];
    if(LqEvntIsConn(h))
    {
        if(h->Flag & LQEVNT_FLAG_END)
           LqFileEventSet((int)Events->EventArr[0]);
        auto r = WSAEventSelect(h->Fd, Events->EventArr[Index], LqEvntSystemEventByConnEvents(h)) != SOCKET_ERROR;
        h->Flag &= ~(LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN);
        if(h->Flag & LQEVNT_FLAG_RD)
        {
            u_long res = -1;
            ioctlsocket(h->Fd, FIONREAD, &res);
            if(res > 0)
                h->Flag |= LQCONN_FLAG_RD_AGAIN;
        }
        if(h->Flag & LQEVNT_FLAG_WR)
        {
            char t;
            if(send(h->Fd, &t, 0, 0) >= 0)
                h->Flag |= LQCONN_FLAG_WR_AGAIN;
        }
        if(h->Flag & (LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN))
            LqFileEventSet((int)Events->EventArr[Index]);
        h->Flag &= ~_LQEVNT_FLAG_SYNC;
        return r;
    } else
    {
        if((HANDLE)h->Fd != Events->EventArr[Index])
        {
            bool IsCleared = false;
            auto Fd = (LqEvntFd*)h;
            LARGE_INTEGER *ppl = nullptr, pl;
            if(h->Flag & LQEVNT_FLAG_RD)
            {
                if(Fd->__Reserved1.Status == STATUS_NOT_SUPPORTED)
                {
                    static char Buf;
                    LqFileEventReset((int)Events->EventArr[Index]);
                    IsCleared = true;
                    ppl = nullptr;
lblAgain:
                    Fd->__Reserved1.Status = STATUS_PENDING;
                    NTSTATUS Stat = NtReadFile((HANDLE)h->Fd, Events->EventArr[Index], NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved1, &Buf, 0, ppl, NULL);
                    if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
                    {
                        ppl = &pl;
                        pl.QuadPart = LqFileTell(h->Fd);
                        goto lblAgain;
                    } else if(Stat != STATUS_PENDING)
                    {
                        Fd->__Reserved1.Status = Stat;
                    }
                }
            } else
            {
                if((Fd->__Reserved1.Status != STATUS_NOT_SUPPORTED) && (Fd->__Reserved1.Status != STATUS_PENDING))
                    NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
                Fd->__Reserved1.Status = STATUS_NOT_SUPPORTED;
            }

            if(h->Flag & LQEVNT_FLAG_WR)
            {
                if(Fd->__Reserved2.Status == STATUS_NOT_SUPPORTED)
                {
                    static char Buf;
                    if(!IsCleared)
                        LqFileEventReset((int)Events->EventArr[Index]);
                    ppl = nullptr;
lblAgain2:
                    Fd->__Reserved2.Status = STATUS_PENDING;
                    NTSTATUS Stat = NtWriteFile((HANDLE)h->Fd, Events->EventArr[Index], NULL, NULL, (PIO_STATUS_BLOCK)&Fd->__Reserved2, &Buf, 0, ppl, NULL);
                    if((Stat == STATUS_INVALID_PARAMETER) && (ppl == nullptr))
                    {
                        ppl = &pl;
                        pl.QuadPart = LqFileTell(h->Fd);
                        goto lblAgain2;
                    } else if(Stat != STATUS_PENDING)
                    {
                        Fd->__Reserved2.Status = Stat;
                    }
                }
            } else
            {
                if((Fd->__Reserved2.Status != STATUS_NOT_SUPPORTED) && (Fd->__Reserved2.Status != STATUS_PENDING))
                    NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
                Fd->__Reserved2.Status = STATUS_NOT_SUPPORTED;
            }
        }

        if(h->Flag & LQEVNT_FLAG_END)
            LqFileEventSet((int)Events->EventArr[0]);
    }
    return true;
}

bool LqEvntSetMaskByCurrent(LqEvnt* Events)
{
    return LqEvntSetMask(Events, Events->EventEnumIndex);
}

bool LqEvntSetMaskByHdr(LqEvnt* Events, LqEvntHdr* Hdr)
{
    for(register size_t i = 1, m = Events->Count; i < m; i++)
        if(Events->ClientArr[i] == Hdr)
        {
            return LqEvntSetMask(Events, i);
        }
    return false;
}

bool LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator)
{
	Events->DeepLoop++;
	Interator->Index = 0;
    return LqEvntEnumNext(Events, Interator);
}

bool LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator)
{
	for(register int Index = Interator->Index + 1; Index < Events->Count; Index++)
	{
		if(Events->ClientArr[Index] != nullptr)
		{
			Interator->Index = Index;
			return true;
		}
	}
	return false;
}

void LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
    Events->ClientArr[Interator->Index]->Flag &= ~_LQEVNT_FLAG_SYNC;
    if(LqEvntIsConn(Events->ClientArr[Interator->Index]))
    {
        LqFileClose((int)Events->EventArr[Interator->Index]);
    } else
    {
        auto Fd = (LqEvntFd*)Events->ClientArr[Interator->Index];
        if((HANDLE)Fd->Fd != Events->EventArr[Interator->Index])
        {
            if(Fd->__Reserved1.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved1);
            if(Fd->__Reserved2.Status == STATUS_PENDING)
                NtCancelIoFile((HANDLE)Fd->Fd, (PIO_STATUS_BLOCK)&Fd->__Reserved2);
            LqFileClose((int)Events->EventArr[Interator->Index]);
        }
    }
	Events->ClientArr[Interator->Index] = nullptr;
	Events->IsRemoved = 1;
}

LqEvntHdr* LqEvntGetHdrByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
    return Events->ClientArr[Interator->Index];
}

bool LqEvntSignalCheckAndReset(LqEvnt* Events)
{
    return LqFileEventReset(Events->SignalFd) > 0;
}

void LqEvntSignalSet(LqEvnt* Events)
{
    LqFileEventSet(Events->SignalFd);
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime)
{
    LARGE_INTEGER WaitTimeOut;
    WaitTimeOut.QuadPart = -WaitTime;
    switch(NtWaitForMultipleObjects(Events->Count, Events->EventArr, WaitAny, FALSE, &WaitTimeOut))
    {
        case STATUS_SUCCESS:
            return 1;
        case STATUS_TIMEOUT:
            return  0;
    }
    return -1;
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->Count - 1;
}

