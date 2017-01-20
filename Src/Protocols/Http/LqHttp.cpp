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
#include "LqHttpRsp.h"
#include "LqEvnt.h"
#include "LqTime.h"
#include "LqHttpMdl.h"
#include "LqAtm.hpp"
#include "LqHttpConn.h"
#include "LqHttpRcv.h"
#include "LqHttpMdlHandlers.h"
#include "LqFileTrd.h"
#include "LqHttpLogging.h"
#include "LqHttpAct.h"
#include "LqDfltRef.hpp"
#include "LqStr.hpp"
#include "LqWrkBoss.h"
#include "LqZmbClr.h"
#include "LqShdPtr.hpp"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/dh.h>
#endif

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#define LqHttpConnGetRmtAddr(ConnectionPointer) ((sockaddr*)((LqHttpConn*)ConnectionPointer + 1))


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct HttpConnIp4 {
    LqHttpConn Conn;
    sockaddr_in Ip4Addr;
};

struct HttpConnIp6 {
    LqHttpConn Conn;
    sockaddr_in6 Ip6Addr;
};

#pragma pack(pop)

/*
* Protocol managment procs.
*/
static void LQ_CALL LqHttpCoreHandler(LqConn* Connection, LqEvntFlag Flag);
static void LQ_CALL LqHttpCoreCloseHandler(LqConn* Connection);
static bool LQ_CALL LqHttpCoreCmpIpAddress(LqConn* Connection, const void* Address);
static void LQ_CALL LqHttpFreeProtoNotifyProc(LqProto* This);
static char*LQ_CALL LqHttpCoreDbgInfoProc(LqConn* Connection);
static bool LQ_CALL LqHttpCoreKickByTimeOutProc(LqConn* Connection, LqTimeMillisec CurrentTimeMillisec, LqTimeMillisec EstimatedLiveTime);
static bool LqHttpMultipartAddHeaders(LqHttpMultipartHeaders** CurMultipart, const char* Buf, size_t BufLen);

static void LQ_CALL LqHttpCoreAcceptHandler(LqConn* Connection, LqEvntFlag Flag);
static void LQ_CALL LqHttpCoreBindCloseHandler(LqConn* Connection);

static void LqHttpCoreRcvMultipartSkipToHdr(LqHttpConn* c);
static void LqHttpRcvMultipartReadHdr(LqHttpConn* c);
static void LqHttpRcvMultipartReadInFile(LqHttpConn* c);
static void LqHttpRcvMultipartReadInStream(LqHttpConn* c);

static void LqHttpCoreRspHdr(LqHttpConn* c);
static void LqHttpCoreRspCache(LqHttpConn* c);
static void LqHttpCoreRspFd(LqHttpConn* c);
static void LqHttpCoreRspStream(LqHttpConn* c);

static void LqHttpQurReadHeaders(LqHttpConn* Connection);
static void LqHttpParseHeader(LqHttpConn* c, LqHttpQuery* q, char* StartKey, char* StartVal, char* EndVal, size_t* CountHeders);

void LqCachedFileHdr::Cache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, LqFileStat const* Stat) {
    Etag = nullptr;
    MimeType = nullptr;
    memset(&Hash, 0, sizeof(Hash));
}

void LqCachedFileHdr::Recache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, LqFileStat const* Stat) {
    if(Etag != nullptr)
        free(Etag), Etag = nullptr;
    if(MimeType != nullptr)
        free(MimeType), MimeType = nullptr;
    memset(&Hash, 0, sizeof(Hash));
}

