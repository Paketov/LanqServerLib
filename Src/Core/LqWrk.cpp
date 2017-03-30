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
#include "LqLib.h"

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

//#define LQWRK_ENABLE_RW_HNDL_PROTECT


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

#define LqWrkCallConnHandler(Conn, Flags, Wrk) {                            \
    auto Handler = ((LqConn*)(Conn))->Proto->Handler;                       \
    ((LqWrk*)(Wrk))->Unlock();                                              \
    Handler((LqConn*)(Conn), (Flags));                                      \
    ((LqWrk*)(Wrk))->Lock();                                                \
}

#define LqWrkCallConnCloseHandler(Conn, Wrk) {                              \
    auto Handler = ((LqConn*)(Conn))->Proto->CloseHandler;                  \
    LqAtmIntrlkOr(((LqConn*)(Conn))->Flag, _LQEVNT_FLAG_NOW_EXEC);          \
    ((LqWrk*)(Wrk))->Unlock();                                              \
    Handler((LqConn*)(Conn));                                               \
    ((LqWrk*)(Wrk))->Lock();                                                \
}

#define LqWrkCallEvntFdHandler(EvntHdr, Flags, Wrk) {                       \
    auto Handler = ((LqEvntFd*)(EvntHdr))->Handler;                         \
    ((LqWrk*)(Wrk))->Unlock();                                              \
    Handler((LqEvntFd*)(EvntHdr), (Flags));                                 \
    ((LqWrk*)(Wrk))->Lock();                                                \
}

#define LqWrkCallEvntFdCloseHandler(EvntHdr, Wrk) {                         \
    auto Handler = ((LqEvntFd*)(EvntHdr))->CloseHandler;                    \
    LqAtmIntrlkOr(((LqEvntFd*)(EvntHdr))->Flag, _LQEVNT_FLAG_NOW_EXEC);     \
    ((LqWrk*)(Wrk))->Unlock();                                              \
    Handler((LqEvntFd*)(EvntHdr));                                          \
    ((LqWrk*)(Wrk))->Lock();                                                \
}

#define LqWrkCallEvntHdrCloseHandler(Event, Wrk) {                          \
    if(LqClientGetFlags(Event) & _LQEVNT_FLAG_CONN){                        \
        LqWrkCallConnCloseHandler(Event, Wrk);                              \
    }else{                                                                  \
       LqWrkCallEvntFdCloseHandler(Event, Wrk);}                            \
}
/*
* Enum event changes
*  Use only by worker thread
*/
#define LqWrkEnumChangesEvntDo(Wrk, EventFlags)                             \
     {((LqWrk*)(Wrk))->Lock();                                              \
     ((LqWrk*)(Wrk))->DeepLoop++;                                           \
     for(LqEvntFlag EventFlags = __LqSysPollEnumEventBegin(&(((LqWrk*)(Wrk))->EventChecker)); EventFlags != ((LqEvntFlag)0); EventFlags = __LqEvntEnumEventNext(&(((LqWrk*)(Wrk))->EventChecker)))

#define LqWrkEnumChangesEvntWhile(Wrk)                                      \
    if(((--((LqWrk*)(Wrk))->DeepLoop) <= ((intptr_t)0)) &&                  \
        __LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {          \
       __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));   \
    }                                                                       \
    ((LqWrk*)(Wrk))->Unlock();                                              \
 }

/*
* Use when enum by another thread or by worker tread
*/
#define LqWrkEnumEvntDo(Wrk, IndexName) {                                   \
    LqEvntInterator IndexName;                                              \
    ((LqWrk*)(Wrk))->Lock();                                                \
    ((LqWrk*)(Wrk))->AcceptAllEventFromQueue();                             \
    ((LqWrk*)(Wrk))->DeepLoop++;                                            \
    for(auto __r = __LqSysPollEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &(IndexName)); __r; __r = __LqSysPollEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &(IndexName)))


#define LqWrkEnumEvntWhile(Wrk)                                             \
    if(((--((LqWrk*)(Wrk))->DeepLoop) <= ((intptr_t)0)) &&                  \
        __LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {          \
       LqWrkWaiterLock(Wrk);                                                \
       __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));   \
       LqWrkWaiterUnlock(Wrk);                                              \
    }                                                                       \
    ((LqWrk*)(Wrk))->Unlock();                                              \
}

/*
* Use when enum by worker(owner) thread
*/
#define LqWrkEnumEvntOwnerDo(Wrk, IndexName) {                              \
    LqEvntInterator IndexName;                                              \
    ((LqWrk*)(Wrk))->Lock();                                                \
    ((LqWrk*)(Wrk))->DeepLoop++;                                            \
    for(auto __r = __LqSysPollEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqSysPollEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))

#define LqWrkEnumEvntOwnerWhile(Wrk)                                        \
    if(((--((LqWrk*)(Wrk))->DeepLoop) <= ((intptr_t)0)) &&                  \
        __LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {          \
        __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));  \
    }                                                                       \
    ((LqWrk*)(Wrk))->Unlock();                                              \
}

/*
* Custum enum, use in LqWrkBoss::TransferAllEvnt
*/
#define LqWrkEnumEvntNoLkDo(Wrk, IndexName) {                               \
    LqEvntInterator IndexName;                                              \
    ((LqWrk*)(Wrk))->DeepLoop++;                                            \
    for(auto __r = __LqSysPollEnumBegin(&(((LqWrk*)(Wrk))->EventChecker), &IndexName); __r; __r = __LqSysPollEnumNext(&(((LqWrk*)(Wrk))->EventChecker), &IndexName))

#define LqWrkEnumEvntNoLkWhile(Wrk)                                         \
    if(((--((LqWrk*)(Wrk))->DeepLoop) <= ((intptr_t)0)) &&                  \
        __LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {          \
       LqWrkWaiterLock(Wrk);                                                \
       __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));   \
       LqWrkWaiterUnlock(Wrk);                                              \
    }                                                                       \
}

#define LqWrkUpdateAllMaskByOwner(Wrk, DelProc) {                           \
    ((LqWrk*)(Wrk))->Lock();                                                \
    ((LqWrk*)(Wrk))->DeepLoop++;                                            \
    LqSysPollUpdateAllMask(&((LqWrk*)(Wrk))->EventChecker, ((LqWrk*)(Wrk)), (DelProc));\
    ((LqWrk*)(Wrk))->DeepLoop--;                                            \
    ((LqWrk*)(Wrk))->Unlock();                                              \
}

