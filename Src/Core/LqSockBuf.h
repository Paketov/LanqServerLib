/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSockBuf... - Hi-level async socket.
*   Used queue for receive or send data.
*
*                Primitive circuit work                                                          +     Another module/process
*                                           ___________________________________________         +
*   _______    ____________________________/_________          ________________________\______________________________
*  /       \__|__     LqSockBuf #1        /          |        |       App level protocol struct |      User handlers  |
*  | LqWrk    |  |   q1 [][][][] < q2 [][] Rcv Queue |        |                                 |                     |
*  |        __|__|   [][][][][] \          Rsp Queue |        |                                 |                     |
*  |       |  |              \   \   UserData--------|------->|                                 |                     |
*  |       |  |_______________\__\___________________|        |________|________________________|_____|_______________|
*  |       |                  |  |_____________________________________|_____________________________/
*  |       |                   \______________________________________/                         +
* ...     ...                ...                                                                 +
*  |       |   ______________________________________                                           +
*  |       |__|__   LqSockBuf #n                     |                                           +
*  |          |  | [][][] < q2 <empty>    Rcv Queue  |                                          +
*  |         _|__|  <empty>               Rsp Queue  |                                           +
*  |        | |                      UserData        |                                          +
*  \__\____/  |______________________________________|                                           +
*     /
*
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

/*
    This error type passed in ErrHandler
*/
#define LQSOCKBUF_ERR_INPUT               1
#define LQSOCKBUF_ERR_OUTPUT_DATA         2
#define LQSOCKBUF_ERR_WRITE_SOCKET        4
#define LQSOCKBUF_ERR_READ_SOCKET         8
#define LQSOCKBUF_ERR_UNKNOWN_SOCKET      16

/* Flags returned by LqSockBufGetErrFlags */
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
    LqConn         Conn;      /* Data for worker */
    LqFbuf         Stream;    /* Stream for bufffering read/write from socket. Also allows you to slip additional levels of compression/encryption */

    void*          RspHeader; /* Address of response header. Element of queue can be only virtual pipe. */
    LqListHdr      Rsp;       /* Async response queue */

    LqListHdr      Rcv;       /* Front async recive queue */
    LqListHdr      Rcv2;      /* Backend async recive queue (Is used to simplify app protocol) */

    LqTimeMillisec StartTime; /* Start time (can only read)*/
    LqTimeMillisec LastExchangeTime;/* Last read/write to socket (Used for KeepAlive)*/
    LqTimeMillisec KeepAlive;   /* Keep alive time */

    void(LQ_CALL  *ErrHandler)(LqSockBuf* Buf, int Err); /* Error handler. In @Err passed type of error (LQSOCKBUF_ERR_INPUT,LQSOCKBUF_ERR_OUTPUT_DATA, ...)
                                                            (Can be read/write by user)*/
    void(LQ_CALL  *CloseHandler)(LqSockBuf* Buf); /* Called when remote side want interrupt connection, or when has been
                                                    called LqSockBufSetClose. (Can be read/write by user). */

    void*          UserData;   /* User data 1*/
    void*          UserData2;  /* User data 2 (can be used for enum) */

    LqFche*        Cache;      /* Instance of cache (Don`t touch this) */

    uint8_t        Flags;      /* Internall flags (Don`t touch this) */

    /* Data for recursive blocking (Don`t touch this)*/
    uint8_t        Lk;
    int16_t        Deep;
    volatile int   ThreadOwnerId;

    /* Internal used (Don`t touch this) */
    uint32_t       RWPortion;

    /* Counters */
    int64_t        ReadOffset;  /* Read count from last reset (For touch this use LqSockBufRcvResetCount/LqSockBufRcvGetCount) */
    int64_t        WriteOffset; /* Write count from last reset (For touch this use LqSockBufRspResetCount/LqSockBufRspGetCount) */
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
    Create socket buffer from user-defined cookie. Can be used for adding level encryption, compression or other stuffs
        @SockFd - Socket descriptor, fro event following
        @Cookie - User-defined cookie functions
        @CookieData - Data for coockie functions
        @UserData - User data
