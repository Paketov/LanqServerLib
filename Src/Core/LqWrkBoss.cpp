/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkBoss - Accept clients and send him to workers.
*/

#include "LqOs.h"
#include "LqConn.h"
#include "LqLog.h"
#include "LqWrk.hpp"
#include "LqWrkBoss.hpp"
#include "Lanq.h"
#include "LqTime.h"
#include "LqStr.h"
#include "LqFile.h"

#include <fcntl.h>
#include <string>
#include <string.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#undef max

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqBossCmdTransfConnAndEnd
{
    LqListEvnt				List;
    LqWrk*					Wrk;
    inline LqBossCmdTransfConnAndEnd(LqListEvnt& nList, LqWrk* nWorker): List(nList), Wrk(nWorker) {}
    inline LqBossCmdTransfConnAndEnd() {}
};

#pragma pack(pop)

enum LqWrkBossCommands
{
    LQBOSS_CMD_TRANSFER_CONN,
    LQBOSS_CMD_TRANSFER_CONN_AND_END,
    LQBOSS_CMD_ADD_CONN,
    LQBOSS_CMD_REBIND
};

static ullong __IdGen = 0;
static LqLocker<uchar> __IdGenLocker;

static ullong GenId()
{
    ullong r;
    __IdGenLocker.LockWriteYield();
    r = __IdGen;
    __IdGen++;
    __IdGenLocker.UnlockWrite();
    return r;
}


