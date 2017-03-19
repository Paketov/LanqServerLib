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

#define LqEvntIsWorking(LqSysPoll) (((LqClientHdr*)(LqSysPoll))->WrkOwner != NULL)

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

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseAllClientsAsync();
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseAllClientsSync();

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossUpdateAllClientsFlagAsync();
LQ_IMPORTEXPORT int LQ_CALL LqWrkBossUpdateAllClientsFlagSync();

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseClientsByIpAsync(const struct sockaddr* Addr);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseClientsByIpSync(const struct sockaddr* Addr);

LQ_IMPORTEXPORT bool LQ_CALL LqWrkBossRemoveClients(LqClientHdr* Conn);
LQ_IMPORTEXPORT bool LQ_CALL LqWrkBossCloseClients(LqClientHdr* Conn);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseClientsByProtoAsync(const LqProto* Addr);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseClientsByProtoSync(const LqProto* Addr);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseClientsByTimeoutAsync(LqTimeMillisec TimeLive);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseClientsByTimeoutSync(LqTimeMillisec TimeLive);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossCloseClientsByProtoTimeoutAsync(const LqProto* Proto, LqTimeMillisec TimeLive);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCloseClientsByProtoTimeoutSync(const LqProto* Proto, LqTimeMillisec TimeLive);

LQ_IMPORTEXPORT int LQ_CALL LqWrkBossAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCancelAsyncCall(void(LQ_CALL*AsyncProc)(void*), void* UserData, bool IsAll);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossEnumClientsCloseRmEvntByProto(int(LQ_CALL*Proc)(void*, LqClientHdr*), const LqProto* Proto, void * UserData);
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossEnumClients(int(LQ_CALL*Proc)(void*, LqClientHdr*), void * UserData);

LQ_IMPORTEXPORT bool LQ_CALL LqWrkBossEnumClientsAndCallFinAsync(
    int(LQ_CALL*EventAct)(void*, size_t, void*, LqClientHdr*, LqTimeMillisec),
    uintptr_t(LQ_CALL*FinFunc)(void*, size_t),
    void * UserData,
    size_t UserDataSize
);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossSetMinWrkCount(size_t NewCount);

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossStartAllWrkSync();
LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossStartAllWrkAsync();

LQ_IMPORTEXPORT size_t LQ_CALL LqWrkBossCountWrk();

/*
* @EvntOrConn - LqConn or LqEvntFd.
* @Flag - New flags LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP, LQEVNT_FLAG_RDHUP
* @return: 0 - Time out, 1 - thread work set a new value
*/
LQ_IMPORTEXPORT int LQ_CALL LqClientSetFlags(void* EvntOrConn, LqEvntFlag Flag, LqTimeMillisec WaitTime);

#define LqClientGetFlags(EvntOrConn) (((LqClientHdr*)EvntOrConn)->Flag)
/*
* Set close connection. (In async mode)
*  @EvntOrConn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqClientSetClose(void* EvntOrConn);
/*
* Set close immediately(call close handler in worker owner)
*  !!! Be careful when use this function !!!
*  @EvntOrConn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqClientSetClose2(void* EvntOrConn, LqTimeMillisec WaitTime);
/*
* Set close force immediately(call close handler if found event header immediately)
*  @EvntOrConn: LqConn or LqEvntFd
*  @return: 1- when close handle called, <= 0 - when not deleted
*/
LQ_IMPORTEXPORT bool LQ_CALL LqClientSetClose3(void* lqaio EvntOrConn);

/*
* Remove event(not call close handler) from main worker boss immediately
*  @EvntOrConn: LqConn or LqEvntFd
*  @return: 1- when removed, <= 0 - when not removed
*/
LQ_IMPORTEXPORT bool LQ_CALL LqClientSetRemove3(void* lqaio EvntOrConn);

/*
* Add new file descriptor to follow async
*/
LQ_IMPORTEXPORT bool LQ_CALL LqClientAdd(void* lqaio EvntOrConn, void* lqaopt lqain WrkBoss);
/*
* Add new file descriptor force immediately
*/
LQ_IMPORTEXPORT int LQ_CALL LqClientAdd2(void* lqaio EvntOrConn, void* lqaopt lqain WrkBoss);


LQ_IMPORTEXPORT void LQ_CALL LqConnInit(void* lqaout Conn, int NewFd, void* lqain NewProto, LqEvntFlag NewFlags);

LQ_IMPORTEXPORT void LQ_CALL LqEvntFdInit(
    void* lqaout EvntDest, 
    int NewFd, 
    LqEvntFlag NewFlags, 
    void(LQ_CALL*Handler)(LqEvntFd*, LqEvntFlag),
    void(LQ_CALL*CloseHandler)(LqEvntFd*)
);

LQ_IMPORTEXPORT void LQ_CALL LqClientCallCloseHandler(void* lqain EvntHdr);

LQ_IMPORTEXPORT void LQ_CALL LqClientSetOnlyOneBoss(void* lqaio EvntHdr, bool State);

#define LqClientIsOnlyOneBoss(EvntHdr) (LqClientGetFlags(EvntOrConn) & _LQEVNT_FLAG_ONLY_ONE_BOSS)

LQ_EXTERN_C_END

#endif