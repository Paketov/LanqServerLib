/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrk - Worker class.
* Recive and handle command from LqWrkBoss.
*/


#include "LqWrk.hpp"
#include "LqAlloc.hpp"
#include "LqWrkBoss.hpp"
#include "LqLog.h"
#include "LqOs.h"
#include "LqConn.h"
#include "LqTime.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#include <time.h>

#if !defined(LQPLATFORM_WINDOWS)
#include <signal.h>
#endif

enum
{
	LQWRK_CMD_ADD_CONN,						/*Add connection to work*/
	LQWRK_CMD_RM_CONN_ON_TIME_OUT,		/*Signal for close all time out connections*/
	LQWRK_CMD_WAIT_EVENT,
	LQWRK_CMD_CLOSE_CONN,
	LQWRK_CMD_TAKE_ALL_CONN,
	LQWRK_CMD_RM_CONN_BY_IP,
	LQWRK_CMD_UNLOCK_CONN
};


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

union ClientAddr
{
	sockaddr			Addr;
	sockaddr_in			AddrInet;
	sockaddr_in6		AddrInet6;
};

struct LqWrkCmdWaitEvnt
{
	void(*EventAct)(void* Data);
	void* UserData;
	inline LqWrkCmdWaitEvnt() {};
	inline LqWrkCmdWaitEvnt(void(*NewEventProc)(void* Data), void* NewUserData):
		EventAct(NewEventProc), UserData(NewUserData)
	{}
};


struct LqWrkCmdTakeAllConn
{
	void(*TakeProc)(void* Data, LqListEvnt& Connection);
	void* UserData;
	inline LqWrkCmdTakeAllConn() {};
	inline LqWrkCmdTakeAllConn(void(*TakeEventProc)(void* Data, LqListEvnt& Connection), void* NewUserData):
		TakeProc(TakeEventProc), UserData(NewUserData)
	{}
};

#pragma pack(pop)

static ullong __IdGen = 0;
static LqLocker<uchar> __IdGenLocker;

static ullong GenId()
{
	ullong r;
	__IdGenLocker.LockWrite();
	r = __IdGen;
	__IdGen++;
	__IdGenLocker.UnlockWrite();
	return r;
}

LqWorkerPtr LqWrk::New(bool IsStart) { return LqFastAlloc::New<LqWrk>(IsStart); }


LqWrk::LqWrk(bool IsStart):
	CountPointers(0),
	LqThreadBase(([&] { Id = GenId();  LqString Str = "Worker #" + LqToString(Id); return Str; })().c_str())
{
	LqEvntInit(&EventChecker);
	TimeStart = LqTimeGetLocMillisec();
	CountConnectionsInQueue = 0;
	if(IsStart) StartSync();
}

LqWrk::~LqWrk()
{
	EndWorkSync();
	LqEvntUninit(&EventChecker);
}

ullong LqWrk::GetId() const
{
	return Id;
}

void LqWrk::ParseInputCommands()
{
	/*
	* Recive async command loop.
	*/
	for(auto Command = CommandQueue.Fork(); Command;)
	{
		switch(Command.Type)
		{
			case LQWRK_CMD_ADD_CONN:
				/*
				Adding new connection.
				*/
			{
				LqEvntHdr* Connection = Command.Val<LqEvntHdr*>();
				Command.Pop<LqEvntHdr*>();
				CountConnectionsInQueue--;
				AddEvnt(Connection);
			}
			break;
			case LQWRK_CMD_RM_CONN_ON_TIME_OUT:
				/*
				Remove zombie connections by time val.
				*/
			{
				LqTimeMillisec TimeLiveMilliseconds = Command.Val<LqTimeMillisec>();
				Command.Pop<LqTimeMillisec>();
				RemoveConnOnTimeOut(TimeLiveMilliseconds);
			}
			break;
			case LQWRK_CMD_WAIT_EVENT:
				/*
				Call procedure for wait event.
				*/
				Command.Val<LqWrkCmdWaitEvnt>().EventAct(Command.Val<LqWrkCmdWaitEvnt>().UserData);
				Command.Pop<LqWrkCmdWaitEvnt>();
				break;
			case LQWRK_CMD_CLOSE_CONN:
				/*
				Close all waiting connections.
				*/
				Command.Pop();
				CloseAllEvnt();
				break;
			case LQWRK_CMD_TAKE_ALL_CONN:
				TakeAllEvnt(Command.Val<LqWrkCmdTakeAllConn>().TakeProc,
							Command.Val<LqWrkCmdTakeAllConn>().UserData);
				Command.Pop<LqWrkCmdTakeAllConn>();
				break;
			case LQWRK_CMD_RM_CONN_BY_IP:
				RemoveConnByIp(&Command.Val<ClientAddr>().Addr);
				Command.Pop<LqWrkCmdTakeAllConn>();
				break;
			case LQWRK_CMD_UNLOCK_CONN:
				/*
				* Unlock connection.
				*/
				LqEvntSetMaskByHdr(&EventChecker, Command.Val<LqEvntHdr*>());
				Command.Pop<LqEvntHdr*>();
				break;
			default:
				/*
				Is command unknown.
				*/
				Command.JustPop();
		}

	}
}

