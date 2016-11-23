/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*/

#ifndef __LQ_WRK_BOSS_H_HAS_BEEN_DEFINED__
#define __LQ_WRK_BOSS_H_HAS_BEEN_DEFINED__

#include "LqOs.h"
#include "Lanq.h"
#include "LqConn.h"

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT void* LQ_CALL LqWrkBossGet();

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAddWrks(size_t Count, bool IsStart);
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAddWrk(void* Wrk);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossKickAllWrk();
LQ_IMPORTEXPORT void LQ_CALL LqWrkBossKickWrks(size_t Count);
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossKickWrk(size_t Index);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseAllEvntAsync();
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseAllEvntSync();

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAddEvntAsync(LqEvntHdr* Conn);
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAddEvntSync(LqEvntHdr* Conn);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossUpdateAllEvntFlagAsync();
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossUpdateAllEvntFlagSync();

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseConnByIpAsync(const struct sockaddr* Addr);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseConnByIpSync(const struct sockaddr* Addr);

LQ_IMPORTEXPORT bool LQ_CALL LqWrkBossRemoveEvnt(LqEvntHdr* Conn);
LQ_IMPORTEXPORT bool LQ_CALL LqWrkBossCloseEvnt(LqEvntHdr* Conn);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseConnByProtoAsync(const LqProto* Addr);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseConnByProtoSync(const LqProto* Addr);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseConnByTimeoutAsync(LqTimeMillisec TimeLive);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseConnByTimeoutSync(LqTimeMillisec TimeLive);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseConnByProtoTimeoutAsync(const LqProto* Proto, LqTimeMillisec TimeLive);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseConnByProtoTimeoutSync(const LqProto* Proto, LqTimeMillisec TimeLive);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossEnumCloseRmEvntByProto(const LqProto* Proto, void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr *Conn));
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossEnumCloseRmEvnt(void * UserData, unsigned(*Proc)(void *UserData, LqEvntHdr *Conn));

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossStartAllWrkSync();
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossStartAllWrkAsync();

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCountWrk();

LQ_EXTERN_C_END

#endif