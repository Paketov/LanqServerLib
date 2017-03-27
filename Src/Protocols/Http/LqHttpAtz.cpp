/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpAtz... - Make user uthorization.
*/


#include "LqOs.h"
#include "LqHttp.hpp"
#include "LqHttp.h"
#include "LqTime.h"
#include "LqMd5.h"
#include "LqAtm.hpp"
#include "LqHttpAtz.h"
#include "LqHttpMdl.h"
#include "LqStr.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#include <stdlib.h>


/*
LqHttpAtz... - authorization functions
*/

#define LqCheckedFree(MemReg) (((MemReg) != nullptr)? free(MemReg): void())


static LqTimeSec LastCheck = LqTimeGetLocSec();
static bool ___d = ([] { srand(LastCheck); return true; })();
static uint64_t Random = rand();


static void LqHttpAtzRsp401Basic(LqHttpConn* HttpConn);
static void LqHttpAtzRsp401Digest(LqHttpConn* HttpConn, LqHttpAtz* Authoriz, const char* Nonce, bool IsStyle);
static bool LqHttpAtzGetAuthorizationParametr(const char* Str, const char* NameParametr, const char** Res, size_t* ResLen, bool IsBracketsRequired);
static bool LqHttpAtzGetBasicBase64LoginPassword(const char* Str, const char** Res, size_t* ResLen);
static char* LqHttpAtzGetBasicCode(const char* User, const char* Password);


//*
//* Handler for responce digest nonce
//*/
void LQ_CALL LqHttpMdlHandlersNonce(LqHttpConn* HttpConn, char* MethodBuf, size_t MethodBufSize) {
    LqTimeMillisec Ms = LqTimeGetLocMillisec();
    LqTimeSec Sec = Ms / 1000;
    uint64_t h;
    char IpBuf[100];
    LqHttpData* HttpData;

    HttpData = LqHttpConnGetHttpData(HttpConn);
    if((Sec - LastCheck) > HttpData->PeriodChangeDigestNonce) {
        LastCheck = Sec;
        Random = (((uint64_t)rand() << 32) | (uint64_t)rand()) + Ms % 50000;
    }
    IpBuf[0] = '\0';
    LqHttpConnGetRemoteIpStr(HttpConn, IpBuf, sizeof(IpBuf));
    h = Random;
    for(const char* k = IpBuf; *k != '\0'; k++)
        h = 63 * h + *k;
    LqFbuf_snprintf(MethodBuf, MethodBufSize, "%q64x", h);
}

