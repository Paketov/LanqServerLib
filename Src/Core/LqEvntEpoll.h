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
# error "Only use in LqEvnt.cpp !"
#endif

#include <sys/epoll.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>
#include "LqFile.h"

#ifndef EPOLLRDHUP
# define LQ_NOT_HAVE_EPOLLRDHUP
# define EPOLLRDHUP 0
#endif

#define LqEvntSystemEventByConnEvents(Client)               \
        ((((Client)->Flag & LQEVNT_FLAG_RD) ?   EPOLLIN : 0) |  \
        (((Client)->Flag & LQEVNT_FLAG_WR) ?    EPOLLOUT : 0) | \
        (((Client)->Flag & LQEVNT_FLAG_HUP) ?   EPOLLHUP: 0) |  \
        (((Client)->Flag & LQEVNT_FLAG_RDHUP) ? EPOLLRDHUP: 0))

#define LqConnIsLock(c) ((c->Flag & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) == 0)

bool LqEvntInit(LqEvnt* Dest)
{
    Dest->AllocCount = 1;
    Dest->Count = 0;
    Dest->ClientArr = (LqEvntHdr**)malloc(sizeof(LqEvntHdr*));
    if(Dest->ClientArr == nullptr)
        return false;
    Dest->EpollFd = epoll_create(65000);

	LqFileDescrSetInherit(Dest->EpollFd, 0);
    Dest->SignalFd = LqFileEventCreate(LQ_O_NOINHERIT);
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
    Dest->FirstEnd = -1;
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

bool LqEvntAddHdr(LqEvnt* Dest, LqEvntHdr* Client)
{
    if(Dest->Count >= Dest->AllocCount)
    {
        size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))Dest->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;
        auto ClientArr = (LqEvntHdr**)realloc(Dest->ClientArr, sizeof(LqEvntHdr*) * NewSize);
        if(ClientArr == nullptr)
            return false;
        Dest->ClientArr = ClientArr;
        Dest->AllocCount = NewSize;
    }
    epoll_event ev;
    ev.data.ptr = Dest->ClientArr[Dest->Count] = Client;
    ev.events = LqEvntSystemEventByConnEvents(Client);
    Dest->Count++;
    auto r = true;
    if(!LqConnIsLock(Client))
    {
        if(epoll_ctl(Dest->EpollFd, EPOLL_CTL_ADD, Client->Fd, &ev) == -1)
            r = false;
    }
    Client->Flag &= ~_LQEVNT_FLAG_SYNC;
    return r;
}

LqEvntFlag LqEvntEnumEventBegin(LqEvnt* Events)
{
    Events->EventEnumIndex = -1;//Set start index
    return LqEvntEnumEventNext(Events);
}

LqEvntFlag LqEvntEnumEventNext(LqEvnt* Events)
{
    for(int i = Events->EventEnumIndex + 1, m = Events->CountReady; i < m; i++)
    {
        if(((epoll_event*)Events->EventArr)[i].data.ptr != nullptr)
        {
            auto c = (LqEvntHdr*)((epoll_event*)Events->EventArr)[i].data.ptr;
            auto e = ((epoll_event*)Events->EventArr)[Events->EventEnumIndex = i].events;
            LqEvntFlag r = 0;
            if(e & EPOLLIN)
            {
#ifdef LQ_NOT_HAVE_EPOLLRDHUP
                int res = -1;
                if(
                    (c->Flag & LQEVNT_FLAG_RDHUP) &&
                    (ioctl(c->Fd, FIONREAD, &res) >= 0) &&
                    (res <= 0)
                    )
                    r |= LQEVNT_FLAG_RDHUP;
#endif
                r |= LQEVNT_FLAG_RD;
            }
            if(e & EPOLLOUT)
                r |= LQEVNT_FLAG_WR;
            if(e & EPOLLHUP)
                r |= LQEVNT_FLAG_HUP;
            if(e & EPOLLRDHUP)
                r |= LQEVNT_FLAG_RDHUP;
            if(e & EPOLLERR)
                r |= LQEVNT_FLAG_ERR;
            if(c->Flag & LQEVNT_FLAG_END)
                r |= LQEVNT_FLAG_END;
            return r;
        }
    }
    Events->CountReady = -1;
    return 0;
}

