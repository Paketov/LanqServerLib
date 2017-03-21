/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSockBuf... - Hi-level async socket.
*   Used queue for recive or send data.
*/

#ifndef __LQ_SOCK_BUF_HAS_INCLUDED_H__
#define __LQ_SOCK_BUF_HAS_INCLUDED_H__

#include "LqOs.h"
#include "LqConn.h"
#include "LqErr.h"
#include "LqSbuf.h"
#include "LqDef.h"
#include "LqFche.h"
#include "LqZmbClr.h"


#define LQSOCKBUF_ERR_INPUT               1
#define LQSOCKBUF_ERR_OUTPUT_DATA         2
#define LQSOCKBUF_ERR_WRITE_SOCKET        4
#define LQSOCKBUF_ERR_READ_SOCKET         8
#define LQSOCKBUF_ERR_UNKNOWN_SOCKET      16

#define LQSOCKBUF_PEEK                    LQFBUF_SCANF_PEEK
#define LQSOCKBUF_PEEK_WHEN_ERR           LQFBUF_SCANF_PEEK_WHEN_ERR

#define LQSOCKBUF_FLAGS_WRITE_ERROR       LQFBUF_WRITE_ERROR
#define LQSOCKBUF_FLAGS_WRITE_WOULD_BLOCK LQFBUF_WRITE_WOULD_BLOCK
#define LQSOCKBUF_FLAGS_READ_ERROR        LQFBUF_READ_ERROR
#define LQSOCKBUF_FLAGS_READ_WOULD_BLOCK  LQFBUF_READ_WOULD_BLOCK

typedef LqFbufFlag LqSockBufErrFlags;

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqSockBuf {
    LqConn         Conn;
    LqFbuf         Stream;

    void*          RspHeader;
    LqListHdr      Rsp;

    LqListHdr      Rcv;
    LqListHdr      Rcv2;

    LqTimeMillisec StartTime;
    LqTimeMillisec LastExchangeTime;
    LqTimeMillisec KeepAlive;

    void(LQ_CALL  *ErrHandler)(LqSockBuf* Buf, int Err);
    void(LQ_CALL  *CloseHandler)(LqSockBuf* Buf);

    void*          UserData;
    void*          UserData2;
    LqFche*        Cache;

    uint8_t        Flags;

    uint8_t        Lk;
    int16_t        Deep;
    volatile int   ThreadOwnerId;
    uint32_t       RWPortion;

    int64_t        ReadOffset;
    int64_t        WriteOffset;
};

typedef struct LqSockBuf LqSockBuf;


struct LqSockAcceptor {
    LqConn         Conn;

    void(LQ_CALL  *AcceptProc)(LqSockAcceptor* Acceptor);
    void(LQ_CALL  *CloseHandler)(LqSockAcceptor* Acceptor);
    void*          UserData;
    LqFche*        Cache;
    uint8_t        Flags;