LQ_EXTERN_C bool LQ_CALL LqHttpAtzDo(LqHttpConn* HttpConn, uint8_t AccessMask) {
    LqHttpConnData* HttpConnData;
    LqHttpData* HttpData;
    LqHttpMdl* Mdl;
    LqHttpPth* Pth;
    LqHttpRcvHdrs* RcvHdr;
    const char *HdrVal, *NonceParam, *UsernameParam, *UriParam, *ResponsePraram, *Base64LogPass; 
    char *t;
    size_t LenLogPass, NonceParamLen, i, UsernameParamLen, UriParamLen, ResponsePraramLen;
    char Nonce[100];
    char HashMethodPath[LqMd5HexStringLen + 1];
    char HashBuf[LqMd5HexStringLen + 1];
    LqMd5 h;
    LqMd5Ctx ctx;


    Mdl = LqHttpConnGetMdl(HttpConn);
    HttpConnData = LqHttpConnGetData(HttpConn);
    HttpData = LqHttpConnGetHttpData(HttpConn);
    RcvHdr = HttpConnData->RcvHdr;
    Pth = HttpConnData->Pth;

    auto a = LqObPtrGetEx<LqHttpAtz, _LqHttpAtzDelete, true>(Pth->Atz, Pth->AtzPtrLk);
    if((Pth->Permissions & AccessMask) == AccessMask)
        return true;
    if(a == nullptr) {
        LqHttpConnRspError(HttpConn, 401);
        return false;
    }
    LqHttpAtzLockRead(a.Get());

    if(a->AuthType == LQHTTPATZ_TYPE_BASIC) {
        if((HdrVal = LqHttpConnRcvHdrGet(HttpConn, "authorization")) == NULL) {
            LqHttpAtzRsp401Basic(HttpConn);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if(!LqHttpAtzGetBasicBase64LoginPassword(HdrVal, &Base64LogPass, &LenLogPass)) {
            LqHttpAtzRsp401Basic(HttpConn);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        for(i = 0; i < a->CountAuthoriz; i++) {
            if((LenLogPass == LqStrLen(a->Basic[i].LoginPassword)) && LqStrSameMax(Base64LogPass, a->Basic[i].LoginPassword, LenLogPass)) {
                if((a->Basic[i].AccessMask & AccessMask) == AccessMask) {
                    LqHttpAtzUnlock(a.Get());
                    return true;
                }
            }
        }
        LqHttpAtzRsp401Basic(HttpConn);
    } else if(a->AuthType == LQHTTPATZ_TYPE_DIGEST) {
        Nonce[0] = '\0';
        Mdl->NonceProc(HttpConn, Nonce, sizeof(Nonce) - 1);
        if((HdrVal = LqHttpConnRcvHdrGet(HttpConn, "authorization")) == NULL) {
            LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, "nonce", &NonceParam, &NonceParamLen, true)) {
            LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if((NonceParamLen != LqStrLen(Nonce)) || !LqStrSameMax(NonceParam, Nonce, NonceParamLen)) {
            LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, true);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, "username", &UsernameParam, &UsernameParamLen, true)) {
            LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, "uri", &UriParam, &UriParamLen, true)) {
            LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, "response", &ResponsePraram, &ResponsePraramLen, true) || (ResponsePraramLen != LqMd5HexStringLen)) {
            LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }

        LqMd5Init(&ctx);
        LqMd5Update(&ctx, RcvHdr->Method, LqStrLen(RcvHdr->Method));
        LqMd5Update(&ctx, ":", 1);
        LqMd5Update(&ctx, UriParam, UriParamLen);
        LqMd5Final((unsigned char*)&h, &ctx);
        LqMd5ToString(HashMethodPath, &h);
        for(size_t i = 0; i < a->CountAuthoriz; i++) {
            if((UsernameParamLen == LqStrLen(a->Digest[i].UserName)) && LqStrSameMax(UsernameParam, a->Digest[i].UserName, UsernameParamLen)) {
                LqMd5Init(&ctx);
                LqMd5Update(&ctx, a->Digest[i].DigestLoginPassword, LqMd5HexStringLen);
                LqMd5Update(&ctx, ":", 1);
                LqMd5Update(&ctx, Nonce, LqStrLen(Nonce));
                LqMd5Update(&ctx, ":", 1);
                LqMd5Update(&ctx, HashMethodPath, LqMd5HexStringLen);
                LqMd5Final((unsigned char*)&h, &ctx);
                LqMd5ToString(HashBuf, &h);
                if(LqStrSameMax(HashBuf, ResponsePraram, LqMd5HexStringLen) && ((a->Digest[i].AccessMask & AccessMask) == AccessMask)) {
                    LqHttpAtzUnlock(a.Get());
                    return true;
                }
            }
        }
        LqHttpAtzRsp401Digest(HttpConn, a.Get(), Nonce, false);
    }
    LqHttpAtzUnlock(a.Get());
    return false;
}

static bool LqHttpAtzGetBasicBase64LoginPassword(const char* Str, const char** Res, size_t* ResLen) {
    int a = -1, b = -1;
    LqFbuf_snscanf(Str, LqStrLen(Str), "%#*{basic}%*[ ]%n%*[a-zA-Z0-9/+=]%n", &a, &b);
    if((a != -1) && (b != -1)) {
        *Res = Str + a;
        *ResLen = b - a;
        return true;
    }
    return false;
}

