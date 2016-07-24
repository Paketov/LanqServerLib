/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqZombieKillerTask
*/


#include "LqOs.h"
#include "LqWrk.hpp"
#include "LqZombieKiller.hpp"
#include "LqStr.h"

#include <stdarg.h>

LqZombieKillerTask::LqZombieKillerTask(): Task("LqZombieKillerTask")
{
    this->SetPeriodMillisec(30 * 1000);

    IsSyncCheck = false;
    TimeLiveConnectionMillisec = 30 * 60 * 1000; //30Min
}

void LqZombieKillerTask::WorkingMethod()
{
    WorkerListLocker.LockRead();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
    {
	if(IsSyncCheck)
	    Workers[i]->RemoveConnOnTimeOutSync(TimeLiveConnectionMillisec);
	else
	    Workers[i]->RemoveConnOnTimeOutAsync(TimeLiveConnectionMillisec);
    }
    WorkerListLocker.UnlockRead();
}


uintptr_t LqZombieKillerTask::SendCommand(const char * Command, ...)
{
    va_list va;
    va_start(va, Command);
    if(LqStrSame(Command, "settimelife"))
    {
	TimeLiveConnectionMillisec = va_arg(va, LqTimeMillisec);
	return 0;
    } else if(LqStrSame(Command, "gettimelife"))
    {
	auto TimeDest = va_arg(va, LqTimeMillisec*);
	*TimeDest = TimeLiveConnectionMillisec;
	return 0;
    }
    return -1;
}