void LqCachedFileHdr::GetMD5(const void* CacheInterator, LqMd5* Dest) {
    static const LqMd5 ZeroHash = {0};
    if(LqMd5Compare(&Hash, &ZeroHash) == 0) {
        Hash.data[0] = 1;
        LqMd5Gen(&Hash, ((LqFileChe<LqCachedFileHdr>::CachedFile*)CacheInterator)->Buf, ((LqFileChe<LqCachedFileHdr>::CachedFile*)CacheInterator)->SizeFile);
    }
    memcpy(Dest, &Hash, sizeof(Hash));
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoCreateSSL
(
    LqHttpProtoBase* Reg,
    const void* MethodSSL, /* Example SSLv23_method()*/
    const char* CertFile, /* Example: "server.pem"*/
    const char* KeyFile, /*Example: "server.key"*/
    const char* CipherList,
    int TypeCertFile, /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
    const char* CAFile,
    const char* DhpFile
) {
#ifdef HAVE_OPENSSL

    LqAtmLkWr(Reg->sslLocker);
    SSL_CTX* NewCtx = (SSL_CTX*)LqConnSslCreate(MethodSSL, MethodSSL, CertFile, KeyFile, CipherList, TypeCertFile, CAFile, DhpFile);
    if(NewCtx == nullptr) {
        LqAtmUlkWr(Reg->sslLocker);
        return -1;
    }

    if(Reg->ssl_ctx != nullptr)
        SSL_CTX_free(Reg->ssl_ctx);
    Reg->ssl_ctx = NewCtx;

    LqAtmUlkWr(Reg->sslLocker);
    return 0;
#else
    return false;
#endif
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoSetSSL(LqHttpProtoBase* Reg, void* SSL_Ctx) {
#ifdef HAVE_OPENSSL
    LqAtmLkWr(Reg->sslLocker);
    if(Reg->ssl_ctx != nullptr)
        SSL_CTX_free(Reg->ssl_ctx);
    Reg->ssl_ctx = SSL_Ctx;
    LqAtmUlkWr(Reg->sslLocker);
    return 0;
#else
    return -1;
#endif

}

LQ_EXTERN_C void LQ_CALL LqHttpProtoRemoveSSL(LqHttpProtoBase* Reg) {
#ifdef HAVE_OPENSSL
    bool r = false;
    LqAtmLkWr(Reg->sslLocker);
    if(Reg->ssl_ctx != nullptr) {
        SSL_CTX_free(Reg->ssl_ctx);
        Reg->ssl_ctx = nullptr;
    }
    LqAtmUlkWr(Reg->sslLocker);
#endif
}

LQ_EXTERN_C LqHttpProtoBase* LQ_CALL LqHttpProtoCreate() {
    LqHttpProto* r = LqFastAlloc::New<LqHttpProto>();
    if(r == nullptr)
        return nullptr;
    LqProtoInit(&r->Base.Proto);
    r->Base.Proto.Handler = LqHttpCoreHandler;
    r->Base.Proto.CloseHandler = LqHttpCoreCloseHandler;
    r->Base.Proto.KickByTimeOutProc = LqHttpCoreKickByTimeOutProc;
    r->Base.Proto.CmpAddressProc = LqHttpCoreCmpIpAddress;
    r->Base.Proto.DebugInfoProc = LqHttpCoreDbgInfoProc;

    r->Base.BindProto.UserData = r->Base.Proto.UserData = r;

    LqProtoInit(&r->Base.BindProto);
    r->Base.BindProto.Handler = LqHttpCoreAcceptHandler;
    r->Base.BindProto.CloseHandler = LqHttpCoreBindCloseHandler;

    r->Base.ZmbClr.Fd = -1;


    LqConnInit(&r->Base.Conn, -1, &r->Base.BindProto, LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_RD);

    r->Base.UseDefaultDmn = true;
    r->Base.IsResponse429 = false;
    r->Base.CountConnections = 0;
    r->Base.IsUnregister = false;

    r->Base.TimeLive = 1000 * 60 * 5; //5 min

    r->Base.MaxHeadersSize = 32 * 1024; //32 kByte
    r->Base.MaxMultipartHeadersSize = 32 * 1024;
    r->Base.Proto.MaxSendInTact = 32 * 1024;
    r->Base.Proto.MaxReciveInSingleTime = 32 * 1024 * 4;
    r->Base.Proto.MaxSendInSingleTime = 32 * 1024 * 4;

    r->Base.BindProto.MaxSendInTact = 32 * 1024;
    r->Base.BindProto.MaxReciveInSingleTime = 32 * 1024;
    r->Base.BindProto.MaxSendInSingleTime = 32 * 1024;

    LqAtmLkInit(r->Base.BindLocker);


    r->Base.PeriodChangeDigestNonce = 5; //5 Sec

    r->Port = "80";
    r->Host = "";
    r->Base.MaxConnections = 65000;
    r->Base.RouteProtoFamily = AF_INET;

    LqAtmLkInit(r->Base.ServNameLocker);
    LqHttpProtoSetNameServer(&r->Base, "Lanq(Lan Quick) 1.0");

    r->Cache.SetMaxSize(1024 * 1024 * 400);
    LqFbuf_snprintf(r->Base.HTTPProtoVer, sizeof(r->Base.HTTPProtoVer), "1.1");
    LqHttpPthDmnCreate(&r->Base, "*");
#ifdef HAVE_OPENSSL
    r->Base.ssl_ctx = nullptr;
    LqAtmLkInit(r->Base.sslLocker);
#endif
    LqHttpMdlInit(&r->Base, &r->Base.StartModule, "StartModule", 0);
    r->CountPointers = 0;
    LqObPtrReference(r); //Main reference

    return &r->Base;
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoDelete(LqHttpProtoBase* Proto) {
    LqHttpMdlFreeAll(Proto);
    LqHttpMdlFreeMain(Proto);
    LqAtmLkWr(Proto->BindLocker);

    if(Proto->ZmbClr.Fd != -1)
        LqZmbClrUninit(&Proto->ZmbClr);

    LqWrkBossCloseConnByProtoSync(&Proto->Proto);
    LqWrkBossCloseConnByProtoSync(&Proto->BindProto);
#ifdef HAVE_OPENSSL
    if(Proto.ssl_ctx != nullptr)
        LqAtmLkInit(r->Base.sslLocker);
#endif
    LqObPtrDereference<LqHttpProto, LqFastAlloc::Delete>((LqHttpProto*)Proto);
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoBind(LqHttpProtoBase* Reg) {
    LqHttpProto* Proto = (LqHttpProto*)Reg;
    LqAtmLkWr(Reg->BindLocker);
    if(Reg->Conn.Fd != -1) {
        LqEvntSetClose(&Reg->Conn);
        volatile int* Test = &Reg->Conn.Fd;
        while(*Test != -1);
    }
    LqWrkBossSetMinWrkCount(1);
    if(Reg->ZmbClr.Fd == -1) {

        LqZmbClrInit(
            &Reg->ZmbClr,
            &Reg->Proto,
            Reg->TimeLive,
            [](LqEvntFd* EventFd, void* Data) -> bool {
            auto Ob = (LqHttpProto*)Data;
            LqObPtrDereference<LqHttpProto, LqFastAlloc::Delete>(Ob);
            return 1;
        },
            Reg
            );

        LqObPtrReference(Proto); //Reference by zombie killer
        LqWrkBossAddEvntSync((LqEvntHdr*)&Reg->ZmbClr);

    }
    int BindedSock = LqConnBind(Proto->Host.c_str(), Proto->Port.c_str(), Proto->Base.RouteProtoFamily, SOCK_STREAM, IPPROTO_TCP, Proto->Base.MaxConnections, true);

    if(BindedSock == -1) {
        LqAtmUlkWr(Reg->BindLocker);
        return -1;
    }

    LqConnInit(&Reg->Conn, BindedSock, &Reg->BindProto, LQEVNT_FLAG_ACCEPT);

    LqObPtrReference(Proto); //Reference by bineded sock

    LqWrkBossAddEvntSync((LqEvntHdr*)&Reg->Conn);

    LqAtmUlkWr(Reg->BindLocker);
    return 0;
}

LQ_EXTERN_C bool LQ_CALL LqHttpProtoIsBind(LqHttpProtoBase* Reg) {
    return Reg->Conn.Fd != -1;
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoUnbind(LqHttpProtoBase* Reg) {
    LqHttpProto* Proto = (LqHttpProto*)Reg;
    LqAtmLkWr(Reg->BindLocker);
    int r = -1;
    if(Reg->Conn.Fd != -1) {
        LqEvntSetClose3(&Reg->Conn);
        volatile int* Test = &Reg->Conn.Fd;
        while(*Test != -1);
        r = 0;
    }
    if(Reg->ZmbClr.Fd != -1)
        LqZmbClrUninit(&Reg->ZmbClr);
    LqAtmUlkWr(Reg->BindLocker);
    return r;
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoGetInfo(LqHttpProtoBase* Reg, char* Host, size_t HostBufSize, char* Port, size_t PortBufSize, int* RouteProtoFamily, int* MaxConnections, LqTimeMillisec* TimeLive) {
    LqHttpProto* Proto = (LqHttpProto*)Reg;
    LqAtmLkWr(Reg->BindLocker);
    if(Port != nullptr)
        LqStrCopyMax(Port, Proto->Port.c_str(), PortBufSize);
    if(Host != nullptr)
        LqStrCopyMax(Host, Proto->Host.c_str(), HostBufSize);
    if(RouteProtoFamily != nullptr)
        *RouteProtoFamily = Proto->Base.RouteProtoFamily;
    if(MaxConnections != nullptr)
        *MaxConnections = Proto->Base.MaxConnections;
    if(TimeLive != nullptr)
        *TimeLive = Proto->Base.TimeLive;
    LqAtmUlkWr(Reg->BindLocker);
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpProtoSetInfo(LqHttpProtoBase* Reg, const char* Host, const char* Port, const int * RouteProtoFamily, const int* MaxConnections, const LqTimeMillisec* TimeLive) {
    LqHttpProto* Proto = (LqHttpProto*)Reg;
    LqAtmLkWr(Reg->BindLocker);

    if(Port != nullptr)
        Proto->Port = Port;

    if(Host != nullptr)
        Proto->Host = Host;

    if(RouteProtoFamily != nullptr)
        Proto->Base.RouteProtoFamily = *RouteProtoFamily;
    if(MaxConnections != nullptr)
        Proto->Base.MaxConnections = *MaxConnections;

    if(TimeLive != nullptr) {
        Proto->Base.TimeLive = *TimeLive;
        if(Proto->Base.ZmbClr.Fd != -1) {
            LqZmbClrSetTimeLive(&Proto->Base.ZmbClr, Proto->Base.TimeLive);
        }
    }
    LqAtmUlkWr(Reg->BindLocker);
    return 0;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpProtoSetNameServer(LqHttpProtoBase* Reg, const char* NewName) {
    size_t SizeWritten = 0;
    LqAtmLkWr(Reg->ServNameLocker);
    LqStrCopyMax(Reg->ServName, NewName, sizeof(Reg->ServName));
    SizeWritten = LqStrLen(Reg->ServName);
    LqAtmUlkWr(Reg->ServNameLocker);
    return SizeWritten;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpProtoGetNameServer(LqHttpProtoBase* Reg, char* Name, size_t SizeName) {
    size_t SizeWritten = 0;
    LqAtmLkRd(Reg->ServNameLocker);
    LqStrCopyMax(Name, Reg->ServName, SizeName);
    SizeWritten = LqStrLen(Reg->ServName);
    LqAtmUlkRd(Reg->ServNameLocker);
    return SizeWritten;
}

LQ_EXTERN_C void LQ_CALL LqHttpEvntDfltIgnoreAnotherEventHandler(LqHttpConn* c) {
    switch(c->ActionState) {
        case LQHTTPACT_STATE_MULTIPART_RCV_FILE:
        case LQHTTPACT_STATE_RCV_FILE: //Если закончился этап получения файла
        {
            if(c->ActionResult != LQHTTPACT_RES_OK) {
                LqHttpRcvFileCancel(c);
                LqHttpRspError(c, 500);
                break;
            }
            switch(LqHttpRcvFileCommit(c)) {
                case LQHTTPRCV_UPDATED:
                    LqHttpRspError(c, 200);
                    break;
                case LQHTTPRCV_CREATED:
                    LqHttpRspError(c, 201);
                    break;
                default:
                case LQHTTPRCV_ERR:
                    LqHttpRspError(c, 500);
                    break;
            }
        }
        break;
        case LQHTTPACT_STATE_RESPONSE_HANDLE_PROCESS:
            LqHttpRspError(c, 501);
            break;
        case LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS:
        case LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS:
        case LQHTTPACT_STATE_MULTIPART_RCV_HDRS:
        case LQHTTPACT_STATE_RCV_STREAM:
        case LQHTTPACT_STATE_MULTIPART_RCV_STREAM:
            LqHttpRspError(c, 500);
            break;
        case LQHTTPACT_STATE_SKIP_QUERY_BODY:
        case LQHTTPACT_STATE_RSP:
        case LQHTTPACT_STATE_RSP_CACHE:
        case LQHTTPACT_STATE_RSP_FD:
        case LQHTTPACT_STATE_RSP_STREAM:
            if(c->ActionResult != LQHTTPACT_RES_OK)
                LqHttpActSwitchToClose(c);
            break;
    }
}

/*
* C functions shell for cache
*/

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetMaxSize(LqHttpProtoBase* Reg) {
    return ((LqHttpProto*)Reg)->Cache.GetMaxSize();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetMaxSize(LqHttpProtoBase* Reg, size_t NewVal) {
    ((LqHttpProto*)Reg)->Cache.SetMaxSize(NewVal);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetMaxSizeFile(LqHttpProtoBase* Reg) {
    return ((LqHttpProto*)Reg)->Cache.GetMaxSizeFile();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetMaxSizeFile(LqHttpProtoBase* Reg, size_t NewVal) {
    ((LqHttpProto*)Reg)->Cache.SetMaxSizeFile(NewVal);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetEmployedSize(LqHttpProtoBase* Reg) {
    return ((LqHttpProto*)Reg)->Cache.GetEmployedSize();
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqHttpCheGetPeriodUpdateStat(LqHttpProtoBase* Reg) {
    return ((LqHttpProto*)Reg)->Cache.GetPeriodUpdateStat();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetPeriodUpdateStat(LqHttpProtoBase* Reg, LqTimeMillisec Millisec) {
    ((LqHttpProto*)Reg)->Cache.SetPeriodUpdateStat(Millisec);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetMaxCountOfPrepared(LqHttpProtoBase* Reg) {
    return ((LqHttpProto*)Reg)->Cache.GetMaxCountOfPrepared();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetMaxCountOfPrepared(LqHttpProtoBase* Reg, size_t Count) {
    ((LqHttpProto*)Reg)->Cache.SetMaxCountOfPrepared(Count);
}


static void LQ_CALL LqHttpCoreCloseHandler(LqConn* Connection) {
    LqHttpConn* c = (LqHttpConn*)Connection;
    LqHttpProto* r = LqHttpGetReg(c);
    c->EventClose(c);

    r->DisconnectHndls.Call(c);
    LqHttpActKeepOnlyHeaders(c);
    LqHttpConnPthRemove(c);
    if(c->Buf != nullptr)
        ___free(c->Buf);
    if(c->UserData != nullptr)
        LqFastAlloc::ReallocCount<LqHttpUserData>(c->UserData, c->UserDataCount, 0);
    LqAtmIntrlkDec(r->Base.CountConnections);
    if(r->Base.IsUnregister && (r->Base.CountConnections == 0))
        LqFastAlloc::Delete(r);
#ifdef HAVE_OPENSSL
    if(c->ssl != nullptr) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
#endif
    //shutdown(Connection->Fd, SHUT_RD | SHUT_WR | SHUT_RDWR);
    closesocket(Connection->Fd);
    switch(LqHttpConnGetRmtAddr(c)->sa_family) {
        case AF_INET:
            LqFastAlloc::Delete((HttpConnIp4*)Connection);
            break;
        case AF_INET6:
            LqFastAlloc::Delete((HttpConnIp6*)Connection);
            break;
        default:
            LqFastAlloc::JustDelete(Connection);
    }

}

static void LQ_CALL LqHttpCoreHandler(LqConn* Connection, LqEvntFlag Flag) {
    LqHttpConn* c = (LqHttpConn*)Connection;
    c->TimeLastExchangeMillisec = LqTimeGetLocMillisec();
    LqHttpActState OldAct = (LqHttpActState)-1;

    if(Flag & LQEVNT_FLAG_ERR) {
        LqEvntSetClose(Connection);
        LQHTTPLOG_ERR("LqHttpCoreErrorProc: have error when wait event on socket\n");
    } else if(Flag & LQEVNT_FLAG_WR) {
lblSwitch2:
        switch(OldAct = c->ActionState) {
            case LQHTTPACT_STATE_RSP:
                LqHttpCoreRspHdr(c);
lblResponseResult:
                switch(c->ActionResult) {
                    case LQHTTPACT_RES_PARTIALLY: return;
                    case LQHTTPACT_RES_OK:
                        LqHttpConnCallEvntAct(c);
                        if(c->Flags & LQHTTPCONN_FLAG_CLOSE) {
                            LqEvntSetClose(c);
                            return;
                        }
                        {
                            auto t = c->Response.HeadersStart;
                            c->Response.HeadersStart = 0;
                            ((LqHttpProto*)LqHttpProtoGetByConn(c))->EndResponseHndls.Call(c);
                            c->Response.HeadersStart = t;
                        }
                        LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);
                        LqHttpActSwitchToRcv(c);
                        LqEvntSetFlags(c, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                        return;
                    default:
                        LqHttpConnCallEvntAct(c);
                        LqEvntSetClose(c);
                        return;
                }
            case LQHTTPACT_STATE_RSP_CACHE:
                LqHttpCoreRspCache(c);
                goto lblResponseResult;
            case LQHTTPACT_STATE_RSP_FD:
                LqHttpCoreRspFd(c);
                goto lblResponseResult;
            case LQHTTPACT_STATE_RSP_STREAM:
                LqHttpCoreRspStream(c);
                goto lblResponseResult;
            case LQHTTPACT_STATE_RSP_INIT_HANDLE:
                LqHttpConnCallEvntAct(c);
                if(OldAct != c->ActionState)
                    return LqHttpCoreHandler(&c->CommonConn, LQEVNT_FLAG_RD);
                if(!(c->Flags & _LQEVNT_FLAG_USER_SET))
                    LqEvntSetFlags(c, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                return;
            case LQHTTPACT_STATE_RESPONSE_SSL_HANDSHAKE:
            case LQHTTPACT_STATE_QUERY_SSL_HANDSHAKE:
            {
#ifdef HAVE_OPENSSL
                auto SslAcptErr = SSL_accept(c->ssl);
                if(SslAcptErr < 0) {
                    if(((SslAcptErr == SSL_ERROR_WANT_READ) || (SslAcptErr == SSL_ERROR_WANT_WRITE))) {
                        return;
                    } else {
                        c->ActionResult = LQHTTPACT_RES_SSL_FAILED_HANDSHAKE;
                        LqEvntSetClose(c);
                        return;
                    }
                }
                LqEvntSetFlags(c, ((OldAct == LQHTTPACT_STATE_RESPONSE_SSL_HANDSHAKE) ? LQEVNT_FLAG_RD : LQEVNT_FLAG_WR) | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
                c->ActionState = LQHTTPACT_STATE_GET_HDRS;
#endif
            }
            return;
            case LQHTTPACT_STATE_RESPONSE_HANDLE_PROCESS:
            case LQHTTPACT_STATE_QUERY_HANDLE_PROCESS:
            {
                LqHttpActState OldAct = (LqHttpActState)-1;

                LqHttpConnCallEvntAct(c);

                if((c->CommonConn.Flag & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR)) == 0) //Is locked read or write
                    return;
                if((OldAct == c->ActionState) && (c->ActionResult != LQHTTPACT_RES_BEGIN)) {
                    //If EventAct don`t change state
                    LQHTTPLOG_ERR("module \"%s\" dont change action state", LqHttpMdlGetByConn(c)->Name);
                    if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER) {
                        static const char ErrCode[] =
                            "HTTP/1.1 500 Internal Server Error\r\n"
                            "Connection: close\r\n"
                            "Content-Type: text/html; charset=\"UTF-8\"\r\n"
                            "Content-Length: 25\r\n"
                            "\r\n"
                            "500 Internal Server Error";
                        LqHttpConnSend_Native(c, ErrCode, sizeof(ErrCode) - 1);
                    }
                    c->ActionResult = LQHTTPACT_RES_INVALID_ACT_CHAIN;
                    LqEvntSetClose(c);
                    return;
                }
            }
            goto lblSwitch2;
        }
        return;
    } else if(Flag & LQEVNT_FLAG_RD) {
lblSwitch:
        if((c->CommonConn.Flag & (LQEVNT_FLAG_RD | LQEVNT_FLAG_WR)) == 0) //Is locked read or write
            return;

        if((OldAct == c->ActionState) && (c->ActionResult != LQHTTPACT_RES_BEGIN)) {
            //If EventAct don`t change state
            LQHTTPLOG_ERR("module \"%s\" don`t change action state\n", LqHttpMdlGetByConn(c)->Name);
            if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER) {
                static const char ErrCode[] =
                    "HTTP/1.1 500 Internal Server Error\r\n"
                    "Connection: close\r\n"
                    "Content-Type: text/html; charset=\"UTF-8\"\r\n"
                    "Content-Length: 25\r\n"
                    "\r\n"
                    "500 Internal Server Error";
                LqHttpConnSend_Native(c, ErrCode, sizeof(ErrCode) - 1);
            }
            c->ActionResult = LQHTTPACT_RES_INVALID_ACT_CHAIN;
            LqEvntSetClose(c);
            return;
        }
        switch(OldAct = c->ActionState) {
            case LQHTTPACT_STATE_GET_HDRS:
                LqHttpQurReadHeaders(c);
                if(c->ActionResult == LQHTTPACT_RES_PARTIALLY)
                    return;
                goto lblSwitch;
            case LQHTTPACT_STATE_RESPONSE_HANDLE_PROCESS:
            case LQHTTPACT_STATE_QUERY_HANDLE_PROCESS:
                LqHttpConnCallEvntAct(c);
                goto lblSwitch;
            case LQHTTPACT_STATE_RCV_FILE:
                if(LqHttpConnReciveInFile(c, c->Query.OutFd, c->Query.PartLen - c->ReadedBodySize) < 0) {
                    c->ActionResult = LQHTTPACT_RES_FILE_WRITE_ERR;
                } else if(c->ReadedBodySize < c->Query.PartLen) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    return;
                } else {
                    c->ActionResult = LQHTTPACT_RES_OK;
                }
                LqHttpConnCallEvntAct(c);
                goto lblSwitch;
            case LQHTTPACT_STATE_RCV_STREAM:
                if(LqHttpConnReciveInStream(c, &c->Query.Stream, c->Query.PartLen - c->ReadedBodySize) < 0) {
                    c->ActionResult = LQHTTPACT_RES_STREAM_WRITE_ERR;
                } else if(c->ReadedBodySize < c->Query.PartLen) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    return;
                } else {
                    c->ActionResult = LQHTTPACT_RES_OK;
                }
                LqHttpConnCallEvntAct(c);
                goto lblSwitch;
                ////////
            case LQHTTPACT_STATE_RCV_INIT_HANDLE:
                LqHttpConnCallEvntAct(c);
                if(OldAct != c->ActionState)
                    goto lblSwitch;
                return;
            case LQHTTPACT_STATE_RSP_INIT_HANDLE:
                LqHttpConnCallEvntAct(c);
                if(OldAct != c->ActionState)
                    goto lblSwitch;
                if(!(c->Flags & _LQEVNT_FLAG_USER_SET))
                    LqEvntSetFlags(c, LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                return;
                ////////
            case LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS:
                LqHttpCoreRcvMultipartSkipToHdr(c);
                if(c->ActionResult == LQHTTPACT_RES_PARTIALLY) return;
                LqHttpConnCallEvntAct(c);
                goto lblSwitch;
            case LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS:
            {
                LqHttpCoreRcvMultipartSkipToHdr(c);
                switch(c->ActionResult) {
                    case LQHTTPACT_RES_PARTIALLY: return;
                    default:
                        LqHttpConnCallEvntAct(c);
                        goto lblSwitch;
                    case LQHTTPACT_RES_OK: OldAct = c->ActionState = LQHTTPACT_STATE_MULTIPART_RCV_HDRS;
                }
            }
            case LQHTTPACT_STATE_MULTIPART_RCV_HDRS:
            {
                LqHttpRcvMultipartReadHdr(c);
                if(c->ActionResult == LQHTTPACT_RES_PARTIALLY)
                    return;
                LqHttpConnCallEvntAct(c);
            }
            goto lblSwitch;
            case LQHTTPACT_STATE_MULTIPART_RCV_FILE:
            {
                LqHttpRcvMultipartReadInFile(c);
                if(c->ActionResult == LQHTTPACT_RES_PARTIALLY)
                    return;
                LqHttpConnCallEvntAct(c);
            }
            goto lblSwitch;
            case LQHTTPACT_STATE_MULTIPART_RCV_STREAM:
            {
                LqHttpRcvMultipartReadInStream(c);
                if(c->ActionResult == LQHTTPACT_RES_PARTIALLY)
                    return;
                LqHttpConnCallEvntAct(c);
            }
            goto lblSwitch;
            case LQHTTPACT_STATE_RESPONSE_SSL_HANDSHAKE:
            case LQHTTPACT_STATE_QUERY_SSL_HANDSHAKE:
            {
#ifdef HAVE_OPENSSL
                auto SslAcptErr = SSL_accept(c->ssl);
                if(SslAcptErr < 0) {
                    if(((SslAcptErr == SSL_ERROR_WANT_READ) || (SslAcptErr == SSL_ERROR_WANT_WRITE))) {
                        return;
                    } else {
                        c->ActionResult = LQHTTPACT_RES_SSL_FAILED_HANDSHAKE;
                        LqEvntSetClose(c);
                        return;
                    }
                }
                LqEvntSetFlags(c, ((OldAct == LQHTTPACT_STATE_RESPONSE_SSL_HANDSHAKE) ? LQEVNT_FLAG_RD : LQEVNT_FLAG_WR) | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
                c->ActionState = LQHTTPACT_STATE_GET_HDRS;
#endif
            }
            return;
            ////////
            case LQHTTPACT_STATE_SKIP_QUERY_BODY: lblSkip:
            {
                c->Response.CountNeedRecive -= LqHttpConnSkip(c, c->Response.CountNeedRecive);
                if(c->Response.CountNeedRecive > 0) {
                    if(c->ActionState == LQHTTPACT_STATE_RSP) {
                        c->ActionState = LQHTTPACT_STATE_SKIP_QUERY_BODY;
                        if(!(c->Flags & _LQEVNT_FLAG_USER_SET))
                            LqEvntSetFlags(c, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                    }
                    return;
                } else {
                    if(c->Flags & LQHTTPCONN_FLAG_CLOSE) {
                        LqEvntSetClose(c);
                        return;
                    }
                    {
                        auto t = c->Response.HeadersStart;
                        c->Response.HeadersStart = 0;
                        ((LqHttpProto*)LqHttpProtoGetByConn(c))->EndResponseHndls.Call(c);
                        c->Response.HeadersStart = t;
                    }
                    LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);
                    LqHttpActSwitchToRcv(c);
                    if(!(c->Flags & _LQEVNT_FLAG_USER_SET))
                        LqEvntSetFlags(c, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                    return;
                }
            }
            case LQHTTPACT_STATE_RSP:
                LqHttpCoreRspHdr(c);
lblResponseResult2:
                switch(c->ActionResult) {
                    case LQHTTPACT_RES_OK:
                        LqHttpConnCallEvntAct(c); //Send OK state event to user //If you dont want this event, then set empty function instead this
                        if(c->Response.CountNeedRecive > 0)
                            goto lblSkip;
                        if(c->Flags & LQHTTPCONN_FLAG_CLOSE) {
                            LqEvntSetClose(c);
                            return;
                        }
                        LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);
                        {
                            auto t = c->Response.HeadersStart;
                            c->Response.HeadersStart = 0;
                            ((LqHttpProto*)LqHttpProtoGetByConn(c))->EndResponseHndls.Call(c);
                            c->Response.HeadersStart = t;
                        }
                        LqHttpActSwitchToRcv(c);
                        LqEvntSetFlags(c, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                        return;
                    case LQHTTPACT_RES_PARTIALLY:
                        if(!(c->Flags & _LQEVNT_FLAG_USER_SET))
                            LqEvntSetFlags(c, LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
                        return;
                    default:
                        LqHttpConnCallEvntAct(c); //Send Error event to user
                        LqEvntSetClose(c);
                        return;
                }
            case LQHTTPACT_STATE_RSP_CACHE:
                LqHttpCoreRspCache(c);
                goto lblResponseResult2;
            case LQHTTPACT_STATE_RSP_FD:
                LqHttpCoreRspFd(c);
                goto lblResponseResult2;
            case LQHTTPACT_STATE_RSP_STREAM:
                LqHttpCoreRspStream(c);
                goto lblResponseResult2;
            case LQHTTPACT_STATE_CLS_CONNECTION:
                LqEvntSetClose(c);
                return;
        }
    }
}


/*============================================================
* Binded conn working
*/

/*
* Create new connection to client
*/
static void LQ_CALL LqHttpCoreAcceptHandler(LqConn* Connection, LqEvntFlag Flag) {
    if(Flag & LQEVNT_FLAG_ERR) {
        LqEvntSetClose(Connection);
        LQHTTPLOG_ERR("LqHttpCoreBindErrorProc() have error on binded socket\n");
        return;
    }

    LqConnInetAddress ClientAddr;
    auto Proto = (LqHttpProto*)Connection->Proto->UserData;
    socklen_t ClientAddrLen = sizeof(ClientAddr);
    int ClientFd;
    LqHttpConn* c;

    if((ClientFd = accept(Connection->Fd, &ClientAddr.Addr, &ClientAddrLen)) == -1) {
        LQHTTPLOG_ERR("LqHttpCoreAcceptHandler() client not accepted\n");
        return;
    }

    switch(ClientAddr.Addr.sa_family) {
        case AF_INET:
        {
            auto r = LqFastAlloc::New<HttpConnIp4>();
            if(r == nullptr) {
                closesocket(ClientFd);
                LQHTTPLOG_ERR("LqHttpCoreAcceptHandler() not alloc memory for connection\n");
                return;
            }
            r->Ip4Addr = ClientAddr.AddrInet;
            c = (LqHttpConn*)r;
        }
        break;
        case AF_INET6:
        {
            auto r = LqFastAlloc::New<HttpConnIp6>();
            if(r == nullptr) {
                closesocket(ClientFd);
                LQHTTPLOG_ERR("LqHttpCoreAcceptHandler() not alloc memory for connection\n");
                return;
            }
            r->Ip6Addr = ClientAddr.AddrInet6;
            c = (LqHttpConn*)r;
        }
        break;
        default:
            LQHTTPLOG_ERR("LqHttpCoreAcceptHandler() unicknown connection protocol\n");
            closesocket(ClientFd);
            return;
    }
    LqConnInit(c, ClientFd, &Proto->Base.Proto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_RD);

    c->Buf = nullptr;
    c->BufSize = 0;
    c->_Reserved = 0;

    c->TimeStartMillisec = c->TimeLastExchangeMillisec = LqTimeGetLocMillisec();
    c->Pth = nullptr;

    LqHttpActSwitchToRcv(c);

#ifdef HAVE_OPENSSL
    c->ssl = nullptr;
    LqAtmLkRd(Proto->Base.sslLocker);
    if(r->Base.ssl_ctx != nullptr) {
        if((c->ssl = SSL_new(Proto->Base.ssl_ctx)) == nullptr) {
            LqAtmLkUlkRd(Proto->Base.sslLocker);
            LqHttpCoreCloseHandler(&c->CommonConn);
            return;
        }
        LqAtmLkUlkRd(Proto->Base.sslLocker);
        if(SSL_set_fd(c->ssl, c->CommonConn.Fd) == 0) {
            SSL_free(c->ssl);
            c->ssl = nullptr;
            LqHttpCoreCloseHandler(&c->CommonConn);
            return;
        }
        auto SslAcptErr = SSL_accept(c->ssl);
        if(SslAcptErr < 0) {
            if(((SslAcptErr == SSL_ERROR_WANT_READ) || (SslAcptErr == SSL_ERROR_WANT_WRITE))) {
                c->ActionState = LQHTTPACT_STATE_RESPONSE_SSL_HANDSHAKE;
                LqEvntSetFlags(c, LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP);
            } else {
                LqHttpCoreCloseHandler(&c->CommonConn);
                return;
            }
        }
    } else {
        LqAtmLkUlkRd(Proto->Base.sslLocker);
    }
#endif
    LqAtmIntrlkInc(Proto->Base.CountConnections);
    LqWrkBossAddEvntAsync((LqEvntHdr*)c);
    Proto->ConnectHndls.Call(c);
}

static void LQ_CALL LqHttpCoreBindCloseHandler(LqConn* Connection) {
    auto Proto = (LqHttpProto*)Connection->Proto->UserData;
    LqHttpConn* c = (LqHttpConn*)Connection;
    if(Connection->Fd != -1)
        closesocket(Connection->Fd);
    Connection->Fd = -1;
    LQHTTPLOG_DEBUG("LqHttpCoreBindCloseHandler() disconnected\n");
    LqObPtrDereference<LqHttpProto, LqFastAlloc::Delete>(Proto);
}

LQ_EXTERN_C LqEvntFlag LQ_CALL LqHttpEvntGetFlagByAct(LqHttpConn* Conn) {
    switch(LqHttpActGetClassByConn(Conn)) {
        case LQHTTPACT_CLASS_QER:
            return LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_RD;
        case LQHTTPACT_CLASS_RSP:
            return LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_WR;
        case LQHTTPACT_CLASS_CLS:
            return LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP;
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqHttpEvntSetFlagByAct(LqHttpConn* Conn) {
    switch(LqHttpActGetClassByConn(Conn)) {
        case LQHTTPACT_CLASS_QER:
            return LqEvntSetFlags(Conn, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_RD, 0);
        case LQHTTPACT_CLASS_RSP:
            return LqEvntSetFlags(Conn, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_WR, 0);
        case LQHTTPACT_CLASS_CLS:
            return LqEvntSetClose(Conn);
    }
    return 0;
}

static bool LQ_CALL LqHttpCoreCmpIpAddress(LqConn* c, const void* Address) {
    if(LqHttpConnGetRmtAddr(c)->sa_family != ((sockaddr*)Address)->sa_family)
        return false;
    switch(LqHttpConnGetRmtAddr(c)->sa_family) {
        case AF_INET: return memcmp(&((sockaddr_in*)LqHttpConnGetRmtAddr(c))->sin_addr, &((sockaddr_in*)Address)->sin_addr, sizeof(((sockaddr_in*)Address)->sin_addr)) == 0;
        case AF_INET6: return memcmp(&((sockaddr_in6*)LqHttpConnGetRmtAddr(c))->sin6_addr, &((sockaddr_in6*)Address)->sin6_addr, sizeof(((sockaddr_in6*)Address)->sin6_addr)) == 0;
    }
    return false;
}

static void LQ_CALL LqHttpFreeProtoNotifyProc(LqProto* This) {
    LqHttpProto* CurReg = (LqHttpProto*)This;
    CurReg->Base.IsUnregister = true;
    LqHttpMdlFreeAll(&CurReg->Base);
    if(CurReg->Base.CountConnections == 0) {
#ifdef HAVE_OPENSSL
        if(CurProto->Base.ssl_ctx != nullptr)
            SSL_CTX_free(CurProto->Base.ssl_ctx);
#endif
        LqFastAlloc::Delete(CurReg);
    }
}

static bool LQ_CALL LqHttpCoreKickByTimeOutProc(LqConn* Connection, LqTimeMillisec CurrentTimeMillisec, LqTimeMillisec EstimatedLiveTime) {
    LqHttpConn* c = (LqHttpConn*)Connection;
    LqTimeMillisec TimeDiff = CurrentTimeMillisec - c->TimeLastExchangeMillisec;
    return TimeDiff > EstimatedLiveTime;
}

static char* LQ_CALL LqHttpCoreDbgInfoProc(LqConn* Connection) {
    char * Info = (char*)malloc(255);
    if(Info == nullptr)
        return nullptr;
    LqFbuf_snprintf(
        Info,
        254,
        "  HTTP connection. flags: %s%s%s",
        (Connection->Flag & LQEVNT_FLAG_RD) ? "RD " : "",
        (Connection->Flag & LQEVNT_FLAG_WR) ? "WR " : "",
        (Connection->Flag & LQEVNT_FLAG_HUP) ? "HUP " : ""
    );
    return Info;
}

/////////////////////////////////////////
//Start Rsp
/////////////////////////////////////////

static bool LqHttpCoreRspRangesRestruct(LqHttpConn* c) {
    auto Module = LqHttpMdlGetByConn(c);
    c->Response.CurRange++;
    if(c->Response.CurRange < c->Response.CountRanges) {
        char* HedersBuf = LqHttpRspHdrResize(c, 4096);
        LqFileSz CommonLenFile;
        LqFileStat s;
        s.ModifTime = 0;
        if(c->Response.Fd != -1) {
            LqFileGetStatByFd(c->Response.Fd, &s);
            CommonLenFile = s.Size;
        } else if(c->Response.CacheInterator != nullptr) {
            CommonLenFile = ((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator)->SizeFile;
        } else {
            CommonLenFile = 0;
        }
        char MimeBuf[1024];
        MimeBuf[0] = '\0';
        Module->GetMimeProc(c->Pth->RealPath, c, MimeBuf, sizeof(MimeBuf), (s.ModifTime == 0) ? nullptr : &s);

        int LenWritten = LqHttpRspPrintRangeBoundaryHeaders
        (
            HedersBuf,
            c->BufSize,
            c,
            CommonLenFile,
            c->Response.Ranges[c->Response.CurRange].Start,
            c->Response.Ranges[c->Response.CurRange].End,
            (MimeBuf[0] == '\0') ? nullptr : MimeBuf
        );
        LqHttpRspHdrResize(c, LenWritten);
        return true;
    } else if(c->Response.CountRanges > 1) {
        char* HedersBuf = LqHttpRspHdrResize(c, 4096);
        int EndHederLen = LqHttpRspPrintEndRangeBoundaryHeader(HedersBuf, c->BufSize);
        LqHttpRspHdrResize(c, EndHederLen);
        return true;
    }
    return false;
}

static void LqHttpCoreRspHdr(LqHttpConn* c) {
    if(c->Response.HeadersStart < c->Response.HeadersEnd) {
        auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
        c->Response.HeadersStart += SendedSize;
        if(c->Response.HeadersStart < c->Response.HeadersEnd) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            return;
        }
    }
    c->ActionResult = LQHTTPACT_RES_OK;
}

static void LqHttpCoreRspCache(LqHttpConn* c) {
    if(c->Response.HeadersStart < c->Response.HeadersEnd) {
lblOutHeader:
        auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
        if(c->Response.CurRange > 0)
            c->WrittenBodySize += SendedSize;
        c->Response.HeadersStart += SendedSize;
        if(c->Response.HeadersStart < c->Response.HeadersEnd) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            return;
        }
    }
    LqFileSz ResponseSize = c->Response.Ranges[c->Response.CurRange].End - c->Response.Ranges[c->Response.CurRange].Start;
    LqFileSz SendedSize = LqHttpConnSend
    (
        c,
        (const char*)((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator)->Buf +
        c->Response.Ranges[c->Response.CurRange].Start,
        ResponseSize
    );
    c->Response.Ranges[c->Response.CurRange].Start += SendedSize;
    if(c->Response.Ranges[c->Response.CurRange].End <= c->Response.Ranges[c->Response.CurRange].Start) {
        if(LqHttpCoreRspRangesRestruct(c))
            goto lblOutHeader;
    } else {
        c->ActionResult = LQHTTPACT_RES_PARTIALLY;
        return;
    }
    c->ActionResult = LQHTTPACT_RES_OK;
}

static void LqHttpCoreRspFd(LqHttpConn* c) {
    if(c->Response.HeadersStart < c->Response.HeadersEnd) {
lblOutHeader:
        auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
        if(c->Response.CurRange > 0)
            c->WrittenBodySize += SendedSize;
        c->Response.HeadersStart += SendedSize;
        if(c->Response.HeadersStart < c->Response.HeadersEnd) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            return;
        }
    }
    LqFileSz ResponseSize = c->Response.Ranges[c->Response.CurRange].End - c->Response.Ranges[c->Response.CurRange].Start;
    LqFileSz SendedSize = LqHttpConnSendFromFile(c, c->Response.Fd, c->Response.Ranges[c->Response.CurRange].Start, ResponseSize);
    c->Response.Ranges[c->Response.CurRange].Start += SendedSize;
    if(c->Response.Ranges[c->Response.CurRange].End <= c->Response.Ranges[c->Response.CurRange].Start) {
        if(LqHttpCoreRspRangesRestruct(c)) goto lblOutHeader;
    } else {
        c->ActionResult = LQHTTPACT_RES_PARTIALLY;
        return;
    }
    c->ActionResult = LQHTTPACT_RES_OK;
}

static void LqHttpCoreRspStream(LqHttpConn* c) {
    if(c->Response.HeadersStart < c->Response.HeadersEnd) {
        auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
        if(c->Response.CurRange > 0)
            c->WrittenBodySize += SendedSize;
        c->Response.HeadersStart += SendedSize;
        if(c->Response.HeadersStart < c->Response.HeadersEnd) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            return;
        }
    }
    LqHttpConnSendFromStream(c, &c->Response.Stream, c->Response.Stream.Len);
    if(c->Response.Stream.Len > 0) {
        c->ActionResult = LQHTTPACT_RES_PARTIALLY;
        return;
    }
    c->ActionResult = LQHTTPACT_RES_OK;
}

/////////////////////////////////////////
//End Rsp
/////////////////////////////////////////


////////////////////////////////////////
// Multipart data
////////////////////////////////////////

static void LqHttpCoreRcvMultipartSkipToHdr(LqHttpConn* c) {
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    const static uint16_t BeginChain = *(uint16_t*)"--";
    const static uint16_t EndChain = *(uint16_t*)"\r\n";
    const static uint16_t EndChain2 = *(uint16_t*)"--";


    auto q = &c->Query;
    auto ReadedBodySize = c->ReadedBodySize;
    for(; q->MultipartHeaders != nullptr; q = &q->MultipartHeaders->Query)
        ReadedBodySize = q->MultipartHeaders->ReadedBodySize;

    LqFileSz LeftContentLen = q->ContentLen - ReadedBodySize;

    size_t BoundaryChainSize = (sizeof(BeginChain) + sizeof(EndChain)) + q->ContentBoundaryLen;
    LqFileSz InTactReaded = 0;
    LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;

    while(true) {
        LqFileSz CurSizeRead = LeftContentLen - InTactReaded;
        if(CurSizeRead <= BoundaryChainSize) {
            if(CurSizeRead > 0) {
                auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
                if(Skipped < 0) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblOut;
                }
                InTactReaded += Skipped;
                if(Skipped < CurSizeRead) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblOut;
                }
            }
            c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
            goto lblOut;
        }
        if(CurSizeRead > (sizeof(Buf) - sizeof(BeginChain)))
            CurSizeRead = (sizeof(Buf) - sizeof(BeginChain));
        auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
        if(PeekRecived == -1) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblOut;
        }
        *(int16_t*)(Buf + PeekRecived) = BeginChain;
        size_t Checked = 0;
        for(register char* i = Buf; ; i++) {
            if(*(uint16_t*)i == BeginChain) {
                if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived)
                    break;
                if(memcmp(q->ContentBoundary, i + sizeof(BeginChain), q->ContentBoundaryLen) == 0) {
                    if(EndChain == *(uint16_t*)(i + sizeof(BeginChain) + q->ContentBoundaryLen)) {
                        InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked + BoundaryChainSize, 0);
                        c->ActionResult = LQHTTPACT_RES_OK;
                        goto lblOut;
                    } else if(EndChain2 == *(uint16_t*)(i + sizeof(BeginChain) + q->ContentBoundaryLen)) {
                        InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked + BoundaryChainSize, 0);
                        c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
                        goto lblOut;
                    }
                }
            }
        }
        InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
        if(InTactReaded > MaxReciveSizeInTact) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblOut;
        }
    }

lblOut:
    c->ReadedBodySize += InTactReaded;
    for(auto mh = c->Query.MultipartHeaders; mh != nullptr; mh = mh->Query.MultipartHeaders)
        mh->ReadedBodySize += InTactReaded;
}

/*
*
* Returned errors: SNDWD_STAT_OK, SNDWD_STAT_PARTIALLY, SNDWD_STAT_FULL_MAX, SNDWD_STAT_MULTIPART_END, SNDWD_STAT_MULTIPART_INVALID_HEADER
*/

static void LqHttpRcvMultipartReadHdr(LqHttpConn* c) {
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    static const uint32_t EndChain = *(uint32_t*)"\r\n\r\n";
    static const size_t BoundaryChainSize = sizeof(EndChain);

    LqHttpMultipartHeaders* CurMultipart = (LqHttpMultipartHeaders*)c->_Reserved;
    LqFileSz HdrRecived = 0;
    if(CurMultipart != nullptr)
        HdrRecived = CurMultipart->BufSize;

    auto q = &c->Query;
    auto ReadedBodySize = c->ReadedBodySize;
    for(; q->MultipartHeaders != nullptr; q = &q->MultipartHeaders->Query)
        ReadedBodySize = q->MultipartHeaders->ReadedBodySize;
    LqFileSz LeftContentLen = q->ContentLen - ReadedBodySize;

    LqFileSz InTactReaded = 0;
    LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;
    LqFileSz MaxHeadersSize = LqHttpProtoGetByConn(c)->MaxMultipartHeadersSize;

    while(true) {
        LqFileSz CurSizeRead = LeftContentLen - InTactReaded;
        if(CurSizeRead <= BoundaryChainSize) {
            if(CurSizeRead > 0) {
                auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
                if(Skipped < 0) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblPartial;
                }
                InTactReaded += Skipped;
                if(Skipped < CurSizeRead) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblPartial;
                }
            }
            c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
            goto lblOutErr;
        }
        if(CurSizeRead > (sizeof(Buf) - sizeof(EndChain)))
            CurSizeRead = (sizeof(Buf) - sizeof(EndChain));
        if((HdrRecived + InTactReaded) > MaxHeadersSize) {
            CurSizeRead = MaxHeadersSize - (InTactReaded + HdrRecived);
            if(CurSizeRead <= 0) {
                c->ActionResult = LQHTTPACT_RES_HEADERS_READ_MAX;
                goto lblOutErr;
            }
        }
        auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
        if(PeekRecived == -1) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblPartial;
        }

        *(uint32_t*)(Buf + PeekRecived) = EndChain;
        size_t Checked = 0;
        for(register char* i = Buf; ; i++) {
            if(EndChain == *(uint32_t*)i) {
                if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived)
                    break;
                if(!LqHttpMultipartAddHeaders(&CurMultipart, Buf, Checked + BoundaryChainSize)) {
                    c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
                    goto lblOutErr;
                }
                InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked + BoundaryChainSize, 0);
                char* Start = CurMultipart->Buf;
                for(;;) {
                    char *StartKey, *EndKey, *StartVal, *EndVal, *EndHeader;
                    LqHttpPrsHdrStatEnm r = LqHttpPrsHeader(Start, &StartKey, &EndKey, &StartVal, &EndVal, &EndHeader);
                    switch(r) {
                        case LQPRS_HDR_SUCCESS:
                        {
                            char t = *EndKey;
                            *EndKey = '\0';
                            LqHttpParseHeader(c, &CurMultipart->Query, StartKey, StartVal, EndVal, LqDfltRef());
                            *EndKey = t;
                            Start = EndHeader;
                        }
                        continue;
                        case LQPRS_HDR_ERR:
                            c->ActionResult = LQHTTPACT_RES_INVALID_HEADER;
                            goto lblOutErr;
                    }
                    if(r == LQPRS_HDR_END) break;
                }
                c->ActionResult = LQHTTPACT_RES_OK;
                q->MultipartHeaders = CurMultipart;
                CurMultipart->Buf[CurMultipart->BufSize] = '\0';
                c->_Reserved = 0;
                goto lblOk;
            }
        }
        if(!LqHttpMultipartAddHeaders(&CurMultipart, Buf, Checked)) {
            c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
            goto lblOutErr;
        }
        InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
        if(InTactReaded > MaxReciveSizeInTact) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblPartial;
        }
    }
lblOutErr:
    if(CurMultipart != nullptr) {
        free(CurMultipart);
        CurMultipart = nullptr;
    }
lblPartial:
    c->_Reserved = (uintptr_t)CurMultipart;
lblOk:
    c->ReadedBodySize += InTactReaded;
    for(auto mh = c->Query.MultipartHeaders; (mh != nullptr) && (q->MultipartHeaders != CurMultipart); mh = mh->Query.MultipartHeaders)
        mh->ReadedBodySize += InTactReaded;
}

static void LqHttpRcvMultipartReadInFile(LqHttpConn* c) {
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    static const uint32_t BeginChain = *(uint32_t*)"\r\n--";

    auto mph = c->Query.MultipartHeaders;
    if(mph == nullptr) {
        c->ActionResult = LQHTTPACT_RES_INVALID_HEADER;
        return;
    }
    auto bq = &c->Query;
    for(; ; mph = mph->Query.MultipartHeaders) {
        if(mph->Query.ContentBoundary)
            bq = &mph->Query;
        if(mph->Query.MultipartHeaders == nullptr)
            break;
    }
    int OutFd = c->Query.OutFd;
    size_t BoundaryChainSize = sizeof(BeginChain) + bq->ContentBoundaryLen;
    LqFileSz LeftContentLen = mph->Query.ContentLen - mph->ReadedBodySize;
    LqFileSz InTactReaded = 0;
    LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;

    while(true) {
        size_t CurSizeRead = LeftContentLen - InTactReaded;
        if(CurSizeRead <= BoundaryChainSize) {
            if(CurSizeRead > 0) {
                auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
                if(Skipped < 0) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblOut;
                }
                InTactReaded += Skipped;
                if(Skipped < CurSizeRead) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblOut;
                }
            }
            c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
            goto lblOut;
        }
        if(CurSizeRead > (sizeof(Buf) - sizeof(BeginChain)))
            CurSizeRead = (sizeof(Buf) - sizeof(BeginChain));
        if((CurSizeRead + InTactReaded) > LeftContentLen) {
            if((CurSizeRead = LeftContentLen - InTactReaded) <= 0) {
                c->ActionResult = LQHTTPACT_RES_OK;
                goto lblOut;
            }
        }
        auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
        if(PeekRecived == -1) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblOut;
        }
        *(uint32_t*)(Buf + PeekRecived) = BeginChain;

        size_t Checked = 0;
        for(register char* i = Buf; ; i++) {
            if(BeginChain == *(uint32_t*)i) {
                if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived)
                    break;
                if(memcmp(bq->ContentBoundary, i + sizeof(BeginChain), bq->ContentBoundaryLen) == 0) {
                    auto Written = LqFileWrite(OutFd, Buf, Checked);
                    if(Written < 0) {
                        c->ActionResult = LQHTTPACT_RES_FILE_WRITE_ERR;
                        goto lblOut;
                    }
                    InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
                    c->ActionResult = LQHTTPACT_RES_OK;
                    goto lblOut;
                }
            }
        }
        auto Written = LqFileWrite(OutFd, Buf, Checked);
        if(Written < Checked) {
            c->ActionResult = LQHTTPACT_RES_FILE_WRITE_ERR;
            goto lblOut;
        }
        InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
        if(InTactReaded > MaxReciveSizeInTact) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblOut;
        }
    }

lblOut:
    c->ReadedBodySize += InTactReaded;
    for(auto mh = c->Query.MultipartHeaders; mh != nullptr; mh = mh->Query.MultipartHeaders)
        mh->ReadedBodySize += InTactReaded;
}

