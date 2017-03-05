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
# include <openssl/dh.h>
# include <openssl/crypto.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
# include <openssl/ssl.h>
# include <openssl/err.h>
#endif

#include "Lanq.h"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef union LqConnAddr {
    struct sockaddr         Addr;
    struct sockaddr_in      AddrInet;
    struct sockaddr_in6     AddrInet6;
    struct sockaddr_storage AddrStorage;
} LqConnAddr;

#pragma pack(pop)

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqConnCountPendingData(LqConn* c);
/* Return -1 is err. */
LQ_IMPORTEXPORT int LQ_CALL LqConnSwitchNonBlock(int Fd, int IsNonBlock);

LQ_IMPORTEXPORT int LQ_CALL LqConnBind(const char* lqain Host, const char* lqain Port, int RouteProto, int SockType, int TransportProto, int MaxConnections, bool IsNonBlock);

LQ_IMPORTEXPORT int LQ_CALL LqConnConnect(const char* lqain Address, const char* lqain Port, int RouteProto, int SockType, int TransportProto, void* lqaout lqaopt IpPrtAddress, socklen_t* lqaio lqaopt IpPrtAddressLen, bool IsNonBlock);

LQ_IMPORTEXPORT int LQ_CALL LqConnStrToRowIp(int TypeIp, const char* lqain SourseStr, LqConnAddr* lqaout DestAddress);

LQ_IMPORTEXPORT int LQ_CALL LqConnRowIpToStr(LqConnAddr* lqain SourceAddress, char* lqaout DestStr, size_t DestStrLen);

/*
* @return: SSL_CTX
*/
LQ_IMPORTEXPORT void* LQ_CALL LqConnSslCreate(
    const void* lqain lqaopt MethodSSL, /* Example SSLv23_method()*/
    const char* lqain CertFile, /* Example: "server.pem"*/
    const char* lqain KeyFile, /*Example: "server.key"*/
    const char*lqain lqaopt CipherList,
    int TypeCertFile, /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
    const char* lqain lqaopt CAFile,
    const char* lqain lqaopt DhpFile
);

LQ_IMPORTEXPORT void LQ_CALL LqConnSslDelete(void* Ctx);

LQ_EXTERN_C_END

#endif
