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
* This macro used for some platform kind LqEvntCheck functions
*  When used, never add/remove to/from event list, while worker thread is in LqEvntCheck function
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

#define LqWrkCallConnCloseHandler(Conn, Wrk)                           \
{                                                                      \
    auto Handler = ((LqConn*)(Conn))->Proto->CloseHandler;             \
    ((LqConn*)(Conn))->Flag |= _LQEVNT_FLAG_NOW_EXEC;                  \
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqConn*)(Conn));                                          \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallEvntFdHandler(EvntHdr, Flags, Wrk)                    \
{                                                                      \
    auto Handler = ((LqEvntFd*)(EvntHdr))->Handler;                    \
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqEvntFd*)(EvntHdr), (Flags));                            \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallEvntFdCloseHandler(EvntHdr, Wrk)                      \
{                                                                      \
    auto Handler = ((LqEvntFd*)(EvntHdr))->CloseHandler;               \
    ((LqEvntFd*)(EvntHdr))->Flag |= _LQEVNT_FLAG_NOW_EXEC;             \
    ((LqWrk*)(Wrk))->Unlock();                                         \
    Handler((LqEvntFd*)(EvntHdr));                                     \
    ((LqWrk*)(Wrk))->Lock();                                           \
}

#define LqWrkCallEvntHdrCloseHandler(Event, Wrk)                       \
{                                                                      \
    if(((LqEvntHdr*)(Event))->Flag & _LQEVNT_FLAG_CONN){               \
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
     for(LqEvntFlag EventFlags = __LqEvntEnumEventBegin(&(((LqWrk*)(Wrk))->EventChecker)); EventFlags != 0; EventFlags = __LqEvntEnumEventNext(&(((LqWrk*)(Wrk))->EventChecker)))

#define LqWrkEnumChangesEvntWhile(Wrk)                                 \
 if(__LqEvntIsRestruct(&(((LqWrk*)(Wrk))->EventChecker)))              \
    __LqEvntRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));    \
    ((LqWrk*)(Wrk))->Unlock();                                         \
 }

/*
* Use when enum by another thread or by worker tread
*/
#define LqWrkEnumEvntDo(Wrk, IndexName)                                \
 {                                                                     \
    LqEvntInterator IndexName;                                         \
    ((LqWrk*)(Wrk))->Lock();                                           \
    for(auto __r = __LqEvntEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqEvntEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))


#define LqWrkEnumEvntWhile(Wrk)                                        \
    if(__LqEvntIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {         \
       LqWrkWaiterLock(Wrk);                                           \
        __LqEvntRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));\
       LqWrkWaiterUnlock(Wrk);                                         \
    }                                                                  \
    ((LqWrk*)(Wrk))->Unlock();                                         \
}

/*
* Use when enum by worker(owner) thread
*/
#define LqWrkEnumEvntOwnerDo(Wrk, IndexName)                           \
 {                                                                     \
    LqEvntInterator IndexName;                                         \
    ((LqWrk*)(Wrk))->Lock();                                           \
    for(auto __r = __LqEvntEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqEvntEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))

#define LqWrkEnumEvntOwnerWhile(Wrk)                                   \
    if(__LqEvntIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {         \
    __LqEvntRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));    \
    }                                                                  \
    ((LqWrk*)(Wrk))->Unlock();                                         \
}

/*
* Custum enum, use in LqWrkBoss::TransferAllEvnt
*/
#define LqWrkEnumEvntNoLkDo(Wrk, IndexName)                            \
 {                                                                     \
    LqEvntInterator IndexName;                                         \
    for(auto __r = __LqEvntEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqEvntEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))

#define LqWrkEnumEvntNoLkWhile(Wrk)                                    \
    if(__LqEvntIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {         \
    LqWrkWaiterLock(Wrk);                                              \
    __LqEvntRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));    \
    LqWrkWaiterUnlock(Wrk);                                            \
    }                                                                  \
}

