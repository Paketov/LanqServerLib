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
#include "LqHttpConn.h"
#include "LqHttpRcv.h"
#include "LqHttpRsp.h"
#include "LqHttpAtz.h"
#include "LqHttpMdl.h"
#include "LqHttpAct.h"
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


static void LqHttpAtzRsp401Basic(LqHttpConn* Connection);
static void LqHttpAtzRsp401Digest(LqHttpConn* Connection, LqHttpAtz* Authoriz, const char* Nonce, bool IsStyle);
static bool LqHttpAtzGetAuthorizationParametr(char* Str, size_t Len, char* NameParametr, char** Res, size_t* ResLen, bool IsBracketsRequired);
static bool LqHttpAtzGetBasicBase64LoginPassword(char* Str, size_t Len, char** Res, size_t* ResLen);
static char* LqHttpAtzGetBasicCode(const char* User, const char* Password);


/*
* Handler for responce digest nonce
*/
void LQ_CALL LqHttpMdlHandlersNonce(LqHttpConn* c, char* MethodBuf, size_t MethodBufSize) {
    LqTimeMillisec Ms = LqTimeGetLocMillisec();
    LqTimeSec Sec = Ms / 1000;
    if((Sec - LastCheck) > LqHttpProtoGetByConn(c)->PeriodChangeDigestNonce) {
        LastCheck = Sec;
        Random = (((uint64_t)rand() << 32) | (uint64_t)rand()) + Ms % 50000;
    }
    char IpBuf[100];
    IpBuf[0] = '\0';
    LqHttpConnGetRemoteIpStr(c, IpBuf, sizeof(IpBuf));
    uint64_t h = Random;
    for(const char* k = IpBuf; *k != '\0'; k++)
        h = 63 * h + *k;
    LqFbuf_snprintf(MethodBuf, MethodBufSize, "%q64x", h);
}

