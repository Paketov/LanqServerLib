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


#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include "LqFile.h"

#define LqEvntSystemEventByConnEvents(Client)               \
    ((((Client)->Flag & LQEVNT_FLAG_RD) ? POLLIN : 0) |       \
    (((Client)->Flag & LQEVNT_FLAG_WR) ?  POLLOUT : 0) |      \
    (((Client)->Flag & LQEVNT_FLAG_HUP) ? POLLHUP: 0) |       \
    (((Client)->Flag & LQEVNT_FLAG_ERR) ? POLLERR : 0))


bool LqEvntInit(LqEvnt* Dest) {
    LqArr3Init(&Dest->EvntFdArr);
    Dest->EventEnumIndex = 0;
    Dest->DeepLoop = 0;
    Dest->CommonCount = 0;
    return true;
}

void LqEvntUninit(LqEvnt* Dest) {
    LqArr3Uninit(&Dest->EvntFdArr);
}

bool LqEvntAddHdr(LqEvnt* Dest, LqEvntHdr* Client) {
    LqArr3PushBack(&Dest->EvntFdArr, pollfd, LqEvntHdr*);
    auto El = &LqArr3Back_1(&Dest->EvntFdArr, pollfd);
    LqArr3Back_2(&Dest->EvntFdArr, LqEvntHdr*) = Client;
    Client->Flag &= ~_LQEVNT_FLAG_SYNC;
    El->fd = Client->Fd;
    El->events = LqEvntSystemEventByConnEvents(Client);
    El->revents = 0;
    Dest->CommonCount++;
    return true;
}

LqEvntFlag __LqEvntEnumEventBegin(LqEvnt* Events) {
    Events->EventEnumIndex = -1; //Set start index
    Events->DeepLoop++;
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqEvnt* Events) {
    for(register intptr_t i = Events->EventEnumIndex + 1, m = Events->EvntFdArr.Count; i < m; i++) {
        if(LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, i) == nullptr)
            continue;
        auto e = LqArr3At_1(&Events->EvntFdArr, pollfd, i).revents;
        if(e & (POLLIN | POLLOUT | POLLHUP | POLLERR)) {
            Events->EventEnumIndex = i;
            LqEvntFlag r = 0;
            if(e & POLLIN) {
                int res = -1;
                if(
                    (LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, i)->Flag & LQEVNT_FLAG_RDHUP) &&
                    (ioctl(LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, i)->Fd, FIONREAD, &res) >= 0) &&
                    (res <= 0)
                    )
                    r |= LQEVNT_FLAG_RDHUP;
                r |= LQEVNT_FLAG_RD;
            }
            if(e & POLLOUT)
                r |= LQEVNT_FLAG_WR;
            if(e & POLLHUP)
                r |= LQEVNT_FLAG_HUP;
            if(e & POLLERR)
                r |= LQEVNT_FLAG_ERR;
            return r;
        }
    }
    return 0;
}

void LqEvntRemoveCurrent(LqEvnt* Events) {
    LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EventEnumIndex)->Flag &= ~_LQEVNT_FLAG_SYNC;
    LqArr3RemoveAt(&Events->EvntFdArr, pollfd, LqEvntHdr*, Events->EventEnumIndex, nullptr);
    Events->CommonCount--;
}

void __LqEvntRestructAfterRemoves(LqEvnt* Events) {
    LqArr3AlignAfterRemove(&Events->EvntFdArr, pollfd, LqEvntHdr*, nullptr);
}

LqEvntHdr* LqEvntGetHdrByCurrent(LqEvnt* Events) {
    return LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EventEnumIndex);
}

bool LqEvntSetMaskByCurrent(LqEvnt* Events) {
    auto c = LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EventEnumIndex);
    LqArr3At_1(&Events->EvntFdArr, pollfd, Events->EventEnumIndex).events = LqEvntSystemEventByConnEvents(c);
    c->Flag &= ~_LQEVNT_FLAG_SYNC;
    return true;
}

int LqEvntUpdateAllMask(LqEvnt* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*), bool IsRestruct) {
    Events->DeepLoop++;
    for(register auto i = &LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, 0), m = i + Events->EvntFdArr.Count; i < m; i++)
        if((*i != nullptr) && ((*i)->Flag & _LQEVNT_FLAG_SYNC)) {
            auto Index = ((uintptr_t)i - (uintptr_t)&LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, 0)) / sizeof(LqEvntHdr*);
            if((*i)->Flag & LQEVNT_FLAG_END) {
                LqEvntInterator Iter;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                i = &LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Index);
                m = &LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EvntFdArr.Count);
            } else {
                LqArr3At_1(&Events->EvntFdArr, pollfd, Index).events = LqEvntSystemEventByConnEvents(*i);
                (*i)->Flag &= ~_LQEVNT_FLAG_SYNC;
            }
        }

    if(IsRestruct)
        __LqEvntRestructAfterRemoves(Events);
    else
        Events->DeepLoop--;
    return 1;
}

bool __LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator) {
    Events->DeepLoop++;
    Interator->Index = -1;
    return __LqEvntEnumNext(Events, Interator);
}

bool __LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator) {
    for(register intptr_t i = Interator->Index + 1, m = Events->EvntFdArr.Count; i < m; i++)
        if(LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, i) != nullptr) {
            Interator->Index = i;
            return true;
        }
    return false;
}

LqEvntHdr* LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator) {
    auto Conn = LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Interator->Index);
    LqArr3RemoveAt(&Events->EvntFdArr, pollfd, LqEvntHdr*, Interator->Index, nullptr);
    Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
    Events->CommonCount--;
    return Conn;
}

LqEvntHdr* LqEvntGetHdrByInterator(LqEvnt* Events, LqEvntInterator* Interator) {
    return LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Interator->Index);
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime) {
    return poll(&LqArr3At_1(&Events->EvntFdArr, pollfd, 0), Events->EvntFdArr.Count, WaitTime);
}

size_t LqEvntCount(const LqEvnt* Events) {
    return Events->CommonCount;
}

