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


typedef unsigned char RcvFlag;
typedef unsigned char RspFlag;

#define LQSOCKBUF_FLAG_USED     ((unsigned char)1)
#define LQSOCKBUF_FLAG_WORK     ((unsigned char)2)
#define LQSOCKBUF_FLAG_AUTO_HDR ((unsigned char)4)
#define LQSOCKBUF_FLAG_IN_HANDLER     ((unsigned char)8)

#define LQRCV_FLAG_MATCH            ((RcvFlag)0x00)
#define LQRCV_FLAG_READ_REGION      ((RcvFlag)0x01)
#define LQRCV_FLAG_RECV_IN_FBUF       ((RcvFlag)0x02)
#define LQRCV_FLAG_RECV_PULSE_READ      ((RcvFlag)0x03)
#define LQRCV_FLAG_RECV_IN_FBUF_TO_SEQ       ((RcvFlag)0x04)
#define LQRCV_FLAG_COMPLETION_PROC       ((RcvFlag)0x05)
#define LQRCV_FLAG_WAIT_LENGTH       ((RcvFlag)0x06)


#define LQRSP_FLAG_BUF              ((RspFlag)0x00)
#define LQRSP_FLAG_COMPLETION_PROC  ((RspFlag)0x01)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct RspElement {
    union {
        struct {
            LqFbuf Buf;
            LqFileSz RspSize;
        };
        struct {
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData);
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
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData);
        } Match;
        struct {
            void* Dest;
            size_t Written;
            size_t MaxSize;
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* Dest, size_t Written, void* UserData);
            bool IsPeek;
        } ReadRegion;
        struct {
            LqFbuf* DestStream;
            LqFileSz Size;
            LqFileSz MaxSize;
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, LqFbuf* DestStream, LqFileSz WrittenSz, void* UserData);
        } RcvFbuf;
        struct {
            LqFbuf* DestStream;
            char* Seq;
            size_t SeqSize;
            LqFileSz Size;
            LqFileSz MaxSize;
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, LqFbuf* DestStream, LqFileSz WrittenSz, void* UserData, bool IsFoundSeq);
            bool IsCaseI;
        } RcvFbufToSeq;
        struct {
            intptr_t(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData);
        } PulseRead;
        struct {
            size_t Length;
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData);
        } WaitLen;
        struct {
            void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData);
        } Completion;
    };
    void* Data;
    RcvElement* Next;
    RcvFlag Flag;
} RcvElement;
#pragma pack(pop)

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

