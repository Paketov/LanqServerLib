/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkBoss (LanQ WoRKer BOSS) - Accept event object and send him to workers.
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
#include <stdlib.h> 
#include <string.h>
#include <string>
#include <vector>
#include "LqWrkBoss.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#undef max

static LqWrkBoss* Boss = LqWrkBoss::New();

LqWrkBoss::LqWrkBoss(): MinCount(1), NeedDelete(false) { }

LqWrkBoss::LqWrkBoss(size_t CountWorkers) : MinCount(1) { AddWorkers(CountWorkers); }

LqWrkBoss * LqWrkBoss::New() {
    return LqFastAlloc::New<LqWrkBoss>();
}

LqWrkBoss * LqWrkBoss::New(size_t CountWorkers) {
    return LqFastAlloc::New<LqWrkBoss>(CountWorkers);
}

void LqWrkBoss::Delete(LqWrkBoss * Target) {
    const LqWrkPtr* Arr;
    intptr_t ArrCount;
    bool IsDelete = false;
    Target->Wrks.begin_locket_enum(&Arr, &ArrCount);
    Target->NeedDelete = true;
    if(ArrCount <= 0) {
        IsDelete = true;
    } else {
        for(intptr_t i = 0; i < ArrCount; i++)
            Arr[i]->TransferClientsAndRemoveFromBossAsync(Target, false);
    }
    Target->Wrks.end_locket_enum(-((intptr_t)1));
    if(IsDelete)
        LqFastAlloc::Delete(Target);
}

LqWrkBoss::~LqWrkBoss(){ }

int LqWrkBoss::AddWorkers(size_t Count, bool IsStart) {
    if(Count <= 0)
        return 0;
    int Res = 0;
    for(size_t i = 0; i < Count; i++) {
        auto NewWorker = LqWrk::New(IsStart);
        if(NewWorker == nullptr) {
            LqLogErr("LqWrkBoss::AddWorkers() not alloc new worker\n");
            continue;
        }
        Wrks.push_back(NewWorker);
        Res++;
    }
    return Res;
}

bool LqWrkBoss::AddWorker(const LqWrkPtr& Wrk) { return Wrks.push_back(Wrk); }