void LqWrk::RewindToEndForketCommandQueue(LqQueueCmd<uchar>::Interator& Command)
{
	while(Command)
	{
		switch(Command.Type)
		{
			case LQWRK_CMD_ADD_CONN:
			{
				auto Evnt = Command.Val<LqEvntHdr*>();
				LqEvntHdrClose(Evnt);
				Command.Pop<LqEvntHdr*>();
			}
			break;
			default:
				Command.JustPop();
		}
	}
}

void LqWrk::CloseAllEvnt()
{
	LqEvntInterator Interator;
	for(auto r = LqEvntEnumBegin(&EventChecker, &Interator); r; r = LqEvntEnumNext(&EventChecker, &Interator))
	{
		auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &Interator);
		LqEvntRemoveByInterator(&EventChecker, &Interator);
		LqEvntHdrClose(Evnt);
	}
}

void LqWrk::ClearQueueCommands()
{
	for(auto Command = CommandQueue.Fork(); Command;)
	{
		switch(Command.Type)
		{
			case LQWRK_CMD_ADD_CONN:
			{
				auto Evnt = Command.Val<LqEvntHdr*>();
				LqEvntHdrClose(Evnt);
				Command.Pop<LqEvntHdr*>();
			}
			break;
			default:
				Command.JustPop();
		}
	}
}

void LqWrk::RemoveEvntInList(LqListEvnt& Dest)
{
	LqEvntInterator Interator;
	for(auto r = LqEvntEnumBegin(&EventChecker, &Interator); r; r = LqEvntEnumNext(&EventChecker, &Interator))
	{
		auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &Interator);
		if(!Dest.Add(Evnt))
		{
			LqEvntHdrClose(Evnt);
			LQ_ERR("Not remove connection in list from #%llu worker", Id);
		}
		LqEvntRemoveByInterator(&EventChecker, &Interator);
	}
}

void LqWrk::RemoveEvntInListFromCmd(LqListEvnt& Dest)
{
	for(auto Command = CommandQueue.SeparateBegin(); !CommandQueue.SeparateIsEnd(Command);)
	{
		switch(Command.Type)
		{
			case LQWRK_CMD_ADD_CONN:
			{
				/* Is command - adding connection, add this connection in List*/
				auto Connection = Command.Val<LqEvntHdr*>();
				if(!Dest.Add(Connection))
				{
					LQ_ERR("LqWrk::RemoveEvntInListFromCmd: not alloc memory in list.");
					LqEvntHdrClose(Connection);
				}
				Command.Pop<LqEvntHdr*>();
			}
			break;
			default:
				/* Otherwise return current command in list*/
				CommandQueue.SeparatePush(Command);
		}
	}
}

