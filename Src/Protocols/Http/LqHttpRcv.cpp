/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpRcv... - Functions for recive data from connection.
*/

#include "LqConn.h"
#include "LqHttpRcv.h"
#include "LqHttp.hpp"
#include "LqFileTrd.h"
#include "LqStr.hpp"

#include <stdarg.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"



//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFile(LqHttpConn* c, const char* Path, LqFileSz ReadLen, int Access, bool IsReplace, bool IsCreateSubdir) {
//    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_QER)
//        return LQHTTPRCV_ACT_ERR;
//    if(c->Query.ContentBoundary != nullptr)
//        return LQHTTPRCV_MULTIPART;
//    int Fd;
//    if(Path == nullptr)
//        Path = c->Pth->RealPath;
//    if((Fd = LqFileTrdCreate(Path, LQ_O_CREATE | LQ_O_BIN | LQ_O_WR | LQ_O_TRUNC | ((IsCreateSubdir) ? LQ_TC_SUBDIR : 0) | ((IsReplace) ? LQ_TC_REPLACE : 0), Access)) == -1)
//        return LQHTTPRCV_ERR;
//    c->Query.OutFd = Fd;
//    c->Query.PartLen = lq_min(c->Query.ContentLen, c->ReadedBodySize + ReadLen);
//    c->ActionState = LQHTTPACT_STATE_RCV_FILE;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileByFd(LqHttpConn* c, int Fd, LqFileSz ReadLen) {
//    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_QER)
//        return LQHTTPRCV_ACT_ERR;
//    if(c->Query.ContentBoundary != nullptr)
//        return LQHTTPRCV_MULTIPART;
//    c->Query.OutFd = Fd;
//    c->Query.PartLen = lq_min(c->Query.ContentLen, c->ReadedBodySize + ReadLen);
//    c->ActionState = LQHTTPACT_STATE_RCV_FILE;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvStream(LqHttpConn* c, LqFileSz ReadLen) {
//    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_QER)
//        return LQHTTPRCV_ACT_ERR;
//    if(c->Query.ContentBoundary != nullptr)
//        return LQHTTPRCV_MULTIPART;
//    LqSbufInit(&c->Query.Stream);
//    c->Query.PartLen = lq_min(c->Query.ContentLen, c->ReadedBodySize + ReadLen);
//    c->ActionState = LQHTTPACT_STATE_RCV_STREAM;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvStreamRead(LqHttpConn* c, void* Buffer, intptr_t BufferSize) {
//    if(c->ActionState != LQHTTPACT_STATE_RCV_STREAM)
//        return -1;
//    return LqSbufRead(&c->Query.Stream, Buffer, BufferSize);
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvStreamPeek(LqHttpConn* c, void* Buffer, intptr_t BufferSize) {
//    if(c->ActionState != LQHTTPACT_STATE_RCV_STREAM)
//        return -1;
//    return LqSbufPeek(&c->Query.Stream, Buffer, BufferSize);
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileCommit(LqHttpConn* c) {
//    if((c->ActionState != LQHTTPACT_STATE_MULTIPART_RCV_FILE) && (c->ActionState != LQHTTPACT_STATE_RCV_FILE))
//        return LQHTTPRCV_ACT_ERR;
//
//    char NameBuf[LQ_MAX_PATH];
//    NameBuf[0] = '\0';
//    LqFileTrdGetNameTarget(c->Query.OutFd, NameBuf, sizeof(NameBuf) - 1);
//    auto r = LqFileTrdCommit(c->Query.OutFd);
//    c->Query.OutFd = -1;
//    if(r == 1) {
//        LqHttpGetReg(c)->Cache.Update(NameBuf);
//        return LQHTTPRCV_UPDATED;
//    } else if(r == 0) {
//        LqHttpGetReg(c)->Cache.Update(NameBuf);
//        return LQHTTPRCV_CREATED;
//    }
//    return LQHTTPRCV_ERR;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileCommitToPlace(LqHttpConn* c, const char* DestPath) {
//    if((c->ActionState != LQHTTPACT_STATE_MULTIPART_RCV_FILE) && (c->ActionState != LQHTTPACT_STATE_RCV_FILE))
//        return LQHTTPRCV_ACT_ERR;
//    auto r = LqFileTrdCommitToPlace(c->Query.OutFd, DestPath);
//    c->Query.OutFd = -1;
//    if(r == 1) {
//        LqHttpGetReg(c)->Cache.Update(DestPath);
//        return LQHTTPRCV_UPDATED;
//    } else if(r == 0) {
//        LqHttpGetReg(c)->Cache.Update(DestPath);
//        return LQHTTPRCV_CREATED;
//    }
//    return LQHTTPRCV_ERR;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvFileCancel(LqHttpConn* c) {
//    if((c->ActionState != LQHTTPACT_STATE_MULTIPART_RCV_FILE) && (c->ActionState != LQHTTPACT_STATE_RCV_FILE))
//        return LQHTTPRCV_ACT_ERR;
//    auto r = LqFileTrdCancel(c->Query.OutFd);
//    c->Query.OutFd = -1;
//    if(r == 0)
//        return LQHTTPRCV_FILE_OK;
//    return LQHTTPRCV_ERR;
//}
//
//LQ_EXTERN_C int LQ_CALL LqHttpRcvFileGetTempName(const LqHttpConn* c, char* NameBuffer, size_t NameBufSize) {
//    if((c->ActionState != LQHTTPACT_STATE_MULTIPART_RCV_FILE) && (c->ActionState != LQHTTPACT_STATE_RCV_FILE))
//        return -1;
//    return LqFileTrdGetNameTemp(c->Query.OutFd, NameBuffer, NameBufSize);
//}
//
//LQ_EXTERN_C int LQ_CALL LqHttpRcvFileGetTargetName(const LqHttpConn* c, char* NameBuffer, size_t NameBufSize) {
//    if((c->ActionState != LQHTTPACT_STATE_MULTIPART_RCV_FILE) && (c->ActionState != LQHTTPACT_STATE_RCV_FILE))
//        return -1;
//    return LqFileTrdGetNameTarget(c->Query.OutFd, NameBuffer, NameBufSize);
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartSkip(LqHttpConn* c) {
//    if(c->Query.ContentBoundary == nullptr)
//        return LQHTTPRCV_NOT_MULTIPART;
//    c->ActionState = LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartHdrRecive(LqHttpConn* c) {
//    if(c->Query.ContentBoundary == nullptr)
//        return LQHTTPRCV_NOT_MULTIPART;
//    c->ActionState = LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartHdrRemoveLast(LqHttpConn* c) {
//    if(c->Query.ContentBoundary == nullptr)
//        return LQHTTPRCV_NOT_MULTIPART;
//    auto *Cur = &c->Query.MultipartHeaders;
//    if(*Cur != nullptr) {
//        while(true) {
//            if(Cur[0]->Query.MultipartHeaders == nullptr) {
//                free(*Cur);
//                *Cur = nullptr;
//                break;
//            }
//            Cur = &Cur[0]->Query.MultipartHeaders;
//        }
//    }
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C size_t LQ_CALL LqHttpRcvMultipartHdrGetDeep(const LqHttpConn* c) {
//    size_t Result = 0;
//    auto q = &c->Query;
//    for(; q->MultipartHeaders != nullptr; q = &q->MultipartHeaders->Query)
//        Result++;
//    return Result;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartHdrRemoveAll(LqHttpConn* c) {
//    if(c->Query.ContentBoundary == nullptr)
//        return LQHTTPRCV_NOT_MULTIPART;
//    if(auto Cur = c->Query.MultipartHeaders) {
//        auto Next = Cur->Query.MultipartHeaders;
//        while(true) {
//            free(Cur);
//            if((Cur = Next) == nullptr)
//                break;
//            Next = Cur->Query.MultipartHeaders;
//        }
//        c->Query.MultipartHeaders = nullptr;
//    }
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartInFile(LqHttpConn* c, const char* DestPath, int Access, bool IsCreateSubdir, bool IsReplace) {
//    if(c->Query.ContentBoundary == nullptr)
//        return LQHTTPRCV_NOT_MULTIPART;
//    int Fd;
//    if((Fd = LqFileTrdCreate(DestPath, LQ_O_CREATE | LQ_O_BIN | LQ_O_WR | LQ_O_TRUNC | ((IsCreateSubdir) ? LQ_TC_SUBDIR : 0) | ((IsReplace) ? LQ_TC_REPLACE : 0), Access)) == -1)
//        return LQHTTPRCV_ERR;
//    c->Query.OutFd = Fd;
//    c->ActionState = LQHTTPACT_STATE_MULTIPART_RCV_FILE;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C LqHttpRcvFileResultEnm LQ_CALL LqHttpRcvMultipartInStream(LqHttpConn* c, LqFileSz ReadLen) {
//    if(c->Query.ContentBoundary == nullptr)
//        return LQHTTPRCV_NOT_MULTIPART;
//    LqSbufInit(&c->Query.Stream);
//    c->ActionState = LQHTTPACT_STATE_MULTIPART_RCV_STREAM;
//    c->ActionResult = LQHTTPACT_RES_BEGIN;
//    return LQHTTPRCV_FILE_OK;
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvHdrSearchEx(const LqHttpConn* c, size_t Deep, const char* HeaderName, size_t HeaderNameLen, char** HeaderNameResult, char** HeaderValResult, char** HeaderValEnd) {
//    char* Headers = c->Buf;
//    auto mph = c->Query.MultipartHeaders;
//    for(size_t i = 0; (mph != nullptr) && (i < Deep); mph = mph->Query.MultipartHeaders)
//        Headers = mph->Buf;
//    char* i = Headers;
//    while(true)
//    {
//        if((*i == '\r') && (i[1] == '\n'))
//            i += 2;
//        else if(*i == ' ')
//            i++;
//        else
//            break;
//    }
//    if((i[0] == 'H') && (i[1] == 'T') && (i[2] == 'T') && (i[3] == 'P') && (i[4] == '/'))
//        i += 4;
//    else
//        goto lblCheckName;
//
//    for(; *i != '\0'; i++)
//    {
//        if((*i == '\r') && (i[1] == '\n'))
//        {
//            i += 2;
//            if((*i == '\r') && (i[1] == '\n'))
//            {
//                return -1;
//            } else
//            {
//lblCheckName:
//                for(; *i == ' '; i++);
//                if(LqStrUtf8CmpCaseLen(HeaderName, i, HeaderNameLen) && ((i[HeaderNameLen] == ':') || (i[HeaderNameLen] == ' ')))
//                {
//                    intptr_t Res = i - Headers;
//                    if(HeaderNameResult != nullptr) *HeaderNameResult = i;
//                    i += HeaderNameLen;
//                    if(HeaderValResult != nullptr)
//                    {
//                        for(; ((*i == ' ') || (*i == ':')) && (*i != '0'); i++);
//                        *HeaderValResult = i;
//                    }
//                    if(HeaderValEnd != nullptr)
//                    {
//                        for(; !((*i == '\r') && (i[1] == '\n')) && (*i != '\0'); i++);
//                        *HeaderValEnd = i;
//                    }
//                    return Res;
//                }
//            }
//        }
//    }
//    return -1;
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvHdrSearch(const LqHttpConn* c, size_t Deep, const char* HeaderName, char** HeaderNameResult, char** HeaderValResult, char** HeaderValEnd) {
//    return LqHttpRcvHdrSearchEx(c, Deep, HeaderName, LqStrLen(HeaderName), HeaderNameResult, HeaderValResult, HeaderValEnd);
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvHdrScanf(const LqHttpConn* c, size_t Deep, const char* HeaderName, const char* Format, ...) {
//    va_list Va;
//    va_start(Va, Format);
//    intptr_t Res = LqHttpRcvHdrScanfVa(c, Deep, HeaderName, Format, Va);
//	va_end(Va);
//	return Res;
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvHdrScanfVa(const LqHttpConn* c, size_t Deep, const char* HeaderName, const char* Format, va_list Va) {
//    char* StartVal = "", *EndVal = StartVal;
//    if(LqHttpRcvHdrSearchEx(c, Deep, HeaderName, LqStrLen(HeaderName), nullptr, &StartVal, &EndVal) == -1)
//        return -1;
//    auto t = *EndVal;
//    *EndVal = '\0';
//    auto r = LqFbuf_snscanf(StartVal, EndVal - StartVal, Format, Va);
//    *EndVal = t;
//    return r;
//}
//
//LQ_EXTERN_C intptr_t LQ_CALL LqHttpRcvHdrEnum(const LqHttpConn* c, char** HeaderNameResult, char** HeaderNameResultEnd, char** HeaderValResult, char** HeaderValEnd) {
//    return LqHttpConnHdrEnm(false, c->Buf, c->Query.HeadersEnd, HeaderNameResult, HeaderNameResultEnd, HeaderValResult, HeaderValEnd);
//}
