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


#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include "LqFile.h"

#define LqEvntSystemEventByConnEvents(Client)               \
    (((LqEvntGetFlags(Client) & LQEVNT_FLAG_RD) ? POLLIN : 0) |       \
    ((LqEvntGetFlags(Client) & LQEVNT_FLAG_WR) ?  POLLOUT : 0) |      \
    ((LqEvntGetFlags(Client) & LQEVNT_FLAG_HUP) ? POLLHUP: 0) |       \
    ((LqEvntGetFlags(Client) & LQEVNT_FLAG_ERR) ? POLLERR : 0))


bool LqSysPollInit(LqSysPoll* Dest) {
    LqArr3Init(&Dest->EvntFdArr);
    Dest->EventEnumIndex = 0;
    Dest->DeepLoop = 0;
    Dest->CommonCount = 0;
    return true;
}

void LqSysPollUninit(LqSysPoll* Dest) {
    LqArr3Uninit(&Dest->EvntFdArr);
}

bool LqSysPollAddHdr(LqSysPoll* Dest, LqEvntHdr* Client) {
    LqArr3PushBack(&Dest->EvntFdArr, pollfd, LqEvntHdr*);
    auto El = &LqArr3Back_1(&Dest->EvntFdArr, pollfd);
    LqArr3Back_2(&Dest->EvntFdArr, LqEvntHdr*) = Client;
	LqAtmIntrlkAnd(Client->Flag, ~_LQEVNT_FLAG_SYNC);
    El->fd = Client->Fd;
    El->events = LqEvntSystemEventByConnEvents(Client);
    El->revents = 0;
    Dest->CommonCount++;
    return true;
}

LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events) {
    Events->EventEnumIndex = -1; //Set start index
    Events->DeepLoop++;
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events) {
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
                    (LqEvntGetFlags(LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, i)) & LQEVNT_FLAG_RDHUP) &&
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

void LqSysPollRemoveCurrent(LqSysPoll* Events) {
	LqAtmIntrlkAnd(LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EventEnumIndex)->Flag, ~_LQEVNT_FLAG_SYNC);
    LqArr3RemoveAt(&Events->EvntFdArr, pollfd, LqEvntHdr*, Events->EventEnumIndex, nullptr);
    Events->CommonCount--;
}

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events) {
    LqArr3AlignAfterRemove(&Events->EvntFdArr, pollfd, LqEvntHdr*, nullptr);
}

LqEvntHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    return LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EventEnumIndex);
}

bool LqSysPollSetMaskByCurrent(LqSysPoll* Events) {
    auto c = LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EventEnumIndex);
    LqArr3At_1(&Events->EvntFdArr, pollfd, Events->EventEnumIndex).events = LqEvntSystemEventByConnEvents(c);
	LqAtmIntrlkAnd(c->Flag, ~_LQEVNT_FLAG_SYNC);
    return true;
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*), bool IsRestruct) {
    Events->DeepLoop++;
    for(register auto i = &LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, 0), m = i + Events->EvntFdArr.Count; i < m; i++)
        if((*i != nullptr) && ((*i)->Flag & _LQEVNT_FLAG_SYNC)) {
            auto Index = ((uintptr_t)i - (uintptr_t)&LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, 0)) / sizeof(LqEvntHdr*);
            if(LqEvntGetFlags(*i) & LQEVNT_FLAG_END) {
                LqEvntInterator Iter;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                i = &LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Index);
                m = &LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Events->EvntFdArr.Count);
            } else {
                LqArr3At_1(&Events->EvntFdArr, pollfd, Index).events = LqEvntSystemEventByConnEvents(*i);
				LqAtmIntrlkAnd((*i)->Flag, ~_LQEVNT_FLAG_SYNC);
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
    for(register intptr_t i = Interator->Index + 1, m = Events->EvntFdArr.Count; i < m; i++)
        if(LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, i) != nullptr) {
            Interator->Index = i;
            return true;
        }
    return false;
}

LqEvntHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    auto Conn = LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Interator->Index);
    LqArr3RemoveAt(&Events->EvntFdArr, pollfd, LqEvntHdr*, Interator->Index, nullptr);
	LqAtmIntrlkAnd(Conn->Flag, ~_LQEVNT_FLAG_SYNC);
    Events->CommonCount--;
    return Conn;
}

LqEvntHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    return LqArr3At_2(&Events->EvntFdArr, LqEvntHdr*, Interator->Index);
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    return poll(&LqArr3At_1(&Events->EvntFdArr, pollfd, 0), Events->EvntFdArr.Count, WaitTime);
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

