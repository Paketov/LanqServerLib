/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqConn... - Functions for work with connections.
*/

#ifndef __LQ_CONN_HAS_INCLUDED_H__
#define __LQ_CONN_HAS_INCLUDED_H__

#include "LqErr.h"
#include "LqSbuf.h"
#include "LqDef.h"
#include "LqOs.h"

#ifndef LQCONN_MAX_LOCAL_SIZE
#define LQCONN_MAX_LOCAL_SIZE 32768
#endif

#if defined(_MSC_VER)
# if  defined(_WINDOWS_) && !defined(_WINSOCK2API_)
#  error "Must stay before windows.h!"
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
# include <ws2def.h>
# include <ws2ipdef.h>
# include <wchar.h>

#define gai_strerror gai_strerrorA


# ifdef EWOULDBLOCK
#  define LQCONN_IS_WOULD_BLOCK (lq_errno == EWOULDBLOCK)
# else
#  define LQCONN_IS_WOULD_BLOCK (lq_errno == EAGAIN)
# endif
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
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

# if defined(__sun__)
#  define LQCONN_IS_WOULD_BLOCK (uwsgi_is_again())
# elif defined(EWOULDBLOCK) && defined(EAGAIN)
#  define LQCONN_IS_WOULD_BLOCK ((lq_errno == EWOULDBLOCK) || (lq_errno == EAGAIN))
# elif defined(EWOULDBLOCK)
#  define LQCONN_IS_WOULD_BLOCK (lq_errno == EWOULDBLOCK)
# else
#  define LQCONN_IS_WOULD_BLOCK (lq_errno == EAGAIN)
# endif

#endif

#if defined(HAVE_OPENSSL)
# include <openssl/crypto.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
# include <openssl/ssl.h>
# include <openssl/err.h>
#endif

#include "Lanq.h"


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

#define LqConnSetEvents(Conn, Flags) (void)(((LqConn*)(Conn))->Flag &= ~(LQCONN_FLAG_RD | LQCONN_FLAG_WR | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP), ((LqConn*)(Conn))->Flag |= (Flags));

#define LqConnSetClose(Conn) (void)(((LqConn*)(Conn))->Flag |= LQCONN_FLAG_END)
#define LqConnIsClose(Conn) (((LqConn*)(Conn))->Flag | LQCONN_FLAG_END)

#define LqConnLock(Conn) (void)(((LqConn*)(Conn))->Flag |= LQCONN_FLAG_LOCK)
#define LqConnIsLock(Conn) (((LqConn*)(Conn))->Flag & LQCONN_FLAG_LOCK)

int LqConnWaitUnlock(LqConn* Conn, LqTimeMillisec WaitTime);
void LqConnUnlock(LqConn* Conn);


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
