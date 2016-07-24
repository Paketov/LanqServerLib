/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower
* This part of server support:
*	+Windows native events objects.
*	+linux epoll.
*	+kevent for FreeBSD like systems.(*But not yet implemented)
*	+poll for others unix systems.
*
*/

#ifndef LQ_EVNT
#error "Only use in LqEvnt.cpp !"
#endif


#define	LQCONN_FLAG_RD_AGAIN _LQCONN_FLAG_RESERVED_1
#define	LQCONN_FLAG_WR_AGAIN _LQCONN_FLAG_RESERVED_2


#define LqEvntSystemEventByConnEvents(Client)							\
	((Client->Flag & LQCONN_FLAG_RD)			? (FD_ACCEPT | FD_READ) : 0) |	\
	((Client->Flag & LQCONN_FLAG_WR)			? (FD_WRITE | FD_CONNECT) : 0) |\
	((Client->Flag & (LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP)) ? FD_CLOSE: 0)


bool LqEvntInit(LqEvnt* Dest)
{
    Dest->EventArr = nullptr;
    Dest->ClientArr = nullptr;
    Dest->EventArr = (HANDLE*)malloc(sizeof(HANDLE));
    if(Dest->EventArr == nullptr)
	return false;
    Dest->ClientArr = (LqConn**)malloc(sizeof(LqConn*));
    if(Dest->ClientArr == nullptr)
	return false;

    Dest->AllocCount = Dest->Count = 1;
    Dest->EventArr[0] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Dest->ClientArr[0] = nullptr;
    Dest->IsSignalSended = false;
    Dest->EventEnumIndex = 0;
    return true;
}

void LqEvntUninit(LqEvnt* Dest)
{
    if(Dest->EventArr != nullptr)
    {
	for(size_t i = 0, m = Dest->Count; i < m; i++)
	    CloseHandle(Dest->EventArr[i]);
	free(Dest->EventArr);
    }
    if(Dest->ClientArr != nullptr)
	free(Dest->ClientArr);
}

bool LqEvntAddConnection(LqEvnt* Dest, LqConn* Client)
{
    HANDLE Event = WSACreateEvent();
    if(Event == WSA_INVALID_EVENT)
	return false;
    if(WSAEventSelect(Client->SockDscr, Event, LqConnIsLock(Client) ? 0 : LqEvntSystemEventByConnEvents(Client)) == SOCKET_ERROR)
	goto lblErrOut;

    if((Client->Flag & (LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN)) && !LqConnIsLock(Client))
	SetEvent(Event);

    if(Dest->Count >= Dest->AllocCount)
    {
	size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))Dest->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;
	auto NewEventArr = (HANDLE*)realloc(Dest->EventArr, sizeof(HANDLE) * NewSize);
	auto ClientArr = (LqConn**)realloc(Dest->ClientArr, sizeof(LqConn*) * NewSize);
	if(NewEventArr == nullptr)
	    goto lblErrOut;
	Dest->EventArr = NewEventArr;
	if(ClientArr == nullptr)
	    goto lblErrOut;
	Dest->ClientArr = ClientArr;
	Dest->AllocCount = NewSize;
    }
    Dest->EventArr[Dest->Count] = Event;
    Dest->ClientArr[Dest->Count] = Client;
    Dest->Count++;
    return true;
lblErrOut:
    WSACloseEvent(Event);
    return false;
}

LqConnFlag LqEvntEnumEventBegin(LqEvnt* Events)
{
    Events->EventEnumIndex = 0; //Set start index
    return LqEvntEnumEventNext(Events);
}

LqConnFlag LqEvntEnumEventNext(LqEvnt* Events)
{
    WSANETWORKEVENTS e;
    for(int i = Events->EventEnumIndex + 1, m = Events->Count; i < m; i++)
    {
	LqConn* c = Events->ClientArr[i];
	WSAEnumNetworkEvents(c->SockDscr, Events->EventArr[i], &e);
	if((e.lNetworkEvents != 0) || (!LqConnIsLock(c) && (c->Flag & (LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN))))
	{
	    Events->EventEnumIndex = i;
	    LqConnFlag r = 0;
	    if((e.lNetworkEvents & (FD_ACCEPT | FD_READ)) || (c->Flag & LQCONN_FLAG_RD_AGAIN))
		r |= LQCONN_FLAG_RD;
	    if((e.lNetworkEvents & (FD_WRITE | FD_CONNECT)) || (c->Flag & LQCONN_FLAG_WR_AGAIN))
		r |= LQCONN_FLAG_WR;
	    if(e.lNetworkEvents & FD_CLOSE)
		r |= (c->Flag & (LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP));
	    c->Flag &= ~(LQCONN_FLAG_WR_AGAIN | LQCONN_FLAG_RD_AGAIN);
	    return r;
	}
    }
    return 0;
}

