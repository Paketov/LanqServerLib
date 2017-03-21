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

    void(LQ_CALL  *RecvHandler)(LqFdIpc* FdIpc);  /* Use for recive descriptor */
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
	LqFdIpcOpen - Open interprocess descriptor port.
		@Name - Source name
		@IsNoInherit - Inherit this IPC to child process
		@UserData - User data for port (Placed in the field LqFdIpc::UserData)
*/
LQ_IMPORTEXPORT LqFdIpc* LQ_CALL LqFdIpcOpen(const char* lqain lqacpeng Name, bool IsNoInherit, void* lqain UserData);

/*
	LqFdIpcCreate - Create interprocess descriptor port.
		@Name - Dest name
		@IsNoInherit - Inherit this IPC to child process
		@UserData - User data for port (Placed in the field LqFdIpc::UserData)
*/
LQ_IMPORTEXPORT LqFdIpc* LQ_CALL LqFdIpcCreate(const char* lqain lqacpeng Name, bool IsNoInherit, void* lqain UserData);


/*
	LqFdIpcDelete - delete interprocess fd port.
		@FdIpc - Target interprocess port
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFdIpcDelete(LqFdIpc* lqain lqats FdIpc);
/*
	LqFdIpcGoWork - Give task to workers
		@FdIpc - interprocess port
		@WrkBoss - target worker boss. If NULL, used global boss( LqWrkBoss::GetGlobal() ).
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFdIpcGoWork(LqFdIpc* lqaio lqats FdIpc, void* lqain lqaopt WrkBoss);

/*
	LqFdIpcInterruptWork - Interrupt recive/send descriptors. For begin again work, use LqFdIpcGoWork function.
		@FdIpc - interprocess port
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFdIpcInterruptWork(LqFdIpc* lqaio lqats FdIpc);

/*
	LqFdIpcSend - Send descriptor with attached data.
		@FdIpc - target descriptor port
		@Fd - Transfer descriptor. Can be -1
		@AttachedBuffer - attached buffer. Can be NULL
		@SizeBuffer - size transferred bufer. If @AttachedBuffer == NULL, then @SizeBuffer must be 0.
		@return - -1 - if have error, otherwise return count sended data
*/
LQ_IMPORTEXPORT int LQ_CALL LqFdIpcSend(LqFdIpc* lqaio lqats FdIpc, int Fd, const void* lqain lqaopt AttachedBuffer, int SizeBuffer);

/*
	LqFdIpcRecive - Recive descriptor and attached buffer.
		@FdIpc - target descriptor port
		@Fd - Dest descriptor.
		@AttachedBuffer - attached buffer. Can be NULL.
		@return - -1 - if have error, otherwise return count recived attached data
*/
LQ_IMPORTEXPORT int LQ_CALL LqFdIpcRecive(LqFdIpc* lqaio lqats FdIpc, int* lqaout Fd, void** lqaout lqaopt AttachedBuffer);

/*
	LqFdIpcLock - Lock descriptor port for exclusive use
*/
LQ_IMPORTEXPORT void LQ_CALL LqFdIpcLock(LqFdIpc* lqaio lqats FdIpc);

/*
	LqFdIpcUnlock - Unlock descriptor port
*/
LQ_IMPORTEXPORT void LQ_CALL LqFdIpcUnlock(LqFdIpc* lqaio lqats FdIpc);

LQ_EXTERN_C_END

#endif