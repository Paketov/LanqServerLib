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
#include <string>
#include <string.h>
#include <vector>
#include "LqWrkBoss.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#undef max

static LqWrkBoss Boss;

LqWrkBoss::LqWrkBoss(): MinCount(0) { }

LqWrkBoss::LqWrkBoss(size_t CountWorkers) : MinCount(0) { AddWorkers(CountWorkers); }

LqWrkBoss::~LqWrkBoss() { }

int LqWrkBoss::AddWorkers(size_t Count, bool IsStart) {
    if(Count <= 0)
        return 0;
    int Res = 0;
    for(size_t i = 0; i < Count; i++) {
        auto NewWorker = LqWrk::New(IsStart);
        if(NewWorker == nullptr) {
            LQ_LOG_ERR("LqWrkBoss::AddWorkers() not alloc new worker\n");
            continue;
        }
        Wrks.push_back(NewWorker);
        Res++;
    }
    return Res;
}

bool LqWrkBoss::AddWorker(const LqWrkPtr& Wrk) { return Wrks.push_back(Wrk); }

bool LqWrkBoss::AddEvntAsync(LqEvntHdr* Evnt) {
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
    return LocalWrks[IndexMinUsed]->AddEvntAsync(Evnt);
}

bool LqWrkBoss::AddEvntSync(LqEvntHdr* Evnt) {
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
    return LocalWrks[IndexMinUsed]->AddEvntSync(Evnt);
}

size_t LqWrkBoss::CountWorkers() const { return Wrks.size(); }

size_t LqWrkBoss::CountEvnts() const {
    size_t Ret = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Ret += (*i)->CountEvnts();
    return Ret;
}

size_t LqWrkBoss::MinBusy(const WrkArray::interator& AllWrks, size_t* MinCount) {
    size_t Min = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = AllWrks.size(); i < m; i++) {
        size_t l = AllWrks[i]->GetAssessmentBusy();
        if(l < Min)
            Min = l, Index = i;
    }
    *MinCount = Min;
    return Index;
}

size_t LqWrkBoss::MaxBusy(const WrkArray::interator& AllWrks, size_t* MaxCount) {
    size_t Max = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = AllWrks.size(); i < m; i++) {
        size_t l = AllWrks[i]->GetAssessmentBusy();
        if(l > Max)
            Max = l, Index = i;
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
        Res += ((*i)->StartSync() ? 1 : 0);
    return Res;
}

size_t LqWrkBoss::StartAllWorkersAsync() const {
    size_t Ret = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Ret += ((*i)->StartAsync() ? 1 : 0);
    return Ret;
}

size_t LqWrkBoss::TransferAllEvntFromWrkList(WrkArray& SourceWrks) {
    struct AsyncData {
        int Fd;
        LqWrkBoss* This;
        LqWrk* Wrk;
        size_t Res;
        static void Handler(void* Data) {
            auto v = (AsyncData*)Data;
            v->Res = v->This->TransferAllEvnt(v->Wrk);
            LqFileEventSet(v->Fd);
        }
    };
    size_t Res = 0;
    size_t CountTryng = 0;
    AsyncData Context;
    Context.Fd = LqFileEventCreate(LQ_O_NOINHERIT);
    Context.This = this;
    for(auto& i : SourceWrks) {
        if(i->IsThisThread() || i->IsThreadEnd()) {
            TransferAllEvnt(i.Get());
        } else {
            Context.Wrk = i.Get();
            Context.Res = 0;
            i->AsyncCall(AsyncData::Handler, &Context);
            CountTryng = 0;
lblWaitAgain:
            if(LqFilePollCheckSingle(Context.Fd, LQ_POLLIN, 700) == 0) {
                if(((++CountTryng) >= 3) || i->IsThreadEnd()) {
                    i->CancelAsyncCall(AsyncData::Handler, &Context, false);
                    Res += TransferAllEvnt(i.Get());
                } else {
                    goto lblWaitAgain;
                }
            } else {
                Res += Context.Res;
            }
            LqFileEventReset(Context.Fd);
        }
    }
    LqFileClose(Context.Fd);
    return Res;
}