static bool LqHttpAtzGetAuthorizationParametr(const char* Str, const char* NameParametr, const char** Res, size_t* ResLen, bool IsBracketsRequired) {
    size_t i;
    size_t npl = LqStrLen(NameParametr);
    for(size_t i = 0; Str[i]; i++)
        if(LqStrUtf8CmpCaseLen(NameParametr, Str + i, npl)) {
            bool b;
            auto g = i + npl;
            for(; Str[g] == ' '; g++);
            if(Str[g++] != '=')
                continue;
            for(; Str[g] == ' '; g++);
            if(Str[g] == '"') {
                g++;
                b = true;
            } else {
                if(IsBracketsRequired)
                    continue;
                b = false;
            }
            *Res = Str + g;
            for(; (Str[g] != '"') && (Str[g] != ',') && (Str[g] != '\0'); g++);
            if(b && (Str[g] != '"'))
                continue;
            *ResLen = (Str + g) - *Res;
            return true;
        }
    return false;
}

static void LqHttpAtzRsp401Basic(LqHttpConn* HttpConn) {
    char HdrVal[4096];
    LqHttpConnData* HttpConnData;
    HttpConnData = LqHttpConnGetData(HttpConn);

    LqFbuf_snprintf(HdrVal, sizeof(HdrVal) - 1, "Basic realm=\"%s\"", HttpConnData->Pth->Atz->Realm);
    LqHttpConnRspHdrInsert(HttpConn, "WWW-Authenticate", HdrVal);
    LqHttpConnRspError(HttpConn, 401);
}

static void LqHttpAtzRsp401Digest(LqHttpConn* HttpConn, LqHttpAtz* Authoriz, const char* Nonce, bool IsStale) {
    char HdrVal[4096];

    LqFbuf_snprintf(HdrVal, sizeof(HdrVal) - 1, "Digest realm=\"%s\", nonce=\"%s\"%s", Authoriz->Realm, Nonce, ((IsStale) ? ", stale=true" : ""));
    LqHttpConnRspHdrInsert(HttpConn, "WWW-Authenticate", HdrVal);
    LqHttpConnRspError(HttpConn, 401);
}

static char* LqHttpAtzGetBasicCode(const char* User, const char* Password) {
    size_t l = LqStrLen(User) + LqStrLen(Password) + 3;
    char* Step1Buf = (char*)malloc(l);
    if(Step1Buf == nullptr)
        return nullptr;
    Step1Buf[0] = '\0';
    LqFbuf_snprintf(Step1Buf, l, "%s:%s", User, Password);
    size_t Step2BufLen = (size_t)((float)l * 1.4f) + 2;
    char* Step2Buf = (char*)calloc(Step2BufLen, 1);
    if(Step2Buf == nullptr) {
        free(Step1Buf);
        return nullptr;
    }
    /* Write base 64*/
    LqFbuf_snprintf(Step2Buf, Step2BufLen, "%#b", Step1Buf);
    free(Step1Buf);
    return Step2Buf;
}

LQ_EXTERN_C LqHttpAtz* LQ_CALL LqHttpAtzCreate(LqHttpAtzTypeEnm AuthType, const char* Realm) {
    LqHttpAtz* Atz = LqFastAlloc::New<LqHttpAtz>();
    if(Atz == nullptr)
        return nullptr;
    memset(Atz, 0, sizeof(LqHttpAtz));

    Atz->CountPointers = 1;
    LqAtmLkInit(Atz->Locker);
    Atz->AuthType = AuthType;
    Atz->Realm = LqStrDuplicate(Realm);
    if(Atz->Realm == nullptr) {
        LqHttpAtzRelease(Atz);
        return nullptr;
    }
    return Atz;
}

