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

LqWrkBoss::LqWrkBoss(): MinCount(0){}

LqWrkBoss::LqWrkBoss(size_t CountWorkers) : MinCount(0) { AddWorkers(CountWorkers); }

LqWrkBoss::~LqWrkBoss() { }

size_t LqWrkBoss::TransferAllEvnt(LqWrk* Source) const
{
    size_t Res = 0;
    std::vector<LqEvntHdr*> RmHdrs;
    Source->Lock();
    const auto LocalWrks = Wrks.begin();
    lqevnt_enum_do(Source->EventChecker, i)
    {
        auto Hdr = LqEvntGetHdrByInterator(&Source->EventChecker, &i);

        intptr_t Min = std::numeric_limits<intptr_t>::max(), Index = -1;
        for(size_t i = 0, m = LocalWrks.size(); i < m; i++)
        {
            if(LocalWrks[i] == Source)
                continue;
            size_t l = LocalWrks[i]->GetAssessmentBusy();
            if(l < Min)
                Min = l, Index = i;
        }
        LqEvntRemoveByInterator(&Source->EventChecker, &i);
        if((Index != -1) && LocalWrks[Index]->AddEvntAsync(Hdr))
        {
            Res++;
        } else
        {
            RmHdrs.push_back(Hdr);
            LQ_ERR("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
        }
    }lqevnt_enum_while(Source->EventChecker);

    for(auto Command = Source->CommandQueue.SeparateBegin(); !Source->CommandQueue.SeparateIsEnd(Command);)
    {
        switch(Command.Type)
        {
            case LqWrk::LQWRK_CMD_ADD_CONN:
            {
                auto Hdr = Command.Val<LqEvntHdr*>();
                Command.Pop<LqEvntHdr*>();
                Source->CountConnectionsInQueue--;

                intptr_t Min = std::numeric_limits<intptr_t>::max(), Index;
                for(size_t i = 0, m = LocalWrks.size(); i < m; i++)
                {
                    if(LocalWrks[i] == Source)
                        continue;
                    size_t l = LocalWrks[i]->GetAssessmentBusy();
                    if(l < Min)
                        Min = l, Index = i;
                }
               
                if((Index != -1) && LocalWrks[Index]->AddEvntAsync(Hdr))
                {
                    Res++;
                } else
                {
                    RmHdrs.push_back(Hdr);
                    LQ_ERR("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
                }
            }
            break;
            default:
                /* Otherwise return current command in list*/
                Source->CommandQueue.SeparatePush(Command);
        }
    }
    Source->Unlock();
    for(auto i : RmHdrs)
        LqEvntHdrClose(i);
    return Res;
}

int LqWrkBoss::AddWorkers(size_t Count, bool IsStart)
{
    if(Count <= 0)
        return 0;
    int Res = 0;
    for(size_t i = 0; i < Count; i++)
    {
        auto NewWorker = LqWrk::New(IsStart);
        if(NewWorker == nullptr)
        {
            LQ_ERR("LqWrkBoss::AddWorkers() not alloc new worker\n");
            continue;
        }
        Wrks.push_back(NewWorker);
        Res++;
    }
    return Res;
}

bool LqWrkBoss::AddWorker(const LqWrkPtr& Wrk) { return Wrks.push_back(Wrk); }

bool LqWrkBoss::AddEvntAsync(LqEvntHdr* Evnt)
{
    bool Res = true;
    auto LocalWrks = Wrks.begin();
    if(LocalWrks.size() <= 0)
    {
        if(!AddWorkers(1))
        {
            return false;
        } else
        {
            LocalWrks = Wrks.begin();
            if(LocalWrks.size() <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks[IndexMinUsed]->AddEvntAsync(Evnt);
}

bool LqWrkBoss::AddEvntSync(LqEvntHdr* Evnt)
{
    bool Res = true;
    auto LocalWrks = Wrks.begin();

    if(LocalWrks.size() <= 0)
    {
        if(!AddWorkers(1))
        {
            return false;
        } else
        {
            LocalWrks = Wrks.begin();
            if(LocalWrks.size() <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks[IndexMinUsed]->AddEvntSync(Evnt);
}

size_t LqWrkBoss::CountWorkers() const { return Wrks.size(); }
size_t LqWrkBoss::CountEvnts() const 
{
    size_t Ret = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Ret += (*i)->CountEvnts();
    return Ret;
}

size_t LqWrkBoss::MinBusy(const WrkArray::interator& AllWrks, size_t* MinCount)
{
    size_t Min = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = AllWrks.size(); i < m; i++)
    {
        size_t l = AllWrks[i]->GetAssessmentBusy();
        if(l < Min)
            Min = l, Index = i;
    }
    *MinCount = Min;
    return Index;
}

size_t LqWrkBoss::MaxBusy(const WrkArray::interator& AllWrks, size_t* MaxCount)
{
    size_t Max = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = AllWrks.size(); i < m; i++)
    {
        size_t l = AllWrks[i]->GetAssessmentBusy();
        if(l > Max)
            Max = l, Index = i;
    }
    *MaxCount = Max;
    return Index;
}

size_t LqWrkBoss::MinBusy(size_t* MinCount)
{
    const auto LocalWrks = Wrks.begin();
    return MinBusy(LocalWrks, MinCount);
}

size_t LqWrkBoss::StartAllWorkersSync() const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += ((*i)->StartSync() ? 1 : 0);
    return Res;
}

size_t LqWrkBoss::StartAllWorkersAsync() const
{
    size_t Ret = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Ret += ((*i)->StartAsync() ? 1 : 0);
    return Ret;
}


bool LqWrkBoss::KickWorker(ullong IdWorker)
{
    /*Lock operation remove from array*/
    return Wrks.remove_by_compare_fn([&](LqWrkPtr& Wrk) { return Wrk->GetId() == IdWorker; });
}

size_t LqWrkBoss::KickWorkers(uintptr_t Count) { return Wrks.unappend(Count); }

bool LqWrkBoss::CloseAllEvntAsync() const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseAllEvntAsync();
    return Res;
}

size_t LqWrkBoss::CloseAllEvntSync() const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseAllEvntSync();
    return Res;
}

size_t LqWrkBoss::CloseConnByTimeoutSync(LqTimeMillisec LiveTime) const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->RemoveConnOnTimeOutSync(LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByTimeoutAsync(LqTimeMillisec LiveTime) const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->RemoveConnOnTimeOutAsync(LiveTime);
    return Res;
}

size_t LqWrkBoss::CloseConnByTimeoutSync(const LqProto * Proto, LqTimeMillisec LiveTime) const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->RemoveConnOnTimeOutSync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByTimeoutAsync(const LqProto * Proto, LqTimeMillisec LiveTime) const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->RemoveConnOnTimeOutAsync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByIpAsync(const sockaddr* Addr) const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseConnByIpAsync(Addr);
    return Res;
}

size_t LqWrkBoss::CloseConnByIpSync(const sockaddr* Addr) const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseConnByIpSync(Addr);
    return Res;
}

bool LqWrkBoss::CloseConnByProtoAsync(const LqProto* Proto) const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->CloseConnByProtoAsync(Proto);
    return Res;
}