bool LqWrkBoss::KickWorker(ullong IdWorker, bool IsTransferAllEvnt) {
    /*Lock operation remove from array*/
    if(!IsTransferAllEvnt)
        return Wrks.remove_by_compare_fn([&](LqWrkPtr& Wrk) { return Wrk->GetId() == IdWorker; });
    WrkArray DelArr;
    auto Res = Wrks.remove_by_compare_fn([&](LqWrkPtr& Wrk) {
        if(Wrk->GetId() != IdWorker)
            return false;
        DelArr.push_back(Wrk);
        return true;
    }); 
    TransferAllEvntFromWrkList(DelArr);
    return Res;
}

size_t LqWrkBoss::TransferAllEvnt(LqWrkBoss & Source) {
    return TransferAllEvntFromWrkList(Source.Wrks);
}

size_t LqWrkBoss::KickWorkers(uintptr_t Count, bool IsTransferAllEvnt) {
    if(!IsTransferAllEvnt)
        return Wrks.unappend(Count, MinCount);

    WrkArray DelWrks;
    auto Res = Wrks.unappend(Count, MinCount, DelWrks);
    TransferAllEvntFromWrkList(DelWrks);
    return Res;
}

bool LqWrkBoss::CloseAllEvntAsync() const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseAllEvntAsync();
    return Res;
}

size_t LqWrkBoss::CloseAllEvntSync() const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseAllEvntSync();
    return Res;
}

size_t LqWrkBoss::CloseConnByTimeoutSync(LqTimeMillisec LiveTime) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->RemoveConnOnTimeOutSync(LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByTimeoutAsync(LqTimeMillisec LiveTime) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->RemoveConnOnTimeOutAsync(LiveTime);
    return Res;
}

size_t LqWrkBoss::CloseConnByTimeoutSync(const LqProto * Proto, LqTimeMillisec LiveTime) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->RemoveConnOnTimeOutSync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByTimeoutAsync(const LqProto * Proto, LqTimeMillisec LiveTime) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->RemoveConnOnTimeOutAsync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByIpAsync(const sockaddr* Addr) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseConnByIpAsync(Addr);
    return Res;
}

size_t LqWrkBoss::CloseConnByIpSync(const sockaddr* Addr) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseConnByIpSync(Addr);
    return Res;
}

bool LqWrkBoss::CloseConnByProtoAsync(const LqProto* Proto) const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseConnByProtoAsync(Proto);
    return Res;
}

size_t LqWrkBoss::CloseConnByProtoSync(const LqProto* Proto) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseConnByProtoSync(Proto);
    return Res;
}

size_t LqWrkBoss::EnumCloseRmEvnt(unsigned(*Proc)(void *UserData, LqEvntHdr* EvntHdr), void * UserData) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->EnumCloseRmEvnt(Proc, UserData);
    return Res;
}

size_t LqWrkBoss::EnumCloseRmEvntByProto(unsigned(*Proc)(void *UserData, LqEvntHdr *EvntHdr), const LqProto* Proto, void * UserData) const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->EnumCloseRmEvntByProto(Proc, Proto, UserData);
    return Res;
}

bool LqWrkBoss::RemoveEvnt(LqEvntHdr* EvntHdr) const {
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end() && !Res; i++)
        Res |= (*i)->RemoveEvnt(EvntHdr);
    return Res;
}

bool LqWrkBoss::CloseEvnt(LqEvntHdr* EvntHdr) const {
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end() && !Res; i++)
        Res |= (*i)->CloseEvnt(EvntHdr);
    return Res;
}

bool LqWrkBoss::UpdateAllEvntFlagAsync() const {
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->UpdateAllEvntFlagAsync();
    return Res;
}

size_t LqWrkBoss::UpdateAllEvntFlagSync() const {
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->UpdateAllEvntFlagSync();
    return Res;
}

