/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqConn... - Functions for work with connections.
*/

#ifndef __LQ_CONN_HAS_INCLUDED_H__
#define __LQ_CONN_HAS_INCLUDED_H__

#include "LqOs.h"
#include "LqErr.h"
#include "LqSbuf.h"
#include "LqDef.h"


#ifndef LQCONN_MAX_LOCAL_SIZE
#define LQCONN_MAX_LOCAL_SIZE 32768
#endif

#if defined(LQPLATFORM_WINDOWS)
# if  defined(_WINDOWS_) && !defined(_WINSOCK2API_)
#  error "Must stay before windows.h!"
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
# include <ws2def.h>
# include <ws2ipdef.h>
# include <wchar.h>

# define gai_strerror gai_strerrorA

# define SHUT_RD    SD_RECEIVE
# define SHUT_WR    SD_SEND
# define SHUT_RDWR  SD_BOTH

# pragma comment(lib, "Ws2_32.lib")
# pragma comment(lib, "Mswsock.lib")
#else
# include <stdio.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <unistd.h>
# include <poll.h>
# include <sys/ioctl.h>
# define closesocket(socket)  close(socket)

#endif

#if defined(HAVE_OPENSSL)
# include <openssl/crypto.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
# include <openssl/ssl.h>
# include <openssl/err.h>
#endif

#include "Lanq.h"


typedef union LqConnInetAddress {
    struct sockaddr         Addr;
    struct sockaddr_in      AddrInet;
    struct sockaddr_in6     AddrInet6;
    struct sockaddr_storage AddrStorage;
} LqConnInetAddress;

LqFileSz LqConnSendFromFile(LqConn* c, int InFd, LqFileSz OffsetInFile, LqFileSz Count);
intptr_t LqConnSendFromStream(LqConn* c, LqSbuf* Stream, intptr_t Count);
size_t LqConnSend(LqConn* c, const void* Buf, size_t WriteSize);

int LqConnRecive(LqConn* c, void* Buf, int ReadSize, int Flags);
size_t LqConnSkip(LqConn* c, size_t Count);

LqFileSz LqConnReciveInFile(LqConn* c, int OutFd, LqFileSz Count);
intptr_t LqConnReciveInStream(LqConn* c, LqSbuf* Stream, intptr_t Count);
LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqConnCountPendingData(LqConn* c);
/* Return -1 is err. */
LQ_IMPORTEXPORT int LQ_CALL LqConnSwitchNonBlock(int Fd, int IsNonBlock);

/*
* @Conn - Target connection or children of connection.
* @Flag - New flags LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP, LQEVNT_FLAG_RDHUP
* @return: 0 - Time out, 1 - thread work set a new value
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetFlags(void* Conn, LqEvntFlag Flag, LqTimeMillisec WaitTime /* Wait while worker set new events value*/);

/*
* Set close connection. (In async mode)
*  @Conn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose(void* lqain Conn);

/*
* Set close immediately(call close handler in worker owner)
*  !!! Be careful when use this function !!!
*  @Conn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose2(void* lqain Conn, LqTimeMillisec WaitTime);

/*
* Set close force immediately(call close handler if found event header immediately)
*  @Conn: LqConn or LqEvntFd
*  @return: 1- when close handle called, <= 0 - when not deleted
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose3(void* lqain Conn);

/*
* Remove event from main worker boss immediately(not call close handler)
*  @Conn: LqConn or LqEvntFd
*  @return: 1- when removed, <= 0 - when not removed
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetRemove3(void* lqain Conn);

LQ_IMPORTEXPORT int LQ_CALL LqConnBind(const char* Host, const char* Port, int RouteProto, int SockType, int TransportProto, int MaxConnections, bool IsNonBlock);

LQ_IMPORTEXPORT int LQ_CALL LqConnConnect(const char* lqain Address, const char* lqain Port, int RouteProto, int SockType, int TransportProto, void* lqaout lqaopt IpPrtAddress, socklen_t* lqaio lqaopt IpPrtAddressLen, bool IsNonBlock);

LQ_IMPORTEXPORT int LQ_CALL LqConnStrToRowIp(int TypeIp, const char* lqain SourseStr, LqConnInetAddress* lqaout DestAddress);

LQ_IMPORTEXPORT int LQ_CALL LqConnRowIpToStr(LqConnInetAddress* lqain SourceAddress, char* lqaout DestStr, size_t DestStrLen);

LQ_IMPORTEXPORT void LQ_CALL __LqEvntFdDfltHandler(LqEvntFd* Instance, LqEvntFlag Flags);

LQ_IMPORTEXPORT void LQ_CALL __LqEvntFdDfltCloseHandler(LqEvntFd*);

/*
* Add new file descriptor to follow async
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntFdAdd(LqEvntFd* lqain Evnt);

/*
* Add new file descriptor force immediately
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntFdAdd2(LqEvntFd* lqain Evnt);

/*
* @return: SSL_CTX
*/
LQ_IMPORTEXPORT void* LQ_CALL LqConnSslCreate(
    const void* lqain MethodSSL, /* Example SSLv23_method()*/
    const char* lqain CertFile, /* Example: "server.pem"*/
    const char* lqain KeyFile, /*Example: "server.key"*/
    const char*lqain lqaopt CipherList,
    int TypeCertFile, /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
    const char* lqain lqaopt CAFile,
    const char* lqain lqaopt DhpFile
);

