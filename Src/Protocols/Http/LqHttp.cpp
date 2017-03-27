/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpCore... - Main handlers of HTTP protocol.
*/

#include "LqOs.h"
#include "LqConn.h"
#include "LqStrSwitch.h"
#include "LqHttp.hpp"
#include "LqAlloc.hpp"
#include "LqLog.h"
#include "LqHttpPrs.h"
#include "LqHttpPth.hpp"
#include "LqTime.h"
#include "LqHttpMdl.h"
#include "LqAtm.hpp"
#include "LqHttpMdlHandlers.h"
#include "LqFileTrd.h"
#include "LqDfltRef.hpp"
#include "LqStr.hpp"
#include "LqWrkBoss.h"
#include "LqZmbClr.h"
#include "LqShdPtr.hpp"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

typedef struct _LqRecvStream {
    LqFbuf DestFbuf;
    void* UserData;
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*);
} _LqRecvStream;

typedef struct _LqRecvStreamFbuf {
    void* UserData;
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*);
} _LqRecvStreamFbuf;

typedef struct _LqRcvMultipartHdrsData {
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*);
    void* UserData;
    char* Boundary;
    size_t MaxLen;
} _LqRcvMultipartHdrsData;

typedef struct _LqHttpMultipartFbufNext {
    void* UserData;
    LqFbuf* Target;
    char* Boundary;
    LqFileSz MaxLen;
    LqHttpMultipartHdrs* Hdrs;
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*);
} _LqHttpMultipartFbufNext;

typedef struct _LqHttpConnRcvRcvMultipartFileStruct {
    void* UserData;
    LqFbuf Fbuf;
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*);
} _LqHttpConnRcvRcvMultipartFileStruct;

typedef struct _LqHttpConnRcvWaitLenStruct {
    void* UserData;
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*);
}_LqHttpConnRcvWaitLenStruct;

typedef struct _LqHttpEnumConnProcData {
    void* UserData;
    LqHttp* Http;
    int(LQ_CALL* Proc)(void*, LqHttpConn*);
}_LqHttpEnumConnProcData;

static int LQ_CALL _LqHttpEnumConnProc(void* UserData, LqHttpConn* HttpConn) {
    int Res = 0;
    if(HttpConn->UserData2 == ((_LqHttpEnumConnProcData*)UserData)->Http)
        Res = ((_LqHttpEnumConnProcData*)UserData)->Proc(((_LqHttpEnumConnProcData*)UserData)->UserData, HttpConn);
    return Res;
}

static intptr_t LQ_CALL _EmptyReadProc(LqFbuf* Context, char*, size_t) {
    Context->Flags |= LQFBUF_READ_ERROR;
    return 0;
}

static intptr_t LQ_CALL _EmptyWriteProc(LqFbuf* Context, char*, size_t) {
    Context->Flags |= LQFBUF_WRITE_ERROR;
    return 0;
}

static LqFbufCookie TrdCookie = {_EmptyReadProc, _EmptyWriteProc, NULL, NULL, NULL};

static void _LqHttpConnAfterCallUserHandler(LqHttpConn* HttpConn);
static void LQ_CALL _LqHttpConnErrHandler(LqSockBuf* SockBuf, int Err);
static void LQ_CALL _LqHttpConnCloseHandler(LqSockBuf* SockBuf);
static void LQ_CALL _LqHttpConnRcvHdrProc(LqSockBuf* SockBuf, void* UserData);

static void _LqHttpDereference(LqHttp* Http) {
    LqHttpData* HttpData;
    LqSockAcceptorLock(Http);
    HttpData = LqHttpGetHttpData(Http);
    HttpData->CountPtrs--;
    if(HttpData->CountPtrs <= 0) {
        LqHttpMdlFreeAll(Http);
        LqHttpMdlFreeMain(Http);
        if(HttpData->ZmbClr)
            LqZmbClrDelete(HttpData->ZmbClr);
        LqSockAcceptorDelete(Http);
#ifdef HAVE_OPENSSL
        if(HttpData->SslCtx != NULL)
            SSL_CTX_free((SSL_CTX*)HttpData->SslCtx);
#endif
        if(HttpData->IsDeleteFlag != NULL)
            *HttpData->IsDeleteFlag = true;
        LqFastAlloc::Delete(HttpData);
    }
    LqSockAcceptorUnlock(Http);
}

static void LQ_CALL _LqHttpConnRcvFileProc(LqSockBuf* HttpConn, LqFbuf* DestStream, LqFileSz Written, void* UserData) {
    _LqRecvStream* RecvStream = (_LqRecvStream*)UserData;
    bool Commit = HttpConn != NULL;
    int CommitRes;
    LqHttpConnData* HttpConnData;
    LqHttpConnRcvResult RcvRes;

    LqFbuf_flush(DestStream);
    if(RecvStream->CompleteOrCancelProc) {
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = HttpConn;
        RcvRes.UserData = RecvStream->UserData;
        RcvRes.Written = Written;
        Commit = RecvStream->CompleteOrCancelProc(&RcvRes);
    }
    DestStream->Cookie = &TrdCookie;
    if(Commit) {
        CommitRes = LqFileTrdCommit((int)DestStream->UserData);
        if((RecvStream->CompleteOrCancelProc == NULL) && (HttpConn != NULL)) {
            if(CommitRes == 1) {
                LqHttpConnRspError(HttpConn, 201); /* Created */
            } else if(CommitRes == 0) {
                LqHttpConnRspError(HttpConn, 200); /* OK */
            } else {
                LqHttpConnRspError(HttpConn, 500); /* Internal server error */
            }
        }
    } else {
        LqFileTrdCancel((int)DestStream->UserData);
        if((RecvStream->CompleteOrCancelProc == NULL) && (HttpConn != NULL)) {
            LqHttpConnRspError(HttpConn, 500); /* Internal server error */
        }
    }
    LqFbuf_close(DestStream);
    LqFastAlloc::Delete(RecvStream);
    if(HttpConn)
        _LqHttpConnAfterCallUserHandler(HttpConn);
}

static void LQ_CALL _LqHttpConnRcvFileSeqProc(LqSockBuf* HttpConn, LqFbuf* DestStream, LqFileSz Written, void* UserData, bool IsFound) {
    _LqRecvStream* RecvStream = (_LqRecvStream*)UserData;
    bool Commit = HttpConn != NULL;
    int CommitRes;
    LqHttpConnRcvResult RcvRes;

    LqFbuf_flush(DestStream);
    if(RecvStream->CompleteOrCancelProc) {
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = HttpConn;
        RcvRes.UserData = RecvStream->UserData;
        RcvRes.Written = Written;
        RcvRes.IsFoundedSeq = IsFound;
        Commit = RecvStream->CompleteOrCancelProc(&RcvRes);
    }
    DestStream->Cookie = &TrdCookie;
    if(Commit) {
        CommitRes = LqFileTrdCommit((int)DestStream->UserData);
        if((RecvStream->CompleteOrCancelProc == NULL) && (HttpConn != NULL)) {
            if(CommitRes == 1) {
                LqHttpConnRspError(HttpConn, 201); /* Created */
            } else if(CommitRes == 0) {
                LqHttpConnRspError(HttpConn, 200); /* OK */
            } else {
                LqHttpConnRspError(HttpConn, 500); /* Internal server error */
            }
        }
    } else {
        LqFileTrdCancel((int)DestStream->UserData);
        if((RecvStream->CompleteOrCancelProc == NULL) && (HttpConn != NULL)) {
            LqHttpConnRspError(HttpConn, 500); /* Internal server error */
        }
    }
    LqFbuf_close(DestStream);
    LqFastAlloc::Delete(RecvStream);
    if(HttpConn)
        _LqHttpConnAfterCallUserHandler(HttpConn);
}

static void LQ_CALL _LqHttpConnRcvFbufProc(LqSockBuf* HttpConn, LqFbuf* DestStream, LqFileSz Written, void* UserData) {
    _LqRecvStreamFbuf* RecvStream = (_LqRecvStreamFbuf*)UserData;
    LqHttpConnRcvResult RcvRes;

    LqFbuf_flush(DestStream);
    if(RecvStream->CompleteOrCancelProc) {
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = HttpConn;
        RcvRes.UserData = RecvStream->UserData;
        RcvRes.Written = Written;
        RcvRes.TargetFbuf = DestStream;
        RecvStream->CompleteOrCancelProc(&RcvRes);
    }
    LqFastAlloc::Delete(RecvStream);
    if(HttpConn)
        _LqHttpConnAfterCallUserHandler(HttpConn);
}

static void LQ_CALL _LqHttpConnRcvFbufSeqProc(LqSockBuf* HttpConn, LqFbuf* DestStream, LqFileSz Written, void* UserData, bool IsFound) {
    _LqRecvStreamFbuf* RecvStream = (_LqRecvStreamFbuf*)UserData;
    LqHttpConnRcvResult RcvRes;

    LqFbuf_flush(DestStream);
    if(RecvStream->CompleteOrCancelProc) {
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = HttpConn;
        RcvRes.UserData = RecvStream->UserData;
        RcvRes.Written = Written;
        RcvRes.TargetFbuf = DestStream;
        RcvRes.IsFoundedSeq = IsFound;
        RecvStream->CompleteOrCancelProc(&RcvRes);
    }
    LqFastAlloc::Delete(RecvStream);
    if(HttpConn)
        _LqHttpConnAfterCallUserHandler(HttpConn);
}

static LqHttpMultipartHdrs* _LqHttpMultipartHdrsRead(LqSockBuf* HttpConn, const char* Boundary, size_t MaxLength) {
    char Buf[4096], *StartBuf, *EndBuf, *CurPos, *HdrName, *HdrVal;
    int Start, End, n, ScannedLen, ScannedLen2, AllocCountHdrs;
    LqHttpMultipartHdrs* Res;
    LqHttpRcvHdr*NewHdrs;

    if(LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "%%?*{\r\n}--%s--%*{\r\n}", Boundary) > (sizeof(Buf) - 3)) {
        lq_errno_set(EOVERFLOW);
        return NULL;
    }
    if(LqSockBufScanf(HttpConn, LQSOCKBUF_PEEK, Buf) == 2) {
        LqSockBufScanf(HttpConn, 0, Buf);
        lq_errno_set(ESPIPE);
        return NULL;
    }
    if(LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "%%?*{\r\n}--%s\r\n%n%%n%%*.%lli{^\r\n\r\n}%%n%%*{\r\n\r\n}", Boundary, &n, (long long)MaxLength) > (sizeof(Buf) - 3)) {
        lq_errno_set(EOVERFLOW);
        return NULL;
    }
    if(LqSockBufScanf(HttpConn, LQSOCKBUF_PEEK, Buf, &Start, &End) < 3) {
        lq_errno_set(EIO);
        return NULL;
    }
    if((End - Start) > (MaxLength - 2)) {
        lq_errno_set(E2BIG);
        return NULL;
    }
    LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "%%?*{\r\n}--%s%%*{\r\n}", Boundary);
    LqSockBufScanf(HttpConn, 0, Buf);

    CurPos = (char*)LqMemAlloc((End - Start) + sizeof(LqHttpMultipartHdrs) + 20);

    Res = (LqHttpMultipartHdrs*)CurPos;
    memset(Res, 0, sizeof(LqHttpMultipartHdrs));
    CurPos = (char*)(((LqHttpMultipartHdrs*)CurPos) + 1);
    EndBuf = CurPos + ((End - Start) + 20);
    AllocCountHdrs = 0;
    for(;;) {
        if(LqSockBufScanf(HttpConn, 0, "%*{\r\n}") == 1)
            break;
        ScannedLen = ScannedLen2 = 0;
        if(LqSockBufScanf(HttpConn, 0, "%?*[ \t]%n%.*[^: \r\n]%n*?[ \t]", &ScannedLen, EndBuf - CurPos, CurPos, &ScannedLen2) < 2) {
            if(Res->Hdrs)
                LqMemFree(Res->Hdrs);
            LqMemFree(Res);
            lq_errno_set(EIO);
            return NULL;
        }
        HdrName = CurPos;
        CurPos += ((ScannedLen2 - ScannedLen) + 1);
        ScannedLen = ScannedLen2 = 0;
        if(LqSockBufScanf(HttpConn, 0, ":%?*[ \t]%n%?.*{^\r\n}%n%*{\r\n}", &ScannedLen, EndBuf - CurPos, CurPos, &ScannedLen2) < 3) {
            if(Res->Hdrs)
                LqMemFree(Res->Hdrs);
            LqMemFree(Res);
            lq_errno_set(EIO);
            return NULL;
        }
        HdrVal = CurPos;
        CurPos += ((ScannedLen2 - ScannedLen) + 1);
        if(Res->CountHdrs >= AllocCountHdrs) {
            AllocCountHdrs += 8;
            if((NewHdrs = (LqHttpRcvHdr*)LqMemRealloc(Res->Hdrs, AllocCountHdrs * sizeof(LqHttpRcvHdr))) == NULL) {
                if(Res->Hdrs)
                    LqMemFree(Res->Hdrs);
                LqMemFree(Res);
                lq_errno_set(ENOMEM);
                return NULL;
            }
            Res->Hdrs = NewHdrs;
        }
        Res->Hdrs[Res->CountHdrs].Name = HdrName;
        Res->Hdrs[Res->CountHdrs].Val = HdrVal;
        Res->CountHdrs++;
    }
    return Res;
}

