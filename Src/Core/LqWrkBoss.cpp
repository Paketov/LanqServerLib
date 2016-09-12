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
#include <vector>
#include "LqWrkBoss.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
#include "LqQueueCmd.hpp"

#undef max

static LqWrkBoss Boss;


LqWrkBoss::LqWrkBoss(): MinCount(0)
{
    Wrks = LqFastAlloc::New<WorkerArray>();
}
LqWrkBoss::LqWrkBoss(size_t CountWorkers) : MinCount(0)
{
    Wrks = LqFastAlloc::New<WorkerArray>();
    AddWorkers(CountWorkers);
}

LqWrkBoss::WorkerArray::WorkerArray(const LqWrkArrPtr Another, const LqWrkPtr NewWorker, bool IsAdd): CountPointers(0)
{
    if(IsAdd)
    {
        intptr_t NewCount = Another->Count + 1;
        auto NewArr = (LqWrkPtr*)malloc(NewCount * sizeof(LqWrkPtr));
        if(NewArr == nullptr)
        {
            LQ_ERR("LqWrkBoss::WorkerArray::WorkerArray() not alloc memory\n");
            throw "Not alloc";
        }
        for(intptr_t i = 0; i < Another->Count; i++)
            new(NewArr + i) LqWrkPtr(Another->Ptrs[i]);
        new(NewArr + Another->Count) LqWrkPtr(NewWorker);
        Count = NewCount;
        Ptrs = NewArr;
    } else
    {
        auto NewArr = (LqWrkPtr*)malloc(Another->Count * sizeof(LqWrkPtr));
        if(NewArr == nullptr)
        {
            LQ_ERR("LqWrkBoss::WorkerArray::WorkerArray() not alloc memory\n");
            throw "Not alloc";
        }
        int NewCount = 0;
        for(intptr_t i = 0, j = 0; i < Another->Count;)
        {
            if(NewWorker == Another->Ptrs[i])
            {
                i++;
                continue;
            }
            new(NewArr + j) LqWrkPtr(Another->Ptrs[i]);
            j++;
            i++;
            NewCount++;
        }
        Count = NewCount;
        Ptrs = NewArr;
    }
}

LqWrkBoss::WorkerArray::WorkerArray(const LqWrkArrPtr Another, ullong Id): CountPointers(0)
{
    auto NewArr = (LqWrkPtr*)malloc(Another->Count * sizeof(LqWrkPtr));
    if(NewArr == nullptr)
    {
        LQ_ERR("LqWrkBoss::WorkerArray::WorkerArray() not alloc memory\n");
        throw "Not alloc";
    }
    int NewCount = 0;
    for(intptr_t i = 0, j = 0; i < Another->Count;)
    {
        if(Id == Another->Ptrs[i]->Id)
        {
            i++;
            continue;
        }
        new(NewArr + j) LqWrkPtr(Another->Ptrs[i]);
        j++;
        i++;
        NewCount++;
    }
    Count = NewCount;
    Ptrs = NewArr;
}

LqWrkBoss::WorkerArray::WorkerArray(const LqWrkArrPtr Another, bool, size_t RemoveCount, size_t MinCount): CountPointers(0)
{
    intptr_t NewCount = Another->Count - RemoveCount;
    if(NewCount < MinCount)
    {
        if(Another->Count <= MinCount)
            NewCount = Another->Count;
        else
            NewCount = MinCount;
    }
    if(NewCount <= 0)
    {
        Count = 0;
        Ptrs = nullptr;
        return;
    }

    auto NewArr = (LqWrkPtr*)malloc(NewCount * sizeof(LqWrkPtr));
    if(NewArr == nullptr)
    {
        LQ_ERR("LqWrkBoss::WorkerArray::WorkerArray() not alloc memory\n");
        throw "Not alloc";
    }
    for(intptr_t i = 0; i < NewCount; i++)
        new(NewArr + i) LqWrkPtr(Another->Ptrs[i]);
    Count = NewCount;
    Ptrs = NewArr;
}

LqWrkBoss::WorkerArray::WorkerArray(): CountPointers(0), Count(0), Ptrs(nullptr) {}

LqWrkBoss::WorkerArray::~WorkerArray()
{
    for(intptr_t i = 0; i < Count; i++)
        Ptrs[i].~LqWrkPtr();
    if(Ptrs != nullptr)
        free(Ptrs);
}

LqWrk* LqWrkBoss::WorkerArray::operator[](size_t Index) const
{
    return Ptrs[Index].Get();
}

LqWrk* LqWrkBoss::WorkerArray::At(size_t Index) const
{
    return Ptrs[Index].Get();
}