LqWrkBoss::LqWrkBoss():
    Port("80"),
    MaxConnections(2000),
    ProtoReg(nullptr),
    CountConnIgnored(0),
    CountConnAccepted(0),
    TransportProtoFamily(AF_UNSPEC),
    ErrBind(0),
    IsRebind(false),
	ZombieKillerIsSyncCheck(false),
	ZombieKillerTimeLiveConnMillisec(40 * 1000),
    LqThreadBase(([&] { Id = GenId(); LqString Str = "Boss #" + LqToString(Id); return Str; })().c_str())
{
	int TimerFd = LqFileTimerCreate(LQ_O_NOINHERIT);
	if(TimerFd == -1)
		LQ_ERR("LqWrkBoss::LqWrkBoss(): not create ZombieKiller timer for %s\n", this->Name);

	LqEvntFdInit(&ZombieKiller, TimerFd, this, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
	ZombieKiller.Handler = ZombieKillerHandler;
	ZombieKiller.CloseHandler = ZombieKillerHandlerClose;

    Sock.Fd = -1;
    Sock.Proto = nullptr;
    LqEvntInit(&EventChecker);
}

LqWrkBoss::LqWrkBoss(LqProto* ProtocolRegistration):
    Port("80"),
    MaxConnections(2000),
    ProtoReg(ProtocolRegistration),
    CountConnIgnored(0),
    CountConnAccepted(0),
    ErrBind(0),
    IsRebind(false),
    TransportProtoFamily(AF_UNSPEC),
	ZombieKillerIsSyncCheck(false),
	ZombieKillerTimeLiveConnMillisec(40 * 1000),
    LqThreadBase(([&] { Id = GenId(); LqString Str = "Boss #" + LqToString(Id); return Str; })().c_str())
{
	int TimerFd = LqFileTimerCreate(LQ_O_NOINHERIT);
	if(TimerFd == -1)
		LQ_ERR("LqWrkBoss::LqWrkBoss(): not create ZombieKiller timer for %s\n", this->Name);
	LqEvntFdInit(&ZombieKiller, TimerFd, this, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
	ZombieKiller.Handler = ZombieKillerHandler;
	ZombieKiller.CloseHandler = ZombieKillerHandlerClose;
	ZombieKiller.UserData = 0;


    Sock.Fd = -1;
    Sock.Proto = nullptr;
    LqEvntInit(&EventChecker);
    ProtoReg->Boss = this;
}

LqWrkBoss::~LqWrkBoss()
{ 
	EndWorkSync();
	if(ZombieKiller.Fd != -1)
	{
		LqEvntSetClose(&ZombieKiller);
		volatile uintptr_t* UsrData = &ZombieKiller.UserData;
		while(*UsrData != 0)
			LqThreadYield();
		LqFileClose(ZombieKiller.Fd);
	}
	for(size_t i = 0; i < WorkersCount; i++)
		Workers[i].~LqWorkerPtr();
	if(Workers != nullptr)
		free(Workers);
	WorkersCount = 0;
    if(ProtoReg != nullptr)
        ProtoReg->FreeProtoNotifyProc(ProtoReg);
    LqEvntUninit(&EventChecker);
}

void LqWrkBoss::SetPrt(const char * Name)
{
    LockerBind.LockWriteYield();
    Port = Name;
    LockerBind.UnlockWrite();
    Rebind();
}

void LqWrkBoss::GetPrt(char * DestName, size_t DestLen)
{
    LockerBind.LockReadYield();
    LqStrCopyMax(DestName, Port.c_str(), DestLen);
    LockerBind.UnlockRead();
}

void LqWrkBoss::SetProtocolFamily(int Val)
{
    LockerBind.LockWriteYield();
    TransportProtoFamily = Val;
    LockerBind.UnlockWrite();
    Rebind();
}

int LqWrkBoss::GetProtocolFamily()
{
    LockerBind.LockReadYield();
    auto r = TransportProtoFamily;
    LockerBind.UnlockRead();
    return r;
}

void LqWrkBoss::SetMaxConn(int Val)
{
    LockerBind.LockWriteYield();
    MaxConnections = Val;
    LockerBind.UnlockWrite();
    Rebind();
}

int LqWrkBoss::GetMaxConn()
{
    LockerBind.LockReadYield();
    auto r = MaxConnections;
    LockerBind.UnlockRead();
    return r;
}

void LqWrkBoss::Rebind()
{
    IsRebind = true;
    if(!IsShouldEnd)
        NotifyThread();
}

bool LqWrkBoss::SetTimeLifeConn(LqTimeMillisec TimeLife)
{
	ZombieKillerTimeLiveConnMillisec = TimeLife;
	if(ZombieKiller.Fd != -1)
	{
		return LqFileTimerSet(ZombieKiller.Fd, ZombieKillerTimeLiveConnMillisec / 2) == 0;
	}
	return -1;
}

LqTimeMillisec LqWrkBoss::GetTimeLifeConn() const
{
	return ZombieKillerTimeLiveConnMillisec;
}


ullong LqWrkBoss::GetId() const
{
    return Id;
}

bool LqWrkBoss::UnbindSock()
{
    LqEvntInterator Interator;
    if(LqEvntEnumBegin(&EventChecker, &Interator))
    {
        auto Fd = LqEvntGetHdrByInterator(&EventChecker, &Interator)->Fd;
        LqEvntRemoveByInterator(&EventChecker, &Interator);
        closesocket(Fd);
        return true;
    }
    return false;
}

bool LqWrkBoss::Bind()
{
    LockerBind.LockWriteYield();
    UnbindSock();
    static const int True = 1;
    int s;
    addrinfo *Addrs = nullptr, HostInfo = {0};
    HostInfo.ai_family = TransportProtoFamily;
    HostInfo.ai_socktype = SOCK_STREAM;
    HostInfo.ai_flags = AI_PASSIVE;//AI_ALL;
    HostInfo.ai_protocol = IPPROTO_TCP;
    int res;
    if((res = getaddrinfo((Host.length() > 0) ? Host.c_str() : (const char*)nullptr, Port.c_str(), &HostInfo, &Addrs)) != 0)
    {
        ErrBind = lq_errno;
        LQ_ERR("getaddrinfo() failed \"%s\" ", gai_strerror(res));
        LockerBind.UnlockWrite();
        return false;
    }

    for(auto i = Addrs; i != nullptr; i = i->ai_next)
    {
        if((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
            continue;
		LqFileDescrSetInherit(s, 0);
        if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&True, sizeof(True)) == -1)
        {
            ErrBind = lq_errno;
            LQ_ERR("setsockopt() failed");
            continue;
        }

        if(LqConnSwitchNonBlock(s, 1))
        {
            ErrBind = lq_errno;
            LQ_ERR("not swich socket to non blocket mode");
            continue;
        }

        if(i->ai_family == AF_INET6)
        {
            if(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&True, sizeof(True)) == -1)
            {
                ErrBind = lq_errno;
                LQ_ERR("setsockopt() failed");
                continue;
            }
        }
        if(bind(s, i->ai_addr, i->ai_addrlen) == -1)
        {
            ErrBind = lq_errno;
            LQ_ERR("bind() failed with error: %s\n", strerror(lq_errno));
            closesocket(s);
            s = -1;
            continue;
        }
        if(listen(s, MaxConnections) == -1)
        {
            ErrBind = lq_errno;
            LQ_ERR("bind() failed");
            closesocket(s);
            s = -1;
            continue;
        }
        break;
    }

    if(Addrs != nullptr)
        freeaddrinfo(Addrs);
    if(s == -1)
    {
        ErrBind = lq_errno;
        LQ_ERR("not binded to sock");
        LockerBind.UnlockWrite();
        return false;
    }
    Sock.Flag = _LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_CONN;
    LqEvntSetFlags(&Sock, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
    Sock.Fd = s;
    if(!LqEvntAddHdr(&EventChecker, (LqEvntHdr*)&Sock))
    {
        ErrBind = lq_errno;
        LQ_ERR("not adding sock in event checker");
        closesocket(s);
        LockerBind.UnlockWrite();
        return false;
    }
    ErrBind = 0;
    LockerBind.UnlockWrite();
    return true;
}

LqProto* LqWrkBoss::RegisterProtocol(LqProto* ConnectManager)
{
    LqProto* r;
    WorkerListLocker.LockWriteYield();
    ProtoReg->FreeProtoNotifyProc(ProtoReg);
    r = ProtoReg;
    ProtoReg = ConnectManager;
    WorkerListLocker.UnlockWrite();
    return r;
}

LqProto* LqWrkBoss::GetProto()
{
    return ProtoReg;
}

void LqWrkBoss::BeginThread()
{
    /*
    In this function must be only one thread.
    This thread have rw privileges for change worker list.
    Another thread for read worker list, must blocking list.
    */
    size_t		MinimumConnectionsInWorker;
    int			ClientFd;
    socklen_t	ClientAddrLen;
    uint		IndexWorker;
    LqConn*		RegistredConnection;
    LqEvntFlag	Revent;
    IsRebind = false;
    if(!Bind())
    {
        LQ_ERR("not bind socket");
        return;
    }
    StartAllWorkersAsync();
    union
    {
        sockaddr			Addr;
        sockaddr_in			AddrInet;
        sockaddr_in6		AddrInet6;
        sockaddr_storage	AddrStorage;
    } ClientAddr;
    while(true)
    {
        LqEvntCheck(&EventChecker, LqTimeGetMaxMillisec());
        if(LqEvntSignalCheckAndReset(&EventChecker))
        {
            ParseInputCommands();
            if(IsShouldEnd) break;
            if(IsRebind)
            {
                IsRebind = false;
                if(!Bind())
                {
                    LQ_ERR("not rebind socket");
                    break;
                }
            }

        }
        if((Revent = LqEvntEnumEventBegin(&EventChecker)) == 0)
            continue;
        if(Revent & LQEVNT_FLAG_RD)
        {
            LQ_LOG_DEBUG("Has connect request\n");
            WorkerListLocker.LockReadYield();
            IndexWorker = MinBusyWithoutLock(&MinimumConnectionsInWorker);
            if(MinimumConnectionsInWorker > MaxConnections)
            {
                if((ClientFd = accept(Sock.Fd, nullptr, nullptr)) != -1) closesocket(ClientFd);
                WorkerListLocker.UnlockRead();
                continue;
            }
            ClientAddrLen = sizeof(ClientAddr);

            if((ClientFd = accept(Sock.Fd, &ClientAddr.Addr, &ClientAddrLen)) == -1)
            {
                LQ_ERR("Client not accepted\n");
                WorkerListLocker.UnlockRead();
                continue;
            }
            LqWorkerPtr TargetWorker = Workers[IndexWorker];
            if((RegistredConnection = ProtoReg->NewConnProc(ProtoReg, ClientFd, &ClientAddr.Addr)) == nullptr)
            {
                CountConnIgnored++;
                LQ_LOG_DEBUG("Connection has aborted by application protocol\n");
                WorkerListLocker.UnlockRead();
                continue;
            }
            TargetWorker->AddEvntAsync((LqEvntHdr*)RegistredConnection);
            CountConnAccepted++;
            WorkerListLocker.UnlockRead();
        } else if(Revent & LQEVNT_FLAG_HUP)
        {
            LQ_ERR("Bind socket disconnect\n");
            LQ_ERR("Try bind again ...\n");
            if(!Bind())
            {
                LQ_ERR("Not binded\n");
                break;
            }
        }
    }
    UnbindSock();
}


bool LqWrkBoss::TransferEvntEnd(LqListEvnt& ConnectionsList, LqWrk* LqWorker)
{
    if(!CommandQueue.Push(LQBOSS_CMD_TRANSFER_CONN_AND_END, LqBossCmdTransfConnAndEnd(ConnectionsList, LqWorker)))
        return false;
    LqEvntSignalSet(&EventChecker);
    return true;
}

bool LqWrkBoss::TransferEvnt(const LqListEvnt& ConnectionsList)
{
    if(!CommandQueue.Push<LqListEvnt>(LQBOSS_CMD_TRANSFER_CONN, ConnectionsList))
        return false;
    LqEvntSignalSet(&EventChecker);
    return true;
}

void LqWrkBoss::ParseInputCommands()
{
    for(auto Command = CommandQueue.Fork(); Command;)
    {
        switch(Command.Type)
        {
            case LQBOSS_CMD_TRANSFER_CONN:
            {
                LqListEvnt& ConnectionList = Command.Val<LqListEvnt>();
                if(!DistributeListConnections(ConnectionList))
                {
                    LQ_LOG_DEBUG("Not destribute list connections");
                }
                Command.Pop<LqListEvnt>();
            }
            break;
            case LQBOSS_CMD_TRANSFER_CONN_AND_END:
            {
                auto& ConnectionList = Command.Val<LqBossCmdTransfConnAndEnd>().List;
                if(!DistributeListConnections(ConnectionList))
                {
                    LQ_LOG_DEBUG("Not destribute list connections");
                }
                LqFastAlloc::Delete(Command.Val<LqBossCmdTransfConnAndEnd>().Wrk);
                Command.Pop<LqBossCmdTransfConnAndEnd>();
            }
            break;
            default:
                Command.JustPop();
        }
    }
}

void LQ_CALL LqWrkBoss::ZombieKillerHandler(LqEvntFd* Fd, LqEvntFlag RetFlags)
{
	auto Ob = (LqWrkBoss*)((char*)Fd - (uintptr_t)&((LqWrkBoss*)0)->ZombieKiller);
	if(RetFlags & LQEVNT_FLAG_RD)
	{
		Ob->WorkerListLocker.LockReadYield();
		for(size_t i = 0, m = Ob->WorkersCount; i < m; i++)
		{
			if(Ob->ZombieKillerIsSyncCheck)
				Ob->Workers[i]->RemoveConnOnTimeOutSync(Ob->ZombieKillerTimeLiveConnMillisec);
			else
				Ob->Workers[i]->RemoveConnOnTimeOutAsync(Ob->ZombieKillerTimeLiveConnMillisec);
		}
		Ob->WorkerListLocker.UnlockRead();
		LqFileTimerSet(Fd->Fd, Ob->ZombieKillerTimeLiveConnMillisec / 2);
	}
}

void LQ_CALL LqWrkBoss::ZombieKillerHandlerClose(LqEvntFd* Fd, LqEvntFlag RetFlags)
{
	Fd->UserData = 0;
}

bool LqWrkBoss::DistributeListConnections(const LqListEvnt& List)
{
    bool r = false;
    WorkerListLocker.LockReadYield();
    if(WorkersCount > 0)
    {
        for(size_t i = 0; i < List.GetCount(); i++)
        {
            auto IndexWorker = MinBusyWithoutLock();
            Workers[IndexWorker]->AddEvnt(List[i]);
        }
        r = true;
    }
    WorkerListLocker.UnlockRead();
    return r;
}

size_t LqWrkBoss::CountConnections() const
{
    size_t Summ = 0;
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        Summ += Workers[i]->GetAssessmentBusy();
    WorkerListLocker.UnlockRead();
    return Summ;
}

bool LqWrkBoss::AddWorkers(size_t Count, bool IsStart)
{
    for(uint i = 0; i < Count; i++)
    {
        auto NewWorker = LqWrk::New(IsStart);
        if(NewWorker == nullptr)
        {
            LQ_ERR("Not alloc new worker");
            continue;
        }
        AddWorker(NewWorker);
    }
    return true;
}

bool LqWrkBoss::AddWorker(const LqWorkerPtr& Wrk)
{
    WorkerListLocker.LockWriteYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
    {
        if(Workers[i] == Wrk)
        {
            WorkerListLocker.UnlockWrite();
            return true;
        }
    }
    auto NewWorkers = (LqWorkerPtr*)realloc(Workers, sizeof(LqWorkerPtr) * (WorkersCount + 1));
    if(NewWorkers == nullptr)
    {
        LQ_ERR("Not adding new worker in list");
        WorkerListLocker.UnlockWrite();
        return false;
    }
    new(&(Workers = NewWorkers)[WorkersCount++]) LqWorkerPtr(Wrk);
	if(ZombieKiller.UserData == 0)
	{
		LqFileTimerSet(ZombieKiller.Fd, this->ZombieKillerTimeLiveConnMillisec / 2);
		ZombieKiller.UserData = (uintptr_t)Wrk.Get();
		Wrk->AddEvntAsync((LqEvntHdr*)&ZombieKiller);
	}
    WorkerListLocker.UnlockWrite();
    return true;
}

bool LqWrkBoss::AddEvntAsync(LqEvntHdr* Evnt)
{
    WorkerListLocker.LockReadYield();
    bool Res = true;
    if(WorkersCount > 0)
    {
        auto IndexMinUsed = MinBusyWithoutLock();
        Res = Workers[IndexMinUsed]->AddEvntAsync(Evnt);
    }
    WorkerListLocker.UnlockRead();
    return Res;
}

bool LqWrkBoss::AddEvntSync(LqEvntHdr* Evnt)
{
    WorkerListLocker.LockReadYield();
    bool Res = true;
    if(WorkersCount > 0)
    {
        auto IndexMinUsed = MinBusyWithoutLock();
        Res = Workers[IndexMinUsed]->AddEvntSync(Evnt);
    }
    WorkerListLocker.UnlockRead();
    return Res;
}

size_t LqWrkBoss::CountWorkers() const
{
    return WorkersCount;
}


size_t LqWrkBoss::MinBusyWithoutLock(size_t* MinCount)
{
    size_t Min = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = WorkersCount; i < m; i++)
    {
        size_t l = Workers[i]->GetAssessmentBusy();
        if(l < Min)
            Min = l, Index = i;
    }
    *MinCount = Min;
    return Index;
}

size_t LqWrkBoss::MaxBusyWithoutLock(size_t* MaxCount)
{
    size_t Max = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = WorkersCount; i < m; i++)
    {
        size_t l = Workers[i]->GetAssessmentBusy();
        if(l > Max)
            Max = l, Index = i;
    }
    *MaxCount = Max;
    return Index;
}

