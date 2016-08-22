/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpRcv... - Functions for recive data from connection.
*/


#ifndef __HTTP_RECIVING_H_HAS_INCLUDED__
#define __HTTP_RECIVING_H_HAS_INCLUDED__


#include "LqHttp.h"
#include "LqOs.h"


LQ_EXTERN_C_BEGIN

enum LqHttpRcvFileResultEnm
{
    LQHTTPRCV_FILE_OK,
    LQHTTPRCV_MULTIPART,
    LQHTTPRCV_NOT_MULTIPART,
    LQHTTPRCV_NOT_HAVE_BOUNDARY,
    LQHTTPRCV_BAD_BOUDARY,
    LQHTTPRCV_ERR,
    LQHTTPRCV_UPDATED,
    LQHTTPRCV_CREATED,
    LQHTTPRCV_ACT_ERR
};

LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFile(LqHttpConn* c, const char* lqaopt lqain lqautf8 Path, LqFileSz ReadLen, int Access, bool IsReplace, bool IsCreateSubdir);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileByFd(LqHttpConn* c, int Fd, LqFileSz ReadLen);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileCommit(LqHttpConn* c);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileCommitToPlace(LqHttpConn* c, const char* DestPath);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileCancel(LqHttpConn* c);

LQ_IMPORTEXPORT int LQ_CALL LqHttpRcvFileGetTempName(const LqHttpConn* c, char* lqaout lqautf8 NameBuffer, size_t NameBufSize);
LQ_IMPORTEXPORT int LQ_CALL LqHttpRcvFileGetTargetName(const LqHttpConn* c, char* lqaout lqautf8 NameBuffer, size_t NameBufSize);

LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvStream(LqHttpConn* c, LqFileSz ReadLen);

LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartHdrRecive(LqHttpConn* c);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartSkip(LqHttpConn* c);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartHdrRemoveLast(LqHttpConn* c);

LQ_IMPORTEXPORT size_t LQ_CALL LqHttpRcvMultipartHdrGetDeep(const LqHttpConn* c);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartHdrRemoveAll(LqHttpConn* c);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartInFile(LqHttpConn* c, const char* lqain lqautf8 DestPath, int Access, bool IsCreateSubdir, bool IsReplace);
LQ_IMPORTEXPORT LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartInStream(LqHttpConn* c, LqFileSz ReadLen);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvStreamRead(LqHttpConn* c, void* lqaout Buffer, intptr_t BufferSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvStreamPeek(LqHttpConn* c, void* lqaout Buffer, intptr_t BufferSize);

/* I like C scan style */
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvHdrScanf(const LqHttpConn* c, size_t Deep /*Multipart header deep*/, const char* lqautf8 lqain HeaderName, const char* lqautf8 Format, ...);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvHdrScanfVa
(
    const LqHttpConn* lqain c,
    size_t Deep,
    const char* lqain lqautf8 HeaderName,
    const char* lqautf8 Format,
    va_list Va
);

/*
* Query functions
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvHdrSearchEx
(
    const LqHttpConn* lqain c,
    size_t Deep,
    const char* lqain lqautf8 HeaderName,
    size_t HeaderNameLen,
    char** lqaopt lqaout lqautf8 HeaderNameResult,
    char** lqaopt lqaout lqautf8 HeaderValResult,
    char** lqaopt lqaout lqautf8 HeaderValEnd
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvHdrSearch
(
    const LqHttpConn* lqain c,
    size_t Deep,
    const char* lqain lqautf8 HeaderName,
    char** lqaopt lqaout lqautf8 HeaderNameResult,
    char** lqaopt lqaout lqautf8 HeaderValResult,
    char** lqaopt lqaout lqautf8 HeaderValEnd
);



LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpRcvHdrEnum
(
    const LqHttpConn* lqain c,
    char** lqaout lqautf8 HeaderName,
    char** lqaout lqautf8 HeaderNameEnd,
    char** lqaout lqautf8 HeaderVal,
    char** lqaout lqautf8 HeaderValEnd
);

LQ_EXTERN_C_END


#endif