/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqZmbClr (LanQ ZoMBie CLeaneR) - Timer event for clean zombie connections.
*/

#include "LqZmbClr.h"
#include "LqFile.h"
#include "LqWrkBoss.hpp"
#include "LqLog.h"
#include "LqAtm.hpp"
#include "LqSockBuf.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#define LQZMBCLR_FLAG_USED ((unsigned char)1)
#define LQZMBCLR_FLAG_WORK ((unsigned char)2)

extern LqProto ___SockBufProto;

static void _ZmbClrLock(LqZmbClr* ZmbClr) {
    int CurThread = LqThreadId();

    while(true) {
        LqAtmLkWr(ZmbClr->Lk);
        if((ZmbClr->ThreadOwnerId == 0) || (ZmbClr->ThreadOwnerId == CurThread)) {
            ZmbClr->ThreadOwnerId = CurThread;
            ZmbClr->Deep++;
            CurThread = 0;
        }
        LqAtmUlkWr(ZmbClr->Lk);
        if(CurThread == 0)
            return;
        LqThreadYield();
    }
}

static void _ZmbClrUnlock(LqZmbClr* ZmbClr) {
    LqAtmLkWr(ZmbClr->Lk);
    ZmbClr->Deep--;
    if(ZmbClr->Deep == 0) {
        ZmbClr->ThreadOwnerId = 0;
        if(!(ZmbClr->Flags & (LQZMBCLR_FLAG_USED | LQZMBCLR_FLAG_WORK))) {
            LqFileClose(ZmbClr->EvntFd.Fd);
            LqFastAlloc::Delete(ZmbClr);
            return;
        }
    }
    LqAtmUlkWr(ZmbClr->Lk);
}

static void LQ_CALL LqZmbClrHandler(LqEvntFd* Fd, LqEvntFlag RetFlags) {
    LqZmbClr* ZmbClr = (LqZmbClr*)Fd;
    _ZmbClrLock(ZmbClr);
    if(ZmbClr->Flags & LQZMBCLR_FLAG_USED) {
        if(ZmbClr->WrkBoss) {
            if(ZmbClr->Proto)
                ((LqWrkBoss*)ZmbClr->WrkBoss)->CloseClientsByTimeoutAsync(ZmbClr->Proto, ZmbClr->Period);
            else
                ((LqWrkBoss*)ZmbClr->WrkBoss)->CloseClientsByTimeoutAsync(ZmbClr->Period);
        }
        LqTimerSet(Fd->Fd, ZmbClr->Period);
    }
    _ZmbClrUnlock(ZmbClr);
}

static int AsyncClrProc(void* UserData, size_t UserDataSize, void*Wrk, LqClientHdr* EvntHdr, LqTimeMillisec CurTime) {
    LqTimeMillisec TimeDiff;
    LqSockBuf* SockBuf;

    if(!LqClientIsConn(EvntHdr) || (((LqConn*)EvntHdr)->Proto != &___SockBufProto))
        return 0;
    SockBuf = (LqSockBuf*)EvntHdr;
    if(SockBuf->UserData2 == *((void**)UserData)) {
        TimeDiff = CurTime - SockBuf->LastExchangeTime;
        if(TimeDiff > SockBuf->KeepAlive)
            return 2;
    }
    return 0;
}

static void LQ_CALL LqZmbClrHandlerForSockBuf(LqEvntFd* Fd, LqEvntFlag RetFlags) {
    LqZmbClr* ZmbClr = (LqZmbClr*)Fd;
    _ZmbClrLock(ZmbClr);
    if(ZmbClr->Flags & LQZMBCLR_FLAG_USED) {
        if(ZmbClr->WrkBoss)
            ((LqWrkBoss*)ZmbClr->WrkBoss)->EnumClientsAsync(AsyncClrProc, &ZmbClr->UserData2, sizeof(ZmbClr->UserData2));
        LqTimerSet(Fd->Fd, ZmbClr->Period);
    }
    _ZmbClrUnlock(ZmbClr);
}

