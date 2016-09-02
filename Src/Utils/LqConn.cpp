/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqConn... - Functions for work with connections.
*/


#include "LqConn.h"
#include "LqDef.hpp"
#include "LqFile.h"
#include "LqWrkBoss.hpp"
#include "LqTime.h"
#include "LqAtm.hpp"
#include "LqLog.h"

#include <wchar.h>

#if defined(__linux__)
# include <sys/sendfile.h>
# include <fcntl.h>
#elif defined(__FreeBSD__)
# include <sys/uio.h>
# include <fcntl.h>
#elif defined(__APPLE__)
# include <sys/uio.h>
# include <fcntl.h>
#elif defined(_MSC_VER)

# pragma comment(lib, "Ws2_32.lib")
# pragma comment(lib, "Mswsock.lib")

# ifndef WSA_VERSION
#  define WSA_VERSION MAKEWORD(2, 2)
# endif

# include <io.h>
static LPWSADATA ____f =
(
    []() -> LPWSADATA
{
    static WSADATA wd;
    if(WSAStartup(WSA_VERSION, &wd) == 0)
        return &wd;
    return nullptr;
}
)();
#endif

/*
*					Response functions.
*/



LQ_EXTERN_C int LQ_CALL LqConnBind(const char* Host, const char* Port, int TransportProtoFamily, int MaxConnections)
{
	static const int True = 1;
	int s;
	addrinfo *Addrs = nullptr, HostInfo = {0};
	HostInfo.ai_family = TransportProtoFamily;
	HostInfo.ai_socktype = SOCK_STREAM;
	HostInfo.ai_flags = AI_PASSIVE;//AI_ALL;
	HostInfo.ai_protocol = IPPROTO_TCP;
	int res;
	if((res = getaddrinfo(((Host != nullptr) && (*Host != '\0')) ? Host : (const char*)nullptr, Port, &HostInfo, &Addrs)) != 0)
	{
		LQ_ERR("getaddrinfo() failed \"%s\" ", gai_strerror(res));
		return false;
	}

	for(auto i = Addrs; i != nullptr; i = i->ai_next)
	{
		if((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
			continue;
		LqFileDescrSetInherit(s, 0);
		if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&True, sizeof(True)) == -1)
		{
			LQ_ERR("setsockopt() failed");
			continue;
		}

		if(LqConnSwitchNonBlock(s, 1))
		{
			LQ_ERR("not swich socket to non blocket mode");
			continue;
		}

		if(i->ai_family == AF_INET6)
		{
			if(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&True, sizeof(True)) == -1)
			{
				LQ_ERR("setsockopt() failed");
				continue;
			}
		}
		if(bind(s, i->ai_addr, i->ai_addrlen) == -1)
		{
			LQ_ERR("bind() failed with error: %s\n", strerror(lq_errno));
			closesocket(s);
			s = -1;
			continue;
		}
		if(listen(s, MaxConnections) == -1)
		{
			LQ_ERR("bind() failed");
			closesocket(s);
			s = -1;
			continue;
		}
		break;
	}

	if(Addrs != nullptr)
		freeaddrinfo(Addrs);
	if(s == -1)
	{
		LQ_ERR("not binded to sock");
		return false;
	}
	return s;
}

LQ_EXTERN_C void LQ_CALL __LqEvntFdDfltHandler(LqEvntFd * Instance, LqEvntFlag Flags)
{
}

/*
* @return: count written in sock. Always >= 0.
*/
size_t LqConnSend(LqConn* c, const void* Buf, size_t WriteSize)
{
    int s;
    size_t Sended = 0;
    const auto MaxSendSizeInTact = c->Proto->MaxSendInTact;
    const auto MaxSendSize = c->Proto->MaxSendInSingleTime;
    while(WriteSize > 0)
    {
        if(Sended > MaxSendSize)
            break;
        if((s = send(c->Fd, (const char*)Buf + Sended, (WriteSize > MaxSendSizeInTact) ? MaxSendSizeInTact : WriteSize, 0)) > 0)
        {
            Sended += s;
            if((WriteSize -= s) <= 0)
                break;
        } else
        {
            break;
        }
    }
    return Sended;
}


