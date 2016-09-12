/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqBase64... - Base64 functions.
*/

#include "LqBse64.h"
#include "LqDef.hpp"

template<bool Pad>
static size_t _CodeBase64(uchar *Dst, const uchar *Src, size_t SrcLen, const uchar *basis);
static int _DecodeBase64(uchar *Dst, const uchar* Src, size_t SrcLen, const uchar *DecodeChain);

static const uchar CodeChain[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const uchar CodeChainURL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"; //-_-

static const uchar DecodeChain[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

static const uchar DecodeSeqURL[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

LQ_EXTERN_C size_t LQ_CALL LqBase64Code(char *Dest, const void *Src, size_t SrcLen) { return _CodeBase64<true>((uchar*)Dest, (const uchar*)Src, SrcLen, CodeChain); }
LQ_EXTERN_C size_t LQ_CALL LqBase64UrlCode(char *Dest, const void *Src, size_t SrcLen) { return _CodeBase64<false>((uchar*)Dest, (const uchar*)Src, SrcLen, CodeChainURL); }

LQ_EXTERN_CPP LqString LQ_CALL LqBase64CodeToStlStr(const void *Src, size_t SrcLen)
{
    LqString r("", ((float)SrcLen * 1.4f) + 2);
    size_t l = _CodeBase64<true>((uchar*)r.data(), (const uchar*)Src, SrcLen, CodeChain);
    r.resize(l);
    return r;
}

LQ_EXTERN_CPP LqString LQ_CALL LqBase64UrlCodeToStlStr(const void *Src, size_t SrcLen)
{
    LqString r("", ((float)SrcLen * 1.4f) + 2);
    size_t l = _CodeBase64<false>((uchar*)r.data(), (const uchar*)Src, SrcLen, CodeChainURL);
    r.resize(l);
    return r;
}

template<bool Pad>
static size_t _CodeBase64(uchar *Dst, const uchar *Src, size_t SrcLen, const uchar *CodeChain)
{
    const uchar *s = Src;
    uchar *d = Dst;
    size_t l = SrcLen;
    while(l > 2)
    {
        *d++ = CodeChain[(s[0] >> 2) & 0x3f];
        *d++ = CodeChain[((s[0] & 3) << 4) | (s[1] >> 4)];
        *d++ = CodeChain[((s[1] & 0x0f) << 2) | (s[2] >> 6)];
        *d++ = CodeChain[s[2] & 0x3f];
        s += 3;
        l -= 3;
    }

    if(l > 0)
    {
        *d++ = CodeChain[(s[0] >> 2) & 0x3f];
        if(l == 1)
        {
            *d++ = CodeChain[(s[0] & 3) << 4];
            if(Pad) *d++ = '=';
        } else
        {
            *d++ = CodeChain[((s[0] & 3) << 4) | (s[1] >> 4)];
            *d++ = CodeChain[(s[1] & 0x0f) << 2];
        }
        if(Pad) *d++ = '=';
    }
    return d - Dst;
}

LQ_EXTERN_C int LQ_CALL LqBase64Decode(void *Dst, const char *Src, size_t SrcLen) { return _DecodeBase64((uchar*)Dst, (const uchar*)Src, SrcLen, DecodeChain); }
LQ_EXTERN_C int LQ_CALL LqBase64UrlDecode(void *Dst, const char *Src, size_t SrcLen) { return _DecodeBase64((uchar*)Dst, (const uchar*)Src, SrcLen, DecodeSeqURL); }

LQ_IMPORTEXPORT LqString LQ_CALL LqBase64DecodeToStlStr(const char *Src, size_t SrcLen)
{
    LqString r("", ((float)SrcLen * 0.8f) + 2);
    int l = LqBase64Decode((void*)r.data(), Src, SrcLen);
    if(l == -1) return LqString();
    r.resize(l);
    return r;
}

LQ_IMPORTEXPORT LqString LQ_CALL LqBase64UrlDecodeToStlStr(const char *Src, size_t SrcLen)
{
    LqString r("", ((float)SrcLen * 0.8f) + 2);
    int l = LqBase64UrlDecode((void*)r.data(), Src, SrcLen);
    if(l == -1) return LqString();
    r.resize(l);
    return r;
}

static int _DecodeBase64(uchar *Dst, const uchar* Src, size_t SrcLen, const uchar *DecodeChain)
{
    size_t l = 0;
    /*Checkin base64 sequence*/
    for(; l < SrcLen; l++)
    {
        if(Src[l] == '=') break;
        if((DecodeChain[Src[l]] == 77) || (Src[l] > 122)) return -1;
    }
    if(l % 4 == 1) return -1;
    uchar *d = Dst;
    const uchar *s = Src;
    /*Decode sequence*/
    while(l > 3)
    {
        *d++ = (uchar)(DecodeChain[s[0]] << 2 | DecodeChain[s[1]] >> 4);
        *d++ = (uchar)(DecodeChain[s[1]] << 4 | DecodeChain[s[2]] >> 2);
        *d++ = (uchar)(DecodeChain[s[2]] << 6 | DecodeChain[s[3]]);
        s += 4;
        l -= 4;
    }

    if(l > 1) *d++ = (uchar)(DecodeChain[s[0]] << 2 | DecodeChain[s[1]] >> 4);
    if(l > 2) *d++ = (uchar)(DecodeChain[s[1]] << 4 | DecodeChain[s[2]] >> 2);

    return d - Dst;
}

