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
	LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO,
	LQWRK_CMD_WAIT_EVENT,
	LQWRK_CMD_CLOSE_ALL_CONN,
	LQWRK_CMD_TAKE_ALL_CONN,
	LQWRK_CMD_RM_CONN_BY_IP,
	LQWRK_CMD_CLOSE_CONN_BY_PROTO,
	LQWRK_CMD_SYNC_FLAG,
	LQWRK_CMD_CLOSE_CONN
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

struct LqWrkCmdCloseByTimeout
{
	LqTimeMillisec TimeLive;
	const LqProto* Proto;
	inline LqWrkCmdCloseByTimeout() {};
	inline LqWrkCmdCloseByTimeout(const LqProto* NewProto, LqTimeMillisec Millisec):
		Proto(NewProto), TimeLive(Millisec)
	{}
};

#pragma pack(pop)

static ullong __IdGen = 1;
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

LqWrkPtr LqWrk::New(bool IsStart) 
{
	return LqFastAlloc::New<LqWrk>(IsStart);
}

void LqWrkDelete(LqWrk* This)
{
	if(This->LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		((LqWrkBoss*)LqWrkBossGet())->TransferAllEvnt(This);
		This->ClearQueueCommands();
		This->IsDelete = true;
		This->EndWorkAsync();
		return;
	}
	LqFastAlloc::Delete(This);
}


void LqWrk::ExitHandlerFn(void * Data)
{
	auto This = (LqWrk*)Data;
	if(This->IsDelete)
		LqFastAlloc::Delete(This);
}

LqWrk::LqWrk(bool IsStart):
	CountPointers(0),
	LqThreadBase(([&] { Id = GenId();  LqString Str = "Worker #" + LqToString(Id); return Str; })().c_str())
{
	LqEvntInit(&EventChecker);
	TimeStart = LqTimeGetLocMillisec();
	CountConnectionsInQueue = 0;

	UserData = this;
	ExitHandler = ExitHandlerFn;
	IsDelete = false;
	if(IsStart) 
		StartSync();
}

LqWrk::~LqWrk()
{
    EndWorkSync();
	CloseAllEvntSync();
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
				auto TimeLiveMilliseconds = Command.Val<LqTimeMillisec>();
				Command.Pop<LqTimeMillisec>();
				RemoveConnOnTimeOut(TimeLiveMilliseconds);
			}
			break;
			case LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO:
			{
				auto TimeLiveMilliseconds = Command.Val<LqWrkCmdCloseByTimeout>().TimeLive;
				auto Proto = Command.Val<LqWrkCmdCloseByTimeout>().Proto;
				Command.Pop<LqWrkCmdCloseByTimeout>();
				RemoveConnOnTimeOut(Proto, TimeLiveMilliseconds);
			}
			break;
			case LQWRK_CMD_WAIT_EVENT:
				/*
				Call procedure for wait event.
				*/
				Command.Val<LqWrkCmdWaitEvnt>().EventAct(Command.Val<LqWrkCmdWaitEvnt>().UserData);
				Command.Pop<LqWrkCmdWaitEvnt>();
				break;
			case LQWRK_CMD_CLOSE_ALL_CONN:
				/*
				Close all waiting connections.
				*/
				Command.Pop();
				CloseAllEvnt();
				break;
			case LQWRK_CMD_CLOSE_CONN:
				CloseEvnt(Command.Val<LqEvntHdr*>());
				Command.Pop<LqEvntHdr*>();
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
			case LQWRK_CMD_SYNC_FLAG:
				/*
				* Unlock connection.
				*/
				if(Command.Val<LqEvntHdr*>()->Flag & LQEVNT_FLAG_END)
					CloseEvnt(Command.Val<LqEvntHdr*>());
				 else
					LqEvntSetMaskByHdr(&EventChecker, Command.Val<LqEvntHdr*>());
				Command.Pop<LqEvntHdr*>();
				break;
			case LQWRK_CMD_CLOSE_CONN_BY_PROTO:
			{
				auto Proto = Command.Val<const LqProto*>();
				Command.Pop<const LqProto*>();
				CloseConnByProto(Proto);
			}
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