bool LqWrkBoss::AsyncCall(void(*AsyncProc)(void* Data), void* UserData) {
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

size_t LqWrkBoss::CancelAsyncCall(void(*AsyncProc)(void* Data), void* UserData, bool IsAll) {
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

/*
* C - shell for global worker boss
*/

LqWrkBoss* LqWrkBoss::GetGlobal() { return &Boss; }

LQ_EXTERN_C void *LQ_CALL LqWrkBossGet() { return &Boss; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossKickWrks(size_t Count) { return Boss.KickWorkers(Count); }

LQ_EXTERN_C int LQ_CALL LqWrkBossKickWrk(size_t Index) { return Boss.KickWorker(Index) ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossAddWrks(size_t Count, bool IsStart) { return Boss.AddWorkers(Count, IsStart); }

LQ_EXTERN_C int LQ_CALL LqWrkBossAddWrk(void * Wrk) { LqWrkPtr Ptr = (LqWrk*)Wrk; return Boss.AddWorker(Ptr) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossKickAllWrk() { return Boss.KickAllWorkers(); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseAllEvntAsync() { return Boss.CloseAllEvntAsync() ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseAllEvntSync() { return Boss.CloseAllEvntSync(); }

LQ_EXTERN_C int LQ_CALL LqWrkBossAddEvntAsync(LqEvntHdr * Conn) { return Boss.AddEvntAsync(Conn) ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossAddEvntSync(LqEvntHdr * Conn) { return Boss.AddEvntSync(Conn) ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossUpdateAllEvntFlagAsync() { return Boss.UpdateAllEvntFlagAsync() ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossUpdateAllEvntFlagSync() { return Boss.UpdateAllEvntFlagSync() ? 0 : -1; }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByIpAsync(const struct sockaddr* Addr) { return Boss.CloseConnByIpAsync(Addr) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByIpSync(const struct sockaddr* Addr) { return Boss.CloseConnByIpSync(Addr); }

LQ_EXTERN_C bool LQ_CALL LqWrkBossRemoveEvnt(LqEvntHdr * Conn) { return Boss.RemoveEvnt(Conn); }

LQ_EXTERN_C bool LQ_CALL LqWrkBossCloseEvnt(LqEvntHdr * Conn) { return Boss.CloseEvnt(Conn); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByProtoAsync(const LqProto* Addr) { return Boss.CloseConnByProtoAsync(Addr); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByProtoSync(const LqProto* Addr) { return Boss.CloseConnByProtoSync(Addr); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByTimeoutAsync(LqTimeMillisec TimeLive) { return Boss.CloseConnByTimeoutAsync(TimeLive) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByTimeoutSync(LqTimeMillisec TimeLive) { return Boss.CloseConnByTimeoutSync(TimeLive); }

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByProtoTimeoutAsync(const LqProto * Proto, LqTimeMillisec TimeLive) { return Boss.CloseConnByTimeoutAsync(Proto, TimeLive) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByProtoTimeoutSync(const LqProto * Proto, LqTimeMillisec TimeLive) { return Boss.CloseConnByTimeoutSync(Proto, TimeLive) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossEnumCloseRmEvntByProto(unsigned(*Proc)(void *UserData, LqEvntHdr *Conn), const LqProto* Proto, void* UserData) 
{ 
    return Boss.EnumCloseRmEvntByProto(Proc, Proto, UserData);
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossEnumCloseRmEvnt(unsigned(*Proc)(void *UserData, LqEvntHdr *Conn), void * UserData) { return Boss.EnumCloseRmEvnt(Proc, UserData); }

LQ_EXTERN_C int LQ_CALL LqWrkBossAsyncCall(void(*AsyncProc)(void* Data), void* UserData) { return Boss.AsyncCall(AsyncProc, UserData) ? 0 : -1; }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCancelAsyncCall(void(*AsyncProc)(void* Data), void* UserData, bool IsAll) { return Boss.CancelAsyncCall(AsyncProc, UserData, IsAll); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount) { return Boss.SetWrkMinCount(NewCount); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossStartAllWrkSync() { return Boss.StartAllWorkersSync(); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossStartAllWrkAsync() { return Boss.StartAllWorkersAsync(); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCountWrk() { return Boss.CountWorkers(); }
