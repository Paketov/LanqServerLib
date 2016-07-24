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
    void (*EventAct)(void* Data);
    void* UserData;
    inline LqWrkCmdWaitEvnt() {};
    inline LqWrkCmdWaitEvnt(void (*NewEventProc)(void* Data), void* NewUserData):
    EventAct(NewEventProc), UserData(NewUserData) {}
};


struct LqWrkCmdTakeAllConn
{
    void (*TakeProc)(void* Data, LqListConn& Connection);
    void* UserData;
    inline LqWrkCmdTakeAllConn() {};
    inline LqWrkCmdTakeAllConn(void (*TakeEventProc)(void* Data, LqListConn& Connection), void* NewUserData):
    TakeProc(TakeEventProc), UserData(NewUserData) {}
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

LqWrk::~LqWrk() { EndWorkSync(); }

ullong LqWrk::GetId() const { return Id; }

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
                LqConn* Connection = Command.Val<LqConn*>();
                Command.Pop<LqConn*>();
                CountConnectionsInQueue--;
                AddConn(Connection);
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
            CloseAllConn();
            break;
        case LQWRK_CMD_TAKE_ALL_CONN:
            TakeAllConn(Command.Val<LqWrkCmdTakeAllConn>().TakeProc,
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
            LqEvntUnlock(&EventChecker, Command.Val<LqConn*>());
            Command.Pop<LqConn*>();
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
                auto Connection = Command.Val<LqConn*>();
                Connection->Proto->EndConnProc(Connection);
                Command.Pop<LqConn*>();
            }
            break;
            default:
                Command.JustPop();
        }
    }
}

void LqWrk::CloseAllConn()
{
    LqEvntConnInterator Interator;
    if(LqEvntEnumConnBegin(&EventChecker, &Interator))
    {
        do
        {
            auto Connection = LqEvntGetClientByConnInterator(&EventChecker, &Interator);
            Connection->Proto->EndConnProc(Connection);
            LqEvntRemoveByConnInterator(&EventChecker, &Interator);
        } while(LqEvntEnumConnNext(&EventChecker, &Interator));
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
                auto Connection = Command.Val<LqConn*>();
                Connection->Proto->EndConnProc(Connection);
                Command.Pop<LqConn*>();
            }
            break;
            default:
                Command.JustPop();
        }
    }
}

void LqWrk::RemoveConnInList(LqListConn& Dest)
{
    LqEvntConnInterator Interator;
    if(LqEvntEnumConnBegin(&EventChecker, &Interator))
    {
        do
        {
            auto Connection = LqEvntGetClientByConnInterator(&EventChecker, &Interator);
            if(!Dest.Add(Connection))
            {
                Connection->Proto->EndConnProc(Connection);
                LQ_ERR("Not remove connection in list from #%llu worker", Id);
            }
            LqEvntRemoveByConnInterator(&EventChecker, &Interator);
        } while(LqEvntEnumConnNext(&EventChecker, &Interator));
    }
}