size_t LqWrk::CloseAllEvnt()
{
	size_t Ret = 0;
	lqevnt_enum_do(EventChecker, i)
	{
		auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
		LqEvntRemoveByInterator(&EventChecker, &i);
		LqEvntHdrClose(Evnt);
		Ret++;
	}lqevnt_enum_while(EventChecker);
	return Ret;
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
				Command.Pop<LqEvntHdr*>();
				if(LqWrkBossAddEvntAsync(Evnt) == -1)
					LqEvntHdrClose(Evnt);
			}
			break;
			default:
				Command.JustPop();
		}
	}
}

void LqWrk::RemoveEvntInList(LqListEvnt& Dest)
{
	lqevnt_enum_do(EventChecker, i)
	{
		auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(!Dest.Add(Evnt))
		{
			LqEvntHdrClose(Evnt);
			LQ_ERR("Not remove connection in list from #%llu worker", Id);
		}
		LqEvntRemoveByInterator(&EventChecker, &i);
	}lqevnt_enum_while(EventChecker);
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


/* 
 Main worker loop 
*/
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
				((LqWrkBoss*)LqWrkBossGet())->TransferAllEvnt(this);
				CloseAllEvnt();
				ClearQueueCommands();
				return;
			}
		}

		lqevnt_enum_changes_do(EventChecker, Revent)
		{
			auto EvntHdr = LqEvntGetHdrByCurrent(&EventChecker);
			auto OldFlags = EvntHdr->Flag;
			EvntHdr->Flag |= _LQEVNT_FLAG_NOW_EXEC;
			if(LqEvntIsConn(EvntHdr))
			{
				if(Revent & LQEVNT_FLAG_WR)
				{
					((LqConn*)EvntHdr)->Proto->WriteProc((LqConn*)EvntHdr);
					//Is removed current connection in handler
					if(LqEvntGetHdrByCurrent(&EventChecker) == nullptr)
						continue;
				}
				if(Revent & LQEVNT_FLAG_RD)
				{
					((LqConn*)EvntHdr)->Proto->ReciveProc((LqConn*)EvntHdr);
					//Is removed current connection in handler
					if(LqEvntGetHdrByCurrent(&EventChecker) == nullptr) 
						continue;
				}
				if(Revent & LQEVNT_FLAG_ERR)
				{
					((LqConn*)EvntHdr)->Proto->ErrorProc((LqConn*)EvntHdr);
					//Is removed current connection in handler
					if(LqEvntGetHdrByCurrent(&EventChecker) == nullptr) 
						continue;
				}
			} else
			{
				if(Revent & (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_ERR))
				{
					((LqEvntFd*)EvntHdr)->Handler((LqEvntFd*)EvntHdr, Revent); 
					//Is removed current connection in handler
					if(LqEvntGetHdrByCurrent(&EventChecker) == nullptr)
						continue;
				}
			}
			if((Revent & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_END)) || (EvntHdr->Flag & LQEVNT_FLAG_END))
			{
				LqEvntRemoveCurrent(&EventChecker);
				LqEvntHdrClose(EvntHdr);
			} else
			{
				EvntHdr->Flag &= ~(_LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_USER_SET);
				if(EvntHdr->Flag != OldFlags) //If have event changes
					LqEvntSetMaskByCurrent(&EventChecker);
				LqEvntUnuseCurrent(&EventChecker);
			}
		}lqevnt_enum_changes_while(EventChecker);

		LqEvntCheck(&EventChecker, LqTimeGetMaxMillisec());
	}
}

bool LqWrk::AddEvnt(LqEvntHdr* Connection)
{
	LQ_LOG_DEBUG("LqWrk::AddEvnt() conn recived from boss\n");
	return LqEvntAddHdr(&EventChecker, Connection);
}

void LqWrk::TakeAllEvnt(void(*TakeEventProc)(void *Data, LqListEvnt &Connection), void * NewUserData)
{
	LqListEvnt DestList;
	RemoveEvntInList(DestList);
	TakeEventProc(NewUserData, DestList);
}

