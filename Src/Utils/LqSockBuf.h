/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSockBuf... - Hi-level async socket.
*/

#ifndef __LQ_SOCK_BUF_HAS_INCLUDED_H__
#define __LQ_SOCK_BUF_HAS_INCLUDED_H__

#include "LqOs.h"
#include "LqConn.h"
#include "LqErr.h"
#include "LqSbuf.h"
#include "LqDef.h"
#include "LqFche.h"


#define  LQSOCKBUF_ERR_INPUT           1
#define  LQSOCKBUF_ERR_OUTPUT_DATA     2
#define  LQSOCKBUF_ERR_WRITE_SOCKET    4
#define  LQSOCKBUF_ERR_READ_SOCKET     8
#define  LQSOCKBUF_ERR_UNKNOWN_SOCKET      16

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqSockBuf {
    LqConn         Conn;
    LqFbuf         Stream;

    void*          RspHeader;
    LqListHdr      Rsp;

    LqListHdr      Rcv;

    LqTimeMillisec StartTime;
    LqTimeMillisec LastExchangeTime;
    LqTimeMillisec KeepAlive;

    void(*ErrHandler)(LqSockBuf* Buf, int Err);
    void(*CloseHandler)(LqSockBuf* Buf);


    void*          UserData;
    LqFche*        Cache;

    unsigned char  Flags;

    uint8_t        Lk;
    int16_t        Deep;
    volatile intptr_t ThreadOwnerId;
};

typedef struct LqSockBuf LqSockBuf;


struct LqSockAcceptor {
    LqConn         Conn;
    void*          UserData;
    void(*AcceptProc)(LqSockAcceptor* Acceptor);
    void(*CloseHandler)(LqSockAcceptor* Acceptor);
    LqFche*        Cache;
    unsigned char  Flags;

    uint8_t        Lk;
    int16_t        Deep;
    volatile intptr_t ThreadOwnerId;
};

typedef struct LqSockAcceptor LqSockAcceptor;

#pragma pack(pop)

LQ_EXTERN_C_BEGIN
/*
Just create socket buffer
    @SockFd - Socket descriptor
    @UserData - User data
*/
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockBufCreate(int SockFd, void* lqain UserData);

/*
  Create socket buffer from SSL context
    @SockFd - Socket descriptor
    @SslCtx - SSL_CTX context
    @IsAccept - true- is accept, false- is connect
    @UserData - User data
*/
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockBufCreateSsl(int SockFd, void* lqain SslCtx, bool IsAccept, void* lqain UserData);

/*
* Uninit and delete LqSockBuf, you can use them in handlers(SockBuf thread protected)
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufDelete(LqSockBuf* lqain lqats SockBuf);
/*
* Give task to workers
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGoWork(LqSockBuf* lqaio lqats SockBuf, void* lqain WrkBoss);

/*
* Set instance of cache to SockBuf
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufSetInstanceCache(LqSockBuf* lqaio lqats SockBuf, LqFche* lqain lqamrelease Cache);
LQ_IMPORTEXPORT LqFche* LQ_CALL LqSockBufGetInstanceCache(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT void LQ_CALL LqSockBufSetKeepAlive(LqSockBuf* lqaio SockBuf, LqTimeMillisec NewValue);
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqSockBufGetKeepAlive(LqSockBuf* lqain SockBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyRspCompletion(LqSockBuf* lqaio lqats SockBuf, void(*CompletionProc)(void*, LqSockBuf*), void* lqain UserData);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFile(LqSockBuf* lqaio lqats SockBuf, const char* lqain Path);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFilePart(LqSockBuf* lqaio lqats SockBuf, const char* lqain Path, LqFileSz OffsetInFile, LqFileSz Count);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspStream(LqSockBuf* lqaio lqats SockBuf, LqFbuf* lqain lqamrelease File);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFd(LqSockBuf* lqaio lqats SockBuf, int InFd);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFdPart(LqSockBuf* lqaio lqats SockBuf, int InFd, LqFileSz OffsetInFile, LqFileSz Count);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintf(LqSockBuf* lqaio lqats SockBuf, const char* lqain Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfVa(LqSockBuf* lqaio lqats SockBuf, const char* lqain Fmt, va_list Va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufWrite(LqSockBuf* lqaio lqats SockBuf, const void* lqain Data, size_t SizeData);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfHdr(LqSockBuf* lqaio lqats SockBuf, const char* lqain Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfVaHdr(LqSockBuf* lqaio lqats SockBuf, const char* lqain Fmt, va_list Va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufWriteHdr(LqSockBuf* lqaio lqats SockBuf, const void* lqain Data, size_t SizeData);

/* Clear all response data queue */
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspClear(LqSockBuf* lqaio lqats SockBuf);
/* Clear all response data queue after forward header */
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspClearAfterHdr(LqSockBuf* lqaio lqats SockBuf);
/*
    Set forward response header
      !You must call LqSockBufRspUnsetHdr() before response, because data queue has locked on header.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspSetHdr(LqSockBuf* lqaio lqats SockBuf);
/* Unset forward header */
LQ_IMPORTEXPORT void LQ_CALL LqSockBufRspUnsetHdr(LqSockBuf* lqaio lqats SockBuf);