static void LQ_CALL _LqHttpConnRcvMultipartHdrsProc(LqSockBuf* HttpConn, void* UserData) {
    _LqRcvMultipartHdrsData* Data = (_LqRcvMultipartHdrsData*)UserData;
    LqHttpMultipartHdrs* Hdrs;
    LqHttpConnRcvResult RcvRes;

    if(HttpConn == NULL) {
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = HttpConn;
        RcvRes.UserData = Data->UserData;
        Data->CompleteOrCancelProc(&RcvRes);
    } else {
        Hdrs = _LqHttpMultipartHdrsRead(HttpConn, Data->Boundary, Data->MaxLen);
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = HttpConn;
        RcvRes.UserData = Data->UserData;
        RcvRes.IsMultipartEnd = lq_errno == ESPIPE;
        RcvRes.MultipartHdrs = Hdrs;
        Data->CompleteOrCancelProc(&RcvRes);
        if(RcvRes.MultipartHdrs != NULL) {
            LqHttpMultipartHdrsDelete(RcvRes.MultipartHdrs);
        }
    }
    free(Data->Boundary);
    LqFastAlloc::Delete(Data);
    if(HttpConn)
        _LqHttpConnAfterCallUserHandler(HttpConn);
}

static void LQ_CALL _LqHttpConnRcvFbufNextProc(LqHttpConnRcvResult* RcvRes) {
    _LqHttpMultipartFbufNext* Data = (_LqHttpMultipartFbufNext*)RcvRes->UserData;

    RcvRes->MultipartHdrs = Data->Hdrs;
    if(Data->CompleteOrCancelProc) {
        RcvRes->TargetFbuf = Data->Target;
        RcvRes->UserData = Data->UserData;
        Data->CompleteOrCancelProc(RcvRes);
    }
    if(RcvRes->MultipartHdrs != NULL) {
        LqHttpMultipartHdrsDelete(RcvRes->MultipartHdrs);
        RcvRes->MultipartHdrs = NULL;
    }
    if(Data->Boundary)
        free(Data->Boundary);
    LqFastAlloc::Delete(Data);
}

static void LQ_CALL _LqHttpConnRcvMultipartHdrsNextProc(LqHttpConnRcvResult* RcvRes) {
    _LqHttpMultipartFbufNext* Data = (_LqHttpMultipartFbufNext*)RcvRes->UserData;
    if((RcvRes->HttpConn == NULL) || (RcvRes->MultipartHdrs == NULL)) {
        if(Data->CompleteOrCancelProc) {
            RcvRes->TargetFbuf = Data->Target;
            RcvRes->UserData = Data->UserData;
            Data->CompleteOrCancelProc(RcvRes);
        }
        goto lblErr;
    } else {
        Data->Hdrs = RcvRes->MultipartHdrs;
        if(!LqHttpConnRcvFbufAboveBoundary(RcvRes->HttpConn, Data->Target, _LqHttpConnRcvFbufNextProc, Data, Data->Boundary, Data->MaxLen)) {
            RcvRes->UserData = Data->UserData;
            RcvRes->TargetFbuf = Data->Target;
            if(Data->CompleteOrCancelProc) {
                Data->CompleteOrCancelProc(RcvRes);
            }
            goto lblErr;
        }
        RcvRes->MultipartHdrs = NULL;
    }
    return;
lblErr:
    if(Data->Boundary)
        free(Data->Boundary);
    LqFastAlloc::Delete(Data);
}

static void LQ_CALL _LqHttpConnRcvMultipartFileNextProc(LqHttpConnRcvResult* RcvRes) {
    _LqHttpConnRcvRcvMultipartFileStruct* Data = (_LqHttpConnRcvRcvMultipartFileStruct*)RcvRes->UserData;
    bool Commit = (RcvRes->HttpConn != NULL) && (RcvRes->IsFoundedSeq) && (RcvRes->MultipartHdrs != NULL);
    int CommitRes;
    LqHttpConnData* HttpConnData;

    LqFbuf_flush(&Data->Fbuf);
    if(Data->CompleteOrCancelProc) {
        RcvRes->TargetFbuf = NULL;
        RcvRes->UserData = Data->UserData;
        Commit = Data->CompleteOrCancelProc(RcvRes);
    }
    Data->Fbuf.Cookie = &TrdCookie;
    if(Commit) {
        CommitRes = LqFileTrdCommit((int)Data->Fbuf.UserData);
        if((Data->CompleteOrCancelProc == NULL) && (RcvRes->HttpConn != NULL)) {
            if(CommitRes == 1) {
                LqHttpConnRspError(RcvRes->HttpConn, 201); /* Created */
            } else if(CommitRes == 0) {
                LqHttpConnRspError(RcvRes->HttpConn, 200); /* OK */
            } else {
                LqHttpConnRspError(RcvRes->HttpConn, 500); /* Internal server error */
            }
        }
    } else {
        LqFileTrdCancel((int)Data->Fbuf.UserData);
        if((Data->CompleteOrCancelProc == NULL) && (RcvRes->HttpConn != NULL)) {
            LqHttpConnRspError(RcvRes->HttpConn, 500); /* Internal server error */
        }
    }
    LqFbuf_close(&Data->Fbuf);
    LqFastAlloc::Delete(Data);
}

static void LQ_CALL _LqHttpConnRcvWaitLenProc(LqSockBuf* SockBuf, void* UserData) {
    _LqHttpConnRcvWaitLenStruct* Data = (_LqHttpConnRcvWaitLenStruct*)UserData;
    LqHttpConnRcvResult RcvRes;
    if(Data->CompleteOrCancelProc) {
        memset(&RcvRes, 0, sizeof(RcvRes));
        RcvRes.HttpConn = SockBuf;
        RcvRes.UserData = Data->UserData;
        Data->CompleteOrCancelProc(&RcvRes);
    }
    LqFastAlloc::Delete(Data);
    if(SockBuf)
        _LqHttpConnAfterCallUserHandler(SockBuf);
}

static void LQ_CALL _LqHttpAcceptProc(LqSockAcceptor* Acceptor) {
    LqSockBuf* SockBuf;
    LqHttpConnData* HttpConnData;
    LqHttpData* HttpData;
    size_t MaxHdrLen;
    char Buf[1024];
    HttpData = LqHttpGetHttpData(Acceptor);

    if(HttpData->CountPtrs > HttpData->MaxCountConn) {
        LqSockAcceptorSkip(Acceptor);
        return;
    }
    HttpConnData = LqFastAlloc::New<LqHttpConnData>();
    if(HttpData->SslCtx != NULL) {
        SockBuf = LqSockAcceptorAcceptSsl(Acceptor, HttpConnData, HttpData->SslCtx);
    } else {
        SockBuf = LqSockAcceptorAccept(Acceptor, HttpConnData);
    }
    if(SockBuf == NULL) {
        LqFastAlloc::Delete(HttpConnData);
        return;
    }
    memset(HttpConnData, 0, sizeof(HttpConnData));
    SockBuf->CloseHandler = _LqHttpConnCloseHandler;
    SockBuf->ErrHandler = _LqHttpConnErrHandler;

    SockBuf->UserData2 = Acceptor;
    LqSockAcceptorLock(Acceptor);
    HttpData->CountPtrs++;
    MaxHdrLen = HttpData->MaxHeadersSize;
    LqSockAcceptorUnlock(Acceptor);

    HttpConnData->LenRspConveyor = HttpData->MaxLenRspConveyor;
    HttpConnData->Flags = 0;
    LqSockBufSetKeepAlive(SockBuf, HttpData->KeepAlive);
    //LqSockBufSetAutoHdr(SockBuf, true);

    LqSockBufGoWork(SockBuf, HttpData->WrkBoss);
    HttpData->ConnectHndls.Call(SockBuf);
    LqFbuf_snprintf(Buf, sizeof(Buf) - 2, "%%*.%zu{^\r\n\r\n}", MaxHdrLen + ((size_t)4));
    LqSockBufRcvNotifyWhenMatch(SockBuf, (void*)MaxHdrLen, _LqHttpConnRcvHdrProc, Buf, 1, MaxHdrLen, false);
}

static void LQ_CALL _LqHttpCloseHandler(LqSockAcceptor* Acceptor) {
    int Fd;
    int Errno = 0;
    socklen_t Len;
    LqSockAcceptorLock(Acceptor);
    Fd = LqSockAcceptorGetFd(Acceptor);
    Len = sizeof(Errno);
    getsockopt(Fd, SOL_SOCKET, SO_ERROR, (char*)&Errno, &Len);
    LqLogErr("LqHttp: Src %s:%i; _LqHttpCloseHandler called. Accept process has been interrupted. Error: %s", __FILE__, __LINE__, strerror(Errno));
    LqSockAcceptorUnlock(Acceptor);

}

static void LQ_CALL _LqHttpConnErrHandler(LqSockBuf* SockBuf, int Err) {
    LqSockBufSetClose(SockBuf);
}

static void _LqHttpConnRcvHdrRemove(LqHttpConn* HttpConn) {
    LqHttpConnData* HttpConnData;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(HttpConnData->RcvHdr != NULL) {
        if(HttpConnData->RcvHdr->Hdrs != NULL) {
            LqMemFree(HttpConnData->RcvHdr->Hdrs);
        }
        LqMemFree(HttpConnData->RcvHdr);
        HttpConnData->RcvHdr = NULL;
    }
    LqSockBufUnlock(HttpConn);
}

static void LQ_CALL _LqHttpConnCloseHandler(LqSockBuf* SockBuf) {
    LqHttpConnData* HttpConnData;
    LqHttpData* HttpData;
    LqHttp* Http;

    LqSockBufLock(SockBuf);
    HttpConnData = LqHttpConnGetData(SockBuf);
    HttpData = LqHttpConnGetHttpData(SockBuf);
    Http = LqHttpConnGetHttp(SockBuf);
    HttpData->DisconnectHndls.Call(SockBuf);

    if(HttpConnData->LongPollCloseHandler) {
        HttpConnData->Flags |= LQHTTPCONN_FLAG_IN_LONG_POLL_CLOSE_HANDLER;
        LqSockBufUnlock(SockBuf);
        HttpConnData->LongPollCloseHandler(SockBuf);
        LqSockBufLock(SockBuf);
        HttpConnData->Flags &= ~LQHTTPCONN_FLAG_IN_LONG_POLL_CLOSE_HANDLER;
        HttpConnData->Flags |= LQHTTPCONN_FLAG_MUST_DELETE;
        if(HttpConnData->Flags & LQHTTPCONN_FLAG_LONG_POLL_OP) {
            LqSockBufUnlock(SockBuf);
            return;
        }
    }
    
    if(HttpConnData->RspFileName) {
        free(HttpConnData->RspFileName);
        HttpConnData->RspFileName = NULL;
    }
    if(HttpConnData->BoundaryOrContentRangeOrLocation) {
        free(HttpConnData->BoundaryOrContentRangeOrLocation);
        HttpConnData->BoundaryOrContentRangeOrLocation = NULL;
    }
    if(HttpConnData->AdditionalHdrs) {
        free(HttpConnData->AdditionalHdrs);
        HttpConnData->AdditionalHdrs = NULL;
    }
    if(HttpConnData->UserData != NULL) {
        LqFastAlloc::ReallocCount<LqHttpUserData>(HttpConnData->UserData, HttpConnData->UserDataCount, 0);
        HttpConnData->UserDataCount = 0;
        HttpConnData->UserData = NULL;
    }
    _LqHttpConnRcvHdrRemove(SockBuf);
    LqHttpConnPthRemove(SockBuf);
    LqSockBufDelete(SockBuf);
    LqFastAlloc::Delete(HttpConnData);
    LqSockBufUnlock(SockBuf);
    _LqHttpDereference(Http);
}