void LqWrk::BeginThread()
{
#if !defined(LQPLATFORM_WINDOWS)
	signal(SIGPIPE, SIG_IGN);
#endif
	while(true)
	{
		if(LqEvntSignalCheckAndReset(&EventChecker))
		{
			SafeReg.EnterSafeRegion();
			if(LqThreadBase::IsShouldEnd) 
				goto lblOut;
			ParseInputCommands();
			if(LqThreadBase::IsShouldEnd)
			{
lblOut:
				CloseAllEvnt();
				ClearQueueCommands();
				return;
			}
		}
		for(LqEvntFlag Revent = LqEvntEnumEventBegin(&EventChecker); Revent != 0; Revent = LqEvntEnumEventNext(&EventChecker))
		{
			auto h = LqEvntGetHdrByCurrent(&EventChecker);
			auto OldFlags = h->Flag;
			h->Flag |= _LQEVNT_FLAG_NOW_EXEC;
			if(h->Flag & _LQEVNT_FLAG_CONN)
			{
				if(Revent & LQEVNT_FLAG_WR)
					((LqConn*)h)->Proto->WriteProc((LqConn*)h);
				if(Revent & LQEVNT_FLAG_RD)
					((LqConn*)h)->Proto->ReciveProc((LqConn*)h);
				if(Revent & LQEVNT_FLAG_ERR)
					((LqConn*)h)->Proto->ErrorProc((LqConn*)h);
			} else
			{
				if(Revent & (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_ERR))
					((LqEvntFd*)h)->Handler((LqEvntFd*)h, Revent);
			}

			if((Revent & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_END)) || (h->Flag & LQEVNT_FLAG_END))
			{
				LqEvntUnuseCurrent(&EventChecker);
				LqEvntRemoveCurrent(&EventChecker);
				LqEvntHdrClose(h);
			} else
			{
				h->Flag &= ~(_LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_USER_SET);
				if(h->Flag != OldFlags) //If have event changes
					LqEvntSetMaskByCurrent(&EventChecker);
				LqEvntUnuseCurrent(&EventChecker);
			}
		}
		LqEvntCheck(&EventChecker, LqTimeGetMaxMillisec());
	}
}

bool LqWrk::AddEvnt(LqEvntHdr* Connection)
{
	LQ_LOG_DEBUG("Conn recived from boss\n");
	return LqEvntAddHdr(&EventChecker, Connection);
}

void LqWrk::TakeAllEvnt(void(*TakeEventProc)(void *Data, LqListEvnt &Connection), void * NewUserData)
{
	LqListEvnt DestList;
	RemoveEvntInList(DestList);
	TakeEventProc(NewUserData, DestList);
}

void LqWrk::RemoveConnByIp(const sockaddr* Addr)
{
	switch(Addr->sa_family)
	{
		case AF_INET: case AF_INET6: break;
		default: return;
	}
	LqEvntInterator Interator;
	for(auto r = LqEvntEnumBegin(&EventChecker, &Interator); r; r = LqEvntEnumNext(&EventChecker, &Interator))
	{
		auto Connection = LqEvntGetHdrByInterator(&EventChecker, &Interator);
		if((Connection->Flag & _LQEVNT_FLAG_CONN) && (((LqConn*)Connection)->Proto->CmpAddressProc((LqConn*)Connection, Addr)))
		{
			LqEvntRemoveByInterator(&EventChecker, &Interator);
			((LqConn*)Connection)->Proto->EndConnProc((LqConn*)Connection);
		}
	}
}

size_t LqWrk::AddEvnt(LqListEvnt& ConnectionList)
{
	size_t CountAdded = 0;
	for(size_t i = 0; i < ConnectionList.GetCount(); i++)
	{
		if(!AddEvnt(ConnectionList[i]))
		{
			LqEvntHdrClose(ConnectionList[i]);
			LQ_ERR("Not adding #%u connection in #%llu worker", i, Id);
		} else
		{
			CountAdded++;
			ConnectionList[i] = nullptr;
		}
	}
	return CountAdded;
}

void LqWrk::RemoveConnOnTimeOut(LqTimeMillisec TimeLiveMilliseconds)
{
	LqEvntInterator Interator;
	auto CurTime = LqTimeGetLocMillisec();
	for(auto r = LqEvntEnumBegin(&EventChecker, &Interator); r; r = LqEvntEnumNext(&EventChecker, &Interator))
	{
		auto c = LqEvntGetHdrByInterator(&EventChecker, &Interator);
		if((c->Flag & _LQEVNT_FLAG_CONN) && (((LqConn*)c)->Proto->KickByTimeOutProc((LqConn*)c, CurTime, TimeLiveMilliseconds)))
		{
			LQ_LOG_USER("Remove connection  by timeout");
			LqEvntRemoveByInterator(&EventChecker, &Interator);
			((LqConn*)c)->Proto->EndConnProc((LqConn*)c);
		}
	}
}

