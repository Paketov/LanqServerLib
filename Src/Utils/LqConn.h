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
LQ_EXTERN_C_BEGIN
/*
* @Conn - Target connection or children of connection.
* @Flag - New flags LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP, LQEVNT_FLAG_RDHUP
* @return: 0 - Time out, 1 - thread work set a new value
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetFlags(void* Conn, LqEvntFlag Flag, LqTimeMillisec WaitTime = 0 /* Wait while worker set new events value*/);

/*
* Set close connection.
*/
LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose(void* Conn);
#define LqConnIsClose(Conn) (((LqConn*)(Conn))->Flag | LQEVNT_FLAG_END)

/*
* Add new file descriptor to follow
*/
LQ_IMPORTEXPORT bool LQ_CALL LqEvntFdAdd(LqEvntFd* Evnt);

LQ_EXTERN_C_END

#define LqConnInit(Conn, NewFd, NewProto, NewFlags)                     \
    ((LqConn*)(Conn))->Fd = NewFd;                                      \
    ((LqConn*)(Conn))->Proto = NewProto;                                \
    ((LqConn*)(Conn))->Flag = _LQEVNT_FLAG_NOW_EXEC | _LQEVNT_FLAG_CONN;\
    LqEvntSetFlags(Conn, NewFlags);                                     \
    ((LqConn*)(Conn))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;                  \

#define LqEvntHdrClose(Event)                                           \
    (((LqEvntHdr*)(Event))->Flag |= _LQEVNT_FLAG_NOW_EXEC,              \
	((((LqEvntHdr*)(Event))->Flag & _LQEVNT_FLAG_CONN)?                 \
    ((LqConn*)(Event))->Proto->EndConnProc(((LqConn*)(Event))):         \
    (((LqEvntFd*)(Event))->CloseHandler((LqEvntFd*)(Event), 0))))

#define LqEvntFdInit(Evnt, NewFd, NewWrkBoss, NewFlags)                 \
    ((LqEvntFd*)(Evnt))->Boss = (void*)(NewWrkBoss);                    \
    ((LqEvntFd*)(Evnt))->Fd = (NewFd);                                  \
    ((LqEvntFd*)(Evnt))->Flag = _LQEVNT_FLAG_NOW_EXEC ;                 \
    LqEvntSetFlags((LqEvntFd*)(Evnt), NewFlags);                        \
    ((LqEvntFd*)(Evnt))->Flag &= ~_LQEVNT_FLAG_NOW_EXEC;                \


#define LqEvntBossByHdr(EvntHdr)										\
    ((LqWrkBoss*)((((LqEvntHdr*)(EvntHdr))->Flag & _LQEVNT_FLAG_CONN)?  \
    ((LqConn*)(EvntHdr))->Proto->Boss:									\
    ((LqEvntFd*)(EvntHdr))->Boss))

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
