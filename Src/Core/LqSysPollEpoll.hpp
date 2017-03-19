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

#define LqEvntSystemEventByConnFlag(NewFlags)               \
        ((((NewFlags) & LQEVNT_FLAG_RD) ?   EPOLLIN : 0) |  \
        (((NewFlags) & LQEVNT_FLAG_WR) ?    EPOLLOUT : 0) | \
        (((NewFlags) & LQEVNT_FLAG_HUP) ?   EPOLLHUP: 0) |  \
        (((NewFlags) & LQEVNT_FLAG_RDHUP) ? EPOLLRDHUP: 0))

#define LqConnIsLock(NewFlags) (((NewFlags) & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) == 0)

bool LqSysPollInit(LqSysPoll* Dest) {
    Dest->EpollFd = epoll_create(65000);
    if(Dest->EpollFd == -1)
        return false;
    Dest->CountReady = -((intptr_t)1);
    Dest->EventEnumIndex = 0;
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

bool LqSysPollAddHdr(LqSysPoll* Dest, LqClientHdr* Client) {
    epoll_event ev;
    LqEvntFlag NewFlags;
    bool Res;

    NewFlags = _LqEvntGetFlagForUpdate(Client);
    LqArrPushBack(&Dest->ClientArr, LqClientHdr*);
    LqArrBack(&Dest->ClientArr, LqClientHdr*) = Client;
   
    ev.data.ptr = Client;
    ev.events = LqEvntSystemEventByConnFlag(NewFlags);
    Res = true;
    if(!LqConnIsLock(NewFlags))
        Res = epoll_ctl(Dest->EpollFd, EPOLL_CTL_ADD, Client->Fd, &ev) != -1;
    Dest->CommonCount++;
    return Res;
}

LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events) {
    Events->EventEnumIndex = -((intptr_t)1);//Set start index
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events) {
    for(intptr_t i = Events->EventEnumIndex + ((intptr_t)1), m = Events->CountReady; i < m; i++) {
        if(auto c = ((LqEvntFd*)LqArrAt(&Events->EventArr, epoll_event, i).data.ptr)) {
            auto e = LqArrAt(&Events->EventArr, epoll_event, i).events;
            Events->EventEnumIndex = i;
            LqEvntFlag r = 0;
            if(e & EPOLLIN) {
#ifdef LQ_NOT_HAVE_EPOLLRDHUP
                int res = -1;
                if(
                    (LqClientGetFlags(c) & LQEVNT_FLAG_RDHUP) &&
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
    Events->CountReady = -((intptr_t)1);
    return 0;
}

void LqSysPollRemoveCurrent(LqSysPoll* Events) {
    LqClientHdr* c = (LqClientHdr*)LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr;
    LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr = nullptr;
    _LqEvntGetFlagForUpdate(c);
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, nullptr);
    register auto Evnts = &LqArrAt(&Events->ClientArr, LqClientHdr*, 0);
    for(; *Evnts != c; Evnts++);
    intptr_t Index = (((uintptr_t)Evnts) - (uintptr_t)&LqArrAt(&Events->ClientArr, LqClientHdr*, 0)) / sizeof(LqClientHdr*);
    LqArrRemoveAt(&Events->ClientArr, LqClientHdr*, Index, nullptr);
    Events->CommonCount--;
}

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events) {
    LqArrAlignAfterRemove(&Events->ClientArr, LqClientHdr*, nullptr);
}

LqClientHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    return (LqClientHdr*)LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr;
}

bool LqSysPollSetMaskByCurrent(LqSysPoll* Events) {
    epoll_event ev;
    LqClientHdr* c;
	LqEvntFlag NewFlags;

    c = (LqClientHdr*)LqArrAt(&Events->EventArr, epoll_event, Events->EventEnumIndex).data.ptr;
    NewFlags = _LqEvntGetFlagForUpdate(c);
    ev.events = LqEvntSystemEventByConnFlag(NewFlags);
    ev.data.ptr = c;
    return epoll_ctl(Events->EpollFd, LqConnIsLock(NewFlags) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, c->Fd, &ev) != -1;
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*)) {
    uintptr_t Index;
    LqEvntInterator Iter;
    epoll_event ev;
    LqEvntFlag NewFlags;

    for(register auto i = &LqArrAt(&Events->ClientArr, LqClientHdr*, 0), m = i + Events->ClientArr.Count; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                Index = ((uintptr_t)i - (uintptr_t)&LqArrAt(&Events->ClientArr, LqClientHdr*, 0)) / sizeof(LqClientHdr*);
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                i = &LqArrAt(&Events->ClientArr, LqClientHdr*, Index);
                m = &LqArrAt(&Events->ClientArr, LqClientHdr*, Events->ClientArr.Count);
            } else {
                ev.events = LqEvntSystemEventByConnFlag(NewFlags);
                ev.data.ptr = (*i);
                epoll_ctl(Events->EpollFd, LqConnIsLock(NewFlags) ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, (*i)->Fd, &ev);
            }
        }
    return 1;
}

bool __LqSysPollEnumBegin(LqSysPoll* Events, LqEvntInterator* Interator) {
    Interator->Index = -((intptr_t)1);
    return __LqSysPollEnumNext(Events, Interator);
}

bool __LqSysPollEnumNext(LqSysPoll* Events, LqEvntInterator* Interator) {
    for(register intptr_t i = Interator->Index + ((intptr_t)1), m = Events->ClientArr.Count; i < m; i++)
        if(LqArrAt(&Events->ClientArr, LqClientHdr*, i) != NULL) {
            Interator->Index = i;
            return true;
        }
    return false;
}

LqClientHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    LqClientHdr* c = LqArrAt(&Events->ClientArr, LqClientHdr*, Interator->Index);
    LqArrRemoveAt(&Events->ClientArr, LqClientHdr*, Interator->Index, NULL);
    _LqEvntGetFlagForUpdate(c);
    epoll_ctl(Events->EpollFd, EPOLL_CTL_DEL, c->Fd, NULL);

    for(register auto i = &LqArrAt(&Events->EventArr, epoll_event, 0), m = i + Events->CountReady; i < m; i++)
        if(i->data.ptr == c) {
            i->data.ptr = nullptr;
            break;
        }
    Events->CommonCount--;
    return c;
}

LqClientHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    return LqArrAt(&Events->ClientArr, LqClientHdr*, Interator->Index);
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    return Events->CountReady = epoll_wait(Events->EpollFd, &LqArrAt(&Events->EventArr, epoll_event, 0), Events->EventArr.Count, WaitTime);
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

