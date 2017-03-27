/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttp... - Main handlers of HTTP protocol.
*/

#ifndef __LANQ_HTTP_H_HAS_INCLUDED__
#define __LANQ_HTTP_H_HAS_INCLUDED__


#include <time.h>
#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#endif

#include "LqOs.h"
#include "LqDef.h"
#include "Lanq.h"
#include "LqFile.h"
#include "LqSbuf.h"
#include "LqConn.h"
#include "LqSockBuf.h"
#include "LqFche.h"

LQ_EXTERN_C_BEGIN


typedef LqSockAcceptor LqHttp;
typedef LqSockBuf      LqHttpConn;


struct LqHttpQuery;
struct LqHttpPth;
struct LqHttpAtz;
struct LqHttpMdl;
struct LqHttpExtensionMime;
struct LqHttpUserData;

typedef struct LqHttpQuery LqHttpQuery;
typedef struct LqHttpPth LqHttpPth;
typedef struct LqHttpAtz LqHttpAtz;
typedef struct LqHttpMdl LqHttpMdl;
typedef struct LqHttpExtensionMime LqHttpExtensionMime;
typedef struct LqHttpUserData LqHttpUserData;

typedef void(LQ_CALL *LqHttpEvntHandlerFn)(LqHttpConn* HttpConn);
typedef void(LQ_CALL *LqHttpNotifyFn)(LqHttpConn* HttpConn);


typedef enum LqHttpPthFlagEnm {
    LQHTTPPTH_FLAG_SUBDIR = 8,
    LQHTTPPTH_FLAG_CHILD = 16
} LqHttpPthFlagEnm;

typedef enum LqHttpPthTypeEnm {
    LQHTTPPTH_TYPE_DIR = 0,
    LQHTTPPTH_TYPE_FILE = 1,
    LQHTTPPTH_TYPE_EXEC_DIR = 2,
    LQHTTPPTH_TYPE_EXEC_FILE = 3,
    LQHTTPPTH_TYPE_FILE_REDIRECTION = 4,
    LQHTTPPTH_TYPE_DIR_REDIRECTION = 5
} LqHttpPthTypeEnm;

typedef enum LqHttpAtzTypeEnm {
    LQHTTPATZ_TYPE_NONE,
    LQHTTPATZ_TYPE_BASIC,
    LQHTTPATZ_TYPE_DIGEST
} LqHttpAtzTypeEnm;

typedef enum LqHttpAuthorizPermissionEnm {
    LQHTTPATZ_PERM_READ = 1,
    LQHTTPATZ_PERM_WRITE = 2,
    LQHTTPATZ_PERM_CHECK = 4,
    LQHTTPATZ_PERM_CREATE = 8,
    LQHTTPATZ_PERM_DELETE = 16,
    LQHTTPATZ_PERM_MODIFY = 32,
    LQHTTPATZ_PERM_CREATE_SUBDIR = 64
} LqHttpAuthorizPermissionEnm;

typedef uint16_t LqHttpConnDataFlag;
#define LQHTTPPTH_TYPE_SEP       0x07
#define LQ_MAX_CONTENT_LEN       0xffffffffff

#define LQHTTPCONN_FLAG_CLOSE    ((LqHttpConnDataFlag)1)
#define LQHTTPCONN_FLAG_NO_BODY  ((LqHttpConnDataFlag)4)
#define LQHTTPCONN_FLAG_BIN_RSP  ((LqHttpConnDataFlag)8)
#define LQHTTPCONN_FLAG_BOUNDARY ((LqHttpConnDataFlag)16)
#define LQHTTPCONN_FLAG_LOCATION ((LqHttpConnDataFlag)32)
#define LQHTTPCONN_FLAG_LONG_POLL_OP ((LqHttpConnDataFlag)64)
#define LQHTTPCONN_FLAG_MUST_DELETE ((LqHttpConnDataFlag)128)
#define LQHTTPCONN_FLAG_IN_LONG_POLL_CLOSE_HANDLER ((LqHttpConnDataFlag)256)
#define LQHTTPCONN_FLAG_AFTER_USER_HANDLER_CALLED ((LqHttpConnDataFlag)512)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

