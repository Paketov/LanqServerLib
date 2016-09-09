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

#include <string.h>
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
*                   Response functions.
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
        LQ_ERR("LqConnBind() getaddrinfo(%s, %s, *, *) failed \"%s\" \n",
            ((Host != nullptr) && (*Host != '\0')) ? Host : "NULL",
            Port,
            gai_strerror(res));
        return -1;
    }

    for(auto i = Addrs; i != nullptr; i = i->ai_next)
    {
        if((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
            continue;
        LqFileDescrSetInherit(s, 0);
        if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&True, sizeof(True)) == -1)
        {
            LQ_ERR("LqConnBind() setsockopt(%i, SOL_SOCKET, SO_REUSEADDR, &1, sizeof(1)) failed \"%s\"\n", s, strerror(lq_errno));
            continue;
        }

        if(LqConnSwitchNonBlock(s, 1))
        {
            LQ_ERR("LqConnBind() LqConnSwitchNonBlock(%i, 1) failed \"%s\"\n", s, strerror(lq_errno));
            continue;
        }

        if(i->ai_family == AF_INET6)
        {
            if(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&True, sizeof(True)) == -1)
            {
                LQ_ERR("LqConnBind() setsockopt(%i, IPPROTO_IPV6, IPV6_V6ONLY, &1, sizeof(1)) failed \"%s\"\n", s, strerror(lq_errno));
                continue;
            }
        }
        if(bind(s, i->ai_addr, i->ai_addrlen) == -1)
        {
            LQ_ERR("LqConnBind() bind(%i, *, %i) failed \"%s\"\n", s, (int)i->ai_addrlen, strerror(lq_errno));
            closesocket(s);
            s = -1;
            continue;
        }
        if(listen(s, MaxConnections) == -1)
        {
            LQ_ERR("LqConnBind() listen(%s, %i) failed \"%s\"\n", s, MaxConnections, strerror(lq_errno));
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
        LQ_ERR("LqConnBind() not binded to sock\n");
        return -1;
    }
    return s;
}

