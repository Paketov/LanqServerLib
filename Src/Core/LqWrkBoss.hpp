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
#include "LqSysPoll.h"

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
	friend LqFastAlloc;
    friend void LqWrkDelete(LqWrk* This);

    typedef LqPtdArr<LqWrkPtr> WrkArray;

    WrkArray      Wrks;
    intptr_t      MinCount;
	bool          NeedDelete;

    static size_t MinBusy(const WrkArray::interator& AllWrks, size_t* MinCount = LqDfltPtr());
    static size_t MaxBusy(const WrkArray::interator& AllWrks, size_t* MaxCount = LqDfltPtr());
    size_t        MinBusy(size_t* MinCount = LqDfltPtr());
	LqWrkBoss();
    LqWrkBoss(size_t CountWorkers);

public:
	static LqWrkBoss* New();
	static LqWrkBoss* New(size_t CountWorkers);

	static void Delete(LqWrkBoss * Target);

    ~LqWrkBoss();

    int         AddWorkers(size_t Count = LqThreadConcurrency(), bool IsStart = true);
    bool        AddWorker(const LqWrkPtr& LqWorker);

    bool        AddClientAsync(LqClientHdr* Evnt);
    bool        AddClientSync(LqClientHdr* Evnt);

    size_t      CountWorkers() const;
    size_t      CountClients() const;

    size_t      StartAllWorkersSync() const;
    size_t      StartAllWorkersAsync() const;

    bool        KickWorker(ullong IdWorker, bool IsTransferAllEvnt = true);
    size_t      KickWorkers(uintptr_t Count, bool IsTransferAllEvnt = true);

    bool        CloseAllClientsAsync() const;
    size_t      CloseAllClientsSync() const;

    /*
      Close all connections by timeout.
       @LiveTime: filter.
       @return: true - on success, false - otherwise
    */
    size_t      CloseClientsByTimeoutSync(LqTimeMillisec LiveTime) const;
    bool        CloseClientsByTimeoutAsync(LqTimeMillisec LiveTime) const;

    /*
      Close all connection by ip.
       @Addr: filter ip address.
       @return: true - on success, false - otherwise
    */
    size_t      CloseClientsByTimeoutSync(const LqProto* Proto, LqTimeMillisec LiveTime) const;
    bool        CloseClientsByTimeoutAsync(const LqProto* Proto, LqTimeMillisec LiveTime) const;

    /*
      Close all connection by ip.
       @Addr: filter ip address.
       @return: true - on success, false - otherwise
    */
    bool        CloseClientsByIpAsync(const sockaddr* Addr) const;
    size_t      CloseClientsByIpSync(const sockaddr* Addr) const;

    /*
      Close all connection by protocol.
       @Proto: filter protocol.
       @return: true - on success, false - otherwise
    */
    bool        CloseClientsByProtoAsync(const LqProto* Proto) const;
    size_t      CloseClientsByProtoSync(const LqProto* Proto) const;

    /*
      Update event flags.
       @Conn: target header.
       @return: true - on success, false - otherwise
    */
    bool        UpdateAllClientsFlagAsync() const;
    size_t      UpdateAllClientsFlagSync() const;
    /*
      Enum and remove or close event header
       @UserData: Use in @Proc
       @Proc: Callback function
         @Conn: Event header
         @return: 0 - just continue, 1 - remove, 2 - remove and close, -1 - Interrupt enum
       @return: Count removed events.
    */
    size_t      EnumClients(int(LQ_CALL*Proc)(void* UserData, LqClientHdr* EvntHdr), void* UserData = nullptr) const;

    /*
      Enum and remove or close event header, use @Proto as filter
       @Proto: Proto filter
       @UserData: Use in @Proc
       @Proc: Callback function
         @Conn: Event header
         @return: 0 - just continue, 1 - remove, 2 - remove and close, -1 - Interrupt enum
       @return: Count removed events.
    */
    size_t      EnumClientsByProto(int(LQ_CALL*Proc)(void* UserData, LqClientHdr* EvntHdr), const LqProto* Proto, void* UserData = nullptr) const;

    bool        EnumClientsAsync(
        int(LQ_CALL*EventAct)(void* UserData, size_t UserDataSize, void*Wrk, LqClientHdr* EvntHdr, LqTimeMillisec CurTime),
        void* UserData,
        size_t UserDataSize
    ) const;

	bool        EnumClientsAndCallFinForMultipleBossAsync11(LqWrkBoss* Bosses, size_t BossesSize, std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct, std::function<uintptr_t()> FinFunc) const;
	
	bool        EnumClientsAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct) const;
	size_t      EnumClients11(std::function<int(LqClientHdr*)> EventAct) const;
	size_t      EnumClientsByProto11(std::function<int(LqClientHdr*)> EventAct, const LqProto* Proto) const;

	/*
	 Use this method for safety remove module
	  @EventAct: Can be NULL. Used for remove or close event in workers
	  @FinFunc: Called when last worker enum all event. You can return handle on module for remove them
	  @UserData: User data for use in @EventAct and @FinFunc
	  @UserDataSize: Size of user data
	*/
    bool        EnumClientsAndCallFinAsync(
        int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
        uintptr_t(LQ_CALL*FinFunc)(void*, size_t), /* You can return module handle for delete them */
        void * UserData,
        size_t UserDataSize
    ) const;

	bool        EnumClientsAndCallFinAsync11(
		std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct, 
		std::function<uintptr_t()> FinFunc
	) const;

	static bool  EnumClientsAndCallFinForMultipleBossAsync(
		LqWrkBoss* Bosses,
		size_t BossesSize,
		int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
		uintptr_t(LQ_CALL*FinFunc)(void*, size_t), /* You can return module handle for delete them */
		void * UserData,
		size_t UserDataSize
	);

    /*
      Just remove event from workers. (In sync mode)
        @EvntHdr: Target event
        @return: true- when removed, false- otherwise
    */
    bool        RemoveClient(LqClientHdr* EvntHdr) const;
    bool        CloseClient(LqClientHdr* EvntHdr) const;


    /*
      Make async call procedure in one of worker.
        @AsyncProc - Target procedure
        @UserData - User data for procedure
        @return - true is async task added
    */
    bool         AsyncCall(void(LQ_CALL*AsyncProc)(void* Data), void* UserData = nullptr);
	bool		 AsyncCall11(std::function<void()> Proc);
    /*
      Remove async task from worker(s) queue
        @AsyncProc - Target procedure
        @UserData - User data for procedure
        @IsAll - Is remove all added tasks
        @return - count removed tasks
    */
    size_t       CancelAsyncCall(void(LQ_CALL*AsyncProc)(void* Data), void* UserData = nullptr, bool IsAll = false);


    size_t      KickAllWorkers();

    size_t      SetWrkMinCount(size_t NewVal);

    LqString    DebugInfo();
    LqString    AllDebugInfo();

    static LqWrkBoss* GetGlobal();

};

#pragma pack(pop)