#define LQHTTPRSP_MAX_RANGES 4

struct LqHttpUserData {
    void* Name;
    void* Data;
};


struct LqHttpPth {
    size_t                      CountPointers;
    char*                       WebPath;
    uint32_t                    WebPathHash;
    uintptr_t                   ModuleData;
    union {
        struct {
            char*               Location;
            short               StatusCode;
        };

        char*                   RealPath;
        LqHttpEvntHandlerFn     ExecQueryProc;
    };
    LqHttpAtz*                  Atz;
    uint16_t                    AtzPtrLk;

    union {
        LqHttpPth*              Parent;
        LqHttpMdl*              ParentModule;
    };
    uint8_t                     Type;           /* NET_PATH_TYPE_ ... */
    uint8_t                     Permissions;    /* AUTHORIZATION_MASK_ ... */
};

typedef struct LqHttpRcvHdr {
    char*        Name;
    char*        Val;
} LqHttpRcvHdr;

typedef struct LqHttpRcvHdrs {
    char*       Scheme;
    char*       Method;
    char*       Host;
    char*       UserInfo;
    char*       Port;
    char*       Path;
    char*       Fragment;
    char*       Args;
    char*       ContentBoundary;
    uint8_t     MajorVer;
    uint8_t     MinorVer;
    LqHttpRcvHdr* Hdrs;
    size_t      CountHdrs;
} LqHttpRcvHdrs;

typedef struct LqHttpMultipartHdrs {
    LqHttpRcvHdr* Hdrs;
    size_t      CountHdrs;
} LqHttpMultipartHdrs;

typedef struct LqHttpConnData {
    int              LenRspConveyor;
    LqHttpRcvHdrs*   RcvHdr;
    unsigned short   UserDataCount;
    LqHttpUserData*  UserData;
    LqHttpConnDataFlag Flags;
    LqHttpPth*       Pth;
    char*            RspFileName;
    char*            BoundaryOrContentRangeOrLocation;
    char*            AdditionalHdrs;
    size_t           AdditionalHdrsLen;
    uint16_t         RspStatus;
    LqFileSz         ContentLength;
    void   (LQ_CALL *LongPollCloseHandler)(LqHttpConn*);
} LqHttpConnData;


#pragma pack(pop)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

typedef struct LqHttpConnRcvResult {
    LqHttpConn*     HttpConn;
    LqFbuf*         TargetFbuf;
    LqHttpMultipartHdrs* MultipartHdrs;
    LqFileSz        Written;
    void*           UserData;
    bool            IsMultipartEnd;
    bool            IsFoundedSeq;
} LqHttpConnRcvResult;

/*
* Module defenition for http protocol
*/
struct LqHttpMdl {
    LqHttp*        HttpAcceptor;

    char*          Name;

    size_t         CountPointers;
    uintptr_t      Handle;

    void (LQ_CALL* BeforeFreeNotifyProc)(LqHttpMdl* This);
    uintptr_t(LQ_CALL* FreeNotifyProc)(LqHttpMdl* This);  //ret != 0, then unload module. If ret == 0, then the module remains in the memory

    /* Create and delete path proc */
    void (LQ_CALL* DeletePathProc)(LqHttpPth* lqaio Pth);
    void (LQ_CALL* CreatePathProc)(LqHttpPth* lqaio Pth);

    bool (LQ_CALL* RegisterPathInDomenProc)(LqHttpPth* lqaio Pth, const char* lqain lqautf8 DomenName);
    void (LQ_CALL* UnregisterPathFromDomenProc)(LqHttpPth* lqaio Pth, const char* lqain lqautf8 DomenName);

    void (LQ_CALL* GetMimeProc) (
        const char* lqain lqautf8 Path,
        LqHttpConn* lqain lqaout Connection,

        char* lqaout lqaopt lqautf8 MimeDestBuf,
        size_t MimeDestBufLen
    );