static void LqHttpRcvMultipartReadInStream(LqHttpConn* c) {
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    static const uint32_t BeginChain = *(uint32_t*)"\r\n--";

    auto mph = c->Query.MultipartHeaders;
    if(mph == nullptr) {
        c->ActionResult = LQHTTPACT_RES_INVALID_HEADER;
        return;
    }
    auto bq = &c->Query;
    for(; ; mph = mph->Query.MultipartHeaders) {
        if(mph->Query.ContentBoundary)
            bq = &mph->Query;
        if(mph->Query.MultipartHeaders == nullptr)
            break;
    }
    size_t BoundaryChainSize = sizeof(BeginChain) + bq->ContentBoundaryLen;
    LqFileSz LeftContentLen = mph->Query.ContentLen - mph->ReadedBodySize;
    LqFileSz InTactReaded = 0;
    LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;
    while(true) {
        size_t CurSizeRead = LeftContentLen - InTactReaded;
        if(CurSizeRead <= BoundaryChainSize) {
            if(CurSizeRead > 0) {
                auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
                if(Skipped < 0) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblOut;
                }
                InTactReaded += Skipped;
                if(Skipped < CurSizeRead) {
                    c->ActionResult = LQHTTPACT_RES_PARTIALLY;
                    goto lblOut;
                }
            }
            c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
            goto lblOut;
        }
        if(CurSizeRead > (sizeof(Buf) - sizeof(BeginChain)))
            CurSizeRead = (sizeof(Buf) - sizeof(BeginChain));
        if((CurSizeRead + InTactReaded) > LeftContentLen) {
            if((CurSizeRead = LeftContentLen - InTactReaded) <= 0) {
                c->ActionResult = LQHTTPACT_RES_OK;
                goto lblOut;
            }
        }
        auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
        if(PeekRecived == -1) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblOut;
        }
        *(uint32_t*)(Buf + PeekRecived) = BeginChain; //Set terminator for loop
        size_t Checked = 0;
        for(register char* i = Buf; ; i++) {
            if(BeginChain == *(uint32_t*)i) {
                if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived) //If have terninator
                    break;
                if(memcmp(bq->ContentBoundary, i + sizeof(BeginChain), bq->ContentBoundaryLen) == 0) {
                    auto Written = LqSbufWrite(&c->Query.Stream, Buf, Checked);
                    if(Written < Checked) {
                        c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
                        goto lblOut;
                    }
                    InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
                    c->ActionResult = LQHTTPACT_RES_OK;
                    goto lblOut;
                }
            }
        }
        auto Written = LqSbufWrite(&c->Query.Stream, Buf, Checked);
        if(Written < Checked) {
            c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
            goto lblOut;
        }
        InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
        if(InTactReaded > MaxReciveSizeInTact) {
            c->ActionResult = LQHTTPACT_RES_PARTIALLY;
            goto lblOut;
        }
    }

