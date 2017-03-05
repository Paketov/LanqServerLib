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

#define LqEvntIsWorking(LqSysPoll) (((LqEvntHdr*)(LqSysPoll))->WrkOwner != NULL)

static inline void LQ_CALL __LqProtoEmptyHandler(LqConn*, LqEvntFlag) {}
static inline void LQ_CALL __LqProtoEmptyCloseHandler(LqConn*) {}
static inline bool LQ_CALL __LqProtoEmptyCmpAddressProc(LqConn*, const void*) { return false; }
static inline bool LQ_CALL __LqProtoEmptyKickByTimeOutProc(LqConn*, LqTimeMillisec, LqTimeMillisec) { return false; }
static inline char* LQ_CALL __LqProtoEmptyDebugInfoProc(LqConn*) { return nullptr; }

#define LqProtoInit(Proto) \
    ((LqProto*)Proto)->Handler = __LqProtoEmptyHandler;\
    ((LqProto*)Proto)->CloseHandler = __LqProtoEmptyCloseHandler;\
    ((LqProto*)Proto)->KickByTimeOutProc = __LqProtoEmptyKickByTimeOutProc;\
    ((LqProto*)Proto)->CmpAddressProc = __LqProtoEmptyCmpAddressProc;\
    ((LqProto*)Proto)->DebugInfoProc = __LqProtoEmptyDebugInfoProc;

#define LqConnIsClose(Conn) (((LqConn*)(Conn))->Flag | LQEVNT_FLAG_END)


LQ_IMPORTEXPORT void* LQ_CALL LqWrkBossGet();

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAddWrks(size_t Count, bool IsStart);
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAddWrk(void* Wrk);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossKickAllWrk();
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossKickWrks(size_t Count);
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossKickWrk(size_t Index);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseAllEvntAsync();
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseAllEvntSync();

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

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCancelAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData, bool IsAll);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossEnumCloseRmEvntByProto(int(LQ_CALL*Proc)(void*, LqEvntHdr*), const LqProto* Proto, void * UserData);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossEnumCloseRmEvnt(int(LQ_CALL*Proc)(void*, LqEvntHdr*), void * UserData);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossStartAllWrkSync();
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossStartAllWrkAsync();

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCountWrk();

/*
* @EvntOrConn - LqConn or LqEvntFd.
* @Flag - New flags LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP, LQEVNT_FLAG_RDHUP
* @return: 0 - Time out, 1 - thread work set a new value
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetFlags(void* EvntOrConn, LqEvntFlag Flag, LqTimeMillisec WaitTime);

#define LqEvntGetFlags(EvntOrConn) (((LqEvntHdr*)EvntOrConn)->Flag)
/*
* Set close connection. (In async mode)
*  @EvntOrConn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose(void* EvntOrConn);
/*
* Set close immediately(call close handler in worker owner)
*  !!! Be careful when use this function !!!
*  @EvntOrConn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose2(void* EvntOrConn, LqTimeMillisec WaitTime);
/*
* Set close force immediately(call close handler if found event header immediately)
*  @EvntOrConn: LqConn or LqEvntFd
*  @return: 1- when close handle called, <= 0 - when not deleted
*/
LQ_IMPORTEXPORT bool LQ_CALL LqEvntSetClose3(void* lqaio EvntOrConn);

/*
* Remove event from main worker boss immediately(not call close handler)
*  @EvntOrConn: LqConn or LqEvntFd
*  @return: 1- when removed, <= 0 - when not removed
*/
LQ_IMPORTEXPORT bool LQ_CALL LqEvntSetRemove3(void* lqaio EvntOrConn);

/*
* Add new file descriptor to follow async
*/
LQ_IMPORTEXPORT bool LQ_CALL LqEvntAdd(void* lqaio EvntOrConn, void* lqaopt lqain WrkBoss);
/*
* Add new file descriptor force immediately
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntAdd2(void* lqaio EvntOrConn, void* lqaopt lqain WrkBoss);


LQ_IMPORTEXPORT void LQ_CALL LqConnInit(void* lqaout Conn, int NewFd, void* lqain NewProto, LqEvntFlag NewFlags);

LQ_IMPORTEXPORT void LQ_CALL LqEvntFdInit(
    void* lqaout EvntDest, 
    int NewFd, 
    LqEvntFlag NewFlags, 
    void(LQ_CALL*Handler)(LqEvntFd*, LqEvntFlag),
    void(LQ_CALL*CloseHandler)(LqEvntFd*)
);

LQ_IMPORTEXPORT void LQ_CALL LqEvntCallCloseHandler(void* lqain EvntHdr);

LQ_IMPORTEXPORT void LQ_CALL LqEvntSetOnlyOneBoss(void* lqaio EvntHdr, bool State);

#define LqEvntIsOnlyOneBoss(EvntHdr) (LqEvntGetFlags(EvntOrConn) & _LQEVNT_FLAG_ONLY_ONE_BOSS)

LQ_EXTERN_C_END

#endif