void LqEvntRemoveByEventInterator(LqEvnt* Events)
{
    WSACloseEvent(Events->EventArr[Events->EventEnumIndex]);
    Events->Count--;
    Events->EventArr[Events->EventEnumIndex] = Events->EventArr[Events->Count];
    Events->ClientArr[Events->EventEnumIndex] = Events->ClientArr[Events->Count];
    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
    {
	size_t NewCount = lq_max(Events->Count, 1);
	Events->EventArr = (HANDLE*)realloc(Events->EventArr, NewCount * sizeof(HANDLE));
	Events->ClientArr = (LqConn**)realloc(Events->ClientArr, NewCount * sizeof(LqConn*));
	Events->AllocCount = NewCount;
    }
    Events->EventEnumIndex--;
}

LqConn* LqEvntGetClientByEventInterator(LqEvnt* Events)
{
    return Events->ClientArr[Events->EventEnumIndex];
}

void LqEvntUnuseClientByEventInterator(LqEvnt* Events)
{
    LqConn* c = Events->ClientArr[Events->EventEnumIndex];
    if(c->Flag & LQCONN_FLAG_RD)
    {
	if(LqConnCountPendingData(c) > 0)
	    c->Flag |= LQCONN_FLAG_RD_AGAIN;
    }
    if(c->Flag & LQCONN_FLAG_WR)
    {
	char t;
	if(send(c->SockDscr, &t, 0, 0) >= 0)
	    c->Flag |= LQCONN_FLAG_WR_AGAIN;
    }
    if(!LqConnIsLock(c) && (c->Flag & (LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN)))
	SetEvent(Events->EventArr[Events->EventEnumIndex]);
}

bool LqEvntSetMaskByEventInterator(LqEvnt* Events)
{
    auto c = Events->ClientArr[Events->EventEnumIndex];
    return WSAEventSelect(c->SockDscr, Events->EventArr[Events->EventEnumIndex], LqConnIsLock(c) ? 0 : LqEvntSystemEventByConnEvents(c)) != SOCKET_ERROR;
}

bool LqEvntUnlock(LqEvnt* Events, LqConn* Conn)
{
    for(size_t i = 1, m = Events->Count; i < m; i++)
	if(Events->ClientArr[i] == Conn)
	{
	    auto c = Events->ClientArr[i];
	    if(WSAEventSelect(c->SockDscr, Events->EventArr[i], LqEvntSystemEventByConnEvents(c)) != SOCKET_ERROR)
	    {
		c->Flag &= ~LQCONN_FLAG_LOCK;
		if(c->Flag & (LQCONN_FLAG_RD_AGAIN | LQCONN_FLAG_WR_AGAIN))
		    SetEvent(c);
		return true;
	    }
	    return false;
	}
    return false;
}

bool LqEvntEnumConnBegin(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    return (Interator->Index = 1) < Events->Count;
}

bool LqEvntEnumConnNext(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    return ++Interator->Index < Events->Count;
}

void LqEvntRemoveByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    CloseHandle(Events->EventArr[Interator->Index]);
    Events->Count--;
    Events->EventArr[Interator->Index] = Events->EventArr[Events->Count];
    Events->ClientArr[Interator->Index] = Events->ClientArr[Events->Count];
    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
    {
	Events->EventArr = (HANDLE*)realloc(Events->EventArr, Events->Count * sizeof(HANDLE));
	Events->ClientArr = (LqConn**)realloc(Events->ClientArr, Events->Count * sizeof(LqConn*));
	Events->AllocCount = Events->Count;
    }
    Interator->Index--;
}

LqConn* LqEvntGetClientByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    return Events->ClientArr[Interator->Index];
}

bool LqEvntSignalCheckAndReset(LqEvnt* Events)
{
    if(Events->IsSignalSended)
    {
	Events->IsSignalSended = false;
	ResetEvent(Events->EventArr[0]);
	return true;
    }
    return false;
}

void LqEvntSignalSet(LqEvnt* Events)
{
    Events->IsSignalSended = true;
    SetEvent(Events->EventArr[0]);
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime)
{
    switch(WSAWaitForMultipleEvents(Events->Count, Events->EventArr, FALSE, (DWORD)WaitTime, FALSE))
    {
	case WSA_WAIT_FAILED:
	    return -1;
	case WAIT_TIMEOUT:
	    return  0;
    }
    return 1;
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->Count - 1;
}