LQ_EXTERN_C bool LQ_CALL LqHttpAtzAdd(LqHttpAtz* NetAutoriz, uint8_t AccessMask, const char* UserName, const char* Password) {
    if(NetAutoriz == nullptr)
        return false;
    LqHttpAtzLockWrite(NetAutoriz);
    if(NetAutoriz->AuthType == LQHTTPATZ_TYPE_BASIC) {
        char* c = LqHttpAtzGetBasicCode(UserName, Password);
        if(c == nullptr) {
            LqHttpAtzUnlock(NetAutoriz);
            return false;
        }
        decltype(NetAutoriz->Basic) m = (decltype(NetAutoriz->Basic))realloc(NetAutoriz->Basic, (NetAutoriz->CountAuthoriz + 1) * sizeof(NetAutoriz->Basic[0]));
        if(m == nullptr) {
            LqHttpAtzUnlock(NetAutoriz);
            free(c);
            return false;
        }
        NetAutoriz->Basic = m;
        NetAutoriz->Basic[NetAutoriz->CountAuthoriz].AccessMask = AccessMask;
        NetAutoriz->Basic[NetAutoriz->CountAuthoriz].LoginPassword = c;
        NetAutoriz->CountAuthoriz++;
    } else if(NetAutoriz->AuthType == LQHTTPATZ_TYPE_DIGEST) {
        char* un = LqStrDuplicate(UserName);
        if(un == nullptr) {
            LqHttpAtzUnlock(NetAutoriz);
            return false;
        }
        decltype(NetAutoriz->Digest) m = (decltype(NetAutoriz->Digest))realloc(NetAutoriz->Digest, (NetAutoriz->CountAuthoriz + 1) * sizeof(NetAutoriz->Digest[0]));
        if(m == nullptr) {
            LqHttpAtzUnlock(NetAutoriz);
            free(un);
            return false;
        }

        NetAutoriz->Digest = m;
        NetAutoriz->Digest[NetAutoriz->CountAuthoriz].AccessMask = AccessMask;
        NetAutoriz->Digest[NetAutoriz->CountAuthoriz].UserName = un;

        LqMd5Ctx ctx;
        LqMd5Init(&ctx);
        LqMd5Update(&ctx, UserName, LqStrLen(UserName));
        LqMd5Update(&ctx, ":", 1);
        LqMd5Update(&ctx, NetAutoriz->Realm, LqStrLen(NetAutoriz->Realm));
        LqMd5Update(&ctx, ":", 1);
        LqMd5Update(&ctx, Password, LqStrLen(Password));
        LqMd5 h;
        LqMd5Final((unsigned char*)&h, &ctx);
        LqFbuf_snprintf(
            NetAutoriz->Digest[NetAutoriz->CountAuthoriz].DigestLoginPassword,
            sizeof(NetAutoriz->Digest[NetAutoriz->CountAuthoriz].DigestLoginPassword),
            "%.*v", //Print hex string
            (int)sizeof(h),
            &h
        );
        NetAutoriz->CountAuthoriz++;
    }
    LqHttpAtzUnlock(NetAutoriz);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqHttpAtzRemove(LqHttpAtz* NetAutoriz, const char* UserName) {
    if(NetAutoriz == nullptr)
        return false;
    LqHttpAtzLockWrite(NetAutoriz);
    if(NetAutoriz->AuthType == LQHTTPATZ_TYPE_BASIC) {
        auto l = LqStrLen(UserName);
        size_t nc = NetAutoriz->CountAuthoriz;
        char Buf[4096];
        for(size_t i = 0; i < nc;) {
            Buf[0] = '\0';
            LqFbuf_snscanf(NetAutoriz->Basic[i].LoginPassword, LqStrLen(NetAutoriz->Basic[i].LoginPassword), "%.4095b", Buf);
            if(LqStrSameMax(Buf, UserName, l)) {
                LqCheckedFree(NetAutoriz->Basic[i].LoginPassword);
                memmove(NetAutoriz->Basic + i, NetAutoriz->Basic + (i + 1), (nc - (i + 1)) * sizeof(NetAutoriz->Basic[0]));
                nc--;
                continue;
            }
            i++;
        }
        NetAutoriz->Basic = (decltype(NetAutoriz->Basic))realloc(NetAutoriz->Basic, nc * sizeof(NetAutoriz->Basic[0]));
        NetAutoriz->CountAuthoriz = nc;
    } else if(NetAutoriz->AuthType == LQHTTPATZ_TYPE_DIGEST) {
        size_t nc = NetAutoriz->CountAuthoriz;
        for(size_t i = 0; i < nc;) {
            if(LqStrSame(NetAutoriz->Digest[i].UserName, UserName)) {
                LqCheckedFree(NetAutoriz->Digest[i].UserName);
                memmove(NetAutoriz->Digest + i, NetAutoriz->Digest + (i + 1), (nc - (i + 1)) * sizeof(NetAutoriz->Digest[0]));
                nc--;
                continue;
            }
            i++;
        }
        NetAutoriz->Digest = (decltype(NetAutoriz->Digest))realloc(NetAutoriz->Digest, nc * sizeof(NetAutoriz->Digest[0]));
        NetAutoriz->CountAuthoriz = nc;
    }
    LqHttpAtzUnlock(NetAutoriz);
    return true;
}

LQ_EXTERN_C void LQ_CALL LqHttpAtzAssign(LqHttpAtz* NetAutoriz) {
    if(NetAutoriz == nullptr)
        return;
    LqAtmIntrlkInc(NetAutoriz->CountPointers);
}

void _LqHttpAtzDelete(LqHttpAtz* NetAutoriz) {
    if(NetAutoriz->AuthType == LQHTTPATZ_TYPE_BASIC) {
        if(NetAutoriz->Basic != nullptr) {
            for(size_t i = 0, m = NetAutoriz->CountAuthoriz; i < m; i++)
                LqCheckedFree(NetAutoriz->Basic[i].LoginPassword);
            LqCheckedFree(NetAutoriz->Basic);
        }
    } else if(NetAutoriz->AuthType == LQHTTPATZ_TYPE_DIGEST) {
        if(NetAutoriz->Digest != nullptr) {
            for(size_t i = 0, m = NetAutoriz->CountAuthoriz; i < m; i++)
                LqCheckedFree(NetAutoriz->Digest[i].UserName);
            LqCheckedFree(NetAutoriz->Digest);
        }
    }
    LqCheckedFree(NetAutoriz->Realm);
    LqFastAlloc::Delete(NetAutoriz);
}


LQ_EXTERN_C bool LQ_CALL LqHttpAtzRelease(LqHttpAtz* NetAutoriz) {
    if(NetAutoriz == nullptr)
        return false;
    return LqObPtrDereference<LqHttpAtz, _LqHttpAtzDelete>(NetAutoriz);
}

LQ_EXTERN_C void LQ_CALL LqHttpAtzLockWrite(LqHttpAtz* NetAutoriz) {
    if(NetAutoriz != nullptr)
        LqAtmLkWr(NetAutoriz->Locker);
}

LQ_EXTERN_C void LQ_CALL LqHttpAtzLockRead(LqHttpAtz* NetAutoriz) {
    if(NetAutoriz != nullptr)
        LqAtmLkRd(NetAutoriz->Locker);
}

LQ_EXTERN_C void LQ_CALL LqHttpAtzUnlock(LqHttpAtz* NetAutoriz) {
    if(NetAutoriz != nullptr) {
        if(LqAtmLkIsRd(NetAutoriz->Locker))
            LqAtmUlkRd(NetAutoriz->Locker);
        else
            LqAtmUlkWr(NetAutoriz->Locker);
    }
}