LqWorkerPtr LqWrkBoss::operator[](size_t Index) const
{
    LqWorkerPtr r;
    WorkerListLocker.LockReadYield();
    if(Index < WorkersCount)
        r = Workers[Index];
    WorkerListLocker.UnlockRead();
    return r;
}

size_t LqWrkBoss::MinBusy(size_t* MinCount)
{
    WorkerListLocker.LockReadYield();
    auto r = MinBusyWithoutLock(MinCount);
    WorkerListLocker.UnlockRead();
    return r;
}

void LqWrkBoss::StartAllWorkersSync()
{
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        Workers[i]->StartSync();
    WorkerListLocker.UnlockRead();
}

void LqWrkBoss::StartAllWorkersAsync()
{
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        Workers[i]->StartAsync();
    WorkerListLocker.UnlockRead();
}

bool LqWrkBoss::KickWorker(ullong IdWorker)
{
    WorkerListLocker.LockWriteYield();
    /*Lock operation remove from array*/
    for(size_t i = 0, m = WorkersCount; i < m; i++)
    {
        if(Workers[i]->Id == IdWorker)
        {
            if(WorkersCount > 1)
            {
                LqListEvnt Connections;
                Workers[i]->TakeAllEvnt(Connections);
                Workers[(i == 0) ? 1 : 0]->AddEvntListAsync(Connections);
            }
            WorkersCount--;
            if(i != WorkersCount)
                Workers[i] = Workers[WorkersCount];
            Workers[WorkersCount].~LqWorkerPtr();
            Workers = (LqWorkerPtr*)realloc(Workers, sizeof(LqWorkerPtr) * WorkersCount);
            WorkerListLocker.UnlockWrite();
            return true;
        }
    }
    WorkerListLocker.UnlockWrite();
    return false;
}

