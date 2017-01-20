/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSockBuf... - 
*/

#ifndef __LQ_SOCK_BUF_HAS_INCLUDED_H__
#define __LQ_SOCK_BUF_HAS_INCLUDED_H__

#include "LqOs.h"
#include "LqErr.h"
#include "LqSbuf.h"
#include "LqDef.h"
#include "LqConn.h"


#define  LQSOCKBUF_ERR_INPUT 1
#define  LQSOCKBUF_ERR_OUTPUT 2
#define  LQSOCKBUF_ERR_SOCKET 3

#define  LQSOCKBUF_FLAG_USED ((unsigned char)1)
#define  LQSOCKBUF_FLAG_WORK ((unsigned char)2)

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

    void(*ErrHandler)(LqSockBuf* Buf, int Err);
    void(*CloseHandler)(LqSockBuf* Buf);
    
    unsigned char Flags;
    void* UserData;
};

typedef struct LqSockBuf LqSockBuf;

#pragma pack(pop)

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockBufCreate(int SockFd, void* UserData);
LQ_IMPORTEXPORT LqSockBuf* LQ_CALL LqSockBufCreateSsl(int SockFd, void* SslCtx, bool IsAccept, void* UserData);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufDelete(LqSockBuf* SockBuf);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufGoWork(LqSockBuf* SockBuf, void* WrkBoss);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFile(LqSockBuf* SockBuf, const char* Path);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFilePart(LqSockBuf* SockBuf, const char* Path, LqFileSz OffsetInFile, LqFileSz Count);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspStream(LqSockBuf* SockBuf, LqFbuf* File);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFd(LqSockBuf* SockBuf, int InFd);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspFdPart(LqSockBuf* SockBuf, int InFd, LqFileSz OffsetInFile, LqFileSz Count);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintf(LqSockBuf* SockBuf, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfVa(LqSockBuf* SockBuf, const char* Fmt, va_list Va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufWrite(LqSockBuf* SockBuf, const void* Data, size_t SizeData);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfHdr(LqSockBuf* SockBuf, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufPrintfVaHdr(LqSockBuf* SockBuf, const char* Fmt, va_list Va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSockBufWriteHdr(LqSockBuf* SockBuf, const void* Data, size_t SizeData);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspClear(LqSockBuf* SockBuf);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRspSetHdr(LqSockBuf* SockBuf);
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* SockBuf);

/*Call RcvProc, when data has been recived and match by Fmt*/
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenMatch(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf), const char* Fmt, int MatchCount, size_t MaxSize);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufScanf(LqSockBuf* SockBuf, bool IsPeek, const char* Fmt, ...);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufScanfVa(LqSockBuf* SockBuf, bool IsPeek, const char* Fmt, va_list Va);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenCompleteRead(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf), void* Dest, size_t Size);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufRead(LqSockBuf* SockBuf, void* Dest, size_t Size);


LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenCompleteStream(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf, LqFbuf* DestStream), LqFbuf* DestStream, size_t Size);
LQ_IMPORTEXPORT int LQ_CALL LqSockBufReadInStream(LqSockBuf* SockBuf, LqFbuf* Dest, size_t Size);

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufNotifyWhenRecivedData(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf));

LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* SockBuf);
LQ_IMPORTEXPORT bool LQ_CALL LqSockBufRcvClear(LqSockBuf* SockBuf);

LQ_IMPORTEXPORT void LQ_CALL LqSockBufFlush(LqSockBuf* SockBuf);

LQ_EXTERN_C_END

#endif