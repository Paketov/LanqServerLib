/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqDirEvnt... - Watch for changes in the directories.
*/


#include "LqOs.h"

#include "LqDirEvnt.h"
#include "LqErr.h"

#define LQ_DIREVNT

#if defined(LQPLATFORM_WINDOWS)
# include "LqDirEvntWin.h"
#elif defined(LQPLATFORM_LINUX) | defined(LQPLATFORM_ANDROID) 
# include "LqDirEvntInotify.h"
#else

#if _MSC_VER
# pragma message ("Warning: Not implement LqDirEvnt for this platform")
#elif   __GNUC__
# warning("Warning: Not implement LqDirEvnt for this platform")
#endif


LQ_EXTERN_C int LQ_CALL LqDirEvntAdd(LqDirEvnt* Evnt, const char* Name, uint8_t FollowFlag)
{
    lq_set_errno(ENOSYS);
    return -1;
}

LQ_EXTERN_C int LQ_CALL LqDirEvntInit(LqDirEvnt * Evnt)
{
    lq_set_errno(ENOSYS);
    return -1;
}

LQ_EXTERN_C void LqDirEvntUninit(LqDirEvnt * Evnt)
{
}

LQ_EXTERN_C void LQ_CALL LqDirEvntPathFree(LqDirEvntPath ** Dest)
{
}

LQ_EXTERN_C int LQ_CALL LqDirEvntRm(LqDirEvnt* Evnt, const char* Name)
{
    lq_set_errno(ENOSYS);
    return -1;
}

LQ_EXTERN_C int LQ_CALL LqDirEvntCheck(LqDirEvnt* Evnt, LqDirEvntPath** Dest, LqTimeMillisec WaitTime)
{
    lq_set_errno(ENOSYS);
    return -1;
}



#endif