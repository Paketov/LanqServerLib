#pragma once

/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrk (LanQ WoRKer) - Worker class.
*  Recive and handle command from LqWrkBoss.
*  Work with connections, call protocol procedures.
*/

class LqWrk;
class LqWrkBoss;


#include "LqSysPoll.h"
#include "LqLock.hpp"
#include "LqQueueCmd.hpp"
#include "LqThreadBase.hpp"
#include "LqShdPtr.hpp"
#include "LqAlloc.hpp"
#include "LqDef.hpp"
#include "Lanq.h"
#include "LqDfltRef.hpp"


LQ_IMPORTEXPORT void LQ_CALL LqWrkDelete(LqWrk* This);

typedef LqShdPtr<LqWrk, LqWrkDelete, false, false> LqWrkPtr;
static llong __LqWrkInitEmpty();

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrk: public LqThreadBase {

    friend LqWrkBoss;
    friend LqConn;
    friend LqWrkPtr;
    friend LqFastAlloc;
    friend void LqWrkDelete(LqWrk* This);

    friend llong __LqWrkInitEmpty();
    /*GetCount external reference, used in SharedPtr*/
    size_t                                              CountPointers;

    /* GetCount waiting connections */
    LqAtomic<size_t>                                    CountConnectionsInQueue;

    LqQueueCmd<uchar>                                   EvntFdQueue;
    LqQueueCmd<uchar>                                   CommandQueue;
    LqSysPoll                                           EventChecker;
    LqEvntFd                                            NotifyEvent;

    mutable LqLocker<uintptr_t>                         Locker;
    mutable LqLocker<uintptr_t>                         WaitLocker;

    llong                                               Id;
    LqTimeMillisec                                      TimeStart;
    bool                                                IsDelete;
    uintptr_t                                           IsSyncAllFlags;

    
    virtual void BeginThread();
    virtual void NotifyThread();

    inline void Unlock() { Locker.UnlockWrite(); }
    inline void Lock() { Locker.LockWriteYield(); }

    inline void WaiterLockMain() { WaitLocker.LockWrite(); }
    inline void WaiterLock() {
        while(!WaitLocker.TryLockWrite()) {
            NotifyThread();
            for(uintptr_t i = 0; i < 50; i++)
                if(WaitLocker.TryLockWrite())
                    return;
            if(IsThreadEnd())
                break;
        }
    }
    inline void WaiterUnlock() { WaitLocker.UnlockWrite(); }

    void ClearQueueCommands();
    void ParseInputCommands();
    void AcceptAllEventFromQueue();

    static void ExitHandlerFn(void* Data);
    static void DelProc(void* Data, LqEvntInterator* Hdr);

    LqWrk(bool IsStart);
    ~LqWrk();
public:

    static LqWrkPtr New(bool IsStart = false);

    static LqWrkPtr ByEvntHdr(LqEvntHdr* EvntHdr);

    static LqWrkPtr GetNull();

    llong   GetId() const;

    /*
    * Get busy info
    */
    size_t   GetAssessmentBusy() const;
    size_t   CountEvnts() const;

    bool     RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds);
    size_t   RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds);

    bool     RemoveConnOnTimeOutAsync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);
    size_t   RemoveConnOnTimeOutSync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);

    bool     AddEvntAsync(LqEvntHdr* EvntHdr);
    bool     AddEvntSync(LqEvntHdr* EvntHdr);

    /*
      Remove event in strong async mode.
    */
    bool     RemoveEvnt(LqEvntHdr* EvntHdr);
    bool     CloseEvnt(LqEvntHdr* EvntHdr);

    bool     UpdateAllEvntFlagAsync();
    int      UpdateAllEvntFlagSync();

    bool     AsyncCall(void(LQ_CALL*AsyncProc)(void* Data), void* UserData = nullptr);
    size_t   CancelAsyncCall(void(LQ_CALL*AsyncProc)(void* Data), void* UserData = nullptr, bool IsAll = false);

    /*
    This method return all connection from this worker.
    */

    size_t   CloseAllEvntSync();
    bool     CloseAllEvntAsync();

    size_t   CloseConnByIpSync(const sockaddr* Addr);
    bool     CloseConnByIpAsync(const sockaddr* Addr);

    size_t   CloseConnByProtoSync(const LqProto* Proto);
    bool     CloseConnByProtoAsync(const LqProto* Proto);

    /*
      @Proc - In this proc must not call another worker methods.
    */
    size_t   EnumCloseRmEvnt(int(LQ_CALL*Proc)(void* UserData, LqEvntHdr* Conn), void* UserData = nullptr, bool* IsIterrupted = LqDfltPtr());
    size_t   EnumCloseRmEvntByProto(int(LQ_CALL*Proc)(void* UserData, LqEvntHdr* Conn), const LqProto* Proto, void* UserData = nullptr, bool* IsIterrupted = LqDfltPtr());

    bool EnumCloseRmEvntAsync(
        int(LQ_CALL*EventAct)(void* UserData, size_t UserDataSize, void*Wrk, LqEvntHdr* EvntHdr, LqTimeMillisec CurTime),
        const LqProto* Proto,
        void* UserData,
        size_t UserDataSize
    );



    LqString DebugInfo() const;
    LqString AllDebugInfo();
};


#pragma pack(pop)