size_t LqWrk::RemoveConnByIp(const sockaddr* Addr)
{
	size_t Res = 0;
	switch(Addr->sa_family)
	{
		case AF_INET: case AF_INET6: break;
		default: return Res;
	}
	lqevnt_enum_do(EventChecker, i)
	{
		auto Connection = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(Connection) && (((LqConn*)Connection)->Proto->CmpAddressProc((LqConn*)Connection, Addr)))
		{
			LqEvntRemoveByInterator(&EventChecker, &i);
			((LqConn*)Connection)->Proto->EndConnProc((LqConn*)Connection);
			Res++;
		}
	}lqevnt_enum_while(EventChecker);

	return Res;
}

int LqWrk::CloseEvnt(LqEvntHdr* Connection)
{
	int Res = 0;
	lqevnt_enum_do(EventChecker, i)
	{
		auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(Connection == Hdr)
		{
			LqEvntRemoveByInterator(&EventChecker, &i);
			LqEvntHdrClose(Hdr);
			Res = 1;
			break;
		}
	}lqevnt_enum_while(EventChecker);
	if(Res == 0)
		for(auto Command = CommandQueue.SeparateBegin(); !CommandQueue.SeparateIsEnd(Command);)
		{
			switch(Command.Type)
			{
				case LQWRK_CMD_ADD_CONN:
				{
					auto Hdr = Command.Val<LqEvntHdr*>();
					if(Hdr == Connection)
					{
						LqEvntHdrClose(Hdr);
						Command.Pop<LqEvntHdr*>();
						Res++;
					} else
					{
						CommandQueue.SeparatePush(Command);
					}
				}
				break;
				default:
					/* Otherwise return current command in list*/
					CommandQueue.SeparatePush(Command);
			}
		}
	return Res;
}

size_t LqWrk::AddEvnt(LqListEvnt& ConnectionList)
{
	size_t Res = 0;
	for(size_t i = 0; i < ConnectionList.GetCount(); i++)
	{
		if(!AddEvnt(ConnectionList[i]))
		{
			LqEvntHdrClose(ConnectionList[i]);
			LQ_ERR("LqWrk::AddEvnt() not adding #%u connection in #%llu worker\n", i, Id);
		} else
		{
			Res++;
			ConnectionList[i] = nullptr;
		}
	}
	return Res;
}

size_t LqWrk::RemoveConnOnTimeOut(LqTimeMillisec TimeLiveMilliseconds)
{
	size_t Res = 0;
	auto CurTime = LqTimeGetLocMillisec();
	lqevnt_enum_do(EventChecker, i)
	{
		auto c = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(c) && (((LqConn*)c)->Proto->KickByTimeOutProc((LqConn*)c, CurTime, TimeLiveMilliseconds)))
		{
			LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut() remove connection by timeout\n");
			LqEvntRemoveByInterator(&EventChecker, &i);
			((LqConn*)c)->Proto->EndConnProc((LqConn*)c);
			Res++;
		}
	}lqevnt_enum_while(EventChecker);
	return Res;
}

size_t LqWrk::RemoveConnOnTimeOut(const LqProto* Proto, LqTimeMillisec TimeLiveMilliseconds)
{
	size_t Res = 0;
	auto CurTime = LqTimeGetLocMillisec();
	lqevnt_enum_do(EventChecker, i)
	{
		auto c = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(c) && (((LqConn*)c)->Proto == Proto) && (Proto->KickByTimeOutProc((LqConn*)c, CurTime, TimeLiveMilliseconds)))
		{
			LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut() remove connection by timeout\n");
			LqEvntRemoveByInterator(&EventChecker, &i);
			((LqConn*)c)->Proto->EndConnProc((LqConn*)c);
			Res++;
		}
	}lqevnt_enum_while(EventChecker);
	return Res;
}

size_t LqWrk::CloseConnByProto(const LqProto* Proto)
{
	size_t Res = 0;
    lqevnt_enum_do(EventChecker, i)
	{
		auto c = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(c) && (((LqConn*)c)->Proto == Proto))
		{
			LQ_LOG_USER("LqWrk::CloseConnByProto() remove connection by protocol\n");
			LqEvntRemoveByInterator(&EventChecker, &i);
			((LqConn*)c)->Proto->EndConnProc((LqConn*)c);
			Res++;
		}
	}lqevnt_enum_while(EventChecker);
	return Res;
}

