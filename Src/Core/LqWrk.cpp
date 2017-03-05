/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrk - Worker class.
* Recive and handle command from LqWrkBoss.
*/


#include "LqWrk.hpp"
#include "LqAlloc.hpp"
#include "LqWrkBoss.hpp"
#include "LqLog.h"
#include "LqOs.h"
#include "LqConn.h"
#include "LqTime.hpp"
#include "LqFile.h"
#include "LqAtm.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#include <time.h>
#include <stdint.h>

#if !defined(LQPLATFORM_WINDOWS)
#include <signal.h>
#endif

/*
*  Use this macro if you want protect sync operations calling close handlers.
*    When this macro used, thread caller of sync methods, waits until worker thread leave r/w/c handler.
*   When close handler called by worker thread, thread-caller of sync kind method continue enum.
*  Disable this macro if you want to shift the entire responsibility of the user.
*/

#define LQWRK_ENABLE_RW_HNDL_PROTECT


/*
* This macro used for some platform kind LqSysPollCheck functions
*  When used, never add/remove to/from event list, while worker thread is in LqSysPollCheck function
*/
#define LQWRK_ENABLE_WAIT_LOCK


#ifdef LQWRK_ENABLE_WAIT_LOCK
#define LqWrkWaiterLock(Wrk)     ((LqWrk*)(Wrk))->WaiterLock()
#define LqWrkWaiterUnlock(Wrk)   ((LqWrk*)(Wrk))->WaiterUnlock()
#define LqWrkWaiterLockMain(Wrk) ((LqWrk*)(Wrk))->WaiterLockMain()
#else
#define LqWrkWaiterLock(Wrk)     ((void)0)
#define LqWrkWaiterUnlock(Wrk)   ((void)0)
#define LqWrkWaiterLockMain(Wrk) ((void)0)
#endif

#define LqWrkCallConnHandler(Conn, Flags, Wrk)                         \
{                                                                      \
    auto Handler = ((LqConn*)(Conn))->Proto->Handler;                  \
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqConn*)(Conn), (Flags));                                 \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallConnCloseHandler(Conn, Wrk) {                         \
    auto Handler = ((LqConn*)(Conn))->Proto->CloseHandler;             \
    LqAtmIntrlkOr(((LqConn*)(Conn))->Flag, _LQEVNT_FLAG_NOW_EXEC);     \
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqConn*)(Conn));                                          \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallEvntFdHandler(EvntHdr, Flags, Wrk) {                  \
    auto Handler = ((LqEvntFd*)(EvntHdr))->Handler;                    \
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqEvntFd*)(EvntHdr), (Flags));                            \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallEvntFdCloseHandler(EvntHdr, Wrk) {                    \
    auto Handler = ((LqEvntFd*)(EvntHdr))->CloseHandler;               \
    LqAtmIntrlkOr(((LqEvntFd*)(EvntHdr))->Flag, _LQEVNT_FLAG_NOW_EXEC);\
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqEvntFd*)(EvntHdr));                                     \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallEvntHdrCloseHandler(Event, Wrk) {                     \
    if(LqEvntGetFlags(Event) & _LQEVNT_FLAG_CONN){                     \
        LqWrkCallConnCloseHandler(Event, Wrk);                         \
    }else{                                                             \
       LqWrkCallEvntFdCloseHandler(Event, Wrk);}                       \
}
/*
* Enum event changes
*  Use only by worker thread
*/
#define LqWrkEnumChangesEvntDo(Wrk, EventFlags)                        \
     {((LqWrk*)(Wrk))->Lock();                                         \
     for(LqEvntFlag EventFlags = __LqSysPollEnumEventBegin(&(((LqWrk*)(Wrk))->EventChecker)); EventFlags != 0; EventFlags = __LqEvntEnumEventNext(&(((LqWrk*)(Wrk))->EventChecker)))

#define LqWrkEnumChangesEvntWhile(Wrk)                                 \
 if(__LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker)))           \
    __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker)); \
    ((LqWrk*)(Wrk))->Unlock();                                         \
 }

/*
* Use when enum by another thread or by worker tread
*/
#define LqWrkEnumEvntDo(Wrk, IndexName) {                              \
    LqEvntInterator IndexName;                                         \
    ((LqWrk*)(Wrk))->Lock();                                           \
    ((LqWrk*)(Wrk))->AcceptAllEventFromQueue();                        \
    for(auto __r = __LqSysPollEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqSysPollEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))


#define LqWrkEnumEvntWhile(Wrk)                                        \
    if(__LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {      \
       LqWrkWaiterLock(Wrk);                                           \
       __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));\
       LqWrkWaiterUnlock(Wrk);                                         \
    }                                                                  \
    ((LqWrk*)(Wrk))->Unlock();                                         \
}