bool LqWrkBoss::AddClientAsync(LqClientHdr* Evnt) {
    bool Res = true;
    auto LocalWrks = Wrks.begin();
    if(LocalWrks.size() <= 0) {
        if(!AddWorkers(1)) {
            return false;
        } else {
            LocalWrks = Wrks.begin();
            if(LocalWrks.size() <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks[IndexMinUsed]->AddClientAsync(Evnt);
}

bool LqWrkBoss::AddClientSync(LqClientHdr* Evnt) {
    bool Res = true;
    auto LocalWrks = Wrks.begin();

    if(LocalWrks.size() <= 0) {
        if(!AddWorkers(1)) {
            return false;
        } else {
            LocalWrks = Wrks.begin();
            if(LocalWrks.size() <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks[IndexMinUsed]->AddClientSync(Evnt);
}

size_t LqWrkBoss::CountWorkers() const { return Wrks.size(); }

size_t LqWrkBoss::CountClients() const {
    size_t Ret = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Ret += (*i)->CountClients();
    return Ret;
}

size_t LqWrkBoss::MinBusy(const WrkArray::interator& AllWrks, size_t* MinCount) {
    size_t Min = std::numeric_limits<size_t>::max(), Index = 0;
    bool IsAllSame = true;
    size_t i = 0, m = AllWrks.size();
    for(; i < m; i++) {
        size_t l = AllWrks[i]->GetAssessmentBusy();
        if(i > 0)
            IsAllSame &= (Min == l);
        if(l < Min)
            Min = l, Index = i;
    }
    if(IsAllSame) {
        Index = rand() % m;
        Min = AllWrks[Index]->GetAssessmentBusy();
    }
    *MinCount = Min;
    return Index;
}

size_t LqWrkBoss::MaxBusy(const WrkArray::interator& AllWrks, size_t* MaxCount) {
    size_t Max = std::numeric_limits<size_t>::max(), Index = 0;
    bool IsAllSame = true;
    size_t i = 0, m = AllWrks.size();
    for(; i < m; i++) {
        size_t l = AllWrks[i]->GetAssessmentBusy();
        if(i > 0)
            IsAllSame &= (Max == l);
        if(l > Max)
            Max = l, Index = i;
    }
    if(IsAllSame) {
        Index = rand() % m;
        Max = AllWrks[Index]->GetAssessmentBusy();
    }
    *MaxCount = Max;
    return Index;
}

size_t LqWrkBoss::MinBusy(size_t* MinCount) {
    const auto LocalWrks = Wrks.begin();
    return MinBusy(LocalWrks, MinCount);
}

size_t LqWrkBoss::StartAllWorkersSync() const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += ((*i)->StartThreadSync() ? 1 : 0);
    return Res;
}

size_t LqWrkBoss::StartAllWorkersAsync() const {
    size_t Ret = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Ret += ((*i)->StartThreadAsync() ? 1 : 0);
    return Ret;
}

bool LqWrkBoss::KickWorker(ullong IdWorker, bool IsTransferAllEvnt) {
    /*Lock operation remove from array*/
    const LqWrkPtr* Arr;
    intptr_t ArrCount;
    bool Res = false;
    Wrks.begin_locket_enum(&Arr, &ArrCount);
    for(intptr_t i = 0; i < ArrCount; i++) {
        if(Arr[i]->GetId() == IdWorker) {
            Arr[i]->TransferClientsAndRemoveFromBossAsync(this, IsTransferAllEvnt);
            Res = true;
            break;
        }
    }
    Wrks.end_locket_enum(-((intptr_t)1));
    return Res;
}

size_t LqWrkBoss::KickWorkers(uintptr_t Count, bool IsTransferAllEvnt) {
    const LqWrkPtr* Arr;
    intptr_t ArrCount;
    size_t Res = 0;
    Wrks.begin_locket_enum(&Arr, &ArrCount);
    for(intptr_t i = 0; i < ArrCount; i++) {
        if((i >= Count) || ((i + MinCount) >= ArrCount))
            break;
        Arr[i]->TransferClientsAndRemoveFromBossAsync(this, IsTransferAllEvnt);
        Res++;
    }
    Wrks.end_locket_enum(-((intptr_t)1));
    return Res;
}

bool LqWrkBoss::CloseAllClientsAsync() const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseAllClientsAsync();
    return Res;
}

size_t LqWrkBoss::CloseAllClientsSync() const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseAllClientsSync();
    return Res;
}

size_t LqWrkBoss::CloseClientsByTimeoutSync(LqTimeMillisec LiveTime) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->RemoveClientsOnTimeOutSync(LiveTime);
    return Res;
}

bool LqWrkBoss::CloseClientsByTimeoutAsync(LqTimeMillisec LiveTime) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->RemoveClientsOnTimeOutAsync(LiveTime);
    return Res;
}

size_t LqWrkBoss::CloseClientsByTimeoutSync(const LqProto * Proto, LqTimeMillisec LiveTime) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->RemoveClientsOnTimeOutSync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseClientsByTimeoutAsync(const LqProto * Proto, LqTimeMillisec LiveTime) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->RemoveClientsOnTimeOutAsync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseClientsByIpAsync(const sockaddr* Addr) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseClientsByIpAsync(Addr);
    return Res;
}

size_t LqWrkBoss::CloseClientsByIpSync(const sockaddr* Addr) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseClientsByIpSync(Addr);
    return Res;
}

bool LqWrkBoss::CloseClientsByProtoAsync(const LqProto* Proto) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseClientsByProtoAsync(Proto);
    return Res;
}

size_t LqWrkBoss::CloseClientsByProtoSync(const LqProto* Proto) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseClientsByProtoSync(Proto);
    return Res;
}

size_t LqWrkBoss::EnumClients(int(LQ_CALL*Proc)(void *, LqClientHdr*), void * UserData) const {
    size_t Res = 0;
    bool IsIterrupted = false;
    for(auto i = Wrks.begin(); !i.is_end() && !IsIterrupted; i++)
        Res += (*i)->EnumClients(Proc, UserData, &IsIterrupted);
    return Res;
}

size_t LqWrkBoss::EnumClientsByProto(int(LQ_CALL*Proc)(void *, LqClientHdr*), const LqProto* Proto, void * UserData) const {
    size_t Res = 0;
    bool IsIterrupted = false;
    for(auto i = Wrks.begin(); !i.is_end() && !IsIterrupted; i++)
        Res += (*i)->EnumClientsByProto(Proc, Proto, UserData, &IsIterrupted);
    return Res;
}