size_t LqWrk::AddEvntListAsync(LqListEvnt& ConnectionList)
{
	size_t r = 0;
	for(size_t i = 0; i < ConnectionList.GetCount(); i++)
	{
		if(ConnectionList[i] == nullptr)
			continue;
		if(!AddEvntAsync(ConnectionList[i]))
		{
			LqEvntHdrClose(ConnectionList[i]);
			LQ_ERR("Not adding #%u connection in #%llu worker", i, Id);
		} else
		{
			r++;
		}
		ConnectionList[i] = nullptr;
	}
	return r;
}

size_t LqWrk::AddEvntListSync(LqListEvnt& ConnectionList)
{
	LockWrite();
	auto r = AddEvnt(ConnectionList);
	UnlockWrite();
	return r;
}

bool LqWrk::RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds)
{
	if(!CommandQueue.PushBegin(LQWRK_CMD_RM_CONN_ON_TIME_OUT, TimeLiveMilliseconds))
		return false;
	NotifyThread();
	return true;
}

bool LqWrk::RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds)
{
	LockWrite();
	RemoveConnOnTimeOut(TimeLiveMilliseconds);
	UnlockWrite();
	return true;
}

bool LqWrk::AddEvntAsync(LqEvntHdr* Connection)
{
	if(!CommandQueue.Push(LQWRK_CMD_ADD_CONN, Connection))
		return false;
	CountConnectionsInQueue++;
	NotifyThread();
	return true;
}

bool LqWrk::AddEvntSync(LqEvntHdr* Connection)
{
	LockWrite();
	auto r = AddEvnt(Connection);
	UnlockWrite();
	return r;
}

bool LqWrk::SyncEvntFlagAsync(LqEvntHdr* Connection)
{
	if(!CommandQueue.PushBegin<LqEvntHdr*>(LQWRK_CMD_UNLOCK_CONN, Connection))
		return false;
	NotifyThread();
	return true;
}

int LqWrk::SyncEvntFlagSync(LqEvntHdr* Connection)
{
	LockWrite();
	auto r = LqEvntSetMaskByHdr(&EventChecker, Connection);
	UnlockWrite();
	return r;
}

bool LqWrk::WaitEvent(void(*NewEventProc)(void* Data), void* NewUserData)
{
	if(!CommandQueue.PushBegin<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, LqWrkCmdWaitEvnt(NewEventProc, NewUserData)))
		return false;
	NotifyThread();
	return true;
}

bool LqWrk::CloseAllEvntAsync()
{
	if(!CommandQueue.PushBegin(LQWRK_CMD_CLOSE_CONN))
		return false;
	NotifyThread();
	return true;
}

void LqWrk::CloseAllEvntSync()
{
	LockWrite();
	CloseAllEvnt();
	UnlockWrite();
}

bool LqWrk::TakeAllConnAsync(void(*TakeEventProc)(void *Data, LqListEvnt &ConnectionList), void * NewUserData)
{
	if(!CommandQueue.PushBegin<LqWrkCmdTakeAllConn>(LQWRK_CMD_TAKE_ALL_CONN, LqWrkCmdTakeAllConn(TakeEventProc, NewUserData)))
		return false;
	NotifyThread();
	return true;
}

bool LqWrk::TakeAllEvnt(LqListEvnt & ConnectionList)
{
	LockWrite();
	RemoveEvntInList(ConnectionList);
	RemoveEvntInListFromCmd(ConnectionList);
	UnlockWrite();
	return true;
}

bool LqWrk::TakeAllConnSync(LqListEvnt& ConnectionList)
{
	LockWrite();
	RemoveEvntInList(ConnectionList);
	UnlockWrite();
	return true;
}


bool LqWrk::CloseConnByIpSync(const sockaddr* Addr)
{
	LockWrite();
	RemoveConnByIp(Addr);
	UnlockWrite();
	return true;
}