void LqEvntRemoveCurrent(LqEvnt* Events)
{
    LqEvntHdr* c = (LqEvntHdr*)((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr;
    c->Flag &= ~_LQEVNT_FLAG_SYNC;
    ((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr = nullptr;
    if(epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, nullptr) != 0)
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
        Events->ClientArr = (LqEvntHdr**)realloc(Events->ClientArr, NewCount * sizeof(LqEvntHdr*));
        Events->AllocCount = NewCount;
    }
}

LqEvntHdr* LqEvntGetHdrByCurrent(LqEvnt* Events)
{
    return (LqEvntHdr*)((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr;
}

bool LqEvntSetMaskByCurrent(LqEvnt* Events)
{
    LqEvntHdr* c = (LqEvntHdr*)((epoll_event*)Events->EventArr)[Events->EventEnumIndex].data.ptr;
    if(c->Flag & LQEVNT_FLAG_END)
    {
        if((Events->FirstEnd == -1) || (Events->FirstEnd > Events->EventEnumIndex))
            Events->FirstEnd = Events->EventEnumIndex;
    }
    epoll_event ev;
    ev.events = LqEvntSystemEventByConnEvents(c);
    ev.data.ptr = c;
    auto r = epoll_ctl(Events->EpollFd, LqConnIsLock(c) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, c->Fd, &ev) != -1;
    c->Flag &= ~_LQEVNT_FLAG_SYNC;
    return r;
}

bool LqEvntSetMaskByHdr(LqEvnt* Events, LqEvntHdr* Conn)
{
    for(size_t i = 0, m = Events->Count; i < m; i++)
        if(Events->ClientArr[i] == Conn)
        {
            if(Conn->Flag & LQEVNT_FLAG_END)
            {
                if((Events->FirstEnd == -1) || (Events->FirstEnd > i))
                    Events->FirstEnd = i;
            }
            epoll_event ev;
            ev.events = LqEvntSystemEventByConnEvents(Conn);
            ev.data.ptr = Conn;
            auto r = epoll_ctl(Events->EpollFd, LqConnIsLock(Conn) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, Conn->Fd, &ev) != -1;
            Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
            return r;
        }
    return false;
}

bool LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator)
{
    return (Interator->Index = 0) < Events->Count;
}

bool LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator)
{
    return ++Interator->Index < Events->Count;
}

void LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
    LqEvntHdr* c = Events->ClientArr[Interator->Index];
    c->Flag &= ~_LQEVNT_FLAG_SYNC;
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, nullptr);
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
        Events->ClientArr = (LqEvntHdr**)realloc(Events->ClientArr, NewCount * sizeof(LqEvntHdr*));
        Events->AllocCount = NewCount;
    }
    Interator->Index--;
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
    if(Events->FirstEnd != -1)
    {
        int j = 0;
        auto ea = (epoll_event*)Events->EventArr;
        int i = Events->FirstEnd;
        Events->FirstEnd = -1;
        for(; i < Events->Count; i++)
        {
            if(Events->ClientArr[i]->Flag & LQEVNT_FLAG_END)
            {
                if(j >= Events->EventArrCount)
                {
                    Events->FirstEnd = i;
                    break;
                }
                ea[j].data.ptr = Events->ClientArr[i];
                ea[j].events = 0;
                j++;
            }
        }
        if(j > 0)
            return Events->CountReady = j;
    }
    return Events->CountReady = epoll_wait(Events->EpollFd, (epoll_event*)Events->EventArr, Events->EventArrCount, WaitTime);
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->Count;
}

