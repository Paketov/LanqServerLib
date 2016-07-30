/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqDirEvnt... - Watch for changes in the directories.
*/


#ifndef __LQ_DIR_EVNT_H_HAS_INCLUDED__
#define __LQ_DIR_EVNT_H_HAS_INCLUDED__


#include "LqOs.h"
#include "LqDef.hpp"
#include "LqLock.hpp"

LQ_EXTERN_C_BEGIN

enum
{
    LQDIREVNT_ADDED = 1,
    LQDIREVNT_RM = 2,
    LQDIREVNT_MOD = 4,
    LQDIREVNT_MOVE_FROM = 8,
    LQDIREVNT_MOVE_TO = 16,
    LQDIREVNT_SUBTREE = 32
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqDirEvnt
{
    void*	    Data;
    size_t	    Count;
};

struct LqDirEvntPath
{
    LqDirEvntPath*  Next;
    uint8_t	    Flag;
    char	    Name[1];
};

#pragma pack(pop)


LQ_IMPORTEXPORT int LQ_CALL LqDirEvntInit(LqDirEvnt* lqaio Evnt);
LQ_IMPORTEXPORT void LQ_CALL LqDirEvntUninit(LqDirEvnt* lqaio Evnt);

LQ_IMPORTEXPORT int LQ_CALL LqDirEvntAdd(LqDirEvnt* lqaio Evnt, const char* lqain Dest, uint8_t FollowFlag);
LQ_IMPORTEXPORT int LQ_CALL LqDirEvntRm(LqDirEvnt* lqaio Evnt, const char* lqain Name);

LQ_IMPORTEXPORT int LQ_CALL LqDirEvntCheck(LqDirEvnt* lqaio Evnt, LqDirEvntPath** lqaout Dest, LqTimeMillisec WaitTime);
LQ_IMPORTEXPORT void LQ_CALL LqDirEvntPathFree(LqDirEvntPath** lqaio Dest);

LQ_EXTERN_C_END

#endif
