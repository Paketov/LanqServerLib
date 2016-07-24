#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkTask - Task scheduler.
*/


class LqWrkTask;


#include "LqLock.hpp"
#include "LqThreadBase.hpp"
#include "LqDef.hpp"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrkTask:
	public LqThreadBase
{
public:
#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)
	class LQ_IMPORTEXPORT Task
	{
		friend					LqWrkTask;
		LqTimeMillisec			LastCheck;
		LqTimeMillisec			Period;
		LqWrkTask*				WorkerOwner;

		bool GoWork(LqTimeMillisec CurTime, LqTimeMillisec& NewElapsedTime);
		virtual LqString DebugInfo();
	public:
		const char*				Name;

		LqWrkTask* GetOwner();
		void SetPeriodMillisec(LqTimeMillisec NewVal);
		LqTimeMillisec GetPeriodMillisec();
		virtual void WorkingMethod() = 0;
		virtual	uintptr_t SendCommand(const char * Command, ...) = 0;
		Task(const char* NameService);
		virtual ~Task();

		inline Task* GetPtr() { return this; }
	};
#pragma pack(pop)

private:
	mutable LqCondVar				CondVar;
	mutable LqMutex					Mut;
	mutable LqSafeRegion<uint>		SafeReg;
	Task**							Tasks;
	size_t							Count;

	virtual void BeginThread();
	virtual void NotifyThread();

	bool LockWrite() const;
	void UnlockWrite() const;
	bool LockRead() const;
	void UnlockRead() const;

public:

	LqWrkTask();
	~LqWrkTask();

	bool Add(Task* Service);
	bool Remove(Task* Service);

	Task* operator[](const char* Name) const;

	void CheckNow() const;

	LqString DebugInfo() const;
	LqString AllDebugInfo() const;
};

#pragma pack(pop)