/*
*  @return: count written in sock. Always >= 0.
*/
LqFileSz LqConnSendFromFile(LqConn* c, int InFd, LqFileSz OffsetInFile, LqFileSz Count)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    LqFileSz Sended = 0;
    int r, wr;
    if(LqFileSeek(InFd, OffsetInFile, LQ_SEEK_SET) < 0)
        return -1;
    const auto MaxSendSize = c->Proto->MaxSendInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Sended) > 0; Sended += r)
    {
        if(Sended > MaxSendSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = LqFileRead(InFd, Buf, ReadSize)) < 1)
            break;
        if((wr = send(c->Fd, Buf, r, 0)) == -1)
            break;
        if(wr < r)
            return Sended + wr;
    }
    return Sended;
}

/*
*  @return: count written in sock. Always >= 0.
*/
intptr_t LqConnSendFromStream(LqConn* c, LqSbuf* Stream, intptr_t Count)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    size_t Sended = 0;
    int r, wr;
    const auto MaxSendSize = c->Proto->MaxSendInSingleTime;
    for(intptr_t ReadSize; (ReadSize = Count - Sended) > 0; Sended += r)
    {
        if(Sended > MaxSendSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if(((r = LqSbufPeek(Stream, Buf, ReadSize)) < 1) || ((wr = send(c->Fd, Buf, r, 0)) == -1))
            return Sended;
        LqSbufRead(Stream, nullptr, wr);
        if((wr < r) || (r < ReadSize))
            return Sended + wr;
    }
    return Sended;
}

/*
*					Reciving functions.
*/

/*
*  @return: count readed from sock. If is less than 0 - which means that not all the data has been written to file.
*/
LqFileSz LqConnReciveInFile(LqConn* c, int OutFd, LqFileSz Count)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    int r;

    LqFileSz Readed = 0;
    const auto MaxReciveSize = c->Proto->MaxReciveInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Readed) > 0; )
    {
        if(Readed > MaxReciveSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = recv(c->Fd, (char*)Buf, ReadSize, 0)) < 1)
            break;
        Readed += r;
        if(LqFileWrite(OutFd, Buf, r) < r)
            return -Readed;
        if(r < ReadSize)
            break;
    }
    return Readed;
}

/*
* @return: count readed from sock. If is less than 0 - which means that not all the data has been written to stream.
*/

intptr_t LqConnReciveInStream(LqConn* c, LqSbuf* Stream, intptr_t Count)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    int r;
    LqFileSz Readed = 0;
    const auto MaxReciveSize = c->Proto->MaxReciveInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Readed) > 0; )
    {
        if(Readed > MaxReciveSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = recv(c->Fd, (char*)Buf, ReadSize, 0)) < 1)
            break;
        Readed += r;
        if(LqSbufWrite(Stream, Buf, r) < r)
            return -Readed;
        if(r < ReadSize)
            break;
    }
    return Readed;
}