LqWrkBoss::~LqWrkBoss()
{}

size_t LqWrkBoss::TransferAllEvnt(LqWrk* Source) const
{
    size_t Res = 0;
    Source->LockWrite();
    const LqWrkArrPtr LocalWrks = Wrks;
    lqevnt_enum_do(Source->EventChecker, i)
    {
        auto Hdr = LqEvntGetHdrByInterator(&Source->EventChecker, &i);

        intptr_t Min = std::numeric_limits<intptr_t>::max(), Index = -1;
        for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        {
            if(LocalWrks->At(i) == Source)
                continue;
            size_t l = LocalWrks->At(i)->GetAssessmentBusy();
            if(l < Min)
                Min = l, Index = i;
        }
        LqEvntRemoveByInterator(&Source->EventChecker, &i);
        if((Index != -1) && LocalWrks->At(Index)->AddEvntAsync(Hdr))
        {
            Res++;
        } else
        {
            LqEvntHdrClose(Hdr);
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

                intptr_t Min = std::numeric_limits<intptr_t>::max(), Index = -1;
                for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
                {
                    if(LocalWrks->At(i) == Source)
                        continue;
                    size_t l = LocalWrks->At(i)->GetAssessmentBusy();
                    if(l < Min)
                        Min = l, Index = i;
                }
               
                if((Index != -1) && LocalWrks->At(Index)->AddEvntAsync(Hdr))
                {
                    Res++;
                } else
                {
                    LqEvntHdrClose(Hdr);
                    LQ_ERR("LqWrkBoss::TransferAllEvnt() not adding event to list\n");
                }
                
            }
            break;
            default:
                /* Otherwise return current command in list*/
                Source->CommandQueue.SeparatePush(Command);
        }
    }

    Source->UnlockWrite();
    return Res;
}

int LqWrkBoss::AddWorkers(size_t Count, bool IsStart)
{
    if(Count <= 0)
        return 0;
    int Res = 0;
    LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0; i < Count; i++)
    {
        auto NewWorker = LqWrk::New(IsStart);
        if(NewWorker == nullptr)
        {
            LQ_ERR("LqWrkBoss::AddWorkers() not alloc new worker\n");
            continue;
        }
        LocalWrks = LqFastAlloc::New<WorkerArray>(LocalWrks, NewWorker, true);
        Res++;
    }
    Wrks = LocalWrks;
    return Res;
}

bool LqWrkBoss::AddWorker(const LqWrkPtr& Wrk)
{
    Wrks = LqFastAlloc::New<WorkerArray>(Wrks, Wrk, true);
    return true;
}

