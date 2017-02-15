/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpCore... - Main handlers of HTTP protocol.
*    Scheme of LqHttpProtoBase(For C) or LqHttpProto(For C++) defenition:
*
*    +----------------------------+
*    |        LqHttpProtoBase     |
*    |    +------------------+    |
*    |    |      LqProto     |    |
*    |    +------------------+    |
*    |    ____________________    |
*    |    ____________________    |
*    |                            |
*    |    +--------------------+  |
*    |    |     LqHttpProto    |  |
*    |    |                    |  |
*    |    |                    |  |
*    |    | +-----------------+|  |
*    |    | | Domen and path  ||  |
*    |    | | hash table      ||  |
*    |    | |LqHttpDomainPaths||  |
*    |    | +------|----------+|  |
*    |    +--------|-----------+  |
*    +-------------|--------------+
*      |           |             |
*      |           |            \/
*      \/          |           +---------+
*    +---------+   |           |LqHttpMdl|
*    |LqHttpMdl|   |           +---------+
*    +---------+   |
*      |       |   \/
*      |     +---------+
*      |     |LqHttpPth|
*      \/    +---------+
*     +---------+
*     |LqHttpPth|
*     +---------+
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

LQ_EXTERN_C_BEGIN


typedef LqSockAcceptor LqHttp;
typedef LqSockBuf      LqHttpConn;


struct LqHttpQuery;
struct LqHttpPth;
struct LqHttpAtz;
struct LqHttpMdl;
struct LqHttpExtensionMime;
struct LqHttpResponse;
struct LqHttpMultipartHeaders;
struct LqHttpUserData;

typedef struct LqHttpQuery LqHttpQuery;
typedef struct LqHttpPth LqHttpPth;
typedef struct LqHttpAtz LqHttpAtz;
typedef struct LqHttpMdl LqHttpMdl;
typedef struct LqHttpExtensionMime LqHttpExtensionMime;
typedef struct LqHttpResponse LqHttpResponse;
typedef struct LqHttpMultipartHeaders LqHttpMultipartHeaders;
typedef struct LqHttpUserData LqHttpUserData;

typedef void(LQ_CALL *LqHttpEvntHandlerFn)(LqHttpConn* Connection);

typedef void(LQ_CALL *LqHttpNotifyFn)(LqHttpConn* NewConn);

typedef uint8_t LqHttpActState;
typedef uint16_t LqHttpActResult;


typedef enum LqHttpActResultEnm {
    LQHTTPACT_RES_BEGIN,
    LQHTTPACT_RES_OK,
    LQHTTPACT_RES_PARTIALLY,
    LQHTTPACT_RES_FILE_NOT_GET_INFO,
    LQHTTPACT_RES_FILE_WRITE_ERR,
    LQHTTPACT_RES_FILE_NOT_OPEN,
    LQHTTPACT_RES_FILE_NOT_FOUND,
    LQHTTPACT_RES_NOT_RECIVE,

    LQHTTPACT_RES_STREAM_WRITE_ERR,

    LQHTTPACT_RES_NOT_ALLOC_MEM,

    LQHTTPACT_RES_MULTIPART_END,
    LQHTTPACT_RES_INVALID_HEADER,

    LQHTTPACT_RES_HEADERS_READ_MAX,

    LQHTTPACT_RES_INVALID_TYPE_PATH,

    LQHTTPACT_RES_SSL_FAILED_HANDSHAKE,

    LQHTTPACT_RES_INVALID_ACT_CHAIN,

    LQHTTPACT_RES_CLOSE_CONN //For event proc
} LqHttpActResultEnm;

//Action classes
typedef enum LqHttpActClassEnm {
    LQHTTPACT_CLASS_QER = 0 << (sizeof(LqHttpActState) * 8 - 2),    /*Query field used*/
    LQHTTPACT_CLASS_RSP = 1 << (sizeof(LqHttpActState) * 8 - 2),    /*Response field used*/
    LQHTTPACT_CLASS_CLS = 2 << (sizeof(LqHttpActState) * 8 - 2)     /*Response and Query filed not used*/
} LqHttpActClassEnm;

