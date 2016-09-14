#ifndef __LQ_ZMB_CLR_H_HAS_BEEN_DEFINED__
#define __LQ_ZMB_CLR_H_HAS_BEEN_DEFINED__

#include "LqOs.h"
#include "Lanq.h"
#include "LqConn.h"

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqZmbClrInit(LqEvntFd* Dest, const LqProto* Proto, LqTimeMillisec TimeLive, bool (*RemoveProc)(LqEvntFd* Evnt, void* UserData), void* UserData);
LQ_IMPORTEXPORT int LQ_CALL LqZmbClrSetTimeLive(LqEvntFd* Dest, LqTimeMillisec TimeLive);
LQ_IMPORTEXPORT int LQ_CALL LqZmbClrUninit(LqEvntFd* Dest);

LQ_EXTERN_C_END


#endif