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

#include <fcntl.h>
#include <string>
#include <string.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqBossCmdTransfConnAndEnd
{
    LqListConn				List;
    LqWrk*					Wrk;
    inline LqBossCmdTransfConnAndEnd(LqListConn& nList, LqWrk* nWorker): List(nList), Wrk(nWorker) {}
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
    __IdGenLocker.LockWrite();
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
    LqThreadBase(([&] { Id = GenId(); LqString Str = "Boss #" + LqToString(Id); return Str; })().c_str())
{
    Sock.SockDscr = -1;
    Sock.Proto = nullptr;
    LqEvntInit(&EventChecker);
    Tasks.Add(LqZombieKillerTask::Task::GetPtr());
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
    LqThreadBase(([&] { Id = GenId(); LqString Str = "Boss #" + LqToString(Id); return Str; })().c_str())
{
    Sock.SockDscr = -1;
    Sock.Proto = nullptr;
    LqEvntInit(&EventChecker);
    ProtoReg->LqWorkerBoss = this;
    Tasks.Add(LqZombieKillerTask::Task::GetPtr());
}

LqWrkBoss::~LqWrkBoss()
{
    KickAllWorkers();
    EndWorkSync();
    if(ProtoReg != nullptr)
	ProtoReg->FreeProtoNotifyProc(ProtoReg);
    LqEvntUninit(&EventChecker);
}

void LqWrkBoss::SetPrt(const char * Name)
{
    LockerBind.LockWrite();
    Port = Name;
    LockerBind.UnlockWrite();
    Rebind();
}

void LqWrkBoss::GetPrt(char * DestName, size_t DestLen)
{
    LockerBind.LockRead();
    LqStrCopyMax(DestName, Port.c_str(), DestLen);
    LockerBind.UnlockRead();
}

void LqWrkBoss::SetProtocolFamily(int Val)
{
    LockerBind.LockWrite();
    TransportProtoFamily = Val;
    LockerBind.UnlockWrite();
    Rebind();
}

int LqWrkBoss::GetProtocolFamily()
{
    LockerBind.LockRead();
    auto r = TransportProtoFamily;
    LockerBind.UnlockRead();
    return r;
}

void LqWrkBoss::SetMaxConn(int Val)
{
    LockerBind.LockWrite();
    MaxConnections = Val;
    LockerBind.UnlockWrite();
    Rebind();
}

int LqWrkBoss::GetMaxConn()
{
    LockerBind.LockRead();
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


ullong LqWrkBoss::GetId() const
{
    return Id;
}

bool LqWrkBoss::UnbindSock()
{
    LqEvntConnInterator Interator;
    if(LqEvntEnumConnBegin(&EventChecker, &Interator))
    {
	auto Fd = LqEvntGetClientByConnInterator(&EventChecker, &Interator)->SockDscr;
	LqEvntRemoveByConnInterator(&EventChecker, &Interator);
	closesocket(Fd);
	return true;
    }
    return false;
}

bool LqWrkBoss::Bind()
{
    LockerBind.LockWrite();
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
    Sock.Flag = 0;
    LqConnSetEvents(&Sock, LQCONN_FLAG_RD | LQCONN_FLAG_HUP);
    Sock.SockDscr = s;
    if(!LqEvntAddConn(&EventChecker, &Sock))
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
    WorkerListLocker.LockWrite();
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
    LqConnFlag	Revent;
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
	if(Revent & LQCONN_FLAG_RD)
	{
	    LQ_LOG_DEBUG("Has connect request\n");
	    WorkerListLocker.LockRead();
	    IndexWorker = MinBusyWithoutLock(&MinimumConnectionsInWorker);
	    if(MinimumConnectionsInWorker > MaxConnections)
	    {
		if((ClientFd = accept(Sock.SockDscr, nullptr, nullptr)) != -1) closesocket(ClientFd);
		WorkerListLocker.UnlockRead();
		continue;
	    }
	    ClientAddrLen = sizeof(ClientAddr);

	    if((ClientFd = accept(Sock.SockDscr, &ClientAddr.Addr, &ClientAddrLen)) == -1)
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
	    TargetWorker->AddConnAsync(RegistredConnection);
	    CountConnAccepted++;
	    WorkerListLocker.UnlockRead();
	} else if(Revent & LQCONN_FLAG_HUP)
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


bool LqWrkBoss::TransferConnectionsEnd(LqListConn& ConnectionsList, LqWrk* LqWorker)
{
    if(!CommandQueue.Push(LQBOSS_CMD_TRANSFER_CONN_AND_END, LqBossCmdTransfConnAndEnd(ConnectionsList, LqWorker)))
	return false;
    LqEvntSignalSet(&EventChecker);
    return true;
}

bool LqWrkBoss::TransferConnections(const LqListConn& ConnectionsList)
{
    if(!CommandQueue.Push<LqListConn>(LQBOSS_CMD_TRANSFER_CONN, ConnectionsList))
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
		LqListConn& ConnectionList = Command.Val<LqListConn>();
		if(!DistributeListConnections(ConnectionList))
		{
		    LQ_LOG_DEBUG("Not destribute list connections");
		}
		Command.Pop<LqListConn>();
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

bool LqWrkBoss::DistributeListConnections(const LqListConn& List)
{
    bool r = false;
    WorkerListLocker.LockRead();
    if(WorkersCount > 0)
    {
	for(size_t i = 0; i < List.GetCount(); i++)
	{
	    auto IndexWorker = MinBusyWithoutLock();
	    Workers[IndexWorker]->AddConn(List[i]);
	}
	r = true;
    }
    WorkerListLocker.UnlockRead();
    return r;
}

size_t LqWrkBoss::CountConnections() const
{
    size_t Summ = 0;
    WorkerListLocker.LockRead();
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
    WorkerListLocker.LockWrite();
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
    WorkerListLocker.UnlockWrite();
    return true;
}

bool LqWrkBoss::AddConnAsync(LqConn* Connection)
{
    WorkerListLocker.LockRead();
    bool Res = true;
    if(WorkersCount > 0)
    {
	auto IndexMinUsed = MinBusyWithoutLock();
	Res = Workers[IndexMinUsed]->AddConnAsync(Connection);
    }
    WorkerListLocker.UnlockRead();
    return Res;
}

bool LqWrkBoss::AddConnSync(LqConn* Connection)
{
    WorkerListLocker.LockRead();
    bool Res = true;
    if(WorkersCount > 0)
    {
	auto IndexMinUsed = MinBusyWithoutLock();
	Res = Workers[IndexMinUsed]->AddConnSync(Connection);
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
#undef max
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
    WorkerListLocker.LockRead();
    if(Index < WorkersCount)
	r = Workers[Index];
    WorkerListLocker.UnlockRead();
    return r;
}

size_t LqWrkBoss::MinBusy(size_t* MinCount)
{
    WorkerListLocker.LockRead();
    auto r = MinBusyWithoutLock(MinCount);
    WorkerListLocker.UnlockRead();
    return r;
}

void LqWrkBoss::StartAllWorkersSync()
{
    WorkerListLocker.LockRead();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
	Workers[i]->StartSync();
    WorkerListLocker.UnlockRead();
}

void LqWrkBoss::StartAllWorkersAsync()
{
    WorkerListLocker.LockRead();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
	Workers[i]->StartAsync();
    WorkerListLocker.UnlockRead();
}

bool LqWrkBoss::KickWorker(ullong IdWorker)
{
    WorkerListLocker.LockWrite();
    /*Lock operation remove from array*/
    for(size_t i = 0, m = WorkersCount; i < m; i++)
    {
	if(Workers[i]->Id == IdWorker)
	{
	    if(WorkersCount > 1)
	    {
		LqListConn Connections;
		Workers[i]->TakeAllConn(Connections);
		Workers[(i == 0) ? 1 : 0]->AddConnListAsync(Connections);
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
    WorkerListLocker.LockWrite();
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
		LqListConn Connections;
		Workers[i]->TakeAllConn(Connections);
		Workers[0]->AddConnListAsync(Connections);
	    }
	    Workers[i].~LqSharedPtr();
	}
	WorkersCount -= Count;
	Workers = (LqWorkerPtr*)realloc(Workers, sizeof(LqWorkerPtr) * WorkersCount);
    }
    WorkerListLocker.UnlockWrite();
}

bool LqWrkBoss::CloseAllConnAsync()
{
    bool r = true;
    WorkerListLocker.LockRead();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
	if(!Workers[i]->CloseAllConnAsync())
	{
	    r = false;
	    break;
	}
    WorkerListLocker.UnlockRead();
    return r;
}

void LqWrkBoss::CloseAllConnSync()
{
    WorkerListLocker.LockRead();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
	Workers[i]->CloseAllConnSync();
    WorkerListLocker.UnlockRead();
}

bool LqWrkBoss::CloseConnByIpAsync(const sockaddr* Addr)
{
    bool r = true;
    WorkerListLocker.LockRead();
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
    WorkerListLocker.LockRead();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
	Workers[i]->CloseConnByIpSync(Addr);
    WorkerListLocker.UnlockRead();
}

void LqWrkBoss::EnumConn(void * UserData, void(*Proc)(void *UserData, LqConn *Conn))
{
    WorkerListLocker.LockRead();
    for(size_t i = 0; i < WorkersCount; i++)
	Workers[i]->EnumConn(UserData, Proc);
    WorkerListLocker.UnlockRead();
}

bool LqWrkBoss::UnlockConnection(LqConn * Conn)
{
    WorkerListLocker.LockRead();
    for(size_t i = 0; i < WorkersCount; i++)
	Workers[i]->UnlockConnAsync(Conn);
    WorkerListLocker.UnlockRead();
    return true;
}

size_t LqWrkBoss::KickAllWorkers()
{
    WorkerListLocker.LockWrite();
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
    r += ((Sock.SockDscr != -1) ? "1" : "0");
    r += "\n";
    r += "Count workers: " + LqToString(WorkersCount) + "\n";
    return r;
}