/*
    Get summary length date after header
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* lqaio lqats SockBuf);

/*Call RcvProc, when data has been recived and match by Fmt*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenMatch(LqSockBuf* lqaio lqats SockBuf, void* lqain UserData, void(*RcvProc)(void* UserData, LqSockBuf* Buf), const char* Fmt, int MatchCount, size_t MaxSize);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufScanf(LqSockBuf* lqaio lqats SockBuf, bool IsPeek, const char* Fmt, ...);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufScanfVa(LqSockBuf* lqaio lqats SockBuf, bool IsPeek, const char* Fmt, va_list Va);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenCompleteRead(LqSockBuf* lqaio lqats SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf), void* Dest, size_t Size);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufRead(LqSockBuf* lqaio lqats SockBuf, void* Dest, size_t Size);


LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenCompleteRecvStream(
    LqSockBuf* lqaio lqats SockBuf,
    void* lqain UserData, 
    intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf, LqFbuf* DestStream), 
    LqFbuf* lqain DestStream, 
    size_t Size
);

LQ_IMPORTEXPORT int LQ_CALL LqSockBufReadInStream(LqSockBuf* lqaio lqats SockBuf, LqFbuf* lqaout lqats Dest, size_t Size);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenCompleteRecvData(LqSockBuf* lqaio lqats SockBuf, void* lqain UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf));

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* lqaio lqats SockBuf);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvClear(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT void LQ_CALL LqSockBufFlush(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetRemoteAddr(LqSockBuf* lqain lqats SockBuf, LqConnAddr* lqaout Dest);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetLocAddr(LqSockBuf* lqain lqats SockBuf, LqConnAddr* lqaout Dest);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetRemoteAddrStr(LqSockBuf* lqain lqats SockBuf, char* lqaout Dest, size_t DestSize);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetLocAddrStr(LqSockBuf* lqain lqats SockBuf, char* lqaout Dest, size_t DestSize);

/* Sock acceptor */
LQ_IMPORTEXPORT LqSockAcceptor* LQ_CALL LqSockAcceptorCreate(
    const char* lqaopt lqain Host, 
    const char* lqain lqaopt Port, 
    int RouteProto, 
    int SockType, 
    int TransportProto, 
    int MaxConnections,
    bool IsNonBlock, 
    void* lqain UserData
);
LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorGoWork(LqSockAcceptor* lqain lqats SockAcceptor, void* lqain WrkBoss);
LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorDelete(LqSockAcceptor* lqain lqats SockAcceptor);

LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockAcceptorAccept(LqSockAcceptor* lqain lqats SockAcceptor, void* lqain UserData);
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockAcceptorAcceptSsl(LqSockAcceptor* lqain lqats SockAcceptor, void* lqain UserData, void* lqain SslCtx);
LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorSetInstanceCache(LqSockAcceptor* lqain lqats SockAcceptor, LqFche* lqain lqamrelease Cache);
LQ_IMPORTEXPORT LqFche* LQ_CALL LqSockAcceptorGetInstanceCache(LqSockAcceptor* lqain lqats SockAcceptor);

LQ_EXTERN_C_END

#endif