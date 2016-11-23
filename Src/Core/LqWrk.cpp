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
#include "LqFile.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#include <time.h>

#if !defined(LQPLATFORM_WINDOWS)
#include <signal.h>
#endif

/* 
  Use this macro if you want protect sync operations calling close handlers.
    When this macro used, thread caller of sync methods, waits until worker thread leave r/w/c handler.
	When close handler called by worker thread, thread-caller of sync kind method continue enum.
  Disable this macro if you want to shift the entire responsibility of the user.
 */
#define LQWRK_ENABLE_RW_HNDL_PROTECT

#define LqWrkCallConnHandler(Conn, Flags, Wrk)              \
{                                                           \
    auto Handler = ((LqConn*)(Conn))->Proto->Handler;       \
    ((LqWrk*)(Wrk))->Unlock();                              \
    Handler((LqConn*)(Conn), (Flags));                      \
    ((LqWrk*)(Wrk))->Lock();                                \
}

#define LqWrkCallConnCloseHandler(Conn, Wrk)                \
{                                                           \
    auto Handler = ((LqConn*)(Conn))->Proto->CloseHandler;  \
	((LqConn*)(Conn))->Flag |= _LQEVNT_FLAG_NOW_EXEC;       \
    ((LqWrk*)(Wrk))->Unlock();                              \
    Handler((LqConn*)(Conn));                               \
    ((LqWrk*)(Wrk))->Lock();                                \
}

#define LqWrkCallEvntFdHandler(EvntHdr, Flags, Wrk)         \
{                                                           \
    auto Handler = ((LqEvntFd*)(EvntHdr))->Handler;         \
    ((LqWrk*)(Wrk))->Unlock();                              \
    Handler((LqEvntFd*)(EvntHdr), (Flags));                 \
    ((LqWrk*)(Wrk))->Lock();                                \
}

#define LqWrkCallEvntFdCloseHandler(EvntHdr, Wrk)           \
{                                                           \
    auto Handler = ((LqEvntFd*)(EvntHdr))->CloseHandler;    \
    ((LqEvntFd*)(EvntHdr))->Flag |= _LQEVNT_FLAG_NOW_EXEC;  \
    ((LqWrk*)(Wrk))->Unlock();                              \
    Handler((LqEvntFd*)(EvntHdr));                          \
    ((LqWrk*)(Wrk))->Lock();                                \
}

#define LqWrkCallEvntHdrCloseHandler(Event, Wrk)            \
{                                                           \
    if(((LqEvntHdr*)(Event))->Flag & _LQEVNT_FLAG_CONN){    \
        LqWrkCallConnCloseHandler(Event, Wrk);              \
    }else{                                                  \
       LqWrkCallEvntFdCloseHandler(Event, Wrk);}            \
}


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqWrkCmdWaitEvnt
{
    void(*EventAct)(void* Data);
    void* UserData;
    inline LqWrkCmdWaitEvnt() {};
    inline LqWrkCmdWaitEvnt(void(*NewEventProc)(void* Data), void* NewUserData): EventAct(NewEventProc), UserData(NewUserData) {}
};

