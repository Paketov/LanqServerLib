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


#include "LqEvnt.h"
#include "LqLock.hpp"
#include "LqQueueCmd.hpp"
#include "LqThreadBase.hpp"
#include "LqShdPtr.hpp"
#include "LqDef.hpp"
#include "Lanq.h"
#include "LqAlloc.hpp"

LQ_IMPORTEXPORT void LQ_CALL LqWrkDelete(LqWrk* This);

typedef LqShdPtr<LqWrk, LqWrkDelete, false, false> LqWrkPtr;

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrk:
    public LqThreadBase {

    friend LqWrkBoss;
    friend LqConn;
    friend LqWrkPtr;
    friend LqFastAlloc;
    friend void LqWrkDelete(LqWrk* This);

    /*GetCount external reference, used in SharedPtr*/
    size_t                                              CountPointers;

    /* GetCount waiting connections */
    LqAtomic<size_t>                                    CountConnectionsInQueue;

    LqQueueCmd<uchar>                                   CommandQueue;
    LqEvnt                                              EventChecker;
    LqEvntFd                                            NotifyEvent;

    mutable LqLocker<uintptr_t>                         Locker;
    mutable LqLocker<uintptr_t>                         WaitLocker;

    ullong                                              Id;
    LqTimeMillisec                                      TimeStart;
    bool                                                IsDelete;
    uintptr_t                                           IsSyncAllFlags;

    void ParseInputCommands();
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

    static void ExitHandlerFn(void* Data);
    static void DelProc(void* Data, LqEvntInterator* Hdr);

    LqWrk(bool IsStart);
    ~LqWrk();
public:

    static LqWrkPtr New(bool IsStart = false);

    ullong   GetId() const;

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

    bool     AsyncCall(void(*AsyncProc)(void* Data), void* UserData = nullptr);
    size_t   CancelAsyncCall(void(*AsyncProc)(void* Data), void* UserData = nullptr, bool IsAll = false);

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
    size_t   EnumCloseRmEvnt(unsigned(*Proc)(void* UserData, LqEvntHdr* Conn), void* UserData = nullptr);
    size_t   EnumCloseRmEvntByProto(unsigned(*Proc)(void* UserData, LqEvntHdr* Conn), const LqProto* Proto, void* UserData = nullptr);

    LqString DebugInfo() const;
    LqString AllDebugInfo();
};


#pragma pack(pop)