#define LqWrkUpdateAllMask(Wrk, DelProc, Res) {                             \
    ((LqWrk*)(Wrk))->Lock();                                                \
    ((LqWrk*)(Wrk))->AcceptAllEventFromQueue();                             \
    ((LqWrk*)(Wrk))->DeepLoop++;                                            \
    (Res) = LqSysPollUpdateAllMask(&((LqWrk*)(Wrk))->EventChecker, ((LqWrk*)(Wrk)), (DelProc));\
    if(((--((LqWrk*)(Wrk))->DeepLoop) <= ((intptr_t)0)) &&                  \
        __LqSysPollIsRestruct(&(((LqWrk*)(Wrk))->EventChecker))) {          \
        LqWrkWaiterLock(Wrk);                                               \
        __LqSysPollRestructAfterRemoves(&(((LqWrk*)(Wrk))->EventChecker));  \
        LqWrkWaiterUnlock(Wrk);                                             \
    }                                                                       \
    ((LqWrk*)(Wrk))->Unlock(); }

/*
* Wait events, use only by worker thread
*/
#define LqWrkWaitEvntsChanges(Wrk) {                                        \
    LqWrkWaiterLockMain(Wrk);                                               \
    LqSysPollCheck(&((LqWrk*)(Wrk))->EventChecker, LqTimeGetMaxMillisec()); \
    LqWrkWaiterUnlock(Wrk);                                                 \
}

/*
* Remove event by interator
*/
#define LqWrkRemoveByInterator(Wrk, Iter) {                                 \
    LqWrkWaiterLock(Wrk);                                                   \
    LqSysPollRemoveByInterator(&((LqWrk*)(Wrk))->EventChecker, (Iter));     \
    LqWrkWaiterUnlock(Wrk);                                                 \
}

#define LqEvntHdrLock(EvntHdr) LqAtmLkWr(((LqClientHdr*)(EvntHdr))->Lk)
#define LqEvntHdrUnlock(EvntHdr) LqAtmUlkWr(((LqClientHdr*)(EvntHdr))->Lk)

#define LqWrkUnsetWrkOwner(EvntHdr) {                                       \
    LqEvntHdrLock(EvntHdr);                                                 \
    ((LqClientHdr*)(EvntHdr))->WrkOwner = NULL;                             \
    LqEvntHdrUnlock(EvntHdr);                                               \
}

#define LqWrkSetWrkOwner(EvntHdr, NewOwner) {                               \
    LqEvntHdrLock(EvntHdr);                                                 \
    ((LqClientHdr*)(EvntHdr))->WrkOwner = (NewOwner);                       \
    LqEvntHdrUnlock(EvntHdr);                                               \
}

enum {
    LQWRK_CMD_RM_CONN_ON_TIME_OUT,
    LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO,
    LQWRK_CMD_WAIT_EVENT,
    LQWRK_CMD_CLOSE_ALL_CONN,
    LQWRK_CMD_RM_CONN_BY_IP,
    LQWRK_CMD_CLOSE_CONN_BY_PROTO,
    LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD,
    LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN,
    LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD11,
    LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN11,
    LQWRK_CMD_TRANSFER_CLIENTS_AND_DELETE_FROM_BOSS
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
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec);
    void* UsrData;
    size_t UsrDataSize;

    inline LqWrkAsyncEventForAllFd(
        void* UserData,
        size_t UserDataSize,
        int(LQ_CALL*NewEventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec)
    ) {
        if((UserDataSize > ((size_t)0)) && (UserData != NULL)) {
            UsrData = LqMemAlloc(UserDataSize);
            UsrDataSize = UserDataSize;
            memcpy(UsrData, UserData, UserDataSize);
        } else {
            UsrData = NULL;
            UsrDataSize = ((size_t)0);
        }
        EventAct = NewEventAct;
    }
    inline LqWrkAsyncEventForAllFd(LqWrkAsyncEventForAllFd& Src) {
        UsrData = Src.UsrData;
        EventAct = Src.EventAct;
        UsrDataSize = Src.UsrDataSize;
        Src.UsrData = NULL;
    }
    ~LqWrkAsyncEventForAllFd() {
        if(UsrData != NULL)
            LqMemFree(UsrData);
    }
};

typedef struct LqWrkAsyncCallFin {
    uintptr_t(LQ_CALL*EventFin)(void*, size_t);
    void* UsrData;
    size_t UsrDataSize;
    size_t CountPointers;
    LqWrkAsyncCallFin(uintptr_t(LQ_CALL*NewEventFin)(void*, size_t), void* UserData, size_t UserDataSize):
        CountPointers(0),
        EventFin(NewEventFin)
    {
        if((UserDataSize > ((size_t)0)) && (UserData != NULL)) {
            UsrData = LqMemAlloc(UserDataSize);
            UsrDataSize = UserDataSize;
            memcpy(UsrData, UserData, UserDataSize);
        } else {
            UsrData = NULL;
            UsrDataSize = ((size_t)0);
        }
    }
    ~LqWrkAsyncCallFin() {
        uintptr_t MdlHandle = EventFin(UsrData, UsrDataSize);
        if(MdlHandle) {
            LqLibFreeSafe(MdlHandle);
        }
        if(UsrData != NULL)
            LqMemFree(UsrData);
    }
} LqWrkAsyncCallFin;

struct LqWrkAsyncEventForAllFdAndCallFin {
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec);
    void* UsrData;
    size_t UsrDataSize;
    LqShdPtr<LqWrkAsyncCallFin, LqFastAlloc::Delete, true, false, uintptr_t> FinPtr;

    inline LqWrkAsyncEventForAllFdAndCallFin(
        void* UserData,
        size_t UserDataSize,
        int(LQ_CALL*NewEventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
        LqShdPtr<LqWrkAsyncCallFin, LqFastAlloc::Delete, true, false, uintptr_t>& FinPtr
    ): FinPtr(FinPtr) 
    {
        if((UserDataSize > ((size_t)0)) && (UserData != NULL)) {
            UsrData = LqMemAlloc(UserDataSize);
            UsrDataSize = UserDataSize;
            memcpy(UsrData, UserData, UserDataSize);
        } else {
            UsrData = NULL;
            UsrDataSize = ((size_t)0);
        }
        EventAct = NewEventAct;
    }
    inline LqWrkAsyncEventForAllFdAndCallFin(LqWrkAsyncEventForAllFdAndCallFin& Src):
        FinPtr(Src.FinPtr) {
        UsrData = Src.UsrData;
        EventAct = Src.EventAct;
        UsrDataSize = Src.UsrDataSize;
        Src.UsrData = NULL;
    }
    ~LqWrkAsyncEventForAllFdAndCallFin() {
        if(UsrData != NULL)
            LqMemFree(UsrData);
    }
};