LqProto ___SockBufProto = {
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

static intptr_t LQ_CALL _SockWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    int Written;
    LqSockBuf* SockBuf = (LqSockBuf*)((char*)Context - (uintptr_t)&((LqSockBuf*)0)->Stream);
    if(SockBuf->RWPortion < Size)
        Size = SockBuf->RWPortion;
    if(Size <= ((size_t)0)) {
        Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK;
        return -((intptr_t)1);
    }
    if((Written = send((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_WRITE_WOULD_BLOCK : LQFBUF_WRITE_ERROR);
        return -((intptr_t)1);
    }
    SockBuf->RWPortion -= Written;
    SockBuf->WriteOffset += ((int64_t)Written);
    return Written;
}

static intptr_t LQ_CALL _SockReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    int Readed;
    LqSockBuf* SockBuf = (LqSockBuf*)((char*)Context - (uintptr_t)&((LqSockBuf*)0)->Stream);
    if(SockBuf->RWPortion < Size)
        Size = SockBuf->RWPortion;
    if(Size <= ((size_t)0)) {
        Context->Flags |= LQFBUF_READ_WOULD_BLOCK;
        return -((intptr_t)1);
    }
    if((Readed = recv((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
        return -((intptr_t)1);
    }
    SockBuf->RWPortion -= Readed;
    SockBuf->ReadOffset += ((int64_t)Readed);
    return Readed;
}

static intptr_t LQ_CALL _SslWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
#if defined(HAVE_OPENSSL)
    int Written;
    LqSockBuf* SockBuf = (LqSockBuf*)((char*)Context - (uintptr_t)&((LqSockBuf*)0)->Stream);
    if(SockBuf->RWPortion < Size)
        Size = SockBuf->RWPortion;
    if(Size <= ((size_t)0)) {
        Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK;
        return -((intptr_t)1);
    }
lblWriteAgain:
    if((Written = SSL_write((SSL*)Context->UserData, Buf, Size)) <= 0) {
        switch(SSL_get_error((SSL*)Context->UserData, Written)) {
            case SSL_ERROR_NONE: goto lblOut;
            case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -((intptr_t)1);
            case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -((intptr_t)1);
            case SSL_ERROR_WANT_ACCEPT:
                if((Written = SSL_accept((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Written)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -((intptr_t)1);
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -((intptr_t)1);
                        default: goto lblError;
                    }
                } else
                    goto lblWriteAgain;
            case SSL_ERROR_WANT_CONNECT:
                if((Written = SSL_connect((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Written)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -((intptr_t)1);
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -((intptr_t)1);
                        default: goto lblError;
                    }
                } else
                    goto lblWriteAgain;
            default: goto lblError;
        }
    }
lblOut:
    SockBuf->RWPortion -= Written;
    SockBuf->WriteOffset += ((int64_t)Written);
    return Written;
#endif
lblError:
    Context->Flags |= LQFBUF_WRITE_ERROR;
    return -((intptr_t)1);
}

static intptr_t LQ_CALL _SslReadProc(LqFbuf* Context, char* Buf, size_t Size) {
#if defined(HAVE_OPENSSL)
    int Readed;
    LqSockBuf* SockBuf = (LqSockBuf*)((char*)Context - (uintptr_t)&((LqSockBuf*)0)->Stream);
    if(SockBuf->RWPortion < Size)
        Size = SockBuf->RWPortion;
    if(Size <= ((size_t)0)) {
        Context->Flags |= LQFBUF_READ_WOULD_BLOCK;
        return -((intptr_t)1);
    }
lblReadAgain:
    if((Readed = SSL_read((SSL*)Context->UserData, Buf, Size)) <= 0) {
        switch(SSL_get_error((SSL*)Context->UserData, Readed)) {
            case SSL_ERROR_NONE: goto lblOut;
            case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -((intptr_t)1);
            case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -((intptr_t)1);
            case SSL_ERROR_WANT_ACCEPT:
                if((Readed = SSL_accept((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Readed)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -((intptr_t)1);
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -((intptr_t)1);
                        default: goto lblError;
                    }
                } else
                    goto lblReadAgain;
            case SSL_ERROR_WANT_CONNECT:
                if((Readed = SSL_connect((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error((SSL*)Context->UserData, Readed)) {
                        case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -((intptr_t)1);
                        case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -((intptr_t)1);
                        default: goto lblError;
                    }
                } else
                    goto lblReadAgain;
            default: goto lblError;
        }
    }
lblOut:
    SockBuf->RWPortion -= Readed;
    SockBuf->ReadOffset += ((int64_t)Readed);
    return Readed;
#endif
lblError:
    Context->Flags |= LQFBUF_READ_ERROR;
    return -((intptr_t)1);
}

static intptr_t LQ_CALL _SslCloseProc(LqFbuf* Context) {
#if defined(HAVE_OPENSSL)
    int Fd = SSL_get_fd((SSL*)Context->UserData);
    SSL_shutdown((SSL*)Context->UserData);
    SSL_free((SSL*)Context->UserData);
    return closesocket(Fd);
#else
    return ((intptr_t)0);
#endif
}

static intptr_t LQ_CALL _SockCloseProc(LqFbuf* Context) {
    return closesocket((int)Context->UserData);
}

static intptr_t LQ_CALL _EmptySeekProc(LqFbuf*, int64_t, int) {
    return -((intptr_t)1);
}

static intptr_t LQ_CALL _StreamSeekProc(LqFbuf*, int64_t, int) {
    return -((intptr_t)1);
}

static intptr_t LQ_CALL _EmptyWriteProc(LqFbuf* Context, char*, size_t) {
    Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK;
    return -((intptr_t)1);
}
static intptr_t LQ_CALL _EmptyCloseProc(LqFbuf*) {
    return ((intptr_t)0);
}

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
        NewStream->RspSize = -((LqFileSz)1);
        LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &((RspElement*)SockBuf->Rsp.Last)->Buf;
}

static LqFbuf* _LqSockBufWriteFile(LqSockBuf* SockBuf, int Fd, LqFileSz Size) {
    RspElement* NewStream = NULL;
    if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
        return NULL;
    NewStream->Flag = LQRSP_FLAG_BUF;
    LqFbuf_fdopen(&NewStream->Buf, LQFBUF_FAST_LK, Fd, ((intptr_t)0), ((intptr_t)4096), ((intptr_t)32768));
    NewStream->RspSize = Size;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
}

static LqFbuf* _LqSockBufWriteVirtFile(LqSockBuf* SockBuf, LqFbuf* File, LqFileSz Size) {
    RspElement* NewStream;
    if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
        return NULL;
    NewStream->Flag = LQRSP_FLAG_BUF;
    if(LqFbuf_copy(&NewStream->Buf, File) < ((intptr_t)0))
        goto lblErr;
    NewStream->RspSize = Size;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
lblErr:
    LqFastAlloc::Delete(NewStream);
    return NULL;
}
static bool _LqSockBufRspClear(LqSockBuf* SockBuf) {
    RspElement* Buf, *Next;
    for(Buf = ((RspElement*)SockBuf->Rsp.First); Buf != NULL; Buf = Next) {
        if(Buf->Flag != LQRSP_FLAG_COMPLETION_PROC)
            LqFbuf_close(&Buf->Buf);
        else if(Buf->Completion.CompleteOrCancelProc)
            Buf->Completion.CompleteOrCancelProc(NULL, Buf->Completion.UserData);
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    LqListInit(&SockBuf->Rsp);
    SockBuf->RspHeader = NULL;
    return true;
}

static bool _LqSockBufRcvClear(LqSockBuf* SockBuf) {
    RcvElement* Buf, *Next;
    bool Queue1 = true;
    Buf = (RcvElement*)SockBuf->Rcv.First;
lblAgain:
    for(; Buf != NULL; Buf = Next) {
        switch(Buf->Flag) {
            case LQRCV_FLAG_MATCH:
                free(Buf->Match.Fmt);
                if(Buf->Match.CompleteOrCancelProc)
                    Buf->Match.CompleteOrCancelProc(NULL, Buf->Data);
                break;
            case LQRCV_FLAG_RECV_IN_FBUF:
                if(Buf->RcvFbuf.CompleteOrCancelProc)
                    Buf->RcvFbuf.CompleteOrCancelProc(NULL, Buf->RcvFbuf.DestStream, Buf->RcvFbuf.MaxSize - Buf->RcvFbuf.Size, Buf->Data);
                break;
            case LQRCV_FLAG_RECV_IN_FBUF_TO_SEQ:
                free(Buf->RcvFbufToSeq.Seq);
                if(Buf->RcvFbufToSeq.CompleteOrCancelProc)
                    Buf->RcvFbufToSeq.CompleteOrCancelProc(NULL, Buf->RcvFbufToSeq.DestStream, Buf->RcvFbufToSeq.MaxSize - Buf->RcvFbufToSeq.Size, Buf->Data, false);
                break;
            case LQRCV_FLAG_READ_REGION:
                if(Buf->ReadRegion.CompleteOrCancelProc)
                    Buf->ReadRegion.CompleteOrCancelProc(NULL, Buf->ReadRegion.Dest, Buf->ReadRegion.Written, Buf->Data);
                break;
            case LQRCV_FLAG_COMPLETION_PROC:
                if(Buf->Completion.CompleteOrCancelProc)
                    Buf->Completion.CompleteOrCancelProc(NULL, Buf->Data);
                break;
            case LQRCV_FLAG_RECV_PULSE_READ:
                if(Buf->PulseRead.CompleteOrCancelProc)
                    Buf->PulseRead.CompleteOrCancelProc(NULL, Buf->Data);
                break;
            case LQRCV_FLAG_WAIT_LENGTH:
                if(Buf->WaitLen.CompleteOrCancelProc)
                    Buf->WaitLen.CompleteOrCancelProc(NULL, Buf->Data);
                break;
        }
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    if(Queue1) {
        Queue1 = false;
        Buf = (RcvElement*)SockBuf->Rcv2.First;
        goto lblAgain;
    }
    LqListInit(&SockBuf->Rcv);
    LqListInit(&SockBuf->Rcv2);
    return true;
}

static LqEvntFlag _EvntFlagBySockBuf(LqSockBuf* SockBuf);

/* Recursive lock*/
static void _SockBufLock(LqSockBuf* SockBuf) {
    int CurThread = LqThreadId();

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
        if(!(SockBuf->Flags & (LQSOCKBUF_FLAG_USED | LQSOCKBUF_FLAG_WORK | LQSOCKBUF_FLAG_IN_HANDLER))) {
            LqFbuf_close(&SockBuf->Stream);
            if(SockBuf->Cache != NULL)
                LqFcheDelete(SockBuf->Cache);
            LqFastAlloc::Delete(SockBuf);
            return;
        }
    }
    LqAtmUlkWr(SockBuf->Lk);
}

LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockBufCreate(int SockFd, void* UserData) {
    LqSockBuf* NewBuf;
    if((NewBuf = LqFastAlloc::New<LqSockBuf>()) == NULL) {
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
    LqConnInit(&NewBuf->Conn, SockFd, &___SockBufProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    if(LqFbuf_open_cookie(&NewBuf->Stream, (void*)SockFd, &_SocketCookie, LQFBUF_FAST_LK, ((intptr_t)0), ((intptr_t)4090), ((intptr_t)32768)) < ((intptr_t)0))
        goto lblErr;
    LqListInit(&NewBuf->Rcv);
    LqListInit(&NewBuf->Rcv2);
    LqListInit(&NewBuf->Rsp);
    NewBuf->KeepAlive = LqTimeGetMaxMillisec();
    NewBuf->LastExchangeTime = NewBuf->StartTime = LqTimeGetLocMillisec();
    NewBuf->UserData = UserData;
    NewBuf->Flags = LQSOCKBUF_FLAG_USED;
    NewBuf->RspHeader = NULL;
    NewBuf->UserData2 = NULL;
    NewBuf->ErrHandler = NULL;
    NewBuf->CloseHandler = NULL;
    NewBuf->Cache = NULL;
    NewBuf->ReadOffset = NewBuf->WriteOffset = ((int64_t)0);
    NewBuf->ThreadOwnerId = 0;
    NewBuf->Deep = ((int16_t)0);
    LqAtmLkInit(NewBuf->Lk);
    return NewBuf;
lblErr:
    if(NewBuf != NULL)
        LqFastAlloc::Delete(NewBuf);
    return NULL;
}

LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockBufCreateSsl(int SockFd, void* SslCtx, bool IsAccept, void* UserData) {
#if defined(HAVE_OPENSSL) 
    SSL* Ssl;
    int Ret;
    LqFbufFlag Flag = 0;
    LqSockBuf* NewBuf;

    if((NewBuf = LqFastAlloc::New<LqSockBuf>()) == NULL) {
        lq_errno_set(ENOMEM);
        goto lblErr;
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
            default: goto lblErr;
        }
    }

    LqConnInit(&NewBuf->Conn, SockFd, &___SockBufProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    if(LqFbuf_open_cookie(&NewBuf->Stream, Ssl, &_SslCookie, LQFBUF_FAST_LK | Flag, 0, 4090, 32768) < ((intptr_t)0))
        goto lblErr;
    LqListInit(&NewBuf->Rcv);
    LqListInit(&NewBuf->Rcv2);
    LqListInit(&NewBuf->Rsp);

    NewBuf->KeepAlive = LqTimeGetMaxMillisec();
    NewBuf->LastExchangeTime = NewBuf->StartTime = LqTimeGetLocMillisec();
    NewBuf->Flags = LQSOCKBUF_FLAG_USED;
    NewBuf->UserData = UserData;
    NewBuf->UserData2 = NULL;
    NewBuf->ErrHandler = NULL;
    NewBuf->CloseHandler = NULL;
    NewBuf->Cache = NULL;
    NewBuf->ReadOffset = NewBuf->WriteOffset = ((int64_t)0);

    LqAtmLkInit(NewBuf->Lk);
    NewBuf->ThreadOwnerId = 0;
    NewBuf->Deep = ((int16_t)0);

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
    _LqSockBufRcvClear(SockBuf);
    _LqSockBufRspClear(SockBuf);
    SockBuf->UserData = NULL;
    SockBuf->UserData2 = NULL;
    SockBuf->CloseHandler = NULL;
    SockBuf->ErrHandler = NULL;
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_USED;
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK)
        LqEvntSetClose(SockBuf);
    _SockBufUnlock(SockBuf);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufSetClose(LqSockBuf* SockBuf) {
    _SockBufLock(SockBuf);
    LqEvntSetClose(SockBuf);
    _SockBufUnlock(SockBuf);
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
LQ_EXTERN_C void LQ_CALL LqSockBufSetAutoHdr(LqSockBuf* SockBuf, bool Val) {
    if(Val)
        SockBuf->Flags |= LQSOCKBUF_FLAG_AUTO_HDR;
    else
        SockBuf->Flags &= ~LQSOCKBUF_FLAG_AUTO_HDR;
}

LQ_EXTERN_C void LQ_CALL LqSockBufRcvResetCount(LqSockBuf* SockBuf) {
    size_t InBufSize = ((size_t)0);
    _SockBufLock(SockBuf);
    LqFbuf_sizes(&SockBuf->Stream, NULL, &InBufSize);
    SockBuf->ReadOffset = (int64_t)InBufSize;
    _SockBufUnlock(SockBuf);
}

LQ_EXTERN_C void LQ_CALL LqSockBufRspResetCount(LqSockBuf* SockBuf) {
    size_t OutBufSize = ((size_t)0);
    _SockBufLock(SockBuf);
    LqFbuf_sizes(&SockBuf->Stream, &OutBufSize, NULL);
    SockBuf->WriteOffset = (int64_t)OutBufSize;
    _SockBufUnlock(SockBuf);
}

LQ_EXTERN_C int64_t LQ_CALL LqSockBufRcvGetCount(LqSockBuf* SockBuf) {
    size_t InBufSize = ((size_t)0);
    int64_t Res;
    _SockBufLock(SockBuf);
    LqFbuf_sizes(&SockBuf->Stream, NULL, &InBufSize);
    Res = SockBuf->ReadOffset - ((int64_t)InBufSize);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C int64_t LQ_CALL LqSockBufRspGetCount(LqSockBuf* SockBuf) {
    size_t OutBufSize = ((size_t)0);
    int64_t Res;
    _SockBufLock(SockBuf);
    LqFbuf_sizes(&SockBuf->Stream, &OutBufSize, NULL);
    Res = SockBuf->WriteOffset - ((int64_t)OutBufSize);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqSockBufSetKeepAlive(LqSockBuf* SockBuf, LqTimeMillisec NewValue) {
    SockBuf->KeepAlive = NewValue;
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqSockBufGetKeepAlive(LqSockBuf* SockBuf) {
    return SockBuf->KeepAlive;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufGoWork(LqSockBuf* SockBuf, void* WrkBoss) {
    bool Res = false;
    size_t InBufSize = 0;
    _SockBufLock(SockBuf);
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK)
        goto lblOut;
    LqEvntSetFlags(&SockBuf->Conn, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_RD | LQEVNT_FLAG_WR, 0);
    if(LqEvntAdd(&SockBuf->Conn, WrkBoss)) {
        SockBuf->Flags |= LQSOCKBUF_FLAG_WORK;
        Res = true;
        LqFbuf_sizes(&SockBuf->Stream, NULL, &InBufSize);
        if(InBufSize > 0)
            _RecvOrSendHandler(&SockBuf->Conn, LQEVNT_FLAG_RD);
        goto lblOut;
    }
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufInterruptWork(LqSockBuf* SockBuf) {
    bool Res;
    _SockBufLock(SockBuf);
    if(Res = LqEvntSetRemove3(&SockBuf->Conn))
        SockBuf->Flags &= ~LQSOCKBUF_FLAG_WORK;
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspNotifyCompletion(LqSockBuf* SockBuf, void* UserData, void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, void*)) {
    RspElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RspElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRSP_FLAG_COMPLETION_PROC;
    NewStream->Completion.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->Completion.UserData = UserData;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
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
        if(_LqSockBufWriteFile(SockBuf, Fd, -((LqFileSz)1)) == NULL) {
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
    int Fd;

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
        if((Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_SEQ | LQ_O_NOINHERIT, 0666)) == -1)
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

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFbuf(LqSockBuf* SockBuf, LqFbuf* File, LqFileSz Size) {
    bool Res = false;
    _SockBufLock(SockBuf);
    if((Size == -1) && !(File->Flags & (LQFBUF_POINTER | LQFBUF_STREAM))) {
        lq_errno_set(EINVAL);
        goto lblOut;
    }
    Res = _LqSockBufWriteVirtFile(SockBuf, File, Size) != NULL;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFd(LqSockBuf* SockBuf, int InFd) {
    bool Res;
    _SockBufLock(SockBuf);
    Res = _LqSockBufWriteFile(SockBuf, InFd, -((LqFileSz)1)) != NULL;
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFdPart(LqSockBuf* SockBuf, int InFd, LqFileSz OffsetInFile, LqFileSz Count) {
    bool Res = false;
    _SockBufLock(SockBuf);
    if(LqFileSeek(InFd, OffsetInFile, LQ_SEEK_SET) == -((LqFileSz)1))
        goto lblOut;
    Res = _LqSockBufWriteFile(SockBuf, InFd, Count) != NULL;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintf(LqSockBuf* SockBuf, bool IsHdr, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufPrintfVa(SockBuf, IsHdr, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfVa(LqSockBuf* SockBuf, bool IsHdr, const char* Fmt, va_list Va) {
    intptr_t Res = -1;
    LqFbuf* Stream;
    _SockBufLock(SockBuf);
    if(IsHdr) {
        if((Stream = (LqFbuf*)SockBuf->RspHeader) == NULL) {
            if(SockBuf->Flags & LQSOCKBUF_FLAG_AUTO_HDR) {
                if((Stream = _LqSockBufGetLastWriteStream(SockBuf)) == NULL) {
                    lq_errno_set(ENOMEM);
                    goto lblOut;
                }
                SockBuf->RspHeader = Stream;
            } else {
                goto lblOut;
            }
        }
    } else {
        if((Stream = _LqSockBufGetLastWriteStream(SockBuf)) == NULL)
            goto lblOut;
    }
    Res = LqFbuf_vprintf(Stream, Fmt, Va);
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufWrite(LqSockBuf* SockBuf, bool IsHdr, const void* Data, size_t SizeData) {
    intptr_t Res = -1;
    LqFbuf* Stream;
    _SockBufLock(SockBuf);
    if(IsHdr) {
        if((Stream = (LqFbuf*)SockBuf->RspHeader) == NULL) {
            if(SockBuf->Flags & LQSOCKBUF_FLAG_AUTO_HDR) {
                if((Stream = _LqSockBufGetLastWriteStream(SockBuf)) == NULL) {
                    lq_errno_set(ENOMEM);
                    goto lblOut;
                }
                SockBuf->RspHeader = Stream;
            } else {
                goto lblOut;
            }
        }
    } else {
        if((Stream = _LqSockBufGetLastWriteStream(SockBuf)) == NULL)
            goto lblOut;
    }
    Res = LqFbuf_write(Stream, Data, SizeData);
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspClear(LqSockBuf* SockBuf) {
    bool Res;
    _SockBufLock(SockBuf);
    Res = _LqSockBufRspClear(SockBuf);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspClearAfterHdr(LqSockBuf* SockBuf) {
    RspElement* Buf, *Next;
    _SockBufLock(SockBuf);
    for(Buf = SockBuf->RspHeader ? ((RspElement*)SockBuf->RspHeader)->Next : ((RspElement*)SockBuf->Rsp.First); Buf != NULL; Buf = Next) {
        if(Buf->Flag != LQRSP_FLAG_COMPLETION_PROC)
            LqFbuf_close(&Buf->Buf);
        else if(Buf->Completion.CompleteOrCancelProc)
            Buf->Completion.CompleteOrCancelProc(NULL, Buf->Completion.UserData);
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

LQ_EXTERN_C LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* SockBuf, bool IsHdr) {
    RspElement* Buf;
    LqFileSz Res = 0, b;
    LqFileStat FileStat;
    size_t a;
    int Fd;

    _SockBufLock(SockBuf);
    if(IsHdr) {
        if(SockBuf->RspHeader) {
            Buf = (RspElement*)SockBuf->RspHeader;
            if(Buf->Buf.Flags & (LQFBUF_POINTER | LQFBUF_STREAM)) {
                Res = LqFbuf_sizes(&Buf->Buf, NULL, NULL);
            }
        }
    } else {
        for(Buf = SockBuf->RspHeader ? ((RspElement*)SockBuf->RspHeader)->Next : ((RspElement*)SockBuf->Rsp.First);
            Buf != NULL;
            Buf = Buf->Next) {
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
    }
    _SockBufUnlock(SockBuf);
    return Res;
}

//////////////////////Rcv

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufScanf(LqSockBuf* SockBuf, int Flags, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    intptr_t Res = LqSockBufScanfVa(SockBuf, Flags, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufScanfVa(LqSockBuf* SockBuf, int Flags, const char* Fmt, va_list Va) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_vscanf(&SockBuf->Stream, Flags, Fmt, Va);
    if(SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufRead(LqSockBuf* SockBuf, void* Dest, size_t Size) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_read(&SockBuf->Stream, Dest, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPeek(LqSockBuf* SockBuf, void* Dest, size_t Size) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_peek(&SockBuf->Stream, Dest, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvNotifyCompletion(
    LqSockBuf* SockBuf, 
    void* UserData, 
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    bool IsSecondQueue
) {
    bool Res = false;
    RcvElement* NewStream;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_COMPLETION_PROC;
    NewStream->Completion.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->Data = UserData;
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvNotifyWhenMatch(
    LqSockBuf* SockBuf, 
    void* UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    const char* Fmt,
    int MatchCount,
    size_t MaxSize,
    bool IsSecondQueue
) {
    bool Res = false;
    RcvElement* NewStream;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_MATCH;
    NewStream->Match.Fmt = LqStrDuplicate(Fmt);
    NewStream->Match.MatchCount = MatchCount;
    NewStream->Match.MaxSize = MaxSize;
    NewStream->Match.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->Data = UserData;
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvInRegion(
    LqSockBuf* SockBuf, 
    void* UserData, 
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf* Buf, void* Dest, size_t Written, void* UserData),
    void* Dest, 
    size_t Size,
    bool IsSecondQueue
) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_READ_REGION;
    NewStream->ReadRegion.Dest = Dest;
    NewStream->ReadRegion.Written = 0;
    NewStream->ReadRegion.MaxSize = Size;
    NewStream->ReadRegion.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->Data = UserData;
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C LqSockBufErrFlags LQ_CALL LqSockBufGetErrFlags(LqSockBuf* SockBuf) {
    return SockBuf->Stream.Flags;
}

LQ_EXTERN_C size_t LQ_CALL LqSockBufRcvBufSz(LqSockBuf* SockBuf) {
    size_t Res = 0;
    _SockBufLock(SockBuf);
    LqFbuf_sizes(&SockBuf->Stream, NULL, &Res);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C size_t LQ_CALL LqSockBufRspBufSz(LqSockBuf* SockBuf) {
    size_t Res = 0;
    _SockBufLock(SockBuf);
    LqFbuf_sizes(&SockBuf->Stream, &Res, NULL);
    _SockBufUnlock(SockBuf);
    return Res;
}

static void LQ_CALL _CompleteProcForNullFbuf(LqSockBuf*, LqFbuf* DestStream, LqFileSz, void*) {
    LqFbuf_close(DestStream);
    LqFastAlloc::Delete(DestStream);
}

static void LQ_CALL _CompleteProcForNullFbufSeq(LqSockBuf*, LqFbuf* DestStream, LqFileSz, void*, bool) {
    LqFbuf_close(DestStream);
    LqFastAlloc::Delete(DestStream);
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvInFbuf(
    LqSockBuf* SockBuf,
    LqFbuf* DestStream,
    void* UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, LqFbuf*, LqFileSz, void*),
    LqFileSz Size,
    bool IsSecondQueue
) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    if(DestStream == NULL) {
        if((DestStream = LqFastAlloc::New<LqFbuf>()) == NULL)
            goto lblOut;
        LqFbuf_null(DestStream);
        CompleteOrCancelProc = _CompleteProcForNullFbuf;
    }
    NewStream->Flag = LQRCV_FLAG_RECV_IN_FBUF;
    NewStream->RcvFbuf.DestStream = DestStream;
    NewStream->RcvFbuf.MaxSize = NewStream->RcvFbuf.Size = Size;
    NewStream->RcvFbuf.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->Data = UserData;
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvInFbufAboveSeq(
    LqSockBuf* SockBuf,
    LqFbuf* DestStream,
    void* UserData,
    void(LQ_CALL*CompleteOrCancelProc)(LqSockBuf*, LqFbuf*, LqFileSz, void*, bool),
    const char* ControlSeq,
    size_t ControlSeqSize,
    LqFileSz MaxSize,
    bool IsCaseIndepended,
    bool IsSecondQueue
) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    if(DestStream == NULL) {
        if((DestStream = LqFastAlloc::New<LqFbuf>()) == NULL)
            goto lblOut;
        LqFbuf_null(DestStream);
        CompleteOrCancelProc = _CompleteProcForNullFbufSeq;
    }
    NewStream->Flag = LQRCV_FLAG_RECV_IN_FBUF_TO_SEQ;
    NewStream->RcvFbufToSeq.DestStream = DestStream;
    NewStream->RcvFbufToSeq.MaxSize = NewStream->RcvFbufToSeq.Size = MaxSize;
    NewStream->RcvFbufToSeq.SeqSize = ControlSeqSize;
    NewStream->RcvFbufToSeq.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->RcvFbufToSeq.IsCaseI = IsCaseIndepended;
    NewStream->Data = UserData;
    NewStream->RcvFbufToSeq.Seq = (char*)malloc(ControlSeqSize);
    memcpy(NewStream->RcvFbufToSeq.Seq, ControlSeq, ControlSeqSize);
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;

}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvPulseRead(
    LqSockBuf* SockBuf,
    void* UserData, 
    intptr_t(LQ_CALL *CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    bool IsSecondQueue
) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_RECV_PULSE_READ;
    NewStream->PulseRead.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->Data = UserData;
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvWaitLenData(
    LqSockBuf* SockBuf,
    void* UserData,
    void(LQ_CALL *CompleteOrCancelProc)(LqSockBuf* Buf, void* UserData),
    size_t TargetLen,  /* !! Recived this len in internal buffer !! Be careful when set the large size of this parameter */
    bool IsSecondQueue
) {
    RcvElement* NewStream;
    bool Res = false;
    _SockBufLock(SockBuf);
    if((NewStream = LqFastAlloc::New<RcvElement>()) == NULL)
        goto lblOut;
    NewStream->Flag = LQRCV_FLAG_WAIT_LENGTH;
    NewStream->WaitLen.CompleteOrCancelProc = CompleteOrCancelProc;
    NewStream->WaitLen.Length = TargetLen;
    NewStream->Data = UserData;
    if(IsSecondQueue) {
        LqListAdd(&SockBuf->Rcv2, NewStream, RcvElement);
    } else {
        LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (LqEvntGetFlags(&SockBuf->Conn) & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    Res = true;
lblOut:
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufReadInStream(LqSockBuf* SockBuf, LqFbuf* Dest, size_t Size) {
    intptr_t Res;
    _SockBufLock(SockBuf);
    Res = LqFbuf_transfer(Dest, &SockBuf->Stream, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* SockBuf, bool IsSecondQueue) {
    RcvElement* Buf, *Prev = NULL;
    LqListHdr* List;
    bool Res = true;
    _SockBufLock(SockBuf);
    List = (IsSecondQueue) ? &SockBuf->Rcv2 : &SockBuf->Rcv;
    for(Buf = (RcvElement*)List->First; Buf != NULL; Buf = Buf->Next) {
        if(Buf == List->Last) {
            switch(Buf->Flag) {
                case LQRCV_FLAG_MATCH:
                    free(Buf->Match.Fmt);
                    if(Buf->Match.CompleteOrCancelProc)
                        Buf->Match.CompleteOrCancelProc(NULL, Buf->Data);
                    break;
                case LQRCV_FLAG_RECV_IN_FBUF:
                    if(Buf->RcvFbuf.CompleteOrCancelProc)
                        Buf->RcvFbuf.CompleteOrCancelProc(NULL, Buf->RcvFbuf.DestStream, Buf->RcvFbuf.MaxSize - Buf->RcvFbuf.Size, Buf->Data);
                    break;
                case LQRCV_FLAG_RECV_IN_FBUF_TO_SEQ:
                    free(Buf->RcvFbufToSeq.Seq);
                    if(Buf->RcvFbufToSeq.CompleteOrCancelProc)
                        Buf->RcvFbufToSeq.CompleteOrCancelProc(NULL, Buf->RcvFbufToSeq.DestStream, Buf->RcvFbufToSeq.MaxSize - Buf->RcvFbufToSeq.Size, Buf->Data, false);
                    break;
                case LQRCV_FLAG_READ_REGION:
                    if(Buf->ReadRegion.CompleteOrCancelProc)
                        Buf->ReadRegion.CompleteOrCancelProc(NULL, Buf->ReadRegion.Dest, Buf->ReadRegion.Written, Buf->Data);
                    break;
                case LQRCV_FLAG_COMPLETION_PROC:
                    if(Buf->Completion.CompleteOrCancelProc)
                        Buf->Completion.CompleteOrCancelProc(NULL, Buf->Data);
                    break;
                case LQRCV_FLAG_RECV_PULSE_READ:
                    if(Buf->PulseRead.CompleteOrCancelProc)
                        Buf->PulseRead.CompleteOrCancelProc(NULL, Buf->Data);
                    break;
                case LQRCV_FLAG_WAIT_LENGTH:
                    if(Buf->WaitLen.CompleteOrCancelProc)
                        Buf->WaitLen.CompleteOrCancelProc(NULL, Buf->Data);
                    break;
            }
            LqFastAlloc::Delete(Buf);
            if(Prev == NULL)
                List->First = NULL;
            else
                Prev->Next = NULL;
            List->Last = Prev;
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
    bool Res;
    _SockBufLock(SockBuf);
    Res = _LqSockBufRcvClear(SockBuf);
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C void LQ_CALL LqSockBufFlush(LqSockBuf* SockBuf) {
    _RecvOrSendHandler(&SockBuf->Conn, LQEVNT_FLAG_WR | LQEVNT_FLAG_RD);
}

LQ_EXTERN_C size_t LQ_CALL LqSockBufRcvQueueLen(LqSockBuf* SockBuf, bool IsSecondQueue) {
    size_t Res = ((size_t)0);
    RcvElement* Buf;
    _SockBufLock(SockBuf);
    if(IsSecondQueue) {
        for(Buf = ((RcvElement*)SockBuf->Rcv2.First); Buf != NULL; Buf = Buf->Next, Res++);
    } else {
        for(Buf = ((RcvElement*)SockBuf->Rcv.First); Buf != NULL; Buf = Buf->Next, Res++);
    }
    _SockBufUnlock(SockBuf);
    return Res;
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

LQ_EXTERN_C int LQ_CALL LqSockBufGetFd(LqSockBuf* SockBuf) {
    return SockBuf->Conn.Fd;
}

static int LQ_CALL _LqSockBufCloseByUserData2Proc(void *UserData2, LqEvntHdr *EvntHdr) {
    return (((LqSockBuf*)EvntHdr)->UserData2 == UserData2)? 2: 0;
}

LQ_EXTERN_C size_t LQ_CALL LqSockBufCloseByUserData2(void* UserData2, void* WrkBoss) {
    if(WrkBoss == NULL)
        WrkBoss = LqWrkBossGet();
    return ((LqWrkBoss*)WrkBoss)->EnumCloseRmEvntByProto(_LqSockBufCloseByUserData2Proc, &___SockBufProto, UserData2);
}

LQ_EXTERN_C void LQ_CALL LqSockBufLock(LqSockBuf* SockBuf) {
    _SockBufLock(SockBuf);
}

LQ_EXTERN_C void LQ_CALL LqSockBufUnlock(LqSockBuf* SockBuf) {
    _SockBufUnlock(SockBuf);
}

LQ_EXTERN_C void* LQ_CALL LqSockBufGetSsl(LqSockBuf* SockBuf) {
    void* Res = NULL;
    _SockBufLock(SockBuf);
    if(SockBuf->Stream.Cookie == &_SslCookie)
        Res = SockBuf->Stream.UserData;
    _SockBufUnlock(SockBuf);
    return Res;
}

LQ_EXTERN_C size_t LQ_CALL LqSockBufEnum(void* WrkBoss, int(LQ_CALL*Proc)(void*, LqSockBuf*), void* UserData) {
    if(WrkBoss == NULL)
        WrkBoss = LqWrkBossGet();
    return ((LqWrkBoss*)WrkBoss)->EnumCloseRmEvntByProto((int(LQ_CALL*)(void *, LqEvntHdr*))Proc, &___SockBufProto, UserData);
}


//////////////////////////////////Hndlrs

static void LQ_CALL _RecvOrSendHandler(LqConn* Sock, LqEvntFlag RetFlags) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    RspElement* WrElem;
    RcvElement *RdElem;
    size_t OutputBufferSize;
    LqEvntFlag ConnFlag = LqEvntGetFlags(Sock);
    LqFbufFlag FbufFlags = ((LqFbufFlag)0);
    intptr_t Readed2, Readed;
    void *UserData, *UserData2;
    intptr_t(LQ_CALL*CompleteOrCancelProc5)(LqSockBuf*, void*);
    void* Dest1;
    LqFbuf* Fbuf;
    LqFileSz Sended;
    bool NotWrite = false;
    bool IsFound;

    _SockBufLock(SockBuf);
    if(SockBuf->Flags & LQSOCKBUF_FLAG_IN_HANDLER){
        _SockBufUnlock(SockBuf);
        return;
    }
    SockBuf->Flags |= LQSOCKBUF_FLAG_IN_HANDLER;
    SockBuf->LastExchangeTime = LqTimeGetLocMillisec();

    if(RetFlags & LQEVNT_FLAG_ERR) {
        if(SockBuf->ErrHandler) {
            _SockBufUnlock(SockBuf);
            SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_UNKNOWN_SOCKET);
            _SockBufLock(SockBuf);
            if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                goto lblOut2;
        } else {
            LqEvntSetClose(SockBuf);
            goto lblOut2;
        }
    }
    if(RetFlags & LQEVNT_FLAG_WR) {/* Can write */
        SockBuf->RWPortion = Sock->Proto->MaxSendInSingleTime;
lblWriteAgain:
        /* Write first stream from list*/
        if((WrElem = LqListFirst(&SockBuf->Rsp, RspElement)) == NULL) {
            LqFbuf_flush(&SockBuf->Stream);
            goto lblTryRead;
        }
        if(WrElem->Flag == LQRSP_FLAG_COMPLETION_PROC) {
            LqFbuf_flush(&SockBuf->Stream);
            LqFbuf_sizes(&SockBuf->Stream, &OutputBufferSize, NULL);
            if(OutputBufferSize > ((size_t)0))
                goto lblTryRead;
            /* If this element is just completeon proc*/
            LqListRemove(&SockBuf->Rsp, RspElement);
            _SockBufUnlock(SockBuf);
            WrElem->Completion.CompleteOrCancelProc(SockBuf, WrElem->Completion.UserData);
            _SockBufLock(SockBuf);
            LqFastAlloc::Delete(WrElem);
            if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                goto lblOut2;
            goto lblWriteAgain;
        } else if(WrElem->Flag == LQRSP_FLAG_BUF) {
            if((SockBuf->RspHeader != NULL) && (SockBuf->RspHeader == LqListFirst(&SockBuf->Rsp, RspElement))) {
                if(SockBuf->Flags & LQSOCKBUF_FLAG_AUTO_HDR) {
                    SockBuf->RspHeader = NULL;
                } else {
                    NotWrite = true;
                    goto lblTryRead;
                }
            }
            Sended = LqFbuf_transfer(&SockBuf->Stream, &WrElem->Buf, (WrElem->RspSize >= ((LqFileSz)0)) ? WrElem->RspSize: SockBuf->RWPortion);
            if(WrElem->RspSize >= ((LqFileSz)0))
                WrElem->RspSize -= lq_max(Sended, ((LqFileSz)0));
            FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
            if(LqFbuf_eof(&WrElem->Buf) || (WrElem->RspSize == ((LqFileSz)0))) {
lblErrWrite:
                LqFbuf_close(&WrElem->Buf);
                LqListRemove(&SockBuf->Rsp, RspElement);
                LqFastAlloc::Delete(WrElem);
                goto lblWriteAgain;
            }
            if(WrElem->Buf.Flags & LQFBUF_READ_ERROR) {
                UserData = WrElem->Next;
                UserData2 = WrElem->Buf.UserData;
                if(SockBuf->ErrHandler) {
                    _SockBufUnlock(SockBuf);
                    SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_OUTPUT_DATA);
                    _SockBufLock(SockBuf);
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
        SockBuf->RWPortion = Sock->Proto->MaxReciveInSingleTime;
lblReadAgain:
        if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED) || (((RdElem = LqListFirst(&SockBuf->Rcv, RcvElement)) == NULL) && ((RdElem = LqListFirst(&SockBuf->Rcv2, RcvElement)) == NULL)))
            goto lblOut;
        switch(RdElem->Flag) {
            case LQRCV_FLAG_MATCH:
                Readed2 = LqFbuf_scanf(&SockBuf->Stream, LQFBUF_SCANF_PEEK, RdElem->Match.Fmt);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed2 < ((intptr_t)0))
                    Readed2 = ((intptr_t)0);
                if((Readed2 >= RdElem->Match.MatchCount) || (SockBuf->Stream.InBuf.Len >= RdElem->Match.MaxSize)) {
                    if(LqListFirst(&SockBuf->Rcv, RcvElement) == RdElem) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                    } else {
                        LqListRemove(&SockBuf->Rcv2, RcvElement);
                    }
                    if(RdElem->Match.CompleteOrCancelProc != NULL) {
                        _SockBufUnlock(SockBuf);
                        RdElem->Match.CompleteOrCancelProc(SockBuf, RdElem->Data);
                        _SockBufLock(SockBuf);
                    }  
                    free(RdElem->Match.Fmt);
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                } else if(RdElem == LqListFirst(&SockBuf->Rcv2, RcvElement)) {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                    LqListAddForward(&SockBuf->Rcv, RdElem, RcvElement);
                }
                break;
            case LQRCV_FLAG_READ_REGION:
                Readed2 = RdElem->ReadRegion.MaxSize - RdElem->ReadRegion.Written;
                Readed = (RdElem->ReadRegion.IsPeek) ? 
                    LqFbuf_peek(&SockBuf->Stream, (RdElem->ReadRegion.Dest)?((char*)RdElem->ReadRegion.Dest + RdElem->ReadRegion.Written): NULL, Readed2) :
                    LqFbuf_read(&SockBuf->Stream, (RdElem->ReadRegion.Dest)?((char*)RdElem->ReadRegion.Dest + RdElem->ReadRegion.Written): NULL, Readed2);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed > ((intptr_t)0))
                    RdElem->ReadRegion.Written += Readed;
                if(Readed >= Readed2) {
                    if(LqListFirst(&SockBuf->Rcv, RcvElement) == RdElem) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                    } else {
                        LqListRemove(&SockBuf->Rcv2, RcvElement);
                    }
                    if(RdElem->ReadRegion.CompleteOrCancelProc != NULL) {
                        _SockBufUnlock(SockBuf);
                        RdElem->ReadRegion.CompleteOrCancelProc(SockBuf, RdElem->ReadRegion.Dest, RdElem->ReadRegion.Written, RdElem->Data);
                        _SockBufLock(SockBuf);
                    }
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                } else if(RdElem == LqListFirst(&SockBuf->Rcv2, RcvElement)) {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                    LqListAddForward(&SockBuf->Rcv, RdElem, RcvElement);
                }
                break;
            case LQRCV_FLAG_RECV_IN_FBUF:
                Readed = LqFbuf_transfer(RdElem->RcvFbuf.DestStream, &SockBuf->Stream, RdElem->RcvFbuf.Size);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed > ((intptr_t)0))
                    RdElem->RcvFbuf.Size -= ((LqFileSz)Readed);
                if(RdElem->RcvFbuf.Size <= ((LqFileSz)0)) {
                    if(LqListFirst(&SockBuf->Rcv, RcvElement) == RdElem) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                    } else {
                        LqListRemove(&SockBuf->Rcv2, RcvElement);
                    }
                    if(RdElem->RcvFbuf.CompleteOrCancelProc != NULL) {
                        _SockBufUnlock(SockBuf);
                        RdElem->RcvFbuf.CompleteOrCancelProc(
                            SockBuf,
                            RdElem->RcvFbuf.DestStream,
                            RdElem->RcvFbuf.MaxSize - RdElem->RcvFbuf.Size,
                            RdElem->Data
                        );
                        _SockBufLock(SockBuf);
                    }
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                } else if(RdElem == LqListFirst(&SockBuf->Rcv2, RcvElement)) {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                    LqListAddForward(&SockBuf->Rcv, RdElem, RcvElement);
                }
                break;
            case LQRCV_FLAG_RECV_IN_FBUF_TO_SEQ:
                IsFound = false;
                Readed = LqFbuf_transfer_while_not_same(
                    RdElem->RcvFbufToSeq.DestStream,
                    &SockBuf->Stream,
                    RdElem->RcvFbufToSeq.Size,
                    RdElem->RcvFbufToSeq.Seq,
                    RdElem->RcvFbufToSeq.SeqSize,
                    RdElem->RcvFbufToSeq.IsCaseI,
                    &IsFound
                );
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed > ((intptr_t)0))
                    RdElem->RcvFbufToSeq.Size -= ((LqFileSz)Readed);
                if(IsFound || (RdElem->RcvFbufToSeq.Size <= ((LqFileSz)0))) {
                    if(LqListFirst(&SockBuf->Rcv, RcvElement) == RdElem) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                    } else {
                        LqListRemove(&SockBuf->Rcv2, RcvElement);
                    }
                    if(RdElem->RcvFbufToSeq.CompleteOrCancelProc) {
                        _SockBufUnlock(SockBuf);
                        RdElem->RcvFbufToSeq.CompleteOrCancelProc(
                            SockBuf,
                            RdElem->RcvFbufToSeq.DestStream,
                            RdElem->RcvFbufToSeq.MaxSize - RdElem->RcvFbufToSeq.Size,
                            RdElem->Data,
                            IsFound
                        );
                        _SockBufLock(SockBuf);
                    }
                    free(RdElem->RcvFbufToSeq.Seq);
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                } else if(RdElem == LqListFirst(&SockBuf->Rcv2, RcvElement)) {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                    LqListAddForward(&SockBuf->Rcv, RdElem, RcvElement);
                }
                break;
            case LQRCV_FLAG_RECV_PULSE_READ:
                if(RdElem == LqListFirst(&SockBuf->Rcv2, RcvElement)) {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                    LqListAddForward(&SockBuf->Rcv, RdElem, RcvElement);
                }
                UserData = RdElem->Data;
                CompleteOrCancelProc5 = RdElem->PulseRead.CompleteOrCancelProc;
                UserData2 = RdElem->Next;
                _SockBufUnlock(SockBuf);
                Readed = CompleteOrCancelProc5(SockBuf, RdElem->Data);
                _SockBufLock(SockBuf);
                if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                    goto lblOut2;
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed < ((intptr_t)0)) {
                    if((RdElem == LqListFirst(&SockBuf->Rcv, RcvElement)) &&
                       (UserData == RdElem->Data) &&
                       (CompleteOrCancelProc5 == RdElem->PulseRead.CompleteOrCancelProc) &&
                       (UserData2 == RdElem->Next)
                  ) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                        LqFastAlloc::Delete(RdElem);
                    }
                    goto lblReadAgain;
                }
                break;
            case LQRCV_FLAG_COMPLETION_PROC:
                if(LqListFirst(&SockBuf->Rcv, RcvElement) == RdElem) {
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                } else {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                }
                _SockBufUnlock(SockBuf);
                RdElem->Completion.CompleteOrCancelProc(SockBuf, RdElem->Data);
                _SockBufLock(SockBuf);
                LqFastAlloc::Delete(RdElem);
                goto lblReadAgain;
            case LQRCV_FLAG_WAIT_LENGTH:
                Readed = LqFbuf_peek(&SockBuf->Stream, NULL, RdElem->WaitLen.Length);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed >= RdElem->WaitLen.Length) {
                    if(LqListFirst(&SockBuf->Rcv, RcvElement) == RdElem) {
                        LqListRemove(&SockBuf->Rcv, RcvElement);
                    } else {
                        LqListRemove(&SockBuf->Rcv2, RcvElement);
                    }
                    if(RdElem->WaitLen.CompleteOrCancelProc) {
                        _SockBufUnlock(SockBuf);
                        RdElem->WaitLen.CompleteOrCancelProc(SockBuf, RdElem->Data);
                        _SockBufLock(SockBuf);
                    }
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                } else if(RdElem == LqListFirst(&SockBuf->Rcv2, RcvElement)) {
                    LqListRemove(&SockBuf->Rcv2, RcvElement);
                    LqListAddForward(&SockBuf->Rcv, RdElem, RcvElement);
                }
                break;
        }
    }
lblOut:
    if(FbufFlags & (LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR)) {
        if(SockBuf->ErrHandler) {
            _SockBufUnlock(SockBuf);
            SockBuf->ErrHandler(SockBuf, ((FbufFlags & LQFBUF_WRITE_ERROR) ? LQSOCKBUF_ERR_WRITE_SOCKET : 0) | ((FbufFlags & LQFBUF_READ_ERROR) ? LQSOCKBUF_ERR_READ_SOCKET : 0));
            _SockBufLock(SockBuf);
            if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED))
                goto lblOut2;
        } else {
            LqEvntSetClose(SockBuf);
            goto lblOut2;
        }
    }
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK) {
        LqFbuf_sizes(&SockBuf->Stream, &OutputBufferSize, NULL);
        if(!NotWrite && ((LqListFirst(&SockBuf->Rsp, RspElement) != NULL) || (OutputBufferSize > 0) || (FbufFlags & LQFBUF_WRITE_WOULD_BLOCK)))
            ConnFlag |= LQEVNT_FLAG_WR;
        else
            ConnFlag &= ~LQEVNT_FLAG_WR;

        if((LqListFirst(&SockBuf->Rcv, RcvElement) != NULL) || (LqListFirst(&SockBuf->Rcv2, RcvElement) != NULL) || (FbufFlags & LQFBUF_READ_WOULD_BLOCK))
            ConnFlag |= LQEVNT_FLAG_RD;
        else
            ConnFlag &= ~LQEVNT_FLAG_RD;
        LqEvntSetFlags(Sock, ConnFlag, 0);
    }
lblOut2:
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_IN_HANDLER;
    _SockBufUnlock(SockBuf);
}

static void LQ_CALL _CloseHandler(LqConn* Sock) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    _SockBufLock(SockBuf);
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_WORK;
    if(SockBuf->CloseHandler != NULL) {
        SockBuf->Flags |= LQSOCKBUF_FLAG_IN_HANDLER;
        _SockBufUnlock(SockBuf);
        SockBuf->CloseHandler(SockBuf);
        _SockBufLock(SockBuf);
        SockBuf->Flags &= ~LQSOCKBUF_FLAG_IN_HANDLER;
    }
    _SockBufUnlock(SockBuf);
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
        (((LqListFirst(&SockBuf->Rcv, RcvElement) != NULL) || 
        (LqListFirst(&SockBuf->Rcv2, RcvElement) != NULL)) ? LQEVNT_FLAG_RD : 0);
}

//////////////////////////////////
///////Acceptor
//////////////////////////////////

/* Recursive lock*/
static void _SockAcceptorLock(LqSockAcceptor* SockAcceptor) {
    int CurThread = LqThreadId();

    while(true) {
        LqAtmLkWr(SockAcceptor->Lk);
        if((SockAcceptor->ThreadOwnerId == 0) || (SockAcceptor->ThreadOwnerId == CurThread)) {
            SockAcceptor->ThreadOwnerId = CurThread;
            SockAcceptor->Deep++;
            CurThread = 0;
        }
        LqAtmUlkWr(SockAcceptor->Lk);
        if(CurThread == 0)
            return;
        LqThreadYield();
    }
}

/* Recursive unlock*/
static void _SockAcceptorUnlock(LqSockAcceptor* SockAcceptor) {
    LqAtmLkWr(SockAcceptor->Lk);
    SockAcceptor->Deep--;
    if(SockAcceptor->Deep == 0) {
        SockAcceptor->ThreadOwnerId = 0;
        if(!(SockAcceptor->Flags & (LQSOCKBUF_FLAG_USED | LQSOCKBUF_FLAG_WORK | LQSOCKBUF_FLAG_IN_HANDLER))) {
            closesocket(SockAcceptor->Conn.Fd);
            if(SockAcceptor->Cache != NULL)
                LqFcheDelete(SockAcceptor->Cache);
            LqFastAlloc::Delete(SockAcceptor);
            return;
        }
    }
    LqAtmUlkWr(SockAcceptor->Lk);
}

LQ_EXTERN_C LqSockAcceptor* LQ_CALL LqSockAcceptorCreate(const char* Host, const char* Port, int RouteProto, int SockType, int TransportProto, int MaxConnections, bool IsNonBlock, void* UserData) {
    LqSockAcceptor* NewAcceptor;
    int Fd = LqConnBind(Host, Port, RouteProto, SockType, TransportProto, MaxConnections, IsNonBlock);
    if(Fd == -1)
        return NULL;
    if((NewAcceptor = LqFastAlloc::New<LqSockAcceptor>()) == NULL) {
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
    if(LqEvntAdd(&SockAcceptor->Conn, WrkBoss)) {
        SockAcceptor->Flags |= LQSOCKBUF_FLAG_WORK;
        Res = true;
        goto lblOut;
    }
lblOut:
    _SockAcceptorUnlock(SockAcceptor);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockAcceptorInterruptWork(LqSockAcceptor* SockAcceptor) {
    bool Res;
    _SockAcceptorLock(SockAcceptor);
    if(Res = LqEvntSetRemove3(&SockAcceptor->Conn))
        SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_WORK;
    _SockAcceptorUnlock(SockAcceptor);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockAcceptorDelete(LqSockAcceptor* SockAcceptor) {
    _SockAcceptorLock(SockAcceptor);
    SockAcceptor->UserData = NULL;
    SockAcceptor->CloseHandler = NULL;
    SockAcceptor->AcceptProc = NULL;
    SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_USED;
    if(SockAcceptor->Flags & LQSOCKBUF_FLAG_WORK)
        LqEvntSetClose(SockAcceptor);
    _SockAcceptorUnlock(SockAcceptor);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockAcceptorSkip(LqSockAcceptor* SockAcceptor) {
    int Fd;
    _SockAcceptorLock(SockAcceptor);
    if((Fd = accept(SockAcceptor->Conn.Fd, NULL, NULL)) != -1)
        closesocket(Fd);
    _SockAcceptorUnlock(SockAcceptor);
    return Fd != -1;
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

LQ_EXTERN_C void LQ_CALL LqSockAcceptorLock(LqSockAcceptor* SockAcceptor) {
    _SockAcceptorLock(SockAcceptor);
}

LQ_EXTERN_C void LQ_CALL LqSockAcceptorUnlock(LqSockAcceptor* SockAcceptor) {
    _SockAcceptorUnlock(SockAcceptor);
}

LQ_EXTERN_C int LQ_CALL LqSockAcceptorGetFd(LqSockAcceptor* SockAcceptor) {
    return SockAcceptor->Conn.Fd;
}

static void LQ_CALL _AcceptorAcceptHandler(LqConn* Connection, LqEvntFlag RetFlags) {
    LqSockAcceptor* SockAcceptor = (LqSockAcceptor*)Connection;
    _SockAcceptorLock(SockAcceptor);
    if(RetFlags & LQEVNT_FLAG_ERR)
        LqEvntSetClose(SockAcceptor);
    if((RetFlags & LQEVNT_FLAG_ACCEPT) && (SockAcceptor->AcceptProc != NULL)) {
        SockAcceptor->Flags |= LQSOCKBUF_FLAG_IN_HANDLER;
        _SockAcceptorUnlock(SockAcceptor);
        SockAcceptor->AcceptProc(SockAcceptor);
        _SockAcceptorLock(SockAcceptor);
        SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_IN_HANDLER;
    }
    _SockAcceptorUnlock(SockAcceptor);
}

static void LQ_CALL _AcceptorCloseHandler(LqConn* Conn) {
    LqSockAcceptor* SockAcceptor = (LqSockAcceptor*)Conn;
    _SockAcceptorLock(SockAcceptor);
    SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_WORK;
    if(SockAcceptor->CloseHandler != NULL) {
        SockAcceptor->Flags |= LQSOCKBUF_FLAG_IN_HANDLER;
        _SockAcceptorUnlock(SockAcceptor);
        SockAcceptor->CloseHandler(SockAcceptor);
        _SockAcceptorLock(SockAcceptor);
        SockAcceptor->Flags &= ~LQSOCKBUF_FLAG_IN_HANDLER;
    }
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