LQ_EXTERN_C int LQ_CALL LqConnConnect(const char* Address, const char* Port, void* IpPrtAddress, socklen_t* IpPrtAddressLen)
{
    int s = -1;
    addrinfo hi = {0}, *ah = nullptr, *i;
    hi.ai_socktype = SOCK_STREAM;
    hi.ai_family = IPPROTO_TCP;
    hi.ai_protocol = AF_UNSPEC;
    hi.ai_flags = 0;                   //AI_PASSIVE

    int res;
    if((res = getaddrinfo(((Address != nullptr) && (*Address != '\0')) ? Address : (const char*)nullptr, Port, &hi, &ah)) != 0)
    {
        LQ_ERR("LqConnConnect() getaddrinfo(%s, %s, *, *) failed \"%s\" \n",
            ((Address != nullptr) && (*Address != '\0')) ? Address : "NULL",
            Port,
            gai_strerror(res));
        return -1;
    }

    for(i = ah; i != nullptr; i = i->ai_next)
    {
        if((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
            continue;
        if(connect(s, i->ai_addr, i->ai_addrlen) != -1)
            break;
        closesocket(s);
    }
    if(i == nullptr)
    {
        if(ah != nullptr)
            freeaddrinfo(ah);
        LQ_ERR("LqConnConnect() not connected\n");
        return -1;
    }

    if((IpPrtAddress != nullptr) && (IpPrtAddressLen != nullptr))
    {
        memcpy(IpPrtAddress, i->ai_addr, lq_min(i->ai_addrlen, *IpPrtAddressLen));
        *IpPrtAddressLen = i->ai_addrlen;
    }
    if(ah != nullptr)
        freeaddrinfo(ah);
    return s;
}

LQ_EXTERN_C void* LQ_CALL LqConnSslCreate
(
    const void* MethodSSL, /* Example SSLv23_method()*/
    const char* CertFile, /* Example: "server.pem"*/
    const char* KeyFile, /*Example: "server.key"*/
    const char* CipherList,
    int TypeCertFile, /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
    const char* CAFile,
    const char* DhpFile
)
{
#ifdef HAVE_OPENSSL

    static const unsigned char dh1024_p[] = {
        0xB1,0x0B,0x8F,0x96,0xA0,0x80,0xE0,0x1D,0xDE,0x92,0xDE,0x5E,
        0xAE,0x5D,0x54,0xEC,0x52,0xC9,0x9F,0xBC,0xFB,0x06,0xA3,0xC6,
        0x9A,0x6A,0x9D,0xCA,0x52,0xD2,0x3B,0x61,0x60,0x73,0xE2,0x86,
        0x75,0xA2,0x3D,0x18,0x98,0x38,0xEF,0x1E,0x2E,0xE6,0x52,0xC0,
        0x13,0xEC,0xB4,0xAE,0xA9,0x06,0x11,0x23,0x24,0x97,0x5C,0x3C,
        0xD4,0x9B,0x83,0xBF,0xAC,0xCB,0xDD,0x7D,0x90,0xC4,0xBD,0x70,
        0x98,0x48,0x8E,0x9C,0x21,0x9A,0x73,0x72,0x4E,0xFF,0xD6,0xFA,
        0xE5,0x64,0x47,0x38,0xFA,0xA3,0x1A,0x4F,0xF5,0x5B,0xCC,0xC0,
        0xA1,0x51,0xAF,0x5F,0x0D,0xC8,0xB4,0xBD,0x45,0xBF,0x37,0xDF,
        0x36,0x5C,0x1A,0x65,0xE6,0x8C,0xFD,0xA7,0x6D,0x4D,0xA7,0x08,
        0xDF,0x1F,0xB2,0xBC,0x2E,0x4A,0x43,0x71,
    };

    static const unsigned char dh1024_g[] = {
        0xA4,0xD1,0xCB,0xD5,0xC3,0xFD,0x34,0x12,0x67,0x65,0xA4,0x42,
        0xEF,0xB9,0x99,0x05,0xF8,0x10,0x4D,0xD2,0x58,0xAC,0x50,0x7F,
        0xD6,0x40,0x6C,0xFF,0x14,0x26,0x6D,0x31,0x26,0x6F,0xEA,0x1E,
        0x5C,0x41,0x56,0x4B,0x77,0x7E,0x69,0x0F,0x55,0x04,0xF2,0x13,
        0x16,0x02,0x17,0xB4,0xB0,0x1B,0x88,0x6A,0x5E,0x91,0x54,0x7F,
        0x9E,0x27,0x49,0xF4,0xD7,0xFB,0xD7,0xD3,0xB9,0xA9,0x2E,0xE1,
        0x90,0x9D,0x0D,0x22,0x63,0xF8,0x0A,0x76,0xA6,0xA2,0x4C,0x08,
        0x7A,0x09,0x1F,0x53,0x1D,0xBF,0x0A,0x01,0x69,0xB6,0xA2,0x8A,
        0xD6,0x62,0xA4,0xD1,0x8E,0x73,0xAF,0xA3,0x2D,0x77,0x9D,0x59,
        0x18,0xD0,0x8B,0xC8,0x85,0x8F,0x4D,0xCE,0xF9,0x7C,0x2A,0x24,
        0x85,0x5E,0x6E,0xEB,0x22,0xB3,0xB2,0xE5,
    };

    SSL_CTX* NewCtx = nullptr;

    bool r = false;
    static bool IsLoaded = false;

    LQ_BREAK_BLOCK_BEGIN
    if(!IsLoaded)
    {
        IsLoaded = true;
        SSL_library_init();
        SSL_load_error_strings();
    }

    if((NewCtx = SSL_CTX_new((const SSL_METHOD*)MethodSSL)) == nullptr)
        break;

    SSL_CTX_set_verify(NewCtx, SSL_VERIFY_NONE, nullptr);

    if(CipherList != nullptr)
    {
        if(SSL_CTX_set_cipher_list(NewCtx, CipherList) == 1)
            SSL_CTX_set_options(NewCtx, SSL_OP_CIPHER_SERVER_PREFERENCE);
    }

    if(CAFile != nullptr)
    {
        if(!SSL_CTX_load_verify_locations(NewCtx, CAFile, NULL))
        {
            SSL_CTX_free(NewCtx);
            NewCtx = nullptr;
            break;
        }

    }
    if((SSL_CTX_use_certificate_file(NewCtx, CertFile, TypeCertFile) <= 0) ||
        (SSL_CTX_use_PrivateKey_file(NewCtx, KeyFile, TypeCertFile) <= 0))
    {
        SSL_CTX_free(NewCtx);
        NewCtx = nullptr;
        break;
    }

    if(SSL_CTX_check_private_key(NewCtx) != 1)
    {
        SSL_CTX_free(NewCtx);
        NewCtx = nullptr;
        break;
    }

    if(DhpFile != nullptr)
    {
        BIO *bio = BIO_new_file(DhpFile, "r");
        if(bio)
        {
            DH *dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
            BIO_free(bio);
            if(dh)
            {
                SSL_CTX_set_tmp_dh(NewCtx, dh);
                SSL_CTX_set_options(NewCtx, SSL_OP_SINGLE_DH_USE);
                DH_free(dh);
            }
        }
    } else
    {
        DH *dh = DH_new();
        if(dh)
        {
            dh->p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), NULL);
            dh->g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), NULL);
            dh->length = 160;
            if(dh->p && dh->g)
            {
                SSL_CTX_set_tmp_dh(NewCtx, dh);
                SSL_CTX_set_options(NewCtx, SSL_OP_SINGLE_DH_USE);
            }
            DH_free(dh);
        }
    }
    LQ_BREAK_BLOCK_END
    return NewCtx;
#else
    return nullptr;
#endif
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
    intptr_t r, wr;
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
*                   Reciving functions.
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

LQ_IMPORTEXPORT int LQ_CALL LqEvntSetClose3(void * Conn)
{
    auto h = (LqEvntHdr*)Conn;
    LqEvntFlag Expected, New;
    do
    {
        Expected = h->Flag;
        New = Expected | LQEVNT_FLAG_END;
    } while(!LqAtmCmpXchg(h->Flag, Expected, New));
    return LqWrkBossCloseEvntSync(h);
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
	intptr_t r, wr;
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
*                   Reciving functions.
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
    auto Flags = fcntl(Fd, F_GETFL, 0);
    if(fcntl(Fd, F_SETFL, (IsNonBlock)? (Flags | O_NONBLOCK): (Flags & ~O_NONBLOCK)) == -1)
        return -1;
#endif
    return 0;
}
