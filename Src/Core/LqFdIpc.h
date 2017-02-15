/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqFdIpc... - Transfer files descriptors between working processes.
*/

#ifndef __LQ_FD_IPC_HAS_INCLUDED_H__
#define __LQ_FD_IPC_HAS_INCLUDED_H__

#include "LqOs.h"
#include "Lanq.h"
#include "LqErr.h"
#include "LqDef.h"


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqFdIpc {
    LqEvntFd       Evnt;
    int            Pid;

    void(LQ_CALL  *RecvHandler)(LqFdIpc* FdIpc);
    void(LQ_CALL  *CloseHandler)(LqFdIpc* FdIpc);

    void*          UserData;

    unsigned char  Flags;

    uint8_t        Lk;
    int16_t        Deep;
    volatile int   ThreadOwnerId;
};

typedef struct LqFdIpc LqFdIpc;

#pragma pack(pop)

LQ_EXTERN_C_BEGIN
/*
Just create socket buffer
@SockFd - Socket descriptor
@UserData - User data
*/
LQ_IMPORTEXPORT LqFdIpc* LQ_CALL LqFdIpcOpen(const char* Name, bool IsNoInherit, void* lqain UserData);
LQ_IMPORTEXPORT LqFdIpc* LQ_CALL LqFdIpcCreate(const char* Name, bool IsNoInherit, void* lqain UserData);



LQ_IMPORTEXPORT bool LQ_CALL LqFdIpcDelete(LqFdIpc* lqain lqats FdIpc);
/*
* Give task to workers
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFdIpcGoWork(LqFdIpc* lqaio lqats FdIpc, void* lqain lqaopt WrkBoss);

/*
*
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFdIpcInterruptWork(LqFdIpc* lqaio lqats FdIpc);

LQ_IMPORTEXPORT int LQ_CALL LqFdIpcSend(LqFdIpc* lqaio lqats FdIpc, int Fd, void* AttachedBuffer, int SizeBuffer);
LQ_IMPORTEXPORT int LQ_CALL LqFdIpcRecive(LqFdIpc* lqaio lqats FdIpc, int* Fd, void** AttachedBuffer);
LQ_IMPORTEXPORT void LQ_CALL LqFdIpcLock(LqFdIpc* lqaio lqats FdIpc);
LQ_IMPORTEXPORT void LQ_CALL LqFdIpcUnlock(LqFdIpc* lqaio lqats FdIpc);

LQ_EXTERN_C_END

#endif