size_t LqWrk::AddEvntListAsync(LqListEvnt& ConnectionList)
{
	size_t Res = 0;
	for(size_t i = 0; i < ConnectionList.GetCount(); i++)
	{
		if(ConnectionList[i] == nullptr)
			continue;
		if(!AddEvntAsync(ConnectionList[i]))
		{
			LqEvntHdrClose(ConnectionList[i]);
			LQ_ERR("LqWrk::AddEvntListAsync() not adding #%u connection in #%llu worker\n", i, Id);
		} else
		{
			Res++;
		}
		ConnectionList[i] = nullptr;
	}
	return Res;
}

size_t LqWrk::AddEvntListSync(LqListEvnt& ConnectionList)
{
	LockWrite();
	auto Res = AddEvnt(ConnectionList);
	UnlockWrite();
	return Res;
}

bool LqWrk::RemoveConnOnTimeOutAsync(LqTimeMillisec TimeLiveMilliseconds)
{
	if(!CommandQueue.PushBegin(LQWRK_CMD_RM_CONN_ON_TIME_OUT, TimeLiveMilliseconds))
		return false;
	NotifyThread();
	return true;
}

size_t LqWrk::RemoveConnOnTimeOutSync(LqTimeMillisec TimeLiveMilliseconds)
{
	LockWrite();
	auto Res = RemoveConnOnTimeOut(TimeLiveMilliseconds);
	UnlockWrite();
	return Res;
}

bool LqWrk::RemoveConnOnTimeOutAsync(const LqProto * Proto, LqTimeMillisec TimeLiveMilliseconds)
{
    if(!CommandQueue.PushBegin<LqWrkCmdCloseByTimeout>(LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO, LqWrkCmdCloseByTimeout(Proto, TimeLiveMilliseconds)))
        return false;
    NotifyThread();
    return true;
}

size_t LqWrk::RemoveConnOnTimeOutSync(const LqProto * Proto, LqTimeMillisec TimeLiveMilliseconds)
{
    LockWrite();
    auto Res = RemoveConnOnTimeOut(Proto, TimeLiveMilliseconds);
    UnlockWrite();
    return Res;
}

bool LqWrk::AddEvntAsync(LqEvntHdr* Connection)
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
		return AddEvnt(Connection);
	if(!CommandQueue.Push(LQWRK_CMD_ADD_CONN, Connection))
		return false;
	CountConnectionsInQueue++;
	NotifyThread();
	return true;
}

bool LqWrk::AddEvntSync(LqEvntHdr* Connection)
{
	LockWrite();
	auto Res = AddEvnt(Connection);
	UnlockWrite();
	return Res;
}

bool LqWrk::SyncEvntFlagAsync(LqEvntHdr* Connection)
{
	if((LqThreadBase::thread::get_id() == std::this_thread::get_id()) && !(Connection->Flag & LQEVNT_FLAG_END))
		return LqEvntSetMaskByHdr(&EventChecker, Connection);
	if(!CommandQueue.PushBegin<LqEvntHdr*>(LQWRK_CMD_SYNC_FLAG, Connection))
		return false;
	NotifyThread();
	return true;
}

bool LqWrk::SyncEvntFlagSync(LqEvntHdr* Connection)
{
	bool Res;
	LockWrite();
	if(Connection->Flag & LQEVNT_FLAG_END)
	{
		CloseEvnt(Connection);
		Res = false;
	} else
	{
		Res = LqEvntSetMaskByHdr(&EventChecker, Connection);
	}
	UnlockWrite();
	return Res;
}

bool LqWrk::CloseEvntAsync(LqEvntHdr * Connection)
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		CloseEvnt(Connection);
		return true;
	}
	if(!CommandQueue.Push<LqEvntHdr*>(LQWRK_CMD_CLOSE_CONN, Connection))
		return false;
	NotifyThread();
	return true;
}

int LqWrk::CloseEvntSync(LqEvntHdr * Connection)
{	
	LockWrite();
	auto Res = CloseEvnt(Connection);
	UnlockWrite();
	return Res;
}