/*
* Use when enum by worker(owner) thread
*/
#define LqWrkEnumEvntOwnerDo(Wrk, IndexName) {                         \
    LqEvntInterator IndexName;                                         \
    ((LqWrk*)(Wrk))->Lock();                                           \
    for(auto __r = __LqSysPollEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqSysPollEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))

#define LqWrkEnumEvntOwnerWhile(Wrk)                                   \
    if(__LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {      \
    __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker)); \
    }                                                                  \
    ((LqWrk*)(Wrk))->Unlock();                                         \
}

/*
* Custum enum, use in LqWrkBoss::TransferAllEvnt
*/
#define LqWrkEnumEvntNoLkDo(Wrk, IndexName)                            \
 {                                                                     \
    LqEvntInterator IndexName;                                         \
    for(auto __r = __LqSysPollEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqSysPollEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))

#define LqWrkEnumEvntNoLkWhile(Wrk)                                    \
    if(__LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {      \
    LqWrkWaiterLock(Wrk);                                              \
    __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker)); \
    LqWrkWaiterUnlock(Wrk);                                            \
    }                                                                  \
}

/*
* Wait events, use only by worker thread
*/
#define LqWrkWaitEvntsChanges(Wrk) {                                   \
    LqWrkWaiterLockMain(Wrk);                                          \
    LqSysPollCheck(&((LqWrk*)(Wrk))->EventChecker, LqTimeGetMaxMillisec());\
    LqWrkWaiterUnlock(Wrk);                                            \
}

/*
* Remove event by interator
*/
#define LqWrkRemoveByInterator(Wrk, Iter) {                            \
    LqWrkWaiterLock(Wrk);                                              \
    LqSysPollRemoveByInterator(&((LqWrk*)(Wrk))->EventChecker, Iter);  \
    LqWrkWaiterUnlock(Wrk);                                            \
}

#define LqWrkUnsetWrkOwner(EvntHdr) {                                  \
    LqAtmLkWr(((LqEvntHdr*)(EvntHdr))->Lk);                            \
    ((LqEvntHdr*)(EvntHdr))->WrkOwner = NULL;                          \
    LqAtmUlkWr(((LqEvntHdr*)(EvntHdr))->Lk);                           \
}

#define LqWrkSetWrkOwner(EvntHdr, NewOwner) {                          \
    LqAtmLkWr(((LqEvntHdr*)(EvntHdr))->Lk);                            \
    ((LqEvntHdr*)(EvntHdr))->WrkOwner = (NewOwner);                    \
    LqAtmUlkWr(((LqEvntHdr*)(EvntHdr))->Lk);                           \
}

enum {
    LQWRK_CMD_RM_CONN_ON_TIME_OUT,
    LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO,
    LQWRK_CMD_WAIT_EVENT,
    LQWRK_CMD_CLOSE_ALL_CONN,
    LQWRK_CMD_RM_CONN_BY_IP,
    LQWRK_CMD_CLOSE_CONN_BY_PROTO,
    LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqWrkCmdWaitEvnt {
    void(LQ_CALL*EventAct)(void*);
    void* UserData;
    inline LqWrkCmdWaitEvnt() {}
    inline LqWrkCmdWaitEvnt(void(LQ_CALL*NewEventProc)(void*), void* NewUserData): EventAct(NewEventProc), UserData(NewUserData) {}
};

struct LqWrkCmdCloseByTimeout {
    LqTimeMillisec TimeLive;
    const LqProto* Proto;
    inline LqWrkCmdCloseByTimeout() {}
    inline LqWrkCmdCloseByTimeout(const LqProto* NewProto, LqTimeMillisec Millisec): Proto(NewProto), TimeLive(Millisec) {}
};

struct LqWrkAsyncEventForAllFd {
    int(LQ_CALL*EventAct)(void* UserData, size_t UserDataSize, void*Wrk, LqEvntHdr* EvntHdr, LqTimeMillisec CurTime);
    void* UserData;
    size_t UserDataSize;
    const LqProto* Proto;

