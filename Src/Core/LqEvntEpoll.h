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
# error "Only use in LqEvnt.cpp !"
#endif

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>

#ifndef EPOLLRDHUP
# define LQ_NOT_HAVE_EPOLLRDHUP
# define EPOLLRDHUP 0
#endif

#define LqEvntSystemEventByConnEvents(Client)				\
		((((Client)->Flag & LQCONN_FLAG_RD) ?	EPOLLIN : 0) |	\
		(((Client)->Flag & LQCONN_FLAG_WR) ?	EPOLLOUT : 0) | \
		(((Client)->Flag & LQCONN_FLAG_HUP) ?	EPOLLHUP: 0) |	\
		(((Client)->Flag & LQCONN_FLAG_RDHUP) ? EPOLLRDHUP: 0))

bool LqEvntInit(LqEvnt* Dest)
{
    Dest->IsSignalSended = false;
    Dest->AllocCount = 1;
    Dest->Count = 0;
    Dest->ClientArr = (LqConn**)malloc(sizeof(LqConn*));
    if(Dest->ClientArr == nullptr)
	return false;
    Dest->EpollFd = epoll_create(65000);
    Dest->SignalFd = eventfd(0, EFD_NONBLOCK);
    if(Dest->SignalFd == -1)
	return false;
    epoll_event ev;
    ev.data.ptr = nullptr;
    ev.events = EPOLLIN;
    if(epoll_ctl(Dest->EpollFd, EPOLL_CTL_ADD, Dest->SignalFd, &ev) == -1)
	return false;
    Dest->EventArr = malloc(sizeof(epoll_event) * LQEVNT_EPOOL_MAX_WAIT_EVENTS);
    if(Dest->EventArr == nullptr)
	return false;
    Dest->EventArrCount = LQEVNT_EPOOL_MAX_WAIT_EVENTS;
    Dest->CountReady = -1;
    Dest->EventEnumIndex = 0;
    return true;
}

void LqEvntUninit(LqEvnt* Dest)
{
    if(Dest->EventArr != nullptr)
	free(Dest->EventArr);
    if(Dest->ClientArr != nullptr)
	free(Dest->ClientArr);
    if(Dest->SignalFd != -1)
	close(Dest->SignalFd);
    if(Dest->EpollFd != -1)
	close(Dest->EpollFd);
}

bool LqEvntAddConnection(LqEvnt* Dest, LqConn* Client)
{
    if(Dest->Count >= Dest->AllocCount)
    {
	size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))Dest->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;
	auto ClientArr = (LqConn**)realloc(Dest->ClientArr, sizeof(LqConn*) * NewSize);
	if(ClientArr == nullptr)
	    return false;
	Dest->ClientArr = ClientArr;
	Dest->AllocCount = NewSize;
    }
    epoll_event ev;
    ev.data.ptr = Dest->ClientArr[Dest->Count] = Client;
    ev.events = LqEvntSystemEventByConnEvents(Client);
    Dest->Count++;
    if(!LqConnIsLock(Client))
    {
	if(epoll_ctl(Dest->EpollFd, EPOLL_CTL_ADD, Client->SockDscr, &ev) == -1)
	    return false;
    }
    return true;
}

LqConnFlag LqEvntEnumEventBegin(LqEvnt* Events)
{
    Events->EventEnumIndex = -1;//Set start index
    return LqEvntEnumEventNext(Events);
}