void LqWrkBoss::KickWorkers(size_t Count)
{
    WorkerListLocker.LockWriteYield();
    bool IsTrasferConnections = true;
    if(Count >= WorkersCount)
    {
        Count = WorkersCount;
        IsTrasferConnections = false;
    }

    if(WorkersCount > 0)
    {
        for(int i = WorkersCount - 1, m = i - Count; i > m; i--)
        {
            if(IsTrasferConnections)
            {
                LqListEvnt Connections;
                Workers[i]->TakeAllEvnt(Connections);
                Workers[0]->AddEvntListAsync(Connections);
            }
            Workers[i].~LqSharedPtr();
        }
        WorkersCount -= Count;
        Workers = (LqWorkerPtr*)realloc(Workers, sizeof(LqWorkerPtr) * WorkersCount);
    }
    WorkerListLocker.UnlockWrite();
}

bool LqWrkBoss::CloseAllEvntAsync()
{
    bool r = true;
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        if(!Workers[i]->CloseAllEvntAsync())
        {
            r = false;
            break;
        }
    WorkerListLocker.UnlockRead();
    return r;
}

void LqWrkBoss::CloseAllEvntSync()
{
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        Workers[i]->CloseAllEvntSync();
    WorkerListLocker.UnlockRead();
}

bool LqWrkBoss::CloseConnByIpAsync(const sockaddr* Addr)
{
    bool r = true;
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        if(!Workers[i]->CloseConnByIpAsync(Addr))
        {
            r = false;
            break;
        }
    WorkerListLocker.UnlockRead();
    return r;
}