    uint8_t        Lk;
    int16_t        Deep;
    volatile int ThreadOwnerId;
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
    @IsAccept - true- is accept, false- is connect (for @SslCtx)
    @UserData - User data
*/
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockBufCreateSsl(int SockFd, void* lqain SslCtx, bool IsAccept, void* lqain UserData);

/*
* Uninit and delete LqSockBuf, you can use them in handlers(SockBuf thread protected)
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufDelete(LqSockBuf* lqain lqats SockBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufSetClose(LqSockBuf* lqain lqats SockBuf);
/*
* Give task to workers
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGoWork(LqSockBuf* lqaio lqats SockBuf, void* lqain lqaopt WrkBoss);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufInterruptWork(LqSockBuf* lqaio lqats SockBuf);

/*
* Set instance of cache to SockBuf
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufSetInstanceCache(LqSockBuf* lqaio lqats SockBuf, LqFche* lqain lqamrelease Cache);
LQ_IMPORTEXPORT LqFche* LQ_CALL LqSockBufGetInstanceCache(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT void LQ_CALL LqSockBufSetKeepAlive(LqSockBuf* lqaio lqats SockBuf, LqTimeMillisec NewValue);
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqSockBufGetKeepAlive(LqSockBuf* lqain SockBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspNotifyCompletion(LqSockBuf* lqaio lqats SockBuf, void* lqain UserData, void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, void*));
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFile(LqSockBuf* lqaio lqats SockBuf, const char* lqain Path);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFilePart(LqSockBuf* lqaio lqats SockBuf, const char* lqain Path, LqFileSz OffsetInFile, LqFileSz Count);

/*
* Response stream or pointer on virt file.
*  When you want send other type file, Size must be >= 0, otherwise can be -1.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFbuf(LqSockBuf* lqaio lqats SockBuf, LqFbuf* lqain lqamrelease File, LqFileSz Size);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFd(LqSockBuf* lqaio lqats SockBuf, int InFd);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFdPart(LqSockBuf* lqaio lqats SockBuf, int InFd, LqFileSz OffsetInFile, LqFileSz Count);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintf(LqSockBuf* lqaio lqats SockBuf, bool IsHdr, const char* lqain Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfVa(LqSockBuf* lqaio lqats SockBuf, bool IsHdr, const char* lqain Fmt, va_list Va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufWrite(LqSockBuf* lqaio lqats SockBuf, bool IsHdr, const void* lqain Data, size_t SizeData);

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

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvNotifyCompletion(
    LqSockBuf* lqaio lqats SockBuf,
    void* lqain UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData), /* if @Buf != NULL */
    bool IsSecondQueue
);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvNotifyWhenMatch(
    LqSockBuf* lqaio lqats SockBuf,
    void* lqain UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    const char* lqain Fmt,
    int MatchCount,
    size_t MaxSize,
    bool IsSecondQueue
);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvInRegion(
    LqSockBuf* lqaio lqats SockBuf,
    void* lqain UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* Dest, size_t Written, void* UserData),
    void* lqaout Dest,
    size_t Size,
    bool IsSecondQueue
);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvInFbuf(
    LqSockBuf* lqaio lqats SockBuf,
    LqFbuf* lqaout lqaopt DestStream,
    void* lqain UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, LqFbuf*, LqFileSz, void*),
    LqFileSz Size,
    bool IsSecondQueue
);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvInFbufAboveSeq(
    LqSockBuf* lqaio lqats SockBuf,
    LqFbuf* lqaout lqaopt DestStream,
    void* lqain UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, LqFbuf*, LqFileSz, void*, bool),
    const char* lqain ControlSeq,
    size_t ControlSeqSize,
    LqFileSz MaxSize,
    bool IsCaseIndepended,
    bool IsSecondQueue
);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvPulseRead(
    LqSockBuf* lqaio lqats SockBuf,
    void* lqain UserData,
    intptr_t(LQ_CALL *CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    bool IsSecondQueue
);
/*
    LqSockBufRcvWaitLenData
        Notify when recived length of data in internal buffer
*/

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvWaitLenData(
    LqSockBuf* lqaio lqats SockBuf,
    void* lqain UserData,
    void(LQ_CALL *CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    size_t TargetLen,  /* !! Be careful when set the large size of this parameter !! */
    bool IsSecondQueue
);

LQ_IMPORTEXPORT LqSockBufErrFlags LQ_CALL LqSockBufGetErrFlags(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufRcvBufSz(LqSockBuf* lqaio lqats SockBuf);
LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufRspBufSz(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* lqaio lqats SockBuf, bool IsHdr);

LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufRcvQueueLen(LqSockBuf* lqaio lqats SockBuf, bool IsSecondQueue);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufScanf(LqSockBuf* lqaio lqats SockBuf, int Flags, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufScanfVa(LqSockBuf* lqaio lqats SockBuf, int Flags, const char* Fmt, va_list Va);


LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufRead(LqSockBuf* lqaio lqats SockBuf, void* lqaout lqaopt Dest, size_t Size);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPeek(LqSockBuf* lqaio lqats SockBuf, void* lqaout lqaopt Dest, size_t Size);

LQ_IMPORTEXPORT LqFileSz LQ_CALL LqSockBufReadInStream(LqSockBuf* lqaio lqats SockBuf, LqFbuf* lqaout lqats Dest, LqFileSz Size);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* lqaio lqats SockBuf, bool IsSecondQueue);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvClear(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT void LQ_CALL LqSockBufFlush(LqSockBuf* lqaio lqats SockBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetRemoteAddr(LqSockBuf* lqain lqats SockBuf, LqConnAddr* lqaout Dest);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetLocAddr(LqSockBuf* lqain lqats SockBuf, LqConnAddr* lqaout Dest);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetRemoteAddrStr(LqSockBuf* lqain lqats SockBuf, char* lqaout Dest, size_t DestSize);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetLocAddrStr(LqSockBuf* lqain lqats SockBuf, char* lqaout Dest, size_t DestSize);

LQ_IMPORTEXPORT int LQ_CALL LqSockBufGetFd(LqSockBuf* lqain SockBuf);

LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufCloseByUserData2(void* lqain UserData2, void* lqain lqaopt WrkBoss);

LQ_IMPORTEXPORT void* LQ_CALL LqSockBufGetSsl(LqSockBuf* lqaio lqats SockBuf);

/*
	LqSockBufRcvResetCount - reset recive counter to zero
*/
LQ_IMPORTEXPORT void LQ_CALL LqSockBufRcvResetCount(LqSockBuf* lqaio lqats SockBuf);


/*
	LqSockBufRspResetCount - reset response counter to zero
*/
LQ_IMPORTEXPORT void LQ_CALL LqSockBufRspResetCount(LqSockBuf* lqaio lqats SockBuf);

/*
	LqSockBufRcvGetCount - get count receved data from last reset.
*/
LQ_IMPORTEXPORT int64_t LQ_CALL LqSockBufRcvGetCount(LqSockBuf* lqaio lqats SockBuf);

/*
	LqSockBufRspGetCount - get count responsed data from last reset.
*/
LQ_IMPORTEXPORT int64_t LQ_CALL LqSockBufRspGetCount(LqSockBuf* lqaio lqats SockBuf);

/* 
* Recursive lock for sock buffer (there is no need to use for LqSockBuf... functions, only for the atomization multiple operations)
* You can use for operate with UserData or UserData2
*  !You must call LqSockBufUnlock after using SockBuf
*/
LQ_IMPORTEXPORT void LQ_CALL LqSockBufLock(LqSockBuf* lqaio lqats SockBuf);
LQ_IMPORTEXPORT void LQ_CALL LqSockBufUnlock(LqSockBuf* lqaio lqats SockBuf);
/*
* @Proc - must return 0 - if you want continue, 2 - for close conn,  -1 - interrupt
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufEnum(void* WrkBoss, int(LQ_CALL*Proc)(void*, LqSockBuf*), void* UserData);

LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqEvntToSockBuf(void* Hdr); /* ((((LqClientHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN) ? ((LqEvntFd*)NULL) : ((LqEvntFd*)(Hdr))) */

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
LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorInterruptWork(LqSockAcceptor* lqain lqats SockAcceptor);

LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorDelete(LqSockAcceptor* lqain lqats SockAcceptor);


LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorSkip(LqSockAcceptor* lqain lqats SockAcceptor);
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockAcceptorAccept(LqSockAcceptor* lqain lqats SockAcceptor, void* lqain UserData);
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockAcceptorAcceptSsl(LqSockAcceptor* lqain lqats SockAcceptor, void* lqain UserData, void* lqain SslCtx);
LQ_IMPORTEXPORT bool LQ_CALL LqSockAcceptorSetInstanceCache(LqSockAcceptor* lqain lqats SockAcceptor, LqFche* lqain lqamrelease Cache);
LQ_IMPORTEXPORT LqFche* LQ_CALL LqSockAcceptorGetInstanceCache(LqSockAcceptor* lqain lqats SockAcceptor);

LQ_IMPORTEXPORT int LQ_CALL LqSockAcceptorGetFd(LqSockAcceptor* lqain lqats SockAcceptor);
/*
* Recursive lock for sock acceptor (there is no need to use for LqSockAcceptor... functions, only for the atomization multiple operations)
*/
LQ_IMPORTEXPORT void LQ_CALL LqSockAcceptorLock(LqSockAcceptor* lqaio lqats SockAcceptor);
LQ_IMPORTEXPORT void LQ_CALL LqSockAcceptorUnlock(LqSockAcceptor* lqaio lqats SockAcceptor);

LQ_EXTERN_C_END

#endif