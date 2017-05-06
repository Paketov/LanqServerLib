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

#include <functional>


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

    friend long long __LqWrkInitEmpty();
    /*GetCount external reference, used in SharedPtr*/
    size_t                                              CountPointers;

    /* GetCount waiting connections */
    LqAtomic<size_t>                                    CountConnectionsInQueue;

    LqQueueCmd<uint8_t>                                 EvntFdQueue;
    LqQueueCmd<uint8_t>                                 CommandQueue;
    LqSysPoll                                           EventChecker;
    LqEvntFd                                            NotifyEvent;
    intptr_t                                            DeepLoop;

    mutable LqLocker<uintptr_t>                         Locker;
    mutable LqLocker<uintptr_t>                         WaitLocker;

    long long                                           Id;
    LqTimeMillisec                                      TimeStart;
    bool                                                IsDelete;
    bool                                                IsRecurseSkipCommands;
    uintptr_t                                           IsSyncAllFlags;

    
    virtual void BeginThread();
    virtual void NotifyThread();

    inline void Unlock() { Locker.UnlockWrite(); }
    inline void Lock() { Locker.LockWriteYield(); }

    inline void WaiterLockMain() { WaitLocker.LockWrite(); }
    inline void WaiterLock() {
        while(!WaitLocker.TryLockWrite()) {
            NotifyThread();
            for(uintptr_t i = ((uintptr_t)0); i < ((uintptr_t)50); i++)
                if(WaitLocker.TryLockWrite())
                    return;
            if(!IsThreadRunning())
                break;
        }
    }
    inline void WaiterUnlock() { WaitLocker.UnlockWrite(); }

    void ClearQueueCommands();
    void ParseInputCommands();
    static bool CmdIsOnlyOneTypeOfCommandInQueue(LqQueueCmd<uint8_t>::Interator& Inter, uint8_t TypeOfCommand);

    void AcceptAllEventFromQueue();

    static void ExitHandlerFn(void* Data);
    static void DelProc(void* Data, LqEvntInterator* Hdr);

    LqWrk(bool IsStart);
    ~LqWrk();
public:

    static LqWrkPtr New(bool IsStart = false);
	/*
		Get a processing worker by client
	*/
    static LqWrkPtr ByEvntHdr(LqClientHdr* EvntHdr);

    static LqWrkPtr GetNull();

    static bool IsNull(LqWrkPtr& WrkPtr);

    long long GetId() const;

    /*
    * Get busy info
    */
    size_t   GetAssessmentBusy() const;
    size_t   CountClients() const;

    bool     RemoveClientsOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds);
    size_t   RemoveClientsOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds);

    bool     RemoveClientsOnTimeOutAsync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);
    size_t   RemoveClientsOnTimeOutSync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);

    bool     AddClientAsync(LqClientHdr* EvntHdr);
    bool     AddClientSync(LqClientHdr* EvntHdr);

    /*
      Remove event in strong async mode.
    */
    bool     RemoveClient(LqClientHdr* EvntHdr);
    bool     CloseClient(LqClientHdr* EvntHdr);

    bool     UpdateAllClientFlagAsync();
    int      UpdateAllClientFlagSync();

    bool     AsyncCall(void(LQ_CALL*AsyncProc)(void* Data), void* UserData = nullptr);
    size_t   CancelAsyncCall(void(LQ_CALL*AsyncProc)(void* Data), void* UserData = nullptr, bool IsAll = false);
    bool     AsyncCall11(std::function<void()> Proc);
    /*
		This method return all connection from this worker.
    */
    size_t   CloseAllClientsSync();
    bool     CloseAllClientsAsync();


    size_t   CloseClientsByIpSync(const sockaddr* Addr);
    bool     CloseClientsByIpAsync(const sockaddr* Addr);

    size_t   CloseClientsByProtoSync(const LqProto* Proto);
    bool     CloseClientsByProtoAsync(const LqProto* Proto);

    /*
      @Proc - In this proc must not call another worker methods.
    */
    size_t   EnumClients(int(LQ_CALL*Proc)(void* UserData, LqClientHdr* Conn), void* UserData = nullptr, bool* IsIterrupted = LqDfltPtr());
    size_t   EnumClientsByProto(int(LQ_CALL*Proc)(void* UserData, LqClientHdr* Conn), const LqProto* Proto, void* UserData = nullptr, bool* IsIterrupted = LqDfltPtr());

    bool     EnumClientsAsync(
        int(LQ_CALL*EventAct)(void* UserData, size_t UserDataSize, void*Wrk, LqClientHdr* EvntHdr, LqTimeMillisec CurTime),
        void* UserData,
        size_t UserDataSize
    );
    /*
    * Use this module for safe delete module
    */
    bool     EnumClientsAndCallFinAsync(
        int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec), /* Can be NULL(Only for call fin)*/
        uintptr_t(LQ_CALL*FinFunc)(void*, size_t), /* You can return module handle for delete them, otherwise 0 */
        void * UserData,
        size_t UserDataSize
    );

    bool     EnumClientsAndCallFinAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct,std::function<uintptr_t()> FinFunc);
    bool     EnumClientsAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct);
    size_t   EnumClients11(std::function<int(LqClientHdr*)> EventAct, bool* IsIterrupted = LqDfltPtr());
    size_t   EnumClientsByProto11(std::function<int(LqClientHdr*)> EventAct, const LqProto* Proto, bool* IsIterrupted = LqDfltPtr());

    bool     TransferClientsAndRemoveFromBossAsync(LqWrkBoss* TargetBoss, bool IsTransferClients);

    LqString DebugInfo() const;
    LqString AllDebugInfo();
};


#pragma pack(pop)