    void (LQ_CALL* GetCacheInfoProc)(
        const char* lqain Path,
        LqHttpConn* lqain lqaopt HttpConn,

        char* lqaout lqaopt lqautf8 CacheControlDestBuf, /* If after call CacheControlDestBuf == "", then Cache-Control no include in response headers. */
        size_t CacheControlDestBufSize,

        char* lqaout lqaopt lqautf8 EtagDestBuf, /*If after call EtagDestBuf == "", then Etag no include in response headers. */
        size_t EtagDestBufSize,

        LqTimeSec* lqaout lqaopt LastModif, /*Local time. If after call LastModif == -1, then then no response Last-Modified.*/

        LqTimeSec* lqaout lqaopt Expires
    );

    void (LQ_CALL* SockError)(LqHttpConn* HttpConn);
    /* Use for response error to client*/
    void (LQ_CALL* RspErrorProc)(LqHttpConn* lqain HttpConn, int Code);

    /*If NameBuf == "", then ignore name server*/
    void (LQ_CALL* ServerNameProc)(LqHttpConn* lqain HttpConn, char* lqaout lqautf8 NameBuf, size_t NameBufSize);

    /*If NameBuf == "", then ignore allow*/
    void (LQ_CALL* AllowProc)(LqHttpConn* lqain HttpConn, char* lqaout lqautf8 MethodBuf, size_t MethodBufSize);

    /*Use for digest auth*/
    /*If NameBuf == "", then ignore name server*/
    void (LQ_CALL* NonceProc)(LqHttpConn* lqain HttpConn, char* lqaout lqautf8 MethodBuf, size_t MethodBufSize);

    /*Use for send redirect */
    void (LQ_CALL* ResponseRedirectionProc)(LqHttpConn* lqain HttpConn);

    /* Must return handler for method or NULL */
    void (LQ_CALL* MethodHandlerProc)(LqHttpConn* lqain HttpConn);

    /* Use for send command to module. If @Command[0] == '?', then the command came from the console*/
    void (LQ_CALL* ReciveCommandProc)(LqHttpMdl* This, const char* Command, void* Data);

    /* Internally used in C++ part */
    char                        _Paths[sizeof(void*) * 3];

    bool                        IsFree;
    uintptr_t                   UserData;
};

/*
*/
struct LqHttpAtz {
    size_t                      CountPointers;
    uintptr_t                   Locker;
    char*                       Realm;
    uint8_t                     AuthType;                       /*AUTHORIZATION_... */
    size_t                      CountAuthoriz;
    union {
        struct {
            uint8_t             AccessMask;
            char*               LoginPassword;
        } *Basic;

        struct {
            uint8_t             AccessMask;
            char*               UserName;
            char                DigestLoginPassword[16 * 2 + 1];
        } *Digest;
    };
};

#pragma pack(pop)

/*
*----------------------------
* Set and get HTTP proto parametrs
*/
LQ_IMPORTEXPORT LqHttp* LQ_CALL LqHttpCreate(const char* Host, const char* Port, int RouteProto, void* SslCtx, bool IsSetCache, bool IsSetZmbClr);

LQ_IMPORTEXPORT int LQ_CALL LqHttpDelete(LqHttp* Http, bool* lqaout lqaopt DeleteFlag);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpGoWork(LqHttp* Http, void* WrkBoss);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpInterruptWork(LqHttp* Http);

LQ_IMPORTEXPORT void LQ_CALL LqHttpCloseAllConn(LqHttp* Http);

LQ_IMPORTEXPORT size_t LQ_CALL LqHttpCountConn(LqHttp* Http);

LQ_IMPORTEXPORT size_t LQ_CALL LqHttpSetNameServer(LqHttp* Http, const char* NewName);
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpGetNameServer(LqHttp* Http, char* Name, size_t SizeName);

LQ_IMPORTEXPORT void LQ_CALL LqHttpSetKeepAlive(LqHttp* Http, LqTimeMillisec Time);
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqHttpGetKeepAlive(LqHttp* Http);

LQ_IMPORTEXPORT void LQ_CALL LqHttpSetMaxHdrsSize(LqHttp* Http, size_t NewSize);
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpGetMaxHdrsSize(LqHttp* Http);

LQ_IMPORTEXPORT void LQ_CALL LqHttpSetNonceChangeTime(LqHttp* Http, LqTimeSec NewTime);
LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqHttpGetNonceChangeTime(LqHttp* Http);