struct LqWrkCmdCloseByTimeout
{
    LqTimeMillisec TimeLive;
    const LqProto* Proto;
    inline LqWrkCmdCloseByTimeout() {};
    inline LqWrkCmdCloseByTimeout(const LqProto* NewProto, LqTimeMillisec Millisec): Proto(NewProto), TimeLive(Millisec) {}
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

LQ_IMPORTEXPORT void LQ_CALL LqWrkDelete(LqWrk* This)
{
    if(This->IsThisThread())
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
    TimeStart = LqTimeGetLocMillisec();
    CountConnectionsInQueue = 0;

    UserData = this;
    ExitHandler = ExitHandlerFn;
    IsDelete = false;
    IsSyncAllFlags = 0;
    auto NotifyFd = LqFileEventCreate(LQ_O_NOINHERIT);
    LqEvntFdInit(&NotifyEvent, NotifyFd, LQEVNT_FLAG_RD);
    LqEvntFdIgnoreHandler(&NotifyEvent);
    LqEvntFdIgnoreCloseHandler(&NotifyEvent);
    LqEvntInit(&EventChecker);
    if(IsStart)
        StartSync();
}

LqWrk::~LqWrk()
{
    EndWorkSync();
    CloseAllEvntSync();
    LqEvntUninit(&EventChecker);
    LqFileClose(NotifyEvent.Fd);
}

ullong LqWrk::GetId() const
{
    return Id;
}

void LqWrk::DelProc(void* Data, LqEvntHdr* Hdr)
{
	if(Hdr->Flag & _LQEVNT_FLAG_NOW_EXEC)
		return;
    LqWrkCallEvntHdrCloseHandler(Hdr, Data);
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
            {   
				/*
                * Adding new connection.
                */
                LqEvntHdr* Connection = Command.Val<LqEvntHdr*>();
                Command.Pop<LqEvntHdr*>();
                CountConnectionsInQueue--;
                AddEvntSync(Connection);
            }
            break;
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT:
            {      
				/*
                * Remove zombie connections by time val.
                */
                auto TimeLiveMilliseconds = Command.Val<LqTimeMillisec>();
                Command.Pop<LqTimeMillisec>();
				auto CurTime = LqTimeGetLocMillisec();
				Lock();
				lqevnt_enum_do(EventChecker, i)
				{
					auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
					if(LqEvntIsConn(Evnt) && ((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
					{
						LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
						LqEvntRemoveByInterator(&EventChecker, &i);
						LqWrkCallConnCloseHandler(Evnt, this);
					}
				}lqevnt_enum_while(EventChecker);
				Unlock();
            }
            break;
            case LQWRK_CMD_RM_CONN_ON_TIME_OUT_PROTO:
            {
                auto TimeLiveMilliseconds = Command.Val<LqWrkCmdCloseByTimeout>().TimeLive;
                auto Proto = Command.Val<LqWrkCmdCloseByTimeout>().Proto;
                Command.Pop<LqWrkCmdCloseByTimeout>();

				auto CurTime = LqTimeGetLocMillisec();
				Lock();
				lqevnt_enum_do(EventChecker, i)
				{
					auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
					if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto) && Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
					{
						LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
						LqEvntRemoveByInterator(&EventChecker, &i);
						LqWrkCallConnCloseHandler(Evnt, this);
					}
				}lqevnt_enum_while(EventChecker);
				Unlock();
            }
            break;
            case LQWRK_CMD_WAIT_EVENT:
			{
				/*
				Call procedure for wait event.
				*/
				Command.Val<LqWrkCmdWaitEvnt>().EventAct(Command.Val<LqWrkCmdWaitEvnt>().UserData);
				Command.Pop<LqWrkCmdWaitEvnt>();
			}
			break;
            case LQWRK_CMD_CLOSE_ALL_CONN:
			{
				/*
				Close all waiting connections.
				*/
				Command.Pop();
				Lock();
				lqevnt_enum_do(EventChecker, i)
				{
					auto Evnt = LqEvntRemoveByInterator(&EventChecker, &i);
					LqWrkCallEvntHdrCloseHandler(Evnt, this);
				}lqevnt_enum_while(EventChecker);
				Unlock();
			}
            break;
            case LQWRK_CMD_RM_CONN_BY_IP:
			{
				CloseConnByIpSync(&Command.Val<LqConnInetAddress>().Addr);
				Command.Pop<LqConnInetAddress>();
			}
            break;
            case LQWRK_CMD_CLOSE_CONN_BY_PROTO:
            {
                auto Proto = Command.Val<const LqProto*>();
                Command.Pop<const LqProto*>();
				Lock();
				lqevnt_enum_do(EventChecker, i)
				{
					auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
					if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto))
					{
						LQ_LOG_USER("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
						LqEvntRemoveByInterator(&EventChecker, &i);
						LqWrkCallConnCloseHandler(Evnt, this);
					}
				}lqevnt_enum_while(EventChecker);
				Unlock();
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
                CountConnectionsInQueue--;
            }
            break;
            default:
                Command.JustPop();
        }
    }
}

