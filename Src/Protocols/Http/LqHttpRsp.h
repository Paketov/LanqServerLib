/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpRsp... - Functions for response data in connection.
*/


#ifndef __HTTP_RESPONSE_H_HAS_INCLUDED__
#define __HTTP_RESPONSE_H_HAS_INCLUDED__


#include "LqHttp.h"
#include "LqOs.h"
#include "LqMd5.h"

#ifdef LANQBUILD
int LqHttpRspPrintRangeBoundaryHeaders(char* Buf, size_t SizeBuf, LqHttpConn* c, LqFileSz SizeFile, LqFileSz Start, LqFileSz End, const char* MimeType);
int LqHttpRspPrintEndRangeBoundaryHeader(char* Buf, size_t SizeBuf);
#endif

LQ_EXTERN_C_BEGIN

/* return -1 is error*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpRspError(LqHttpConn* lqaio c, int Status);

/* return -1 is error*/
LQ_IMPORTEXPORT int LQ_CALL LqHttpRspStatus(LqHttpConn* lqaio c, int Status);

/* Response file automaticly by input connection (also creates headers). */
LQ_IMPORTEXPORT int LQ_CALL LqHttpRspFileAuto(LqHttpConn* lqaio c, const char* lqaopt lqain lqautf8 Path);

/* Adds a file descriptor to the connection regardless of the previous status. (Primary used by user) */
LQ_IMPORTEXPORT LqHttpActResult LQ_CALL LqHttpRspFileByFd(LqHttpConn* lqaio c, int Fd, LqFileSz OffsetStart, LqFileSz OffsetEnd);
/* Adds a file to the connection regardless of the previous status. (Primary used by user) */
LQ_IMPORTEXPORT LqHttpActResult LQ_CALL LqHttpRspFile(LqHttpConn* lqaio c, const char* lqaopt lqain lqautf8 Path, LqFileSz OffsetStart, LqFileSz OffsetEnd);

/*
*   Dynamic content functions.
*/

/*Add a little content to headers*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspHdrAddSmallContent(LqHttpConn* c, const void* Content, size_t LenContent);
/*Add a little content to headers*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspHdrPrintfContent(LqHttpConn* c, const char* FormatStr, ...);


/* Add content to stream */
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspContentWrite(LqHttpConn* c, const void* Content, size_t LenContent);
/* Add content to stream */
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspContentWritePrintf(LqHttpConn* c, const char* FormatStr, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspContentWritePrintfVa(LqHttpConn* c, const char* FormatStr, va_list Va);
/* Get size content in stream */
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqHttpRspContentGetSz(LqHttpConn* c);


/* Work with responsed headers */
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrAddEx(LqHttpConn* c, const char*  HeaderName, size_t HeaderNameLen, const char* HeaderVal, size_t HeaderValLen);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrAdd(LqHttpConn* c, const char* HeaderName, const char* HeaderVal);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrAddPrintfVa(LqHttpConn* c, const char* HeaderName, const char* FormatStr, va_list Va);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrAddPrintf(LqHttpConn* c, const char* HeaderName, const char* FormatStr, ...);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrChangeEx(LqHttpConn* c, const char* HeaderName, size_t HeaderNameLen, const char* HeaderVal, size_t HeaderValLen);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrChange(LqHttpConn* c, const char* HeaderName, const char* HeaderVal);


LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrAddStartLine(LqHttpConn* lqaio c, uint16_t StatusCode);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpRspHdrRemoveEx(LqHttpConn* lqaio c, const char* lqain HeaderName, size_t HeaderNameLen);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpRspHdrRemove(LqHttpConn* lqaio c, const char* lqain HeaderName);


LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrSetEnd(LqHttpConn* lqaio c);

LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrInsertStr(LqHttpConn* lqaio c, size_t DestOffset/*Offset in header*/, size_t DestOffsetEnd, const char* lqain Val);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrInsertStrEx(LqHttpConn* lqaio c, size_t DestOffset/*Offset in header*/, size_t DestOffsetEnd, const char* lqain Val, size_t ValLen);

/*
* @return: Start to appended block. On error return NULL.
*/
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrAppendSize(LqHttpConn* lqaio c, size_t Size);
/*
* @return: Start to all block. On error return NULL.
*/
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrResize(LqHttpConn* lqaio c, size_t Size);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpRspHdrSizeDecrease(LqHttpConn* lqaio c, size_t CountDecreese);

#define LqHttpRspHdrGetStart(Conn) ((c->Response.HeadersStart < c->Response.HeadersEnd)? (c->Buf + c->Response.HeadersStart) : nullptr)

/*
* Return -1 if not found header
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspHdrSearchEx
(
    const LqHttpConn* lqain c,
    const char* lqain HeaderName,
    size_t HeaderNameLen,
    char** lqaopt lqaout HeaderNameResult,
    char** lqaopt lqaout HeaderValResult,
    char** lqaopt lqaout HederValEnd
);

/*
* Return -1 if not found header, otherwise return offset start hedaer;
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspHdrSearch
(
    const LqHttpConn* lqain lqatns c,
    const char* lqain HeaderName,
    char** lqaopt lqaout HeaderNameResult,
    char** lqaopt lqaout HeaderValResult,
    char** lqaopt lqaout HederValEnd
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRspHdrEnum
(
    const LqHttpConn* lqain lqatns c,
    char** lqaout HeaderNameResult,
    char** lqaout HeaderNameResultEnd,
    char** lqaout HeaderValResult,
    char** lqaout HeaderValEnd
);

/*
* If not set @FileName then taken from connection.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqHttpRspGetFileMd5(LqHttpConn* lqain c, const char* lqain lqautf8 lqaopt FileName, LqMd5* lqaout DestMd5);

/*
* If not set @FileName then taken from connection.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqHttpRspGetSbufMd5(LqHttpConn* lqain c, LqSbuf* lqain lqaopt StreamBuf, LqMd5* lqaout DestMd5);

LQ_EXTERN_C_END



#endif