size_t LqWrkBoss::EnumClients11(std::function<int(LqClientHdr*)> EventAct) const {
    size_t Res = 0;
    bool IsIterrupted = false;
    for(auto i = Wrks.begin(); !i.is_end() && !IsIterrupted; i++)
        Res += (*i)->EnumClients11(EventAct, &IsIterrupted);
    return Res;
}

size_t LqWrkBoss::EnumClientsByProto11(std::function<int(LqClientHdr*)> EventAct, const LqProto* Proto) const {
    size_t Res = 0;
    bool IsIterrupted = false;
    for(auto i = Wrks.begin(); !i.is_end() && !IsIterrupted; i++)
        Res += (*i)->EnumClientsByProto11(EventAct, Proto, &IsIterrupted);
    return Res;
}

bool LqWrkBoss::EnumClientsAsync11(std::function<int(LqWrkPtr&, LqClientHdr*)> EventAct) const {
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res |= (*i)->EnumClientsAsync11(EventAct);
    return Res;
}

bool LqWrkBoss::EnumClientsAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
    void* UserData,
    size_t UserDataSize
) const {
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res |= (*i)->EnumClientsAsync(EventAct, UserData, UserDataSize);
    return Res;
}

bool LqWrkBoss::RemoveClient(LqClientHdr* EvntHdr) const {
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end() && !Res; i++)
        Res |= (*i)->RemoveClient(EvntHdr);
    return Res;
}

bool LqWrkBoss::CloseClient(LqClientHdr* EvntHdr) const {
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end() && !Res; i++)
        Res |= (*i)->CloseClient(EvntHdr);
    return Res;
}

bool LqWrkBoss::UpdateAllClientsFlagAsync() const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->UpdateAllClientFlagAsync();
    return Res;
}

size_t LqWrkBoss::UpdateAllClientsFlagSync() const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->UpdateAllClientFlagSync();
    return Res;
}