/*
 Main worker loop
*/
void LqWrk::BeginThread()
{
    LQ_LOG_DEBUG("LqWrk::BeginThread()#%llu start worker thread\n", Id);
#if !defined(LQPLATFORM_WINDOWS)
    signal(SIGPIPE, SIG_IGN);
#endif
    Lock();
    LqEvntThreadInit(&EventChecker);
    LqEvntAddHdr(&EventChecker, (LqEvntHdr*)&NotifyEvent);
    Unlock();
    while(true)
    {
        if(LqFileEventReset(NotifyEvent.Fd))
        {
            if(LqThreadBase::IsShouldEnd)
                break;
            ParseInputCommands();
            if(LqThreadBase::IsShouldEnd)
                break;
            uintptr_t Expected = 1;
            if(LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)0))
            {
                Lock();
                LqEvntUpdateAllMask(&EventChecker, this, DelProc, false);
                Unlock();
            }
        }

        Lock();
        lqevnt_enum_changes_do(EventChecker, Revent)
        {
            auto EvntHdr = LqEvntGetHdrByCurrent(&EventChecker);
            auto OldFlags = EvntHdr->Flag;
            EvntHdr->Flag |= _LQEVNT_FLAG_NOW_EXEC;
            if(LqEvntIsConn(EvntHdr))
            {
                if(Revent & (LQEVNT_FLAG_ERR | LQEVNT_FLAG_WR | LQEVNT_FLAG_RD | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT))
                {
                    LqWrkCallConnHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqEvntGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            } else
            {
                if(Revent & (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_ERR | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_ACCEPT))
                {
                    LqWrkCallEvntFdHandler(EvntHdr, Revent, this);
                    //Is removed current connection in handler
                    if(LqEvntGetHdrByCurrent(&EventChecker) != EvntHdr)
                        continue;
                }
            }
            if((Revent & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_END)) || (EvntHdr->Flag & LQEVNT_FLAG_END))
            {
                LqEvntRemoveCurrent(&EventChecker);
                LqWrkCallEvntHdrCloseHandler(EvntHdr, this);
            } else
            {
                EvntHdr->Flag &= ~(_LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_USER_SET);
                if(EvntHdr->Flag != OldFlags) //If have event changes
                    LqEvntSetMaskByCurrent(&EventChecker);
                LqEvntUnuseCurrent(&EventChecker);
            }
        }lqevnt_enum_changes_while(EventChecker);
        Unlock();

        LqEvntCheck(&EventChecker, LqTimeGetMaxMillisec());
    }

    RemoveEvnt((LqEvntHdr*)&NotifyEvent);
    LqWrkBoss::GetGlobal()->TransferAllEvnt(this);
    CloseAllEvntSync();
    ClearQueueCommands();
    Lock();
    LqEvntThreadUninit(&EventChecker);
    Unlock();
    LQ_LOG_DEBUG("LqWrk::BeginThread()#%llu end worker thread\n", Id);
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
    size_t Res = 0;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    Lock();
    lqevnt_enum_do(EventChecker, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt))
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
			while(Evnt->Flag & NowExec)
			{
				Unlock();
				LqThreadYield();
				Lock();
				if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
					goto lblContinue;
			}