typedef struct LqWrkAsyncCallFin11 {
    std::function<uintptr_t()> FinFunc;
    size_t CountPointers;
    LqWrkAsyncCallFin11(std::function<uintptr_t()>& FinFun):
        CountPointers(0),
        FinFunc(FinFun) {
    }
    ~LqWrkAsyncCallFin11() {
        if(uintptr_t MdlHandle = FinFunc()) {
            FinFunc = nullptr;
            LqLibFreeSafe(MdlHandle);
        }
    }
} LqWrkAsyncCallFin11;

struct LqWrkAsyncEventForAllFdAndCallFin11 {
    std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct;
    LqShdPtr<LqWrkAsyncCallFin11, LqFastAlloc::Delete, true, false, uintptr_t> FinPtr;

    inline LqWrkAsyncEventForAllFdAndCallFin11(
        std::function<int(LqWrkPtr&, LqClientHdr*)>& NewEventAct,
        LqShdPtr<LqWrkAsyncCallFin11, LqFastAlloc::Delete, true, false, uintptr_t>& NewFinPtr
    ):EventAct(NewEventAct), FinPtr(NewFinPtr) {}

    inline LqWrkAsyncEventForAllFdAndCallFin11(LqWrkAsyncEventForAllFdAndCallFin11& Src): FinPtr(Src.FinPtr), EventAct(Src.EventAct){}
};

struct LqWrkAsyncEventForAllFd11 {
    std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct;

    inline LqWrkAsyncEventForAllFd11(std::function<int(LqWrkPtr&, LqClientHdr*)>& NewEventAct): EventAct(NewEventAct) {}
    inline LqWrkAsyncEventForAllFd11(LqWrkAsyncEventForAllFd11& Src): EventAct(Src.EventAct){}
};

struct LqWrkDeleteFromBossCmd {
    LqWrkBoss* Boss;
    bool       IsTransfer;

    inline LqWrkDeleteFromBossCmd(LqWrkBoss* NewBoss, bool NewIsTransfer): Boss(NewBoss), IsTransfer(NewIsTransfer) {}
};

#pragma pack(pop)


static uint8_t _EmptyWrk[sizeof(LqWrk)];

static long long __LqWrkInitEmpty() {
    ((LqWrk*)_EmptyWrk)->CountPointers = 20;
    ((LqWrk*)_EmptyWrk)->Id = -1;
    return 1;
}

static long long __IdGen = __LqWrkInitEmpty();
static LqLocker<uintptr_t> __IdGenLocker;

static long long GenId() {
    long long r;
    __IdGenLocker.LockWrite();
    r = __IdGen;
    __IdGen++;
    __IdGenLocker.UnlockWrite();
    return r;
}

LqWrkPtr LqWrk::New(bool IsStart) {
    return LqFastAlloc::New<LqWrk>(IsStart);
}

LqWrkPtr LqWrk::ByEvntHdr(LqClientHdr * EvntHdr) {
    LqAtmLkWr(EvntHdr->Lk);
    LqWrkPtr Res((((LqClientHdr*)(EvntHdr))->WrkOwner != NULL)? ((LqWrk*)((LqClientHdr*)(EvntHdr))->WrkOwner): ((LqWrk*)_EmptyWrk));
    LqAtmUlkWr(EvntHdr->Lk);
    return Res;
}

LqWrkPtr LqWrk::GetNull() {
    return ((LqWrk*)_EmptyWrk);
}

bool LqWrk::IsNull(LqWrkPtr& WrkPtr) {
    return WrkPtr == ((LqWrk*)_EmptyWrk);
}