static void LQ_CALL _LqHttpConnRcvHdrProc(LqSockBuf* SockBuf, void* UserData) {
    LqHttpConnData* HttpConnData;
    LqHttpRcvHdrs* Hdrs;
    LqHttpRcvHdr* NewHdrs;
    LqHttpMdl* Mdl;
    LqHttpData* HttpData;
    size_t HdrLen;
    char Buf[1024];
    long long TempLength;
    int LenHdr, ScannedLen, ScannedLen2, LenBuffer, NewSize, AllocCountHdrs = 0;
    char *CurPos, *EndBuf, *NewEnd, *Buffer2, *Name, *Val,
         *HdrName, *HdrVal, *HostStart, *HostEnd, *PortStart, *PortEnd, HostType;
    if(SockBuf == NULL)
        return;
    HttpConnData = LqHttpConnGetData(SockBuf);
    HttpData = LqHttpConnGetHttpData(SockBuf);
    if(HttpConnData->Flags & LQHTTPCONN_FLAG_CLOSE)
        return;
    LenHdr = -1;
    HttpConnData->Flags = 0;
    HttpConnData->LenRspConveyor--;
    if(HttpConnData->LenRspConveyor <= 0)
        HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
    HttpConnData->Flags &= ~LQHTTPCONN_FLAG_AFTER_USER_HANDLER_CALLED;
    LqSockBufRspSetHdr(SockBuf);
    LqHttpConnPthRemove(SockBuf);
    if(HttpConnData->RspFileName) {
        free(HttpConnData->RspFileName);
        HttpConnData->RspFileName = NULL;
    }
    if(HttpConnData->BoundaryOrContentRangeOrLocation) {
        free(HttpConnData->BoundaryOrContentRangeOrLocation);
        HttpConnData->BoundaryOrContentRangeOrLocation = NULL;
    }
    if(HttpConnData->AdditionalHdrs) {
        free(HttpConnData->AdditionalHdrs);
        HttpConnData->AdditionalHdrs = NULL;
        HttpConnData->AdditionalHdrsLen = 0;
    }
    if(HttpConnData->UserData != NULL) {
        LqFastAlloc::ReallocCount<LqHttpUserData>(HttpConnData->UserData, HttpConnData->UserDataCount, 0);
        HttpConnData->UserDataCount = 0;
        HttpConnData->UserData = NULL;
    }
    _LqHttpConnRcvHdrRemove(SockBuf);
    HttpConnData->RspStatus = 200;
    HttpConnData->ContentLength = -((LqFileSz)1);
    HdrLen = (size_t)UserData;
    LqFbuf_snprintf(Buf, sizeof(Buf) - 2, "%%*.%zu{^\r\n\r\n}%%*{\r\n\r\n}%%n", HdrLen);
    if((LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK, Buf, &LenHdr) < 2) || (LenHdr == -1)) {
        HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
        LqHttpConnRspError(SockBuf, 400);
        goto lblOut;
    }
    LenBuffer = LenHdr + (sizeof(LqHttpRcvHdrs) + 4);
    Buffer2 = CurPos = (char*)LqMemAlloc(LenBuffer);
    if(CurPos == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
        LqHttpConnRspError(SockBuf, 500);
        goto lblOut;
    }
    Hdrs = (LqHttpRcvHdrs*)CurPos;
    memset(Hdrs, 0, sizeof(LqHttpRcvHdrs));
    EndBuf = Buffer2 + LenBuffer;
    CurPos += (sizeof(LqHttpRcvHdrs) + 1);
    if(LqSockBufScanf(SockBuf, 0, "%.*[^ \r\n\t]%n%*[ \t]", EndBuf - CurPos, CurPos, &ScannedLen) < 2) {/* Read method*/
        HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
        LqMemFree(Buffer2);
        LqHttpConnRspError(SockBuf, 405);
        goto lblOut;
    }
    Hdrs->Method = CurPos;
    CurPos += ScannedLen + 1;
    if(LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "%.*[a-zA-Z]%n%*{://}", EndBuf - CurPos, CurPos, &ScannedLen) == 2) {
        Hdrs->Scheme = CurPos;
        CurPos += ScannedLen + 1;
    }
    if(LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "%.*[^/@: \n\r\t]%*{@}", EndBuf - CurPos, CurPos) == 2) {
        NewEnd = LqHttpPrsEscapeDecodeSz(CurPos, CurPos);
        *NewEnd = '\0';
        Hdrs->UserInfo = CurPos;
        CurPos = (NewEnd + 1);
    }

    if((LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "[%.*[^]/ \r\n\t]%*{]}", EndBuf - CurPos, CurPos) == 2) ||
       (LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "%.*[^/: \r\n\t]", EndBuf - CurPos, CurPos) == 1)) {
        NewEnd = LqHttpPrsEscapeDecodeSz(CurPos, CurPos);
        *NewEnd = '\0';
        Hdrs->Host = CurPos;
        CurPos = (NewEnd + 1);
    }
    if(LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, ":%.*[0-9]", EndBuf - CurPos, CurPos) == 1) {
        Hdrs->Port = CurPos;
        CurPos = (NewEnd + 1);
    }
    if((LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK, "%*{/}") == 1) && (LqSockBufScanf(SockBuf, 0, "%.*[^?# \r\n]", EndBuf - CurPos, CurPos) == 1)) {
        NewEnd = LqHttpPrsEscapeDecodeSz(CurPos, CurPos);
        *NewEnd = '\0';
        Hdrs->Path = CurPos;
        CurPos = (NewEnd + 1);
    } else {
        LqMemFree(Buffer2);
        HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
        LqHttpConnRspError(SockBuf, 400);
        goto lblOut;
    }
    if(LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "?%.*[^# \r\n]", EndBuf - CurPos, CurPos) == 1) {
        NewEnd = LqHttpPrsEscapeDecodeSz(CurPos, CurPos);
        *NewEnd = '\0';
        Hdrs->Args = CurPos;
        CurPos = (NewEnd + 1);
    }
    if(LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "#%.*[^ \r\n]", EndBuf - CurPos, CurPos) == 1) {
        NewEnd = LqHttpPrsEscapeDecodeSz(CurPos, CurPos);
        *NewEnd = '\0';
        Hdrs->Fragment = CurPos;
        CurPos = (NewEnd + 1);
    }
    LqSockBufScanf(SockBuf, LQSOCKBUF_PEEK_WHEN_ERR, "%*[ \t]HTTP/%q8u.%q8u", &Hdrs->MajorVer, &Hdrs->MinorVer);
    LqSockBufScanf(SockBuf, 0, "%?*{^\r\n}%*{\r\n}");
    HdrName = CurPos;
    
    for(;;) {
        if(LqSockBufScanf(SockBuf, 0, "%*{\r\n}") == 1)
            break;

        ScannedLen = ScannedLen2 = 0;
        if(LqSockBufScanf(SockBuf, 0, "%?*[ \t]%n%.*[^: \r\n]%n*?[ \t]", &ScannedLen, EndBuf - CurPos,  CurPos, &ScannedLen2) < 2) {
            if(Hdrs->Hdrs)
                LqMemFree(Hdrs->Hdrs);
            LqMemFree(Buffer2);
            HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
            LqHttpConnRspError(SockBuf, 400);
            goto lblOut;
        }
        HdrName = CurPos;
        CurPos += ((ScannedLen2 - ScannedLen) + 1);
        ScannedLen = ScannedLen2 = 0;
        if(LqSockBufScanf(SockBuf, 0, ":%?*[ \t]%n%?.*{^\r\n}%n%*{\r\n}", &ScannedLen, EndBuf - CurPos, CurPos, &ScannedLen2) < 3) {
            if(Hdrs->Hdrs)
                LqMemFree(Hdrs->Hdrs);
            LqMemFree(Buffer2);
            HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
            LqHttpConnRspError(SockBuf, 400);
            goto lblOut;
        }
        HdrVal = CurPos;
        CurPos += ((ScannedLen2 - ScannedLen) + 1);
        if(Hdrs->CountHdrs >= AllocCountHdrs) {
            AllocCountHdrs += 8;
            if((NewHdrs = (LqHttpRcvHdr*)LqMemRealloc(Hdrs->Hdrs, AllocCountHdrs * sizeof(LqHttpRcvHdr))) == NULL) {
                if(Hdrs->Hdrs)
                    LqMemFree(Hdrs->Hdrs);
                LqMemFree(Buffer2);
                HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
                LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
                LqHttpConnRspError(SockBuf, 500);
                goto lblOut;
            }
            Hdrs->Hdrs = NewHdrs;
        }
        Hdrs->Hdrs[Hdrs->CountHdrs].Name = HdrName;
        Hdrs->Hdrs[Hdrs->CountHdrs].Val = HdrVal;
        if(LqStrUtf8CmpCase(HdrName, "host")) {
            Hdrs->Host = HdrVal;
        } else if(LqStrUtf8CmpCase(HdrName, "connection")) {
            if(LqStrUtf8CmpCaseLen(HdrVal, "close", sizeof("close") - 1))
                HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
        } else if(LqStrUtf8CmpCase(HdrName, "content-length")) {
            TempLength = 0ll;
            LqFbuf_snscanf(HdrVal, 100, "%?*[ \t]%lli", &TempLength);
            if(TempLength >= 0ll)
                HttpConnData->ContentLength = TempLength;
        }
        Hdrs->CountHdrs++;
    }
    LqSockBufRcvResetCount(SockBuf);
    HttpConnData->RcvHdr = Hdrs;
    LqHttpPthRecognize(SockBuf);
    if(HttpConnData->Pth == NULL) {
        LqHttpConnRspError(SockBuf, 404);
        goto lblOut;
    }
    Mdl = LqHttpConnGetMdl(SockBuf);
    HttpData->StartQueryHndls.Call(SockBuf);
    Mdl->MethodHandlerProc(SockBuf);
lblOut:
    _LqHttpConnAfterCallUserHandler(SockBuf);
    return;
}

static void LQ_CALL _LqHttpConnProcForClose(LqSockBuf* Buf, void*) {
    if(Buf == NULL)
        return;
    LqSockBufSetClose(Buf);
}

static void LQ_CALL _LongPollCloseHandler(LqHttpConn* HttpConn) {
}