lblOut:
    c->ReadedBodySize += InTactReaded;
    for(auto mh = c->Query.MultipartHeaders; mh != nullptr; mh = mh->Query.MultipartHeaders)
        mh->ReadedBodySize += InTactReaded;
}

static bool LqHttpMultipartAddHeaders(LqHttpMultipartHeaders** CurMultipart, const char* Buf, size_t BufLen) {
    LqHttpMultipartHeaders* New;
    if(*CurMultipart == nullptr) {
        New = (LqHttpMultipartHeaders*)malloc(sizeof(LqHttpMultipartHeaders) + BufLen + 1);
        if(New == nullptr)
            return false;
        memset(New, 0, sizeof(LqHttpMultipartHeaders));
        New->Query.OutFd = -1;
        New->Query.ContentLen = LQ_MAX_CONTENT_LEN;
    } else {
        auto Old = CurMultipart[0];
        New = (LqHttpMultipartHeaders*)realloc(Old, sizeof(LqHttpMultipartHeaders) + Old->BufSize + BufLen + 1);
        if(New == nullptr)
            return false;
    }
    *CurMultipart = New;
    memcpy(New->Buf + New->BufSize, Buf, BufLen);
    New->BufSize += BufLen;
    return true;
}

static void LqHttpQurReadHeaders(LqHttpConn* c) {
    int CountReaded;
    size_t NewAllocSize, ReadSize, SizeAllReaded, CountLines, UrlLength, LengthHeaders;
    char *EndQuery, *EndStartLine, *StartMethod = "", *EndMethod = StartMethod,
        *StartUri = StartMethod, *EndUri = StartMethod,
        *StartVer = StartMethod, *EndVer = StartMethod,
        *SchemeStart, *SchemeEnd, *UserInfoStart, *UserInfoEnd,
        *HostStart, *HostEnd, *PortStart, *PortEnd,
        *DirStart, *DirEnd, *QueryStart, *QueryEnd,
        *FragmentStart, *FragmentEnd, *End, TypeHost, *NewPlaceForUrl;

    LqHttpProto* CurProto = LqHttpGetReg(c);
    auto q = &c->Query;


lblContinueRead:
    NewAllocSize = q->PartLen + 2045;
    if(c->BufSize < NewAllocSize) {
        if(!LqHttpConnBufferRealloc(c, (NewAllocSize > CurProto->Base.MaxHeadersSize) ? CurProto->Base.MaxHeadersSize : NewAllocSize)) {
            //Error allocate memory
            c->Flags = LQHTTPCONN_FLAG_CLOSE;
            LqHttpRspError(c, 500);
            return;
        }
    }

    ReadSize = c->BufSize - q->PartLen - 2;
    if((CountReaded = LqHttpConnRecive_Native(c, c->Buf + q->PartLen, ReadSize, MSG_PEEK)) <= 0) {
        if((CountReaded < 0) && !LQERR_IS_WOULD_BLOCK) {
            //Error reading
            c->Flags = LQHTTPCONN_FLAG_CLOSE;
            LqHttpRspError(c, 500);
            return;
        }
        //Is empty buffer
        c->ActionResult = LQHTTPACT_RES_PARTIALLY;
        return;
    }

    SizeAllReaded = q->PartLen + CountReaded;
    c->Buf[SizeAllReaded] = '\0';
    if((EndQuery = LqHttpPrsGetEndHeaders(c->Buf + q->PartLen, &CountLines)) == nullptr) {
        if((SizeAllReaded + 4) >= CurProto->Base.MaxHeadersSize) {
            //Error request to large
            q->PartLen = 0;
            c->Flags = LQHTTPCONN_FLAG_CLOSE;
            LqHttpRspError(c, 413);
            return;
        } else {
            LqHttpConnRecive_Native(c, c->Buf + q->PartLen, CountReaded, 0);
            q->PartLen = SizeAllReaded;
            //If data spawn in buffer, while we reading.
            if(LqHttpConnCountPendingData(c) > 0)
                goto lblContinueRead;
            //Is read part
            return;
        }
    }

    LqHttpConnRecive_Native(c, c->Buf + q->PartLen, EndQuery - (c->Buf + q->PartLen), 0);
    *EndQuery = '\0';
    q->PartLen = 0;
    LengthHeaders = EndQuery - c->Buf;

    /*
    Read start line.
    */
lblGetUrlAgain:
    switch(
        LqHttpPrsStartLine(
        c->Buf,
        &StartMethod, &EndMethod,
        &StartUri, &EndUri,
        &StartVer, &EndVer,
        &EndStartLine
        )) {
        case LQPRS_START_LINE_ERR:
            //Err invalid start line
            c->Flags = LQHTTPCONN_FLAG_CLOSE;
            LqHttpRspError(c, 400);
            return;
    }

    UrlLength = LengthHeaders + (EndUri - StartUri) + 10;

    if(UrlLength > c->BufSize) {
        if(!LqHttpConnBufferRealloc(c, UrlLength)) {
            free(c->Buf);
            c->Buf = nullptr;
            c->Flags = LQHTTPCONN_FLAG_CLOSE;
            LqHttpRspError(c, 500);
            return;
        }
        goto lblGetUrlAgain;
    }

    memcpy(NewPlaceForUrl = (c->Buf + (LengthHeaders + 2)), StartUri, EndUri - StartUri);
    NewPlaceForUrl[EndUri - StartUri] = '\0';
    struct LocalFunc {
        static void EnumURIArg(void* QueryData, char* StartKey, char* EndKey, char* StartVal, char* EndVal) {}
    };

    if(
        LqHttpPrsUrl
        (
        NewPlaceForUrl,
        &SchemeStart, &SchemeEnd,
        &UserInfoStart, &UserInfoEnd,
        &HostStart, &HostEnd,
        &PortStart, &PortEnd,
        &DirStart, &DirEnd,
        &QueryStart, &QueryEnd,
        &FragmentStart, &FragmentEnd,
        &End, &TypeHost,
        LocalFunc::EnumURIArg,
        q
        ) != LQPRS_URL_SUCCESS
        ) {
        if(*StartUri == '*') {
            DirEnd = (DirStart = StartUri) + 1;
        } else {
            //Err invalid uri in start line
            LqHttpRspError(c, 400);
            return;
        }
    } else {
        char* NewEnd;

        if(UserInfoStart != nullptr) {
            q->UserInfo = LqHttpPrsEscapeDecode(UserInfoStart, UserInfoEnd, &NewEnd);
            q->UserInfoLen = NewEnd - UserInfoStart;
        }
        if(HostStart != nullptr) {
            q->Host = LqHttpPrsEscapeDecode(HostStart, HostEnd, &NewEnd);
            q->HostLen = NewEnd - HostStart;
        }
        if(DirStart != nullptr) {
            q->Path = LqHttpPrsEscapeDecode(DirStart, DirEnd, &NewEnd);
            q->PathLen = NewEnd - DirStart;
        }
        if(QueryStart != nullptr) {
            q->Arg = LqHttpPrsEscapeDecode(QueryStart, QueryEnd, &NewEnd);
            q->ArgLen = NewEnd - q->Arg;
        }
        if(FragmentStart != nullptr) {
            q->Fragment = LqHttpPrsEscapeDecode(FragmentStart, FragmentEnd, &NewEnd);
            q->FragmentLen = NewEnd - FragmentStart;
        }
    }
    if(StartVer < EndVer) {
        q->ProtoVer = StartVer;
        q->ProtoVerLen = EndVer - StartVer;
    }
    q->Method = StartMethod;
    q->MethodLen = EndMethod - StartMethod;


    /*
    Read all headers
    */

    q->HeadersEnd = SizeAllReaded + 1;
    size_t RecognizedHeadersCount = 0;
    for(;;) {
        char *StartKey, *EndKey, *StartVal, *EndVal, *EndHeader;
        LqHttpPrsHdrStatEnm r = LqHttpPrsHeader(EndStartLine, &StartKey, &EndKey, &StartVal, &EndVal, &EndHeader);
        switch(r) {
            case LQPRS_HDR_SUCCESS:
            {
                char t = *EndKey;
                *EndKey = '\0';
                LqHttpParseHeader(c, &c->Query, StartKey, StartVal, EndVal, &RecognizedHeadersCount);
                *EndKey = t;
                EndStartLine = EndHeader;
            }
            continue;
            case LQPRS_HDR_ERR:
            {
                //Err invalid header in query
                LqHttpRspError(c, 400);
                return;
            }
        }
        if(r == LQPRS_HDR_END) break;
    }
    LqHttpPthRecognize(c);
    c->ActionState = LQHTTPACT_STATE_RESPONSE_HANDLE_PROCESS;
    c->ActionResult = LQHTTPACT_RES_OK;

    CurProto->StartQueryHndls.Call(c);

    if(auto MethodHandler = LqHttpMdlGetByConn(c)->GetActEvntHandlerProc(c))
        c->EventAct = MethodHandler;
    else
        LqHttpRspError(c, 405);

}