LQ_IMPORTEXPORT void LQ_CALL LqWrkDelete(LqWrk* This) {
    if(This->IsThisThread()) {
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
        LqClientHdr* Hdr = Command.Val<LqClientHdr*>();
        Command.Pop<LqClientHdr*>();
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
    DeepLoop(intptr_t(0)),
    IsRecurseSkipCommands(false),
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
        StartThreadSync();
}

LqWrk::~LqWrk() {
    EndWorkSync();
    CloseAllClientsSync();
    for(volatile size_t* t = &CountPointers; *t > 0; LqThreadYield());
    for(unsigned i = 0; i < 10; i++) {
        Lock();
        Unlock();
        LqThreadYield();
    }
    LqSysPollUninit(&EventChecker);
    LqFileClose(NotifyEvent.Fd);
}

long long LqWrk::GetId() const {
    return Id;
}

void LqWrk::DelProc(void* Data, LqEvntInterator* Iter) {
    LqClientHdr* Hdr = LqSysPollGetHdrByInterator(&((LqWrk*)Data)->EventChecker, Iter);
    if(LqClientGetFlags(Hdr) & _LQEVNT_FLAG_NOW_EXEC)
        return;
    LqWrkRemoveByInterator(Data, Iter);
    LqWrkUnsetWrkOwner(Hdr);
    LqWrkCallEvntHdrCloseHandler(Hdr, Data);
}

bool LqWrk::CmdIsOnlyOneTypeOfCommandInQueue(LqQueueCmd<uint8_t>::Interator& Inter, uint8_t TypeOfCommand) {
    if(!Inter)
        return true;
    if(Inter.Type != TypeOfCommand)
        return false;
    LqQueueCmd<uint8_t>::Interator Next = Inter.GetSkipped();
    return CmdIsOnlyOneTypeOfCommandInQueue(Next, TypeOfCommand);
}

void LqWrk::ParseInputCommands() {
    /*
    * Recive async command loop.
    */

    {
        auto Command = EvntFdQueue.Fork();
        if(Command) {
            size_t RmHdrsSize = 0;
            LqClientHdr** RmHdrs = NULL;
            Lock();
            do {
                LqClientHdr* Hdr = Command.Val<LqClientHdr*>();
                Command.Pop<LqClientHdr*>();
                CountConnectionsInQueue--;
                if(LqClientGetFlags(Hdr) & LQEVNT_FLAG_END) {
                    RmHdrs = (LqClientHdr**)LqMemRealloc(RmHdrs, (RmHdrsSize + 1) * sizeof(RmHdrs[0]));
                    RmHdrs[RmHdrsSize] = Hdr;
                    RmHdrsSize++;
                } else {
                    LqLogInfo("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, Hdr->Fd, (ullong)Hdr->Flag);
                    LqSysPollAddHdr(&EventChecker, Hdr);
                }
            } while(Command);
            Unlock();
            if(RmHdrs != NULL) {
                for(size_t i = 0; i < RmHdrsSize; i++) {
                    LqWrkUnsetWrkOwner(RmHdrs[i]);
                    LqClientCallCloseHandler(RmHdrs[i]);
                }
                LqMemFree(RmHdrs);
            }
        }
    }

    for(auto Command = CommandQueue.Fork(); Command;) {
        switch(Command.Type) {
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT:
            {
                /*
                * Remove zombie connections by time val.
                */
                auto TimeLiveMilliseconds = Command.Val<LqTimeMillisec>();
                Command.Pop<LqTimeMillisec>();
                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(LqClientIsConn(Evnt) && ((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds)) {
                        LqLogInfo("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                        LqSysPollRemoveByInterator(&EventChecker, &i);
                        LqWrkUnsetWrkOwner(Evnt);
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
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(LqClientIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto) && Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds)) {
                        LqLogInfo("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
                        LqSysPollRemoveByInterator(&EventChecker, &i);
                        LqWrkUnsetWrkOwner(Evnt);
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
                LqWrkCmdWaitEvnt Tmp = Command.Val<LqWrkCmdWaitEvnt>();
                Command.Pop<LqWrkCmdWaitEvnt>();
                Tmp.EventAct(Tmp.UserData);
            }
            break;
            case LQWRK_CMD_CLOSE_ALL_CONN:
            {
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
            case LQWRK_CMD_RM_CONN_BY_IP:
            {
                LqConnAddr TmpVal = Command.Val<LqConnAddr>();
                Command.Pop<LqConnAddr>();
                CloseClientsByIpSync(&TmpVal.Addr);
            }
            break;
            case LQWRK_CMD_CLOSE_CONN_BY_PROTO:
            {
                auto Proto = Command.Val<const LqProto*>();
                Command.Pop<const LqProto*>();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(LqClientIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
                        LqLogInfo("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
                        LqSysPollRemoveByInterator(&EventChecker, &i);
                        LqWrkUnsetWrkOwner(Evnt);
                        LqWrkCallConnCloseHandler(Evnt, this);
                    }
                }LqWrkEnumEvntOwnerWhile(this);
            }
            break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD:
            {
                auto AsyncEvnt = &Command.Val<LqWrkAsyncEventForAllFd>();
                auto CurTime = LqTimeGetLocMillisec();
                LqWrkEnumEvntOwnerDo(this, i) {
                    auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                    if(Evnt != ((LqClientHdr*)&NotifyEvent)) {
                        switch(AsyncEvnt->EventAct(AsyncEvnt->UsrData, AsyncEvnt->UsrDataSize, this, Evnt, CurTime)) {
                            case 1:
                                LqSysPollRemoveByInterator(&EventChecker, &i);
                                LqWrkUnsetWrkOwner(Evnt);
                                break;
                            case 2:
                                LqSysPollRemoveByInterator(&EventChecker, &i);
                                LqWrkUnsetWrkOwner(Evnt);
                                LqWrkCallEvntHdrCloseHandler(Evnt, this);
                                break;
                        }
                    }
                }LqWrkEnumEvntOwnerWhile(this);
                Command.Pop<LqWrkAsyncEventForAllFd>();
            }
            break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN:
            {
                auto AsyncEvnt = &Command.Val<LqWrkAsyncEventForAllFdAndCallFin>();
                if(AsyncEvnt->EventAct != NULL) {
                    auto CurTime = LqTimeGetLocMillisec();
                    LqWrkEnumEvntOwnerDo(this, i) {
                        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                        if(Evnt != ((LqClientHdr*)&NotifyEvent)) {
                            switch(AsyncEvnt->EventAct(AsyncEvnt->UsrData, AsyncEvnt->UsrDataSize, this, Evnt, CurTime)) {
                                case 1:
                                    LqSysPollRemoveByInterator(&EventChecker, &i);
                                    LqWrkUnsetWrkOwner(Evnt);
                                    break;
                                case 2:
                                    LqSysPollRemoveByInterator(&EventChecker, &i);
                                    LqWrkUnsetWrkOwner(Evnt);
                                    LqWrkCallEvntHdrCloseHandler(Evnt, this);
                                    break;
                            }
                        }
                    }LqWrkEnumEvntOwnerWhile(this);
                }
                Command.Pop<LqWrkAsyncEventForAllFdAndCallFin>();
            }
            break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN11:
            {
                auto AsyncEvnt = &Command.Val<LqWrkAsyncEventForAllFdAndCallFin11>();
                LqWrkPtr ThisWrk = this;
                if(AsyncEvnt->EventAct != nullptr) {
                    LqTimeMillisec CurTime = LqTimeGetLocMillisec();
                    LqWrkEnumEvntOwnerDo(this, i) {
                        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                        if(Evnt != ((LqClientHdr*)&NotifyEvent)) {
                            switch(AsyncEvnt->EventAct(ThisWrk, Evnt)) {
                                case 1:
                                    LqSysPollRemoveByInterator(&EventChecker, &i);
                                    LqWrkUnsetWrkOwner(Evnt);
                                    break;
                                case 2:
                                    LqSysPollRemoveByInterator(&EventChecker, &i);
                                    LqWrkUnsetWrkOwner(Evnt);
                                    LqWrkCallEvntHdrCloseHandler(Evnt, this);
                                    break;
                            }
                        }
                    }LqWrkEnumEvntOwnerWhile(this);
                }
                AsyncEvnt->EventAct = nullptr;
                Command.Pop<LqWrkAsyncEventForAllFdAndCallFin11>();
            }
            break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD11:
            {
                auto AsyncEvnt = &Command.Val<LqWrkAsyncEventForAllFd11>();
                LqWrkPtr ThisWrk = this;
                if(AsyncEvnt->EventAct != nullptr) {
                    LqTimeMillisec CurTime = LqTimeGetLocMillisec();
                    LqWrkEnumEvntOwnerDo(this, i) {
                        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
                        if(Evnt != ((LqClientHdr*)&NotifyEvent)) {
                            switch(AsyncEvnt->EventAct(ThisWrk, Evnt)) {
                                case 1:
                                    LqSysPollRemoveByInterator(&EventChecker, &i);
                                    LqWrkUnsetWrkOwner(Evnt);
                                    break;
                                case 2:
                                    LqSysPollRemoveByInterator(&EventChecker, &i);
                                    LqWrkUnsetWrkOwner(Evnt);
                                    LqWrkCallEvntHdrCloseHandler(Evnt, this);
                                    break;
                            }
                        }
                    }LqWrkEnumEvntOwnerWhile(this);
                }
                AsyncEvnt->EventAct = nullptr;
                Command.Pop<LqWrkAsyncEventForAllFd11>();
            }
            break;
            case LQWRK_CMD_TRANSFER_CLIENTS_AND_DELETE_FROM_BOSS:
            {
                const LqWrkPtr* WrkPtrs;
                intptr_t Count, RemoveIndex;
                LqWrkBoss* TargetWrkBoss;
                LqClientHdr** RmHdrs = NULL;
                intptr_t RmHdrsSize = (intptr_t)0;
                intptr_t Busy, Min, Index;
                bool CanDeleteWrkBoss = false;
                bool NeedRecurseClean = true;
                bool IsTransfer;

                TargetWrkBoss = Command.Val<LqWrkDeleteFromBossCmd>().Boss;
                IsTransfer = Command.Val<LqWrkDeleteFromBossCmd>().IsTransfer;
                Command.Pop<LqWrkDeleteFromBossCmd>();
                CommandQueue.InsertBegin(Command);

                if(IsRecurseSkipCommands) {
                    ParseInputCommands();
                    CommandQueue.PushBegin<LqWrkDeleteFromBossCmd>(LQWRK_CMD_TRANSFER_CLIENTS_AND_DELETE_FROM_BOSS, TargetWrkBoss, IsTransfer);
                    return;
                }

                while(true) {

                    /* Lock all worker boss */
                    TargetWrkBoss->Wrks.begin_locket_enum(&WrkPtrs, &Count);
                    RemoveIndex = -((intptr_t)1);
                    for(intptr_t i = 0; i < Count; i++) {
                        if(WrkPtrs[i] == this) {
                            /* Lock all commands queues */
                            auto AllQueueCmd = CommandQueue.LocketFork();
                            auto AllQueueClients = EvntFdQueue.LocketFork();
                            if(CmdIsOnlyOneTypeOfCommandInQueue(AllQueueCmd, LQWRK_CMD_TRANSFER_CLIENTS_AND_DELETE_FROM_BOSS)) {
                                RemoveIndex = i;
                                /* Filter all same commands */
                                LqQueueCmd<uint8_t>::Interator NewQueueCmd;
                                while(AllQueueCmd) {
                                    if(AllQueueCmd.Val<LqWrkDeleteFromBossCmd>().Boss != TargetWrkBoss) {
                                        AllQueueCmd.Move(NewQueueCmd);
                                    } else {
                                        AllQueueCmd.Pop<LqWrkDeleteFromBossCmd>();
                                    }
                                }
                                AllQueueCmd = NewQueueCmd;
                                if(IsTransfer) {
                                    /* Transfer all clients to another workers */
                                    LqWrkEnumEvntOwnerDo(this, j) {
                                        auto Evnt = LqSysPollGetHdrByInterator(&EventChecker, &j);
                                        if(Evnt != ((LqClientHdr*)&NotifyEvent)) {
                                            LqSysPollRemoveByInterator(&EventChecker, &j);
                                            if(Count == 1) {
                                                RmHdrs = (LqClientHdr**)LqMemRealloc(RmHdrs, (RmHdrsSize + 1) * sizeof(RmHdrs[0]));
                                                LqWrkUnsetWrkOwner(Evnt);
                                                RmHdrs[RmHdrsSize] = Evnt;
                                                RmHdrsSize++;
                                            } else {
                                                Min = INTPTR_MAX;
                                                Index = -((intptr_t)1);
                                                for(intptr_t k = 0; k < Count; k++) {
                                                    if(k == i)
                                                        continue;
                                                    Busy = WrkPtrs[k]->GetAssessmentBusy();
                                                    if(Busy < Min)
                                                        Min = Busy, Index = k;
                                                }
                                                WrkPtrs[Index]->AddClientAsync(Evnt);
                                            }
                                        }
                                    }LqWrkEnumEvntOwnerWhile(this);

                                    while(AllQueueClients) {
                                        LqClientHdr* Evnt = AllQueueClients.Val<LqClientHdr*>();
                                        AllQueueClients.Pop<LqClientHdr*>();
                                        CountConnectionsInQueue--;
                                        if(Count == 1) {
                                            RmHdrs = (LqClientHdr**)LqMemRealloc(RmHdrs, (RmHdrsSize + 1) * sizeof(RmHdrs[0]));
                                            LqWrkUnsetWrkOwner(Evnt);
                                            RmHdrs[RmHdrsSize] = Evnt;
                                            RmHdrsSize++;
                                        } else {
                                            Min = INTPTR_MAX;
                                            Index = -((intptr_t)1);
                                            for(intptr_t k = 0; k < Count; k++) {
                                                if(k == i)
                                                    continue;
                                                Busy = WrkPtrs[k]->GetAssessmentBusy();
                                                if(Busy < Min)
                                                    Min = Busy, Index = k;
                                            }
                                            WrkPtrs[Index]->AddClientAsync(Evnt);
                                        }
                                    }
                                }
                                CanDeleteWrkBoss = TargetWrkBoss->NeedDelete && (Count == 1);
                                NeedRecurseClean = false;
                            }
                            EvntFdQueue.LocketUnfork(AllQueueClients);
                            CommandQueue.LocketUnfork(AllQueueCmd);
                            break;
                        }
                    }
                    TargetWrkBoss->Wrks.end_locket_enum(RemoveIndex);
                    if(RmHdrs != NULL) {
                        for(intptr_t i = ((intptr_t)0); i < RmHdrsSize; i++) {
                            LqClientCallCloseHandler(RmHdrs[i]);
                        }
                        LqMemFree(RmHdrs);
                    }
                    if(CanDeleteWrkBoss) {
                        LqFastAlloc::Delete(TargetWrkBoss);
                    }
                    if(NeedRecurseClean) {
                        IsRecurseSkipCommands = true;
                        ParseInputCommands();
                        IsRecurseSkipCommands = false;
                    } else {
                        break;
                    }
                }
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
        auto Evnt = Command.Val<LqClientHdr*>();
        Command.Pop<LqClientHdr*>();
        LqWrkUnsetWrkOwner(Evnt);
        LqClientCallCloseHandler(Evnt);
        CountConnectionsInQueue--;
    }

    for(auto Command = CommandQueue.Fork(); Command;) {
        switch(Command.Type) {
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD: Command.Pop<LqWrkAsyncEventForAllFd>(); break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN: Command.Pop<LqWrkAsyncEventForAllFdAndCallFin>(); break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN11: Command.Pop<LqWrkAsyncEventForAllFdAndCallFin11>(); break;
            case LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD11: Command.Pop<LqWrkAsyncEventForAllFd11>(); break;
            case LQWRK_CMD_TRANSFER_CLIENTS_AND_DELETE_FROM_BOSS: Command.Pop<LqWrkDeleteFromBossCmd>(); break;
            default: Command.JustPop();
        }
    }
}

/*
 Main worker loop
*/
void LqWrk::BeginThread() {
    LqClientHdr* EvntHdr;
    LqEvntFlag OldFlags;
    uintptr_t Expected;
    LqLogInfo("LqWrk::BeginThread()#%llu begin worker thread\n", Id);
#if !defined(LQPLATFORM_WINDOWS)
    signal(SIGPIPE, SIG_IGN);
#endif
    Lock();
    LqSysPollThreadInit(&EventChecker);
    Unlock();

    AddClientSync((LqClientHdr*)&NotifyEvent);
    while(true) {
        if(LqEventReset(NotifyEvent.Fd)) {
            if(LqThreadBase::IsShouldEnd)
                break;
            ParseInputCommands();
            if(LqThreadBase::IsShouldEnd)
                break;
            Expected = (uintptr_t)1;
            if(LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)0)) {
                LqWrkUpdateAllMaskByOwner(this, DelProc);
            }
        }

        LqWrkEnumChangesEvntDo(this, Revent) {
            EvntHdr = LqSysPollGetHdrByCurrent(&EventChecker);
            OldFlags = EvntHdr->Flag;
            LqAtmIntrlkOr(EvntHdr->Flag, _LQEVNT_FLAG_NOW_EXEC);
            if(LqClientIsConn(EvntHdr)) {
                if(Revent & (LQEVNT_FLAG_ERR | LQEVNT_FLAG_WR | LQEVNT_FLAG_RD | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT)) {
                    LqWrkCallConnHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqSysPollGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            } else {
                if(Revent & (LQEVNT_FLAG_ERR | LQEVNT_FLAG_WR | LQEVNT_FLAG_RD | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT)) {
                    LqWrkCallEvntFdHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqSysPollGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            }
            if((Revent & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_END)) || (LqClientGetFlags(EvntHdr) & LQEVNT_FLAG_END)) {
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

    RemoveClient((LqClientHdr*)&NotifyEvent);
    CloseAllClientsSync();
    ClearQueueCommands();
    Lock();
    LqSysPollThreadUninit(&EventChecker);
    Unlock();
    LqLogInfo("LqWrk::BeginThread()#%llu end worker thread\n", Id);
    LqThreadYield();
    LqThreadYield();
}

bool LqWrk::RemoveClientsOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds) {
    if(!CommandQueue.Push<LqTimeMillisec>(LQWRK_CMD_RM_CONN_ON_TIME_OUT, TimeLiveMilliseconds))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveClientsOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds) {
    size_t Res = 0;
    LqClientHdr* Evnt;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqClientIsConn(Evnt)) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqClientGetFlags(Evnt) & NowExec) {
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

bool LqWrk::RemoveClientsOnTimeOutAsync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds) {
    if(!CommandQueue.Push<LqWrkCmdCloseByTimeout>(LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO, Proto, TimeLiveMilliseconds))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveClientsOnTimeOutSync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds) {
    size_t Res = 0;
    LqClientHdr* Evnt;
    LqTimeMillisec CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqClientIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
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

bool LqWrk::AddClientAsync(LqClientHdr* EvntHdr) {
    if(!EvntFdQueue.Push(EvntHdr, this))
        return false;
    CountConnectionsInQueue++;
    NotifyThread();
    return true;
}

bool LqWrk::AddClientSync(LqClientHdr* EvntHdr) {
    bool Res;
    if(LqClientGetFlags(EvntHdr) & LQEVNT_FLAG_END) {
        LqClientCallCloseHandler(EvntHdr);
        return true;
    }

    LqLogInfo("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, EvntHdr->Fd, (unsigned long long)EvntHdr->Flag);
    Lock();
    LqWrkSetWrkOwner(EvntHdr, this);
    LqWrkWaiterLock(this);
    Res = LqSysPollAddHdr(&EventChecker, EvntHdr);
    LqWrkWaiterUnlock(this);
    Unlock();
    return Res;
}

bool LqWrk::RemoveClient(LqClientHdr* EvntHdr) {
    bool Res = false;
    LqClientHdr* Evnt;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        if((Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i)) == EvntHdr) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqClientGetFlags(Evnt) & NowExec) {
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

bool LqWrk::CloseClient(LqClientHdr* EvntHdr) {
    intptr_t Res = ((intptr_t)0);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    while(true) {
        LqWrkEnumEvntDo(this, i) {
            if(LqSysPollGetHdrByInterator(&EventChecker, &i) == EvntHdr) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
                if(LqClientGetFlags(EvntHdr) & NowExec) {
                    Res = -((intptr_t)1);
                } else
#endif
                {
                    LqWrkRemoveByInterator(this, &i);
                    LqWrkUnsetWrkOwner(EvntHdr);
                    Res = ((intptr_t)1);
                }
                break;
            }
        }LqWrkEnumEvntWhile(this);
        if(Res == ((intptr_t)1)) {
            LqClientCallCloseHandler(EvntHdr);
        } else if(Res == -((intptr_t)1)) {
            LqThreadYield();
            continue;
        }
        break;
    }
    return Res == ((intptr_t)1);
}

bool LqWrk::UpdateAllClientFlagAsync() {
    uintptr_t Expected = ((uintptr_t)0);
    LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)1);
    NotifyThread();
    return true;
}

int LqWrk::UpdateAllClientFlagSync() {
    int Res;
    LqWrkUpdateAllMask(this, DelProc, Res);
    return Res;
}

bool LqWrk::AsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData) {
    if(!CommandQueue.Push<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, AsyncProc, UserData))
        return false;
    NotifyThread();
    return true;
}

bool LqWrk::AsyncCall11(std::function<void()> Proc) {
    struct LqAsyncData {
        std::function<void()> Func;

        static void Proc(void* UserData) {
            LqAsyncData* Async = (LqAsyncData*)UserData;
            Async->Func();
            LqFastAlloc::Delete(Async);
        }
    };
    LqAsyncData* Async = LqFastAlloc::New<LqAsyncData>();
    Async->Func = Proc;
    if(AsyncCall(LqAsyncData::Proc, Async))
        return true;
    LqFastAlloc::Delete(Async);
    return false;

}

size_t LqWrk::CancelAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData, bool IsAll) {
    size_t Res = 0;

    auto Command = CommandQueue.LocketFork();
    LqQueueCmd<uint8_t>::Interator NewQueueCmd;
    while(Command) {
        switch(Command.Type) {
            case LQWRK_CMD_WAIT_EVENT:
            {
                if((IsAll || (Res == 0)) && (Command.Val<LqWrkCmdWaitEvnt>().EventAct == AsyncProc) && (Command.Val<LqWrkCmdWaitEvnt>().UserData == UserData)) {
                    Command.Pop<LqWrkCmdWaitEvnt>();
                    Res++;
                } else {
                    Command.Move(NewQueueCmd);
                }
            }
            break;
            default:
                /* Otherwise return current command in list*/
                Command.Move(NewQueueCmd);
        }
    }
    CommandQueue.LocketUnfork(NewQueueCmd);
    return Res;
}

bool LqWrk::CloseAllClientsAsync() {
    if(!CommandQueue.Push(LQWRK_CMD_CLOSE_ALL_CONN))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::CloseAllClientsSync() {
    size_t Ret = ((size_t)0);
    LqClientHdr* Evnt;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(LqClientGetFlags(Evnt) & NowExec) {
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

size_t LqWrk::CloseClientsByIpSync(const sockaddr* Addr) {
    size_t Res = ((size_t)0);
    LqClientHdr* Evnt;
    switch(Addr->sa_family) {
        case AF_INET: case AF_INET6: break;
        default: return Res;
    }
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqClientIsConn(Evnt) && (((LqConn*)Evnt)->Proto->CmpAddressProc((LqConn*)Evnt, Addr))) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqClientGetFlags(Evnt) & NowExec) {
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

bool LqWrk::CloseClientsByIpAsync(const sockaddr* Addr) {
    StartThreadLocker.LockWriteYield();
    switch(Addr->sa_family) {
        case AF_INET:
        {
            LqConnAddr s;
            s.AddrInet = *(sockaddr_in*)Addr;
            if(!CommandQueue.Push<LqConnAddr>(LQWRK_CMD_RM_CONN_BY_IP, s)) {
                StartThreadLocker.UnlockWrite();
                return false;
            }
        }
        break;
        case AF_INET6:
        {
            LqConnAddr s;
            s.AddrInet6 = *(sockaddr_in6*)Addr;
            if(!CommandQueue.Push<LqConnAddr>(LQWRK_CMD_RM_CONN_BY_IP, s)) {
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

bool LqWrk::EnumClientsAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
    void* UserData,
    size_t UserDataSize
) { 
    bool Res;
    Res = CommandQueue.Push<LqWrkAsyncEventForAllFd>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD, UserData, UserDataSize, EventAct);
    NotifyThread();
    return Res;
}

bool LqWrk::EnumClientsAndCallFinAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec), 
    uintptr_t(LQ_CALL*FinFunc)(void*, size_t),
    void * UserData, 
    size_t UserDataSize
) {
    bool Res;
    LqShdPtr<LqWrkAsyncCallFin, LqFastAlloc::Delete, true, false, uintptr_t> FinObject = LqFastAlloc::New<LqWrkAsyncCallFin>(FinFunc, UserData, UserDataSize);
    Res = CommandQueue.Push<LqWrkAsyncEventForAllFdAndCallFin>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN, UserData, UserDataSize, EventAct, FinObject);
    NotifyThread();
    return Res;
}

static uintptr_t LQ_CALL __LqWrkEmptyFin(void*, size_t) { return 0; }

bool LqWrkBoss::EnumClientsAndCallFinAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
    uintptr_t(LQ_CALL*FinFunc)(void*, size_t),
    void * UserData,
    size_t UserDataSize
) const {
    bool Res = false;
    const LqWrkPtr* Arr;
    intptr_t Count;

    LqShdPtr<LqWrkAsyncCallFin, LqFastAlloc::Delete, true, false, uintptr_t> FinObject = LqFastAlloc::New<LqWrkAsyncCallFin>(FinFunc, UserData, UserDataSize);
    Wrks.begin_locket_enum(&Arr, &Count);
    for(intptr_t i = 0; i < Count; i++) {
        Res |= Arr[i]->CommandQueue.Push<LqWrkAsyncEventForAllFdAndCallFin>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN, UserData, UserDataSize, EventAct, FinObject);
        Arr[i]->NotifyThread();
    }
    Wrks.end_locket_enum(-((intptr_t)1));

    if(!Res) {
        FinObject->EventFin = __LqWrkEmptyFin;
    }
    return Res;
}

bool LqWrkBoss::EnumClientsAndCallFinAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct, std::function<uintptr_t()> FinFunc) const {
    bool Res = false;
    const LqWrkPtr* Arr;
    intptr_t Count;

    LqShdPtr<LqWrkAsyncCallFin11, LqFastAlloc::Delete, true, false, uintptr_t> FinObject = LqFastAlloc::New<LqWrkAsyncCallFin11>(FinFunc);
    Wrks.begin_locket_enum(&Arr, &Count);
    for(intptr_t i = 0; i < Count; i++) {
        Res |= Arr[i]->CommandQueue.Push<LqWrkAsyncEventForAllFdAndCallFin11>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN11, EventAct, FinObject);
        Arr[i]->NotifyThread();
    }
    Wrks.end_locket_enum(-((intptr_t)1));

    if(!Res) {
        FinObject->FinFunc = [] { return 0; };
    }
    return Res;
}

bool LqWrkBoss::EnumClientsAndCallFinForMultipleBossAsync(
    LqWrkBoss* Bosses,
    size_t BossesSize,
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
    uintptr_t(LQ_CALL*FinFunc)(void*, size_t), /* You can return module handle for delete them */
    void * UserData,
    size_t UserDataSize
) {
    bool Res = false;
    const LqWrkPtr* Arr;
    intptr_t Count;

    LqShdPtr<LqWrkAsyncCallFin, LqFastAlloc::Delete, true, false, uintptr_t> FinObject = LqFastAlloc::New<LqWrkAsyncCallFin>(FinFunc, UserData, UserDataSize);
    for(intptr_t j = 0; j < BossesSize; j++) {
        Bosses[j].Wrks.begin_locket_enum(&Arr, &Count);
        for(intptr_t i = 0; i < Count; i++) {
            Res |= Arr[i]->CommandQueue.Push<LqWrkAsyncEventForAllFdAndCallFin>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN, UserData, UserDataSize, EventAct, FinObject);
            Arr[i]->NotifyThread();
        }
        Bosses[j].Wrks.end_locket_enum(-((intptr_t)1));
    }
    if(!Res) {
        FinObject->EventFin = __LqWrkEmptyFin;
    }
    return Res;
}

bool LqWrkBoss::EnumClientsAndCallFinForMultipleBossAsync11(LqWrkBoss* Bosses, size_t BossesSize, std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct, std::function<uintptr_t()> FinFunc) const {
    bool Res = false;
    const LqWrkPtr* Arr;
    intptr_t Count;

    LqShdPtr<LqWrkAsyncCallFin11, LqFastAlloc::Delete, true, false, uintptr_t> FinObject = LqFastAlloc::New<LqWrkAsyncCallFin11>(FinFunc);
    for(intptr_t j = 0; j < BossesSize; j++) {
        Bosses[j].Wrks.begin_locket_enum(&Arr, &Count);
        for(intptr_t i = 0; i < Count; i++) {
            Res |= Arr[i]->CommandQueue.Push<LqWrkAsyncEventForAllFdAndCallFin11>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN11, EventAct, FinObject);
            Arr[i]->NotifyThread();
        }
        Bosses[j].Wrks.end_locket_enum(-((intptr_t)1));
    }
    if(!Res) {
        FinObject->FinFunc = [] { return 0; };
    }
    return Res;
}

bool LqWrk::EnumClientsAndCallFinAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct, std::function<uintptr_t()> FinFunc) {
    LqShdPtr<LqWrkAsyncCallFin11, LqFastAlloc::Delete, true, false, uintptr_t> FinObject = LqFastAlloc::New<LqWrkAsyncCallFin11>(FinFunc);
    bool Res = CommandQueue.Push<LqWrkAsyncEventForAllFdAndCallFin11>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD_FIN11, EventAct, FinObject);
    NotifyThread();
    return Res;
}

bool LqWrk::EnumClientsAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct) {
    bool Res = CommandQueue.Push<LqWrkAsyncEventForAllFd11>(LQWRK_CMD_ASYNC_EVENT_FOR_ALL_FD11, EventAct);
    NotifyThread();
    return Res;
}

size_t LqWrk::EnumClients11(std::function<int(LqClientHdr*)> EventAct, bool* IsIterrupted) {
    return EnumClients(
        [](void* UserData, LqClientHdr* Hdr) -> int {
            std::function<int(LqClientHdr*)>* Func = (std::function<int(LqClientHdr*)>*)UserData;
            return Func->operator()(Hdr);
        },
        &EventAct,
        IsIterrupted
    );
}

size_t LqWrk::EnumClientsByProto11(std::function<int(LqClientHdr*)> EventAct, const LqProto* Proto, bool* IsIterrupted) {
    return EnumClientsByProto(
        [](void* UserData, LqClientHdr* Hdr) -> int {
            std::function<int(LqClientHdr*)>* Func = (std::function<int(LqClientHdr*)>*)UserData;
            return Func->operator()(Hdr);
        },
        Proto,
        &EventAct,
        IsIterrupted
    );
}