LqConnFlag LqEvntEnumEventNext(LqEvnt* Events)
{
    for(int i = Events->EventEnumIndex + 1, m = Events->CountReady; i < m; i++)
    {
	if(((epoll_event*)Events->EventArr)[i].data.ptr != nullptr)
	{
	    auto c = (LqConn*)((epoll_event*)Events->EventArr)[i].data.ptr;
	    auto e = ((epoll_event*)Events->EventArr)[Events->EventEnumIndex = i].events;
	    LqConnFlag r = 0;
	    if(e & EPOLLIN)
	    {
#ifdef LQ_NOT_HAVE_EPOLLRDHUP
		if((c->Flag & LQCONN_FLAG_RDHUP) && (LqConnCountPendingData(c) <= 0))
		    r |= LQCONN_FLAG_RDHUP;
#endif
		r |= LQCONN_FLAG_RD;
	    }
	    if(e & EPOLLOUT)
		r |= LQCONN_FLAG_WR;
	    if(e & EPOLLHUP)
		r |= LQCONN_FLAG_HUP;
	    if(e & EPOLLRDHUP)
		r |= LQCONN_FLAG_RDHUP;
	    return r;
	}
    }
    Events->CountReady = -1;
    return 0;
}

void LqEvntRemoveByEventInterator(LqEvnt* Events)
{
    LqConn* c = (LqConn*)((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr;
    ((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr = nullptr;
    if(epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->SockDscr, nullptr) != 0)
    {
	LQ_ERR("Connection not removed from epoll\n");
    }
    int i = 0;
    while(true)
    {
	if(Events->ClientArr[i] == c)
	    break;
	i++;
    }
    Events->Count--;
    Events->ClientArr[i] = Events->ClientArr[Events->Count];
    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
    {
	size_t NewCount = lq_max(Events->Count, 1);
	Events->ClientArr = (LqConn**)realloc(Events->ClientArr, NewCount * sizeof(LqConn*));
	Events->AllocCount = NewCount;
    }
}

LqConn* LqEvntGetClientByEventInterator(LqEvnt* Events)
{
    return (LqConn*)((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr;
}

bool LqEvntSetMaskByEventInterator(LqEvnt* Events)
{
    LqConn* c = (LqConn*)((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr;
    epoll_event ev;
    ev.events = LqEvntSystemEventByConnEvents(c);
    ev.data.ptr = c;
    return epoll_ctl(Events->EpollFd, LqConnIsLock(c) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, c->SockDscr, &ev) != -1;
}

bool LqEvntUnlock(LqEvnt* Events, LqConn* Conn)
{
    for(size_t i = 0, m = Events->Count; i < m; i++)
	if(Events->ClientArr[i] == Conn)
	{
	    epoll_event ev;
	    ev.events = LqEvntSystemEventByConnEvents(Conn);
	    ev.data.ptr = Conn;
	    Conn->Flag &= ~((LqConnFlag)LQCONN_FLAG_LOCK);
	    return epoll_ctl(Events->EpollFd, EPOLL_CTL_ADD, Conn->SockDscr, &ev) == 0;
	}
    return false;
}

bool LqEvntEnumConnBegin(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    return (Interator->Index = 0) < Events->Count;
}

bool LqEvntEnumConnNext(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    return ++Interator->Index < Events->Count;
}

void LqEvntRemoveByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator)
{
    LqConn* c = Events->ClientArr[Interator->Index];
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->SockDscr, nullptr);
    auto EventArr = (epoll_event*)Events->EventArr;
    for(int i = 0, m = Events->CountReady; i < m; i++)
	if(EventArr[i].data.ptr == c)
	{
	    EventArr[i].data.ptr = nullptr;
	}
    Events->Count--;
    Events->ClientArr[Interator->Index] = Events->ClientArr[Events->Count];
    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
    {
	size_t NewCount = lq_max(Events->Count, 1);
	Events->ClientArr = (LqConn**)realloc(Events->ClientArr, NewCount * sizeof(LqConn*));
	Events->AllocCount = NewCount;
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
	eventfd_t r[2];
	read(Events->SignalFd, &r, sizeof(r));
	return true;
    }
    return false;
}

void LqEvntSignalSet(LqEvnt* Events)
{
    Events->IsSignalSended = true;
    eventfd_t r = 1;
    write(Events->SignalFd, &r, sizeof(r));
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime)
{
    return Events->CountReady = epoll_wait(Events->EpollFd, (epoll_event*)Events->EventArr, Events->EventArrCount, WaitTime);
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->Count;
}