LQ_EXTERN_C bool LQ_CALL LqHttpAtzDo(LqHttpConn* c, uint8_t AccessMask) {
    auto q = &c->Query;
    auto np = c->Pth;
    auto a = LqObPtrGetEx<LqHttpAtz, _LqHttpAtzDelete, true>(np->Atz, np->AtzPtrLk);
    if((np->Permissions & AccessMask) == AccessMask)
        return true;
    if(a == nullptr) {
        LqHttpRspError(c, 401);
        return false;
    }
    LqHttpAtzLockRead(a.Get());

    if(a->AuthType == LQHTTPATZ_TYPE_BASIC) {
        char *HdrVal, *HdrEndVal;
        if(LqHttpRcvHdrSearch(c, 0, "Authorization", nullptr, &HdrVal, &HdrEndVal) < 0) {
            LqHttpAtzRsp401Basic(c);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        char* Base64LogPass;
        size_t LenLogPass;
        if(!LqHttpAtzGetBasicBase64LoginPassword(HdrVal, HdrEndVal - HdrVal, &Base64LogPass, &LenLogPass)) {
            LqHttpAtzRsp401Basic(c);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        auto t = Base64LogPass[LenLogPass];
        Base64LogPass[LenLogPass] = '\0';
        for(size_t i = 0; i < a->CountAuthoriz; i++) {
            if(LqStrSame(Base64LogPass, a->Basic[i].LoginPassword)) {
                if((a->Basic[i].AccessMask & AccessMask) == AccessMask) {
                    Base64LogPass[LenLogPass] = t;
                    LqHttpAtzUnlock(a.Get());
                    return true;
                }
            }
        }
        Base64LogPass[LenLogPass] = t;
        LqHttpAtzRsp401Basic(c);
    } else if(a->AuthType == LQHTTPATZ_TYPE_DIGEST) {
        char *HdrVal, *HdrEndVal;
        char Nonce[100];
        Nonce[0] = '\0';
        LqHttpMdlGetByConn(c)->NonceProc(c, Nonce, sizeof(Nonce) - 1);
        if(LqHttpRcvHdrSearch(c, 0, "Authorization", nullptr, &HdrVal, &HdrEndVal) < 0) {
            LqHttpAtzRsp401Digest(c, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }


        char* Val, *UserName, *Response, t;
        size_t ValLen, UsernameLen, ResponseLen;
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, HdrEndVal - HdrVal, "nonce", &Val, &ValLen, true)) {
            LqHttpAtzRsp401Digest(c, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        t = Val[ValLen];
        Val[ValLen] = '\0';
        if(!LqStrSame(Val, Nonce)) {
            Val[ValLen] = t;
            LqHttpAtzRsp401Digest(c, a.Get(), Nonce, true);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        Val[ValLen] = t;
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, HdrEndVal - HdrVal, "username", &UserName, &UsernameLen, true)) {
            LqHttpAtzRsp401Digest(c, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        if(!LqHttpAtzGetAuthorizationParametr(HdrVal, HdrEndVal - HdrVal, "uri", &Val, &ValLen, true)) {
            LqHttpAtzRsp401Digest(c, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }
        bool r = LqHttpAtzGetAuthorizationParametr(HdrVal, HdrEndVal - HdrVal, "response", &Response, &ResponseLen, true);
        if(!r || (ResponseLen != LqMd5HexStringLen)) {
            LqHttpAtzRsp401Digest(c, a.Get(), Nonce, false);
            LqHttpAtzUnlock(a.Get());
            return false;
        }

        int UserIndex = -1;
        t = UserName[UsernameLen];
        UserName[UsernameLen] = '\0';

        char HashMethodPath[LqMd5HexStringLen + 1];
        LqMd5 h;
        LqMd5Ctx ctx;
        LqMd5Init(&ctx);
        LqMd5Update(&ctx, q->Method, q->MethodLen);
        LqMd5Update(&ctx, ":", 1);
        LqMd5Update(&ctx, Val, ValLen);
        LqMd5Final((unsigned char*)&h, &ctx);
        LqMd5ToString(HashMethodPath, &h);
        for(size_t i = 0; i < a->CountAuthoriz; i++) {
            if(LqStrSame(UserName, a->Digest[i].UserName)) {
                char HashBuf[LqMd5HexStringLen + 1];
                LqMd5Init(&ctx);
                LqMd5Update(&ctx, a->Digest[i].DigestLoginPassword, LqMd5HexStringLen);
                LqMd5Update(&ctx, ":", 1);
                LqMd5Update(&ctx, Nonce, LqStrLen(Nonce));
                LqMd5Update(&ctx, ":", 1);
                LqMd5Update(&ctx, HashMethodPath, LqMd5HexStringLen);
                LqMd5Final((unsigned char*)&h, &ctx);
                LqMd5ToString(HashBuf, &h);
                if(LqStrSameMax(HashBuf, Response, LqMd5HexStringLen) && ((a->Digest[i].AccessMask & AccessMask) == AccessMask)) {
                    LqHttpAtzUnlock(a.Get());
                    return true;
                }
            }
        }
        LqHttpAtzRsp401Digest(c, a.Get(), Nonce, false);
    }
    LqHttpAtzUnlock(a.Get());
    return false;
}

static bool LqHttpAtzGetBasicBase64LoginPassword(char* Str, size_t Len, char** Res, size_t* ResLen) {
    auto t = Str[Len];
    Str[Len] = '\0';
    int a = -1, b = -1;
    LqFbuf_snscanf(Str, Len, "%*{B|b}asic%*[ ]%n%*[a-zA-Z0-9/+=]%n", &a, &b);
    Str[Len] = t;
    if((a != -1) && (b != -1)) {
        *Res = Str + a;
        *ResLen = b - a;
        return true;
    }
    return false;
}

static bool LqHttpAtzGetAuthorizationParametr(char* Str, size_t Len, char* NameParametr, char** Res, size_t* ResLen, bool IsBracketsRequired) {
    auto t = Str[Len];
    Str[Len] = '\0';
    auto npl = LqStrLen(NameParametr);
    for(size_t i = 0; i < Len; i++)
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
            Str[Len] = t;
            return true;
        }
    Str[Len] = t;
    return false;
}

static void LqHttpAtzRsp401Basic(LqHttpConn* c) {
    LqHttpRspError(c, 401);
    if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_RSP) {
        if(LqHttpRspHdrSearch(c, "WWW-Authenticate", nullptr, nullptr, nullptr) == -1) {
            LqHttpRspHdrAddPrintf(c, "WWW-Authenticate", "Basic realm=\"%s\"", c->Pth->Atz->Realm);
        }
    }
}

static void LqHttpAtzRsp401Digest(LqHttpConn* c, LqHttpAtz* Authoriz, const char* Nonce, bool IsStale) {
    LqHttpRspError(c, 401);
    if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_RSP) {
        if(LqHttpRspHdrSearch(c, "WWW-Authenticate", nullptr, nullptr, nullptr) == -1) {
            LqHttpRspHdrAddPrintf(
                c,
                "WWW-Authenticate",
                "Digest realm=\"%s\", nonce=\"%s\"%s",
                Authoriz->Realm,
                Nonce,
                ((IsStale) ? ", stale=true" : "")
            );
        }
    }
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

