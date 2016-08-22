#pragma once

/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrk - Worker class.
* Recive and handle command from LqWrkBoss.
* Work with connections, call protocol procedures.
*/

class LqWrk;
class LqWrkBoss;


#include "LqEvnt.h"
#include "LqLock.hpp"
#include "LqQueueCmd.hpp"
#include "LqThreadBase.hpp"
#include "LqListConn.hpp"
#include "LqSharedPtr.hpp"
#include "LqDef.hpp"
#include "Lanq.h"


typedef LqSharedPtr<LqWrk, LqFastAlloc::Delete> LqWorkerPtr;

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrk:
    public LqThreadBase
{
    friend LqWrkBoss;
    friend LqConn;
    friend LqWorkerPtr;
    friend LqFastAlloc;

    /*GetCount external reference, used in SharedPtr*/
    size_t                                              CountPointers;

    /* GetCount waiting connections */
    LqAtomic<size_t>                                    CountConnectionsInQueue;

    LqQueueCmd<uchar>                                   CommandQueue;
    LqEvnt                                              EventChecker;
    mutable LqSafeRegion<uint>                          SafeReg;
    ullong                                              Id;
    LqTimeMillisec                                      TimeStart;

    void ParseInputCommands();
    virtual void BeginThread();
    virtual void NotifyThread();

    bool LockRead();
    void UnlockRead() const;
    bool LockWrite();
    void UnlockWrite() const;

    void ClearQueueCommands();
    void RemoveEvntInListFromCmd(LqListEvnt& Dest);

    void RemoveEvntInList(LqListEvnt& Dest);

    void RewindToEndForketCommandQueue(LqQueueCmd<uchar>::Interator& Command);

    void TakeAllEvnt(void(*TakeEventProc)(void* Data, LqListEvnt& Connection), void* NewUserData);

    bool AddEvnt(LqEvntHdr* Connection);
    void CloseAllEvnt();
    void RemoveConnOnTimeOut(LqTimeMillisec TimeLiveMilliseconds);

    size_t AddEvnt(LqListEvnt& ConnectionList);
    void RemoveConnByIp(const sockaddr* Addr);

    LqWrk(bool IsStart);
public:

    static LqWorkerPtr New(bool IsStart = true);
    ~LqWrk();

    ullong GetId() const;

    /*Получить загруженность потока-обработчика*/
    size_t GetAssessmentBusy() const;

    bool RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds);
    bool RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds);

    bool AddEvntAsync(LqEvntHdr* Connection);
    bool AddEvntSync(LqEvntHdr* Connection);

    bool SyncEvntFlagAsync(LqEvntHdr* Connection);
    int SyncEvntFlagSync(LqEvntHdr* Connection);

    size_t AddEvntListAsync(LqListEvnt& ConnectionList);
    size_t AddEvntListSync(LqListEvnt& ConnectionList);

    bool WaitEvent(void(*NewEventProc)(void* Data), void* NewUserData = nullptr);

    /*
    This method return all connection from this worker.
    */
    bool TakeAllEvnt(LqListEvnt& ConnectionList);
    bool TakeAllConnSync(LqListEvnt& ConnectionList);
    bool TakeAllConnAsync(void(*TakeEventProc)(void* Data, LqListEvnt& ConnectionList), void* NewUserData = nullptr);



    bool CloseAllEvntAsync();
    void CloseAllEvntSync();

    bool CloseConnByIpSync(const sockaddr* Addr);
    bool CloseConnByIpAsync(const sockaddr* Addr);

    void EnumEvnt(void* UserData, void(*Proc)(void* UserData, LqEvntHdr* Conn));

    LqString DebugInfo() const;
    LqString AllDebugInfo();
};


#pragma pack(pop)