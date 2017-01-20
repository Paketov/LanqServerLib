

#include "LqSockBuf.h"
#include "LqAlloc.hpp"
#include "LqStr.h"
#include "LqTime.h"
#include "LqWrkBoss.hpp"

static void LQ_CALL RecvOrSendHandler(LqConn* Connection, LqEvntFlag RetFlags);
static void LQ_CALL CloseHandler(LqConn* Conn);
static bool LQ_CALL CmpAddressProc(LqConn* Conn, const void* Address);

static bool LQ_CALL KickByTimeOutProc(
    LqConn*        Connection,
    LqTimeMillisec CurrentTimeMillisec,
    LqTimeMillisec EstimatedLiveTime
);

static char* LQ_CALL DebugInfoProc(LqConn* Conn);


static LqProto SockBufProto = {
    nullptr,
    32768,
    32768,
    32768,
    RecvOrSendHandler,
    CloseHandler,
    CmpAddressProc,
    KickByTimeOutProc,
    DebugInfoProc
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
            case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_ACCEPT:
                if((Written = SSL_accept((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error(Ssl, Written)) {
                        case SSL_ERROR_WANT_READ: Flag |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Flag |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblWriteAgain;
            case SSL_ERROR_WANT_CONNECT:
                if((Written = SSL_connect((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error(Ssl, Written)) {
                        case SSL_ERROR_WANT_READ: Flag |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Flag |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
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
        switch(SSL_get_error((SSL*)Context->UserData, Written)) {
            case SSL_ERROR_WANT_READ: Context->Flags |= LQFBUF_READ_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_WRITE: Context->Flags |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
            case SSL_ERROR_WANT_ACCEPT:
                if((Readed = SSL_accept((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error(Ssl, Readed)) {
                        case SSL_ERROR_WANT_READ: Flag |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Flag |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblReadAgain;
            case SSL_ERROR_WANT_CONNECT:
                if((Readed = SSL_connect((SSL*)Context->UserData)) <= 0) {
                    switch(SSL_get_error(Ssl, Readed)) {
                        case SSL_ERROR_WANT_READ: Flag |= LQFBUF_READ_WOULD_BLOCK; return -1;
                        case SSL_ERROR_WANT_WRITE: Flag |= LQFBUF_WRITE_WOULD_BLOCK; return -1;
                        default: goto lblError;
                    }
                } else
                    goto lblReadAgain;
            default: goto lblError;
        }
    }
    return Written;
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

typedef struct RspElement {
    LqFbuf Buf;
    RspElement* Next;
} RspElement;

typedef struct FilePartData {
    LqFileSz Readed;
    LqFileSz Size;
    int Fd;
} FilePartData;

static intptr_t _FilePartReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    intptr_t Readed;
    LqFileSz ReadSize2 = Size;
    LqFileSz ReadSize = ((FilePartData*)Context->UserData)->Size - ((FilePartData*)Context->UserData)->Readed;
    if(ReadSize <= 0) {
        Context->Flags |= LQFBUF_READ_EOF;
        return -1;
    }
    ReadSize = lq_min(ReadSize, ReadSize2);
    if(Size == 0)
        return 0;
    if((Readed = LqFileRead(((FilePartData*)Context->UserData)->Fd, Buf, ReadSize)) <= 0) {
        if(Readed == 0)
            Context->Flags |= LQFBUF_READ_EOF;
        else
            Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
        return 0;
    }
    ((FilePartData*)Context->UserData)->Readed += Readed;
    return Readed;
}

static intptr_t _FilePartSeekProc(LqFbuf* Context, int64_t Offset, int Flags) {
    return LqFileSeek(((FilePartData*)Context->UserData)->Fd, Offset, Flags);
}

static intptr_t _FilePartCloseProc(LqFbuf* Context) {
    int Res;
    Res = LqFileClose(((FilePartData*)Context->UserData)->Fd);
    LqFastAlloc::Delete((FilePartData*)Context->UserData);
    return Res;
}

static LqFbufCookie _SocketCookie = {
    _SockReadProc,
    _SockWriteProc,
    _EmptySeekProc,
    _SockCloseProc
};

static LqFbufCookie _SslCookie = {
    _SslReadProc,
    _SslWriteProc,
    _EmptySeekProc,
    _SslCloseProc
};

static LqFbufCookie _FilePartCookie = {
    _FilePartReadProc,
    _EmptyWriteProc,
    _FilePartSeekProc,
    _FilePartCloseProc
};

static LqFbuf* _LqSockBufGetLastWriteStream(LqSockBuf* SockBuf) {
    RspElement* NewStream;
    if((SockBuf->Rsp.Last == nullptr) ||
       !(((RspElement*)SockBuf->Rsp.Last)->Buf.Flags & LQFBUF_STREAM) &&
       (((RspElement*)SockBuf->Rsp.Last)->Buf.Flags & LQFBUF_USER_FLAG)) {
        if((NewStream = LqFastAlloc::New<RspElement>()) == nullptr)
            return nullptr;
        LqFbuf_stream(&NewStream->Buf);
        LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    }
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &((RspElement*)SockBuf->Rsp.Last)->Buf;
}

static LqFbuf* _LqSockBufWriteFdPart(LqSockBuf* SockBuf, int Fd, LqFileSz Size) {
    RspElement* NewStream = nullptr;
    FilePartData* NewData = nullptr;
    if((NewStream = LqFastAlloc::New<RspElement>()) == nullptr)
        goto lblErr;
    if((NewData = LqFastAlloc::New<FilePartData>()) == nullptr)
        goto lblErr;
    LqFbuf_open_cookie(&NewStream->Buf, NewData, &_FilePartCookie, LQFBUF_FAST_LK, 0, 4096, 32768);
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
lblErr:
    if(NewStream != nullptr)
        LqFastAlloc::Delete(NewStream);
    if(NewData != nullptr)
        LqFastAlloc::Delete(NewData);
    return nullptr;
}

static LqFbuf* _LqSockBufWriteFile(LqSockBuf* SockBuf, int Fd) {
    RspElement* NewStream = nullptr;
    if((NewStream = LqFastAlloc::New<RspElement>()) == nullptr)
        return nullptr;
    LqFbuf_fdopen(&NewStream->Buf, LQFBUF_FAST_LK, Fd, 0, 4096, 32768);
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
}

static LqFbuf* _LqSockBufWriteVirtFile(LqSockBuf* SockBuf, LqFbuf* File) {
    RspElement* NewStream;
    if((NewStream = LqFastAlloc::New<RspElement>()) == nullptr)
        return nullptr;
    if(LqFbuf_copy(&NewStream->Buf, File) < 0)
        goto lblErr;
    LqListAdd(&SockBuf->Rsp, NewStream, RspElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_WR))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_RD) | (LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return &NewStream->Buf;
lblErr:
    LqFastAlloc::Delete(NewStream);
    return nullptr;
}

static LqEvntFlag _EvntFlagBySockBuf(LqSockBuf* SockBuf);


LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockBufCreate(int SockFd, void* UserData) {
    LqSockBuf* NewBuf = LqFastAlloc::New<LqSockBuf>();
    if(NewBuf == nullptr)
        return nullptr;
    LqConnInit(&NewBuf->Conn, SockFd, &SockBufProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    if(LqFbuf_open_cookie(&NewBuf->Stream, (void*)SockFd, &_SocketCookie, LQFBUF_FAST_LK, 0, 4090, 32768) < 0)
        return false;
    LqListInit(&NewBuf->Rcv);
    LqListInit(&NewBuf->Rsp);
    NewBuf->RspHeader = nullptr;
    NewBuf->LastExchangeTime = NewBuf->StartTime = LqTimeGetLocMillisec();
    NewBuf->UserData = UserData;
    NewBuf->Flags = LQSOCKBUF_FLAG_USED;
    return NewBuf;
}

LQ_EXTERN_C LqSockBuf* LQ_CALL LqSockBufCreateSsl(int SockFd, void* SslCtx, bool IsAccept, void* UserData) {
#if defined(HAVE_OPENSSL)
    LqSockBuf* NewBuf = LqFastAlloc::New<LqSockBuf>();
    if(NewBuf == nullptr)
        return nullptr;

    SSL* Ssl;
    int Ret;
    LqFbufFlag Flag = 0;

    if((Ssl = SSL_new((SSL_CTX*)SslCtx)) == nullptr)
        goto lblErr;
    if(SSL_set_fd(Ssl, SockFd) != 1)
        goto lblErr;
    if((Ret = ((IsAccept) ? SSL_accept(Ssl) : SSL_connect(Ssl))) <= 0) {
        switch(SSL_get_error(Ssl, Ret)) {
            case SSL_ERROR_WANT_READ: Flag |= LQFBUF_READ_WOULD_BLOCK; break;
            case SSL_ERROR_WANT_WRITE: Flag |= LQFBUF_WRITE_WOULD_BLOCK; break;
                break;
            default:
                goto lblErr;
        }
    }

    LqConnInit(&NewBuf->Conn, SockFd, &SockBufProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
    if(LqFbuf_open_cookie(&NewBuf->Stream, Ssl, &_SslCookie, LQFBUF_FAST_LK | Flag, 0, 4090, 32768) < 0)
        goto lblErr;
    NewBuf->FirstRcv = NewBuf->LastRcv = NewBuf->FirstRsp = NewBuf->LastRsp = nullptr;
    NewBuf->LastExchangeTime = NewBuf->StartTime = LqTimeGetLocMillisec();
    NewBuf->Flags = LQSOCKBUF_FLAG_USED;
    return NewBuf;
lblErr:
    if(Ssl != nullptr)
        SSL_free(Ssl);
#endif
    return nullptr;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufDelete(LqSockBuf* SockBuf) {
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_USED;
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK) {
        LqEvntSetClose(SockBuf);
        return true;
    }
    LqSockBufRspClear(SockBuf);
    LqSockBufRcvClear(SockBuf);
    LqFbuf_close(&SockBuf->Stream);
    LqFastAlloc::Delete(SockBuf);
    return true;
}


LQ_EXTERN_C bool LQ_CALL LqSockBufGoWork(LqSockBuf* SockBuf, void* WrkBoss) {
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK)
        return false;
    SockBuf->Conn.Flag |= _EvntFlagBySockBuf(SockBuf) | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP;
    if(WrkBoss == nullptr)
        WrkBoss = LqWrkBossGet();
    if(((LqWrkBoss*)WrkBoss)->AddEvntAsync((LqEvntHdr*)&SockBuf->Conn)) {
        SockBuf->Flags |= LQSOCKBUF_FLAG_WORK;
        return true;
    }
    return false;
}


LQ_EXTERN_C bool LQ_CALL LqSockBufRspFile(LqSockBuf* SockBuf, const char* Path) {
    int Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_SEQ | LQ_O_NOINHERIT, 0666);
    if(Fd == -1)
        return false;
    if(!LqSockBufRspFd(SockBuf, Fd)) {
        LqFileClose(Fd);
        return false;
    }
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFilePart(LqSockBuf* SockBuf, const char* Path, LqFileSz OffsetInFile, LqFileSz Count) {
    int Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_SEQ | LQ_O_NOINHERIT, 0666);
    if(Fd == -1)
        return false;
    if(!LqSockBufRspFdPart(SockBuf, Fd, OffsetInFile, Count)) {
        LqFileClose(Fd);
        return false;
    }
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspStream(LqSockBuf* SockBuf, LqFbuf* File) {
    if(!(File->Flags & (LQFBUF_POINTER | LQFBUF_STREAM)))
        return false;
    return _LqSockBufWriteVirtFile(SockBuf, File) != nullptr;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFd(LqSockBuf* SockBuf, int InFd) {
    return _LqSockBufWriteFile(SockBuf, InFd) != nullptr;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspFdPart(LqSockBuf* SockBuf, int InFd, LqFileSz OffsetInFile, LqFileSz Count) {
    if(LqFileSeek(InFd, OffsetInFile, LQ_SEEK_SET) == -1)
        return false;
    return _LqSockBufWriteFdPart(SockBuf, InFd, Count) != nullptr;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintf(LqSockBuf* SockBuf, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufPrintfVa(SockBuf, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfVa(LqSockBuf* SockBuf, const char* Fmt, va_list Va) {
    LqFbuf* Stream = _LqSockBufGetLastWriteStream(SockBuf);
    if(Stream == nullptr)
        return -1;
    return LqFbuf_vprintf(Stream, Fmt, Va);
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufWrite(LqSockBuf* SockBuf, const void* Data, size_t SizeData) {
    LqFbuf* Stream = _LqSockBufGetLastWriteStream(SockBuf);
    if(Stream == nullptr)
        return -1;
    return LqFbuf_write(Stream, Data, SizeData);
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspClear(LqSockBuf* SockBuf) {
    RspElement* Buf, *Next;
    for(Buf = ((RspElement*)SockBuf->Rsp.First); Buf != nullptr; Buf = Next) {
        LqFbuf_close(&Buf->Buf);
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    LqListInit(&SockBuf->Rsp);
    SockBuf->RspHeader = nullptr;
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRspSetHdr(LqSockBuf* SockBuf) {
    LqFbuf* Stream = _LqSockBufGetLastWriteStream(SockBuf);
    if(Stream == nullptr)
        return false;
    Stream->Flags |= LQFBUF_USER_FLAG;
    SockBuf->RspHeader = Stream;
    return true;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfHdr(LqSockBuf* SockBuf, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufPrintfVaHdr(SockBuf, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufPrintfVaHdr(LqSockBuf* SockBuf, const char* Fmt, va_list Va) {
    LqFbuf* Stream = (LqFbuf*)SockBuf->RspHeader;
    if(Stream == nullptr)
        return -1;
    return LqFbuf_vprintf(Stream, Fmt, Va);
}

LQ_EXTERN_C intptr_t LQ_CALL LqSockBufWriteHdr(LqSockBuf* SockBuf, const void* Data, size_t SizeData) {
    LqFbuf* Stream = (LqFbuf*)SockBuf->RspHeader;
    if(Stream == nullptr)
        return -1;
    return LqFbuf_write(Stream, Data, SizeData);
}

LQ_EXTERN_C LqFileSz LQ_CALL LqSockBufRspLen(LqSockBuf* SockBuf) {
    RspElement* Buf;
    LqFileSz Res = 0, b;
    LqFileStat FileStat;
    size_t a;
    int Fd;

    for(Buf = SockBuf->RspHeader ? ((RspElement*)SockBuf->RspHeader) : ((RspElement*)SockBuf->Rsp.First); Buf != nullptr; Buf = Buf->Next) {
        if(Buf->Buf.Flags & (LQFBUF_POINTER | LQFBUF_STREAM)) {
            Res += LqFbuf_sizes(&Buf->Buf, nullptr, nullptr);
        } else if(Buf->Buf.Cookie == &_FilePartCookie) {
            LqFbuf_sizes(&Buf->Buf, nullptr, &a);
            Res += a + (((FilePartData*)Buf->Buf.UserData)->Size - ((FilePartData*)Buf->Buf.UserData)->Readed);
        } else {
            LqFbuf_sizes(&Buf->Buf, nullptr, &a);
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
    return Res;
}

//////////////////////Rcv



typedef unsigned char RcvFlag;

#define LQRCV_FLAG_MATCH ((RcvFlag)0x01)
#define LQRCV_FLAG_READ_REGION ((RcvFlag)0x02)
#define LQRCV_FLAG_RECV_SREAM ((RcvFlag)0x03)
#define LQRCV_FLAG_RECV_HANDLE ((RcvFlag)0x04)

typedef struct RcvElement {
    struct {
        struct {
            char* Fmt;
            int MatchCount;
            size_t MaxSize;
            intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf);
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


LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenMatch(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf), const char* Fmt, int MatchCount, size_t MaxSize) {
    RcvElement* NewStream = nullptr;
    if((NewStream = LqFastAlloc::New<RcvElement>()) == nullptr)
        return false;
    NewStream->Flag = LQRCV_FLAG_MATCH;
    NewStream->Match.Fmt = LqStrDuplicate(Fmt);
    NewStream->Match.MatchCount = MatchCount;
    NewStream->Match.MaxSize = MaxSize;
    NewStream->Match.RcvProc = RcvProc;
    NewStream->Data = UserData;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return true;
}

LQ_EXTERN_C int LQ_CALL LqSockBufScanf(LqSockBuf* SockBuf, bool IsPeek, const char* Fmt, ...) {
    va_list Va;
    va_start(Va, Fmt);
    int Res = LqSockBufScanfVa(SockBuf, IsPeek, Fmt, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqSockBufScanfVa(LqSockBuf* SockBuf, bool IsPeek, const char* Fmt, va_list Va) {
    intptr_t Res = LqFbuf_vscanf(&SockBuf->Stream, (IsPeek) ? LQFRBUF_SCANF_PEEK : 0, Fmt, Va);
    if(SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK))
        lq_errno_set(EWOULDBLOCK);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenCompleteRead(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf), void* Dest, size_t Size) {
    RcvElement* NewStream = nullptr;
    if((NewStream = LqFastAlloc::New<RcvElement>()) == nullptr)
        return false;
    NewStream->Flag = LQRCV_FLAG_READ_REGION;
    NewStream->ReadRegion.Start = Dest;
    NewStream->ReadRegion.End = ((char*)Dest) + Size;
    NewStream->ReadRegion.RcvProc = RcvProc;
    NewStream->Data = UserData;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return true;
}

LQ_EXTERN_C int LQ_CALL LqSockBufRead(LqSockBuf* SockBuf, void* Dest, size_t Size) {
    intptr_t Res = LqFbuf_read(&SockBuf->Stream, Dest, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenCompleteStream(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf, LqFbuf* DestStream), LqFbuf* DestStream, size_t Size) {
    RcvElement* NewStream = nullptr;
    if((NewStream = LqFastAlloc::New<RcvElement>()) == nullptr)
        return false;
    NewStream->Flag = LQRCV_FLAG_RECV_SREAM;
    NewStream->OutStream.DestStream = DestStream;
    NewStream->OutStream.Size = Size;
    NewStream->OutStream.RcvProc = RcvProc;
    NewStream->Data = UserData;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return true;
}

LQ_EXTERN_C int LQ_CALL LqSockBufReadInStream(LqSockBuf* SockBuf, LqFbuf* Dest, size_t Size) {
    intptr_t Res = LqFbuf_transfer(Dest, &SockBuf->Stream, Size);
    if((Res < Size) && (SockBuf->Stream.Flags & (LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
        lq_errno_set(EWOULDBLOCK);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufNotifyWhenRecivedData(LqSockBuf* SockBuf, void* UserData, intptr_t(*RcvProc)(void* UserData, LqSockBuf* Buf)) {
    RcvElement* NewStream = nullptr;
    if((NewStream = LqFastAlloc::New<RcvElement>()) == nullptr)
        return false;
    NewStream->Flag = LQRCV_FLAG_RECV_HANDLE;
    NewStream->RecvData.RcvProc = RcvProc;
    LqListAdd(&SockBuf->Rcv, NewStream, RcvElement);
    if((SockBuf->Flags & LQSOCKBUF_FLAG_WORK) && !(SockBuf->Flags & LQEVNT_FLAG_RD))
        LqEvntSetFlags(SockBuf, (SockBuf->Flags & LQEVNT_FLAG_WR) | (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP), 0);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvCancelLastOperation(LqSockBuf* SockBuf) {
    RcvElement* Buf, *Prev = nullptr;
    for(Buf = ((RcvElement*)SockBuf->Rcv.First); Buf != nullptr; Buf = Buf->Next) {
        if(Buf == SockBuf->Rcv.Last) {
            if(Buf->Flag == LQRCV_FLAG_MATCH)
                free(Buf->Match.Fmt);
            LqFastAlloc::Delete(Buf);
            if(Prev == nullptr)
                SockBuf->Rcv.First = nullptr;
            else
                Prev->Next = nullptr;
            SockBuf->Rcv.Last = Prev;
            return true;
        }
        Prev = Buf;
    }
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqSockBufRcvClear(LqSockBuf* SockBuf) {
    RcvElement* Buf, *Next;
    for(Buf = ((RcvElement*)SockBuf->Rcv.First); Buf != nullptr; Buf = Next) {
        if(Buf->Flag == LQRCV_FLAG_MATCH) {
            free(Buf->Match.Fmt);
        }
        Next = Buf->Next;
        LqFastAlloc::Delete(Buf);
    }
    LqListInit(&SockBuf->Rcv);
    return true;
}

LQ_EXTERN_C void LQ_CALL LqSockBufFlush(LqSockBuf* SockBuf) {
    RecvOrSendHandler(&SockBuf->Conn, LQEVNT_FLAG_WR | LQEVNT_FLAG_RD);
}

//////////////////////////////////Hndlrs

static void LQ_CALL RecvOrSendHandler(LqConn* Sock, LqEvntFlag RetFlags) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    RspElement* WrElem;
    RcvElement *RdElem;
    size_t Size;
    LqEvntFlag ConnFlag = Sock->Flag;
    LqFbufFlag FbufFlags = 0;
    intptr_t Readed2, Readed;

    SockBuf->LastExchangeTime = LqTimeGetLocMillisec();

    if(RetFlags & LQEVNT_FLAG_ERR) {
        SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_SOCKET);
    }
    if(RetFlags & LQEVNT_FLAG_WR) {//Can write
lblWriteAgain:
        WrElem = LqListFirst(&SockBuf->Rsp, RspElement);
        if(WrElem == nullptr) {
            LqFbuf_flush(&SockBuf->Stream);
            goto lblTryRead;
        }
        LqFbuf_transfer(&SockBuf->Stream, &WrElem->Buf, Sock->Proto->MaxSendInSingleTime);
        FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
        if(WrElem->Buf.Flags & LQFBUF_READ_EOF) {
lblErrWrite:
            if(WrElem == SockBuf->RspHeader)
                SockBuf->RspHeader = nullptr;
            LqFbuf_close(&WrElem->Buf);
            LqListRemove(&SockBuf->Rsp, RspElement);
            goto lblWriteAgain;
        }
        if(WrElem->Buf.Flags & (LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK)) {
            SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_OUTPUT);
            goto lblErrWrite;
        }

    }
lblTryRead:
    if(RetFlags & LQEVNT_FLAG_RD) {//Can read
lblReadAgain:
        RdElem = LqListFirst(&SockBuf->Rcv, RcvElement);
        if(RdElem == nullptr) {
            LqFbuf_peek(&SockBuf->Stream, nullptr, 1);
            goto lblOut;
        }
        switch(RdElem->Flag) {
            case LQRCV_FLAG_MATCH:
                Readed2 = LqFbuf_scanf(&SockBuf->Stream, LQFRBUF_SCANF_PEEK, RdElem->Match.Fmt);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                Readed2 = lq_max(Readed2, 0);
                if((Readed2 >= RdElem->Match.MatchCount) && (SockBuf->Stream.InBuf.Len >= RdElem->Match.MaxSize)) {
                    RdElem->Match.RcvProc(RdElem->Data, SockBuf);
                    free(RdElem->Match.Fmt);
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                }
                break;
            case LQRCV_FLAG_READ_REGION:
                Readed2 = (char*)RdElem->ReadRegion.End - (char*)RdElem->ReadRegion.Start;
                Readed = (RdElem->ReadRegion.IsPeek) ? LqFbuf_peek(&SockBuf->Stream, RdElem->ReadRegion.Start, Readed2) : LqFbuf_read(&SockBuf->Stream, RdElem->ReadRegion.Start, Readed2);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                Readed = lq_max(Readed, 0);
                if(Readed >= Readed2) {
                    RdElem->ReadRegion.RcvProc(RdElem->Data, SockBuf);
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                }
                if(RdElem->ReadRegion.Start != nullptr)
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
                    RdElem->OutStream.RcvProc(RdElem->Data, SockBuf, RdElem->OutStream.DestStream);
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                }
                break;
            case LQRCV_FLAG_RECV_HANDLE:
                Readed = RdElem->RecvData.RcvProc(RdElem->Data, SockBuf);
                FbufFlags |= (SockBuf->Stream.Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR));
                if(Readed < 0) {
                    LqListRemove(&SockBuf->Rcv, RcvElement);
                    LqFastAlloc::Delete(RdElem);
                    goto lblReadAgain;
                }
                break;
        }
    }
lblOut:
    if(FbufFlags & (LQFBUF_WRITE_ERROR | LQFBUF_READ_ERROR))
        SockBuf->ErrHandler(SockBuf, LQSOCKBUF_ERR_SOCKET);
    if(SockBuf->Flags & LQSOCKBUF_FLAG_WORK) {
        LqFbuf_sizes(&SockBuf->Stream, &Size, nullptr);
        if((LqListFirst(&SockBuf->Rsp, RspElement) != nullptr) || (Size > 0) || (FbufFlags & LQFBUF_WRITE_WOULD_BLOCK))
            ConnFlag |= LQEVNT_FLAG_WR;
        else
            ConnFlag &= ~LQEVNT_FLAG_WR;

        if((LqListFirst(&SockBuf->Rcv, RcvElement) != nullptr) || (FbufFlags & LQFBUF_READ_WOULD_BLOCK))
            ConnFlag |= LQEVNT_FLAG_RD;
        else
            ConnFlag &= ~LQEVNT_FLAG_RD;
        LqEvntSetFlags(Sock, ConnFlag, 0);
    }
}

static void LQ_CALL CloseHandler(LqConn* Sock) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    if(SockBuf->CloseHandler != nullptr)
        SockBuf->CloseHandler(SockBuf);
    SockBuf->Flags &= ~LQSOCKBUF_FLAG_WORK;
    if(!(SockBuf->Flags & LQSOCKBUF_FLAG_USED)) {
        LqSockBufDelete(SockBuf);
    }
}

static bool LQ_CALL CmpAddressProc(LqConn* Conn, const void* Address) {
    LqConnInetAddress Addr;
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

static bool LQ_CALL KickByTimeOutProc(
    LqConn*        Sock,
    LqTimeMillisec CurrentTime,
    LqTimeMillisec EstimatedLiveTime
) {
    LqSockBuf* SockBuf = (LqSockBuf*)Sock;
    LqTimeMillisec TimeDiff = CurrentTime - SockBuf->LastExchangeTime;
    return TimeDiff > EstimatedLiveTime;
}

static char* LQ_CALL DebugInfoProc(LqConn* Conn) {
    return nullptr;
}

static LqEvntFlag _EvntFlagBySockBuf(LqSockBuf* SockBuf) {
    return ((LqListFirst(&SockBuf->Rsp, RspElement) != nullptr) ? LQEVNT_FLAG_WR : 0) |
        ((LqListFirst(&SockBuf->Rcv, RcvElement) != nullptr) ? LQEVNT_FLAG_RD : 0);
}

#define __METHOD_DECLS__
#include "LqAlloc.hpp"