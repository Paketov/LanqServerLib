/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSockBuf... - Hi-level async socket.
*/

#include "LqSockBuf.h"
#include "LqAlloc.hpp"
#include "LqStr.h"
#include "LqTime.h"
#include "LqWrkBoss.hpp"
#include "LqAtm.hpp"

static void LQ_CALL _RecvOrSendHandler(LqConn* Connection, LqEvntFlag RetFlags);
static void LQ_CALL _CloseHandler(LqConn* Conn);
static bool LQ_CALL _CmpAddressProc(LqConn* Conn, const void* Address);

static bool LQ_CALL _KickByTimeOutProc(
    LqConn*        Connection,
    LqTimeMillisec CurrentTimeMillisec,
    LqTimeMillisec EstimatedLiveTime
);

static char* LQ_CALL _DebugInfoProc(LqConn* Conn);

static void LQ_CALL _AcceptorAcceptHandler(LqConn* Connection, LqEvntFlag RetFlags);
static void LQ_CALL _AcceptorCloseHandler(LqConn* Conn);
static bool LQ_CALL _AcceptorCmpAddressProc(LqConn* Conn, const void* Address);

static bool LQ_CALL _AcceptorKickByTimeOutProc(
    LqConn*        Connection,
    LqTimeMillisec CurrentTimeMillisec,
    LqTimeMillisec EstimatedLiveTime
);

static char* LQ_CALL _AcceptorDebugInfoProc(LqConn* Conn);

static LqProto _SockBufProto = {
    NULL,
    32750,
    32750,
    32750,
    _RecvOrSendHandler,
    _CloseHandler,
    _CmpAddressProc,
    _KickByTimeOutProc,
    _DebugInfoProc
};

static LqProto _AcceptorProto = {
    NULL,
    0,
    0,
    0,
    _AcceptorAcceptHandler,
    _AcceptorCloseHandler,
    _AcceptorCmpAddressProc,
    _AcceptorKickByTimeOutProc,
    _AcceptorDebugInfoProc
};