static void LqHttpParseHeader(LqHttpConn* c, LqHttpQuery* q, char* StartKey, char* StartVal, char* EndVal, size_t* CountHeders) {
    LQSTR_SWITCH_I(StartKey) {
        LQSTR_CASE_I("connection");
        {
            if(LqStrUtf8CmpCaseLen(StartVal, "close", sizeof("close") - 1))
                c->Flags |= LQHTTPCONN_FLAG_CLOSE;
        }
        break;
        LQSTR_CASE_I("content-length");
        {
            ullong InputLen = 0ULL;
            LqFbuf_snscanf(StartVal, EndVal - StartVal, "%llu", &InputLen);
            q->ContentLen = InputLen;
            //!!!!!!!!!!
        }
        break;
        LQSTR_CASE_I("host");
        {
            q->Host = StartVal;
            q->HostLen = EndVal - StartVal;
        }
        break;
        LQSTR_CASE_I("content-type");
        {
            if(LqStrUtf8CmpCaseLen(StartVal, "multipart/form-data", sizeof("multipart/form-data") - 1)) {
                for(char* i = StartVal + sizeof("multipart/form-data"), *m = EndVal - (sizeof("boundary=") - 1); i < m; i++) {
                    if(LqStrUtf8CmpCaseLen(i, "boundary=", sizeof("boundary=") - 1)) {
                        i += (sizeof("boundary=") - 1);
                        for(; *i == ' '; i++);
                        char* v = i;
                        for(; (*i != ';') && (*i != ' ') && (*i != '\0') && !((*i == '\r') && (i[1] == '\n')); i++);
                        if((i - v) <= 0)
                            break;
                        q->ContentBoundaryLen = i - (q->ContentBoundary = v);
                        q->ContentBoundary = v;
                        break;
                    }
                }
            }
        }
        break;
    }
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterQuery(LqHttpProtoBase* Reg, LqHttpNotifyFn QueryFunc) {
    return ((LqHttpProto*)Reg)->StartQueryHndls.Add(QueryFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterQuery(LqHttpProtoBase* Reg, LqHttpNotifyFn QueryFunc) {
    return ((LqHttpProto*)Reg)->StartQueryHndls.Rm(QueryFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterResponse(LqHttpProtoBase* Reg, LqHttpNotifyFn ResponseFunc) {
    return ((LqHttpProto*)Reg)->EndResponseHndls.Add(ResponseFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterResponse(LqHttpProtoBase* Reg, LqHttpNotifyFn ResponseFunc) {
    return ((LqHttpProto*)Reg)->EndResponseHndls.Rm(ResponseFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterConnect(LqHttpProtoBase* Reg, LqHttpNotifyFn ConnectFunc) {
    return ((LqHttpProto*)Reg)->ConnectHndls.Add(ConnectFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterConnect(LqHttpProtoBase* Reg, LqHttpNotifyFn ConnectFunc) {
    return ((LqHttpProto*)Reg)->ConnectHndls.Rm(ConnectFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsRegisterDisconnect(LqHttpProtoBase* Reg, LqHttpNotifyFn DisconnectFunc) {
    return ((LqHttpProto*)Reg)->DisconnectHndls.Add(DisconnectFunc);
}

LQ_EXTERN_C bool LQ_CALL LqHttpHndlsUnregisterDisconnect(LqHttpProtoBase* Reg, LqHttpNotifyFn DisconnectFunc) {
    return ((LqHttpProto*)Reg)->DisconnectHndls.Rm(DisconnectFunc);
}