static void LQ_CALL LqZmbClrHandlerClose(LqEvntFd* Fd) {
    LqZmbClr* ZmbClr = (LqZmbClr*)Fd;
    _ZmbClrLock(ZmbClr);
    ZmbClr->Flags &= ~LQZMBCLR_FLAG_WORK;
    ZmbClr->WrkBoss = NULL;
    if(ZmbClr->CloseHandler != NULL)
        ZmbClr->CloseHandler(ZmbClr);
    _ZmbClrUnlock(ZmbClr);
}

LQ_EXTERN_C LqZmbClr* LQ_CALL LqZmbClrCreate(const void* ProtoOrUserData2ForSockBuf, LqTimeMillisec Period, void* UserData, bool IsSockBuf) {
    LqZmbClr* NewZmbClr;

    if((NewZmbClr = LqFastAlloc::New<LqZmbClr>()) == nullptr) {
        lq_errno_set(ENOMEM);
        return NULL;
    }
    int TimerFd = LqTimerCreate(LQ_O_NOINHERIT);
    if(TimerFd == -1) {
        LqFastAlloc::Delete(NewZmbClr);
        return NULL;
    }
    LqEvntFdInit(&NewZmbClr->EvntFd, TimerFd, LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP, (IsSockBuf)? LqZmbClrHandlerForSockBuf: LqZmbClrHandler, LqZmbClrHandlerClose);
    LqClientSetOnlyOneBoss(NewZmbClr, true); /* Call close handler, when evnt trying move to another boss*/
    LqAtmLkInit(NewZmbClr->Lk);
    NewZmbClr->WrkBoss = NULL;
    NewZmbClr->ThreadOwnerId = 0;
    NewZmbClr->Deep = 0;

    NewZmbClr->Period = Period;
    NewZmbClr->UserData2 = ProtoOrUserData2ForSockBuf;
    NewZmbClr->CloseHandler = NULL;
    NewZmbClr->UserData = UserData;
    NewZmbClr->Flags = LQZMBCLR_FLAG_USED;
    LqTimerSet(TimerFd, Period);
    return NewZmbClr;
}

LQ_EXTERN_C bool LQ_CALL LqZmbClrGoWork(LqZmbClr* ZmbClr, void* WrkBoss) {
    bool Res = false;
    _ZmbClrLock(ZmbClr);
    if(ZmbClr->Flags & LQZMBCLR_FLAG_WORK)
        goto lblOut;
    ZmbClr->WrkBoss = (WrkBoss == NULL) ? LqWrkBossGet() : WrkBoss;
    if(LqClientAdd(&ZmbClr->EvntFd, WrkBoss)) {
        ZmbClr->Flags |= LQZMBCLR_FLAG_WORK;
        Res = true;
        goto lblOut;
    }
lblOut:
    _ZmbClrUnlock(ZmbClr);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqZmbClrInterruptWork(LqZmbClr* ZmbClr) {
    bool Res;
    _ZmbClrLock(ZmbClr);
    if(Res = LqClientSetRemove3(&ZmbClr->EvntFd)) {
        ZmbClr->WrkBoss = NULL;
        ZmbClr->Flags &= ~LQZMBCLR_FLAG_WORK;
    }
    _ZmbClrUnlock(ZmbClr);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqZmbClrSetPeriod(LqZmbClr* ZmbClr, LqTimeMillisec Period) {
    bool Res = false;
    _ZmbClrLock(ZmbClr);
    ZmbClr->Period = Period;
    Res = LqTimerSet(ZmbClr->EvntFd.Fd, Period) >= 0;
    _ZmbClrUnlock(ZmbClr);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqZmbClrDelete(LqZmbClr* ZmbClr) {
    _ZmbClrLock(ZmbClr);
    ZmbClr->UserData = NULL;
    ZmbClr->CloseHandler = NULL;
    ZmbClr->WrkBoss = NULL;
    ZmbClr->Flags &= ~LQZMBCLR_FLAG_USED;
    if(ZmbClr->Flags & LQZMBCLR_FLAG_WORK)
        LqClientSetClose(&ZmbClr->EvntFd);
    _ZmbClrUnlock(ZmbClr);
    return true;
}
