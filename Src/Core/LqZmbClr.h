/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqZmbClr (LanQ ZoMBie CLeaneR) - Timer event for clean zombie connections.
*/

#ifndef __LQ_ZMB_CLR_H_HAS_BEEN_DEFINED__
#define __LQ_ZMB_CLR_H_HAS_BEEN_DEFINED__

#include "LqOs.h"
#include "Lanq.h"
#include "LqConn.h"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqZmbClr {
    LqEvntFd           EvntFd;
    void*              UserData;
    void(LQ_CALL      *CloseHandler)(LqZmbClr* ZmbClr);
    union {
        const LqProto* Proto;
        const void*    UserData2;
    };

	void*              WrkBoss;
    
    LqTimeMillisec     Period;
    uint8_t            Flags;

    uint8_t            Lk;
    int16_t            Deep;
    volatile int  ThreadOwnerId;
} LqZmbClr;

#pragma pack(pop)

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT LqZmbClr* LQ_CALL LqZmbClrCreate(const void* lqain ProtoOrUserData2ForSockBuf, LqTimeMillisec Period, void*  lqain UserData, bool IsSockBuf);
LQ_IMPORTEXPORT bool LQ_CALL LqZmbClrGoWork(LqZmbClr* lqaio lqats ZmbClr, void* lqaopt lqain WrkBoss);
LQ_IMPORTEXPORT bool LQ_CALL LqZmbClrInterruptWork(LqZmbClr* lqaio lqats ZmbClr);
LQ_IMPORTEXPORT bool LQ_CALL LqZmbClrSetPeriod(LqZmbClr* lqaio lqats ZmbClr, LqTimeMillisec Period);
LQ_IMPORTEXPORT int LQ_CALL LqZmbClrDelete(LqZmbClr* lqaio lqats ZmbClr);

LQ_EXTERN_C_END


#endif