static intptr_t _SockWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    int Written;
    if((Written = send((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_WRITE_WOULD_BLOCK : LQFBUF_WRITE_ERROR);
        return -1;
    }
    return Written;
}

static intptr_t _SockReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    int Readed;
    if((Readed = recv((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
        return -1;
    }
    return Readed;
}

static intptr_t _SslWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
#if defined(HAVE_OPENSSL)
    int Written;
lblWriteAgain:
    if((Written = SSL_write((SSL*)Context->UserData, Buf, Size)) <= 0) {
        switch(SSL_get_error((SSL*)Context->UserData, Written)) {
            case SSL_ERROR_NONE: return Written;
            case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_ACCEPT:
                if((Written = SSL_accept((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Written)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblWriteAgain;
            case SSL_ERROR_WANT_CONNECT:
                if((Written = SSL_connect((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Written)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblWriteAgain;
            default: goto lblError;
        }
    }
    return Written;
#endif
lblError:
    Context->Flags |= LQFBUF_WRITE_ERROR;
    return -1;
}

static intptr_t _SslReadProc(LqFbuf* Context, char* Buf, size_t Size) {
#if defined(HAVE_OPENSSL)
    int Readed;
lblReadAgain:
    if((Readed = SSL_read((SSL*)Context->UserData, Buf, Size)) <= 0) {
        switch(SSL_get_error((SSL*)Context->UserData, Readed)) {
            case SSL_ERROR_NONE: return Readed;
            case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_ACCEPT:
                if((Readed = SSL_accept((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Readed)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblReadAgain;
            case SSL_ERROR_WANT_CONNECT:
                if((Readed = SSL_connect((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Readed)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblReadAgain;
            default: goto lblError;
        }
    }
    return Readed;
#endif
lblError:
    Context->Flags |= LQFBUF_READ_ERROR;
    return -1;
}

static intptr_t _SslCloseProc(LqFbuf* Context) {
#if defined(HAVE_OPENSSL)
    int Fd = SSL_get_fd((SSL*)Context->UserData);
    SSL_shutdown((SSL*)Context->UserData);
    SSL_free((SSL*)Context->UserData);
    return closesocket(Fd);
#else
    return 0;
#endif
}

static intptr_t _SockCloseProc(LqFbuf* Context) {
    return closesocket((int)Context->UserData);
}

static intptr_t _EmptySeekProc(LqFbuf*, int64_t, int) {
    return -1;
}

static intptr_t _StreamSeekProc(LqFbuf*, int64_t, int) {
    return -1;
}

static intptr_t _EmptyWriteProc(LqFbuf* Context, char*, size_t) {
    Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK;
    return -1;
}
static intptr_t _EmptyCloseProc(LqFbuf*) {
    return 0;
}

typedef unsigned char RcvFlag;
typedef unsigned char RspFlag;

#define LQSOCKBUF_FLAG_USED ((unsigned char)1)
#define LQSOCKBUF_FLAG_WORK ((unsigned char)2)

#define LQRCV_FLAG_MATCH            ((RcvFlag)0x00)
#define LQRCV_FLAG_READ_REGION      ((RcvFlag)0x01)
#define LQRCV_FLAG_RECV_SREAM       ((RcvFlag)0x02)
#define LQRCV_FLAG_RECV_HANDLE      ((RcvFlag)0x03)

#define LQRSP_FLAG_BUF              ((RspFlag)0x00)
#define LQRSP_FLAG_COMPLETION_PROC  ((RspFlag)0x01)

typedef struct RspElement {
    union {
        struct {
            LqFbuf Buf;
            LqFileSz RspSize;
        };
        struct {
            void(*RspComplete)(void* UserData, LqSockBuf* SockBuf);
            void* UserData;
        } Completion;
    };
    RspElement* Next;
    RspFlag Flag;
} RspElement;

typedef struct RcvElement {
    struct {
        struct {
            char* Fmt;
            int MatchCount;
            size_t MaxSize;
            void(*RcvProc)(void* UserData, LqSockBuf* Buf);
        } Match;
        struct {
            void* Start;
            void* End;
            bool IsPeek;
            intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf);
        } ReadRegion;
        struct {
            LqFbuf* DestStream;
            size_t Size;
            intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf, LqFbuf* DestStream);
        } OutStream;
        struct {
            intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf);
        } RecvData;
    };
    void* Data;
    RcvElement* Next;
    RcvFlag Flag;
} RcvElement;


static bool _EmptyCopyProc(LqFbuf* Dest, LqFbuf*Source) {
    return false;
}

static LqFbufCookie _SocketCookie = {
    _SockReadProc,
    _SockWriteProc,
    _EmptySeekProc,
    _EmptyCopyProc,
    _SockCloseProc
};

static LqFbufCookie _SslCookie = {
    _SslReadProc,
    _SslWriteProc,
    _EmptySeekProc,
    _EmptyCopyProc,
    _SslCloseProc
};

static LqFbuf* _LqSockBufGetLastWriteStream(LqSockBuf* SockBuf) {
    RspElement* NewStream;
    if((SockBuf->Rsp.Last == NULL) ||
       !(((RspElement*)SockBuf->Rsp.Last)->Buf.Flags & LQFBUF_STREAM) ||
       ((SockBuf->RspHeader != NULL) && (SockBuf->Rsp.Last == SockBuf->RspHeader))
       ) {
        if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
            return NULL;
        NewStream->Flag = LQRSP_FLAG_BUF;
        LqFbuf_stream(&NewStream->Buf);
        NewStream->RspSize = -1;
        LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &((RspElement*)SockBuf->Rsp.Last)->Buf;
}

static LqFbuf* _LqSockBufWriteFile(LqSockBuf* SockBuf, int Fd, LqFileSz Size) {
    RspElement* NewStream = NULL;
    if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
        return NULL;
    NewStream->Flag = LQRSP_FLAG_BUF;
    LqFbuf_fdopen(&NewStream->Buf, LQFBUF_FAST_LK, Fd, 0, 4096, 32768);
    NewStream->RspSize = Size;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
}

static LqFbuf* _LqSockBufWriteVirtFile(LqSockBuf* SockBuf, LqFbuf* File, LqFileSz Size) {
    RspElement* NewStream;
    if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
        return NULL;
    NewStream->Flag = LQRSP_FLAG_BUF;
    if(LqFbuf_copy(&NewStream->Buf, File) < 0)
        goto lblErr;
    NewStream->RspSize = Size;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
lblErr:
    LqFastAlloc::Delete(NewStream);
    return NULL;
}

static LqEvntFlag _EvntFlagBySockBuf(LqSockBuf* SockBuf);

/* Recursive lock*/
static void _SockBufLock(LqSockBuf* SockBuf) {
    intptr_t CurThread = LqThreadId();

    while(true) {
        LqAtmLkWr(SockBuf->Lk);
        if((SockBuf->ThreadOwnerId == 0) || (SockBuf->ThreadOwnerId == CurThread)) {
            SockBuf->ThreadOwnerId = CurThread;
            SockBuf->Deep++;
            CurThread = 0;
        }
        LqAtmUlkWr(SockBuf->Lk);
        if(CurThread == 0)
            return;
        LqThreadYield();
    }
}
/* Recursive unlock*/
static void _SockBufUnlock(LqSockBuf* SockBuf) {
    LqAtmLkWr(SockBuf->Lk);
    SockBuf->Deep--;
    if(SockBuf->Deep == 0) {
        SockBuf->ThreadOwnerId = 0;
    }
    LqAtmUlkWr(SockBuf->Lk);
}

LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockBufCreate(int SockFd, void* UserData) {
    LqSockBuf* NewBuf = LqFastAlloc::New<LqSockBuf>();
    if(NewBuf == NULL) {
        lq_errno_set(ENOMEM);
        return NULL;
    }
    LqConnInit(&NewBuf->Conn, SockFd, &_SockBufProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    if(LqFbuf_open_cookie(&NewBuf->Stream, (void*)SockFd, &_SocketCookie, LQFBUF_FAST_LK, 0, 4090, 32768) < 0)
        return false;
    LqListInit(&NewBuf->Rcv);
    LqListInit(&NewBuf->Rsp);
    NewBuf->KeepAlive = LqTimeGetMaxMillisec();
    NewBuf->RspHeader = NULL;
    NewBuf->LastExchangeTime = NewBuf->StartTime = LqTimeGetLocMillisec();
    NewBuf->UserData = UserData;
    NewBuf->Flags = LQSOCKBUF_FLAG_USED;
    NewBuf->ErrHandler = NULL;
    NewBuf->CloseHandler = NULL;
    NewBuf->Cache = NULL;

    LqAtmLkInit(NewBuf->Lk);
    NewBuf->ThreadOwnerId = 0;
    NewBuf->Deep = 0;
    return NewBuf;
}

LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockBufCreateSsl(int SockFd, void* SslCtx, bool IsAccept, void* UserData) {
#if defined(HAVE_OPENSSL) 
    SSL* Ssl;
    int Ret;
    LqFbufFlag Flag = 0;
    LqSockBuf* NewBuf;

    NewBuf = LqFastAlloc::New<LqSockBuf>();
    if(NewBuf == NULL) {
        lq_errno_set(ENOMEM);
        return NULL;
    }

    if((Ssl = SSL_new((SSL_CTX*)SslCtx)) == NULL)
        goto lblErr;
    if(SSL_set_fd(Ssl, SockFd) != 1)
        goto lblErr;
    /* Otherwise OpenSSL lib. generate SSLerr(SSL_F_SSL3_WRITE_PENDING, SSL_R_BAD_WRITE_RETRY); */
    SSL_set_mode(Ssl, SSL_get_mode(Ssl) | SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    if((Ret = ((IsAccept) ? SSL_accept(Ssl) : SSL_connect(Ssl))) <= 0) {
        switch(SSL_get_error(Ssl, Ret)) {
            case SSL_ERROR_WANT_READ: Flag |= LQFBUF_READ_WOULD_BLOCK; break;
            case SSL_ERROR_WANT_WRITE: Flag |= LQFBUF_WRITE_WOULD_BLOCK; break;
            default:
                goto lblErr;
        }
    }

    LqConnInit(&NewBuf->Conn, SockFd, &_SockBufProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    if(LqFbuf_open_cookie(&NewBuf->Stream, Ssl, &_SslCookie, LQFBUF_FAST_LK | Flag, 0, 4090, 32768) < 0)
        goto lblErr;
    LqListInit(&NewBuf->Rcv);
    LqListInit(&NewBuf->Rsp);

    NewBuf->KeepAlive = LqTimeGetMaxMillisec();
    NewBuf->LastExchangeTime = NewBuf->StartTime = LqTimeGetLocMillisec();
    NewBuf->Flags = LQSOCKBUF_FLAG_USED;
    NewBuf->UserData = UserData;
    NewBuf->ErrHandler = NULL;
    NewBuf->CloseHandler = NULL;
    NewBuf->Cache = NULL;

    LqAtmLkInit(NewBuf->Lk);
    NewBuf->ThreadOwnerId = 0;
    NewBuf->Deep = 0;

    return NewBuf;
lblErr:
    if(Ssl != NULL)
        SSL_free(Ssl);
    if(NewBuf != NULL)
        LqFastAlloc::Delete(NewBuf);
#endif
    return NULL;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufDelete(LqSockBuf* SockBuf) {
    _SockBufLock(SockBuf);
    SockBuf->UserData = NULL;
    SockBuf->CloseHandler = NULL;
    SockBuf->ErrHandler = NULL;
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_USED;
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK) {
        LqEvntSetClose(SockBuf);
        _SockBufUnlock(SockBuf);
        return true;
    }
    LqSockBufRspClear(SockBuf);
    LqSockBufRcvClear(SockBuf);
    LqFbuf_close(&SockBuf->Stream);
    if(SockBuf->Cache != NULL)
        LqFcheDelete(SockBuf->Cache);
    LqFastAlloc::Delete(SockBuf);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufSetInstanceCache(LqSockBuf* SockBuf, LqFche* Cache) {
    bool Res = true;
    _SockBufLock(SockBuf);
    if(SockBuf->Cache != NULL) {
        if(Cache != NULL) {
            if(SockBuf->Cache != Cache) {
                LqFcheDelete(SockBuf->Cache);
                SockBuf->Cache = LqFcheCopy(Cache);
                Res = SockBuf->Cache != NULL;
            }
        } else {
            LqFcheDelete(SockBuf->Cache);
            SockBuf->Cache = NULL;
        }
    } else {
        if(Cache != NULL) {
            SockBuf->Cache = LqFcheCopy(Cache);
            Res = SockBuf->Cache != NULL;
        }
    }
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C LqFche* LQ_CALL LqSockBufGetInstanceCache(LqSockBuf* SockBuf) {
    LqFche* Ret = NULL;
    _SockBufLock(SockBuf);
    if(SockBuf->Cache != NULL)
        Ret = LqFcheCopy(SockBuf->Cache);
    _SockBufUnlock(SockBuf);
    return Ret;
}

LQ_EXTERN_C void LQ_CALL LqSockBufSetKeepAlive(LqSockBuf* SockBuf, LqTimeMillisec NewValue) {
    SockBuf->KeepAlive = NewValue;
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqSockBufGetKeepAlive(LqSockBuf* SockBuf) {
    return SockBuf->KeepAlive;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufGoWork(LqSockBuf* SockBuf, void* WrkBoss) {
    bool Res = false;
    _SockBufLock(SockBuf);
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK)
        goto lblOut;
    SockBuf->Conn.Flag |= LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_WR;

    if(WrkBoss == NULL)
        WrkBoss = LqWrkBossGet();
    if(((LqWrkBoss*)WrkBoss)->AddEvntAsync((LqEvntHdr*)&SockBuf->Conn)) {
        SockBuf->Flags |= LQSOCKBUF_FLAG_WORK;
        Res = true;
        goto lblOut;
    }
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyRspCompletion(LqSockBuf* SockBuf, void(*CompletionProc)(void*, LqSockBuf*), void* UserData) {
    RspElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRSP_FLAG_COMPLETION_PROC;
    NewStream->Completion.RspComplete = CompletionProc;
    NewStream->Completion.UserData = UserData;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFile(LqSockBuf* SockBuf, const char* Path) {
    LqFbuf VirtFile;
    bool Res = false;
    _SockBufLock(SockBuf);
    if(SockBuf->Cache != NULL) {
        if(LqFcheUpdateAndRead(SockBuf->Cache, Path, &VirtFile))
            Res = _LqSockBufWriteVirtFile(SockBuf, &VirtFile, -1);
        else
            goto lblOpen;
    } else {
lblOpen:
        int Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_SEQ | LQ_O_NOINHERIT, 0666);
        if(Fd == -1)
            goto lblOut;
        if(_LqSockBufWriteFile(SockBuf, Fd, -1) == NULL) {
            LqFileClose(Fd);
            goto lblOut;
        }
        Res = true;
    }
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFilePart(LqSockBuf* SockBuf, const char* Path, LqFileSz OffsetInFile, LqFileSz Count) {
    LqFbuf VirtFile;
    bool Res = false;
    _SockBufLock(SockBuf);
    if(SockBuf->Cache != NULL) {
        if(LqFcheUpdateAndRead(SockBuf->Cache, Path, &VirtFile)) {
            LqFbuf_read(&VirtFile, NULL, OffsetInFile);
            Res = _LqSockBufWriteVirtFile(SockBuf, &VirtFile, Count);
        } else {
            goto lblOpen;
        }
    } else {
lblOpen:
        int Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_SEQ | LQ_O_NOINHERIT, 0666);
        if(Fd == -1)
            goto lblOut;
        if(!LqSockBufRspFdPart(SockBuf, Fd, OffsetInFile, Count)) {
            LqFileClose(Fd);
            goto lblOut;
        }
        Res = true;
    }
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspStream(LqSockBuf* SockBuf, LqFbuf* File) {
    bool Res = false;
    _SockBufLock(SockBuf);
    if(!(File->Flags & (LQFBUF_POINTER | LQFBUF_STREAM)))
        goto lblOut;
    Res = _LqSockBufWriteVirtFile(SockBuf, File, -1) != NULL;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFd(LqSockBuf* SockBuf, int InFd) {
    bool Res;
    _SockBufLock(SockBuf);
    Res = _LqSockBufWriteFile(SockBuf, InFd, -1) != NULL;
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFdPart(LqSockBuf* SockBuf, int InFd, LqFileSz OffsetInFile, LqFileSz Count) {
    bool Res = false;
    _SockBufLock(SockBuf);
    if(LqFileSeek(InFd, OffsetInFile, LQ_SEEK_SET) == -1)
        goto lblOut;
    Res = _LqSockBufWriteFile(SockBuf, InFd, Count) != NULL;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintf(LqSockBuf* SockBuf, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufPrintfVa(SockBuf, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfVa(LqSockBuf* SockBuf, const char* Fmt, va_list Va) {
    intptr_t Res = -1;
    _SockBufLock(SockBuf);
    LqFbuf* Stream = _LqSockBufGetLastWriteStream(SockBuf);
    if(Stream == NULL)
        goto lblOut;
    Res = LqFbuf_vprintf(Stream, Fmt, Va);
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufWrite(LqSockBuf* SockBuf, const void* Data, size_t SizeData) {
    intptr_t Res = -1;
    _SockBufLock(SockBuf);
    LqFbuf* Stream = _LqSockBufGetLastWriteStream(SockBuf);
    if(Stream == NULL)
        goto lblOut;
    Res = LqFbuf_write(Stream, Data, SizeData);
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspClear(LqSockBuf* SockBuf) {
    RspElement* Buf, *Next;
    _SockBufLock(SockBuf);
    for(Buf = ((RspElement*)SockBuf->Rsp.First); Buf != NULL; Buf = Next) {
        if(Buf->Flag != LQRSP_FLAG_COMPLETION_PROC)
            LqFbuf_close(&Buf->Buf);
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    LqListInit(&SockBuf->Rsp);
    SockBuf->RspHeader = NULL;
    _SockBufUnlock(SockBuf);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspClearAfterHdr(LqSockBuf* SockBuf) {
    RspElement* Buf, *Next;
    _SockBufLock(SockBuf);
    for(Buf = SockBuf->RspHeader ? ((RspElement*)SockBuf->RspHeader)->Next : ((RspElement*)SockBuf->Rsp.First); Buf != NULL; Buf = Next) {
        if(Buf->Flag != LQRSP_FLAG_COMPLETION_PROC)
            LqFbuf_close(&Buf->Buf);
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    if(SockBuf->RspHeader == NULL) {
        LqListInit(&SockBuf->Rsp);
    } else {
        ((RspElement*)SockBuf->RspHeader)->Next = NULL;
        SockBuf->Rsp.Last = SockBuf->RspHeader;
    }
    _SockBufUnlock(SockBuf);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspSetHdr(LqSockBuf* SockBuf) {
    LqFbuf* Stream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if(SockBuf->RspHeader == NULL) {
        Stream = _LqSockBufGetLastWriteStream(SockBuf);
        if(Stream == NULL) {
            lq_errno_set(ENOMEM);
            goto lblOut;
        }
        SockBuf->RspHeader = Stream;
        Res = true;
    } else {
        lq_errno_set(EEXIST);
    }
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqSockBufRspUnsetHdr(LqSockBuf* SockBuf) {
    _SockBufLock(SockBuf);
    SockBuf->RspHeader = NULL;
    _SockBufUnlock(SockBuf);
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfHdr(LqSockBuf* SockBuf, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufPrintfVaHdr(SockBuf, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfVaHdr(LqSockBuf* SockBuf, const char* Fmt, va_list Va) {
    LqFbuf* Stream;
    intptr_t Res = -1;
    _SockBufLock(SockBuf);
    Stream = (LqFbuf*)SockBuf->RspHeader;
    if(Stream == NULL)
        goto lblOut;
    Res = LqFbuf_vprintf(Stream, Fmt, Va);
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufWriteHdr(LqSockBuf* SockBuf, const void* Data, size_t SizeData) {
    LqFbuf* Stream;
    intptr_t Res = -1;
    _SockBufLock(SockBuf);
    Stream = (LqFbuf*)SockBuf->RspHeader;
    if(Stream == NULL)
        goto lblOut;
    Res = LqFbuf_write(Stream, Data, SizeData);
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* SockBuf) {
    RspElement* Buf;
    LqFileSz Res = 0, b;
    LqFileStat FileStat;
    size_t a;
    int Fd;

    _SockBufLock(SockBuf);
    for(Buf = SockBuf->RspHeader ? ((RspElement*)SockBuf->RspHeader)->Next : ((RspElement*)SockBuf->Rsp.First); Buf != NULL; Buf = Buf->Next) {
        if(Buf->Flag == LQRSP_FLAG_COMPLETION_PROC)
            continue;
        if(Buf->RspSize >= 0) {
            Res += Buf->RspSize;
        } else if(Buf->Buf.Flags & (LQFBUF_POINTER | LQFBUF_STREAM)) {
            Res += LqFbuf_sizes(&Buf->Buf, NULL, NULL);
        } else {
            LqFbuf_sizes(&Buf->Buf, NULL, &a);
            Fd = (int)Buf->Buf.UserData;
            b = LqFileTell(Fd);
            if(b < 0) {
                Res += a;
            } else {
                FileStat.Size = a;
                LqFileGetStatByFd(Fd, &FileStat);
                Res += (FileStat.Size - a);
            }
        }
    }
    _SockBufUnlock(SockBuf);
    return Res;
}

//////////////////////Rcv


LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenMatch(LqSockBuf* SockBuf, void* UserData, void(*RcvProc)(void* UserData, LqSockBuf* Buf), const char* Fmt, int MatchCount, size_t MaxSize) {
    bool Res = false;
    RcvElement* NewStream;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_MATCH;
    NewStream->Match.Fmt = LqStrDuplicate(Fmt);
    NewStream->Match.MatchCount = MatchCount;
    NewStream->Match.MaxSize = MaxSize;
    NewStream->Match.RcvProc = RcvProc;
    NewStream->Data = UserData;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSockBufScanf(LqSockBuf* SockBuf, bool IsPeek, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufScanfVa(SockBuf, IsPeek, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSockBufScanfVa(LqSockBuf* SockBuf, bool IsPeek, const char* Fmt, va_list Va) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_vscanf(&SockBuf->Stream, (IsPeek) ? LQFRBUF_SCANF_PEEK : 0, Fmt, Va);
    if(SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenCompleteRead(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf), void* Dest, size_t Size) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_READ_REGION;
    NewStream->ReadRegion.Start = Dest;
    NewStream->ReadRegion.End = ((char*)Dest) + Size;
    NewStream->ReadRegion.RcvProc = RcvProc;
    NewStream->Data = UserData;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSockBufRead(LqSockBuf* SockBuf, void* Dest, size_t Size) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_read(&SockBuf->Stream, Dest, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenCompleteRecvStream(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf, LqFbuf* DestStream), LqFbuf* DestStream, size_t Size) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        return false;
    NewStream->Flag = LQRCV_FLAG_RECV_SREAM;
    NewStream->OutStream.DestStream = DestStream;
    NewStream->OutStream.Size = Size;
    NewStream->OutStream.RcvProc = RcvProc;
    NewStream->Data = UserData;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSockBufReadInStream(LqSockBuf* SockBuf, LqFbuf* Dest, size_t Size) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_transfer(Dest, &SockBuf->Stream, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenCompleteRecvData(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf)) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        return false;
    NewStream->Flag = LQRCV_FLAG_RECV_HANDLE;
    NewStream->RecvData.RcvProc = RcvProc;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Conn.Flag & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Conn.Flag & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* SockBuf) {
    RcvElement* Buf, *Prev = NULL;
    bool Res = true;
    _SockBufLock(SockBuf);
    for(Buf = ((RcvElement*)SockBuf->Rcv.First); Buf != NULL; Buf = Buf->Next) {
        if(Buf == SockBuf->Rcv.Last) {
            if(Buf->Flag == LQRCV_FLAG_MATCH)
                free(Buf->Match.Fmt);
            LqFastAlloc::Delete(Buf);
            if(Prev == NULL)
                SockBuf->Rcv.First = NULL;
            else
                Prev->Next = NULL;
            SockBuf->Rcv.Last = Prev;
            goto lblOut;
        }
        Prev = Buf;
    }
    Res = false;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvClear(LqSockBuf* SockBuf) {
    RcvElement* Buf, *Next;
    _SockBufLock(SockBuf);
    for(Buf = ((RcvElement*)SockBuf->Rcv.First); Buf != NULL; Buf = Next) {
        if(Buf->Flag == LQRCV_FLAG_MATCH) {
            free(Buf->Match.Fmt);
        }
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    LqListInit(&SockBuf->Rcv);
    _SockBufUnlock(SockBuf);
    return true;
}

LQ_EXTERN_C void LQ_CALL LqSockBufFlush(LqSockBuf* SockBuf) {
    _RecvOrSendHandler(&SockBuf->Conn, LQEVNT_FLAG_WR | LQEVNT_FLAG_RD);
}

LQ_EXTERN_C bool LQ_CALL LqSockBufGetRemoteAddr(LqSockBuf* SockBuf, LqConnAddr* Dest) {
    bool Res;
    socklen_t Len = sizeof(LqConnAddr);
    _SockBufLock(SockBuf);
    Res = getpeername(SockBuf->Conn.Fd, &Dest->Addr, &Len) == 0;
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufGetLocAddr(LqSockBuf* SockBuf, LqConnAddr* Dest) {
    bool Res;
    socklen_t Len = sizeof(LqConnAddr);
    _SockBufLock(SockBuf);
    Res = getsockname(SockBuf->Conn.Fd, &Dest->Addr, &Len) == 0;
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufGetRemoteAddrStr(LqSockBuf* SockBuf, char* Dest, size_t DestSize) {
    bool Res = false;
    socklen_t Len = sizeof(LqConnAddr);
    LqConnAddr Addr;
    _SockBufLock(SockBuf);
    if(getpeername(SockBuf->Conn.Fd, &Addr.Addr, &Len) == 0) {
        Res = inet_ntop(
                Addr.Addr.sa_family,
                (Addr.Addr.sa_family == AF_INET6) ? (void*)&Addr.AddrInet6.sin6_addr : (void*)&Addr.AddrInet.sin_addr,
                Dest,
                DestSize
              ) != NULL;
    }
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufGetLocAddrStr(LqSockBuf* SockBuf, char* Dest, size_t DestSize) {
    bool Res = false;
    socklen_t Len = sizeof(LqConnAddr);
    LqConnAddr Addr;
    _SockBufLock(SockBuf);
    if(getsockname(SockBuf->Conn.Fd, &Addr.Addr, &Len) == 0) {
        Res = inet_ntop(
                Addr.Addr.sa_family,
                (Addr.Addr.sa_family == AF_INET6) ? (void*)&Addr.AddrInet6.sin6_addr : (void*)&Addr.AddrInet.sin_addr,
                Dest,
                DestSize
              ) != NULL;
    }
    _SockBufUnlock(SockBuf);
    return Res;
}

//////////////////////////////////Hndlrs

static void LQ_CALL _RecvOrSendHandler(LqConn* Sock, LqEvntFlag RetFlags) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    RspElement* WrElem;
    RcvElement *RdElem;
    size_t Size;
    LqEvntFlag ConnFlag = Sock->Flag;
    LqFbufFlag FbufFlags = 0;
    intptr_t Readed2, Readed;
    void* UserData, *UserData2;
    void(*Proc)(void* UserData, LqSockBuf* Buf);
    intptr_t(*Proc2)(void* UserData, LqSockBuf* Buf);
    intptr_t(*Proc3)(void* UserData, LqSockBuf* Buf, LqFbuf* DestStream);
    LqFbuf* Fbuf;
    intptr_t Sended;
    bool NotWrite = false;

    _SockBufLock(SockBuf);
    SockBuf->LastExchangeTime = LqTimeGetLocMillisec();

    if(RetFlags & LQEVNT_FLAG_ERR) {
        if(SockBuf->ErrHandler) {
            SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_UNKNOWN_SOCKET);
            if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                goto lblOut2;
        } else {
            LqEvntSetClose(SockBuf);
            goto lblOut2;
        }
    }
    if(RetFlags & LQEVNT_FLAG_WR) {//Can write
lblWriteAgain:
        /* Write first stream from list*/
        if((WrElem = LqListFirst(&SockBuf->Rsp, RspElement)) == NULL) {
            LqFbuf_flush(&SockBuf->Stream);
            goto lblTryRead;
        }
        if(WrElem->Flag == LQRSP_FLAG_COMPLETION_PROC) {
            /* If this element is just completeon proc*/
            Proc = WrElem->Completion.RspComplete;
            UserData = WrElem->Completion.UserData;
            LqListRemove(&SockBuf->Rsp, RspElement);
            LqFastAlloc::Delete(WrElem);
            Proc(UserData, SockBuf);
            if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                goto lblOut2;
            goto lblWriteAgain;
        } else if(WrElem->Flag == LQRSP_FLAG_BUF) {
            if((SockBuf->RspHeader != NULL) && (SockBuf->RspHeader == LqListFirst(&SockBuf->Rsp, RspElement))) {
                NotWrite = true;
                goto lblTryRead;
            }
            Sended = LqFbuf_transfer(&SockBuf->Stream, &WrElem->Buf, (WrElem->RspSize >= 0) ? WrElem->RspSize : Sock->Proto->MaxSendInSingleTime);
            if(WrElem->RspSize >= 0)
                WrElem->RspSize -= lq_max(Sended, 0);
            FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
            if((WrElem->Buf.Flags & LQFBUF_READ_EOF) || (WrElem->RspSize == 0)) {
lblErrWrite:
                LqFbuf_close(&WrElem->Buf);
                LqListRemove(&SockBuf->Rsp, RspElement);
                LqFastAlloc::Delete(WrElem);
                goto lblWriteAgain;
            }
            if(WrElem->Buf.Flags & (LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK)) {
                UserData = WrElem->Next;
                UserData2 = WrElem->Buf.UserData;
                if(SockBuf->ErrHandler) {
                    SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_OUTPUT_DATA);
                    if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                        goto lblOut2;
                }
                if((WrElem == LqListFirst(&SockBuf->Rsp, RspElement)) && (WrElem->Next == UserData) && (UserData2 == WrElem->Buf.UserData))
                    goto lblErrWrite;
                else
                    goto lblWriteAgain;
            }
        }
    }
lblTryRead:
    if(RetFlags & LQEVNT_FLAG_RD) {//Can read
lblReadAgain:
        if((RdElem = LqListFirst(&SockBuf->Rcv, RcvElement)) == NULL) {
            LqFbuf_peek(&SockBuf->Stream, NULL, 1);
            goto lblOut;
        }
        switch(RdElem->Flag) {
            case LQRCV_FLAG_MATCH:
                Readed2 = LqFbuf_scanf(&SockBuf->Stream, LQFRBUF_SCANF_PEEK, RdElem->Match.Fmt);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                Readed2 = lq_max(Readed2, 0);
                if((Readed2 >= RdElem->Match.MatchCount) || (SockBuf->Stream.InBuf.Len >= RdElem->Match.MaxSize)) {
                    UserData = RdElem->Data;
                    Proc = RdElem->Match.RcvProc;
                    free(RdElem->Match.Fmt);
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    if(Proc != NULL) {
                        Proc(UserData, SockBuf);
                        if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                            goto lblOut2;
                    }
                    goto lblReadAgain;
                }
                break;
            case LQRCV_FLAG_READ_REGION:
                Readed2 = (char*)RdElem->ReadRegion.End - (char*)RdElem->ReadRegion.Start;
                Readed = (RdElem->ReadRegion.IsPeek) ? LqFbuf_peek(&SockBuf->Stream, RdElem->ReadRegion.Start, Readed2) : LqFbuf_read(&SockBuf->Stream, RdElem->ReadRegion.Start, Readed2);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                Readed = lq_max(Readed, 0);
                if(Readed >= Readed2) {
                    UserData = RdElem->Data;
                    Proc2 = RdElem->ReadRegion.RcvProc;
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    if(Proc2 != NULL) {
                        Proc2(UserData, SockBuf);
                        if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                            goto lblOut2;
                    }
                    goto lblReadAgain;
                }
                if(RdElem->ReadRegion.Start != NULL)
                    RdElem->ReadRegion.Start = ((char*)RdElem->ReadRegion.Start) + Readed;
                else
                    RdElem->ReadRegion.End = ((char*)RdElem->ReadRegion.End) - Readed;
                break;
            case LQRCV_FLAG_RECV_SREAM:
                Readed = LqFbuf_transfer(RdElem->OutStream.DestStream, &SockBuf->Stream, RdElem->OutStream.Size);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                Readed = lq_max(Readed, 0);
                RdElem->OutStream.Size -= Readed;
                if(RdElem->OutStream.Size <= 0) {
                    UserData = RdElem->Data;
                    Proc3 = RdElem->OutStream.RcvProc;
                    Fbuf = RdElem->OutStream.DestStream;
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    if(Proc3 != NULL) {
                        Proc3(UserData, SockBuf, Fbuf);
                        if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                            goto lblOut2;
                    }
                    goto lblReadAgain;
                }
                break;
            case LQRCV_FLAG_RECV_HANDLE:
                UserData = RdElem->Data;
                Proc2 = RdElem->RecvData.RcvProc;
                UserData2 = RdElem->Next;
                Readed = RdElem->RecvData.RcvProc(RdElem->Data, SockBuf);
                if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                    goto lblOut2;
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed < 0) {
                    if((RdElem == LqListFirst(&SockBuf->Rcv, RcvElement)) &&
                        (UserData == RdElem->Data) &&
                       (Proc2 == RdElem->RecvData.RcvProc) &&
                       (UserData2 == RdElem->Next)
                       ) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                        LqFastAlloc::Delete(RdElem);
                    }
                    goto lblReadAgain;
                }
                break;
        }
    }
lblOut:
    if(FbufFlags & (LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR)) {
        if(SockBuf->ErrHandler) {
            SockBuf->ErrHandler(SockBuf, ((FbufFlags & LQFBUF_WRITE_ERROR) ? LQSOCKBUF_ERR_WRITE_SOCKET : 0) | ((FbufFlags & LQFBUF_READ_ERROR) ? LQSOCKBUF_ERR_READ_SOCKET : 0));
            if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                goto lblOut2;
        } else {
            LqEvntSetClose(SockBuf);
            goto lblOut2;
        }
    }
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK) {
        LqFbuf_sizes(&SockBuf->Stream, &Size, NULL);
        if(!NotWrite && ((LqListFirst(&SockBuf->Rsp, RspElement) != NULL) || (Size > 0) || (FbufFlags & LQFBUF_WRITE_WOULD_BLOCK)))
            ConnFlag |= LQEVNT_FLAG_WR;
        else
            ConnFlag &= ~LQEVNT_FLAG_WR;

        if((LqListFirst(&SockBuf->Rcv, RcvElement) != NULL) || (FbufFlags & LQFBUF_READ_WOULD_BLOCK))
            ConnFlag |= LQEVNT_FLAG_RD;
        else
            ConnFlag &= ~LQEVNT_FLAG_RD;
        LqEvntSetFlags(Sock, ConnFlag, 0);
    }
lblOut2:
    _SockBufUnlock(SockBuf);
}

static void LQ_CALL _CloseHandler(LqConn* Sock) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    _SockBufLock(SockBuf);

    if(SockBuf->CloseHandler != NULL)
        SockBuf->CloseHandler(SockBuf);
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_WORK;
    if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED)) {
        LqSockBufDelete(SockBuf);
    } else {
        _SockBufUnlock(SockBuf);
    }
}

static bool LQ_CALL _CmpAddressProc(LqConn* Conn, const void* Address) {
    LqConnAddr Addr;
    socklen_t Len = sizeof(Addr);
    if(getpeername(Conn->Fd, &Addr.Addr, &Len) < 0)
        return false;
    if(Addr.Addr.sa_family != ((sockaddr*)Address)->sa_family)
        return false;
    switch(Addr.Addr.sa_family) {
        case AF_INET: return memcmp(&Addr.AddrInet.sin_addr, &((sockaddr_in*)Address)->sin_addr, sizeof(((sockaddr_in*)Address)->sin_addr)) == 0;
        case AF_INET6: return memcmp(&Addr.AddrInet6.sin6_addr, &((sockaddr_in6*)Address)->sin6_addr, sizeof(((sockaddr_in6*)Address)->sin6_addr)) == 0;
    }
    return false;
}

static bool LQ_CALL _KickByTimeOutProc(
    LqConn*        Sock,
    LqTimeMillisec CurrentTime,
    LqTimeMillisec EstimatedLiveTime
) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    LqTimeMillisec TimeDiff = CurrentTime - SockBuf->LastExchangeTime;
    return TimeDiff > SockBuf->KeepAlive;
}

static char* LQ_CALL _DebugInfoProc(LqConn* Conn) {
    return NULL;
}

static LqEvntFlag _EvntFlagBySockBuf(LqSockBuf* SockBuf) {
    return ((LqListFirst(&SockBuf->Rsp, RspElement) != NULL) ? LQEVNT_FLAG_WR : 0) |
        ((LqListFirst(&SockBuf->Rcv, RcvElement) != NULL) ? LQEVNT_FLAG_RD : 0);
}



//////////////////////////////////
///////Acceptor
//////////////////////////////////


/* Recursive lock*/
static void _SockAcceptorLock(LqSockAcceptor* SockBuf) {
    intptr_t CurThread = LqThreadId();

    while(true) {
        LqAtmLkWr(SockBuf->Lk);
        if((SockBuf->ThreadOwnerId == 0) || (SockBuf->ThreadOwnerId == CurThread)) {
            SockBuf->ThreadOwnerId = CurThread;
            SockBuf->Deep++;
            CurThread = 0;
        }
        LqAtmUlkWr(SockBuf->Lk);
        if(CurThread == 0)
            return;
        LqThreadYield();
    }
}
/* Recursive unlock*/
static void _SockAcceptorUnlock(LqSockAcceptor* SockBuf) {
    LqAtmLkWr(SockBuf->Lk);
    SockBuf->Deep--;
    if(SockBuf->Deep == 0) {
        SockBuf->ThreadOwnerId = 0;
    }
    LqAtmUlkWr(SockBuf->Lk);
}

LQ_EXTERN_C LqSockAcceptor* LQ_CALL LqSockAcceptorCreate(const char* Host, const char* Port, int RouteProto, int SockType, int TransportProto, int MaxConnections, bool IsNonBlock, void* UserData) {
    LqSockAcceptor* NewAcceptor;
    int Fd = LqConnBind(Host, Port, RouteProto, SockType, TransportProto, MaxConnections, IsNonBlock);
    if(Fd == -1)
        return NULL;
    NewAcceptor = LqFastAlloc::New<LqSockAcceptor>();
    if(NewAcceptor == NULL) {
        closesocket(Fd);
        lq_errno_set(ENOMEM);
        return NULL;
    }
    LqConnInit(&NewAcceptor->Conn, Fd, &_AcceptorProto, LQEVNT_FLAG_ACCEPT | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    NewAcceptor->AcceptProc = NULL;
    NewAcceptor->CloseHandler = NULL;
    NewAcceptor->Flags = LQSOCKBUF_FLAG_USED;
    NewAcceptor->UserData = UserData;

    LqAtmLkInit(NewAcceptor->Lk);
    NewAcceptor->ThreadOwnerId = 0;
    NewAcceptor->Deep = 0;

    return NewAcceptor;
}

LQ_EXTERN_C bool LQ_CALL LqSockAcceptorGoWork(LqSockAcceptor* SockAcceptor, void* WrkBoss) {
    bool Res = false;
    _SockAcceptorLock(SockAcceptor);
    if(SockAcceptor->Flags & LQSOCKBUF_FLAG_WORK)
        goto lblOut;
    if(WrkBoss == NULL)
        WrkBoss = LqWrkBossGet();
    if(((LqWrkBoss*)WrkBoss)->AddEvntAsync((LqEvntHdr*)&SockAcceptor->Conn)) {
        SockAcceptor->Flags |= LQSOCKBUF_FLAG_WORK;
        Res = true;
        goto lblOut;
    }
lblOut:
    _SockAcceptorUnlock(SockAcceptor);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockAcceptorDelete(LqSockAcceptor* SockAcceptor) {
    _SockAcceptorLock(SockAcceptor);
    SockAcceptor->UserData = NULL;
    SockAcceptor->CloseHandler = NULL;
    SockAcceptor->AcceptProc = NULL;
    SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_USED;
    if(SockAcceptor->Flags & LQSOCKBUF_FLAG_WORK) {
        LqEvntSetClose(SockAcceptor);
        _SockAcceptorUnlock(SockAcceptor);
        return true;
    }
    closesocket(SockAcceptor->Conn.Fd);
    if(SockAcceptor->Cache != NULL)
        LqFcheDelete(SockAcceptor->Cache);
    LqFastAlloc::Delete(SockAcceptor);
    return true;
}


LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockAcceptorAccept(LqSockAcceptor* SockAcceptor, void* UserData) {
    int Fd;
    LqSockBuf* Res = NULL;
    _SockAcceptorLock(SockAcceptor);
    if((Fd = accept(SockAcceptor->Conn.Fd, NULL, NULL)) != -1) {
        Res = LqSockBufCreate(Fd, UserData);
        if(Res == NULL) {
            closesocket(Fd);
        } else {
            if(SockAcceptor->Cache != NULL)
                Res->Cache = LqFcheCopy(SockAcceptor->Cache);
        }
    }
    _SockAcceptorUnlock(SockAcceptor);
    return Res;
}

LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockAcceptorAcceptSsl(LqSockAcceptor* SockAcceptor, void* UserData, void* SslCtx) {
    int Fd;
    LqSockBuf* Res = NULL;
    _SockAcceptorLock(SockAcceptor);
    if((Fd = accept(SockAcceptor->Conn.Fd, NULL, NULL)) != -1) {
        Res = LqSockBufCreateSsl(Fd, SslCtx, true, UserData);
        if(Res == NULL) {
            closesocket(Fd);
        } else {
            if(SockAcceptor->Cache != NULL)
                Res->Cache = LqFcheCopy(SockAcceptor->Cache);
        }
    }
    _SockAcceptorUnlock(SockAcceptor);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockAcceptorSetInstanceCache(LqSockAcceptor* SockAcceptor, LqFche* Cache) {
    bool Res = true;
    _SockAcceptorLock(SockAcceptor);
    if(SockAcceptor->Cache != NULL) {
        if(Cache != NULL) {
            if(SockAcceptor->Cache != Cache) {
                LqFcheDelete(SockAcceptor->Cache);
                SockAcceptor->Cache = LqFcheCopy(Cache);
                Res = SockAcceptor->Cache != NULL;
            }
        } else {
            LqFcheDelete(SockAcceptor->Cache);
            SockAcceptor->Cache = NULL;
        }
    } else {
        if(Cache != NULL) {
            SockAcceptor->Cache = LqFcheCopy(Cache);
            Res = SockAcceptor->Cache != NULL;
        }
    }
    _SockAcceptorUnlock(SockAcceptor);
    return Res;
}

LQ_EXTERN_C LqFche* LQ_CALL LqSockAcceptorGetInstanceCache(LqSockAcceptor* SockAcceptor) {
    LqFche* Ret = NULL;
    _SockAcceptorLock(SockAcceptor);
    if(SockAcceptor->Cache != NULL)
        Ret = LqFcheCopy(SockAcceptor->Cache);
    _SockAcceptorUnlock(SockAcceptor);
    return Ret;
}


static void LQ_CALL _AcceptorAcceptHandler(LqConn* Connection, LqEvntFlag RetFlags) {
    LqSockAcceptor* SockAcceptor = (LqSockAcceptor*)Connection;
    _SockAcceptorLock(SockAcceptor);
    if(RetFlags & LQEVNT_FLAG_ERR)
        LqEvntSetClose(SockAcceptor);
    if(RetFlags & LQEVNT_FLAG_ACCEPT)
        SockAcceptor->AcceptProc(SockAcceptor);
    _SockAcceptorUnlock(SockAcceptor);
}

static void LQ_CALL _AcceptorCloseHandler(LqConn* Conn) {
    LqSockAcceptor* SockAcceptor = (LqSockAcceptor*)Conn;
    _SockAcceptorLock(SockAcceptor);
    if(SockAcceptor->CloseHandler != NULL)
        SockAcceptor->CloseHandler(SockAcceptor);
    SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_WORK;
    if(!(SockAcceptor->Flags & LQSOCKBUF_FLAG_USED))
        LqSockAcceptorDelete(SockAcceptor);
    else
        _SockAcceptorUnlock(SockAcceptor);
}

static bool LQ_CALL _AcceptorCmpAddressProc(LqConn* Conn, const void* Address) { return false; }

static bool LQ_CALL _AcceptorKickByTimeOutProc(
    LqConn*        Connection,
    LqTimeMillisec CurrentTimeMillisec,
    LqTimeMillisec EstimatedLiveTime
) {
    return false;
}
static char* LQ_CALL _AcceptorDebugInfoProc(LqConn* Conn) { return NULL; }


#define __METHOD_DECLS__
#include "LqAlloc.hpp"