bool LqWrk::TransferClientsAndRemoveFromBossAsync(LqWrkBoss * TargetBoss, bool IsTransferClients) {
    auto Res = CommandQueue.Push<LqWrkDeleteFromBossCmd>(LQWRK_CMD_TRANSFER_CLIENTS_AND_DELETE_FROM_BOSS, TargetBoss, IsTransferClients);
    NotifyThread();
    return Res;
}

bool LqWrk::CloseClientsByProtoAsync(const LqProto* Proto) {
    StartThreadLocker.LockWriteYield();
    auto Res = CommandQueue.Push<const LqProto*>(LQWRK_CMD_CLOSE_CONN_BY_PROTO, Proto);
    StartThreadLocker.UnlockWrite();
    NotifyThread();
    return Res;
}

size_t LqWrk::CloseClientsByProtoSync(const LqProto* Proto) {
    size_t Res = 0;
    LqClientHdr* Evnt;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqClientIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqClientGetFlags(Evnt) & NowExec) {
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

size_t LqWrk::EnumClients(int(LQ_CALL*Proc)(void *, LqClientHdr*), void* UserData, bool* IsIterrupted) {
    size_t Res = 0;
    int Act = 0;
    bool IsAsyncDel = false;
    LqClientHdr* Evnt;
    LqEvntFlag ExpectedEvntFlag, NewEvntFlag;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
        while(LqClientGetFlags(Evnt) & NowExec) {
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
            if(Act == 3) {
                IsAsyncDel = true;
                ExpectedEvntFlag = LqClientGetFlags(Evnt);
                do {
                    NewEvntFlag = ExpectedEvntFlag | LQEVNT_FLAG_END;
                } while(!LqAtmCmpXchg(Evnt->Flag, ExpectedEvntFlag, NewEvntFlag));
            } else {
                LqWrkRemoveByInterator(this, &i);
                LqWrkUnsetWrkOwner(Evnt);
                if(Act == 2)
                    LqWrkCallEvntHdrCloseHandler(Evnt, this);
            }
        } else if(Act < 0) {
            break;
        }
lblContinue:;
    }LqWrkEnumEvntWhile(this);
    if(IsAsyncDel)
        UpdateAllClientFlagAsync();
    *IsIterrupted = Act < 0;
    return Res;
}

