/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkTask - Task scheduler.
*/


#include "LqOs.h"
#include "LqWrk.hpp"
#include "LqWrkTask.hpp"
#include "LqTime.h"
#include "LqStr.h"

#include <string.h>

LqWrkTask::LqWrkTask(): Count(0), Tasks(nullptr), LqThreadBase("Task worker"), SafeReg() {}

LqWrkTask::~LqWrkTask()
{
    LockWrite();
    for(int i = Count - 1; i >= 0; i--)
	Tasks[i]->WorkerOwner = nullptr;
    Count = 0;
    if(Tasks != nullptr)
	free(Tasks);
    UnlockWrite();
    EndWorkSync();
}

void LqWrkTask::BeginThread()
{
    LqUniqueLock			Lock(Mut);
    LqTimeMillisec			TimeWait = 0;
    while(true)
    {
	SafeReg.EnterSafeRegion();
#undef max
	TimeWait = LqTimeGetMaxMillisec();

	auto CurTime = LqTimeGetLocMillisec();
	for(size_t i = 0; i < Count; i++)
	    Tasks[i]->GoWork(CurTime, TimeWait);

	if(LqThreadBase::IsShouldEnd)
	    break;
	CondVar.wait_for(Lock, std::chrono::milliseconds(TimeWait));
    }
}

bool LqWrkTask::Add(Task* Service)
{
    LockWrite();
    for(size_t i = 0; i < Count; i++)
	if(Tasks[i] == Service)
	{
	    UnlockWrite();
	    return true;
	}
    Service->WorkerOwner = this;
    auto NewArr = (decltype(Tasks))realloc(Tasks, sizeof(Tasks[0]) * (Count + 1));
    if(NewArr == nullptr)
    {
	UnlockWrite();
	return false;
    }
    NewArr[Count++] = Service;
    Tasks = NewArr;
    UnlockWrite();
    CheckNow();
    return true;
}

bool LqWrkTask::Remove(Task* Service)
{
    LockWrite();
    for(size_t i = 0; i < Count; i++)
	if(Tasks[i] == Service)
	{
	    Count--;
	    Tasks[i] = Tasks[Count];
	    Tasks = (decltype(Tasks))realloc(Tasks, sizeof(Tasks[0]) * Count);
	    Service->WorkerOwner = nullptr;
	    UnlockWrite();
	    return true;
	}
    UnlockWrite();
    CheckNow();
    return false;
}

bool LqWrkTask::LockWrite() const
{
    if(std::this_thread::get_id() == get_id())
    {
	SafeReg.EnterSafeRegionAndSwitchToWriteMode();
	return true;
    }
    SafeReg.OccupyWrite();
    CondVar.notify_one();
    while(!SafeReg.TryWaitRegion())
    {
	if(IsOut)
	    return false;
	LqThreadYield();
    }
    return true;
}

bool LqWrkTask::LockRead() const
{
    if(std::this_thread::get_id() == get_id())
    {
	SafeReg.EnterSafeRegionAndSwitchToReadMode();
	return true;
    }
    SafeReg.OccupyRead();
    CondVar.notify_one();
    while(!SafeReg.TryWaitRegion())
    {
	if(!joinable())
	    return false;
	LqThreadYield();
    }
    return true;
}

void LqWrkTask::UnlockRead() const { SafeReg.ReleaseRead(); }

void LqWrkTask::UnlockWrite() const { SafeReg.ReleaseWrite(); }

void LqWrkTask::CheckNow() const { if(joinable()) CondVar.notify_one(); }

bool LqWrkTask::Task::GoWork(LqTimeMillisec CurTime, LqTimeMillisec& NewElapsedTime)
{
    bool r = false;
    auto TimeLeft = Period - (CurTime - LastCheck);

    if(TimeLeft <= 0)
    {
	WorkingMethod();
	TimeLeft = Period;
	LastCheck = CurTime;
	r = true;
    }
    if(TimeLeft < NewElapsedTime)
	NewElapsedTime = TimeLeft;
    return r;
}

LqWrkTask::Task::Task(const char* NameService): Name(NameService), WorkerOwner(nullptr), LastCheck(0), Period(5 * 1000) {}

void LqWrkTask::Task::SetPeriodMillisec(LqTimeMillisec NewVal)
{
    Period = NewVal;
    if(WorkerOwner != nullptr)
	WorkerOwner->CheckNow();
}


LqTimeMillisec LqWrkTask::Task::GetPeriodMillisec() { return Period; }

LqWrkTask* LqWrkTask::Task::GetOwner() { return WorkerOwner; }

LqWrkTask::Task::~Task()
{
    if(WorkerOwner != nullptr)
	WorkerOwner->Remove(this);
}


LqWrkTask::Task* LqWrkTask::operator[](const char* Name) const
{
    Task* r = nullptr;
    LockRead();
    for(size_t i = 0; i < Count; i++)
	if(LqStrSame(Tasks[i]->Name, Name))
	{
	    r = Tasks[i];
	    break;
	}
    UnlockRead();
    return r;
}

void LqWrkTask::NotifyThread() { CondVar.notify_one(); }

LqString LqWrkTask::Task::DebugInfo() { return ""; }

LqString LqWrkTask::DebugInfo() const
{
    LockRead();
    LqString r = "Count services: " + LqToString(Count) + "\nServices: ";
    for(size_t i = 0; i < Count; i++)
    {
	r = r + "\"" + Tasks[i]->Name + "\" ";
    }
    r += "\n";
    UnlockRead();
    return r;
}

LqString LqWrkTask::AllDebugInfo() const
{
    LockRead();
    LqString r = "---------\nCount services: " + LqToString(Count) + "\n---------\nServices: ";
    for(size_t i = 0; i < Count; i++)
    {
	r = r + "\nService #" + LqToString(i) + " \"" + Tasks[i]->Name + "\" \n" + Tasks[i]->DebugInfo();
    }
    r += "---------\n";
    UnlockRead();
    return r;
}