//All actions
typedef enum LqHttpActEnm {
    LQHTTPACT_STATE_GET_HDRS = LQHTTPACT_CLASS_QER | 1,
    LQHTTPACT_STATE_RCV_INIT_HANDLE = LQHTTPACT_CLASS_QER | 2,
    LQHTTPACT_STATE_RCV_FILE = LQHTTPACT_CLASS_QER | 3,                //Get file from user
    LQHTTPACT_STATE_RCV_STREAM = LQHTTPACT_CLASS_QER | 4,                //Get file from user
    LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS = LQHTTPACT_CLASS_QER | 5,
    LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS = LQHTTPACT_CLASS_QER | 6,
    LQHTTPACT_STATE_MULTIPART_RCV_HDRS = LQHTTPACT_CLASS_QER | 7,
    LQHTTPACT_STATE_MULTIPART_RCV_FILE = LQHTTPACT_CLASS_QER | 8,
    LQHTTPACT_STATE_MULTIPART_RCV_STREAM = LQHTTPACT_CLASS_QER | 9,
    LQHTTPACT_STATE_RESPONSE_HANDLE_PROCESS = LQHTTPACT_CLASS_QER | 10,
    LQHTTPACT_STATE_QUERY_HANDLE_PROCESS = LQHTTPACT_CLASS_RSP | 11,
    LQHTTPACT_STATE_QUERY_SSL_HANDSHAKE = LQHTTPACT_CLASS_QER | 12,
    LQHTTPACT_STATE_RESPONSE_SSL_HANDSHAKE = LQHTTPACT_CLASS_RSP | 13,
    LQHTTPACT_STATE_RSP_INIT_HANDLE = LQHTTPACT_CLASS_RSP | 14,
    LQHTTPACT_STATE_SKIP_QUERY_BODY = LQHTTPACT_CLASS_QER | 15,
    LQHTTPACT_STATE_RSP = LQHTTPACT_CLASS_RSP | 16,
    LQHTTPACT_STATE_RSP_CACHE = LQHTTPACT_CLASS_RSP | 17,
    LQHTTPACT_STATE_RSP_FD = LQHTTPACT_CLASS_RSP | 18,
    LQHTTPACT_STATE_RSP_STREAM = LQHTTPACT_CLASS_RSP | 19,
    LQHTTPACT_STATE_CLS_CONNECTION = LQHTTPACT_CLASS_CLS | 20
} LqHttpActEnm;

typedef enum LqHttpPthResultEnm {
    LQHTTPPTH_RES_OK,
    LQHTTPPTH_RES_ALREADY_HAVE,
    LQHTTPPTH_RES_NOT_ALLOC_MEM,
    LQHTTPPTH_RES_NOT_HAVE_DOMEN,
    LQHTTPPTH_RES_NOT_HAVE_PATH,
    LQHTTPPTH_RES_NOT_HAVE_ATZ,
    LQHTTPPTH_RES_ALREADY_HAVE_ATZ,
    LQHTTPPTH_RES_NOT_DIR,
    LQHTTPPTH_RES_MODULE_REJECT,
    LQHTTPPTH_RES_DOMEN_NAME_OVERFLOW,
    LQHTTPPTH_RES_INVALID_NAME
} LqHttpPthResultEnm;

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


#define LQHTTPPTH_TYPE_SEP 0x07
#define LQ_MAX_CONTENT_LEN 0xffffffffff

#define LQHTTPCONN_FLAG_CLOSE 1
#define LQHTTPCONN_FLAG_NO_BODY 2

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

//struct LqHttpQuery {
//    char*                       Method;
//    size_t                      MethodLen;
//    char*                       Host;
//    size_t                      HostLen;
//    char*                       UserInfo;
//    size_t                      UserInfoLen;
//    char*                       Path;
//    size_t                      PathLen;
//    char*                       Fragment;
//    size_t                      FragmentLen;
//    char*                       ProtoVer;
//    size_t                      ProtoVerLen;
//    char*                       Arg;
//    size_t                      ArgLen;
//    char*                       ContentBoundary;
//    size_t                      ContentBoundaryLen;
//
//    LqFileSz                    ContentLen;
//    LqHttpMultipartHeaders*     MultipartHeaders;
//    size_t                      HeadersEnd;
//    union {
//        int                     OutFd;
//        LqSbuf                  Stream;
//    };
//    LqFileSz                    PartLen;
//};

//struct LqHttpMultipartHeaders {
//    struct {
//        LqFileSz                ReadedBodySize;
//        LqHttpQuery             Query;
//    };
//    size_t                      BufSize;
//    char                        Buf[1];
//};

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
	char*		Method;
	char*		Host;
	char*		UserInfo;
	char*		Port;
	char*		Path;
	char*		Fragment;
	char*		Args;
	char*		ContentBoundary;
	uint8_t		MajorVer;
	uint8_t		MinorVer;
	LqHttpRcvHdr* Hdrs;
	size_t      CountHdrs;
} LqHttpRcvHdrs;

typedef struct LqHttpConnData {
	int              LenRspConveyor;
	LqHttpRcvHdrs*   RcvHdr;
	unsigned short   UserDataCount;
	LqHttpUserData*  UserData;
	uint8_t			 Flags;
	LqHttpPth*       Pth;
} LqHttpConnData;


