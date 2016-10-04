#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkBoss - Accept clients and send him to workers.
*                +-----------+
*                | LqWrkBoss |            +-------+
*                |     =     |            |LqProto|
*                +-----------+            +-------+
*                    /      \               ^
*                   /        \              |
*                  /         LqConn --------+
*                 /            \
*             +-----+         +-----+
*             |LqWrk|         |LqWrk|
*             +-----+         +-----+
*
*/

class LqWrkBoss;
class LqWrk;

#include "LqQueueCmd.hpp"
#include "LqEvnt.h"

#include "LqThreadBase.hpp"

#include "Lanq.h"
#include "LqListConn.hpp"
#include "LqShdPtr.hpp"
#include "LqDfltRef.hpp"
#include "LqDef.h"
#include "LqWrkBoss.h"
#include "LqAlloc.hpp"
#include "LqPtdArr.hpp"
#include "LqWrk.hpp"

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)


class LQ_IMPORTEXPORT LqWrkBoss
{
    friend LqWrk;
    friend void LqWrkDelete(LqWrk* This);

    typedef LqPtdArr<LqWrkPtr> WrkArray;

    WrkArray      Wrks;
    intptr_t      MinCount;

    static size_t MinBusy(const WrkArray::interator& AllWrks, size_t* MinCount = LqDfltPtr());
    static size_t MaxBusy(const WrkArray::interator& AllWrks, size_t* MaxCount = LqDfltPtr());
    size_t      MinBusy(size_t* MinCount = LqDfltPtr());

    size_t      TransferAllEvnt(LqWrk* Source) const;
public:

    LqWrkBoss();
    LqWrkBoss(size_t CountWorkers);
    ~LqWrkBoss();

    int         AddWorkers(size_t Count = LqSystemThread::hardware_concurrency(), bool IsStart = true);
    bool        AddWorker(const LqWrkPtr& LqWorker);

    bool        AddEvntAsync(LqEvntHdr* Evnt);
    bool        AddEvntSync(LqEvntHdr* Evnt);

    bool        TransferEvnt(const LqListEvnt& ConnectionsList) const;

    size_t      CountWorkers() const;

    size_t      StartAllWorkersSync() const;
    size_t      StartAllWorkersAsync() const;

    bool        KickWorker(ullong IdWorker);
    size_t      KickWorkers(uintptr_t Count);

    bool        CloseAllEvntAsync() const;
    size_t      CloseAllEvntSync() const;

    size_t      CloseEventAsync(LqEvntHdr* Event) const;
    bool        CloseEventSync(LqEvntHdr* Event) const;

    size_t      CloseEventByTimeoutSync(LqTimeMillisec LiveTime) const;
    bool        CloseEventByTimeoutAsync(LqTimeMillisec LiveTime) const;

    size_t      CloseEventByTimeoutSync(const LqProto* Proto, LqTimeMillisec LiveTime) const;
    bool        CloseEventByTimeoutAsync(const LqProto* Proto, LqTimeMillisec LiveTime) const;

    bool        CloseConnByIpAsync(const sockaddr* Addr) const;
    size_t      CloseConnByIpSync(const sockaddr* Addr) const;

    bool        CloseConnByProtoAsync(const LqProto* Proto) const;
    size_t      CloseConnByProtoSync(const LqProto* Proto) const; 
    
    bool        SyncEvntFlagAsync(LqEvntHdr* Conn) const;
    bool        SyncEvntFlagSync(LqEvntHdr* Conn) const;

    size_t      EnumDelEvnt(void* UserData, bool(*Proc)(void* UserData, LqEvntHdr* Conn)) const; //Enum all events
    size_t      EnumDelEvntByProto(const LqProto* Proto, void* UserData, bool(*Proc)(void* UserData, LqEvntHdr* Conn)) const; //Enum event by proto

    size_t      KickAllWorkers();

    size_t      SetWrkMinCount(size_t NewVal); 

    static LqWrkBoss* GetGlobal();

};

#pragma pack(pop)