#endif
			if(((LqConn*)Evnt)->Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
			{
				LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
				LqEvntRemoveByInterator(&EventChecker, &i);
				LqWrkCallConnCloseHandler(Evnt, this);
				Res++;
			}
lblContinue:;
        }
    }lqevnt_enum_while(EventChecker);
    Unlock();
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
    size_t Res = 0;
    auto CurTime = LqTimeGetLocMillisec();
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    Lock();
    lqevnt_enum_do(EventChecker, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto))
        {		
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
			while(Evnt->Flag & NowExec)
			{
				Unlock();
				LqThreadYield(); //Wait until worker thread leave read/write handler
				Lock();
				if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
					goto lblContinue;
			}
#endif
			if(Proto->KickByTimeOutProc((LqConn*)Evnt, CurTime, TimeLiveMilliseconds))
			{
				LQ_LOG_USER("LqWrk::RemoveConnOnTimeOut()#%llu remove connection by timeout\n", Id);
				LqEvntRemoveByInterator(&EventChecker, &i);
				LqWrkCallConnCloseHandler(Evnt, this);
				Res++;
			}
lblContinue:;
        }
    }lqevnt_enum_while(EventChecker);
    Unlock();
    return Res;
}

bool LqWrk::AddEvntAsync(LqEvntHdr* EvntHdr)
{
    if(IsThisThread())
        return AddEvntSync(EvntHdr);
    if(!CommandQueue.Push(LQWRK_CMD_ADD_CONN, EvntHdr))
        return false;
    CountConnectionsInQueue++;
    NotifyThread();
    return true;
}

bool LqWrk::AddEvntSync(LqEvntHdr* EvntHdr)
{
    if(EvntHdr->Flag & LQEVNT_FLAG_END)
    {
        LqEvntHdrClose(EvntHdr);
        return true;
    }
    LQ_LOG_DEBUG("LqWrk::AddEvnt()#%llu event {%i, %llx} recived\n", Id, EvntHdr->Fd, (ullong)EvntHdr->Flag);
    Lock();
    auto Res = LqEvntAddHdr(&EventChecker, EvntHdr);
    Unlock();
    return Res;
}

bool LqWrk::RemoveEvnt(LqEvntHdr* EvntHdr)
{
    bool Res = false;
    Lock();
    lqevnt_enum_do(EventChecker, i)
    {
        if(LqEvntGetHdrByInterator(&EventChecker, &i) == EvntHdr)
        {
            LqEvntRemoveByInterator(&EventChecker, &i);
            Res = true;
            break;
        }
    }lqevnt_enum_while(EventChecker);
    Unlock();
    return Res;
}

bool LqWrk::CloseEvnt(LqEvntHdr* EvntHdr)
{
	intptr_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
	while(true)
	{
		Lock();
		lqevnt_enum_do(EventChecker, i)
		{
			if(LqEvntGetHdrByInterator(&EventChecker, &i) == EvntHdr)
			{
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
				if(EvntHdr->Flag & NowExec)
				{
					Res = -1;
				} else
#endif
				{
					LqEvntRemoveByInterator(&EventChecker, &i);
					Res = 1;
				}
				break;
			}
		}lqevnt_enum_while(EventChecker);
		Unlock();
		if(Res == 1)
		{
			LqEvntHdrClose(EvntHdr);
		} else if(Res == -1)
		{
			LqThreadYield();
			continue;
		}
		break;
	}
	return Res == 1;
}

bool LqWrk::UpdateAllEvntFlagAsync()
{
    uintptr_t Expected = 0;
    LqAtmCmpXchg(IsSyncAllFlags, Expected, (uintptr_t)1);
    NotifyThread();
    return true;
}

int LqWrk::UpdateAllEvntFlagSync()
{
    Lock();
    auto Res = LqEvntUpdateAllMask(&EventChecker, this, DelProc, true);
    Unlock();
    return Res;
}

bool LqWrk::Wait(void(*WaitProc)(void* Data), void* UserData)
{
    if(!CommandQueue.PushBegin<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, LqWrkCmdWaitEvnt(WaitProc, UserData)))
        return false;
    NotifyThread();
    return true;
}

int LqWrk::CloseAllEvntAsync()
{
    if(IsThisThread())
        return CloseAllEvntSync();
    if(!CommandQueue.PushBegin(LQWRK_CMD_CLOSE_ALL_CONN))
        return -1;
    NotifyThread();
    return 0;
}