bool LqWrkBoss::AddEvntAsync(LqEvntHdr* Evnt)
{
    bool Res = true;
    LqWrkArrPtr LocalWrks = Wrks;
    if(LocalWrks->Count <= 0)
    {
        if(!AddWorkers(1, true))
        {
            return false;
        } else
        {
            LocalWrks = Wrks;
            if(LocalWrks->Count <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks->At(IndexMinUsed)->AddEvntAsync(Evnt);
}

bool LqWrkBoss::AddEvntSync(LqEvntHdr* Evnt)
{
    bool Res = true;
    LqWrkArrPtr LocalWrks = Wrks;

    if(LocalWrks->Count <= 0)
    {
        if(!AddWorkers(1, true))
        {
            return false;
        } else
        {
            LocalWrks = Wrks;
            if(LocalWrks->Count <= 0)
                return false;
        }
    }
    auto IndexMinUsed = MinBusy(LocalWrks);
    return LocalWrks->At(IndexMinUsed)->AddEvntSync(Evnt);
}

bool LqWrkBoss::TransferEvnt(const LqListEvnt & ConnectionsList) const
{
    bool Res = false;
    LqWrkArrPtr LocalWrks = Wrks;
    if(LocalWrks->Count > 0)
    {
        for(size_t i = 0; i < ConnectionsList.GetCount(); i++)
        {
            auto IndexWorker = MinBusy(LocalWrks);
            LocalWrks->At(IndexWorker)->AddEvntAsync(ConnectionsList[i]);
        }
        Res = true;
    }
    return Res;
}

size_t LqWrkBoss::CountWorkers() const
{
    const LqWrkArrPtr LocalWrks = Wrks;
    return LocalWrks->Count;
}

size_t LqWrkBoss::MinBusy(const LqWrkArrPtr& AllWrks, size_t* MinCount)
{
    size_t Min = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = AllWrks->Count; i < m; i++)
    {
        size_t l = AllWrks->At(i)->GetAssessmentBusy();
        if(l < Min)
            Min = l, Index = i;
    }
    *MinCount = Min;
    return Index;
}

size_t LqWrkBoss::MaxBusy(const LqWrkArrPtr& AllWrks, size_t* MaxCount)
{
    size_t Max = std::numeric_limits<size_t>::max(), Index = 0;
    for(size_t i = 0, m = AllWrks->Count; i < m; i++)
    {
        size_t l = AllWrks->At(i)->GetAssessmentBusy();
        if(l > Max)
            Max = l, Index = i;
    }
    *MaxCount = Max;
    return Index;
}

LqWrkPtr LqWrkBoss::operator[](size_t Index) const
{
    LqWrkArrPtr LocalWrks = Wrks;
    return LocalWrks->At(Index);
}

size_t LqWrkBoss::MinBusy(size_t* MinCount)
{
    const LqWrkArrPtr LocalWrks = Wrks;
    return MinBusy(LocalWrks, MinCount);
}

size_t LqWrkBoss::StartAllWorkersSync() const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += (LocalWrks->At(i)->StartSync() ? 1 : 0);
    return Res;
}

size_t LqWrkBoss::StartAllWorkersAsync() const
{
    size_t Ret = 0;
    const LqWrkArrPtr LocalWrks = Wrks;//Local ptr for thread safe
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Ret += (LocalWrks->At(i)->StartAsync() ? 1 : 0);
    return Ret;
}

bool LqWrkBoss::KickWorker(ullong IdWorker)
{
    /*Lock operation remove from array*/
    Wrks = LqFastAlloc::New<WorkerArray>(Wrks, IdWorker);
    return false;
}

size_t LqWrkBoss::KickWorkers(uintptr_t Count)
{
    Wrks = LqFastAlloc::New<WorkerArray>(Wrks, false, Count, MinCount);
    return Wrks->Count;
}

bool LqWrkBoss::CloseAllEvntAsync() const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->CloseAllEvntAsync();
    return Res;
}

size_t LqWrkBoss::CloseAllEvntSync() const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->CloseAllEvntSync();
    return Res;
}

size_t LqWrkBoss::CloseEventAsync(LqEvntHdr* Event) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->CloseEvntAsync(Event);
    return Res;
}

bool LqWrkBoss::CloseEventSync(LqEvntHdr* Event) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->CloseEvntSync(Event);
    return Res;
}

size_t LqWrkBoss::CloseEventByTimeoutSync(LqTimeMillisec LiveTime) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->RemoveConnOnTimeOutSync(LiveTime);
    return Res;
}

bool LqWrkBoss::CloseEventByTimeoutAsync(LqTimeMillisec LiveTime) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->RemoveConnOnTimeOutAsync(LiveTime);
    return Res;
}

size_t LqWrkBoss::CloseEventByTimeoutSync(const LqProto * Proto, LqTimeMillisec LiveTime) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->RemoveConnOnTimeOutSync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseEventByTimeoutAsync(const LqProto * Proto, LqTimeMillisec LiveTime) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->RemoveConnOnTimeOutAsync(Proto, LiveTime);
    return Res;
}

bool LqWrkBoss::CloseConnByIpAsync(const sockaddr* Addr) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->CloseConnByIpAsync(Addr);
    return Res;
}

size_t LqWrkBoss::CloseConnByIpSync(const sockaddr* Addr) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->CloseConnByIpSync(Addr);
    return Res;
}

bool LqWrkBoss::CloseConnByProtoAsync(const LqProto* Proto) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->CloseConnByProtoAsync(Proto);
    return Res;
}

size_t LqWrkBoss::CloseConnByProtoSync(const LqProto* Proto) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->CloseConnByProtoSync(Proto);
    return Res;
}

size_t LqWrkBoss::EnumDelEvnt(void * UserData, LqBool(*Proc)(void *UserData, LqEvntHdr* Conn)) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->EnumDelEvnt(UserData, Proc);
    return Res;
}

size_t LqWrkBoss::EnumDelEvntByProto(const LqProto* Proto, void * UserData, LqBool(*Proc)(void *UserData, LqEvntHdr *Conn)) const
{
    size_t Res = 0;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res += LocalWrks->At(i)->EnumDelEvntByProto(Proto, UserData, Proc);
    return Res;
}

bool LqWrkBoss::SyncEvntFlagAsync(LqEvntHdr* Conn) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->SyncEvntFlagAsync(Conn);
    return Res;
}