#pragma pack(pop)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

/*
* Module defenition for http protocol
*/
struct LqHttpMdl {
    LqHttp*             HttpAcceptor;

    char*                       Name;

    size_t                      CountPointers;
    uintptr_t                   Handle;

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
        size_t MimeDestBufLen,

        LqFileStat const* lqain lqaopt Stat/*(Something sends for optimizing)*/
    );

    void (LQ_CALL* GetCacheInfoProc)(
        const char* lqain Path,
        LqHttpConn* lqain lqaopt Connection,

        char* lqaout lqaopt lqautf8 CacheControlDestBuf, /* If after call CacheControlDestBuf == "", then Cache-Control no include in response headers. */
        size_t CacheControlDestBufSize,

        char* lqaout lqaopt lqautf8 EtagDestBuf, /*If after call EtagDestBuf == "", then Etag no include in response headers. */
        size_t EtagDestBufSize,

        LqTimeSec* lqaout lqaopt LastModif, /*Local time. If after call LastModif == -1, then then no response Last-Modified.*/

        LqTimeSec* lqaout lqaopt Expires,

        LqFileStat const* lqain lqaopt Stat /*(Something sends for optimizing)*/
    );
    /* Use for response error to client*/
    int (LQ_CALL* RspErrorProc)(LqHttpConn* lqain c, int lqain Code);
    /* Use for set status in start line*/
    int (LQ_CALL* RspStatusProc)(LqHttpConn* lqain c, int lqain Code);

    /*If NameBuf == "", then ignore name server*/
    void (LQ_CALL* ServerNameProc)(LqHttpConn* lqain c, char* lqaout lqautf8 NameBuf, size_t NameBufSize);

    /*If NameBuf == "", then ignore allow*/
    void (LQ_CALL* AllowProc)(LqHttpConn* lqain c, char* lqaout lqautf8 MethodBuf, size_t MethodBufSize);

    /*Use for digest auth*/
    /*If NameBuf == "", then ignore name server*/
    void (LQ_CALL* NonceProc)(LqHttpConn* lqain c, char* lqaout lqautf8 MethodBuf, size_t MethodBufSize);

    /*Use for send redirect */
    void (LQ_CALL* ResponseRedirectionProc)(LqHttpConn* lqain c);

    /* Must return handler for method or NULL */
    LqHttpEvntHandlerFn(LQ_CALL * GetActEvntHandlerProc)(LqHttpConn* c);

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

LQ_IMPORTEXPORT int LQ_CALL LqHttpDelete(LqHttp* Http);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpGoWork(LqHttp* Http, void* WrkBoss);
LQ_IMPORTEXPORT bool LQ_CALL LqHttpInterruptWork(LqHttp* Http);


LQ_IMPORTEXPORT size_t LQ_CALL LqHttpSetNameServer(LqHttp* Http, const char* NewName);
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpGetNameServer(LqHttp* Http, char* Name, size_t SizeName);

#define LqHttpConnGetHttp(HttpConn) ((LqHttp*)(HttpConn)->UserData2)
#define LqHttpConnGetData(HttpConn) ((LqHttpConnData*)(HttpConn)->UserData)

/*----------------------------
*/

/*
*----------------------------
* Set and get cache parametrs
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpCheGetMaxSize(LqHttp* Http);        /*Get max size cache pool*/
LQ_IMPORTEXPORT void LQ_CALL LqHttpCheSetMaxSize(LqHttp* Http, size_t NewVal);  /* Set max size cache pool */
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpCheGetMaxSizeFile(LqHttp* Http);                   /* Get minimum size file for adding to cache */
LQ_IMPORTEXPORT void LQ_CALL LqHttpCheSetMaxSizeFile(LqHttp* Http, size_t NewVal);
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpCheGetEmployedSize(LqHttp* Http);
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqHttpCheGetPeriodUpdateStat(LqHttp* Http);
LQ_IMPORTEXPORT void LQ_CALL LqHttpCheSetPeriodUpdateStat(LqHttp* Http, LqTimeMillisec Millisec);
LQ_IMPORTEXPORT size_t LQ_CALL LqHttpCheGetMaxCountOfPrepared(LqHttp* Http);

LQ_IMPORTEXPORT void LQ_CALL LqHttpCheSetMaxCountOfPrepared(LqHttp* Http, size_t Count);
/*----------------------------
*/

LQ_IMPORTEXPORT LqEvntFlag LQ_CALL LqHttpEvntGetFlagByAct(LqHttpConn* Conn);
LQ_IMPORTEXPORT int LQ_CALL LqHttpEvntSetFlagByAct(LqHttpConn* Conn);

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