size_t LqWrk::CloseAllEvntSync()
{
    size_t Ret = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    Lock();
    lqevnt_enum_do(EventChecker, i)
    {
        auto Evnt = LqEvntRemoveByInterator(&EventChecker, &i);
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
		while(Evnt->Flag & NowExec)
		{
			Unlock();
			LqThreadYield();
			Lock();
			if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
				goto lblContinue;
		}
#endif
        LqWrkCallEvntHdrCloseHandler(Evnt, this);
        Ret++;
lblContinue:;
    }lqevnt_enum_while(EventChecker);
    Unlock();
    return Ret;
}

size_t LqWrk::CloseConnByIpSync(const sockaddr* Addr)
{
    size_t Res = 0;
    switch(Addr->sa_family)
    {
        case AF_INET: case AF_INET6: break;
        default: return Res;
    }
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
    Lock();
    lqevnt_enum_do(EventChecker, i)
    {
        auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto->CmpAddressProc((LqConn*)Evnt, Addr)))
        {
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
			while(Evnt->Flag & NowExec)
			{
				Unlock();
				LqThreadYield();
				Lock();
				if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
					goto lblContinue;
			}
#endif
            LqEvntRemoveByInterator(&EventChecker, &i);
            LqWrkCallConnCloseHandler(Evnt, this);
            Res++;
lblContinue:;
        }
    }lqevnt_enum_while(EventChecker);
    Unlock();
    return Res;
}