/*
* Wait events, use only by worker thread
*/
#define LqWrkWaitEvntsChanges(Wrk) {                                   \
    LqWrkWaiterLockMain(Wrk);                                          \
    LqEvntCheck(&((LqWrk*)(Wrk))->EventChecker, LqTimeGetMaxMillisec());\
    LqWrkWaiterUnlock(Wrk);                                            \
}

/*
* Remove event by interator
*/
#define LqWrkRemoveByInterator(Wrk, Iter) {                            \
    LqWrkWaiterLock(Wrk);                                              \
    LqEvntRemoveByInterator(&((LqWrk*)(Wrk))->EventChecker, Iter);     \
    LqWrkWaiterUnlock(Wrk);                                            \
}

enum
{
    LQWRK_CMD_ADD_CONN,                     /*Add connection to work*/
    LQWRK_CMD_RM_CONN_ON_TIME_OUT,          /*Signal for close all time out connections*/
    LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO,
    LQWRK_CMD_WAIT_EVENT,
    LQWRK_CMD_CLOSE_ALL_CONN,
    LQWRK_CMD_RM_CONN_BY_IP,
    LQWRK_CMD_CLOSE_CONN_BY_PROTO
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqWrkCmdWaitEvnt
{
    void(*EventAct)(void* Data);
    void* UserData;
    inline LqWrkCmdWaitEvnt() {};
    inline LqWrkCmdWaitEvnt(void(*NewEventProc)(void* Data), void* NewUserData): EventAct(NewEventProc), UserData(NewUserData) {}
};

struct LqWrkCmdCloseByTimeout
{
    LqTimeMillisec TimeLive;
    const LqProto* Proto;
    inline LqWrkCmdCloseByTimeout() {};
    inline LqWrkCmdCloseByTimeout(const LqProto* NewProto, LqTimeMillisec Millisec): Proto(NewProto), TimeLive(Millisec) {}
};

#pragma pack(pop)

static ullong __IdGen = 1;
static LqLocker<uchar> __IdGenLocker;

static ullong GenId()
{
    ullong r;
    __IdGenLocker.LockWrite();
    r = __IdGen;
    __IdGen++;
    __IdGenLocker.UnlockWrite();
    return r;
}

LqWrkPtr LqWrk::New(bool IsStart)
{
    return LqFastAlloc::New<LqWrk>(IsStart);
}

LQ_IMPORTEXPORT void LQ_CALL LqWrkDelete(LqWrk* This)
{
    if(This->IsThisThread())
    {
        LqWrkBoss::GetGlobal()->TransferAllEvnt(This);
        This->ClearQueueCommands();
        This->IsDelete = true;
        This->EndWorkAsync();
        return;
    }
    LqFastAlloc::Delete(This);
}

void LqWrk::ExitHandlerFn(void * Data)
{
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
    auto NotifyFd = LqFileEventCreate(LQ_O_NOINHERIT);
    LqEvntFdInit(&NotifyEvent, NotifyFd, LQEVNT_FLAG_RD);
    LqEvntFdIgnoreHandler(&NotifyEvent);
    LqEvntFdIgnoreCloseHandler(&NotifyEvent);
    LqEvntInit(&EventChecker);
    if(IsStart)
        StartSync();
}

LqWrk::~LqWrk()
{
    EndWorkSync();
    CloseAllEvntSync();
    LqEvntUninit(&EventChecker);
    LqFileClose(NotifyEvent.Fd);
}

size_t LqWrkBoss::TransferAllEvnt(LqWrk* Source)
{
    std::vector<LqEvntHdr*> RmHdrs;
    size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = Source->IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    Source->Lock();
    auto NotifyEvntHdr = (LqEvntHdr*)&Source->NotifyEvent;
    LqWrkEnumEvntNoLkDo(Source, i)
    {
        auto Hdr = LqEvntGetHdrByInterator(&Source->EventChecker, &i);
        if(Hdr == NotifyEvntHdr)
            continue;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(Hdr->Flag & NowExec)
        {
            Source->Unlock();
            LqThreadYield();
            Source->Lock();
            if(LqEvntGetHdrByInterator(&Source->EventChecker, &i) != Hdr)
                goto lblContinue;
        }
#endif
        {
            intptr_t Min = INTPTR_MAX, Index = -1;
            auto LocWrks = Wrks.begin();
            for(size_t i = 0, m = LocWrks.size(); i < m; i++)
            {
                if(LocWrks[i] == Source)
                    continue;
                intptr_t Busy = LocWrks[i]->GetAssessmentBusy();
                if(Busy < Min)
                    Min = Busy, Index = i;
            }
            LqWrkRemoveByInterator(Source, &i);
            if((Index != -1) && LocWrks[Index]->AddEvntAsync(Hdr))
            {
                Res++;
            } else
            {
                RmHdrs.push_back(Hdr);
                LQ_ERR("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
            }
        }
lblContinue:;
    }LqWrkEnumEvntNoLkWhile(Source);

    for(auto Command = Source->CommandQueue.SeparateBegin(); !Source->CommandQueue.SeparateIsEnd(Command);)
    {
        switch(Command.Type)
        {
            case LQWRK_CMD_ADD_CONN:
            {
                auto Hdr = Command.Val<LqEvntHdr*>();
                Command.Pop<LqEvntHdr*>();
                Source->CountConnectionsInQueue--;
                auto LocWrks = Wrks.begin();
                intptr_t Min = INTPTR_MAX, Index;
                for(size_t i = 0, m = LocWrks.size(); i < m; i++)
                {
                    if(LocWrks[i] == Source)
                        continue;
                    intptr_t Busy = LocWrks[i]->GetAssessmentBusy();
                    if(Busy < Min)
                        Min = Busy, Index = i;
                }

                if((Index != -1) && LocWrks[Index]->AddEvntAsync(Hdr))
                {
                    Res++;
                } else
                {
                    RmHdrs.push_back(Hdr);
                    LQ_ERR("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
                }
            }
            break;
            default:
                /* Otherwise return current command in list*/
                Source->CommandQueue.SeparatePush(Command);
        }
    }
    Source->Unlock();
    for(auto i : RmHdrs)
    {
        LqEvntHdrClose(i);
    }
    return Res;
}


ullong LqWrk::GetId() const
{
    return Id;
}

void LqWrk::DelProc(void* Data, LqEvntInterator* Iter)
{
    auto Hdr = LqEvntGetHdrByInterator(&((LqWrk*)Data)->EventChecker, Iter);
    if(Hdr->Flag & _LQEVNT_FLAG_NOW_EXEC)
        return;
    LqWrkRemoveByInterator(Data, Iter);
    LqWrkCallEvntHdrCloseHandler(Hdr, Data);
}

void LqWrk::ParseInputCommands()
{
    /*
    * Recive async command loop.
    */
    for(auto Command = CommandQueue.Fork(); Command;)
    {
        switch(Command.Type)
        {
            case LQWRK_CMD_ADD_CONN:
            {
                /*
                * Adding new connection.
                */
                LqEvntHdr* Connection = Command.Val<LqEvntHdr*>();
                Command.Pop<LqEvntHdr*>();
                CountConnectionsInQueue--;
                if(Connection->Flag & LQEVNT_FLAG_END)
                {
                    LqEvntHdrClose(Connection);
                    break;
                }
                LQ_LOG_DEBUG("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, Connection->Fd, (ullong)Connection->Flag);
                Lock();
                LqEvntAddHdr(&EventChecker, Connection);
                Unlock();
            }
            break;
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT:
            {
                /*
                * Remove zombie connections by time val.
                */
                auto TimeLiveMilliseconds = Command.Val<LqTimeMillisec>();
                Command.Pop<LqTimeMillisec>();
                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i)
                {
                    auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
                    if(LqEvntIsConn(Evnt) && ((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
                    {
                        LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                        LqEvntRemoveByInterator(&EventChecker, &i);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO:
            {
                auto TimeLiveMilliseconds = Command.Val<LqWrkCmdCloseByTimeout>().TimeLive;
                auto Proto = Command.Val<LqWrkCmdCloseByTimeout>().Proto;
                Command.Pop<LqWrkCmdCloseByTimeout>();

                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i)
                {
                    auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
                    if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto) && Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
                    {
                        LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                        LqEvntRemoveByInterator(&EventChecker, &i);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_WAIT_EVENT:
            {
                /*
                Call procedure for wait event.
                */
                Command.Val<LqWrkCmdWaitEvnt>().EventAct(Command.Val<LqWrkCmdWaitEvnt>().UserData);
                Command.Pop<LqWrkCmdWaitEvnt>();
            }
            break;
            case LQWRK_CMD_CLOSE_ALL_CONN:
            {
                /*
                Close all waiting connections.
                */
                Command.Pop();
                LqWrkEnumEvntOwnerDo(this, i)
                {
                    auto Evnt = LqEvntRemoveByInterator(&EventChecker, &i);
                    LqWrkCallEvntHdrCloseHandler(Evnt, this);
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_RM_CONN_BY_IP:
            {
                CloseConnByIpSync(&Command.Val<LqConnInetAddress>().Addr);
                Command.Pop<LqConnInetAddress>();
            }
            break;
            case LQWRK_CMD_CLOSE_CONN_BY_PROTO:
            {
                auto Proto = Command.Val<const LqProto*>();
                Command.Pop<const LqProto*>();
                LqWrkEnumEvntOwnerDo(this, i)
                {
                    auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
                    if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto))
                    {
                        LQ_LOG_USER("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
                        LqEvntRemoveByInterator(&EventChecker, &i);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
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

void LqWrk::ClearQueueCommands()
{
    for(auto Command = CommandQueue.Fork(); Command;)
    {
        switch(Command.Type)
        {
            case LQWRK_CMD_ADD_CONN:
            {
                auto Evnt = Command.Val<LqEvntHdr*>();
                Command.Pop<LqEvntHdr*>();
                if(LqWrkBossAddEvntAsync(Evnt) == -1)
                    LqEvntHdrClose(Evnt);
                CountConnectionsInQueue--;
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
void LqWrk::BeginThread()
{
    LQ_LOG_DEBUG("LqWrk::BeginThread()#%llu start worker thread\n", Id);
#if !defined(LQPLATFORM_WINDOWS)
    signal(SIGPIPE, SIG_IGN);
#endif
    Lock();
    LqEvntThreadInit(&EventChecker);
    LqEvntAddHdr(&EventChecker, (LqEvntHdr*)&NotifyEvent);
    Unlock();
    while(true)
    {
        if(LqFileEventReset(NotifyEvent.Fd))
        {
            if(LqThreadBase::IsShouldEnd)
                break;
            ParseInputCommands();
            if(LqThreadBase::IsShouldEnd)
                break;
            uintptr_t Expected = 1;
            if(LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)0))
            {
                Lock();
                LqEvntUpdateAllMask(&EventChecker, this, DelProc, false);
                Unlock();
            }
        }

        LqWrkEnumChangesEvntDo(this, Revent)
        {
            auto EvntHdr = LqEvntGetHdrByCurrent(&EventChecker);
            auto OldFlags = EvntHdr->Flag;
            EvntHdr->Flag |= _LQEVNT_FLAG_NOW_EXEC;
            if(LqEvntIsConn(EvntHdr))
            {
                if(Revent & (LQEVNT_FLAG_ERR | LQEVNT_FLAG_WR | LQEVNT_FLAG_RD | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT))
                {
                    LqWrkCallConnHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqEvntGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            } else
            {
                if(Revent & (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_ERR | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT))
                {
                    LqWrkCallEvntFdHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqEvntGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            }
            if((Revent & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_END)) || (EvntHdr->Flag & LQEVNT_FLAG_END))
            {
                LqEvntRemoveCurrent(&EventChecker);
                LqWrkCallEvntHdrCloseHandler(EvntHdr, this);
            } else
            {
                EvntHdr->Flag &= ~(_LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_USER_SET);
                if(EvntHdr->Flag != OldFlags) //If have event changes
                    LqEvntSetMaskByCurrent(&EventChecker);
                LqEvntUnuseCurrent(&EventChecker);
            }
        }LqWrkEnumChangesEvntWhile(this);

        LqWrkWaitEvntsChanges(this);
    }

    RemoveEvnt((LqEvntHdr*)&NotifyEvent);
    LqWrkBoss::GetGlobal()->TransferAllEvnt(this);
    CloseAllEvntSync();
    ClearQueueCommands();
    Lock();
    LqEvntThreadUninit(&EventChecker);
    Unlock();
    LQ_LOG_DEBUG("LqWrk::BeginThread()#%llu end worker thread\n", Id);
}


bool LqWrk::RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds)
{
    if(!CommandQueue.PushBegin(LQWRK_CMD_RM_CONN_ON_TIME_OUT, TimeLiveMilliseconds))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds)
{
    size_t Res = 0;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt))
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Evnt->Flag & NowExec)
            {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            if(((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
            {
                LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                LqWrkRemoveByInterator(this, &i);
                LqWrkCallConnCloseHandler(Evnt, this);
                Res++;
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::RemoveConnOnTimeOutAsync(const LqProto * Proto, LqTimeMillisec TimeLiveMilliseconds)
{
    if(!CommandQueue.PushBegin<LqWrkCmdCloseByTimeout>(LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO, LqWrkCmdCloseByTimeout(Proto, TimeLiveMilliseconds)))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveConnOnTimeOutSync(const LqProto * Proto, LqTimeMillisec TimeLiveMilliseconds)
{
    size_t Res = 0;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto))
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Evnt->Flag & NowExec)
            {
                Unlock();
                LqThreadYield(); //Wait until worker thread leave read/write handler
                Lock();
                if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            if(Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
            {
                LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                LqWrkRemoveByInterator(this, &i);
                LqWrkCallConnCloseHandler(Evnt, this);
                Res++;
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::AddEvntAsync(LqEvntHdr* EvntHdr)
{
    if(IsThisThread())
        return AddEvntSync(EvntHdr);
    if(!CommandQueue.Push(LQWRK_CMD_ADD_CONN, EvntHdr))
        return false;
    CountConnectionsInQueue++;
    NotifyThread();
    return true;
}

bool LqWrk::AddEvntSync(LqEvntHdr* EvntHdr)
{
    if(EvntHdr->Flag & LQEVNT_FLAG_END)
    {
        LqEvntHdrClose(EvntHdr);
        return true;
    }
    LQ_LOG_DEBUG("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, EvntHdr->Fd, (ullong)EvntHdr->Flag);
    Lock();
    LqWrkWaiterLock(this);
    auto Res = LqEvntAddHdr(&EventChecker, EvntHdr);
    LqWrkWaiterUnlock(this);
    Unlock();
    return Res;
}

bool LqWrk::RemoveEvnt(LqEvntHdr* EvntHdr)
{
    bool Res = false;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(Evnt == EvntHdr)
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Evnt->Flag & NowExec)
            {
                Unlock();
                LqThreadYield(); //Wait until worker thread leave read/write handler
                Lock();
                if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            LqWrkRemoveByInterator(this, &i);
            Res = true;
            break;
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::CloseEvnt(LqEvntHdr* EvntHdr)
{
    intptr_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    while(true)
    {
        LqWrkEnumEvntDo(this, i)
        {
            if(LqEvntGetHdrByInterator(&EventChecker, &i) == EvntHdr)
            {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
                if(EvntHdr->Flag & NowExec)
                {
                    Res = -1;
                } else
#endif
                {
                    LqWrkRemoveByInterator(this, &i);
                    Res = 1;
                }
                break;
            }
        }LqWrkEnumEvntWhile(this);
        if(Res == 1)
        {
            LqEvntHdrClose(EvntHdr);
        } else if(Res == -1)
        {
            LqThreadYield();
            continue;
        }
        break;
    }
    return Res == 1;
}

bool LqWrk::UpdateAllEvntFlagAsync()
{
    uintptr_t Expected = 0;
    LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)1);
    NotifyThread();
    return true;
}

int LqWrk::UpdateAllEvntFlagSync()
{
    Lock();
    auto Res = LqEvntUpdateAllMask(&EventChecker, this, DelProc, true);
    Unlock();
    return Res;
}

bool LqWrk::AsyncCall(void(*AsyncProc)(void* Data), void* UserData)
{
    if(!CommandQueue.PushBegin<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, LqWrkCmdWaitEvnt(AsyncProc, UserData)))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::CancelAsyncCall(void(*AsyncProc)(void *Data), void* UserData, bool IsAll)
{
    size_t Res = 0;
    for(auto Command = CommandQueue.SeparateBegin(); !CommandQueue.SeparateIsEnd(Command);)
    {
        switch(Command.Type)
        {
            case LQWRK_CMD_ADD_CONN:
            {
                if((IsAll || (Res == 0)) && (Command.Val<LqWrkCmdWaitEvnt>().EventAct == AsyncProc) && (Command.Val<LqWrkCmdWaitEvnt>().UserData == UserData))
                {
                    Command.Pop<LqWrkCmdWaitEvnt>();
                    Res++;
                } else
                {
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

bool LqWrk::CloseAllEvntAsync()
{
    if(!CommandQueue.PushBegin(LQWRK_CMD_CLOSE_ALL_CONN))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::CloseAllEvntSync()
{
    size_t Ret = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(Evnt->Flag & NowExec)
        {
            Unlock();
            LqThreadYield();
            Lock();
            if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
                goto lblContinue;
        }
#endif
        LqWrkRemoveByInterator(this, &i);
        LqWrkCallEvntHdrCloseHandler(Evnt, this);
        Ret++;
lblContinue:;
    }LqWrkEnumEvntWhile(this);
    return Ret;
}

size_t LqWrk::CloseConnByIpSync(const sockaddr* Addr)
{
    size_t Res = 0;
    switch(Addr->sa_family)
    {
        case AF_INET: case AF_INET6: break;
        default: return Res;
    }
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto->CmpAddressProc((LqConn*)Evnt, Addr)))
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Evnt->Flag & NowExec)
            {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            LqWrkRemoveByInterator(this, &i);
            LqWrkCallConnCloseHandler(Evnt, this);
            Res++;
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

bool LqWrk::CloseConnByIpAsync(const sockaddr* Addr)
{
    StartThreadLocker.LockWriteYield();
    switch(Addr->sa_family)
    {
        case AF_INET:
        {
            LqConnInetAddress s;
            s.AddrInet = *(sockaddr_in*)Addr;
            if(!CommandQueue.PushBegin<LqConnInetAddress>(LQWRK_CMD_RM_CONN_BY_IP, s))
            {
                StartThreadLocker.UnlockWrite();
                return false;
            }
        }
        break;
        case AF_INET6:
        {
            LqConnInetAddress s;
            s.AddrInet6 = *(sockaddr_in6*)Addr;
            if(!CommandQueue.PushBegin<LqConnInetAddress>(LQWRK_CMD_RM_CONN_BY_IP, s))
            {
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

bool LqWrk::CloseConnByProtoAsync(const LqProto * Proto)
{
    StartThreadLocker.LockWriteYield();
    auto Res = CommandQueue.PushBegin<const LqProto*>(LQWRK_CMD_CLOSE_CONN_BY_PROTO, Proto);
    StartThreadLocker.UnlockWrite();
    NotifyThread();
    return Res;
}

size_t LqWrk::CloseConnByProtoSync(const LqProto * Proto)
{
    size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto))
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Evnt->Flag & NowExec)
            {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
                    goto lblContinue;
            }
#endif
            LQ_LOG_USER("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
            LqWrkRemoveByInterator(this, &i);
            LqWrkCallConnCloseHandler(Evnt, this);
            Res++;
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

size_t LqWrk::EnumCloseRmEvnt(unsigned(*Proc)(void *UserData, LqEvntHdr* Conn), void* UserData)
{
    size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
        unsigned Act;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(Hdr->Flag & NowExec)
        {
            Unlock();
            LqThreadYield();
            Lock();
            if(LqEvntGetHdrByInterator(&EventChecker, &i) != Hdr)
                goto lblContinue;
        }
#endif
        Act = Proc(UserData, Hdr); /* !! Not unlocket for safety !! in @Proc not call @LqEvntHdrClose (If you want, use async method or just return 2)*/
        if(Act > 0)
        {
            Res++;
            LqWrkRemoveByInterator(this, &i);
            if(Act > 1)
                LqWrkCallEvntHdrCloseHandler(Hdr, this);
        }
lblContinue:;
    }LqWrkEnumEvntWhile(this);
    return Res;
}

size_t LqWrk::EnumCloseRmEvntByProto(unsigned(*Proc)(void *UserData, LqEvntHdr *Conn), const LqProto* Proto, void* UserData)
{
    size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i)
    {
        auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Hdr) && (((LqConn*)Hdr)->Proto == Proto))
        {
            unsigned Act;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(Hdr->Flag & NowExec)
            {
                Unlock();
                LqThreadYield();
                Lock();
                if(LqEvntGetHdrByInterator(&EventChecker, &i) != Hdr)
                    goto lblContinue;
            }
#endif
            Act = Proc(UserData, Hdr); /* !! Not unlocket for safety !! in @Proc not call @LqEvntHdrClose (If you want, use async method or just return 2)*/
            if(Act > 0)
            {
                Res++;
                LqWrkRemoveByInterator(this, &i);
                if(Act > 1)
                    LqWrkCallConnCloseHandler(Hdr, this);
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    return Res;
}

LqString LqWrk::DebugInfo() const
{
    char Buf[1024];
    size_t ccip = LqEvntCount(&EventChecker);
    size_t cciq = CountConnectionsInQueue;

    auto CurrentTimeMillisec = LqTimeGetLocMillisec();
    sprintf
    (
        Buf,
        "--------------\n"
        " Worker Id: %llu\n"
        " Time start: %s (%s)\n"
        " Count conn. & event obj. in process: %u\n"
        " Count conn. & event obj. in queue: %u\n"
        " Common conn. & event obj.: %u\n",
        Id,
        LqTimeLocSecToStlStr(CurrentTimeMillisec / 1000).c_str(),
        LqTimeDiffMillisecToStlStr(TimeStart, CurrentTimeMillisec).c_str(),
        (uint)ccip,
        (uint)cciq,
        (uint)(ccip + cciq)
    );
    return Buf;
}

LqString LqWrk::AllDebugInfo()
{
    LqString r =
        "~~~~~~~~~~~~~~\n Common worker info\n" +
        DebugInfo() +
        "--------------\n Thread info\n" +
        LqThreadBase::DebugInfo() +
        "--------------\n";
    ullong CurTime = LqTimeGetLocMillisec();
    int k = 0;
    LqWrkEnumEvntDo(this, i)
    {
        auto Conn = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Conn))
        {
            r += " Conn #" + LqToString(k) + "\n";
            char* DbgInf = ((LqConn*)Conn)->Proto->DebugInfoProc((LqConn*)Conn); /*!!! Not call another worker methods in this function !!!*/
            if(DbgInf != nullptr)
            {
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

size_t LqWrk::GetAssessmentBusy() const { return CountConnectionsInQueue + LqEvntCount(&EventChecker); }

size_t LqWrk::CountEvnts() const { return LqEvntCount(&EventChecker); }

void LqWrk::NotifyThread() { LqFileEventSet(NotifyEvent.Fd); }