static void _LqHttpConnAfterCallUserHandler(LqHttpConn* HttpConn) {
    LqFileSz HdrLen, RspLen, ContentLengthLeft;
    size_t RcvQueueLen;
    LqHttpConnData* HttpConnData;
    LqHttpData* HttpData;
    LqHttpMdl* Mdl;
    char ServerNameBuf[1024];
    char MimeBuf[1024];
    char CacheControl[1024];
    char Etag[1024];
    char AllowBuf[1024];
    char Buf[1024];
    uint32_t AdditionalHdrsHave = 0;
    LqTimeSec LastModif = -1ll;
    LqTimeSec Expires = -1ll;
    const char *StatusMsg;
    char *c, *sc, *ec;
    tm ctm;
    int RspStatus;
    int64_t i64;
    size_t MaxHdrLen;

    LqSockBufLock(HttpConn);
    Mdl = LqHttpConnGetMdl(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    HttpData = LqHttpConnGetHttpData(HttpConn);
    HdrLen = LqSockBufRspLen(HttpConn, true);
    RspLen = LqSockBufRspLen(HttpConn, false);
    RcvQueueLen = LqSockBufRcvQueueLen(HttpConn, false);
    RspStatus = HttpConnData->RspStatus;

    if((RcvQueueLen <= 0) && 
       !(HttpConnData->Flags & (LQHTTPCONN_FLAG_AFTER_USER_HANDLER_CALLED | LQHTTPCONN_FLAG_LONG_POLL_OP | LQHTTPCONN_FLAG_MUST_DELETE | LQHTTPCONN_FLAG_IN_LONG_POLL_CLOSE_HANDLER))) {
        HttpConnData->Flags |= LQHTTPCONN_FLAG_AFTER_USER_HANDLER_CALLED;
        if(HdrLen <= 0) {
            AllowBuf[0] = CacheControl[0] = Etag[0] = MimeBuf[0] = '\0';
            StatusMsg = LqHttpPrsGetMsgByStatus(RspStatus);
            if(HttpConnData->AdditionalHdrs) {
                c = HttpConnData->AdditionalHdrs;
                while(*c != '\0') {
                    for(; (*c == ' ') || (*c == '\t'); c++);
                    sc = c;
                    for(; (*c != ':') && (*c != '\t') && (*c != ' ') && (*c != '\r') && (*c != '\0'); c++);
                    ec = c;
                    for(; (*c != '\r') && (*c != '\0'); c++);
                    if(*c == '\r') {
                        c++;
                        if(*c == '\n')
                            c++;
                    }
                    LQSTR_SWITCH_NI(sc, ec - sc) {
                        LQSTR_CASE_I("connection") AdditionalHdrsHave |= 0x00000001; break;
                        LQSTR_CASE_I("date") AdditionalHdrsHave |= 0x00000002; break;
                        LQSTR_CASE_I("cache-control") AdditionalHdrsHave |= 0x00000004; break;
                        LQSTR_CASE_I("server") AdditionalHdrsHave |= 0x00000008; break;
                        LQSTR_CASE_I("etag") AdditionalHdrsHave |= 0x00000010; break;
                        LQSTR_CASE_I("last-modified") AdditionalHdrsHave |= 0x00000020; break;
                        LQSTR_CASE_I("expires") AdditionalHdrsHave |= 0x00000040; break;
                        LQSTR_CASE_I("allow") AdditionalHdrsHave |= 0x00000080; break;
                        LQSTR_CASE_I("content-type") AdditionalHdrsHave |= 0x00000100; break;
                        LQSTR_CASE_I("location") AdditionalHdrsHave |= 0x00000200; break;
                        LQSTR_CASE_I("content-range") AdditionalHdrsHave |= 0x00000400; break;
                        LQSTR_CASE_I("content-length") AdditionalHdrsHave |= 0x00000800; break;
                    }
                }
            }
            LqSockBufUnlock(HttpConn);

            LqTimeGetGmtTm(&ctm);
            if(!(AdditionalHdrsHave & 0x00000080) && ((RspStatus == 501) || (RspStatus == 405))) {
                Mdl->AllowProc(HttpConn, AllowBuf, sizeof(AllowBuf) - 1);
            }
            if(HttpConnData->RspFileName) {
                Mdl->GetMimeProc(HttpConnData->RspFileName, HttpConn, MimeBuf, sizeof(MimeBuf) - 1);
                Mdl->GetCacheInfoProc(HttpConnData->RspFileName, HttpConn, CacheControl, sizeof(CacheControl) - 1, Etag, sizeof(Etag) - 1, &LastModif, &Expires);
            } else if(RspStatus != 304) {
                memcpy(CacheControl, "no-cache", sizeof("no-cache"));
            }
            if((MimeBuf[0] == '\0') && (HttpConnData->Flags & LQHTTPCONN_FLAG_BIN_RSP)) {
                memcpy(MimeBuf, "application/octet-stream", sizeof("application/octet-stream"));
            }
            if(RspLen <= 0) {
                switch(RspStatus) {
                    case 200:
                    case 304:
                    case 201:
                    case 202:
                        break;
                    default:
                        LqSockBufPrintf(
                            HttpConn,
                            false,
                            "<html><head></head><body>%i %s</body></html>",
                            RspStatus,
                            StatusMsg
                        );
                        RspLen = LqSockBufRspLen(HttpConn, false);
                }
            }

            LqSockBufPrintf(
                HttpConn,
                true,
                "HTTP/%s %i %s\r\n",
                HttpData->HTTPProtoVer, RspStatus, StatusMsg
            );
            if(!(AdditionalHdrsHave & 0x00000001)) {
                LqSockBufPrintf(HttpConn, true, "Connection: %s\r\n", (HttpConnData->Flags & LQHTTPCONN_FLAG_CLOSE) ? "close" : "Keep-Alive");
            }
            if(!(AdditionalHdrsHave & 0x00000002)) {
                LqSockBufPrintf(HttpConn, true, "Date: " PRINTF_TIME_TM_FORMAT_GMT "\r\n", PRINTF_TIME_TM_ARG_GMT(ctm));
            }
            if((CacheControl[0] != '\0') && !(AdditionalHdrsHave & 0x00000004)) {
                LqSockBufPrintf(HttpConn, true, "Cache-Control: %s\r\n", CacheControl);
            }
            if(!(AdditionalHdrsHave & 0x00000008)) {
                ServerNameBuf[0] = '\0';
                Mdl->ServerNameProc(HttpConn, ServerNameBuf, sizeof(ServerNameBuf));
                if(ServerNameBuf[0] != '\0') {
                    LqSockBufPrintf(HttpConn, true, "Server: %s\r\n", ServerNameBuf);
                }
            }
            if((Etag[0] != '\0') && !(AdditionalHdrsHave & 0x00000010)) {
                LqSockBufPrintf(HttpConn, true, "ETag: %s\r\n", Etag);
            }
            if((LastModif != -1ll) && !(AdditionalHdrsHave & 0x00000020)) {
                LqTimeLocSecToGmtTm(&ctm, LastModif);
                LqSockBufPrintf(HttpConn, true, "Last-Modified: " PRINTF_TIME_TM_FORMAT_GMT "\r\n", PRINTF_TIME_TM_ARG_GMT(ctm));
            }
            if((Expires != -1ll) && !(AdditionalHdrsHave & 0x00000040)) {
                LqTimeLocSecToGmtTm(&ctm, Expires);
                LqSockBufPrintf(HttpConn, true, "Expires: " PRINTF_TIME_TM_FORMAT_GMT "\r\n", PRINTF_TIME_TM_ARG_GMT(ctm));
            }
            if(AllowBuf[0] != '\0') {
                LqSockBufPrintf(HttpConn, true, "Allow: %s\r\n", AllowBuf);
            }
            if(RspStatus != 304) {
                if(HttpConnData->BoundaryOrContentRangeOrLocation != NULL) {
                    if(HttpConnData->Flags & LQHTTPCONN_FLAG_BOUNDARY) {
                        if(!(AdditionalHdrsHave & 0x00000100))
                            LqSockBufPrintf(HttpConn, true, "Content-Type: multipart/byteranges; boundary=%s\r\n", HttpConnData->BoundaryOrContentRangeOrLocation);
                    } else if(HttpConnData->Flags & LQHTTPCONN_FLAG_LOCATION) {
                        if(!(AdditionalHdrsHave & 0x00000100))
                            LqSockBufPrintf(HttpConn, true, "Content-Type: %s\r\n", (MimeBuf[0] != '\0') ? MimeBuf : "text/html; charset=\"UTF-8\"");
                        if(!(AdditionalHdrsHave & 0x00000200))
                            LqSockBufPrintf(HttpConn, true, "Location: %s\r\n", HttpConnData->BoundaryOrContentRangeOrLocation);
                    } else {
                        if(!(AdditionalHdrsHave & 0x00000100))
                            LqSockBufPrintf(HttpConn, true, "Content-Type: %s\r\n", (MimeBuf[0] != '\0') ? MimeBuf : "text/html; charset=\"UTF-8\"");
                        if(!(AdditionalHdrsHave & 0x00000400))
                            LqSockBufPrintf(HttpConn, true, "Content-Range: bytes %s\r\n", HttpConnData->BoundaryOrContentRangeOrLocation);
                    }
                } else if(!(AdditionalHdrsHave & 0x00000100)) {
                    LqSockBufPrintf(HttpConn, true, "Content-Type: %s\r\n", (MimeBuf[0] != '\0') ? MimeBuf : "text/html; charset=\"UTF-8\"");
                }
                if(!(AdditionalHdrsHave & 0x00000800))
                    LqSockBufPrintf(HttpConn, true, "Content-Length: %lli\r\n", (long long)RspLen);
            }
            if(HttpConnData->AdditionalHdrs)
                LqSockBufPrintf(HttpConn, true, "%s", HttpConnData->AdditionalHdrs);
            LqSockBufPrintf(HttpConn, true, "\r\n");
            HdrLen = LqSockBufRspLen(HttpConn, true);
        } else {
            LqSockBufUnlock(HttpConn);
        }
        HttpData->EndResponseHndls.Call(HttpConn);
        if(HttpConnData->RspFileName) {
            free(HttpConnData->RspFileName);
            HttpConnData->RspFileName = NULL;
        }
        if(HttpConnData->BoundaryOrContentRangeOrLocation) {
            free(HttpConnData->BoundaryOrContentRangeOrLocation);
            HttpConnData->BoundaryOrContentRangeOrLocation = NULL;
        }
        if(HttpConnData->AdditionalHdrs) {
            free(HttpConnData->AdditionalHdrs);
            HttpConnData->AdditionalHdrsLen = 0;
            HttpConnData->AdditionalHdrs = NULL;
        }
        if((HttpConnData->Flags & LQHTTPCONN_FLAG_NO_BODY) && (RspLen > ((LqFileSz)0))) {
            LqSockBufRspClearAfterHdr(HttpConn);
        }
        LqHttpConnPthRemove(HttpConn);
        ContentLengthLeft = LqHttpConnRcvGetContentLengthLeft(HttpConn);
        if(ContentLengthLeft > ((LqFileSz)0)) {
            if(ContentLengthLeft > HttpData->MaxSkipContentLength) {
                RspLen = HdrLen = ((LqFileSz)1);
                HttpConnData->Flags |= LQHTTPCONN_FLAG_CLOSE;
            } else {
                LqSockBufRcvInFbuf(HttpConn, NULL, NULL, NULL, ContentLengthLeft, false);
            }
        }
        if((HttpConnData->Flags & LQHTTPCONN_FLAG_CLOSE) && ((HdrLen > ((LqFileSz)0)) || (RspLen > ((LqFileSz)0)))) {
            LqSockBufRspNotifyCompletion(HttpConn, NULL, _LqHttpConnProcForClose);
        }
        MaxHdrLen = HttpData->MaxHeadersSize;
        LqFbuf_snprintf(Buf, sizeof(Buf) - 2, "%%*.%zu{^\r\n\r\n}", MaxHdrLen + ((size_t)4));
        LqSockBufRcvNotifyWhenMatch(HttpConn, (void*)MaxHdrLen, _LqHttpConnRcvHdrProc, Buf, 1, MaxHdrLen, true);
        LqSockBufRspUnsetHdr(HttpConn);
    } else {
        LqSockBufUnlock(HttpConn);
    }
}

static int _LqHttpRspCheckMatching(char* HdrVal, char* Etag) {
    char* p = HdrVal;
    char t = 1;
    char* i;
    while(true) {
        for(; *p == ' '; p++);
        if(*p == '*') {
            p++;
        } else {
            //Compare Etag
            for(i = Etag; ; p++, i++) {
                if(*p != *i) {
                    if(*i == '\0')
                        return 0;
                    else
                        break;
                }
                if(*i == '\0')
                    return 0;
            }
        }
        for(; *p == ' '; p++);
        if(*p == ',') {
            p++;
            continue;
        } else {
            return 1;
        }
    }
}

static int _LqHttpRspCheckTimeModifyng(char* Hdr, LqTimeSec TimeModify) {
    LqTimeSec ReqGmtTime = 0;
    if((LqTimeStrToGmtSec(Hdr, &ReqGmtTime) == -1) || (TimeModify > ReqGmtTime))
        return 1;
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqHttpConnAsyncClose(LqHttpConn* HttpConn) {
    LqSockBufSetClose(HttpConn);
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspBeginLongPoll(LqHttpConn* HttpConn, void (LQ_CALL *LongPollCloseHandler)(LqHttpConn*)) {
    LqHttpConnData* HttpConnData;
    bool Res = false;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(HttpConnData->Flags & LQHTTPCONN_FLAG_LONG_POLL_OP) {
        lq_errno_set(EALREADY);
        goto lblErr;
    }
    HttpConnData->Flags |= LQHTTPCONN_FLAG_LONG_POLL_OP;
    HttpConnData->LongPollCloseHandler = ((LongPollCloseHandler != NULL)? LongPollCloseHandler: _LongPollCloseHandler);
lblErr:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspEndLongPoll(LqHttpConn* HttpConn) {
    LqHttpConnData* HttpConnData;
    bool Res = false;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(!(HttpConnData->Flags & LQHTTPCONN_FLAG_LONG_POLL_OP)) {
        lq_errno_set(ENOENT);
        goto lblErr;
    }
    HttpConnData->Flags &= ~LQHTTPCONN_FLAG_LONG_POLL_OP;
    HttpConnData->LongPollCloseHandler = NULL;
    if(HttpConnData->Flags & LQHTTPCONN_FLAG_IN_LONG_POLL_CLOSE_HANDLER) {
        Res = true;
        goto lblErr;
    }
    if(HttpConnData->Flags & LQHTTPCONN_FLAG_MUST_DELETE)
        _LqHttpConnCloseHandler(HttpConn);
    else
        _LqHttpConnAfterCallUserHandler(HttpConn);
    Res = true;
lblErr:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqHttpConnRspError(LqHttpConn* HttpConn, int Code) {
    LqHttpConnData* HttpConnData;
    LqHttpMdl* Mdl;

    LqSockBufLock(HttpConn);
    Mdl = LqHttpConnGetMdl(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    HttpConnData->RspStatus = Code;
    Mdl->RspErrorProc(HttpConn, Code);
    LqSockBufUnlock(HttpConn);
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspFile(LqHttpConn* HttpConn, const char* PathToFile) {
    LqHttpConnData* HttpConnData;
    bool Res = false;

    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(PathToFile == NULL) {
        if((PathToFile = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            lq_errno_set(ENODATA);
            goto lblOut;
        }
    }
    if(Res = LqSockBufRspFile(HttpConn, PathToFile)) {
        HttpConnData = LqHttpConnGetData(HttpConn);
        if(HttpConnData->RspFileName)
            free(HttpConnData->RspFileName);
        HttpConnData->RspFileName = LqStrDuplicate(PathToFile);
    }
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspFilePart(LqHttpConn* HttpConn, const char* PathToFile, LqFileSz OffsetInFile, LqFileSz Length) {
    LqHttpConnData* HttpConnData;
    bool Res = false;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(PathToFile == NULL) {
        if((PathToFile = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            lq_errno_set(ENODATA);
            goto lblOut;
        }
    }
    if(Res = LqSockBufRspFilePart(HttpConn, PathToFile, OffsetInFile, Length)) {
        if(HttpConnData->RspFileName)
            free(HttpConnData->RspFileName);
        HttpConnData->RspFileName = LqStrDuplicate(PathToFile);
    }
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspRedirection(LqHttpConn* HttpConn, int StatusCode, const char* Link) {
    LqHttpConnData* HttpConnData;
    bool Res = false;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if((Link == NULL) || (StatusCode == 0)) {
        if((HttpConnData->Pth != NULL) && ((HttpConnData->Pth->Type & LQHTTPPTH_TYPE_SEP) == LQHTTPPTH_TYPE_FILE_REDIRECTION)) {
            if(StatusCode == 0)
                StatusCode = HttpConnData->Pth->StatusCode;
            if(Link == NULL) 
                Link = HttpConnData->Pth->Location;
        } else {
            lq_errno_set(ENODATA);
            goto lblOut;
        }
    }
    HttpConnData->BoundaryOrContentRangeOrLocation = LqStrDuplicate(Link);
    HttpConnData->Flags |= LQHTTPCONN_FLAG_LOCATION;
    HttpConnData->RspStatus = StatusCode;
    Res = true;
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnRetrieveResponseStatus(LqHttpConn* HttpConn, const char* PathToFile) {
    LqHttpConnData* HttpConnData;
    LqHttpRcvHdrs* RcvHdr;
    LqHttpPth* Pth;
    LqHttpMdl* Mdl;
    int Res = 0;
    size_t i;
    char Etag[1024];
    LqTimeSec LastModificate = -1;
    int IfNoneMatch = -1;
    int IfMatch = -1;
    int IfModSince = -1;
    int IfUnmodSince = -1;
    int IfRange = -1;
    bool IsHaveCond = false;
    char* RangeHdr = NULL;

    Etag[0] = Etag[sizeof(Etag) - 1] = '\0';
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    Mdl = LqHttpConnGetMdl(HttpConn);
    Pth = HttpConnData->Pth;
    if(PathToFile == NULL) {
        if((PathToFile = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            Res = 404;
            goto lblOut;
        }
    }

    Mdl->GetCacheInfoProc(PathToFile, HttpConn, NULL, 0, Etag, sizeof(Etag) - 1, &LastModificate, NULL);
    if((RcvHdr = HttpConnData->RcvHdr) == NULL) {
        Res = 200;
        goto lblOut;
    }

    for(i = 0 ;i < RcvHdr->CountHdrs;i++) {
        LQSTR_SWITCH_I(RcvHdr->Hdrs[i].Name) {
            LQSTR_CASE_I("if-range") {
                if(IfRange != -1) {
                    Res = 200;
                    goto lblOut;
                }
                IfRange = _LqHttpRspCheckMatching(RcvHdr->Hdrs[i].Val, Etag);
                break;
            }
            LQSTR_CASE_I("if-none-match") {
                if(IfNoneMatch != -1) {
                    Res = 200;
                    goto lblOut;
                }
                IfNoneMatch = _LqHttpRspCheckMatching(RcvHdr->Hdrs[i].Val, Etag);
                IsHaveCond = true;
                break;
            }
            LQSTR_CASE_I("if-match") {
                if(IfMatch != -1) {
                    Res = 200;
                    goto lblOut;
                }
                IfMatch = _LqHttpRspCheckMatching(RcvHdr->Hdrs[i].Val, Etag);
                break;
            }
            LQSTR_CASE_I("if-modified-since") {
                if(IfModSince != -1) {
                    Res = 200;
                    goto lblOut;
                }
                IfModSince = _LqHttpRspCheckTimeModifyng(RcvHdr->Hdrs[i].Val, LastModificate);
                IsHaveCond = true;
                break;
            }
            LQSTR_CASE_I("if-unmodified-since") {
                if(IfUnmodSince != -1) {
                    Res = 200;
                    goto lblOut;
                }
                IfUnmodSince = _LqHttpRspCheckTimeModifyng(RcvHdr->Hdrs[i].Val, LastModificate);
                break;
            }
            LQSTR_CASE_I("range") {
                if(RangeHdr != NULL) {
                    Res = 200;
                    goto lblOut;
                }
                RangeHdr = RcvHdr->Hdrs[i].Val;
                break;
            }
        }
    }
    if((IfMatch == 1) || (IfUnmodSince == 1)) {
        Res = 412;
        goto lblOut;
    }
    if(IsHaveCond && (IfNoneMatch < 1) && (IfModSince < 1)) {
        Res = 304;
        goto lblOut;
    } else {
        if((RangeHdr != NULL) && (IfRange < 1)) {
            Res = 206;
            goto lblOut;
        }
    }
    Res = 200;
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspFileMultipart(LqHttpConn* HttpConn, const char* PathToFile, const char* Boundary, const LqFileSz* Ranges, int CountRanges) {
    LqHttpConnData* HttpConnData;
    LqHttpMdl* Mdl;
    LqFileStat FileStat;
    size_t i;
    LqFileSz MultipartRanges[20 * 2];
    const LqFileSz* CurRng;
    char MimeBuf[1024];
    bool Res = false;
    LqFileSz StartPos, EndPos;

    MimeBuf[sizeof(MimeBuf) - 1] = MimeBuf[0] = '\0';
    LqSockBufLock(HttpConn);
    Mdl = LqHttpConnGetMdl(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(PathToFile == NULL) {
        if((PathToFile = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            lq_errno_set(ENODATA);
            goto lblOut;
        }
    }
    if((Ranges == NULL) || (CountRanges < 0)) {
        CountRanges = LqHttpConnRspRetrieveMultipartRanges(HttpConn, MultipartRanges, 19);
        if(CountRanges <= 0) {
            lq_errno_set(ENOENT);
            goto lblOut;
        }
    }
    Mdl->GetMimeProc(PathToFile, HttpConn, MimeBuf, sizeof(MimeBuf) - 1);
    if(Boundary == NULL) {
        Boundary = "z3d6b6a416f9b53e416f9b5z";
    }
    if(LqFileGetStat(PathToFile, &FileStat) < 0) {
        goto lblOut;
    }
    CurRng = Ranges;
    for(i = 0; i < CountRanges; i++, CurRng += 2) {
        StartPos = (CurRng[0] > FileStat.Size) ? FileStat.Size : CurRng[0];
        if(StartPos < 0) {
            StartPos = (StartPos < FileStat.Size) ? (FileStat.Size + StartPos) : 0;
        }
        EndPos = (CurRng[1] > FileStat.Size) ? FileStat.Size : CurRng[1];
        if(EndPos < 0) {
            EndPos = (EndPos < FileStat.Size) ? (FileStat.Size + EndPos) : 0;
        }
        if(StartPos >= EndPos) {
            lq_errno_set(EINVAL);
            goto lblOut;
        }
    }
    if(CountRanges > 1) {
        CurRng = Ranges;
        for(i = 0; i < CountRanges; i++, CurRng += 2) {
            StartPos = (CurRng[0] > FileStat.Size) ? FileStat.Size : CurRng[0];
            if(StartPos < 0) {
                StartPos = (StartPos < FileStat.Size) ? (FileStat.Size + StartPos) : 0;
            }
            EndPos = (CurRng[1] > FileStat.Size) ? FileStat.Size : CurRng[1];
            if(EndPos < 0) {
                EndPos = (EndPos < FileStat.Size) ? (FileStat.Size + EndPos) : 0;
            }
            LqSockBufPrintf(
                HttpConn,
                false,
                "\r\n--%s\r\n"
                "Content-Type: %s\r\n"
                "Content-Range: bytes %lli-%lli/%lli\r\n"
                "\r\n",
                Boundary,
                ((MimeBuf[0] == '\0') ? "application/octet-stream" : MimeBuf),
                (long long)StartPos, (long long)(EndPos - 1), (long long)FileStat.Size
            );

            if(!LqSockBufRspFilePart(HttpConn, PathToFile, StartPos, EndPos - StartPos)) {
                LqSockBufRspClearAfterHdr(HttpConn);
                goto lblOut;
            }
            LqSockBufPrintf(
                HttpConn,
                false,
                "\r\n--%s--",
                Boundary
            );
        }
    }
    if(HttpConnData->BoundaryOrContentRangeOrLocation != NULL) {
        free(HttpConnData->BoundaryOrContentRangeOrLocation);
        HttpConnData->BoundaryOrContentRangeOrLocation = NULL;
    }
    HttpConnData->Flags &= ~(LQHTTPCONN_FLAG_BOUNDARY | LQHTTPCONN_FLAG_LOCATION);
    if(HttpConnData->RspFileName != NULL) {
        free(HttpConnData->RspFileName);
        HttpConnData->RspFileName = NULL;
    }
    if(CountRanges == 1) {
        if(!LqSockBufRspFilePart(HttpConn, PathToFile, StartPos, EndPos - StartPos)) {
            goto lblOut;
        }
        LqFbuf_snprintf(MimeBuf, sizeof(MimeBuf), "%lli-%lli/%lli", (long long)StartPos, (long long)(EndPos - 1), (long long)FileStat.Size);
        HttpConnData->BoundaryOrContentRangeOrLocation = LqStrDuplicate(MimeBuf);
    } else {
        HttpConnData->BoundaryOrContentRangeOrLocation = LqStrDuplicate(Boundary);
        HttpConnData->Flags |= LQHTTPCONN_FLAG_BOUNDARY;
    }
    HttpConnData->RspFileName = LqStrDuplicate(PathToFile);
    HttpConnData->RspStatus = 206;
    Res = true;
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnRspRetrieveMultipartRanges(LqHttpConn* HttpConn, LqFileSz* DestRanges, int CountRanges) {
    int Res = -1;
    const char* RangeHdrVal;
    size_t SourceLen;
    int n, n2, n3, k;
    long long FilePos, FilePos2;
    LqFileSz* CurRng;

    LqSockBufLock(HttpConn);
    if((RangeHdrVal = LqHttpConnRcvHdrGet(HttpConn, "range")) == NULL) {
        lq_errno_set(ENOENT);
        goto lblOut;
    }
    SourceLen = LqStrLen(RangeHdrVal);
    n = -1;
    LqFbuf_snscanf(RangeHdrVal, SourceLen, "%#*{bytes=}%n", &n);
    if(n == -1) {
        goto lblOut;
    }
    k = n;
    for(Res = 0; (Res < CountRanges) && (k < SourceLen); DestRanges += 2, Res++) {
        CurRng = DestRanges;
        n2 = n3 = n = -1;
        FilePos = FilePos2 = LQ_MAX_CONTENT_LEN;
        LqFbuf_snscanf(RangeHdrVal + k, SourceLen - k, "%?*[ \t]%lli%?*[ \t]%n-%n%?*[ \t]%lli%?*[ \t,]%n", &FilePos, &n, &n2, &FilePos2, &n3);
        if(n < 0) {
            break;
        }
        CurRng[0] = FilePos;
        CurRng[1] = FilePos2 + 1;
        if(n3 > 0)
            k += n3;
        else if(n2 > 0)
            k += n2;
        else
            k += n;
    }
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C const char* LQ_CALL LqHttpConnRcvGetFileName(LqHttpConn* HttpConn) {
    LqHttpConnData* HttpConnData;
    const char* Res = NULL;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(HttpConnData->Pth == NULL) {
        goto lblOut;
    }
    switch(HttpConnData->Pth->Type & LQHTTPPTH_TYPE_SEP) {
        case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
            Res = HttpConnData->Pth->RealPath;
            break;
        default:
            goto lblOut;
    }
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspFileAuto(LqHttpConn* HttpConn, const char* PathToFile, const char* Boundary) {
    int Status;
    bool Res = false;
    LqFileSz MultipartRanges[20 * 2];
    int MultipartRangesCount;
    char Buf[1024];
    LqFileStat FileStat;
    LqHttpConnData* HttpConnData;

    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    Status = LqHttpConnRetrieveResponseStatus(HttpConn, PathToFile);
    switch(Status) {
        case 200:
            if(!(Res = LqHttpConnRspFile(HttpConn, PathToFile))) {
                if(lq_errno == ENOENT) {
                    LqHttpConnRspError(HttpConn, 404);
                } else {
                    LqHttpConnRspError(HttpConn, 500);
                }
            }
            break;
        case 206:
            MultipartRangesCount = LqHttpConnRspRetrieveMultipartRanges(HttpConn, MultipartRanges, 19);
            if(MultipartRangesCount <= 0) {
                if(PathToFile == NULL) {
                    if((PathToFile = LqHttpConnRcvGetFileName(HttpConn)) == NULL)
                        goto lblOut2;
                }
                if(LqFileGetStat(PathToFile, &FileStat) < 0) {
lblOut2:
                    LqHttpConnRspError(HttpConn, 404);
                    Res = true;
                    goto lblOut;
                }
                LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "*/%lli", (long long)FileStat.Size);
                if(HttpConnData->BoundaryOrContentRangeOrLocation) {
                    free(HttpConnData->BoundaryOrContentRangeOrLocation);
                }
                HttpConnData->Flags &= ~(LQHTTPCONN_FLAG_BOUNDARY | LQHTTPCONN_FLAG_LOCATION);
                HttpConnData->BoundaryOrContentRangeOrLocation = LqStrDuplicate(Buf);
                LqHttpConnRspError(HttpConn, 416);
                Res = true;
                goto lblOut;
            }
            Res = LqHttpConnRspFileMultipart(HttpConn, PathToFile, Boundary, MultipartRanges, MultipartRangesCount);
            break;
        default:
            LqHttpConnRspError(HttpConn, Status);
            Res = true;
    }
lblOut:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRspPrintf(LqHttpConn* HttpConn, const char* Fmt, ...) {
    va_list Va;
    intptr_t Res;
    va_start(Va, Fmt);
    Res = LqSockBufPrintfVa(HttpConn, false, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRspPrintfVa(LqHttpConn* HttpConn, const char* Fmt, va_list Va) {
    return LqSockBufPrintfVa(HttpConn, false, Fmt, Va);
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRspHdrPrintf(LqHttpConn* HttpConn, const char* Fmt, ...) {
    va_list Va;
    intptr_t Res;
    va_start(Va, Fmt);
    Res = LqSockBufPrintfVa(HttpConn, true, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRspHdrPrintfVa(LqHttpConn* HttpConn, const char* Fmt, va_list Va) {
    return LqSockBufPrintfVa(HttpConn, true, Fmt, Va);
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspHdrInsert(LqHttpConn* HttpConn, const char* HeaderName, const char* HeaderVal) {
    bool Res = false;
    LqHttpConnData* HttpConnData;
    char*c, *StartNameHdr, *EndNameHdr,  *StartLine, *EndLine, *StartBuf;
    int HdrValLen = -1, HdrNameLen, DeleteSize, AppendSize, NewLength;

    if(HeaderVal != NULL)
        HdrValLen = LqStrLen(HeaderVal);
    HdrNameLen = LqStrLen(HeaderName);
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    StartBuf = c = HttpConnData->AdditionalHdrs;
    if(c != NULL) {
        while(*c != '\0') {
            StartLine = c;
            for(; (*c == ' ') || (*c == '\t'); c++);
            StartNameHdr = c;
            for(; (*c != ':') && (*c != '\t') && (*c != ' ') && (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
            EndNameHdr = c;
            for(; (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
            for(; (*c == '\r') || (*c == '\n'); c++);
            if(((EndNameHdr - StartNameHdr) == HdrNameLen) && LqStrUtf8CmpCaseLen(HeaderName, StartNameHdr, HdrNameLen)) {
                EndLine = c;
                DeleteSize = EndLine - StartLine;
                memmove(StartLine, EndLine, ((StartBuf + HttpConnData->AdditionalHdrsLen) - EndLine) + 1);
                if(HdrValLen == -1) { /* If we want delete line*/
                    HttpConnData->AdditionalHdrs = (char*)realloc(StartBuf, (HttpConnData->AdditionalHdrsLen - DeleteSize) + 1);
                    Res = true;
                }
                HttpConnData->AdditionalHdrsLen -= DeleteSize;
                break;
            }
        }
    }
    if(HdrValLen != -1) {
        NewLength = HttpConnData->AdditionalHdrsLen + HdrNameLen + HdrValLen + (sizeof(": \r\n") - 1);
        if((StartBuf = (char*)realloc(HttpConnData->AdditionalHdrs, NewLength + 1)) == NULL) {
            lq_errno_set(ENOMEM);
            goto lblErr;
        }
        HttpConnData->AdditionalHdrs = (char*)StartBuf;
        LqFbuf_snprintf(HttpConnData->AdditionalHdrs + HttpConnData->AdditionalHdrsLen, 0xffff, "%s: %s\r\n", HeaderName, HeaderVal);
        HttpConnData->AdditionalHdrsLen = NewLength;
        Res = true;
    }
lblErr:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspHdrGet(LqHttpConn* HttpConn, const char* HeaderName, const char** StartVal, size_t* Len) {
    bool Res = false;
    LqHttpConnData* HttpConnData;
    char*c, *StartNameHdr, *EndNameHdr, *StartValHdr, *EndValHdr;
    int HdrNameLen;

    HdrNameLen = LqStrLen(HeaderName);
    LqSockBufLock(HttpConn);
    if((c = LqHttpConnGetData(HttpConn)->AdditionalHdrs) != NULL) {
        while(*c != '\0') {
            for(; (*c == ' ') || (*c == '\t'); c++);
            StartNameHdr = c;
            for(; (*c != ':') && (*c != '\t') && (*c != ' ') && (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
            EndNameHdr = c;
            for(; (*c == ' ') || (*c == '\t') || (*c == ':'); c++);
            StartValHdr = c;
            for(; (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
            EndValHdr = c;
            if(((EndNameHdr - StartNameHdr) == HdrNameLen) && LqStrUtf8CmpCaseLen(HeaderName, StartNameHdr, HdrNameLen)) {
                *StartVal = StartValHdr;
                *Len = EndValHdr - StartValHdr;
                Res = true;
                break;
            }
            for(; (*c == '\r') || (*c == '\n'); c++);
        }
    }
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRspHdrEnumNext(LqHttpConn* HttpConn, const char** StartName, size_t* Len, const char** StartVal, size_t* ValLen) {
    bool Res = false;
    LqHttpConnData* HttpConnData;
    char*c, *StartNameHdr, *EndNameHdr, *StartValHdr, *EndValHdr;
    int HdrNameLen;

    HdrNameLen = *Len;
    LqSockBufLock(HttpConn);
    if((c = LqHttpConnGetData(HttpConn)->AdditionalHdrs) != NULL) {
        if(*StartName == NULL) {
            for(; (*c == ' ') || (*c == '\t'); c++);
            StartNameHdr = c;
            for(; (*c != ':') && (*c != '\t') && (*c != ' ') && (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
            EndNameHdr = c;
            if(StartNameHdr != EndNameHdr) {
                *StartName = StartNameHdr;
                *Len = EndNameHdr - StartNameHdr;
                if(StartVal != NULL) {
                    for(; (*c == ' ') || (*c == '\t') || (*c == ':'); c++);
                    StartValHdr = c;
                    for(; (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
                    EndValHdr = c;
                    *StartVal = StartValHdr;
                    *ValLen = EndValHdr - StartValHdr;
                }
                Res = true;
            }
        } else {
            while(*c != '\0') {
                for(; (*c == ' ') || (*c == '\t'); c++);
                StartNameHdr = c;
                for(; (*c != ':') && (*c != '\t') && (*c != ' ') && (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
                EndNameHdr = c;
                for(; (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
                for(; (*c == '\r') || (*c == '\n'); c++);
                if(((EndNameHdr - StartNameHdr) == HdrNameLen) && LqStrUtf8CmpCaseLen(*StartName, StartNameHdr, HdrNameLen)) {
                    for(; (*c == ' ') || (*c == '\t'); c++);
                    StartNameHdr = c;
                    for(; (*c != ':') && (*c != '\t') && (*c != ' ') && (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
                    EndNameHdr = c;
                    if(StartNameHdr != EndNameHdr) {
                        *StartName = StartNameHdr;
                        *Len = EndNameHdr - StartNameHdr;
                        if(StartVal != NULL) {
                            for(; (*c == ' ') || (*c == '\t') || (*c == ':'); c++);
                            StartValHdr = c;
                            for(; (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
                            EndValHdr = c;
                            *StartVal = StartValHdr;
                            *ValLen = EndValHdr - StartValHdr;
                        }
                        Res = true;
                    }
                    break;
                }
            }
        }
    }
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRspWrite(LqHttpConn* HttpConn, const void* Source, size_t Size) {
    intptr_t Res;
    LqSockBufLock(HttpConn);
    if((Res = LqSockBufWrite(HttpConn, false, Source, Size)) > 0) {
        LqHttpConnGetData(HttpConn)->Flags |= LQHTTPCONN_FLAG_BIN_RSP;
    }
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C const char* LQ_CALL LqHttpConnRcvHdrGet(LqHttpConn* HttpConn, const char* Name) {
    LqHttpConnData* HttpConnData;
    LqHttpRcvHdrs* Hdrs;
    size_t i;

    HttpConnData = LqHttpConnGetData(HttpConn);
    Hdrs = HttpConnData->RcvHdr;
    if(Hdrs == NULL)
        return NULL;
    for(i = 0; i < Hdrs->CountHdrs; i++) {
        if(LqStrUtf8CmpCase(Name, Hdrs->Hdrs[i].Name))
            return Hdrs->Hdrs[i].Val;
    }
    return NULL;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnRcvGetBoundary(LqHttpConn* HttpConn, char* Dest, int MaxDest) {
    const char* ContentType;
    size_t ContentTypeLen;
    int n, n2, Res = -1;

    LqSockBufLock(HttpConn);
    if((ContentType = LqHttpConnRcvHdrGet(HttpConn, "content-type")) == NULL) {
        lq_errno_set(ENODATA);
        goto lblErr;
    }
    n2 = n = -1;
    
    ContentTypeLen = LqStrLen(ContentType);
    if(Dest) {
        Dest[0] = Dest[MaxDest - 1] = '\0';
        LqFbuf_snscanf(ContentType, ContentTypeLen, "%?*[ \t]%#*{multipart/form-data}%#*{^boundary=}%#*{boundary=}%n%.*[^ ;\r\n]%n", &n, (int)(MaxDest - 1), Dest, &n2);
    } else {
        LqFbuf_snscanf(ContentType, ContentTypeLen, "%?*[ \t]%#*{multipart/form-data}%#*{^boundary=}%#*{boundary=}%n%*[^ ;\r\n]%n", &n, &n2);
    }
    if((n2 == -1) || (n == -1)) {
        lq_errno_set(ENODATA);
        goto lblErr;
    }
    Res = n2 - n;
lblErr:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqHttpConnRcvGetContentLength(LqHttpConn* HttpConn) {
    LqFileSz Res;
    LqHttpConnData* HttpConnData;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    Res = HttpConnData->ContentLength;
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqHttpConnRcvGetContentLengthLeft(LqHttpConn* HttpConn) {
    LqFileSz Res;
    LqHttpConnData* HttpConnData;
    int64_t CounterVal;

    LqSockBufLock(HttpConn);
    CounterVal = LqSockBufRcvGetCount(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(HttpConnData->ContentLength == -((LqFileSz)1)) {
        Res = -((LqFileSz)1);
    }if(HttpConnData->ContentLength < CounterVal) {
        Res = 0;
    } else {    
        Res = HttpConnData->ContentLength - CounterVal;
    }
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRcvTryScanf(LqHttpConn* HttpConn, int Flags, const char* Fmt, ...){
    va_list Va;
    intptr_t Res;

    va_start(Va, Fmt);
    Res = LqSockBufScanfVa(HttpConn, Flags, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRcvTryScanfVa(LqHttpConn* HttpConn, int Flags, const char* Fmt, va_list Va) {
    return LqSockBufScanfVa(HttpConn, Flags, Fmt, Va);
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRcvTryRead(LqHttpConn* HttpConn, void* Buf, size_t Len) {
    return LqSockBufRead(HttpConn, Buf, Len);
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnRcvTryPeek(LqHttpConn* HttpConn, void* Buf, size_t Len) {
    return LqSockBufPeek(HttpConn, Buf, Len);
}

LQ_EXTERN_C LqFileSz LQ_CALL LqHttpConnRspGetContentLength(LqHttpConn* HttpConn) {
    return LqSockBufRspLen(HttpConn, false);
}

LQ_EXTERN_C void LQ_CALL LqHttpMultipartHdrsDelete(LqHttpMultipartHdrs* Target) {
    if(Target->Hdrs)
        LqMemFree(Target->Hdrs);
    LqMemFree(Target);
}

LQ_EXTERN_C int LQ_CALL LqHttpConnRcvMultipartHdrs(
    LqHttpConn* HttpConn,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* Boundary,
    LqHttpMultipartHdrs** Dest
) {
    int Res = -1, Start, End, n;
    _LqRcvMultipartHdrsData* Data;
    char Buf[4096], BoundaryBuf[4096];
    LqFileSz MaxLength;
    LqHttpMultipartHdrs* Hdrs;

    LqSockBufLock(HttpConn);
    if(Boundary == NULL) {
        if(LqHttpConnRcvGetBoundary(HttpConn, BoundaryBuf, sizeof(BoundaryBuf)) <= 0) {
            lq_errno_set(ENODATA);
            goto lblErr;
        }
        Boundary = BoundaryBuf;
    }
    if(LqStrChr(Boundary, '%')) {
        lq_errno_set(EINVAL);
        goto lblErr;
    }

    if(LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "%%?*{\r\n}--%s--%%*{\r\n}", Boundary) > (sizeof(Buf) - 3)) {
        lq_errno_set(EOVERFLOW);
        goto lblErr;
    }
    if(LqSockBufScanf(HttpConn, LQSOCKBUF_PEEK, Buf) == 2) {
        LqSockBufScanf(HttpConn, 0, Buf);
        LqSockBufUnlock(HttpConn);
        return 0;
    }
    MaxLength = LqHttpConnRcvGetContentLengthLeft(HttpConn);
    if(MaxLength == -((LqFileSz)1)){
        lq_errno_set(EFAULT);
        goto lblErr;
    }
    if(MaxLength > 32768)
        MaxLength = 32768;
    if(LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "%%?*{\r\n}--%s\r\n%n%%n%%*.%lli{^\r\n\r\n}%%n%%*{\r\n\r\n}", Boundary, &n, (long long)MaxLength) > (sizeof(Buf) - 3)) {
        lq_errno_set(EOVERFLOW);
        goto lblErr;
    }
    End = Start = -1;
    if((LqSockBufScanf(HttpConn, LQSOCKBUF_PEEK, Buf, &Start, &End) < 3) || (Dest == NULL)) {
        if((Start == -1) && (LqSockBufRcvBufSz(HttpConn) > n)) {
            lq_errno_set(ESRCH);
            goto lblErr;
        }
        if((End - Start) > (MaxLength - 2)) {
            lq_errno_set(E2BIG);
            goto lblErr;
        }
        if(LqSockBufGetErrFlags(HttpConn) & LQSOCKBUF_FLAGS_READ_ERROR) {
            lq_errno_set(EPIPE);
            goto lblErr;
        }
        if(CompleteOrCancelProc == NULL) {
            Res = 0;
            goto lblErr;
        }
        if((Data = LqFastAlloc::New<_LqRcvMultipartHdrsData>()) == NULL) {
            lq_errno_set(ENOMEM);
            goto lblErr;
        }

        if((Data->Boundary = LqStrDuplicate(Boundary)) == NULL) {
            lq_errno_set(ENOMEM);
            goto lblErr2;
        }
        Data->MaxLen = MaxLength;
        Data->UserData = UserData;
        Data->CompleteOrCancelProc = CompleteOrCancelProc;

        LqFbuf_snprintf(Buf, sizeof(Buf) - 1, "%%?*{\r\n}--%s\r\n%%*.%lli{^\r\n\r\n}%%*{\r\n\r\n}", Boundary, (long long)MaxLength);
        if(LqSockBufRcvNotifyWhenMatch(HttpConn, Data, _LqHttpConnRcvMultipartHdrsProc, Buf, 3, MaxLength, false)) {
            Res = 0;
        } else {
            free(Data->Boundary);
lblErr2:
            LqFastAlloc::Delete(Data);
        }
        goto lblErr;
    } else {
        Hdrs = _LqHttpMultipartHdrsRead(HttpConn, Boundary, MaxLength);
        if(Hdrs != NULL) {
            *Dest = Hdrs;
            Res = 1;
        }
    }
lblErr:
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvFile(
    LqHttpConn* HttpConn, 
    const char* Path, 
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    LqFileSz ReadLen, 
    int Access, 
    bool IsReplace,
    bool IsCreateSubdir
) {
    LqHttpConnData* HttpConnData;
    bool Res;
    int Fd;
    _LqRecvStream* Dest;
    const char* ContentLen;
    long long SizeFile;

    Res = false;
    Dest = NULL;
    Fd = -1;
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    if(Path == NULL) {
        if((Path = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            lq_errno_set(ENODATA);
            goto lblErr;
        }
    }
    if(ReadLen == -1) {
        if((ReadLen = LqHttpConnRcvGetContentLengthLeft(HttpConn)) == -((LqFileSz)1)) {
            lq_errno_set(ENODATA);
            goto lblErr;
        }
    }
    if((Dest = LqFastAlloc::New<_LqRecvStream>()) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
    if((Fd = LqFileTrdCreate(Path, LQ_O_CREATE | LQ_O_BIN | LQ_O_WR | LQ_O_TRUNC | ((IsCreateSubdir) ? LQ_TC_SUBDIR : 0) | ((IsReplace) ? LQ_TC_REPLACE : 0), Access)) == -1)
        goto lblErr;
    LqFbuf_fdopen(&Dest->DestFbuf, LQFBUF_FAST_LK, Fd, 0, 4050, 10);
    Dest->CompleteOrCancelProc = CompleteOrCancelProc;
    Dest->UserData = UserData;
    Res = LqSockBufRcvInFbuf(HttpConn, &Dest->DestFbuf, Dest, _LqHttpConnRcvFileProc, ReadLen, false);
    if(!Res) {
        Dest->DestFbuf.Cookie = &TrdCookie;
        LqFbuf_close(&Dest->DestFbuf);
        goto lblErr;
    }
    LqSockBufUnlock(HttpConn);
    return Res;
lblErr:
    if(Dest)
        LqFastAlloc::Delete(Dest);
    if(Fd != -1)
        LqFileTrdCancel(Fd);
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvFileAboveBoundary(
    LqHttpConn* HttpConn,
    const char* Path,
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* Boundary,
    LqFileSz MaxLen,
    int Access,
    bool IsReplace,
    bool IsCreateSubdir
) {
    bool Res;
    int Fd = -1;
    _LqRecvStream* Dest;
    size_t BndrLen;
    intptr_t SeqLen;
    char* FullSeq;
    char BoundaryBuf[1024];


    Dest = NULL;
    FullSeq = NULL;
    Res = false;
    LqSockBufLock(HttpConn);
    if(Path == NULL) {
        if((Path = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            lq_errno_set(ENODATA);
            goto lblErr;
        }
    }
    if(MaxLen == -1) {
        if((MaxLen = LqHttpConnRcvGetContentLengthLeft(HttpConn)) == -((LqFileSz)1)) {
            lq_errno_set(ENOMEM);
            goto lblErr;
        }
    }
    if(Boundary == NULL) {
        if(LqHttpConnRcvGetBoundary(HttpConn, BoundaryBuf, sizeof(BoundaryBuf) - 2) <= 0)
            goto lblErr;
        Boundary = BoundaryBuf;
    }
    if((Dest = LqFastAlloc::New<_LqRecvStream>()) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        goto lblErr;
    }   
    BndrLen = LqStrLen(Boundary);
    if((FullSeq = (char*)LqMemAlloc(BndrLen + 20)) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
    if((Fd = LqFileTrdCreate(Path, LQ_O_CREATE | LQ_O_BIN | LQ_O_WR | LQ_O_TRUNC | ((IsCreateSubdir) ? LQ_TC_SUBDIR : 0) | ((IsReplace) ? LQ_TC_REPLACE : 0), Access)) == -1) {
        goto lblErr;
    }
    LqFbuf_fdopen(&Dest->DestFbuf, LQFBUF_FAST_LK, Fd, 0, 4050, 10);
    Dest->CompleteOrCancelProc = CompleteOrCancelProc;
    Dest->UserData = UserData;
    SeqLen = LqFbuf_snprintf(FullSeq, BndrLen + 19, "\r\n--%s", Boundary);
    Res = LqSockBufRcvInFbufAboveSeq(HttpConn, &Dest->DestFbuf, Dest, _LqHttpConnRcvFileSeqProc, FullSeq, SeqLen, MaxLen, false, false);
    if(!Res) {
        Dest->DestFbuf.Cookie = &TrdCookie;
        LqFbuf_close(&Dest->DestFbuf);
        goto lblErr;
    }
    LqMemFree(FullSeq);
    LqSockBufUnlock(HttpConn);
    return Res;
lblErr:
    if(Dest)
        LqFastAlloc::Delete(Dest);
    if(FullSeq)
        LqMemFree(FullSeq);
    if(Fd != -1)
        LqFileTrdCancel(Fd);
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvFbuf(
    LqHttpConn* HttpConn,
    LqFbuf* Target,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    LqFileSz ReadLen
) {
    bool Res;
    _LqRecvStreamFbuf* Dest;

    Res = false;
    Dest = NULL;
    LqSockBufLock(HttpConn);
    if(ReadLen == -((LqFileSz)1)) {
        if((ReadLen = LqHttpConnRcvGetContentLengthLeft(HttpConn)) == -((LqFileSz)1)) {
            lq_errno_set(ENODATA);
            goto lblErr;
        }
    }
    if((Dest = LqFastAlloc::New<_LqRecvStreamFbuf>()) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
    Dest->CompleteOrCancelProc = CompleteOrCancelProc;
    Dest->UserData = UserData;
    Res = LqSockBufRcvInFbuf(HttpConn, Target, Dest, _LqHttpConnRcvFbufProc, ReadLen, false);
    if(!Res) {
        goto lblErr;
    }
    LqSockBufUnlock(HttpConn);
    return Res;
lblErr:
    if(Dest)
        LqFastAlloc::Delete(Dest);
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvFbufAboveBoundary(
    LqHttpConn* HttpConn,
    LqFbuf* Target,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* Boundary,
    LqFileSz MaxLen
) {
    bool Res;
    _LqRecvStreamFbuf* Dest;
    size_t BndrLen;
    intptr_t SeqLen;
    char* FullSeq, BoundaryBuf[1024];


    Dest = NULL;
    FullSeq = NULL;
    Res = false;
    LqSockBufLock(HttpConn);
    if(MaxLen == -((LqFileSz)1)) {
        if((MaxLen = LqHttpConnRcvGetContentLengthLeft(HttpConn)) == -((LqFileSz)1)) {
            lq_errno_set(ENOMEM);
            goto lblErr;
        }
    }
    if(Boundary == NULL) {
        if(LqHttpConnRcvGetBoundary(HttpConn, BoundaryBuf, sizeof(BoundaryBuf) - 2) <= 0)
            goto lblErr;
        Boundary = BoundaryBuf;
    }
    if((Dest = LqFastAlloc::New<_LqRecvStreamFbuf>()) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
    BndrLen = LqStrLen(Boundary);
    if((FullSeq = (char*)LqMemAlloc(BndrLen + 20)) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        goto lblErr;
    }

    Dest->CompleteOrCancelProc = CompleteOrCancelProc;
    Dest->UserData = UserData;
    SeqLen = LqFbuf_snprintf(FullSeq, BndrLen + 19, "\r\n--%s", Boundary);
    Res = LqSockBufRcvInFbufAboveSeq(HttpConn, Target, Dest, _LqHttpConnRcvFbufSeqProc, FullSeq, SeqLen, MaxLen, false, false);
    if(!Res) {
        goto lblErr;
    }
    LqMemFree(FullSeq);
    LqSockBufUnlock(HttpConn);
    return Res;
lblErr:
    LqSockBufUnlock(HttpConn);
    if(Dest)
        LqFastAlloc::Delete(Dest);
    if(FullSeq)
        LqMemFree(FullSeq);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvMultipartFbufNext(
    LqHttpConn* HttpConn,
    LqFbuf* Target,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult* RcvRes),
    void* UserData,
    const char* Boundary,
    LqFileSz MaxLen
) {
    int MutipartHdrsRes;
    LqHttpMultipartHdrs* Hdrs = NULL;
    _LqHttpMultipartFbufNext* Data = NULL;
    char BoundaryBuf[1024];

    if((Data = LqFastAlloc::New<_LqHttpMultipartFbufNext>()) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        return false;
    }
    LqSockBufLock(HttpConn);
    if(Boundary == NULL) {
        if(LqHttpConnRcvGetBoundary(HttpConn, BoundaryBuf, sizeof(BoundaryBuf) - 2) <= 0)
            goto lblErr;
        Boundary = BoundaryBuf;
    }
    Data->Hdrs = NULL;
    Data->MaxLen = MaxLen;
    Data->CompleteOrCancelProc = CompleteOrCancelProc;
    Data->Target = Target;
    Data->UserData = UserData;
    Data->Boundary = LqStrDuplicate(Boundary);
    MutipartHdrsRes = LqHttpConnRcvMultipartHdrs(HttpConn, _LqHttpConnRcvMultipartHdrsNextProc, Data, Boundary, &Hdrs);
    if(MutipartHdrsRes == -1) {
        goto lblErr;
    }
    if(MutipartHdrsRes == 1) {
        Data->Hdrs = Hdrs;
        if(!LqHttpConnRcvFbufAboveBoundary(HttpConn, Target, _LqHttpConnRcvFbufNextProc, Data, Boundary, MaxLen))
            goto lblErr;
    }
    LqSockBufUnlock(HttpConn);
    return true;
lblErr:
    LqSockBufUnlock(HttpConn);
    if(Data) {
        if(Data->Boundary)
            free(Data->Boundary);
        LqFastAlloc::Delete(Data);
    }
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvMultipartFileNext(
    LqHttpConn* HttpConn,
    const char* Path,
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* Boundary,
    LqFileSz MaxLen,
    int Access,
    bool IsReplace,
    bool IsCreateSubdir
) {
    int Fd = -1;
    _LqHttpConnRcvRcvMultipartFileStruct* Data;


    if((Data = LqFastAlloc::New<_LqHttpConnRcvRcvMultipartFileStruct>()) == NULL) {
        lq_errno_set(ENOMEM);
        return false;
    }
    LqSockBufLock(HttpConn);
    if(Path == NULL) {
        if((Path = LqHttpConnRcvGetFileName(HttpConn)) == NULL) {
            LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
            lq_errno_set(ENODATA);
            goto lblErr;
        }
    }
    Data->CompleteOrCancelProc = CompleteOrCancelProc;
    Data->UserData = UserData;
    if((Fd = LqFileTrdCreate(Path, LQ_O_CREATE | LQ_O_BIN | LQ_O_WR | LQ_O_TRUNC | ((IsCreateSubdir) ? LQ_TC_SUBDIR : 0) | ((IsReplace) ? LQ_TC_REPLACE : 0), Access)) == -1)
        goto lblErr;
    LqFbuf_fdopen(&Data->Fbuf, LQFBUF_FAST_LK, Fd, 0, 4050, 10);
    if(!LqHttpConnRcvMultipartFbufNext(HttpConn, &Data->Fbuf, _LqHttpConnRcvMultipartFileNextProc, Data, Boundary, MaxLen))
        goto lblErr;
    LqSockBufUnlock(HttpConn);
    return true;
lblErr:
    LqSockBufUnlock(HttpConn);
    if(Fd != -1)
        LqFileTrdCancel(Fd);
    if(Data != NULL)
        LqFastAlloc::Delete(Data);
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnRcvWaitLen(
    LqHttpConn* HttpConn,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    intptr_t TargetLen
) {
    _LqHttpConnRcvWaitLenStruct* Data;
    LqFileSz FileSz;
    if((Data = LqFastAlloc::New<_LqHttpConnRcvWaitLenStruct>()) == NULL) {
        LqLogErr("LqHttp: Src %s:%i; not alloc mem", __FILE__, __LINE__);
        lq_errno_set(ENOMEM);
        return false;
    }
    Data->CompleteOrCancelProc = CompleteOrCancelProc;
    Data->UserData = UserData;
    if(TargetLen == -((intptr_t)1)) {
        FileSz = LqHttpConnRcvGetContentLengthLeft(HttpConn);
        if(FileSz == -((LqFileSz)1)) {
            lq_errno_set(ENODATA);
            return false;
        }
        TargetLen = FileSz;
    }
    if(!LqSockBufRcvWaitLenData(HttpConn, Data, _LqHttpConnRcvWaitLenProc, TargetLen, false)) {
        LqFastAlloc::Delete(Data);
        return false;
    }
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnGetRemoteIpStr(LqHttpConn* HttpConn, char* DestStr, size_t DestStrLen) {
    return LqSockBufGetRemoteAddrStr(HttpConn, DestStr, DestStrLen);
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnGetRemoteIp(LqHttpConn* HttpConn, LqConnAddr* AddrDest) {
    return LqSockBufGetRemoteAddr(HttpConn, AddrDest);
}

LQ_EXTERN_C unsigned short LQ_CALL LqHttpConnGetRemotePort(LqHttpConn* HttpConn) {
    LqConnAddr AddrDest = {0};
    if(!LqSockBufGetRemoteAddr(HttpConn, &AddrDest))
        return 0;
    if(AddrDest.Addr.sa_family == AF_INET) {
        return ntohs(AddrDest.AddrInet.sin_port);
    }else if(AddrDest.Addr.sa_family == AF_INET6) {
        return ntohs(AddrDest.AddrInet6.sin6_port);
    } else {
        return 0;
    }
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnGetLocIpStr(LqHttpConn* HttpConn, char* DestStr, size_t DestStrLen) {
    return LqSockBufGetLocAddrStr(HttpConn, DestStr, DestStrLen);
}

LQ_EXTERN_C bool LQ_CALL LqHttpConnGetLocIp(LqHttpConn* HttpConn, LqConnAddr* AddrDest) {
    return LqSockBufGetLocAddr(HttpConn, AddrDest);
}

LQ_EXTERN_C LqHttp* LQ_CALL LqHttpCreate(const char* Host, const char* Port, int RouteProto, void* SslCtx, bool IsSetCache, bool IsSetZmbClr) {
    LqHttpData* HttpData;
    LqSockAcceptor* SockAcceptor;
    LqFche* Cache;
    if((HttpData = LqFastAlloc::New<LqHttpData>()) == NULL) {
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
    SockAcceptor = LqSockAcceptorCreate(Host, Port, RouteProto, SOCK_STREAM, IPPROTO_TCP, 32768, true, HttpData);
    if(SockAcceptor == NULL)
        goto lblErr;
    SockAcceptor->AcceptProc = _LqHttpAcceptProc;
    SockAcceptor->CloseHandler = _LqHttpCloseHandler;

    HttpData->MaxHeadersSize = 32 * 1024; //32 kByte
    HttpData->SslCtx = SslCtx;
    HttpData->PeriodChangeDigestNonce = 5; //5 Sec

    HttpData->MaxLenRspConveyor = 10;
    HttpData->MaxCountConn = 32768;

    HttpData->KeepAlive = 5 * 60 * 1000;
    HttpData->IsDeleteFlag = NULL;
    HttpData->UseDefaultDmn = true;
    HttpData->WrkBoss = NULL;
    HttpData->MaxSkipContentLength = 1024ll * 1024ll * 1024ll * 5ll; /* 5 gb*/
    LqHttpSetNameServer(SockAcceptor, "Lanq(Lan Quick) 1.0");
    HttpData->CountPtrs = 1;
    if(IsSetCache) {
        Cache = LqFcheCreate();
        if(Cache != NULL) {
            LqFcheSetMaxSize(Cache, 1024 * 1024 * 400);
            LqFcheSetMaxSizeFile(Cache, 1024 * 1024 * 27);
            LqSockAcceptorSetInstanceCache(SockAcceptor, Cache);
            LqFcheDelete(Cache);
        }
    }
    if(IsSetZmbClr) {
        HttpData->ZmbClr = LqZmbClrCreate(SockAcceptor, 5 * 1000, SockAcceptor, true);
    } else {
        HttpData->ZmbClr = NULL;
    }
    LqFbuf_snprintf(HttpData->HTTPProtoVer, sizeof(HttpData->HTTPProtoVer) - 2, "1.1");
    LqHttpMdlInit(SockAcceptor, &HttpData->StartModule, "StartModule", 0);
    LqHttpPthDmnCreate(SockAcceptor, "*");
    return SockAcceptor;
lblErr:
    if(HttpData != NULL)
        LqFastAlloc::Delete(HttpData);
    if(SockAcceptor != NULL)
        LqSockAcceptorDelete(SockAcceptor);
    return NULL;
}

LQ_EXTERN_C int LQ_CALL LqHttpDelete(LqHttp* Http, volatile bool* lqaout lqaopt DeleteFlag) {
    LqHttpData* HttpData;
    void* WrkBoss;

    LqSockAcceptorLock(Http);
    HttpData = LqHttpGetHttpData(Http);
    HttpData->IsDeleteFlag = DeleteFlag;
    WrkBoss = HttpData->WrkBoss;
    _LqHttpDereference(Http);
    LqSockAcceptorUnlock(Http);

    LqSockAcceptorInterruptWork(Http);
    LqSockBufCloseByUserData2(Http, WrkBoss);
    return 0;
}

LQ_EXTERN_C bool LQ_CALL LqHttpGoWork(LqHttp* Http, void* WrkBoss){
    LqHttpData* HttpData;
    bool Res = true;
    LqSockAcceptorLock(Http);
    HttpData = LqHttpGetHttpData(Http);
    if(HttpData->WrkBoss == NULL) {
        HttpData->WrkBoss = WrkBoss;
        if(HttpData->ZmbClr) {
            if(!(Res = LqZmbClrGoWork(HttpData->ZmbClr, WrkBoss)))
                goto lblOut;
        }
        Res = LqSockAcceptorGoWork(Http, WrkBoss);
    } else {
        Res = false;
    }
lblOut:
    LqSockAcceptorUnlock(Http);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqHttpInterruptWork(LqHttp* Http) {
    LqHttpData* HttpData;
    bool Res = false;
    LqSockAcceptorLock(Http);
    HttpData = LqHttpGetHttpData(Http);
    if(HttpData->WrkBoss != NULL) {
        if(HttpData->ZmbClr) {
            Res |= LqZmbClrInterruptWork(HttpData->ZmbClr);
        }
        Res |= LqSockAcceptorInterruptWork(Http);
        HttpData->WrkBoss = NULL;
    }
lblOut:
    LqSockAcceptorUnlock(Http);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqHttpCloseAllConn(LqHttp* Http) {
    LqSockBufCloseByUserData2(Http, LqHttpGetHttpData(Http)->WrkBoss);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCountConn(LqHttp* Http) {
    size_t Res = 0;
    LqSockAcceptorLock(Http);
    Res = LqHttpGetHttpData(Http)->CountPtrs - 1;
    LqSockAcceptorUnlock(Http);
    return Res;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpSetNameServer(LqHttp* Http, const char* NewName) {
    size_t SizeWritten = 0;
    LqSockAcceptorLock(Http);
    LqStrCopyMax(LqHttpGetHttpData(Http)->ServName, NewName, sizeof(LqHttpGetHttpData(Http)->ServName));
    SizeWritten = LqStrLen(LqHttpGetHttpData(Http)->ServName);
    LqSockAcceptorUnlock(Http);
    return SizeWritten;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpGetNameServer(LqHttp* Http, char* Name, size_t SizeName) {
    size_t SizeWritten = 0;
    LqSockAcceptorLock(Http);
    LqStrCopyMax(Name, LqHttpGetHttpData(Http)->ServName, SizeName);
    SizeWritten = LqStrLen(LqHttpGetHttpData(Http)->ServName);
    LqSockAcceptorUnlock(Http);
    return SizeWritten;
}

LQ_EXTERN_C void LQ_CALL LqHttpSetKeepAlive(LqHttp* Http, LqTimeMillisec Time) {
    LqHttpGetHttpData(Http)->KeepAlive = Time;
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqHttpGetKeepAlive(LqHttp* Http) {
    return LqHttpGetHttpData(Http)->KeepAlive;
}

LQ_EXTERN_C LqFche* LQ_CALL LqHttpGetCache(LqHttp* Http) {
    return LqSockAcceptorGetInstanceCache(Http);
}

LQ_EXTERN_C void LQ_CALL LqHttpConnLock(LqHttpConn* HttpConn) {
    LqSockBufLock(HttpConn);
}

LQ_EXTERN_C void LQ_CALL LqHttpConnUnlock(LqHttpConn* HttpConn) {
    LqSockBufUnlock(HttpConn);
}

LQ_EXTERN_C int LQ_CALL LqHttpConnDataStore(LqHttpConn* HttpConn, const void* Name, const void* Value) {
    LqHttpConnData *HttpConnData;
    LqHttpUserData *New, *UserData;
    int Res = -1;
    unsigned short i;
    
    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    UserData = HttpConnData->UserData;
    for(i = 0; i < HttpConnData->UserDataCount; i++) {
        if(UserData[i].Name == Name) {
            UserData[i].Data = (void*)Value;
            Res = HttpConnData->UserDataCount;
            LqSockBufUnlock(HttpConn);
            return Res;
        }
    }
    New = LqFastAlloc::ReallocCount<LqHttpUserData>(HttpConnData->UserData, HttpConnData->UserDataCount, HttpConnData->UserDataCount + 1);
    if(New == NULL) {
        LqSockBufUnlock(HttpConn);
        return -1;
    }
    HttpConnData->UserData = New;
    New[HttpConnData->UserDataCount].Name = (void*)Name;
    New[HttpConnData->UserDataCount].Data = (void*)Value;
    HttpConnData->UserDataCount++;
    Res = HttpConnData->UserDataCount;
    LqSockBufUnlock(HttpConn);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnDataGet(LqHttpConn* HttpConn, const void* Name, void** Value) {
    LqHttpConnData *HttpConnData;
    LqHttpUserData *UserData;
    unsigned short i;

    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    UserData = HttpConnData->UserData;
    for(i = 0; i < HttpConnData->UserDataCount; i++) {
        if(UserData[i].Name == Name) {
            *Value = UserData[i].Data;
            LqSockBufUnlock(HttpConn);
            return 1;
        }
    }
    LqSockBufUnlock(HttpConn);
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnDataUnstore(LqHttpConn* HttpConn, const void* Name) {
    LqHttpConnData *HttpConnData;
    LqHttpUserData *UserData;
    int Res;
    unsigned short i;

    LqSockBufLock(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    UserData = HttpConnData->UserData;
    for(i = 0; i < HttpConnData->UserDataCount; i++) {
        if(UserData[i].Name == Name) {
            HttpConnData->UserDataCount--;
            UserData[i] = UserData[HttpConnData->UserDataCount];
            HttpConnData->UserData = LqFastAlloc::ReallocCount<LqHttpUserData>(UserData, HttpConnData->UserDataCount + 1, HttpConnData->UserDataCount);
            Res = HttpConnData->UserDataCount;
            LqSockBufUnlock(HttpConn);
            return Res;
        }
    }
    LqSockBufUnlock(HttpConn);
    return -1;
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterQuery(LqHttp* Http, LqHttpNotifyFn QueryFunc) {
    return LqHttpGetHttpData(Http)->StartQueryHndls.Add(QueryFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterQuery(LqHttp* Http, LqHttpNotifyFn QueryFunc) {
    return LqHttpGetHttpData(Http)->StartQueryHndls.Rm(QueryFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterResponse(LqHttp* Http, LqHttpNotifyFn ResponseFunc) {
    return LqHttpGetHttpData(Http)->EndResponseHndls.Add(ResponseFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterResponse(LqHttp* Http, LqHttpNotifyFn ResponseFunc) {
    return LqHttpGetHttpData(Http)->EndResponseHndls.Rm(ResponseFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterConnect(LqHttp* Http, LqHttpNotifyFn ConnectFunc) {
    return LqHttpGetHttpData(Http)->ConnectHndls.Add(ConnectFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterConnect(LqHttp* Http, LqHttpNotifyFn ConnectFunc) {
    return LqHttpGetHttpData(Http)->ConnectHndls.Rm(ConnectFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterDisconnect(LqHttp* Http, LqHttpNotifyFn DisconnectFunc) {
    return LqHttpGetHttpData(Http)->DisconnectHndls.Add(DisconnectFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterDisconnect(LqHttp* Http, LqHttpNotifyFn DisconnectFunc) {
    return LqHttpGetHttpData(Http)->DisconnectHndls.Rm(DisconnectFunc);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpEnumConn(LqHttp* Http, int(LQ_CALL* Proc)(void*, LqHttpConn*), void* UserData) {
    LqHttpData* HttpData;
    _LqHttpEnumConnProcData Data = {UserData, Http, Proc};
    HttpData = LqHttpGetHttpData(Http);
    return LqSockBufEnum(HttpData->WrkBoss, _LqHttpEnumConnProc, &Data);
}

LQ_EXTERN_C void LQ_CALL LqHttpSetMaxHdrsSize(LqHttp* Http, size_t NewSize) {
    LqSockAcceptorLock(Http);
    LqHttpGetHttpData(Http)->MaxHeadersSize = NewSize;
    LqSockAcceptorUnlock(Http);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpGetMaxHdrsSize(LqHttp* Http) {
    size_t Res;
    LqSockAcceptorLock(Http);
    Res = LqHttpGetHttpData(Http)->MaxHeadersSize;
    LqSockAcceptorUnlock(Http);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqHttpSetNonceChangeTime(LqHttp* Http, LqTimeSec NewTime) {
    LqSockAcceptorLock(Http);
    LqHttpGetHttpData(Http)->PeriodChangeDigestNonce = NewTime;
    LqSockAcceptorUnlock(Http);
}

LQ_EXTERN_C LqTimeSec LQ_CALL LqHttpGetNonceChangeTime(LqHttp* Http) {
    LqTimeSec Res;
    LqSockAcceptorLock(Http);
    Res = LqHttpGetHttpData(Http)->PeriodChangeDigestNonce;
    LqSockAcceptorUnlock(Http);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqHttpSetLenConveyor(LqHttp* Http, size_t NewLen) {
    LqSockAcceptorLock(Http);
    LqHttpGetHttpData(Http)->MaxLenRspConveyor = NewLen;
    LqSockAcceptorUnlock(Http);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpGetLenConveyor(LqHttp* Http) {
    size_t Res;
    LqSockAcceptorLock(Http);
    Res = LqHttpGetHttpData(Http)->MaxLenRspConveyor;
    LqSockAcceptorUnlock(Http);
    return Res;
}
