#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkBoss (LanQ WoRKer BOSS)- Accept clients and send him to workers.
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
#include "LqShdPtr.hpp"
#include "LqDfltRef.hpp"
#include "LqDef.h"
#include "LqWrkBoss.h"
#include "LqAlloc.hpp"
#include "LqPtdArr.hpp"
#include "LqWrk.hpp"

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)


class LQ_IMPORTEXPORT LqWrkBoss {
    friend LqWrk;
    friend void LqWrkDelete(LqWrk* This);

    typedef LqPtdArr<LqWrkPtr> WrkArray;

    WrkArray      Wrks;
    intptr_t      MinCount;

    static size_t MinBusy(const WrkArray::interator& AllWrks, size_t* MinCount = LqDfltPtr());
    static size_t MaxBusy(const WrkArray::interator& AllWrks, size_t* MaxCount = LqDfltPtr());
    size_t        MinBusy(size_t* MinCount = LqDfltPtr());

    size_t TransferAllEvntFromWrkList(WrkArray& SourceWrks);

public:

    LqWrkBoss();
    LqWrkBoss(size_t CountWorkers);
    ~LqWrkBoss();

    int         AddWorkers(size_t Count = LqSystemThread::hardware_concurrency(), bool IsStart = true);
    bool        AddWorker(const LqWrkPtr& LqWorker);

    bool        AddEvntAsync(LqEvntHdr* Evnt);
    bool        AddEvntSync(LqEvntHdr* Evnt);

    size_t      CountWorkers() const;
    size_t      CountEvnts() const;

    size_t      StartAllWorkersSync() const;
    size_t      StartAllWorkersAsync() const;

    bool        KickWorker(ullong IdWorker, bool IsTransferAllEvnt = true);
    size_t      KickWorkers(uintptr_t Count, bool IsTransferAllEvnt = true);

    bool        CloseAllEvntAsync() const;
    size_t      CloseAllEvntSync() const;

    /*
      Close all connections by timeout.
       @LiveTime: filter.
       @return: true - on success, false - otherwise
    */
    size_t      CloseConnByTimeoutSync(LqTimeMillisec LiveTime) const;
    bool        CloseConnByTimeoutAsync(LqTimeMillisec LiveTime) const;

    /*
      Close all connection by ip.
       @Addr: filter ip address.
       @return: true - on success, false - otherwise
    */
    size_t      CloseConnByTimeoutSync(const LqProto* Proto, LqTimeMillisec LiveTime) const;
    bool        CloseConnByTimeoutAsync(const LqProto* Proto, LqTimeMillisec LiveTime) const;

    /*
      Close all connection by ip.
       @Addr: filter ip address.
       @return: true - on success, false - otherwise
    */
    bool        CloseConnByIpAsync(const sockaddr* Addr) const;
    size_t      CloseConnByIpSync(const sockaddr* Addr) const;

    /*
      Close all connection by protocol.
       @Proto: filter protocol.
       @return: true - on success, false - otherwise
    */
    bool        CloseConnByProtoAsync(const LqProto* Proto) const;
    size_t      CloseConnByProtoSync(const LqProto* Proto) const;

    /*
      Update event flags.
       @Conn: target header.
       @return: true - on success, false - otherwise
    */
    bool        UpdateAllEvntFlagAsync() const;
    size_t      UpdateAllEvntFlagSync() const;
    /*
      Enum and remove or close event header
       @UserData: Use in @Proc
       @Proc: Callback function
         @Conn: Event header
         @return: 0 - just continue, 1 - remove, 2 - remove and close
       @return: Count removed events.
    */
    size_t      EnumCloseRmEvnt(unsigned(*Proc)(void* UserData, LqEvntHdr* EvntHdr), void* UserData = nullptr) const;

    /*
      Enum and remove or close event header, use @Proto as filter
       @Proto: Proto filter
       @UserData: Use in @Proc
       @Proc: Callback function
         @Conn: Event header
         @return: 0 - just continue, 1 - remove, 2 - remove and close
       @return: Count removed events.
    */
    size_t      EnumCloseRmEvntByProto(unsigned(*Proc)(void* UserData, LqEvntHdr* EvntHdr), const LqProto* Proto, void* UserData = nullptr) const;

    /*
      Just remove event from workers. (In sync mode)
        @EvntHdr: Target event
        @return: true- when removed, false- otherwise
    */
    bool        RemoveEvnt(LqEvntHdr* EvntHdr) const;
    bool        CloseEvnt(LqEvntHdr* EvntHdr) const;


    /*
      Make async call procedure in in one of worker.
        @AsyncProc - Target procedure
        @UserData - User data for procedure
        @return - true is async task added
    */
    bool         AsyncCall(void(*AsyncProc)(void* Data), void* UserData = nullptr);
    /*
      Remove async task from worker(s) queue
        @AsyncProc - Target procedure
        @UserData - User data for procedure
        @IsAll - Is remove all added tasks
        @return - count removed tasks
    */
    size_t       CancelAsyncCall(void(*AsyncProc)(void* Data), void* UserData = nullptr, bool IsAll = false);

    /*
       Transfer all events follovers to other workers.
        @Source - Source worker or worker list(Boss).
        @return - count transfered events
    */
    size_t      TransferAllEvnt(LqWrk* Source);
    size_t      TransferAllEvnt(LqWrkBoss& Source);

    size_t      KickAllWorkers();

    size_t      SetWrkMinCount(size_t NewVal);

    LqString    DebugInfo();
    LqString    AllDebugInfo();

    static LqWrkBoss* GetGlobal();

};

#pragma pack(pop)
