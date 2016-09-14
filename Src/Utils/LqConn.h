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

#define gai_strerror gai_strerrorA



#define SHUT_RD    SD_RECEIVE
#define SHUT_WR    SD_SEND
#define SHUT_RDWR  SD_BOTH

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


union LqConnInetAddress
{
	sockaddr			Addr;
	sockaddr_in			AddrInet;
	sockaddr_in6		AddrInet6;
	sockaddr_storage	AddrStorage;
};

int LqConnCountPendingData(LqConn* c);
/* Return -1 is err. */
int LqConnSwitchNonBlock(int Fd, int IsNonBlock);

LqFileSz LqConnSendFromFile(LqConn* c, int InFd, LqFileSz OffsetInFile, LqFileSz Count);
intptr_t LqConnSendFromStream(LqConn* c, LqSbuf* Stream, intptr_t Count);
size_t LqConnSend(LqConn* c, const void* Buf, size_t WriteSize);

int LqConnRecive(LqConn* c, void* Buf, int ReadSize, int Flags);
size_t LqConnSkip(LqConn* c, size_t Count);

LqFileSz LqConnReciveInFile(LqConn* c, int OutFd, LqFileSz Count);
intptr_t LqConnReciveInStream(LqConn* c, LqSbuf* Stream, intptr_t Count);
LQ_EXTERN_C_BEGIN
/*
* @Conn - Target connection or children of connection.
* @Flag - New flags LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP, LQEVNT_FLAG_RDHUP
* @return: 0 - Time out, 1 - thread work set a new value
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetFlags(void* Conn, LqEvntFlag Flag, LqTimeMillisec WaitTime = 0 /* Wait while worker set new events value*/);

/*
* Set close connection. (In async mode)
*  @Conn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose(void* lqain Conn);

/*
* Set close immediately(call close handler in worker owner)
*  @Conn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose2(void* lqain Conn, LqTimeMillisec WaitTime);

/*
* Set close force immediately(call close handler in worker owner)
*  @Conn: LqConn or LqEvntFd
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose3(void* lqain Conn);

LQ_IMPORTEXPORT int LQ_CALL LqConnBind(const char* lqain lqaopt Host, const char* lqain Port, int TransportProtoFamily, int MaxConnections);

LQ_IMPORTEXPORT int LQ_CALL LqConnConnect(const char* lqain Address, const char* lqain Port, void* lqaout lqaopt IpPrtAddress, socklen_t* lqaio lqaopt IpPrtAddressLen);

LQ_IMPORTEXPORT int LQ_CALL LqConnStrToRowIp(int TypeIp, const char* lqain SourseStr, LqConnInetAddress* lqaout DestAddress);

LQ_IMPORTEXPORT int LQ_CALL LqConnRowIpToStr(LqConnInetAddress* lqain SourceAddress, char* lqaout DestStr, size_t DestStrLen);

LQ_IMPORTEXPORT void LQ_CALL __LqEvntFdDfltHandler(LqEvntFd* Instance, LqEvntFlag Flags);

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
LQ_IMPORTEXPORT void* LQ_CALL LqConnSslCreate
(
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
    LqEvntSetFlags(Conn, NewFlags);                                     \
    ((LqConn*)(Conn))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;

#define LqEvntHdrClose(Event)                                           \
    (((LqEvntHdr*)(Event))->Flag |= _LQEVNT_FLAG_NOW_EXEC,              \
	((((LqEvntHdr*)(Event))->Flag & _LQEVNT_FLAG_CONN)?                 \
    ((LqConn*)(Event))->Proto->EndConnProc(((LqConn*)(Event))):         \
    (((LqEvntFd*)(Event))->CloseHandler((LqEvntFd*)(Event), 0))))

#define LqEvntFdInit(Evnt, NewFd, NewFlags)                             \
    ((LqEvntFd*)(Evnt))->Fd = (NewFd);                                  \
    ((LqEvntFd*)(Evnt))->Flag = _LQEVNT_FLAG_NOW_EXEC ;                 \
    LqEvntSetFlags((LqEvntFd*)(Evnt), NewFlags);                        \
    ((LqEvntFd*)(Evnt))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;

#define LqConnIsClose(Conn) (((LqConn*)(Conn))->Flag | LQEVNT_FLAG_END)
#define LqEvntFdIgnoreHandler(Evnt) (((LqEvntFd*)(Evnt))->Handler = __LqEvntFdDfltHandler)
#define LqEvntFdIgnoreCloseHandler(Evnt) (((LqEvntFd*)(Evnt))->CloseHandler = __LqEvntFdDfltHandler)

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
