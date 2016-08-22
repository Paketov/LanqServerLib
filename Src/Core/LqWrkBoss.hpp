#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkBoss - Accept clients and send him to workers.
*                +-----------+
*                | LqWrkBoss |            +-------+
*                |     =   --|----------> |LqProto|
*                +-----------+            +-------+
*                    /      \               ^
*                   /        \              |
*                  /         LqConn --------+
*                 /             \
*             +-----+        +-----+
*             |LqWrk|        |LqWrk|
*             +-----+        +-----+
*
*/

class LqWrkBoss;
class LqWrk;

#include "LqQueueCmd.hpp"
#include "LqEvnt.h"

#include "LqWrkList.hpp"

#include "LqWrkTask.hpp"
#include "LqZombieKiller.hpp"
#include "LqThreadBase.hpp"

#include "Lanq.h"
#include "LqListConn.hpp"
#include "LqSharedPtr.hpp"
#include "LqDfltRef.hpp"
#include "LqDef.h"


typedef LqSharedPtr<LqWrk, LqFastAlloc::Delete> LqWorkerPtr;

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrkBoss:
    virtual private LqWrkList,
    virtual protected LqZombieKillerTask,
    public LqThreadBase
{
    friend LqWrk;

    ullong                  Id;
    LqEvnt                  EventChecker;
    LqConn                  Sock;
    LqQueueCmd<uchar>       CommandQueue;
    LqString                Port;
    LqString                Host;
    LqAtomic<size_t>        CountConnAccepted;
    LqAtomic<size_t>        CountConnIgnored;
    LqLocker<uintptr_t>     LockerBind;
    bool                    IsRebind;
    int                     TransportProtoFamily;
    size_t                  MaxConnections;
public:
    volatile int            ErrBind;
    LqWrkTask               Tasks;
private:
    LqProto*                ProtoReg;


    bool        DistributeListConnections(const LqListEvnt& List);
    size_t      MinBusyWithoutLock(size_t* MinCount = LqDfltPtr());
    size_t      MaxBusyWithoutLock(size_t* MaxCount = LqDfltPtr());
    void        ParseInputCommands();
    size_t      MinBusy(size_t* MinCount = LqDfltPtr());
    virtual void BeginThread();
    virtual void NotifyThread();
    bool        UnbindSock();
    bool        Bind();
public:

    LqWrkBoss();
    LqWrkBoss(LqProto* ConnectManager);
    ~LqWrkBoss();

    void        SetPrt(const char* Name);
    void        GetPrt(char* DestName, size_t DestLen);
    void        SetProtocolFamily(int Val);
    int         GetProtocolFamily();
    void        SetMaxConn(int Val);
    int         GetMaxConn();
    void        Rebind();


    ullong      GetId() const;

    LqProto*    RegisterProtocol(LqProto* ConnectManager);
    LqProto*    GetProto();

    size_t      CountConnections() const;

    bool        TransferEvnt(const LqListEvnt& ConnectionsList);
    bool        TransferEvntEnd(LqListEvnt& ConnectionsList, LqWrk* LqWorker);

    bool        AddWorkers(size_t Count = LqSystemThread::hardware_concurrency(), bool IsStart = true);
    bool        AddWorker(const LqWorkerPtr& LqWorker);

    bool        AddEvntAsync(LqEvntHdr* Evnt);
    bool        AddEvntSync(LqEvntHdr* Evnt);

    size_t      CountWorkers() const;
    /* Get worker by id*/
    LqWorkerPtr operator[](size_t Index) const;

    void        StartAllWorkersSync();
    void        StartAllWorkersAsync();

    bool        KickWorker(ullong IdWorker);
    void        KickWorkers(size_t Count);

    bool        CloseAllEvntAsync();
    void        CloseAllEvntSync();

    bool        CloseConnByIpAsync(const sockaddr* Addr);
    void        CloseConnByIpSync(const sockaddr* Addr);

    /* !!! In @Proc you must not call in workers or Boss ..Sync methods. In this case block inevitable. !!!*/
    void        EnumEvnt(void* UserData, void(*Proc)(void* UserData, LqEvntHdr* Conn));

    bool        SyncEvntFlag(LqEvntHdr* Conn);

    size_t      KickAllWorkers();

    LqString    DebugInfo();
};

#pragma pack(pop)