bool LqWrkBoss::AsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData) {
    bool Res = true;
    auto LocalWrks = Wrks.begin();
    if(LocalWrks.size() <= 0) {
        if(!AddWorkers(1)) {
            return false;
        } else {
            LocalWrks = Wrks.begin();
            if(LocalWrks.size() <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks[IndexMinUsed]->AsyncCall(AsyncProc, UserData);
}

bool LqWrkBoss::AsyncCall11(std::function<void()> Proc) {
    bool Res = true;
    auto LocalWrks = Wrks.begin();
    if(LocalWrks.size() <= 0) {
        if(!AddWorkers(1)) {
            return false;
        } else {
            LocalWrks = Wrks.begin();
            if(LocalWrks.size() <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks[IndexMinUsed]->AsyncCall11(Proc);
}

size_t LqWrkBoss::CancelAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData, bool IsAll) {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end() && (IsAll || (Res <= 0)); i++)
        Res += (*i)->CancelAsyncCall(AsyncProc, UserData, IsAll);
    return Res;
}

size_t LqWrkBoss::KickAllWorkers() { return KickWorkers(0xfffffff); }

size_t LqWrkBoss::SetWrkMinCount(size_t NewVal) { return MinCount = NewVal; }

LqString LqWrkBoss::DebugInfo() {
    LqString DbgStr;
    auto i = Wrks.begin();
    DbgStr += "=========================\n";
    DbgStr += " Count workers " + std::to_string(i.size()) + "\n";

    for(; !i.is_end(); i++) {
        DbgStr.append("=========================\n");
        DbgStr.append((*i)->DebugInfo());
    }
    return DbgStr;
}

LqString LqWrkBoss::AllDebugInfo() {
    LqString DbgStr;
    auto i = Wrks.begin();
    DbgStr += "=========================\n";
    DbgStr += " Count workers " + std::to_string(i.size()) + "\n";

    for(; !i.is_end(); i++) {
        DbgStr.append("=========================\n");
        DbgStr.append((*i)->AllDebugInfo());
    }
    return DbgStr;
}

LqWrkBoss* LqWrkBoss::GetGlobal() { return Boss; }

LQ_EXTERN_C int LQ_CALL LqClientSetFlags(void* EvntOrConn, LqEvntFlag NewFlag, LqTimeMillisec WaitTime) {
    LqClientHdr* EvntHdr = (LqClientHdr*)EvntOrConn;
    LqEvntFlag ExpectedEvntFlag, NewEvntFlag;
    LqTimeMillisec Start;
    volatile LqEvntFlag* Flag;
    bool IsSync;

    if((ExpectedEvntFlag = LqClientGetFlags(EvntHdr)) & LQEVNT_FLAG_END)
        return 0;
    do {
        NewEvntFlag = (ExpectedEvntFlag & ~(LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_ACCEPT | LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) | NewFlag;
        if(IsSync = !(ExpectedEvntFlag & _LQEVNT_FLAG_NOW_EXEC))
            NewEvntFlag |= _LQEVNT_FLAG_SYNC;
    } while(!LqAtmCmpXchg(EvntHdr->Flag, ExpectedEvntFlag, NewEvntFlag));
    if(IsSync) {
        LqWrkPtr Wrk = LqWrk::ByEvntHdr((LqClientHdr*)EvntOrConn);
        if(!LqWrk::IsNull(Wrk)) {
            Wrk->UpdateAllClientFlagAsync();
            if(WaitTime <= 0)
                return 1;
            Start = LqTimeGetLocMillisec();
            for(Flag = &EvntHdr->Flag; *Flag & _LQEVNT_FLAG_SYNC;) {
                if((LqTimeGetLocMillisec() - Start) > WaitTime)
                    return 1;
                LqThreadYield();
            }
        }
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqClientSetClose(void* EvntOrConn) {
    LqClientHdr* EvntHdr = (LqClientHdr*)EvntOrConn;
    LqEvntFlag ExpectedEvntFlag, NewEvntFlag;
    bool IsSync;
    ExpectedEvntFlag = LqClientGetFlags(EvntHdr);
    do {
        NewEvntFlag = ExpectedEvntFlag | LQEVNT_FLAG_END;
        if(IsSync = !(ExpectedEvntFlag & _LQEVNT_FLAG_NOW_EXEC))
            NewEvntFlag |= _LQEVNT_FLAG_SYNC;
    } while(!LqAtmCmpXchg(EvntHdr->Flag, ExpectedEvntFlag, NewEvntFlag));
    if(IsSync) {
        LqWrkPtr Wrk = LqWrk::ByEvntHdr((LqClientHdr*)EvntOrConn);
        if(!LqWrk::IsNull(Wrk))
            Wrk->UpdateAllClientFlagAsync();
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqClientSetClose2(void* EvntOrConn, LqTimeMillisec WaitTime) {
    LqClientHdr* EvntHdr = (LqClientHdr*)EvntOrConn;
    LqEvntFlag ExpectedEvntFlag, NewEvntFlag;
    LqTimeMillisec StartMillisec;
    volatile LqEvntFlag* Flag;
    bool IsSync;

    ExpectedEvntFlag = LqClientGetFlags(EvntHdr);
    do {
        NewEvntFlag = ExpectedEvntFlag | LQEVNT_FLAG_END;
        if(IsSync = !(ExpectedEvntFlag & _LQEVNT_FLAG_NOW_EXEC))
            NewEvntFlag |= _LQEVNT_FLAG_SYNC;
    } while(!LqAtmCmpXchg(EvntHdr->Flag, ExpectedEvntFlag, NewEvntFlag));
    if(IsSync) {
        LqWrkPtr Wrk = LqWrk::ByEvntHdr((LqClientHdr*)EvntOrConn);
        if(!LqWrk::IsNull(Wrk)) {
            Wrk->UpdateAllClientFlagAsync();
            if(WaitTime <= 0)
                return 1;
            StartMillisec = LqTimeGetLocMillisec();
            for(Flag = &EvntHdr->Flag; *Flag & _LQEVNT_FLAG_SYNC;) {
                if((LqTimeGetLocMillisec() - StartMillisec) > WaitTime)
                    return 1;
                LqThreadYield();
            }
        }
    }
    return 0;
}

LQ_EXTERN_C bool LQ_CALL LqClientSetClose3(void * EvntOrConn) {
    LqClientHdr* EvntHdr = (LqClientHdr*)EvntOrConn;
    bool Res;
    LqEvntFlag ExpectedEvntFlag, NewEvntFlag;
    ExpectedEvntFlag = LqClientGetFlags(EvntHdr);
    do {
        NewEvntFlag = ExpectedEvntFlag | LQEVNT_FLAG_END;
    } while(!LqAtmCmpXchg(EvntHdr->Flag, ExpectedEvntFlag, NewEvntFlag));
    do {
        LqWrkPtr Wrk = LqWrk::ByEvntHdr((LqClientHdr*)EvntOrConn);
        if(LqWrk::IsNull(Wrk))
            return false;
        Res = Wrk->CloseClient(EvntHdr);
    } while(!Res);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqClientSetRemove3(void* EvntOrConn) {
    LqClientHdr* EvntHdr = (LqClientHdr*)EvntOrConn;
    bool Res;
    do {
        LqWrkPtr Wrk = LqWrk::ByEvntHdr((LqClientHdr*)EvntOrConn);
        if(LqWrk::IsNull(Wrk))
            return false;
        Res = Wrk->RemoveClient(EvntHdr);
    } while(!Res);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqClientAdd(void* EvntOrConn, void* WrkBoss) {
    if(WrkBoss == NULL)
        WrkBoss = LqWrkBossGet();
    return ((LqWrkBoss*)WrkBoss)->AddClientAsync((LqClientHdr*)EvntOrConn);
}

LQ_EXTERN_C int LQ_CALL LqClientAdd2(void* EvntOrConn, void* WrkBoss) {
    if(WrkBoss == NULL)
        WrkBoss = LqWrkBossGet();
    return ((LqWrkBoss*)WrkBoss)->AddClientSync((LqClientHdr*)EvntOrConn);
}


LQ_EXTERN_C void LQ_CALL LqConnInit(void* Conn, int NewFd, void* NewProto, LqEvntFlag NewFlags) {
    LqAtmLkInit(((LqConn*)(Conn))->Lk);
    ((LqConn*)(Conn))->Fd = NewFd;
    ((LqConn*)(Conn))->Proto = (LqProto*)NewProto;
    ((LqConn*)(Conn))->WrkOwner = NULL;
    ((LqConn*)(Conn))->Flag = _LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_CONN;
    LqClientSetFlags(((LqConn*)(Conn)), NewFlags, 0);
    ((LqConn*)(Conn))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;
}

static void LQ_CALL __LqEvntFdDfltHandler(LqEvntFd * Instance, LqEvntFlag Flags) {}

static void LQ_CALL __LqEvntFdDfltCloseHandler(LqEvntFd*) {}

LQ_EXTERN_C void LQ_CALL LqEvntFdInit(void* Evnt, int NewFd, LqEvntFlag NewFlags, void(LQ_CALL *Handler)(LqEvntFd*, LqEvntFlag), void(LQ_CALL *CloseHandler)(LqEvntFd*)) {
    LqAtmLkInit(((LqEvntFd*)(Evnt))->Lk);
    ((LqEvntFd*)(Evnt))->Fd = (NewFd);
    ((LqEvntFd*)(Evnt))->Handler = (Handler) ? Handler : __LqEvntFdDfltHandler;
    ((LqEvntFd*)(Evnt))->CloseHandler = (CloseHandler) ? CloseHandler : __LqEvntFdDfltCloseHandler;
    ((LqEvntFd*)(Evnt))->WrkOwner = NULL;
    ((LqEvntFd*)(Evnt))->Flag = _LQEVNT_FLAG_NOW_EXEC;
    LqClientSetFlags((LqEvntFd*)(Evnt), NewFlags, 0);
    ((LqEvntFd*)(Evnt))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;
}

LQ_EXTERN_C void LQ_CALL LqClientCallCloseHandler(void * EvntHdr) {
    LqAtmIntrlkOr(((LqClientHdr*)(EvntHdr))->Flag, _LQEVNT_FLAG_NOW_EXEC);
    if(LqClientGetFlags(EvntHdr) & _LQEVNT_FLAG_CONN)
        ((LqConn*)(EvntHdr))->Proto->CloseHandler((LqConn*)(EvntHdr));
    else
        ((LqEvntFd*)(EvntHdr))->CloseHandler((LqEvntFd*)(EvntHdr));
}

LQ_EXTERN_C void LQ_CALL LqClientSetOnlyOneBoss(void * EvntHdr, bool State) {
    if(State)
        LqAtmIntrlkOr(((LqClientHdr*)EvntHdr)->Flag, _LQEVNT_FLAG_ONLY_ONE_BOSS);
    else
        LqAtmIntrlkAnd(((LqClientHdr*)EvntHdr)->Flag, ~_LQEVNT_FLAG_ONLY_ONE_BOSS);
}

/*
* C - shell for global worker boss
*/



LQ_EXTERN_C void *LQ_CALL LqWrkBossGet() { return Boss; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossKickWrks(size_t Count) { return Boss->KickWorkers(Count); }

LQ_EXTERN_C int LQ_CALL LqWrkBossKickWrk(size_t Index) { return Boss->KickWorker(Index) ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossAddWrks(size_t Count, bool IsStart) { return Boss->AddWorkers(Count, IsStart); }

LQ_EXTERN_C int LQ_CALL LqWrkBossAddWrk(void * Wrk) { LqWrkPtr Ptr = (LqWrk*)Wrk; return Boss->AddWorker(Ptr) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossKickAllWrk() { return Boss->KickAllWorkers(); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseAllClientsAsync() { return Boss->CloseAllClientsAsync() ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseAllClientsSync() { return Boss->CloseAllClientsSync(); }

LQ_EXTERN_C int LQ_CALL LqWrkBossUpdateAllClientsFlagAsync() { return Boss->UpdateAllClientsFlagAsync() ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossUpdateAllClientsFlagSync() { return Boss->UpdateAllClientsFlagSync() ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseClientsByIpAsync(const struct sockaddr* Addr) { return Boss->CloseClientsByIpAsync(Addr) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseClientsByIpSync(const struct sockaddr* Addr) { return Boss->CloseClientsByIpSync(Addr); }

LQ_EXTERN_C bool LQ_CALL LqWrkBossRemoveClients(LqClientHdr * Conn) { return Boss->RemoveClient(Conn); }

LQ_EXTERN_C bool LQ_CALL LqWrkBossCloseClients(LqClientHdr * Conn) { return Boss->CloseClient(Conn); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseClientsByProtoAsync(const LqProto* Addr) { return Boss->CloseClientsByProtoAsync(Addr); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseClientsByProtoSync(const LqProto* Addr) { return Boss->CloseClientsByProtoSync(Addr); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseClientsByTimeoutAsync(LqTimeMillisec TimeLive) { return Boss->CloseClientsByTimeoutAsync(TimeLive) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseClientsByTimeoutSync(LqTimeMillisec TimeLive) { return Boss->CloseClientsByTimeoutSync(TimeLive); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseClientsByProtoTimeoutAsync(const LqProto* Proto, LqTimeMillisec TimeLive) { return Boss->CloseClientsByTimeoutAsync(Proto, TimeLive) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseClientsByProtoTimeoutSync(const LqProto* Proto, LqTimeMillisec TimeLive) { return Boss->CloseClientsByTimeoutSync(Proto, TimeLive) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossEnumClientsCloseRmEvntByProto(int(LQ_CALL*Proc)(void*, LqClientHdr*), const LqProto* Proto, void* UserData) {
    return Boss->EnumClientsByProto(Proc, Proto, UserData);
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossEnumClients(int(LQ_CALL*Proc)(void*, LqClientHdr*), void * UserData) { return Boss->EnumClients(Proc, UserData); }

LQ_IMPORTEXPORT bool LQ_CALL LqWrkBossEnumClientsAndCallFinAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
    uintptr_t(LQ_CALL*FinFunc)(void*, size_t),
    void * UserData,
    size_t UserDataSize
) {
    return Boss->EnumClientsAndCallFinAsync(EventAct, FinFunc, UserData, UserDataSize);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData) { return Boss->AsyncCall(AsyncProc, UserData) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCancelAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData, bool IsAll) { return Boss->CancelAsyncCall(AsyncProc, UserData, IsAll); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount) { return Boss->SetWrkMinCount(NewCount); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossStartAllWrkSync() { return Boss->StartAllWorkersSync(); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossStartAllWrkAsync() { return Boss->StartAllWorkersAsync(); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCountWrk() { return Boss->CountWorkers(); }