LQ_EXTERN_C int LQ_CALL LqEvntSetFlags(void* Conn, LqEvntFlag Flag, LqTimeMillisec WaitTime)
{
    auto h = (LqEvntHdr*)Conn;
    if(h->Flag & LQEVNT_FLAG_END)
        return 0;
	LqEvntFlag Expected, New;
	bool IsSync;
	do
	{
		Expected = h->Flag;
		New = (Expected & ~(LQEVNT_FLAG_RD | LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP)) | Flag;
		if((IsSync = !(Expected & _LQEVNT_FLAG_NOW_EXEC)) && (WaitTime > 0))
			New |= _LQEVNT_FLAG_SYNC;
	} while(!LqAtmCmpXchg(h->Flag, Expected, New));
    if(IsSync)
    {
		LqWrkBossSyncEvntFlagAsync(h);
        if(WaitTime <= 0)
            return 1;
        auto Start = LqTimeGetLocMillisec();
        while(h->Flag & _LQEVNT_FLAG_SYNC)
        {
            LqThreadYield();
            if((LqTimeGetLocMillisec() - Start) > WaitTime)
                return 1;
        }
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqEvntSetClose(void* Conn)
{
	auto h = (LqEvntHdr*)Conn;
	LqEvntFlag Expected, New;
	bool IsSync;
	do
	{
		Expected = h->Flag;
		New = Expected | LQEVNT_FLAG_END;
		IsSync = !(Expected & _LQEVNT_FLAG_NOW_EXEC);
	} while(!LqAtmCmpXchg(h->Flag, Expected, New));
	if(IsSync)
		LqWrkBossSyncEvntFlagAsync(h);
	return 0;
}

LQ_EXTERN_C int LQ_CALL LqEvntSetClose2(void* Conn, LqTimeMillisec WaitTime)
{
    auto h = (LqEvntHdr*)Conn;
	LqEvntFlag Expected, New;
	bool IsSync;
	do
	{
		Expected = h->Flag;
		New = Expected | LQEVNT_FLAG_END;
		if((IsSync = !(Expected & _LQEVNT_FLAG_NOW_EXEC)) && (WaitTime > 0))
			New |= _LQEVNT_FLAG_SYNC;
	} while(!LqAtmCmpXchg(h->Flag, Expected, New));
	if(IsSync)
	{
		LqWrkBossCloseEvntAsync(h);
		if(WaitTime <= 0)
			return 1;
		auto Start = LqTimeGetLocMillisec();
		while(h->Flag & _LQEVNT_FLAG_SYNC)
		{
			LqThreadYield();
			if((LqTimeGetLocMillisec() - Start) > WaitTime)
				return 1;
		}
	}
    return 0;
}

LQ_EXTERN_C bool LQ_CALL LqEvntFdAdd(LqEvntFd* Evnt)
{
	return LqWrkBossAddEvntAsync((LqEvntHdr*)Evnt);
}

int LqConnRecive(LqConn* c, void* Buf, int ReadSize, int Flags)
{
    return recv(c->Fd, (char*)Buf, ReadSize, Flags);
}

size_t LqConnSkip(LqConn* c, size_t Count)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    int r;
    LqFileSz Readed = 0;
    const auto MaxReciveSize = c->Proto->MaxReciveInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Readed) > 0; )
    {
        if(Readed > MaxReciveSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = recv(c->Fd, (char*)Buf, ReadSize, 0)) < 1)
            break;
        Readed += r;
        if(r < ReadSize)
            break;
    }

    return Readed;
}


////////////////SSL
#if defined(HAVE_OPENSSL)
/*
* @return: count written in sock. Always >= 0.
*/
size_t LqConnSendSSL(LqConn* c, const void* Buf, size_t WriteSize, SSL* ssl)
{
    int s;
    size_t Sended = 0;
    const size_t MaxSendSizeInTact = c->Proto->MaxSendInTact;
    const size_t MaxSendSize = c->Proto->MaxSendInSingleTime;
    while(WriteSize > 0)
    {
        if(Sended > MaxSendSize)
            break;
        if((s = SSL_write(ssl, (const char*)Buf + Sended, (WriteSize > MaxSendSizeInTact) ? MaxSendSizeInTact : WriteSize)) > 0)
        {
            Sended += s;
            if((WriteSize -= s) == 0)
                break;
        } else
        {
            break;
        }
    }
    return Sended;
}

/*
*  @return: count written in sock. Always >= 0.
*/
LqFileSz LqConnSendFromFileSSL(LqConn* c, int InFd, LqFileSz OffsetInFile, LqFileSz Count, SSL* ssl)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    LqFileSz Sended = 0;
    int r, wr;
    if(LqFileSeek(InFd, OffsetInFile, LQ_SEEK_SET) < 0)
        return -1;
    const auto MaxSendSize = c->Proto->MaxSendInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Sended) > 0; Sended += r)
    {
        if(Sended > MaxSendSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = LqFileRead(InFd, Buf, ReadSize)) < 1)
            break;
        if((wr = SSL_write(ssl, Buf, r)) < 1)
            break;
        if(wr < r)
            return Sended + wr;
    }
    return Sended;
}