bool LqWrkBoss::SyncEvntFlagSync(LqEvntHdr * Conn) const
{
    bool Res = true;
    const LqWrkArrPtr LocalWrks = Wrks;
    for(size_t i = 0, m = LocalWrks->Count; i < m; i++)
        Res &= LocalWrks->At(i)->SyncEvntFlagSync(Conn);
    return Res;
}

size_t LqWrkBoss::KickAllWorkers()
{
    return KickWorkers(0xfffffff);
}

size_t LqWrkBoss::SetWrkMinCount(size_t NewVal)
{
    return MinCount = NewVal;
}

/*
* C - shell for global worker boss
*/

LqWrkBoss * LqWrkBoss::GetGlobal()
{
    return &Boss;
}

LQ_EXTERN_C void *LQ_CALL LqWrkBossGet()
{
    return &Boss;
}

LQ_EXTERN_C void LQ_CALL LqWrkBossKickWrks(size_t Count)
{
    Boss.KickWorkers(Count);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossKickWrk(size_t Index)
{
    return Boss.KickWorker(Index) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqWrkBossAddWrks(size_t Count, LqBool IsStart)
{
    return Boss.AddWorkers(Count, IsStart);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossAddWrk(void * Wrk)
{
    LqWrkPtr Ptr = (LqWrk*)Wrk;
    return Boss.AddWorker(Ptr) ? 0 : -1;
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossKickAllWrk()
{
    return Boss.KickAllWorkers();
}

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseAllEvntAsync()
{
    return Boss.CloseAllEvntAsync() ? 0 : -1;
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseAllEvntSync()
{
    return Boss.CloseAllEvntSync();
}

LQ_EXTERN_C int LQ_CALL LqWrkBossAddEvntAsync(LqEvntHdr * Conn)
{
    return Boss.AddEvntAsync(Conn) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqWrkBossAddEvntSync(LqEvntHdr * Conn)
{
    return Boss.AddEvntSync(Conn) ? 0 : -1;
}

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseEvntAsync(LqEvntHdr * Conn)
{
    return Boss.CloseEventAsync(Conn);
}

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseEvntSync(LqEvntHdr * Conn)
{
    return Boss.CloseEventSync(Conn) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqWrkBossSyncEvntFlagAsync(LqEvntHdr * Conn)
{
    return Boss.SyncEvntFlagAsync(Conn) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqWrkBossSyncEvntFlagSync(LqEvntHdr * Conn)
{
    return Boss.SyncEvntFlagSync(Conn) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByIpAsync(const sockaddr* Addr)
{
    return Boss.CloseConnByIpAsync(Addr) ? 0 : -1;
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByIpSync(const sockaddr* Addr)
{
    return Boss.CloseConnByIpSync(Addr);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByProtoAsync(const LqProto* Addr)
{
    return Boss.CloseConnByProtoAsync(Addr);
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByProtoSync(const LqProto* Addr)
{
    return Boss.CloseConnByProtoSync(Addr);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByTimeoutAsync(LqTimeMillisec TimeLive)
{
    return Boss.CloseEventByTimeoutAsync(TimeLive) ? 0 : -1;
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByTimeoutSync(LqTimeMillisec TimeLive)
{
    return Boss.CloseEventByTimeoutSync(TimeLive);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossCloseConnByProtoTimeoutAsync(const LqProto * Proto, LqTimeMillisec TimeLive)
{
    return Boss.CloseEventByTimeoutAsync(Proto, TimeLive) ? 0 : -1;
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCloseConnByProtoTimeoutSync(const LqProto * Proto, LqTimeMillisec TimeLive)
{
    return Boss.CloseEventByTimeoutSync(Proto, TimeLive) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqWrkBossEnumDelEvntByProto(const LqProto* Proto, void * UserData, LqBool(*Proc)(void *UserData, LqEvntHdr *Conn))
{
    return Boss.EnumDelEvntByProto(Proto, UserData, Proc);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossEnumDelEvnt(void * UserData, LqBool(*Proc)(void *UserData, LqEvntHdr *Conn))
{
    return Boss.EnumDelEvnt(UserData, Proc);
}

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount)
{
    return Boss.SetWrkMinCount(NewCount);
}

LQ_EXTERN_C int LQ_CALL LqWrkBossStartAllWrkSync()
{
    return Boss.StartAllWorkersSync();
}

LQ_EXTERN_C int LQ_CALL LqWrkBossStartAllWrkAsync()
{
    return Boss.StartAllWorkersAsync();
}

LQ_EXTERN_C size_t LQ_CALL LqWrkBossCountWrk()
{
    return Boss.CountWorkers();
}
