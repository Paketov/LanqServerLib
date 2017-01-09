/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpAtz... - Make user uthorization.
*/

#ifndef __LQ_HTTP_ATZ_H_HAS_INCLUDED__
#define __LQ_HTTP_ATZ_H_HAS_INCLUDED__

#include "LqHttp.h"
#include "LqOs.h"

#if defined(LANQBUILD)
void _LqHttpAtzDelete(LqHttpAtz* NetAutoriz);
#endif

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT LqHttpAtz* LQ_CALL LqHttpAtzCreate(LqHttpAtzTypeEnm AuthType, const char* lqain lqautf8 Realm);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpAtzAdd(
    LqHttpAtz* lqaio NetAutoriz,
    uint8_t AccessMask,
    const char* lqain lqautf8 UserName,
    const char* lqain lqautf8 Password
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpAtzRemove(LqHttpAtz* lqaio NetAutoriz, const char* lqain lqautf8 UserName);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpAtzDo(LqHttpConn* lqaio c, uint8_t AccessMask);

LQ_IMPORTEXPORT void LQ_CALL LqHttpAtzAssign(LqHttpAtz* lqaio NetAutoriz);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpAtzRelease(LqHttpAtz* lqaio NetAutoriz);

LQ_IMPORTEXPORT void LQ_CALL LqHttpAtzLockWrite(LqHttpAtz* lqaio NetAutoriz);

LQ_IMPORTEXPORT void LQ_CALL LqHttpAtzLockRead(LqHttpAtz* lqaio NetAutoriz);

LQ_IMPORTEXPORT void LQ_CALL LqHttpAtzUnlock(LqHttpAtz* lqaio NetAutoriz);

LQ_EXTERN_C_END

#endif