size_t LqWrkBoss::CloseConnByProtoSync(const LqProto* Proto) const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->CloseConnByProtoSync(Proto);
    return Res;
}

size_t LqWrkBoss::EnumCloseRmEvnt(void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr* EvntHdr)) const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->EnumCloseRmEvnt(UserData, Proc);
    return Res;
}

size_t LqWrkBoss::EnumCloseRmEvntByProto(const LqProto* Proto, void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr *EvntHdr)) const
{
    size_t Res = 0;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res += (*i)->EnumCloseRmEvntByProto(Proto, UserData, Proc);
    return Res;
}

bool LqWrkBoss::RemoveEvnt(LqEvntHdr* EvntHdr) const
{
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end() && !Res; i++)
        Res |= (*i)->RemoveEvnt(EvntHdr);
    return Res;
}
bool LqWrkBoss::CloseEvnt(LqEvntHdr* EvntHdr) const
{
    bool Res = false;
    for(auto i = Wrks.begin(); !i.is_end() && !Res; i++)
        Res |= (*i)->CloseEvnt(EvntHdr);
    return Res;
}

bool LqWrkBoss::UpdateAllEvntFlagAsync() const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->UpdateAllEvntFlagAsync();
    return Res;
}

bool LqWrkBoss::UpdateAllEvntFlagSync() const
{
    bool Res = true;
    for(auto i = Wrks.begin(); !i.is_end(); i++)
        Res &= (*i)->UpdateAllEvntFlagSync();
    return Res;
}

size_t LqWrkBoss::KickAllWorkers() { return KickWorkers(0xfffffff); }

size_t LqWrkBoss::SetWrkMinCount(size_t NewVal) { return MinCount = NewVal; }

LqString LqWrkBoss::DebugInfo()
{
    LqString DbgStr;
    auto i = Wrks.begin();
    DbgStr += "============\n";
    DbgStr += " Count workers " + std::to_string(i.size()) + "\n";

    for(; !i.is_end(); i++)
    {
        DbgStr.append("============\n");
        DbgStr.append((*i)->DebugInfo());
    }
    return DbgStr;
}

LqString LqWrkBoss::AllDebugInfo()
{
    LqString DbgStr;
    auto i = Wrks.begin();
    DbgStr += "============\n";
    DbgStr += " Count workers " + std::to_string(i.size()) + "\n";

    for(; !i.is_end(); i++)
    {
        DbgStr.append("============\n");
        DbgStr.append((*i)->AllDebugInfo());
    }
    return DbgStr;
}

/*
* C - shell for global worker boss
*/

LqWrkBoss* LqWrkBoss::GetGlobal() { return &Boss; }

LQ_EXTERN_C void *LQ_CALL LqWrkBossGet() { return &Boss; }

LQ_EXTERN_C void LQ_CALL LqWrkBossKickWrks(size_t Count) { Boss.KickWorkers(Count); }

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

LQ_EXTERN_C int LQ_CALL LqWrkBossEnumCloseRmEvntByProto(const LqProto* Proto, void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr *Conn)) 
{ 
    return Boss.EnumCloseRmEvntByProto(Proto, UserData, Proc);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossEnumCloseRmEvnt(void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr *Conn)) { return Boss.EnumCloseRmEvnt(UserData, Proc); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount) { return Boss.SetWrkMinCount(NewCount); }

LQ_EXTERN_C int LQ_CALL LqWrkBossStartAllWrkSync() { return Boss.StartAllWorkersSync(); }

LQ_EXTERN_C int LQ_CALL LqWrkBossStartAllWrkAsync() { return Boss.StartAllWorkersAsync(); }

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCountWrk() { return Boss.CountWorkers(); }