void LqWrkBoss::CloseConnByIpSync(const sockaddr* Addr)
{
    WorkerListLocker.LockReadYield();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        Workers[i]->CloseConnByIpSync(Addr);
    WorkerListLocker.UnlockRead();
}

void LqWrkBoss::EnumEvnt(void * UserData, void(*Proc)(void *UserData, LqEvntHdr* Conn))
{
    WorkerListLocker.LockReadYield();
    for(size_t i = 0; i < WorkersCount; i++)
        Workers[i]->EnumEvnt(UserData, Proc);
    WorkerListLocker.UnlockRead();
}

bool LqWrkBoss::SyncEvntFlag(LqEvntHdr* Conn)
{
    WorkerListLocker.LockReadYield();
    for(size_t i = 0; i < WorkersCount; i++)
        Workers[i]->SyncEvntFlagAsync(Conn);
    WorkerListLocker.UnlockRead();
    return true;
}

size_t LqWrkBoss::KickAllWorkers()
{
    WorkerListLocker.LockWriteYield();
    for(size_t i = 0; i < WorkersCount; i++)
        Workers[i].~LqWorkerPtr();
    size_t r = WorkersCount;

    if(Workers != nullptr)
    {
        free(Workers);
        Workers = nullptr;
    }
    WorkersCount = 0;
    WorkerListLocker.UnlockWrite();
    return r;
}

void LqWrkBoss::NotifyThread()
{
    LqEvntSignalSet(&EventChecker);
}

LqString LqWrkBoss::DebugInfo()
{
    std::basic_string<char> r;
    r += "Working on port: " + Port + "\n";
    r += "Connections accepted: " + LqToString((size_t)CountConnAccepted) + "\n";
    r += "Connections ignored: " + LqToString((size_t)CountConnIgnored) + "\n";
    r += "Max connections in queue: " + LqToString(MaxConnections) + "\n";
    r += "Is waiting connection: ";
    r += ((Sock.Fd != -1) ? "1" : "0");
    r += "\n";
    r += "Count workers: " + LqToString(WorkersCount) + "\n";
    return r;
}