*/
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockBufCreateFromCookie(int SockFd, LqFbufCookie* lqain Cookie, void* lqain CookieData, void* lqain UserData);

/*
    Uninit and delete LqSockBuf, you can use them in handlers(SockBuf thread protected)
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufDelete(LqSockBuf* lqain lqats SockBuf);

/*
    Switch SockBuf to close mode (call CloseHandler in async mode).
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufSetClose(LqSockBuf* lqain lqats SockBuf);

/*
    Begin async works (start processing the recive/response queue by workers)
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGoWork(LqSockBuf* lqaio lqats SockBuf, void* lqain lqaopt WrkBoss);

/*
    Interrupt async work
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufInterruptWork(LqSockBuf* lqaio lqats SockBuf);

/*
    Set instance of cache to SockBuf
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufSetInstanceCache(LqSockBuf* lqaio lqats SockBuf, LqFche* lqain lqamrelease Cache);
LQ_IMPORTEXPORT LqFche* LQ_CALL LqSockBufGetInstanceCache(LqSockBuf* lqaio lqats SockBuf);

/*
    Specify the time during which the socket will not be closed without incoming/outgoing data
*/
LQ_IMPORTEXPORT void LQ_CALL LqSockBufSetKeepAlive(LqSockBuf* lqaio lqats SockBuf, LqTimeMillisec NewValue);
/*
    Get keep-alive time
*/
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqSockBufGetKeepAlive(LqSockBuf* lqain SockBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspNotifyCompletion(LqSockBuf* lqaio lqats SockBuf, void* lqain UserData, void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, void*));
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFile(LqSockBuf* lqaio lqats SockBuf, const char* lqain Path);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFilePart(LqSockBuf* lqaio lqats SockBuf, const char* lqain Path, LqFileSz OffsetInFile, LqFileSz Count);

/*
    Response stream or pointer on virt file.
        When you want send other type file, Size must be >= 0, otherwise can be -1.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFbuf(LqSockBuf* lqaio lqats SockBuf, LqFbuf* lqain lqamrelease File, LqFileSz Size);

/*
    Response data from file descriptor asynchronously
        @SockBuf - Target socket buffer
        @InFd - Descriptor from which the information will be transferred
        @return - true - is complete, false - is have error(memory for response element queue not allocate)
*/
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
      !You must call LqSockBufRspUnsetHdr() before response, because response data queue has locked on header.
    This used for apllication-level protocols (Ex HTTP)
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspSetHdr(LqSockBuf* lqaio lqats SockBuf);
/* Unset forward header */
LQ_IMPORTEXPORT void LQ_CALL LqSockBufRspUnsetHdr(LqSockBuf* lqaio lqats SockBuf);

