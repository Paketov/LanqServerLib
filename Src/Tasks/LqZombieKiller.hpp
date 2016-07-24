#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqZombieKillerTask
*/



#include "LqWrkTask.hpp"
#include "LqWrkList.hpp"
#include "LqDef.h"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LqZombieKillerTask:
    public LqWrkTask::Task,
    public virtual LqWrkList
{
public:
    LqTimeMillisec              TimeLiveConnectionMillisec;
    bool                        IsSyncCheck;

    virtual uintptr_t SendCommand(const char * Command, ...); //gettimelife settimelife

    LqZombieKillerTask();
    virtual void WorkingMethod();
};

#pragma pack(pop)