bool LqWrk::CloseConnByIpAsync(const sockaddr* Addr)
{
	StartThreadLocker.LockWriteYield();
	switch(Addr->sa_family)
	{
		case AF_INET:
		{
			ClientAddr s;
			s.AddrInet = *(sockaddr_in*)Addr;
			if(!CommandQueue.PushBegin<ClientAddr>(LQWRK_CMD_RM_CONN_BY_IP, s))
			{
				StartThreadLocker.UnlockWrite();
				return false;
			}
		}
		break;
		case AF_INET6:
		{
			ClientAddr s;
			s.AddrInet6 = *(sockaddr_in6*)Addr;
			if(!CommandQueue.PushBegin<ClientAddr>(LQWRK_CMD_RM_CONN_BY_IP, s))
			{
				StartThreadLocker.UnlockWrite();
				return false;
			}
		}
		break;
		default: StartThreadLocker.UnlockWrite(); return false;
	}
	StartThreadLocker.UnlockWrite();
	NotifyThread();
	return true;
}

void LqWrk::EnumEvnt(void* UserData, void(*Proc)(void *UserData, LqEvntHdr* Conn))
{
	LockWrite();
	LqEvntInterator Interator;
	for(auto r = LqEvntEnumBegin(&EventChecker, &Interator); r; r = LqEvntEnumNext(&EventChecker, &Interator))
	{
		auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &Interator);
		Proc(UserData, Hdr);
	}
	UnlockWrite();
}

LqString LqWrk::DebugInfo() const
{
	char Buf[1024];
	size_t ccip = LqEvntCount(&EventChecker);
	size_t cciq = CountConnectionsInQueue;

	auto CurrentTimeMillisec = LqTimeGetLocMillisec();
	sprintf
	(
		Buf,
		"Worker Id: %llu\n"
		"Time start: %s (%s)\n"
		"Count connections in process: %u\n"
		"Count connections in queue: %u\n"
		"Common count connections: %u\n",
		Id,
		LqTimeLocSecToStlStr(CurrentTimeMillisec / 1000).c_str(),
		LqTimeDiffMillisecToStlStr(TimeStart, CurrentTimeMillisec).c_str(),
		(uint)ccip,
		(uint)cciq,
		(uint)(ccip + cciq)
	);
	return Buf;
}

LqString LqWrk::AllDebugInfo()
{
	LqString r =
		"---------\nCommon worker info\n" +
		DebugInfo() +
		"---------\nThread info\n" +
		LqThreadBase::DebugInfo() +
		"---------\n";
	LockRead();

	LqEvntInterator Interator;
	ullong CurTime = LqTimeGetLocMillisec();
	if(LqEvntEnumBegin(&EventChecker, &Interator))
	{
		int k = 0;
		do
		{
			auto Conn = LqEvntGetHdrByInterator(&EventChecker, &Interator);
			if(Conn->Flag & _LQEVNT_FLAG_CONN)
			{
				r += "Conn #" + LqToString(k) + "\n";
				char* DbgInf = ((LqConn*)Conn)->Proto->DebugInfoProc((LqConn*)Conn);
				if(DbgInf != nullptr)
				{
					r += DbgInf;
					r += "\n";
					free(DbgInf);
				}
				k++;
			}
		} while(LqEvntEnumNext(&EventChecker, &Interator));
	}
	UnlockRead();
	r += "---------\n";
	return r;
}

size_t LqWrk::GetAssessmentBusy() const { return CountConnectionsInQueue + LqEvntCount(&EventChecker); }

bool LqWrk::LockWrite()
{
	if(IsThisThread())
	{
		SafeReg.EnterSafeRegionAndSwitchToWriteMode();
		return true;
	}
	SafeReg.OccupyWriteYield();
	NotifyThread();
	while(!SafeReg.TryWaitRegion())
	{
		if(IsThreadEnd())
			return false;
		std::this_thread::yield();
	}
	return true;
}

bool LqWrk::LockRead()
{
	if(IsThisThread())
	{
		SafeReg.EnterSafeRegionAndSwitchToReadMode();
		return true;
	}
	SafeReg.OccupyReadYield();
	NotifyThread();
	while(!SafeReg.TryWaitRegion())
	{
		if(IsThreadEnd())
			return false;
		std::this_thread::yield();
	}
	return true;
}

void LqWrk::UnlockRead() const { SafeReg.ReleaseRead(); }

void LqWrk::UnlockWrite() const { SafeReg.ReleaseWrite(); }

void LqWrk::NotifyThread() { LqEvntSignalSet(&EventChecker); }

void WORKER_POINTERWorkerDeleter(LqWrk* p) { LqFastAlloc::Delete(p); }
