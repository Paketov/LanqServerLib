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
    Dest->DeepLoop = 0;
    Dest->IsRemoved = 0;
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
    Events->DeepLoop++;
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
        LQ_ERR("Connection not removed from epoll\n");
    register int i = 0;
    while(Events->ClientArr[i] != c) i++;
    Events->ClientArr[i] = nullptr;
    Events->IsRemoved = 1;
}

void LqEvntRestructAfterRemoves(LqEvnt* Events)
{
    Events->DeepLoop--;
    if((Events->DeepLoop > 0) || (Events->IsRemoved == 0))
        return;
    Events->IsRemoved = 0;
    register auto AllClients = Events->ClientArr;
    for(register size_t i = 1; i < Events->Count; )
    {
        if(AllClients[i] == nullptr)
        {
            Events->Count--;
            AllClients[i] = AllClients[Events->Count];
        } else
        {
            i++;
        }
    }
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
    Events->DeepLoop++;
    Interator->Index = -1;
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
    LqEvntHdr* c = Events->ClientArr[Interator->Index];
    c->Flag &= ~_LQEVNT_FLAG_SYNC;
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, nullptr);

    auto EventArr = (epoll_event*)Events->EventArr;
    Events->ClientArr[Interator->Index] = nullptr;
    for(register int i = 0, m = Events->CountReady; i < m; i++)
        if(EventArr[i].data.ptr == c)
        {
            EventArr[i].data.ptr = nullptr;
            break;
        }
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
    return Events->CountReady = epoll_wait(Events->EpollFd, (epoll_event*)Events->EventArr, Events->EventArrCount, WaitTime);
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->Count;
}