bool LqWrk::CloseConnByIpAsync(const sockaddr* Addr)
{
    if(IsThisThread())
    {
        CloseConnByIpSync(Addr);
        return true;
    }
    StartThreadLocker.LockWriteYield();
    switch(Addr->sa_family)
    {
        case AF_INET:
        {
            LqConnInetAddress s;
            s.AddrInet = *(sockaddr_in*)Addr;
            if(!CommandQueue.PushBegin<LqConnInetAddress>(LQWRK_CMD_RM_CONN_BY_IP, s))
            {
                StartThreadLocker.UnlockWrite();
                return false;
            }
        }
        break;
        case AF_INET6:
        {
            LqConnInetAddress s;
            s.AddrInet6 = *(sockaddr_in6*)Addr;
            if(!CommandQueue.PushBegin<LqConnInetAddress>(LQWRK_CMD_RM_CONN_BY_IP, s))
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

bool LqWrk::CloseConnByProtoAsync(const LqProto * Proto)
{
    if(IsThisThread())
    {
        CloseConnByProtoSync(Proto);
        return true;
    }
    StartThreadLocker.LockWriteYield();
    auto Res = CommandQueue.PushBegin<const LqProto*>(LQWRK_CMD_CLOSE_CONN_BY_PROTO, Proto);
    StartThreadLocker.UnlockWrite();
    NotifyThread();
    return Res;
}

size_t LqWrk::CloseConnByProtoSync(const LqProto * Proto)
{
	size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
	Lock();
	lqevnt_enum_do(EventChecker, i)
	{
		auto Evnt = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(Evnt) && (((LqConn*)Evnt)->Proto == Proto))
		{
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
			while(Evnt->Flag & NowExec)
			{
				Unlock();
				LqThreadYield();
				Lock();
				if(LqEvntGetHdrByInterator(&EventChecker, &i) != Evnt)
					goto lblContinue;
			}
#endif
			LQ_LOG_USER("LqWrk::CloseConnByProto()#%llu remove connection by protocol\n", Id);
			LqEvntRemoveByInterator(&EventChecker, &i);
			LqWrkCallConnCloseHandler(Evnt, this);
			Res++;
lblContinue:;
		}
	}lqevnt_enum_while(EventChecker);
	Unlock();
	return Res;
}

size_t LqWrk::EnumCloseRmEvnt(void* UserData, unsigned(*Proc)(void *UserData, LqEvntHdr* Conn))
{
	size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
	Lock();
	lqevnt_enum_do(EventChecker, i)
	{
		auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
		unsigned Act;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
		while(Hdr->Flag & NowExec)
		{
			Unlock();
			LqThreadYield();
			Lock();
			if(LqEvntGetHdrByInterator(&EventChecker, &i) != Hdr)
				goto lblContinue;
		}
#endif
		Act = Proc(UserData, Hdr); /* !! Not unlocket for safety !! in @Proc not call @LqEvntHdrClose (If you want, use async method or just return 2)*/
		if(Act > 0)
		{
			Res++;
			LqEvntRemoveByInterator(&EventChecker, &i);
			if(Act > 1)
				LqWrkCallEvntHdrCloseHandler(Hdr, this);
		}
lblContinue:;
	}lqevnt_enum_while(EventChecker);
	Unlock();
	return Res;
}

size_t LqWrk::EnumCloseRmEvntByProto(const LqProto * Proto, void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr *Conn))
{
	size_t Res = 0;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
	const LqEvntFlag NowExec = IsThisThread() ? 0 : _LQEVNT_FLAG_NOW_EXEC;
#endif
	Lock();
	lqevnt_enum_do(EventChecker, i)
	{
		auto Hdr = LqEvntGetHdrByInterator(&EventChecker, &i);
		if(LqEvntIsConn(Hdr) && (((LqConn*)Hdr)->Proto == Proto))
		{
			unsigned Act;
#ifdef LQWRK_ENABLE_RW_HNDL_PROTECT
			while(Hdr->Flag & NowExec)
			{
				Unlock();
				LqThreadYield();
				Lock();
				if(LqEvntGetHdrByInterator(&EventChecker, &i) != Hdr)
					goto lblContinue;
			}
#endif
			Act = Proc(UserData, Hdr); /* !! Not unlocket for safety !! in @Proc not call @LqEvntHdrClose (If you want, use async method or just return 2)*/
			if(Act > 0)
			{
				Res++;
				LqEvntRemoveByInterator(&EventChecker, &i);
				if(Act > 1)
					LqWrkCallConnCloseHandler(Hdr, this);
			}
lblContinue:;
		}
	}lqevnt_enum_while(EventChecker);
	Unlock();
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
        "--------------\n"
		" Worker Id: %llu\n"
        " Time start: %s (%s)\n"
        " Count conn. & event obj. in process: %u\n"
        " Count conn. & event obj. in queue: %u\n"
        " Common conn. & event obj.: %u\n",
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
        "~~~~~~~~~~~~~~\n Common worker info\n" +
        DebugInfo() +
        "--------------\n Thread info\n" +
        LqThreadBase::DebugInfo() +
        "--------------\n";
    Lock();
    ullong CurTime = LqTimeGetLocMillisec();
    int k = 0;
    lqevnt_enum_do(EventChecker, i)
    {
        auto Conn = LqEvntGetHdrByInterator(&EventChecker, &i);
        if(LqEvntIsConn(Conn))
        {
            r += " Conn #" + LqToString(k) + "\n";
            char* DbgInf = ((LqConn*)Conn)->Proto->DebugInfoProc((LqConn*)Conn); /*!!! Not call another worker methods in this function !!!*/
            if(DbgInf != nullptr)
            {
                r += DbgInf;
                r += "\n";
                free(DbgInf);
            }
            k++;
        }
    }lqevnt_enum_while(EventChecker);
    Unlock();
    r += "~~~~~~~~~~~~~~\n";
    return r;
}

size_t LqWrk::GetAssessmentBusy() const { return CountConnectionsInQueue + LqEvntCount(&EventChecker); }

size_t LqWrk::CountEvnts() const { return LqEvntCount(&EventChecker); }

void LqWrk::NotifyThread() { LqFileEventSet(NotifyEvent.Fd); }