    inline LqWrkAsyncEventForAllFd() {}
};

#pragma pack(pop)


static uint8_t _EmptyWrk[sizeof(LqWrk)];

static llong __LqWrkInitEmpty() {
    ((LqWrk*)_EmptyWrk)->CountPointers = 20;
    ((LqWrk*)_EmptyWrk)->Id = -1;
    return 1;
}

static llong __IdGen = __LqWrkInitEmpty();
static LqLocker<uintptr_t> __IdGenLocker;

static llong GenId() {
    llong r;
    __IdGenLocker.LockWrite();
    r = __IdGen;
    __IdGen++;
    __IdGenLocker.UnlockWrite();
    return r;
}

LqWrkPtr LqWrk::New(bool IsStart) {
    return LqFastAlloc::New<LqWrk>(IsStart);
}

LqWrkPtr LqWrk::ByEvntHdr(LqEvntHdr * EvntHdr) {
    LqAtmLkWr(EvntHdr->Lk);
    LqWrkPtr Res((((LqEvntHdr*)(EvntHdr))->WrkOwner != NULL)? ((LqWrk*)((LqEvntHdr*)(EvntHdr))->WrkOwner): ((LqWrk*)_EmptyWrk));
    LqAtmUlkWr(EvntHdr->Lk);
    return Res;
}

LqWrkPtr LqWrk::GetNull() {
    return ((LqWrk*)_EmptyWrk);
}

LQ_IMPORTEXPORT void LQ_CALL LqWrkDelete(LqWrk* This) {
    if(This->IsThisThread()) {
        LqWrkBoss::GetGlobal()->TransferAllEvnt(This);
        This->ClearQueueCommands();
        for(volatile size_t* t = &This->CountPointers; *t > 0; LqThreadYield());
        for(unsigned i = 0; i < 10; i++) {
            This->Lock();
            This->Unlock();
            LqThreadYield();
        }
        This->IsDelete = true;
        This->EndWorkAsync();
        return;
    }
    LqFastAlloc::Delete(This);
}

void LqWrk::AcceptAllEventFromQueue() {
    for(auto Command = EvntFdQueue.Fork(); Command;) {
        LqEvntHdr* Hdr = Command.Val<LqEvntHdr*>();
        Command.Pop<LqEvntHdr*>();
        CountConnectionsInQueue--;
        LqLogInfo("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, Hdr->Fd, (ullong)Hdr->Flag);
        LqSysPollAddHdr(&EventChecker, Hdr);
    }
}

void LqWrk::ExitHandlerFn(void * Data) {
    auto This = (LqWrk*)Data;
    if(This->IsDelete)
        LqFastAlloc::Delete(This);
}

LqWrk::LqWrk(bool IsStart):
    CountPointers(0),
    LqThreadBase(([&] { Id = GenId();  LqString Str = "Worker #" + LqToString(Id); return Str; })().c_str()) 
{
    TimeStart = LqTimeGetLocMillisec();
    CountConnectionsInQueue = 0;

    UserData = this;
    ExitHandler = ExitHandlerFn;
    IsDelete = false;
    IsSyncAllFlags = 0;
    auto NotifyFd = LqEventCreate(LQ_O_NOINHERIT);
    LqEvntFdInit(&NotifyEvent, NotifyFd, LQEVNT_FLAG_RD, NULL, NULL);
    LqSysPollInit(&EventChecker);
    if(IsStart)
        StartSync();
}

LqWrk::~LqWrk() {
    EndWorkSync();
    CloseAllEvntSync();
    for(volatile size_t* t = &CountPointers; *t > 0; LqThreadYield());
    for(unsigned i = 0; i < 10; i++) {
        Lock();
        Unlock();
        LqThreadYield();
    }
    LqSysPollUninit(&EventChecker);
    LqFileClose(NotifyEvent.Fd);
}

size_t LqWrkBoss::_TransferAllEvnt(LqWrk* Source, bool ByThisBoss) {
    LqEvntHdr** RmHdrs = NULL;
    size_t RmHdrsSize = 0;
    size_t Res = 0;
    LqEvntHdr* Hdr;
    intptr_t Busy;
    intptr_t Min, Index;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = Source->IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    Source->Lock();
    auto NotifyEvntHdr = (LqEvntHdr*)&Source->NotifyEvent;
    LqWrkEnumEvntNoLkDo(Source, i) {
        if((Hdr = LqSysPollGetHdrByInterator(&Source->EventChecker, &i)) == NotifyEvntHdr)
            continue;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(LqEvntGetFlags(Hdr) & NowExec) {
            Source->Unlock();
            LqThreadYield();
            Source->Lock();
            if(LqSysPollGetHdrByInterator(&Source->EventChecker, &i) != Hdr)
                goto lblContinue;
        }
#endif
        {
            Min = INTPTR_MAX;
            Index = -1;
            WrkArray::interator LocWrks = Wrks.begin();
            for(size_t i = 0, m = LocWrks.size(); i < m; i++) {
                if(LocWrks[i] == Source)
                    continue;
                Busy = LocWrks[i]->GetAssessmentBusy();
                if(Busy < Min)
                    Min = Busy, Index = i;
            }
            LqWrkRemoveByInterator(Source, &i);
            if((ByThisBoss || !(LqEvntGetFlags(Hdr) & _LQEVNT_FLAG_ONLY_ONE_BOSS)) && (Index != -1) && LocWrks[Index]->AddEvntAsync(Hdr)) {
                Res++;
            } else {
                LqWrkUnsetWrkOwner(Hdr);
                RmHdrs = (LqEvntHdr**)___realloc(RmHdrs, RmHdrsSize + 1);
                RmHdrs[RmHdrsSize] = Hdr;
                RmHdrsSize++;
                LqLogErr("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
            }
        }
lblContinue:;
    }LqWrkEnumEvntNoLkWhile(Source);

    for(auto Command = Source->EvntFdQueue.Fork(); Command;) {
        Hdr = Command.Val<LqEvntHdr*>();
        Command.Pop<LqEvntHdr*>();
        Source->CountConnectionsInQueue--;
        WrkArray::interator LocWrks = Wrks.begin();
        Min = INTPTR_MAX;
        Index = -1;
        for(size_t i = 0, m = LocWrks.size(); i < m; i++) {
            if(LocWrks[i] == Source)
                continue;
            Busy = LocWrks[i]->GetAssessmentBusy();
            if(Busy < Min)
                Min = Busy, Index = i;
        }

        if((ByThisBoss || !(LqEvntGetFlags(Hdr) & _LQEVNT_FLAG_ONLY_ONE_BOSS)) && (Index != -1) && LocWrks[Index]->AddEvntAsync(Hdr)) {
            Res++;
        } else {
            LqWrkUnsetWrkOwner(Hdr);
            RmHdrs = (LqEvntHdr**)___realloc(RmHdrs, RmHdrsSize + 1);
            RmHdrs[RmHdrsSize] = Hdr;
            RmHdrsSize++;
            LqLogErr("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
        }
    }
    Source->Unlock();
    if(RmHdrs != NULL) {
        for(size_t i = 0; i < RmHdrsSize; i++) {
            LqEvntCallCloseHandler(RmHdrs[i]);
        }
        ___free(RmHdrs);
    }
    return Res;
}


llong LqWrk::GetId() const {
    return Id;
}

void LqWrk::DelProc(void* Data, LqEvntInterator* Iter) {
    auto Hdr = LqSysPollGetHdrByInterator(&((LqWrk*)Data)->EventChecker, Iter);
    if(LqEvntGetFlags(Hdr) & _LQEVNT_FLAG_NOW_EXEC)
        return;
    LqWrkRemoveByInterator(Data, Iter);
    LqWrkUnsetWrkOwner(Hdr);
    LqWrkCallEvntHdrCloseHandler(Hdr, Data);
}

void LqWrk::ParseInputCommands() {
    /*
    * Recive async command loop.
    */
    LqEvntHdr** RmHdrs = NULL;
    size_t RmHdrsSize = 0;
    Lock();
    for(auto Command = EvntFdQueue.Fork(); Command;) {
        LqEvntHdr* Hdr = Command.Val<LqEvntHdr*>();
        Command.Pop<LqEvntHdr*>();
        CountConnectionsInQueue--;
        if(LqEvntGetFlags(Hdr) & LQEVNT_FLAG_END) {
            RmHdrs = (LqEvntHdr**)___realloc(RmHdrs, RmHdrsSize + 1);
            RmHdrs[RmHdrsSize] = Hdr;
            RmHdrsSize++;
        } else {
            LqLogInfo("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, Hdr->Fd, (ullong)Hdr->Flag);
            LqSysPollAddHdr(&EventChecker, Hdr);
        }
    }
    Unlock();
    if(RmHdrs != NULL) {
        for(size_t i = 0; i < RmHdrsSize; i++) {
            LqWrkUnsetWrkOwner(RmHdrs[i]);
            LqEvntCallCloseHandler(RmHdrs[i]);
        }
        ___free(RmHdrs);
    }
    for(auto Command = CommandQueue.Fork(); Command;) {
        switch(Command.Type) {
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT: {
                /*
                * Remove zombie connections by time val.
                */
                auto TimeLiveMilliseconds = Command.Val<LqTimeMillisec>();
                Command.Pop<LqTimeMillisec>();
                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(LqEvntIsConn(Evnt) && ((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds)) {
                        LqLogInfo("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                        LqSysPollRemoveByInterator(&EventChecker, &i);
                        LqWrkUnsetWrkOwner(Evnt);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO: {
                auto TimeLiveMilliseconds = Command.Val<LqWrkCmdCloseByTimeout>().TimeLive;
                auto Proto = Command.Val<LqWrkCmdCloseByTimeout>().Proto;
                Command.Pop<LqWrkCmdCloseByTimeout>();

                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto) && Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds)) {
                        LqLogInfo("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                        LqSysPollRemoveByInterator(&EventChecker, &i);
                        LqWrkUnsetWrkOwner(Evnt);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_WAIT_EVENT: {
                /*
                Call procedure for wait event.
                */
                Command.Val<LqWrkCmdWaitEvnt>().EventAct(Command.Val<LqWrkCmdWaitEvnt>().UserData);
                Command.Pop<LqWrkCmdWaitEvnt>();
            }
            break;
            case LQWRK_CMD_CLOSE_ALL_CONN: {
                /*
                Close all waiting connections.
                */
                Command.Pop();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollRemoveByInterator(&EventChecker, &i);
                    LqWrkUnsetWrkOwner(Evnt);
                    LqWrkCallEvntHdrCloseHandler(Evnt, this);
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_RM_CONN_BY_IP: {
                CloseConnByIpSync(&Command.Val<LqConnAddr>().Addr);
                Command.Pop<LqConnAddr>();
            }
            break;
            case LQWRK_CMD_CLOSE_CONN_BY_PROTO: {
                auto Proto = Command.Val<const LqProto*>();
                Command.Pop<const LqProto*>();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
                        LqLogInfo("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
                        LqSysPollRemoveByInterator(&EventChecker, &i);
                        LqWrkUnsetWrkOwner(Evnt);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD: {
                auto AsyncEvnt = &Command.Val<LqWrkAsyncEventForAllFd>();
                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if((((LqConn*)Evnt)->Proto == NULL) || (LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == AsyncEvnt->Proto))){
                        switch(AsyncEvnt->EventAct(AsyncEvnt->UserData, AsyncEvnt->UserDataSize, this, Evnt, CurTime)) {
                            case 1:
                                LqSysPollRemoveByInterator(&EventChecker, &i);
                                LqWrkUnsetWrkOwner(Evnt);
                                break;
                            case 2:
                                LqSysPollRemoveByInterator(&EventChecker, &i);
                                LqWrkUnsetWrkOwner(Evnt);
                                LqWrkCallConnCloseHandler(Evnt, this);
                                break;
                        }
                    }
                }LqWrkEnumEvntOwnerWhile(this);
                
                if(AsyncEvnt->UserDataSize > 0)
                    ___free(AsyncEvnt->UserData);

                Command.Pop<LqWrkAsyncEventForAllFd>();
            }
            break;
            default:
                /*
                Is command unknown.
                */
                Command.JustPop();
        }
    }
}

void LqWrk::ClearQueueCommands() {

    for(auto Command = EvntFdQueue.Fork(); Command;) {
        auto Evnt = Command.Val<LqEvntHdr*>();
        Command.Pop<LqEvntHdr*>();
        LqWrkUnsetWrkOwner(Evnt);
        LqEvntCallCloseHandler(Evnt);
        CountConnectionsInQueue--;
    }

    for(auto Command = CommandQueue.Fork(); Command;) {
        switch(Command.Type) {
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD: {
                if(Command.Val<LqWrkAsyncEventForAllFd>().UserDataSize > 0)
                    ___free(Command.Val<LqWrkAsyncEventForAllFd>().UserData);
                Command.Pop<LqWrkAsyncEventForAllFd>();
            }
            break;
            default:
                Command.JustPop();
        }
    }
}

/*
 Main worker loop
*/
void LqWrk::BeginThread() {
    LqLogInfo("LqWrk::BeginThread()#%llu start worker thread\n", Id);
#if !defined(LQPLATFORM_WINDOWS)
    signal(SIGPIPE, SIG_IGN);
#endif
    Lock();
    LqSysPollThreadInit(&EventChecker);
    LqSysPollAddHdr(&EventChecker, (LqEvntHdr*)&NotifyEvent);
    Unlock();
    while(true) {
        if(LqEventReset(NotifyEvent.Fd)) {
            if(LqThreadBase::IsShouldEnd)
                break;
            ParseInputCommands();
            if(LqThreadBase::IsShouldEnd)
                break;
            uintptr_t Expected = 1;
            if(LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)0)) {
                Lock();
                LqSysPollUpdateAllMask(&EventChecker, this, DelProc, false);
                Unlock();
            }
        }

        LqWrkEnumChangesEvntDo(this, Revent) {
            auto EvntHdr = LqSysPollGetHdrByCurrent(&EventChecker);
            auto OldFlags = EvntHdr->Flag;
            LqAtmIntrlkOr(EvntHdr->Flag, _LQEVNT_FLAG_NOW_EXEC);
            if(LqEvntIsConn(EvntHdr)) {
                if(Revent & (LQEVNT_FLAG_ERR | LQEVNT_FLAG_WR | LQEVNT_FLAG_RD | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT)) {
                    LqWrkCallConnHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqSysPollGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            } else {
                if(Revent & (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_ERR | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT)) {
                    LqWrkCallEvntFdHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqSysPollGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            }
            if((Revent & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_END)) || (LqEvntGetFlags(EvntHdr) & LQEVNT_FLAG_END)) {
                LqSysPollRemoveCurrent(&EventChecker);
                LqWrkUnsetWrkOwner(EvntHdr);
                LqWrkCallEvntHdrCloseHandler(EvntHdr, this);
            } else {
                LqAtmIntrlkAnd(EvntHdr->Flag, ~_LQEVNT_FLAG_NOW_EXEC);
                if(EvntHdr->Flag != OldFlags) //If have event changes
                    LqSysPollSetMaskByCurrent(&EventChecker);
                LqSysPollUnuseCurrent(&EventChecker);
            }
        }LqWrkEnumChangesEvntWhile(this);

        LqWrkWaitEvntsChanges(this);
    }

    RemoveEvnt((LqEvntHdr*)&NotifyEvent);
    LqWrkBoss::GetGlobal()->TransferAllEvnt(this);
    CloseAllEvntSync();
    ClearQueueCommands();
    Lock();
    LqSysPollThreadUninit(&EventChecker);
    Unlock();
    LqLogInfo("LqWrk::BeginThread()#%llu end worker thread\n", Id);
    LqThreadYield();
    LqThreadYield();
}

bool LqWrk::RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds) {
    if(!CommandQueue.PushBegin(LQWRK_CMD_RM_CONN_ON_TIME_OUT, TimeLiveMilliseconds))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds) {
    size_t Res = 0;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt)) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqEvntGetFlags(Evnt) & NowExec) {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            if(((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds)) {
                LqLogInfo("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                LqWrkRemoveByInterator(this, &i);
                LqWrkUnsetWrkOwner(Evnt);
                LqWrkCallConnCloseHandler(Evnt, this);
                Res++;
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::RemoveConnOnTimeOutAsync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds) {
    if(!CommandQueue.PushBegin<LqWrkCmdCloseByTimeout>(LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO, LqWrkCmdCloseByTimeout(Proto, TimeLiveMilliseconds)))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveConnOnTimeOutSync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds) {
    size_t Res = 0;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Evnt->Flag & NowExec) {
                Unlock();
                LqThreadYield(); //Wait until worker thread leave read/write handler
                Lock();
                if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            if(Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds)) {
                LqLogInfo("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                LqWrkRemoveByInterator(this, &i);
                LqWrkUnsetWrkOwner(Evnt);
                LqWrkCallConnCloseHandler(Evnt, this);
                Res++;
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::AddEvntAsync(LqEvntHdr* EvntHdr) {
    if(IsThisThread())
        return AddEvntSync(EvntHdr);
    if(!EvntFdQueue.Push(EvntHdr, this))
        return false;
    CountConnectionsInQueue++;
    NotifyThread();
    return true;
}

bool LqWrk::AddEvntSync(LqEvntHdr* EvntHdr) {
    if(LqEvntGetFlags(EvntHdr) & LQEVNT_FLAG_END) {
        LqEvntCallCloseHandler(EvntHdr);
        return true;
    }
    LqLogInfo("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, EvntHdr->Fd, (ullong)EvntHdr->Flag);
    Lock();
    LqWrkSetWrkOwner(EvntHdr, this);
    LqWrkWaiterLock(this);
    auto Res = LqSysPollAddHdr(&EventChecker, EvntHdr);
    LqWrkWaiterUnlock(this);
    Unlock();
    return Res;
}

bool LqWrk::RemoveEvnt(LqEvntHdr* EvntHdr) {
    bool Res = false;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(Evnt == EvntHdr) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqEvntGetFlags(Evnt) & NowExec) {
                Unlock();
                LqThreadYield(); //Wait until worker thread leave read/write handler
                Lock();
                if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            LqWrkRemoveByInterator(this, &i);
            LqWrkUnsetWrkOwner(EvntHdr);
            Res = true;
            break;
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::CloseEvnt(LqEvntHdr* EvntHdr) {
    intptr_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    while(true) {
        LqWrkEnumEvntDo(this, i) {
            if(LqSysPollGetHdrByInterator(&EventChecker, &i) == EvntHdr) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
                if(LqEvntGetFlags(EvntHdr) & NowExec) {
                    Res = -1;
                } else
#endif
                {
                    LqWrkRemoveByInterator(this, &i);
                    LqWrkUnsetWrkOwner(EvntHdr);
                    Res = 1;
                }
                break;
            }
        }LqWrkEnumEvntWhile(this);
        if(Res == 1) {
            LqEvntCallCloseHandler(EvntHdr);
        } else if(Res == -1) {
            LqThreadYield();
            continue;
        }
        break;
    }
    return Res == 1;
}

bool LqWrk::UpdateAllEvntFlagAsync() {
    uintptr_t Expected = 0;
    LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)1);
    NotifyThread();
    return true;
}

int LqWrk::UpdateAllEvntFlagSync() {
    Lock();
    int Res = LqSysPollUpdateAllMask(&EventChecker, this, DelProc, true);
    Unlock();
    return Res;
}

bool LqWrk::AsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData) {
    if(!CommandQueue.PushBegin<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, LqWrkCmdWaitEvnt(AsyncProc, UserData)))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::CancelAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData, bool IsAll) {
    size_t Res = 0;
    for(auto Command = CommandQueue.SeparateBegin(); !CommandQueue.SeparateIsEnd(Command);) {
        switch(Command.Type) {
            case LQWRK_CMD_WAIT_EVENT:
            {
                if((IsAll || (Res == 0)) && (Command.Val<LqWrkCmdWaitEvnt>().EventAct == AsyncProc) && (Command.Val<LqWrkCmdWaitEvnt>().UserData == UserData)) {
                    Command.Pop<LqWrkCmdWaitEvnt>();
                    Res++;
                } else {
                    CommandQueue.SeparatePush(Command);
                }
            }
            break;
            default:
                /* Otherwise return current command in list*/
                CommandQueue.SeparatePush(Command);
        }
    }
    return Res;
}

bool LqWrk::CloseAllEvntAsync() {
    if(!CommandQueue.PushBegin(LQWRK_CMD_CLOSE_ALL_CONN))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::CloseAllEvntSync() {
    size_t Ret = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(LqEvntGetFlags(Evnt) & NowExec) {
            Unlock();
            LqThreadYield();
            Lock();
            if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                goto lblContinue;
        }
#endif
        LqWrkRemoveByInterator(this, &i);
        LqWrkUnsetWrkOwner(Evnt);
        LqWrkCallEvntHdrCloseHandler(Evnt, this);
        Ret++;
lblContinue:;
    }LqWrkEnumEvntWhile(this);
    return Ret;
}

size_t LqWrk::CloseConnByIpSync(const sockaddr* Addr) {
    size_t Res = 0;
    switch(Addr->sa_family) {
        case AF_INET: case AF_INET6: break;
        default: return Res;
    }
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto->CmpAddressProc((LqConn*)Evnt, Addr))) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqEvntGetFlags(Evnt) & NowExec) {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            LqWrkRemoveByInterator(this, &i);
            LqWrkUnsetWrkOwner(Evnt);
            LqWrkCallConnCloseHandler(Evnt, this);
            Res++;
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::CloseConnByIpAsync(const sockaddr* Addr) {
    StartThreadLocker.LockWriteYield();
    switch(Addr->sa_family) {
        case AF_INET:
        {
            LqConnAddr s;
            s.AddrInet = *(sockaddr_in*)Addr;
            if(!CommandQueue.PushBegin<LqConnAddr>(LQWRK_CMD_RM_CONN_BY_IP, s)) {
                StartThreadLocker.UnlockWrite();
                return false;
            }
        }
        break;
        case AF_INET6:
        {
            LqConnAddr s;
            s.AddrInet6 = *(sockaddr_in6*)Addr;
            if(!CommandQueue.PushBegin<LqConnAddr>(LQWRK_CMD_RM_CONN_BY_IP, s)) {
                StartThreadLocker.UnlockWrite();
                return false;
            }
        }
        break;
        default: StartThreadLocker.UnlockWrite(); return false;
    }
    StartThreadLocker.UnlockWrite();
    NotifyThread();
    return true;
}

bool LqWrk::EnumCloseRmEvntAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqEvntHdr*, LqTimeMillisec),
    const LqProto* Proto,
    void* UserData,
    size_t UserDataSize
) {
    LqWrkAsyncEventForAllFd Arg;
    bool Res;
    if((UserDataSize > 0) && (UserData != NULL)) {
        if((Arg.UserData = ___malloc(UserDataSize)) == NULL)
            return false;
        Arg.UserDataSize = UserDataSize;
        memcpy(Arg.UserData, UserData, UserDataSize);
    } else {
        Arg.UserData = NULL;
        Arg.UserDataSize = 0;
    }
    Arg.EventAct = EventAct;
    Arg.Proto = Proto;
    Res = CommandQueue.PushBegin<LqWrkAsyncEventForAllFd>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD, Arg);
    NotifyThread();
    return Res;
}

bool LqWrk::CloseConnByProtoAsync(const LqProto* Proto) {
    StartThreadLocker.LockWriteYield();
    auto Res = CommandQueue.PushBegin<const LqProto*>(LQWRK_CMD_CLOSE_CONN_BY_PROTO, Proto);
    StartThreadLocker.UnlockWrite();
    NotifyThread();
    return Res;
}


size_t LqWrk::CloseConnByProtoSync(const LqProto* Proto) {
    size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqEvntGetFlags(Evnt) & NowExec) {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            LqLogInfo("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
            LqWrkRemoveByInterator(this, &i);
            LqWrkUnsetWrkOwner(Evnt);
            LqWrkCallConnCloseHandler(Evnt, this);
            Res++;
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

size_t LqWrk::EnumCloseRmEvnt(int(LQ_CALL*Proc)(void *, LqEvntHdr*), void* UserData, bool* IsIterrupted) {
    size_t Res = 0;
    int Act = 0;
    LqEvntHdr* Evnt;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(LqEvntGetFlags(Evnt) & NowExec) {
            Unlock();
            LqThreadYield();
            Lock();
            if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                goto lblContinue;
        }
#endif
        Act = Proc(UserData, Evnt); /* !! Not unlocket for safety !! in @Proc not call @LqEvntHdrClose (If you want, use async method or just return 2)*/
        if(Act > 0) {
            Res++;
            LqWrkRemoveByInterator(this, &i);
            LqWrkUnsetWrkOwner(Evnt);
            if(Act > 1)
                LqWrkCallEvntHdrCloseHandler(Evnt, this);
        } else if(Act < 0) {
            break;
        }
lblContinue:;
    }LqWrkEnumEvntWhile(this);
    *IsIterrupted = Act < 0;
    return Res;
}

size_t LqWrk::EnumCloseRmEvntByProto(int(LQ_CALL*Proc)(void *, LqEvntHdr *), const LqProto* Proto, void* UserData, bool* IsIterrupted) {
    size_t Res = 0;
    LqEvntHdr* Evnt;
    int Act = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
            
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqEvntGetFlags(Evnt) & NowExec) {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqSysPollGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            Act = Proc(UserData, Evnt); /* !! Not unlocket for safety !! in @Proc not call @LqEvntHdrClose (If you want, use async method or just return 2)*/
            if(Act > 0) {
                Res++;
                LqWrkRemoveByInterator(this, &i);
                LqWrkUnsetWrkOwner(Evnt);
                if(Act > 1)
                    LqWrkCallConnCloseHandler(Evnt, this);
            } else if(Act < 0) {
                break;
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    *IsIterrupted = Act < 0;
    return Res;
}

LqString LqWrk::DebugInfo() const {
    char Buf[1024];
    size_t ccip = LqSysPollCount(&EventChecker);
    size_t cciq = CountConnectionsInQueue;

    auto CurrentTimeMillisec = LqTimeGetLocMillisec();
    LqFbuf_snprintf(
        Buf,
        sizeof(Buf),
        "--------------\n"
        " Worker Id: %llu\n"
        " Time start: %s (%s)\n"
        " Count conn. & event obj. in process: %u\n"
        " Count conn. & event obj. in queue: %u\n"
        " Common conn. & event obj.: %u\n",
        Id,
        LqTimeLocSecToStlStr(TimeStart / 1000).c_str(),
        LqTimeDiffMillisecToStlStr(TimeStart, CurrentTimeMillisec).c_str(),
        (uint)ccip,
        (uint)cciq,
        (uint)(ccip + cciq)
    );
    return Buf;
}

LqString LqWrk::AllDebugInfo() {
    LqString r =
        "~~~~~~~~~~~~~~\n Common worker info\n" +
        DebugInfo() +
        "--------------\n Thread info\n" +
        LqThreadBase::DebugInfo() +
        "--------------\n";
    ullong CurTime = LqTimeGetLocMillisec();
    int k = 0;
    LqWrkEnumEvntDo(this, i) {
        auto Conn = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Conn)) {
            r += " Conn #" + LqToString(k) + "\n";
            char* DbgInf = ((LqConn*)Conn)->Proto->DebugInfoProc((LqConn*)Conn); /*!!! Not call another worker methods in this function !!!*/
            if(DbgInf != nullptr) {
                r += DbgInf;
                r += "\n";
                free(DbgInf);
            }
            k++;
        }
    }LqWrkEnumEvntWhile(this);
    r += "~~~~~~~~~~~~~~\n";
    return r;
}

size_t LqWrk::GetAssessmentBusy() const { return CountConnectionsInQueue + LqSysPollCount(&EventChecker); }

size_t LqWrk::CountEvnts() const { return LqSysPollCount(&EventChecker); }

void LqWrk::NotifyThread() { LqEventSet(NotifyEvent.Fd); }