/*
    Notify when recive queue come to this
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvNotifyCompletion(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    void(LQ_CALL*CompleteOrCancelProc)(                         /* Callback procedure. Called when complete or have error.                          */
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        void* UserData                                          /*      User data                                                                   */
    ),
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    Notifies asynchronously if a certain number of arguments are match.
    For check count match used LqFbuf_scanf
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvNotifyWhenMatch(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    void(LQ_CALL*CompleteOrCancelProc)(                         /* Callback procedure. Called when complete or have error when transmitting data.   */
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        void* UserData                                          /*      User data                                                                   */
    ),
    const char* lqain Fmt,                                      /* Format string for scan. The syntax is the same as when using LqFbuf_scanf. All arguments must have '*' */
    intptr_t MatchCount,                                        /* When this number of read arguments is reached, the callback is called            */
    size_t MaxSize,                                             /* The number of bytes at which the callback is made                                */
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    Receive data in memory region asynchronously
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvInRegion(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    void(LQ_CALL*CompleteOrCancelProc)(                         /* Callback procedure. Called when complete or have error when transmitting data.   */
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        void* Dest,                                             /*      Dest memory region                                                          */
        size_t Written,                                         /*      The number of bytes written to memory                                       */
        void* UserData                                          /*      User data                                                                   */
    ),
    void* lqaout Dest,                                          /* Start of memory region                                                           */
    size_t Size,                                                /* The size of the data being read                                                  */
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    Receive data in file buffer asynchronously
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvInFbuf(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    LqFbuf* lqaout lqaopt DestStream,                           /* Target file buffer                                                               */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    void(LQ_CALL*CompleteOrCancelProc)(                         /* Callback procedure. Called when complete or have error when transmitting data.   */
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        LqFbuf* DestFileBuffer,                                 /*      Dest file buffer                                                            */
        LqFileSz Written,                                       /*      The number of bytes written to the file buffer                              */
        void*UserData                                           /*      User data                                                                   */
    ),
    LqFileSz Size,                                              /* The size of the data being read                                                  */
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    Receive data in file buffer asynchronously. Receiving data to a specific sequence or to the maximum size.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvInFbufAboveSeq(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    LqFbuf* lqaout lqaopt DestStream,                           /* Target file buffer                                                               */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    void(LQ_CALL*CompleteOrCancelProc)(                         /* Callback procedure. Called when complete or when have error when transmitting data.*/
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        LqFbuf* DestFileBuffer,                                 /*      Dest file buffer                                                            */
        LqFileSz Written,                                       /*      The number of bytes written to the file buffer                              */
        void* UserData,                                         /*      User data                                                                   */
        bool IsFoundSeq                                         /*      Is sequence reached                                                         */
    ),
    const char* lqain ControlSeq,                               /* Sequence, above which transmission is interrupted                                */
    size_t ControlSeqSize,                                      /* Size of sequence                                                                 */
    LqFileSz MaxSize,                                           /* The maximum size, when reached, is called callback procedure                     */
    bool IsCaseIndepended,                                      /* Is used different sequence letter sizes(only for english letters)                */
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    Read as new data arrive
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvPulseRead(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    intptr_t(LQ_CALL *CompleteOrCancelProc)(                    /* Callback procedure. Called when data arrives on a socket. To stop tracking, you can return -1 */
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        void* UserData                                          /*      User data                                                                   */
    ),
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    LqSockBufRcvWaitLenData
        Notify when recived length of data in internal buffer
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvWaitLenData(
    LqSockBuf* lqaio lqats SourceSockBuf,                       /* Source socket buffer (Must be in work state)                                     */
    void* lqain UserData,                                       /* User data passed in callback proc                                                */
    void(LQ_CALL *CompleteOrCancelProc)(                        /* Callback procedure. Called when completed or when have error when fill internal buffer.*/
        LqSockBuf* SourceSockBuf,                               /*      Source buffer. When is NULL, then there was an error when reading the data  */
        void* UserData                                          /*      User data                                                                   */
    ),
    size_t TargetLen,                                           /* The size, when reached, is called callback procedure                             */ 
                                                                /* !! Be careful when set the large size of this parameter !!                       */
    bool IsSecondQueue                                          /* Add task to the second queue (Used for top level protocols)                      */
);

/*
    Get internal buffer flags
        LQSOCKBUF_FLAGS_WRITE_ERROR, LQSOCKBUF_FLAGS_WRITE_WOULD_BLOCK, 
        LQSOCKBUF_FLAGS_READ_ERROR, LQSOCKBUF_FLAGS_READ_WOULD_BLOCK
*/
LQ_IMPORTEXPORT LqSockBufErrFlags LQ_CALL LqSockBufGetErrFlags(LqSockBuf* lqaio lqats SockBuf);

/*
    Current data size in the input buffer(ex. can be used in LqSockBufRcvWaitLenData callback proc)
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufRcvBufSz(LqSockBuf* lqaio lqats SockBuf);

/*
    Current data size in the output buffer
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufRspBufSz(LqSockBuf* lqaio lqats SockBuf);

/*
    Get response size starting from heder (LqSockBufRspSetHdr/LqSockBufRspUnsetHdr) or only size header
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* lqaio lqats SockBuf, bool IsHdr);


LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufRcvQueueLen(LqSockBuf* lqaio lqats SockBuf, bool IsSecondQueue);

/*
    Try format scan from internal buffer and sock (Not async, but not blocking)
        @SockBuf - Source socket buffer
        @Flags - can be: LQFBUF_SCANF_PEEK - for only peek data(Internal buffer does not move), 
            LQFBUF_SCANF_PEEK_WHEN_ERR - Peek when have reading error (For example, if there is not enough data 
                in the buffer  (LqSockBufGetErrFlags return LQSOCKBUF_FLAGS_READ_WOULD_BLOCK))
        @return - Count scanned arguments
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufScanf(LqSockBuf* lqaio lqats SockBuf, int Flags, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufScanfVa(LqSockBuf* lqaio lqats SockBuf, int Flags, const char* Fmt, va_list Va);

/*
    Try read from internal buffer and sock (Not async, but not blocking)
        @SockBuf - Source socket buffer
        @Dest - Dest memory region. If NULL, then data only skip from internal buffer
        @Size - Size of output buffer or skip data.
        @return - Size of readed data or -1 when have error (check LqSockBufGetErrFlags)
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufRead(LqSockBuf* lqaio lqats SockBuf, void* lqaout lqaopt Dest, size_t Size);

/*
    Try read from internal buffer and sock (Not async, but not blocking), without moving internal buffer
        @SockBuf - Source socket buffer
        @Dest - Dest memory region. If NULL, then data only fill internall buffer from socket to @Size
        @Size - Size of output buffer.
        @return - Size of readed data or -1 when have error (check LqSockBufGetErrFlags)
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPeek(LqSockBuf* lqaio lqats SockBuf, void* lqaout lqaopt Dest, size_t Size);

/*
    Try read from internal buffer and sock in file buffer stream
        @SockBuf - Source socket buffer
        @Dest - Dest buffered stream
        @Size - Size of transfer data
        @return - Size of transferred data
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqSockBufReadInStream(LqSockBuf* lqaio lqats SockBuf, LqFbuf* lqaout lqats Dest, LqFileSz Size);

/*
    Cancel last recive async operation
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* lqaio lqats SockBuf, bool IsSecondQueue);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvClear(LqSockBuf* lqaio lqats SockBuf);

/* For manual send/reacv from sock buf queue (without working) */
LQ_IMPORTEXPORT void LQ_CALL LqSockBufFlush(LqSockBuf* lqaio lqats SockBuf);

/*
    Get remote IP and port
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetRemoteAddr(LqSockBuf* lqain lqats SockBuf, LqConnAddr* lqaout Dest);

/*
    Get local IP and port
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetLocAddr(LqSockBuf* lqain lqats SockBuf, LqConnAddr* lqaout Dest);

/*
    Get remote IP as string
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetRemoteAddrStr(LqSockBuf* lqain lqats SockBuf, char* lqaout Dest, size_t DestSize);

/*
    Get local IP as string
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGetLocAddrStr(LqSockBuf* lqain lqats SockBuf, char* lqaout Dest, size_t DestSize);

/*
    Get socket descriptor
*/
LQ_IMPORTEXPORT int LQ_CALL LqSockBufGetFd(LqSockBuf* lqain SockBuf);

/*
    Asynchronous closing of all socket buffer, where UserData2 the same
        @UserData2 - filter for socket buffers
        @WrkBoss - boss of workers where enums socket buffers 
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqSockBufCloseByUserData2(void* lqain UserData2, void* lqain lqaopt WrkBoss);

/*
    Get SSL instance 
        @return - NULL - when not have SSL layer
*/
LQ_IMPORTEXPORT void* LQ_CALL LqSockBufGetSsl(LqSockBuf* lqaio lqats SockBuf);

/*
    Start the SSL session
    Be careful! Do not read unintentionally the data of the SSL handshake before call this function.
        @SockBuf - Target socket buffer
        @SslCtx - SSL_CTX context
        @IsAccept - true- is accept, false- is connect (for @SslCtx)
        @return - true - is complete, false - is have error
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufBeginSslSession(LqSockBuf* lqaio lqats SockBuf, void* lqain SslCtx, bool IsAccept);

/*
    End of SSL session
        @SockBuf - Target socket buffer
        @return - true - is complete, false - is have error
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufEndSslSession(LqSockBuf* lqaio lqats SockBuf);

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

/*
    Start accepting new connections
*/
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