LQ_EXTERN_C_END

#define LqConnInit(Conn, NewFd, NewProto, NewFlags)                     \
    ((LqConn*)(Conn))->Fd = NewFd;                                      \
    ((LqConn*)(Conn))->Proto = NewProto;                                \
    ((LqConn*)(Conn))->Flag = _LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_CONN;\
    LqEvntSetFlags(Conn, NewFlags, 0);                                  \
    ((LqConn*)(Conn))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;

static inline void LQ_CALL __LqProtoEmptyHandler(LqConn*, LqEvntFlag) {}
static inline void LQ_CALL __LqProtoEmptyCloseHandler(LqConn*) {}
static inline bool LQ_CALL __LqProtoEmptyCmpAddressProc(LqConn*, const void*) { return false; }
static inline bool LQ_CALL __LqProtoEmptyKickByTimeOutProc(LqConn*, LqTimeMillisec, LqTimeMillisec) { return false; }
static inline char* LQ_CALL __LqProtoEmptyDebugInfoProc(LqConn*) { return nullptr; }

#define LqProtoInit(Proto) \
    ((LqProto*)Proto)->Handler = __LqProtoEmptyHandler;\
    ((LqProto*)Proto)->CloseHandler = __LqProtoEmptyCloseHandler;\
    ((LqProto*)Proto)->KickByTimeOutProc = __LqProtoEmptyKickByTimeOutProc;\
    ((LqProto*)Proto)->CmpAddressProc = __LqProtoEmptyCmpAddressProc;\
    ((LqProto*)Proto)->DebugInfoProc = __LqProtoEmptyDebugInfoProc;

#define LqEvntHdrClose(Event)                                           \
    (((LqEvntHdr*)(Event))->Flag |= _LQEVNT_FLAG_NOW_EXEC,              \
    ((((LqEvntHdr*)(Event))->Flag & _LQEVNT_FLAG_CONN)?                 \
    ((LqConn*)(Event))->Proto->CloseHandler(((LqConn*)(Event))):         \
    (((LqEvntFd*)(Event))->CloseHandler((LqEvntFd*)(Event)))))

#define LqEvntFdInit(Evnt, NewFd, NewFlags)                             \
    ((LqEvntFd*)(Evnt))->Fd = (NewFd);                                  \
    ((LqEvntFd*)(Evnt))->Flag = _LQEVNT_FLAG_NOW_EXEC ;                 \
    LqEvntSetFlags((LqEvntFd*)(Evnt), NewFlags, 0);                     \
    ((LqEvntFd*)(Evnt))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;



#define LqConnIsClose(Conn) (((LqConn*)(Conn))->Flag | LQEVNT_FLAG_END)
#define LqEvntFdIgnoreHandler(Evnt) (((LqEvntFd*)(Evnt))->Handler = __LqEvntFdDfltHandler)
#define LqEvntFdIgnoreCloseHandler(Evnt) (((LqEvntFd*)(Evnt))->CloseHandler = __LqEvntFdDfltCloseHandler)

#if defined(HAVE_OPENSSL)

LqFileSz LqConnSendFromFileSSL(LqConn* c, int InFd, LqFileSz OffsetInFile, LqFileSz Count, SSL* ssl);
intptr_t LqConnSendFromStreamSSL(LqConn* c, LqSbuf* Stream, intptr_t Count, SSL* ssl);
size_t LqConnSendSSL(LqConn* c, const void* Buf, size_t WriteSize, SSL* ssl);

int LqConnReciveSSL(LqConn* c, void* Buf, int ReadSize, int Flags, SSL* ssl);
LqFileSz LqConnReciveInFileSSL(LqConn* c, int OutFd, LqFileSz Count, SSL* ssl);
intptr_t LqConnReciveInStreamSSL(LqConn* c, LqSbuf* Stream, intptr_t Count, SSL* ssl);

int LqConnCountPendingDataSSL(LqConn* c, SSL* ssl);
size_t LqConnSkipSSL(LqConn* c, size_t Count, SSL* ssl);

#endif

#endif
