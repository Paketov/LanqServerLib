
#include "LqZmbClr.h"
#include "LqFile.h"
#include "LqWrkBoss.hpp"
#include "LqLog.h"

#include <string.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"


struct LqZmbClrAdditionalInfo
{
    LqTimeMillisec          TimeLive;
    const LqProto*          Proto;
    volatile LqBool         IsUsed;
    void*                   UserData;
    LqBool(*RemoveProc)(LqEvntFd* Evnt, void* UserData);
};


static void LQ_CALL LqZmbClrHandler(LqEvntFd* Fd, LqEvntFlag RetFlags)
{
    auto Proto = ((LqZmbClrAdditionalInfo*)Fd->UserData)->Proto;
    auto TimeLive = ((LqZmbClrAdditionalInfo*)Fd->UserData)->TimeLive;

    LqWrkBossCloseConnByProtoTimeoutAsync(Proto, TimeLive);
    LqFileTimerSet(Fd->Fd, TimeLive / 2);
}

static void LQ_CALL LqZmbClrHandlerClose(LqEvntFd* Fd, LqEvntFlag RetFlags)
{
    auto AddInfo = (LqZmbClrAdditionalInfo*)Fd->UserData;
    if((AddInfo->RemoveProc == nullptr) || (AddInfo->RemoveProc(Fd, AddInfo->UserData)))
    {
        Fd->UserData = 0;
        LqFastAlloc::Delete(AddInfo);
        LqFileClose(Fd->Fd);
        Fd->Fd = -1;
    }
}

LQ_EXTERN_C int LQ_CALL LqZmbClrInit(LqEvntFd* Dest, const LqProto* Proto, LqTimeMillisec TimeLive, LqBool(*RemoveProc)(LqEvntFd* Evnt, void* UserData), void* UserData)
{
    auto AddInfo = LqFastAlloc::New<LqZmbClrAdditionalInfo>();
    if(AddInfo == nullptr)
        return -1;
    int TimerFd = LqFileTimerCreate(LQ_O_NOINHERIT);
    if(TimerFd == -1)
    {
        LQ_ERR("LqZmbClrInit() LqFileTimerCreate(LQ_O_NOINHERIT) not create timer \"%s\"\n", strerror(lq_errno));
        LqFastAlloc::Delete(AddInfo);
        return -1;
    }
    LqEvntFdInit(Dest, TimerFd, LQEVNT_FLAG_RD | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_HUP);
    Dest->Handler = LqZmbClrHandler;
    Dest->CloseHandler = LqZmbClrHandlerClose;

    Dest->UserData = (uintptr_t)AddInfo;
    AddInfo->Proto = Proto;
    AddInfo->TimeLive = TimeLive;
    AddInfo->IsUsed = 1;

    AddInfo->UserData = UserData;
    AddInfo->RemoveProc = RemoveProc;

    LqFileTimerSet(Dest->Fd, TimeLive / 2);
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqZmbClrSetTimeLive(LqEvntFd* Dest, LqTimeMillisec TimeLive)
{
    if(Dest->UserData == 0)
        return -1;
    auto AddInfo = (LqZmbClrAdditionalInfo*)Dest->UserData;
    AddInfo->TimeLive = TimeLive;
    LqFileTimerSet(Dest->Fd, TimeLive / 2);
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqZmbClrUninit(LqEvntFd* Dest)
{
    LqEvntSetClose3(Dest);
    return 0;
}