void LqWrk::RemoveConnInListFromCmd(LqListConn& Dest)
{
    for(auto Command = CommandQueue.SeparateBegin(); !CommandQueue.SeparateIsEnd(Command);)
    {
        switch(Command.Type)
        {
            case LQWRK_CMD_ADD_CONN:
            {
                /* Is command - adding connection, add this connection in List*/
                auto Connection = Command.Val<LqConn*>();
                if(!Dest.Add(Connection))
                {
                    LQ_ERR("LqWrk::RemoveConnInListFromCmd: not alloc memory in list.");
                    Connection->Proto->EndConnProc(Connection);
                }
                Command.Pop<LqConn*>();
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
            if(LqThreadBase::IsShouldEnd) goto lblOut;
            ParseInputCommands();
            if(LqThreadBase::IsShouldEnd)
            {
lblOut:
                CloseAllConn();
                ClearQueueCommands();
                return;
            }
        }
        //Проверка всех подключений на предмет входящих данных
        for(LqConnFlag Revent = LqEvntEnumEventBegin(&EventChecker); Revent != 0; Revent = LqEvntEnumEventNext(&EventChecker))
        {
            auto c = LqEvntGetClientByEventInterator(&EventChecker);
            auto OldFlags = c->Flag;
            if(Revent & LQCONN_FLAG_WR)
                c->Proto->WriteProc(c);
            if(Revent & LQCONN_FLAG_RD)
                c->Proto->ReciveProc(c);
            if(((Revent & (LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP)) || (c->Flag & LQCONN_FLAG_END)) && !LqConnIsLock(c))
            {
				LqEvntUnuseClientByEventInterator(&EventChecker);
                LqEvntRemoveByEventInterator(&EventChecker);
                c->Proto->EndConnProc(c);
            } else
            {
                if(c->Flag != OldFlags) //If have event changes
                    LqEvntSetMaskByEventInterator(&EventChecker);
				LqEvntUnuseClientByEventInterator(&EventChecker);
            }
        }
        LqEvntCheck(&EventChecker, LqTimeGetMaxMillisec());
    }
}


bool LqWrk::AddConn(LqConn* Connection)
{
	LQ_LOG_DEBUG("Connection recived from boss\n");
    return LqEvntAddConnection(&EventChecker, Connection);
}

void LqWrk::TakeAllConn(void(*TakeEventProc)(void *Data, LqListConn &Connection), void * NewUserData)
{
    LqListConn DestList;
    RemoveConnInList(DestList);
    TakeEventProc(NewUserData, DestList);
}

void LqWrk::RemoveConnByIp(const sockaddr* Addr)
{
    switch(Addr->sa_family)
    {
        case AF_INET: case AF_INET6: break;
        default: return;
    }
    LqEvntConnInterator Interator;
	for(auto r = LqEvntEnumConnBegin(&EventChecker, &Interator); r; r = LqEvntEnumConnNext(&EventChecker, &Interator))
	{
		auto Connection = LqEvntGetClientByConnInterator(&EventChecker, &Interator);
		if(Connection->Proto->CmpAddressProc(Connection, Addr))
		{
			LqEvntRemoveByConnInterator(&EventChecker, &Interator);
			Connection->Proto->EndConnProc(Connection);
		}
	}
}


size_t LqWrk::AddConnections(LqListConn& ConnectionList)
{
    size_t CountAdded = 0;
    for(size_t i = 0; i < ConnectionList.GetCount(); i++)
    {
        if(!AddConn(ConnectionList[i]))
        {
            ConnectionList[i]->Proto->EndConnProc(ConnectionList[i]);
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
    LqEvntConnInterator Interator;
    auto CurTime = LqTimeGetLocMillisec();
    for(auto r = LqEvntEnumConnBegin(&EventChecker, &Interator); r; r = LqEvntEnumConnNext(&EventChecker, &Interator))
    {
        auto c = LqEvntGetClientByConnInterator(&EventChecker, &Interator);
        if(c->Proto->KickByTimeOutProc(c, CurTime, TimeLiveMilliseconds))
        {
            LQ_LOG_USER("Remove connection  by timeout");
            LqEvntRemoveByConnInterator(&EventChecker, &Interator);
            c->Proto->EndConnProc(c);
        }
    }
}

size_t LqWrk::AddConnListAsync(LqListConn& ConnectionList)
{
    size_t r = 0;
    for(size_t i = 0; i < ConnectionList.GetCount(); i++)
    {
        if(ConnectionList[i] == nullptr)
            continue;
        if(!AddConnAsync(ConnectionList[i]))
        {
            ConnectionList[i]->Proto->EndConnProc(ConnectionList[i]);
            LQ_ERR("Not adding #%u connection in #%llu worker", i, Id);
        } else
        {
            r++;
        }
        ConnectionList[i] = nullptr;
    }
    return r;
}

size_t LqWrk::AddConnListSync(LqListConn& ConnectionList)
{
    LockWrite();
    auto r = AddConnections(ConnectionList);
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

bool LqWrk::AddConnAsync(LqConn* Connection)
{
    if(!CommandQueue.Push(LQWRK_CMD_ADD_CONN, Connection))
        return false;
    CountConnectionsInQueue++;
    NotifyThread();
    return true;
}

bool LqWrk::AddConnSync(LqConn* Connection)
{
    LockWrite();
    auto r = AddConn(Connection);
    UnlockWrite();
    return r;
}

bool LqWrk::UnlockConnAsync(LqConn* Connection)
{
    if(!CommandQueue.PushBegin<LqConn*>(LQWRK_CMD_UNLOCK_CONN, Connection))
        return false;
    NotifyThread();
    return true;
}

int LqWrk::UnlockConnSync(LqConn* Connection)
{
    LockWrite();
    auto r = LqEvntUnlock(&EventChecker, Connection);
    UnlockWrite();
    return r;
}

bool LqWrk::WaitEvent(void (*NewEventProc)(void* Data), void* NewUserData)
{
    if(!CommandQueue.PushBegin<LqWrkCmdWaitEvnt>(LQWRK_CMD_WAIT_EVENT, LqWrkCmdWaitEvnt(NewEventProc, NewUserData)))
        return false;
    NotifyThread();
    return true;
}

bool LqWrk::CloseAllConnAsync()
{
    if(!CommandQueue.PushBegin(LQWRK_CMD_CLOSE_CONN))
        return false;
    NotifyThread();
    return true;
}

void LqWrk::CloseAllConnSync()
{
    LockWrite();
    CloseAllConn();
    UnlockWrite();
}

bool LqWrk::TakeAllConnAsync(void(*TakeEventProc)(void *Data, LqListConn &ConnectionList), void * NewUserData)
{
    if(!CommandQueue.PushBegin<LqWrkCmdTakeAllConn>(LQWRK_CMD_TAKE_ALL_CONN, LqWrkCmdTakeAllConn(TakeEventProc, NewUserData)))
        return false;
    NotifyThread();
    return true;
}

bool LqWrk::TakeAllConn(LqListConn & ConnectionList)
{
    LockWrite();
    RemoveConnInList(ConnectionList);
    RemoveConnInListFromCmd(ConnectionList);
    UnlockWrite();
    return true;
}

bool LqWrk::TakeAllConnSync(LqListConn& ConnectionList)
{
    LockWrite();
    RemoveConnInList(ConnectionList);
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
    StartThreadLocker.LockWrite();
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

void LqWrk::EnumConn(void* UserData, void(*Proc)(void *UserData, LqConn *Conn))
{
	LockWrite();
	LqEvntConnInterator Interator;
	for(auto r = LqEvntEnumConnBegin(&EventChecker, &Interator); r; r = LqEvntEnumConnNext(&EventChecker, &Interator))
	{
		auto Connection = LqEvntGetClientByConnInterator(&EventChecker, &Interator);
		Proc(UserData, Connection);
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

    LqEvntConnInterator Interator;
    ullong CurTime = LqTimeGetLocMillisec();
    if(LqEvntEnumConnBegin(&EventChecker, &Interator))
    {
        int k = 0;
        do
        {
            auto Connection = LqEvntGetClientByConnInterator(&EventChecker, &Interator);
            r += "Connection #" + LqToString(k) + "\n";
            char* DbgInf = Connection->Proto->DebugInfoProc(Connection);
            if(DbgInf != nullptr)
            {
                r += DbgInf;
                r += "\n";
                free(DbgInf);
            }
            k++;
        } while(LqEvntEnumConnNext(&EventChecker, &Interator));
    }
    UnlockRead();
    r += "---------\n";
    return r;
}

size_t LqWrk::GetAssessmentBusy() const { return CountConnectionsInQueue + LqEvntCount(&EventChecker); }

bool LqWrk::LockWrite()
{
    if(std::this_thread::get_id() == get_id())
    {
        SafeReg.EnterSafeRegionAndSwitchToWriteMode();
        return true;
    }
    SafeReg.OccupyWrite();
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
    if(std::this_thread::get_id() == this->get_id())
    {
        SafeReg.EnterSafeRegionAndSwitchToReadMode();
        return true;
    }
    SafeReg.OccupyRead();
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
