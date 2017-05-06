/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSysPoll... - Multiplatform abstracted event folower
* This part of server support:
*   +Windows window message. (one thread processing all message for multiple threads)
*   +Windows internal async iocp poll. (most powerfull method in windows)
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

#define LqEvntSystemEventByConnFlag(NewFlags)               \
    ((((NewFlags) & LQEVNT_FLAG_RD) ? POLLIN : 0) |       \
    (((NewFlags) & LQEVNT_FLAG_WR) ?  POLLOUT : 0) |      \
    (((NewFlags) & LQEVNT_FLAG_HUP) ? POLLHUP: 0) |       \
    (((NewFlags) & LQEVNT_FLAG_ERR) ? POLLERR : 0))


bool LqSysPollInit(LqSysPoll* Dest) {
    LqArr3Init(&Dest->EvntFdArr);
    Dest->EventEnumIndex = 0;
    Dest->CommonCount = 0;
    return true;
}

void LqSysPollUninit(LqSysPoll* Dest) {
    LqArr3Uninit(&Dest->EvntFdArr);
}

bool LqSysPollAddHdr(LqSysPoll* Dest, LqClientHdr* Client) {
    LqEvntFlag NewFlags = _LqEvntGetFlagForUpdate(Client);
    LqArr3PushBack(&Dest->EvntFdArr, pollfd, LqClientHdr*);
    auto El = &LqArr3Back_1(&Dest->EvntFdArr, pollfd);
    LqArr3Back_2(&Dest->EvntFdArr, LqClientHdr*) = Client;
    El->fd = Client->Fd;
    El->events = LqEvntSystemEventByConnFlag(NewFlags);
    El->revents = 0;
    Dest->CommonCount++;
    return true;
}

LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events) {
    Events->EventEnumIndex = -((intptr_t)1); //Set start index
    return __LqEvntEnumEventNext(Events);
}

LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events) {
    for(register intptr_t i = Events->EventEnumIndex + 1, m = Events->EvntFdArr.Count; i < m; i++) {
        if(LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, i) == NULL)
            continue;
        auto e = LqArr3At_1(&Events->EvntFdArr, pollfd, i).revents;
        if(e & (POLLIN | POLLOUT | POLLHUP | POLLERR)) {
            Events->EventEnumIndex = i;
            LqEvntFlag r = 0;
            if(e & POLLIN) {
                int res = -1;
                if(
                    (LqClientGetFlags(LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, i)) & LQEVNT_FLAG_RDHUP) &&
                    (ioctl(LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, i)->Fd, FIONREAD, &res) >= 0) &&
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
    LqAtmIntrlkAnd(LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Events->EventEnumIndex)->Flag, ~_LQEVNT_FLAG_SYNC);
    LqArr3RemoveAt(&Events->EvntFdArr, pollfd, LqClientHdr*, Events->EventEnumIndex, NULL);
    Events->CommonCount--;
}

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events) {
    LqArr3AlignAfterRemove(&Events->EvntFdArr, pollfd, LqClientHdr*, NULL);
}

LqClientHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events) {
    return LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Events->EventEnumIndex);
}

bool LqSysPollSetMaskByCurrent(LqSysPoll* Events) {
    auto c = LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Events->EventEnumIndex);
    LqEvntFlag NewFlags = _LqEvntGetFlagForUpdate(c);
    LqArr3At_1(&Events->EvntFdArr, pollfd, Events->EventEnumIndex).events = LqEvntSystemEventByConnFlag(NewFlags);
    return true;
}

int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*)) {
    LqEvntFlag NewFlags;
    for(register auto i = &LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, 0), m = i + Events->EvntFdArr.Count; i < m; i++)
        if((*i != NULL) && (LqClientGetFlags(*i) & _LQEVNT_FLAG_SYNC)) {
            auto Index = ((uintptr_t)i - (uintptr_t)&LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, 0)) / sizeof(LqClientHdr*);
            NewFlags = _LqEvntGetFlagForUpdate(*i);
            if(NewFlags & LQEVNT_FLAG_END) {
                LqEvntInterator Iter;
                Iter.Index = Index;
                DelProc(UserData, &Iter);
                i = &LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Index);
                m = &LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Events->EvntFdArr.Count);
            } else {
                LqArr3At_1(&Events->EvntFdArr, pollfd, Index).events = LqEvntSystemEventByConnFlag(NewFlags);
            }
        }
    return 1;
}

bool __LqSysPollEnumBegin(LqSysPoll* Events, LqEvntInterator* Interator) {
    Interator->Index = -((intptr_t)1);
    return __LqSysPollEnumNext(Events, Interator);
}

bool __LqSysPollEnumNext(LqSysPoll* Events, LqEvntInterator* Interator) {
    for(register intptr_t i = Interator->Index + ((intptr_t)1), m = Events->EvntFdArr.Count; i < m; i++)
        if(LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, i) != NULL) {
            Interator->Index = i;
            return true;
        }
    return false;
}

LqClientHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    auto Conn = LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Interator->Index);
    LqArr3RemoveAt(&Events->EvntFdArr, pollfd, LqClientHdr*, Interator->Index, nullptr);
    _LqEvntGetFlagForUpdate(Conn)
    Events->CommonCount--;
    return Conn;
}

LqClientHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator) {
    return LqArr3At_2(&Events->EvntFdArr, LqClientHdr*, Interator->Index);
}

int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime) {
    return poll(&LqArr3At_1(&Events->EvntFdArr, pollfd, 0), Events->EvntFdArr.Count, WaitTime);
}

size_t LqSysPollCount(const LqSysPoll* Events) {
    return Events->CommonCount;
}