/*
*  @return: count written in sock. Always >= 0.
*/
intptr_t LqConnSendFromStreamSSL(LqConn* c, LqSbuf* Stream, intptr_t Count, SSL* ssl)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    size_t Sended = 0;
    int r, wr;
    const auto MaxSendSize = c->Proto->MaxSendInSingleTime;
    for(size_t ReadSize; (ReadSize = Count - Sended) > 0; Sended += r)
    {
        if(Sended > MaxSendSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if(((r = LqSbufPeek(Stream, Buf, ReadSize)) < 1) || ((wr = SSL_write(ssl, Buf, r)) < 1))
            return Sended;
        LqSbufRead(Stream, nullptr, wr);
        if((wr < r) || (r < ReadSize))
            return Sended + wr;
    }
    return Sended;
}

/*
*					Reciving functions.
*/

/*
*  @return: count readed from sock. If is less than 0 - which means that not all the data has been written to file.
*/
LqFileSz LqConnReciveInFileSSL(LqConn* c, int OutFd, LqFileSz Count, SSL* ssl)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    int r;

    LqFileSz Readed = 0;
    const auto MaxReciveSize = c->Proto->MaxReciveInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Readed) > 0; )
    {
        if(Readed > MaxReciveSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = SSL_read(c->Fd, (char*)Buf, ReadSize)) < 1)
            break;
        Readed += r;
        if(LqFileWrite(OutFd, Buf, r) < r)
            return -Readed;
        if(r < ReadSize)
            break;
    }
    return Readed;
}

/*
* @return: count readed from sock. If is less than 0 - which means that not all the data has been written to stream.
*/

intptr_t LqConnReciveInStreamSSL(LqConn* c, LqSbuf* Stream, intptr_t Count, SSL* ssl)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    int r;
    LqFileSz Readed = 0;
    const auto MaxReciveSize = c->Proto->MaxReciveInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Readed) > 0; )
    {
        if(Readed > MaxReciveSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = SSL_read(c->Fd, (char*)Buf, ReadSize)) < 1)
            break;
        Readed += r;
        if(LqSbufWrite(Stream, Buf, r) < r)
            return -Readed;
        if(r < ReadSize)
            break;
    }
    return Readed;
}

int LqConnReciveSSL(LqConn* c, void* Buf, int ReadSize, int Flags, SSL* ssl)
{
    return ((Flags & MSG_PEEK) ? SSL_peek : SSL_read)(ssl, Buf, ReadSize);
}

int LqConnCountPendingDataSSL(LqConn* c, SSL* ssl)
{
    auto res = SSL_pending(ssl);
    if(res < 500) return 500;
    return res;
}

size_t LqConnSkipSSL(LqConn* c, size_t Count, SSL* ssl)
{
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    int r;
    LqFileSz Readed = 0;
    const size_t MaxReciveSize = c->Proto->MaxReciveInSingleTime;
    for(LqFileSz ReadSize; (ReadSize = Count - Readed) > 0; )
    {
        if(Readed > MaxReciveSize)
            break;
        if(ReadSize > sizeof(Buf))
            ReadSize = sizeof(Buf);
        if((r = SSL_read(ssl, (char*)Buf, ReadSize)) < 1)
            break;
        Readed += r;
        if(r < ReadSize)
            break;
    }
    return Readed;
}

#endif

int LqConnCountPendingData(LqConn* c)
{
#ifdef _MSC_VER
    u_long res = -1;
    if(ioctlsocket(c->Fd, FIONREAD, &res) == -1)
        return -1;
#else
    int res;
    if(ioctl(c->Fd, FIONREAD, &res) < 0)
        return -1;
#endif
    return res;
}

int LqConnSwitchNonBlock(int Fd, int IsNonBlock)
{
#ifdef _MSC_VER
    u_long nonBlocking = IsNonBlock;
    if(ioctlsocket(Fd, FIONBIO, &nonBlocking) == -1)
        return -1;
#else
    int nonBlocking = IsNonBlock;
    if(fcntl(Fd, F_SETFL, O_NONBLOCK, nonBlocking) == -1)
        return -1;
#endif
    return 0;
}
