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
	LqAtomic<int>					CountPointers;

	/* GetCount waiting connections */
	LqAtomic<size_t>				CountConnectionsInQueue;

	LqQueueCmd<uchar>				CommandQueue;
	LqEvnt							EventChecker;
	mutable LqSafeRegion<uint>		SafeReg;
	ullong							Id;
	LqTimeMillisec					TimeStart;

	void ParseInputCommands();
	virtual void BeginThread();
	virtual void NotifyThread();
	
	bool LockRead();
	void UnlockRead() const;
	bool LockWrite();
	void UnlockWrite() const;

	void ClearQueueCommands();
	void RemoveConnInListFromCmd(LqListConn& Dest);

	void RemoveConnInList(LqListConn& Dest);

	void RewindToEndForketCommandQueue(LqQueueCmd<uchar>::Interator& Command);

	void TakeAllConn(void (*TakeEventProc)(void* Data, LqListConn& Connection), void* NewUserData);

	bool AddConn(LqConn* Connection);
	void CloseAllConn();
	void RemoveConnOnTimeOut(LqTimeMillisec TimeLiveMilliseconds);

	size_t AddConnections(LqListConn& ConnectionList);
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

	bool AddConnAsync(LqConn* Connection);
	bool AddConnSync(LqConn* Connection);

	bool UnlockConnAsync(LqConn* Connection);
	int UnlockConnSync(LqConn* Connection);

	size_t AddConnListAsync(LqListConn& ConnectionList);
	size_t AddConnListSync(LqListConn& ConnectionList);

	bool WaitEvent(void (*NewEventProc)(void* Data), void* NewUserData = nullptr);

	/*
	This method return all connection from this worker.
	*/
	bool TakeAllConn(LqListConn& ConnectionList);
	bool TakeAllConnSync(LqListConn& ConnectionList);
	bool TakeAllConnAsync(void (*TakeEventProc)(void* Data, LqListConn& ConnectionList), void* NewUserData = nullptr);



	bool CloseAllConnAsync();
	void CloseAllConnSync();

	bool CloseConnByIpSync(const sockaddr* Addr);
	bool CloseConnByIpAsync(const sockaddr* Addr);

	void EnumConn(void* UserData, void(* Proc)(void* UserData, LqConn* Conn));

	LqString DebugInfo() const;
	LqString AllDebugInfo();
};


#pragma pack(pop)