#pragma once

/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrk - Worker class.
*  Recive and handle command from LqWrkBoss.
*  Work with connections, call protocol procedures.
*/

class LqWrk;
class LqWrkBoss;


#include "LqEvnt.h"
#include "LqLock.hpp"
#include "LqQueueCmd.hpp"
#include "LqThreadBase.hpp"
#include "LqListConn.hpp"
#include "LqShdPtr.hpp"
#include "LqDef.hpp"
#include "Lanq.h"

void LqWrkDelete(LqWrk* This);

typedef LqShdPtr<LqWrk, LqWrkDelete, true> LqWrkPtr;

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrk:
    public LqThreadBase
{
	enum
	{
		LQWRK_CMD_ADD_CONN,                     /*Add connection to work*/
		LQWRK_CMD_RM_CONN_ON_TIME_OUT,      /*Signal for close all time out connections*/
		LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO,
		LQWRK_CMD_WAIT_EVENT,
		LQWRK_CMD_CLOSE_ALL_CONN,
		LQWRK_CMD_TAKE_ALL_CONN,
		LQWRK_CMD_RM_CONN_BY_IP,
		LQWRK_CMD_CLOSE_CONN_BY_PROTO,
		LQWRK_CMD_SYNC_FLAG,
		LQWRK_CMD_CLOSE_CONN
	};

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
    mutable LqSafeRegion<uintptr_t>                     SafeReg;
    ullong                                              Id;
    LqTimeMillisec                                      TimeStart;
    bool                                                IsDelete;

    void ParseInputCommands();
    virtual void BeginThread();
    virtual void NotifyThread();

    int LockRead();
    void UnlockRead() const;
    int LockWrite();
    void UnlockWrite() const;

    void ClearQueueCommands();
    void RemoveEvntInListFromCmd(LqListEvnt& Dest);

    void RemoveEvntInList(LqListEvnt& Dest);

    void RewindToEndForketCommandQueue(LqQueueCmd<uchar>::Interator& Command);

    void TakeAllEvnt(void(*TakeEventProc)(void* Data, LqListEvnt& Connection), void* NewUserData);

    bool AddEvnt(LqEvntHdr* Connection);
    size_t CloseAllEvnt();
    size_t RemoveConnOnTimeOut(LqTimeMillisec TimeLiveMilliseconds);
    size_t RemoveConnOnTimeOut(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);

    size_t CloseConnByProto(const LqProto* Proto);

    size_t AddEvnt(LqListEvnt& ConnectionList);
    size_t RemoveConnByIp(const sockaddr* Addr);

    int CloseEvnt(LqEvntHdr* Connection);

    static void ExitHandlerFn(void* Data);

    LqWrk(bool IsStart);
    ~LqWrk();
public:

    static LqWrkPtr New(bool IsStart = true);

    ullong GetId() const;

    /*Получить загруженность потока-обработчика*/
    size_t GetAssessmentBusy() const;

    bool RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds);
    size_t RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds);

    bool RemoveConnOnTimeOutAsync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);
    size_t RemoveConnOnTimeOutSync(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds);

    bool AddEvntAsync(LqEvntHdr* Connection);
    bool AddEvntSync(LqEvntHdr* Connection);

    bool SyncEvntFlagAsync(LqEvntHdr* Connection);
    bool SyncEvntFlagSync(LqEvntHdr* Connection);

    bool CloseEvntAsync(LqEvntHdr* Connection);
    int CloseEvntSync(LqEvntHdr* Connection);

    size_t AddEvntListAsync(LqListEvnt& ConnectionList);
    size_t AddEvntListSync(LqListEvnt& ConnectionList);

    bool WaitEvent(void(*NewEventProc)(void* Data), void* NewUserData = nullptr);

    /*
    This method return all connection from this worker.
    */
    bool TakeAllEvnt(LqListEvnt& ConnectionList);
    bool TakeAllConnSync(LqListEvnt& ConnectionList);
    bool TakeAllConnAsync(void(*TakeEventProc)(void* Data, LqListEvnt& ConnectionList), void* NewUserData = nullptr);

    int CloseAllEvntAsync();
    size_t CloseAllEvntSync();

    size_t CloseConnByIpSync(const sockaddr* Addr);
    bool CloseConnByIpAsync(const sockaddr* Addr);

    bool CloseConnByProtoAsync(const LqProto* Addr);
    size_t CloseConnByProtoSync(const LqProto* Addr);

    size_t EnumDelEvnt(void* UserData, LqBool(*Proc)(void* UserData, LqEvntHdr* Conn));
    size_t EnumDelEvntByProto(const LqProto* Proto, void* UserData, LqBool(*Proc)(void* UserData, LqEvntHdr* Conn));

    LqString DebugInfo() const;
    LqString AllDebugInfo();
};


#pragma pack(pop)