size_t LqWrk::EnumClientsByProto(int(LQ_CALL*Proc)(void *, LqClientHdr *), const LqProto* Proto, void* UserData, bool* IsIterrupted) {
    size_t Res = ((size_t)0);
    LqClientHdr* Evnt;
    int Act = 0;
    bool IsAsyncDel = false;
    LqEvntFlag ExpectedEvntFlag, NewEvntFlag;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
    const LqEvntFlag NowExec = IsThisThread() ? ((LqEvntFlag)0) : _LQEVNT_FLAG_NOW_EXEC;
#endif
    LqWrkEnumEvntDo(this, i) {
        Evnt = LqSysPollGetHdrByInterator(&EventChecker, &i);
        if(LqClientIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto)) {
            
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
            while(LqClientGetFlags(Evnt) & NowExec) {
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
                if(Act == 3) {
                    IsAsyncDel = true;
                    ExpectedEvntFlag = LqClientGetFlags(Evnt);
                    do {
                        NewEvntFlag = ExpectedEvntFlag | LQEVNT_FLAG_END;
                    } while(!LqAtmCmpXchg(Evnt->Flag, ExpectedEvntFlag, NewEvntFlag));
                } else {
                    LqWrkRemoveByInterator(this, &i);
                    LqWrkUnsetWrkOwner(Evnt);
                    if(Act == 2)
                        LqWrkCallEvntHdrCloseHandler(Evnt, this);
                }
            } else if(Act < 0) {
                break;
            }
lblContinue:;
        }
    }LqWrkEnumEvntWhile(this);
    if(IsAsyncDel)
        UpdateAllClientFlagAsync();
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
        if(LqClientIsConn(Conn)) {
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

size_t LqWrk::CountClients() const { return LqSysPollCount(&EventChecker); }

void LqWrk::NotifyThread() { LqEventSet(NotifyEvent.Fd); }

