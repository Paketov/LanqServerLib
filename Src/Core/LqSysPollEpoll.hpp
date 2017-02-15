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
        (((LqEvntGetFlags(Client) & LQEVNT_FLAG_RD) ?   EPOLLIN : 0) |  \
        ((LqEvntGetFlags(Client) & LQEVNT_FLAG_WR) ?    EPOLLOUT : 0) | \
        ((LqEvntGetFlags(Client) & LQEVNT_FLAG_HUP) ?   EPOLLHUP: 0) |  \
        ((LqEvntGetFlags(Client) & LQEVNT_FLAG_RDHUP) ? EPOLLRDHUP: 0))

#define LqConnIsLock(c) ((LqEvntGetFlags(c) & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) == 0)

bool LqSysPollInit(LqSysPoll* Dest) {
    Dest->EpollFd = epoll_create(65000);
    if(Dest->EpollFd == -1)
        return false;
    Dest->CountReady = -1;
    Dest->EventEnumIndex = 0;
    Dest->DeepLoop = 0;
    Dest->CommonCount = 0;
    LqArrInit(&Dest->ClientArr);
    LqArrInit(&Dest->EventArr);
    LqArrResize(&Dest->EventArr, epoll_event, LQEVNT_EPOOL_MAX_WAIT_EVENTS);
    return true;
}

void LqSysPollUninit(LqSysPoll* Dest) {
    LqArrUninit(&Dest->ClientArr);
    LqArrUninit(&Dest->EventArr);
    if(Dest->EpollFd != -1)
        close(Dest->EpollFd);
}

bool LqSysPollAddHdr(LqSysPoll* Dest, LqEvntHdr* Client) {
    LqArrPushBack(&Dest->ClientArr, LqEvntHdr*);
    LqArrBack(&Dest->ClientArr, LqEvntHdr*) = Client;
    epoll_event ev;
    ev.data.ptr = Client;
    ev.events = LqEvntSystemEventByConnEvents(Client);
    auto Res = true;
    if(!LqConnIsLock(Client))
        Res = epoll_ctl(Dest->EpollFd, EPOLL_CTL_ADD, Client->Fd, &ev) != -1;
	LqAtmIntrlkAnd(Client->Flag, ~_LQEVNT_FLAG_SYNC);
    Dest->CommonCount++;
    return Res;
}

LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events) {
    Events->DeepLoop++;
    Events->EventEnumIndex = -1;//Set start index
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events) {
    for(int i = Events->EventEnumIndex + 1, m = Events->CountReady; i < m; i++) {
        if(auto c = ((LqEvntFd*)LqArrAt(&Events->EventArr, epoll_event, i).data.ptr)) {
            auto e = LqArrAt(&Events->EventArr, epoll_event, i).events;
            Events->EventEnumIndex = i;
            LqEvntFlag r = 0;
            if(e & EPOLLIN) {
#ifdef LQ_NOT_HAVE_EPOLLRDHUP
                int res = -1;
                if(
                    (LqEvntGetFlags(c) & LQEVNT_FLAG_RDHUP) &&
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
            return r;
        }
    }
    Events->CountReady = -1;
    return 0;
}

void LqSysPollRemoveCurrent(LqSysPoll* Events) {
    LqEvntHdr* c = (LqEvntHdr*)LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr;
    LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr = nullptr;
	LqAtmIntrlkAnd(c->Flag, ~_LQEVNT_FLAG_SYNC);
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, nullptr);
    register auto Evnts = &LqArrAt(&Events->ClientArr, LqEvntHdr*, 0);
    for(; *Evnts != c; Evnts++);
    intptr_t Index = (((uintptr_t)Evnts) - (uintptr_t)&LqArrAt(&Events->ClientArr, LqEvntHdr*, 0)) / sizeof(LqEvntHdr*);
    LqArrRemoveAt(&Events->ClientArr, LqEvntHdr*, Index, nullptr);
    Events->CommonCount--;
}

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events) {
    LqArrAlignAfterRemove(&Events->ClientArr, LqEvntHdr*, nullptr);
}

LqEvntHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    return (LqEvntHdr*)LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr;
}

bool LqSysPollSetMaskByCurrent(LqSysPoll* Events) {
    LqEvntHdr* c = (LqEvntHdr*)LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr;
	LqAtmIntrlkAnd(c->Flag, ~_LQEVNT_FLAG_SYNC);
    epoll_event ev;
    ev.events = LqEvntSystemEventByConnEvents(c);
    ev.data.ptr = c;
    return epoll_ctl(Events->EpollFd, LqConnIsLock(c) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, c->Fd, &ev) != -1;
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*), bool IsRestruct) {
    Events->DeepLoop++;
    for(register auto i = &LqArrAt(&Events->ClientArr, LqEvntHdr*, 0), m = i + Events->ClientArr.Count; i < m; i++)
        if((*i != nullptr) && ((*i)->Flag & _LQEVNT_FLAG_SYNC)) {
            if((*i)->Flag & LQEVNT_FLAG_END) {
                auto Index = ((uintptr_t)i - (uintptr_t)&LqArrAt(&Events->ClientArr, LqEvntHdr*, 0)) / sizeof(LqEvntHdr*);
                LqEvntInterator Iter;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                i = &LqArrAt(&Events->ClientArr, LqEvntHdr*, Index);
                m = &LqArrAt(&Events->ClientArr, LqEvntHdr*, Events->ClientArr.Count);
            } else {
				LqAtmIntrlkAnd((*i)->Flag, ~_LQEVNT_FLAG_SYNC);
                epoll_event ev;
                ev.events = LqEvntSystemEventByConnEvents(*i);
                ev.data.ptr = (*i);
                epoll_ctl(Events->EpollFd, LqConnIsLock(*i) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, (*i)->Fd, &ev);
            }
        }
    if(IsRestruct)
        __LqSysPollRestructAfterRemoves(Events);
    else
        Events->DeepLoop--;
    return 1;
}

bool __LqSysPollEnumBegin(LqSysPoll* Events, LqEvntInterator* Interator) {
    Events->DeepLoop++;
    Interator->Index = -1;
    return __LqSysPollEnumNext(Events, Interator);
}

bool __LqSysPollEnumNext(LqSysPoll* Events, LqEvntInterator* Interator) {
    for(register intptr_t i = Interator->Index + 1, m = Events->ClientArr.Count; i < m; i++)
        if(LqArrAt(&Events->ClientArr, LqEvntHdr*, i) != nullptr) {
            Interator->Index = i;
            return true;
        }
    return false;
}

LqEvntHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    LqEvntHdr* c = LqArrAt(&Events->ClientArr, LqEvntHdr*, Interator->Index);
    LqArrRemoveAt(&Events->ClientArr, LqEvntHdr*, Interator->Index, nullptr);
	LqAtmIntrlkAnd(c->Flag, ~_LQEVNT_FLAG_SYNC);
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, nullptr);

    for(register auto i = &LqArrAt(&Events->EventArr, epoll_event, 0), m = i + Events->CountReady; i < m; i++)
        if(i->data.ptr == c) {
            i->data.ptr = nullptr;
            break;
        }
    Events->CommonCount--;
    return c;
}

LqEvntHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    return LqArrAt(&Events->ClientArr, LqEvntHdr*, Interator->Index);
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    return Events->CountReady = epoll_wait(Events->EpollFd, &LqArrAt(&Events->EventArr, epoll_event, 0), Events->EventArr.Count, WaitTime);
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