LQ_IMPORTEXPORT void LQ_CALL LqHttpSetLenConveyor(LqHttp* Http, size_t NewLen);
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpGetLenConveyor(LqHttp* Http);

/*
* @Proc Must return 0 - for continue, -1 - for interrupt
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpEnumConn(LqHttp* Http, int(LQ_CALL* Proc)(void*, LqHttpConn*), void* UserData);

LQ_IMPORTEXPORT LqFche* LQ_CALL LqHttpGetCache(LqHttp* Http);

#define LqHttpConnGetHttp(HttpConn) ((LqHttp*)(HttpConn)->UserData2)
#define LqHttpConnGetData(HttpConn) ((LqHttpConnData*)(HttpConn)->UserData)

#define LqHttpConnGetRcvHdrs(HttpConn) (LqHttpConnGetData(HttpConn)->RcvHdr)

/*----------------------------
*/

LQ_IMPORTEXPORT void LQ_CALL LqHttpConnRspError(LqHttpConn* HttpConn, int Code);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspFile(LqHttpConn* HttpConn, const char* PathToFile);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspFilePart(LqHttpConn* HttpConn, const char* lqain PathToFile, LqFileSz OffsetInFile, LqFileSz Length);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspRedirection(LqHttpConn* HttpConn, int StatusCode, const char* lqain lqaopt Link);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnRetrieveResponseStatus(LqHttpConn* HttpConn, const char* lqain lqaopt PathToFile);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspFileMultipart(LqHttpConn* HttpConn, const char* lqain lqaopt PathToFile, const char* lqain lqaopt Boundary, const LqFileSz* lqain lqaopt Ranges, int CountRanges);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnRspRetrieveMultipartRanges(LqHttpConn* HttpConn, LqFileSz* lqaout DestRanges, int CountRanges);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspFileAuto(LqHttpConn* HttpConn, const char* lqain lqaopt PathToFile, const char* lqain lqaopt Boundary);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRspPrintf(LqHttpConn* HttpConn, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRspPrintfVa(LqHttpConn* HttpConn, const char* Fmt, va_list Va);
/* Used for manual create heders*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRspHdrPrintf(LqHttpConn* HttpConn, const char* Fmt, ...);
/* Used for manual create heders*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRspHdrPrintfVa(LqHttpConn* HttpConn, const char* Fmt, va_list Va);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspHdrInsert(LqHttpConn* HttpConn, const char* HeaderName, const char* HeaderVal);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspHdrGet(LqHttpConn* HttpConn, const char* HeaderName, const char** StartVal, size_t* Len);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspHdrEnumNext(LqHttpConn* HttpConn, const char** StartName, size_t* Len, const char** StartVal, size_t* ValLen);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRspWrite(LqHttpConn* HttpConn, const void* Source, size_t Size);
LQ_IMPORTEXPORT const char* LQ_CALL LqHttpConnRcvHdrGet(LqHttpConn* HttpConn, const char* Name);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnRcvGetBoundary(LqHttpConn* HttpConn, char* lqaout lqaopt Dest, int MaxDest);

LQ_IMPORTEXPORT LqFileSz LQ_CALL LqHttpConnRspGetContentLength(LqHttpConn* HttpConn);

LQ_IMPORTEXPORT const char* LQ_CALL LqHttpConnRcvGetFileName(LqHttpConn* HttpConn);
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqHttpConnRcvGetContentLength(LqHttpConn* HttpConn);
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqHttpConnRcvGetContentLengthLeft(LqHttpConn* HttpConn);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRcvTryScanf(LqHttpConn* HttpConn, int Flags, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRcvTryScanfVa(LqHttpConn* HttpConn, int Flags, const char* Fmt, va_list Va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRcvTryRead(LqHttpConn* HttpConn, void* Buf, size_t Len);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqHttpConnRcvTryPeek(LqHttpConn* HttpConn, void* Buf, size_t Len);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvFile(
    LqHttpConn* HttpConn,
    const char* lqaopt lqain Path,
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* lqain UserData,
    LqFileSz ReadLen, /* Can be -1*/
    int Access,       /* Create access */
    bool IsReplace,
    bool IsCreateSubdir
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvFbuf(
    LqHttpConn* HttpConn,
    LqFbuf* Target,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    LqFileSz ReadLen
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvFileAboveBoundary(
    LqHttpConn* HttpConn,
    const char* lqain lqaopt Path,
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* lqain UserData,
    const char* lqain lqaopt Boundary,
    LqFileSz MaxLen,
    int Access,
    bool IsReplace,
    bool IsCreateSubdir
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvFbufAboveBoundary(
    LqHttpConn* HttpConn,
    LqFbuf* Target,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* Boundary,
    LqFileSz MaxLen
);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnRcvMultipartHdrs(
    LqHttpConn* HttpConn,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* lqaopt lqain Boundary,
    LqHttpMultipartHdrs** lqaout lqaopt Dest
);

LQ_IMPORTEXPORT void LQ_CALL LqHttpMultipartHdrsDelete(LqHttpMultipartHdrs* Target);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvMultipartFbufNext(
    LqHttpConn* HttpConn,
    LqFbuf* lqain Target,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* lqain UserData,
    const char* lqain lqaopt Boundary,
    LqFileSz MaxLen
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvMultipartFileNext(
    LqHttpConn* HttpConn,
    const char* Path,
    bool(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    const char* Boundary,
    LqFileSz MaxLen,
    int Access,
    bool IsReplace,
    bool IsCreateSubdir
);

/*
	Wait until the required number of bytes is received in the internal buffer 
*/
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRcvWaitLen(
    LqHttpConn* HttpConn,
    void(LQ_CALL*CompleteOrCancelProc)(LqHttpConnRcvResult*),
    void* UserData,
    intptr_t TargetLen
);

LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnGetRemoteIpStr(LqHttpConn* HttpConn, char* DestStr, size_t DestStrLen);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnGetRemoteIp(LqHttpConn* HttpConn, LqConnAddr* AddrDest);
LQ_IMPORTEXPORT unsigned short LQ_CALL LqHttpConnGetRemotePort(LqHttpConn* HttpConn);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnGetLocIpStr(LqHttpConn* HttpConn, char* DestStr, size_t DestStrLen);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnGetLocIp(LqHttpConn* HttpConn, LqConnAddr* AddrDest);
/*
 Call LqHttpConnRspEndLongPoll in @LongPollCloseHandler for release connection
    You can set @LongPollCloseHandler parameter in NULL for non closing conn
*/
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspBeginLongPoll(LqHttpConn* HttpConn, void (LQ_CALL *LongPollCloseHandler)(LqHttpConn*));
LQ_IMPORTEXPORT bool LQ_CALL LqHttpConnRspEndLongPoll(LqHttpConn* HttpConn);

LQ_IMPORTEXPORT void LQ_CALL LqHttpConnLock(LqHttpConn* HttpConn);
LQ_IMPORTEXPORT void LQ_CALL LqHttpConnUnlock(LqHttpConn* HttpConn);

LQ_IMPORTEXPORT void LQ_CALL LqHttpConnAsyncClose(LqHttpConn* HttpConn);

LQ_IMPORTEXPORT int LQ_CALL LqHttpConnDataStore(LqHttpConn* HttpConn, const void* Name, const void* Value);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnDataGet(LqHttpConn* HttpConn, const void* Name, void** Value);
LQ_IMPORTEXPORT int LQ_CALL LqHttpConnDataUnstore(LqHttpConn* HttpConn, const void* Name);

/*
* Protocol notify handlers
*/

LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsRegisterQuery(LqHttp* Http, LqHttpNotifyFn QueryFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsUnregisterQuery(LqHttp* Http, LqHttpNotifyFn QueryFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsRegisterResponse(LqHttp* Http, LqHttpNotifyFn ResponseFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsUnregisterResponse(LqHttp* Http, LqHttpNotifyFn ResponseFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsRegisterConnect(LqHttp* Http, LqHttpNotifyFn ConnectFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsUnregisterConnect(LqHttp* Http, LqHttpNotifyFn ConnectFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsRegisterDisconnect(LqHttp* Http, LqHttpNotifyFn DisconnectFunc);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpHndlsUnregisterDisconnect(LqHttp* Http, LqHttpNotifyFn DisconnectFunc);

LQ_EXTERN_C_END

#endif
