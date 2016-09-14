/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpAct... - Functions for work with action.
*/


#include "LqHttp.h"
#include "LqOs.h"
#include "LqHttpConn.h"
#include "LqConn.h"
#include "LqAlloc.hpp"
#include "LqHttpPth.hpp"


intptr_t LqHttpConnHdrEnm(bool IsResponse, char* Buf, size_t SizeHeaders, char** HeaderNameResult, char** HeaderNameResultEnd, char** HeaderValResult, char** HeaderValEnd)
{
    if(SizeHeaders <= 0)
        return -1;
    char *Name, *NameEnd, *Val, *ValEnd;
    char *i, *m = Buf + SizeHeaders - 1;
    if(*HeaderNameResult == nullptr)
    {
        i = Buf;
        if(IsResponse)
        {
            if(((i + 5) < m) && (i[0] == 'H') && (i[1] == 'T') && (i[2] == 'T') && (i[3] == 'P') && (i[4] == '/'))
            {
                i += 5;
                for(; ; i++)
                {
                    if(((i + 1) >= m))
                        return -1;
                    if((*i == '\r') && (i[1] == '\n'))
                    {
                        i += 2;
                        if(((i + 2) >= m) || (*i == '\r') && (i[1] == '\n'))
                            return -1;
                        break;
                    }
                }
            }
        } else
        {
            for(; ; i++)
            {
                if(((i + 1) >= m))
                    return -1;
                if((*i == '\r') && (i[1] == '\n'))
                {
                    i += 2;
                    if(((i + 2) >= m) || (*i == '\r') && (i[1] == '\n'))
                        return -1;
                    break;
                }
            }
        }

    } else
    {
        i = *HeaderValEnd + 2;
        if(i >= m)
            return -1;
    }

    for(; (i < m) && ((*i == ' ') || (*i == '\t')); i++);
    Name = i;
    for(;; i++)
    {
        if(((i + 1) >= m) || (*i == '\r') && (i[1] == '\n'))
            return -1;
        if(*i == ':')
            break;
    }
    NameEnd = i;
    i++;
    for(;; i++)
    {
        if((i + 1) >= m)
            return -1;
        if((*i != ' ') && (*i != '\t'))
            break;
    }
    Val = i;
    for(; ; i++)
    {
        if((i + 1) >= m)
            return -1;
        if((*i == '\r') && (i[1] == '\n'))
            break;
    }
    ValEnd = i;
    *HeaderNameResult = Name;
    *HeaderNameResultEnd = NameEnd;
    *HeaderValResult = Val;
    *HeaderValEnd = ValEnd;
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqHttpConnPthRemove(LqHttpConn* c)
{
    if(c->Pth != nullptr)
    {
        LqHttpPthRelease(c->Pth);
        c->Pth = nullptr;
    }
}

LQ_EXTERN_C int LQ_CALL LqHttpConnGetRemoteIpStr(const LqHttpConn* c, char* DestStr, size_t DestStrSize)
{
    switch(LqHttpConnGetRmtAddr(c)->sa_family)
    {
        case AF_INET:
            if(inet_ntop(AF_INET, &((sockaddr_in*)LqHttpConnGetRmtAddr(c))->sin_addr, DestStr, DestStrSize) == nullptr)
                return 0;
            return 4;
        case AF_INET6:
            if(inet_ntop(AF_INET6, &((sockaddr_in6*)LqHttpConnGetRmtAddr(c))->sin6_addr, DestStr, DestStrSize) == nullptr)
                return 0;
            return 6;
        default:
            return 0;
    }
}

LQ_EXTERN_C int LQ_CALL LqHttpConnGetRemotePort(const LqHttpConn* c)
{
    switch(LqHttpConnGetRmtAddr(c)->sa_family)
    {
        case AF_INET:
            return ntohs(((sockaddr_in*)LqHttpConnGetRmtAddr(c))->sin_port);
        case AF_INET6:
            return ntohs(((sockaddr_in6*)LqHttpConnGetRmtAddr(c))->sin6_port);
        default:
            return 0;
    }
}

bool LqHttpConnBufferRealloc(LqHttpConn* c, size_t NewSize)
{
    char* NewBuff = (char*)___realloc(c->Buf, NewSize);
    if(NewBuff == nullptr)
        return false;
    c->Buf = NewBuff;
    c->BufSize = NewSize;
    return true;
}

size_t LqHttpConnSend_Native(LqHttpConn* c, const void* SourceBuf, size_t SizeBuf)
{
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        return LqConnSendSSL(&c->CommonConn, SourceBuf, SizeBuf, c->ssl);
#endif
    return LqConnSend(&c->CommonConn, SourceBuf, SizeBuf);
}

LQ_EXTERN_C void LQ_CALL LqHttpConnCallEvntAct(LqHttpConn * Conn)
{
    auto f = Conn->CommonConn.Flag;
    Conn->EventAct(Conn);
    if(f != Conn->CommonConn.Flag)
    Conn->CommonConn.Flag |= _LQEVNT_FLAG_USER_SET;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpConnSend(LqHttpConn* c, const void* SourceBuf, size_t SizeBuf)
{
    size_t r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnSendSSL(&c->CommonConn, SourceBuf, SizeBuf, c->ssl);
    else
#endif
        r = LqConnSend(&c->CommonConn, SourceBuf, SizeBuf);
    c->WrittenBodySize += r;
    return r;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnSendFromStream(LqHttpConn* c, LqSbuf* Stream, intptr_t Count)
{
    intptr_t r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnSendFromStreamSSL(&c->CommonConn, Stream, Count, c->ssl);
    else
#endif
        r = LqConnSendFromStream(&c->CommonConn, Stream, Count);
    c->WrittenBodySize += r;
    return r;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqHttpConnSendFromFile(LqHttpConn* c, int InFd, LqFileSz OffsetInFile, LqFileSz Count)
{
    LqFileSz r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnSendFromFileSSL(&c->CommonConn, InFd, OffsetInFile, Count c->ssl);
    else
#endif
        r = LqConnSendFromFile(&c->CommonConn, InFd, OffsetInFile, Count);
    c->WrittenBodySize += r;
    return r;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnCountPendingData(LqHttpConn* c)
{
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        return LqConnCountPendingDataSSL(&c->CommonConn, c->ssl);
#endif
    return LqConnCountPendingData(&c->CommonConn);
}

int LqHttpConnRecive_Native(LqHttpConn* c, void* Buf, int ReadSize, int Flags)
{
    int Res;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        Res = LqConnReciveSSL(&c->CommonConn, Buf, ReadSize, Flags, c->ssl);
    else
#endif
        Res = LqConnRecive(&c->CommonConn, Buf, ReadSize, Flags);
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnRecive(LqHttpConn* c, void* Buf, int ReadSize)
{
    int r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnReciveSSL(&c->CommonConn, Buf, ReadSize, 0, c->ssl);
    else
#endif
        r = LqConnRecive(&c->CommonConn, Buf, ReadSize, 0);

    if((r < ReadSize) && LQERR_IS_WOULD_BLOCK)
    {
        if(r == -1)
            r = 0;
        c->ReadedBodySize += r;
    } else
    {
        c->ReadedBodySize += lq_max(r, 0);
    }
    return r;
}

LQ_EXTERN_C int LQ_CALL LqHttpConnPeek(LqHttpConn* c, void* Buf, int ReadSize)
{
    int Res;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        Res = LqConnReciveSSL(&c->CommonConn, Buf, ReadSize, MSG_PEEK, c->ssl);
    else
#endif
        Res = LqConnRecive(&c->CommonConn, Buf, ReadSize, MSG_PEEK);
    if((Res == -1) && LQERR_IS_WOULD_BLOCK)
        Res = 0;
    return Res;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqHttpConnReciveInFile(LqHttpConn* c, int OutFd, LqFileSz Count)
{
    LqFileSz r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnReciveInFileSSL(&c->CommonConn, OutFd, Count, c->ssl);
    else
#endif
        r = LqConnReciveInFile(&c->CommonConn, OutFd, Count);
    if(r < 0)
        c->ReadedBodySize -= r;
    else
        c->ReadedBodySize += r;
    return r;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpConnReciveInStream(LqHttpConn* c, LqSbuf* Stream, intptr_t Count)
{
    intptr_t r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnReciveInStreamSSL(&c->CommonConn, Stream, Count, c->ssl);
    else
#endif
        r = LqConnReciveInStream(&c->CommonConn, Stream, Count);
    if(r < 0)
        c->ReadedBodySize -= r;
    else
        c->ReadedBodySize += r;
    return r;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpConnSkip(LqHttpConn* c, size_t Count)
{
    size_t r;
#if defined(HAVE_OPENSSL)
    if(c->ssl)
        r = LqConnSkipSSL(&c->CommonConn, Count, c->ssl);
    else
#endif
        r = LqConnSkip(&c->CommonConn, Count);
    c->ReadedBodySize += r;
    return r;
}