bool LqWrk::WaitEvent(void(*NewEventProc)(void* Data), void* NewUserData)
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		NewEventProc(NewUserData);
		return true;
	}
	if(!CommandQueue.PushBegin<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, LqWrkCmdWaitEvnt(NewEventProc, NewUserData)))
		return false;
	NotifyThread();
	return true;
}

int LqWrk::CloseAllEvntAsync()
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
		return CloseAllEvnt();
	if(!CommandQueue.PushBegin(LQWRK_CMD_CLOSE_ALL_CONN))
		return -1;
	NotifyThread();
	return 0;
}

size_t LqWrk::CloseAllEvntSync()
{
	LockWrite();
	auto Res = CloseAllEvnt();
	UnlockWrite();
	return Res;
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

size_t LqWrk::CloseConnByIpSync(const sockaddr* Addr)
{
	LockWrite();
	auto Res = RemoveConnByIp(Addr);
	UnlockWrite();
	return Res;
}

bool LqWrk::CloseConnByIpAsync(const sockaddr* Addr)
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		RemoveConnByIp(Addr);
		return true;
	}
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

bool LqWrk::CloseConnByProtoAsync(const LqProto * Addr)
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		CloseConnByProto(Addr);
		return true;
	}
	StartThreadLocker.LockWriteYield();
	auto Res = CommandQueue.PushBegin<const LqProto*>(LQWRK_CMD_CLOSE_CONN_BY_PROTO, Addr);
	StartThreadLocker.UnlockWrite();
	NotifyThread();
	return Res;
}

size_t LqWrk::CloseConnByProtoSync(const LqProto * Addr)
{
	LockWrite();
	auto Res = CloseConnByProto(Addr);
	UnlockWrite();
	return Res;
}

size_t LqWrk::EnumDelEvnt(void* UserData, LqBool(*Proc)(void *UserData, LqEvntHdr* Conn))
{
    size_t Res = 0;
    LockWrite();
    lqevnt_enum_do(EventChecker, i)
    {
        auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(Proc(UserData, Hdr))
        {
            LqEvntRemoveByInterator(&EventChecker, &i);
            LqEvntHdrClose(Hdr);
            Res++;
        }
    }lqevnt_enum_while(EventChecker);
    UnlockWrite();
    return Res;
}

size_t LqWrk::EnumDelEvntByProto(const LqProto * Proto, void * UserData, LqBool(*Proc)(void *UserData, LqEvntHdr *Conn))
{
	size_t Res = 0;
	LockWrite();
	lqevnt_enum_do(EventChecker, i)
	{
		auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(Hdr) && (((LqConn*)Hdr)->Proto == Proto))
		{
			if(Proc(UserData, Hdr))
			{
				LqEvntRemoveByInterator(&EventChecker, &i);
				LqEvntHdrClose(Hdr);
				Res++;
			}
		}
	}lqevnt_enum_while(EventChecker);
	UnlockWrite();
	return Res;
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
			if(LqEvntIsConn(Conn))
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

int LqWrk::LockWrite()
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		SafeReg.EnterSafeRegionAndSwitchToWriteMode();
		return 1;
	}
	SafeReg.OccupyWriteYield();
	NotifyThread();
	while(!SafeReg.TryWaitRegion())
	{
		if(IsThreadEnd())
			return -1;
		std::this_thread::yield();
	}
	return 0;
}

int LqWrk::LockRead()
{
	if(LqThreadBase::thread::get_id() == std::this_thread::get_id())
	{
		SafeReg.EnterSafeRegionAndSwitchToReadMode();
		return 1;
	}
	SafeReg.OccupyReadYield();
	NotifyThread();
	while(!SafeReg.TryWaitRegion())
	{
		if(IsThreadEnd())
			return -1;
		std::this_thread::yield();
	}
	return 0;
}

void LqWrk::UnlockRead() const { SafeReg.ReleaseRead(); }

void LqWrk::UnlockWrite() const { SafeReg.ReleaseWrite(); }

void LqWrk::NotifyThread() { LqEvntSignalSet(&EventChecker); }

