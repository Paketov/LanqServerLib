/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpAct... - Functions for work with action.
*/


#ifndef __LANQ_HTTP_CONN_H_HAS_INCLUDED__
#define __LANQ_HTTP_CONN_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqDef.h"
#include "LqHttp.h"

LQ_EXTERN_C_BEGIN


#if defined(LANQBUILD)

intptr_t LqHttpConnHdrEnm(bool IsResponse, char* Buf, size_t SizeHeaders, char** HeaderNameResult, char** HeaderNameResultEnd, char** HeaderValResult, char** HeaderValEnd);
bool LqHttpConnBufferRealloc(LqHttpConn* c, size_t NewSize);
int LqHttpConnRecive_Native(LqHttpConn* c, void* Buf, int ReadSize, int Flags);
size_t LqHttpConnSend_Native(LqHttpConn* c, const void* SourceBuf, size_t SizeBuf);

#endif

#define LqHttpConnGetRmtAddr(ConnectionPointer) ((sockaddr*)((LqHttpConn*)ConnectionPointer + 1))

#define LqHttpEvntActSet(Conn, EvntProc) ((Conn)->EventAct = (EvntProc))
#define LqHttpEvntCloseSet(Conn, EvntProc) ((Conn)->EventClose = (EvntProc))

#define LqHttpEvntCloseGet(Conn) ((Conn)->EventClose)
#define LqHttpEvntActGet(Conn) ((Conn)->EventAct)

#define LqHttpEvntActSetIgnore(Conn) LqHttpEvntActSet(Conn, LqHttpEvntDfltIgnoreAnotherEventHandler)
#define LqHttpEvntCloseSetIgnore(Conn) LqHttpEvntActSet(Conn, LqHttpEvntDfltIgnoreAnotherEventHandler)

LQ_IMPORTEXPORT void LQ_CALL LqHttpConnCallEvntAct(LqHttpConn* Conn);

LQ_IMPORTEXPORT void LQ_CALL LqHttpEvntDfltIgnoreAnotherEventHandler(LqHttpConn* lqaio lqatns c);

LQ_IMPORTEXPORT void LQ_CALL LqHttpConnPthRemove(LqHttpConn* lqaio lqatns c);

/*
* @return: only > 0, but may be < @SizeBuf
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpConnSend(LqHttpConn* lqain lqatns c, const void* lqain SourceBuf, size_t SizeBuf);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnSendFromStream(LqHttpConn* lqain lqatns c, LqSbuf* lqaio lqatns Stream, intptr_t Count);

LQ_IMPORTEXPORT LqFileSz LQ_CALL LqHttpConnSendFromFile(LqHttpConn* lqain lqatns c, int InFd, LqFileSz OffsetInFile, LqFileSz Count);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnCountPendingData(LqHttpConn* lqain lqatns c);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnRecive(LqHttpConn* lqain lqatns c, void* lqaout lqatns Buf, int ReadSize);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnPeek(LqHttpConn* lqain lqatns c, void* lqaout lqatns Buf, int ReadSize);

LQ_IMPORTEXPORT LqFileSz LQ_CALL LqHttpConnReciveInFile(LqHttpConn* lqain lqatns c, int OutFd, LqFileSz Count);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnReciveInStream(LqHttpConn* lqain lqatns c, LqSbuf* lqaio lqatns Stream, intptr_t Count);

LQ_IMPORTEXPORT size_t LQ_CALL LqHttpConnSkip(LqHttpConn* lqain lqatns c, size_t Count);

/*
* @return: If success return 4 for IPv4, 6 for IPv6. If err return 0;
*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnGetRemoteIpStr(const LqHttpConn* lqain lqatns c, char* lqaout DestStr, size_t DestStrSize);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnGetRemotePort(const LqHttpConn* lqain lqatns c);

//LQ_IMPORTEXPORT const LqHttpHdr* LQ_CALL LqHttpQurHdrSearchByCode(const LqHttpConn* lqain c, HttpHdrTypeEnm Type);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnDataStore(LqHttpConn* lqain lqatns c, const void* Name, const void* Value);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnDataGet(const LqHttpConn* lqain lqatns c, const void* Name, void** Value);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnDataUnstore(LqHttpConn* lqain lqatns c, const void* Name);



LQ_EXTERN_C_END


#endif
