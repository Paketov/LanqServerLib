/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpRsp... - Functions for response headers and content in connection.
*/

#include "LqOs.h"
#include "LqConn.h"
#include "LqDef.hpp"
#include "LqHttpRsp.h"
#include "LqHttp.hpp"
#include "LqHttpConn.h"
#include "LqFileChe.hpp"

#include "LqTime.h"
#include "LqFile.h"
#include "LqStr.hpp"
#include "LqStrSwitch.h"
#include "LqHttpRcv.h"
#include "LqHttpPrs.h"
#include "LqHttpAct.h"
#include "LqHttpMdl.h"

#include <stdarg.h>
#include <stdio.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

LQ_EXTERN_C int LQ_CALL LqHttpRspError(LqHttpConn* c, int Status) {
    return LqHttpMdlGetByConn(c)->RspErrorProc(c, Status);
}

LQ_EXTERN_C int LQ_CALL LqHttpRspStatus(LqHttpConn* c, int Status) {
    return LqHttpMdlGetByConn(c)->RspStatusProc(c, Status);
}

static void LqHttpRspBasicHeaders(LqHttpConn* c, char* Etag, char* CacheControl, tm* LastMod, tm* Expires) {
    if(LastMod != nullptr)
        LqHttpRspHdrAddPrintf(c, "Last-Modified", PRINTF_TIME_TM_FORMAT_GMT, PRINTF_TIME_TM_ARG_GMT(*LastMod));
    if(Expires != nullptr)
        LqHttpRspHdrAddPrintf(c, "Expires", PRINTF_TIME_TM_FORMAT_GMT, PRINTF_TIME_TM_ARG_GMT(*Expires));
    if(CacheControl[0] != '\0')
        LqHttpRspHdrAdd(c, "Cache-Control", CacheControl);
    if(Etag[0] != '\0')
        LqHttpRspHdrAdd(c, "ETag", Etag);
}

static bool LqHttpRsp200Headers(LqHttpConn* c, LqFileSz SizeFile, char* MimeType, char* Etag, char* CacheControl, tm* LastMod, tm* CurrTime, tm* Expires) {
    if(LqHttpRspStatus(c, 200)) {
        LqHttpActSwitchToClose(c);
        return false;
    }
    LqHttpRspHdrAdd(c, "Content-Type", (MimeType == nullptr) ? "application/octet-stream" : MimeType);
    LqHttpRspHdrAddPrintf(c, "Content-Length", "%llu", (unsigned long long)SizeFile);
    LqHttpRspBasicHeaders(c, Etag, CacheControl, LastMod, Expires);
    return true;
}

static bool LqHttpRsp304Headers(LqHttpConn* c, char* Etag, char* CacheControl, tm* LastMod, tm* CurrTime, tm* Expires) {
    if(LqHttpRspStatus(c, 304)) {
        LqHttpActSwitchToClose(c);
        return false;
    }
    LqHttpRspBasicHeaders(c, Etag, CacheControl, LastMod, Expires);
    LqHttpActKeepOnlyHeaders(c);
    return true;
}

int LqHttpRspPrintRangeBoundaryHeaders(char* Buf, size_t SizeBuf, LqHttpConn* c, LqFileSz SizeFile, LqFileSz Start, LqFileSz End, const char* MimeType) {
    return LqFwbuf_snprintf(
        Buf,
        SizeBuf,
        "\r\n--z3d6b6a416f9b53e416f9b5z\r\n"
        "Content-Type: %s\r\n"
        "Content-Range: bytes %llu-%llu/%llu\r\n"
        "\r\n",
        (char*)((MimeType == nullptr) ? "application/octet-stream" : MimeType),
        (ullong)Start, (ullong)(End - 1), (ullong)SizeFile
    );
}

int LqHttpRspPrintEndRangeBoundaryHeader(char* Buf, size_t SizeBuf) {
    return LqFwbuf_snprintf(
        Buf,
        SizeBuf,
        "\r\n--z3d6b6a416f9b53e416f9b5z--"
    );
}

/*
* Creates response on range query.
*/
enum LqHttpRspByRngResultEnm {
    LQHTTPRSP_RNG_OK,
    LQHTTPRSP_RNG_HAVE_ERR,
    LQHTTPRSP_RNG_IGNORED //Range header has been ignored
};

static LqHttpRspByRngResultEnm LqHttpRspByRage(LqHttpConn* c, char *RangeHeader, char *RangeHeaderEnd, LqFileSz SizeFile, char* MimeType, char* Etag, char* CacheControl, tm* LastMod, tm* CurrTime, tm* Expires) {
    char* p = RangeHeader;

    //Range: bytes=500-700,601-999\r\n
    for(; *p == ' '; p++);

    for(uint i = 0; "bytes="[i]; i++, p++) {
        if("bytes="[i] != *p)
            return LQHTTPRSP_RNG_IGNORED;
    }
    uint8_t IndexRange = 0;
    LqFileSz FragmentsLen = 0;
    bool IsSatisfiable = false;
    while(true) {
        for(; *p == ' '; p++);
        llong Start = 0;
        ullong End = 0;
        int Readed;
        if((Readed = LqStrToLl(&Start, p, 10)) == 0)
            return LQHTTPRSP_RNG_IGNORED; //Ignore this header
        p += Readed;
        if(*p == '-') {
            p++;
            if(Start < 0) return LQHTTPRSP_RNG_IGNORED;
            if((Readed = LqStrToUll(&End, p, 10)) == 0)
                End = SizeFile;
            else
                End += 1;
            p += Readed;
        } else {
            End = SizeFile;
        }
        if(End > SizeFile)
            End = SizeFile;
        if(Start < 0) {
            Start = (llong)End + Start;
            if(Start < 0)
                Start = 0;
        }
        if(Start >= End)
            IsSatisfiable = true;
        c->Response.Ranges[IndexRange].Start = Start;
        c->Response.Ranges[IndexRange].End = End;
        IndexRange++;
        FragmentsLen += (End - Start);
        for(; *p == ' '; p++);
        if((*p == ',') && (IndexRange < LQHTTPRSP_MAX_RANGES)) {
            p++;
            continue;
        } else if(*p == '\r') {
            break;
        } else {
            return LQHTTPRSP_RNG_IGNORED;
        }
    }

    char* Dest = LqHttpRspHdrResize(c, 4096);
    if(Dest == nullptr) {
        LqHttpActSwitchToClose(c);
        return LQHTTPRSP_RNG_HAVE_ERR;
    }

    auto r = LqHttpGetReg(c);
    if(IsSatisfiable) {
        if(IndexRange > 1)
            return LQHTTPRSP_RNG_IGNORED;
        //// response 416 Requested range not satisfiable
        LqHttpRspStatus(c, 416);
        LqHttpRspHdrAdd(c, "Content-Type", MimeType);
        LqHttpRspHdrAddPrintf(c, "Content-Range", "bytes */%q64u", (uint64_t)SizeFile);
        LqHttpRspBasicHeaders(c, Etag, CacheControl, LastMod, Expires);
        LqHttpActKeepOnlyHeaders(c);
        return LQHTTPRSP_RNG_HAVE_ERR;
    }

    if(IndexRange > 1) {
        char HedersBuf[4096];
        LqFileSz CommonSizeBoundaryHeaders = 0;
        int FirstBoundaryLen = 0;
        CommonSizeBoundaryHeaders += LqHttpRspPrintEndRangeBoundaryHeader(HedersBuf, sizeof(HedersBuf));
        for(int i = IndexRange - 1; i >= 0; i--)
            CommonSizeBoundaryHeaders += (
            FirstBoundaryLen = LqHttpRspPrintRangeBoundaryHeaders
            (
            HedersBuf,
            sizeof(HedersBuf),
            c,
            SizeFile,
            c->Response.Ranges[i].Start,
            c->Response.Ranges[i].End,
            MimeType
            ));

        LqFileSz CommonSize = FragmentsLen + CommonSizeBoundaryHeaders;
        LqHttpRspStatus(c, 206);
        LqHttpRspHdrAdd(c, "Content-Type", "multipart/byteranges; boundary=z3d6b6a416f9b53e416f9b5z");
        LqHttpRspHdrAddPrintf(c, "Content-Length", "%q64u", (uint64_t)CommonSize);
        LqHttpRspBasicHeaders(c, Etag, CacheControl, LastMod, Expires);
        LqHttpRspHdrAddSmallContent(c, HedersBuf, FirstBoundaryLen);
    } else {
        LqHttpRspStatus(c, 206);
        LqHttpRspHdrAdd(c, "Content-Type", (MimeType == nullptr) ? "application/octet-stream" : MimeType);
        LqHttpRspHdrAddPrintf(c, "Content-Length", "%q64u", (uint64_t)((uint64_t)c->Response.Ranges[0].End - (uint64_t)c->Response.Ranges[0].Start));
        LqHttpRspHdrAddPrintf(c, "Content-Range", "bytes %q64u-%q64u/%q64u", (uint64_t)c->Response.Ranges[0].Start, (uint64_t)(c->Response.Ranges[0].End - 1), (uint64_t)SizeFile);
        LqHttpRspBasicHeaders(c, Etag, CacheControl, LastMod, Expires);
    }
    c->Response.CountRanges = IndexRange;
    return LQHTTPRSP_RNG_OK;
}

static bool LqHttpRsp412Headers(LqHttpConn* c, tm* CurrTime) {
    if(LqHttpRspStatus(c, 412)) {
        LqHttpActSwitchToClose(c);
        return false;
    }
    LqHttpActKeepOnlyHeaders(c);
    return true;
}

static int LqHttpRspCheckMatching(char* HdrVal, char* HdrValEnd, char* Etag) {
    char* p = HdrVal;
    char t = 1;
    while(true) {
        for(; *p == ' '; p++);
        if(*p == '*') {
            p++;
        } else {
            //Compare Etag
            for(char* i = Etag; ; p++, i++) {
                if(*p != *i) {
                    if(*i == '\0')
                        return 0;
                    else
                        break;
                }
            }
        }
        for(; *p == ' '; p++);
        if(*p == ',') {
            p++;
            continue;
        } else {
            return 1;
        }
    }
}

static int LqHttpRspCheckTimeModifyng(LqHttpConn* c, char* Hdr, char* HdrEnd, time_t TimeModify) {
    char t = *HdrEnd;
    *HdrEnd = '\0';
    LqTimeSec ReqGmtTime = 0;
    if(
        (LqTimeStrToGmtSec(Hdr, &ReqGmtTime) == -1) ||
        (TimeModify > ReqGmtTime)
        ) {
        *HdrEnd = t;
        return 1;
    }
    *HdrEnd = t;
    return 0;
}

enum LqHttpRspChckPrecondEnm {
    LQHTTPRSP_PRECOND_200,
    LQHTTPRSP_PRECOND_304,
    LQHTTPRSP_PRECOND_206, //Range header has been ignored
    LQHTTPRSP_PRECOND_412
};

static LqHttpRspChckPrecondEnm LqHttpRspCheckPrecondition(LqHttpConn* c, char* Etag, time_t TimeModify, char **RangeHeader, char **RangeHeaderEnd) {
    auto q = &c->Query;
    int r = 0;
    if(Etag[0] == '\0')
        Etag = "\n\n";
    auto IfNoneMatch = -1;
    auto IfMatch = -1;
    auto IfModSince = -1;
    auto IfUnmodSince = -1;
    auto IfRange = -1;
    bool IsHaveCond = false;
    char* RangeHdr = nullptr, *RangeHdrEnd;
    char* HeaderNameResult = nullptr, *HeaderNameResultEnd = nullptr, *HeaderValResult = nullptr, *HeaderValEnd = nullptr;


    while(LqHttpRcvHdrEnum(c, &HeaderNameResult, &HeaderNameResultEnd, &HeaderValResult, &HeaderValEnd) >= 0) {
        LQSTR_SWITCH_NI(HeaderNameResult, HeaderNameResultEnd - HeaderNameResult) {
            LQSTR_CASE_I("if-range") {
                if(IfRange != -1)
                    return LQHTTPRSP_PRECOND_200;
                IfRange = LqHttpRspCheckMatching(HeaderValResult, HeaderValEnd, Etag);
                break;
            }
            LQSTR_CASE_I("if-none-match") {
                if(IfNoneMatch != -1)
                    return LQHTTPRSP_PRECOND_200;
                IfNoneMatch = LqHttpRspCheckMatching(HeaderValResult, HeaderValEnd, Etag);
                IsHaveCond = true;
                break;
            }
            LQSTR_CASE_I("if-match") {
                if(IfMatch != -1)
                    return LQHTTPRSP_PRECOND_200;
                IfMatch = LqHttpRspCheckMatching(HeaderValResult, HeaderValEnd, Etag);
                break;
            }
            LQSTR_CASE_I("if-modified-since") {
                if(IfModSince != -1)
                    return LQHTTPRSP_PRECOND_200;
                IfModSince = LqHttpRspCheckTimeModifyng(c, HeaderValResult, HeaderValEnd, TimeModify);
                IsHaveCond = true;
                break;
            }
            LQSTR_CASE_I("if-unmodified-since") {
                if(IfUnmodSince != -1)
                    return LQHTTPRSP_PRECOND_200;
                IfUnmodSince = LqHttpRspCheckTimeModifyng(c, HeaderValResult, HeaderValEnd, TimeModify);
                break;
            }
            LQSTR_CASE_I("range") {
                if(RangeHdr != nullptr)
                    return LQHTTPRSP_PRECOND_200;
                RangeHdr = HeaderValResult;
                RangeHdrEnd = HeaderValEnd;
                break;
            }
        }
    }
    if((IfMatch == 1) || (IfUnmodSince == 1))
        return LQHTTPRSP_PRECOND_412;
    if(IsHaveCond && (IfNoneMatch < 1) && (IfModSince < 1)) {
        //Return 304
        return LQHTTPRSP_PRECOND_304;
    } else {
        if((RangeHdr != nullptr) && (IfRange < 1)) {
            *RangeHeader = RangeHdr;
            *RangeHeaderEnd = RangeHdrEnd;
            return LQHTTPRSP_PRECOND_206;
        }
    }

    return LQHTTPRSP_PRECOND_200;
}

LQ_EXTERN_C int LQ_CALL LqHttpRspFileAuto(LqHttpConn* c, const char* Path) {
    auto r = LqHttpGetReg(c);
    auto q = &c->Query;
    auto np = c->Pth;
    auto Module = LqHttpMdlGetByPth(np);
    LqFileStat s;
    s.ModifTime = -1;

    LqTimeSec LastModificate = -1;
    LqTimeSec Expires = -1;
    LqFileSz CommonFileSize = 0;
    tm TmCur, TmModif, TmExpires;
    tm* LpTmModif = &TmModif, *LpTmExpires = &TmExpires;
    int Fd = -1;
    char *RangeHdr = nullptr, *RangeHdrEnd = nullptr;
    if(Path == nullptr) {
        switch(np->Type & LQHTTPPTH_TYPE_SEP) {
            case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
                Path = np->RealPath;
                break;
            default:
                LqHttpRspError(c, 404);
                return -1;
        }

    }

    auto CachInter = r->Cache.UpdateAndRead(Path, &s);
    if(CachInter != nullptr) {
        CommonFileSize = CachInter->SizeFile;
    } else {
        if(((Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_BIN, 0)) == -1) || ((s.ModifTime == -1) ? (LqFileGetStatByFd(Fd, &s) != 0) : false)) {
            if(Fd != -1) LqFileClose(Fd);
            LqHttpRspError(c, 404);
            return -1;
        }
        CommonFileSize = s.Size;
    }

    char Etag[1024], CacheControl[1024], Mime[1024];
    Mime[0] = CacheControl[0] = Etag[0] = '\0';

    Module->GetCacheInfoProc(Path, c, CacheControl, sizeof(CacheControl), Etag, sizeof(Etag), &LastModificate, &Expires, (s.ModifTime == -1) ? nullptr : &s);
    Module->GetMimeProc(Path, c, Mime, sizeof(Mime), (s.ModifTime == -1) ? nullptr : &s);


    LqTimeGetGmtTm(&TmCur);

    if(LastModificate != -1) {
        LqTimeLocSecToGmtTm(&TmModif, LastModificate);
        LastModificate = LqTimeLocSecToGmtSec(LastModificate);
    } else {
        LpTmModif = nullptr;
    }
    if(Expires != -1)
        LqTimeLocSecToGmtTm(&TmExpires, Expires);
    else
        LpTmExpires = nullptr;

    LqHttpRspChckPrecondEnm PrecondRes;
    if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_RSP) {
        PrecondRes = LQHTTPRSP_PRECOND_200;
    } else {
        PrecondRes = LqHttpRspCheckPrecondition(c, Etag, LastModificate, &RangeHdr, &RangeHdrEnd);
        LqHttpActSwitchToRsp(c);
    }

    c->Response.Ranges[0].Start = 0;
    c->Response.Ranges[0].End = CommonFileSize;
    c->Response.CountRanges = 1;
    if(CachInter != nullptr) {
        c->Response.CacheInterator = CachInter;
        c->ActionState = LQHTTPACT_STATE_RSP_CACHE;
    } else {
        c->Response.Fd = Fd;
        c->ActionState = LQHTTPACT_STATE_RSP_FD;
    }

    c->ActionResult = LQHTTPACT_RES_BEGIN;
    switch(PrecondRes) {
        case LQHTTPRSP_PRECOND_304:
        {
            if(!LqHttpRsp304Headers(c, Etag, CacheControl, LpTmModif, &TmCur, LpTmExpires))
                return -1;
        }
        break;
        case LQHTTPRSP_PRECOND_206:
        {
            switch(LqHttpRspByRage(c, RangeHdr, RangeHdrEnd, CommonFileSize, Mime, Etag, CacheControl, LpTmModif, &TmCur, LpTmExpires)) {
                case LQHTTPRSP_RNG_IGNORED: goto lblCase200Cache;
                case LQHTTPRSP_RNG_HAVE_ERR:
                {
                    LqHttpActKeepOnlyHeaders(c);
                    return -1;
                }
            }
        }
        break;
        case LQHTTPRSP_PRECOND_412:
            LqHttpRspError(c, 412);
            break;
lblCase200Cache:
        case LQHTTPRSP_PRECOND_200:
        {
            if(!LqHttpRsp200Headers(c, CommonFileSize, Mime, Etag, CacheControl, LpTmModif, &TmCur, LpTmExpires)) {
                LqHttpActKeepOnlyHeaders(c);
                return -1;
            }
            if(c->Flags & LQHTTPCONN_FLAG_NO_BODY) {
                LqHttpActKeepOnlyHeaders(c);
            } else if((c->BufSize - c->Response.HeadersEnd) > CommonFileSize) {
                if(CachInter != nullptr) {
                    if((CommonFileSize == 0) || (LqHttpRspHdrAddSmallContent(c, CachInter->Buf, CommonFileSize) > 0))
                        LqHttpActKeepOnlyHeaders(c);
                } else {
                    if(char * NewDest = LqHttpRspHdrAppendSize(c, CommonFileSize)) {
                        LqFileRead(Fd, NewDest, CommonFileSize);
                        LqHttpActKeepOnlyHeaders(c);
                    }
                }

            }
        }
        break;
    }
    return 0;
}

/*
* Adds a file descriptor to the connection regardless of the previous status. (Primary used by user)
*/
LQ_EXTERN_C LqHttpActResult LQ_CALL LqHttpRspFileByFd(LqHttpConn* c, int Fd, LqFileSz OffsetStart, LqFileSz OffsetEnd) {
    if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER)
        LqHttpActSwitchToRsp(c);
    else
        LqHttpActKeepOnlyHeaders(c);
    c->ActionResult = LQHTTPACT_RES_BEGIN;
    c->ActionState = LQHTTPACT_STATE_RSP_FD;
    c->Response.Fd = Fd;
    c->Response.CountRanges = 1;
    c->Response.Ranges[0].Start = OffsetStart;
    c->Response.Ranges[0].End = OffsetEnd;
    return LQHTTPACT_RES_OK;
}

/*
* Adds a file to the connection regardless of the previous status. (Primary used by user)
*/
LQ_EXTERN_C LqHttpActResult LQ_CALL LqHttpRspFile(LqHttpConn* c, const char* Path, LqFileSz OffsetStart, LqFileSz OffsetEnd) {
    LqFileStat s;
    s.ModifTime = -1;
    if(Path == nullptr) {
        switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP) {
            case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
                Path = c->Pth->RealPath;
                break;
            default:
                return LQHTTPACT_RES_INVALID_TYPE_PATH;
        }
    }

    if(auto CachInter = LqHttpGetReg(c)->Cache.UpdateAndRead(Path, &s)) {
        if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER)
            LqHttpActSwitchToRsp(c);
        else
            LqHttpActKeepOnlyHeaders(c);
        c->ActionState = LQHTTPACT_STATE_RSP_CACHE;
        c->Response.CacheInterator = CachInter;
        c->Response.Ranges[0].End = lq_min(OffsetEnd, CachInter->SizeFile);
    } else {
        int Fd;
        if(((Fd = LqFileOpen(Path, LQ_O_RD | LQ_O_BIN, 0)) == -1) || ((s.ModifTime == -1) ? (LqFileGetStatByFd(Fd, &s) != 0) : false)) {
            if(Fd != -1) LqFileClose(Fd);
            return LQHTTPACT_RES_FILE_NOT_OPEN;
        }
        if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER)
            LqHttpActSwitchToRsp(c);
        else
            LqHttpActKeepOnlyHeaders(c);
        c->ActionState = LQHTTPACT_STATE_RSP_FD;
        c->Response.Fd = Fd;
        c->Response.Ranges[0].End = lq_min(OffsetEnd, s.Size);
    }
    c->Response.CountRanges = 1;
    c->Response.Ranges[0].Start = OffsetStart;

    c->ActionResult = LQHTTPACT_RES_BEGIN;
    return LQHTTPACT_RES_OK;
}

/*
* Adding contents after headers.
* Use for optimizing response.
*/
LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspHdrAddSmallContent(LqHttpConn* c, const void* Content, size_t LenContent) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return -1;
    char* Place = LqHttpRspHdrAppendSize(c, LenContent);
    if(Place == nullptr)
        return 0;
    memcpy(Place, Content, LenContent);
    return LenContent;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspHdrPrintfContent(LqHttpConn* c, const char* FormatStr, ...) {
    char LocalBuf[LQCONN_MAX_LOCAL_SIZE];
    va_list Va;
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return -1;
    va_start(Va, FormatStr);
    auto Written = LqFwbuf_svnprintf(LocalBuf, sizeof(LocalBuf) - 1, FormatStr, Va);
    va_end(Va);
    return LqHttpRspHdrAddSmallContent(c, LocalBuf, Written);
}


LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspContentWrite(LqHttpConn* c, const void* Content, size_t LenContent) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return -1;
    if(c->ActionState != LQHTTPACT_STATE_RSP_STREAM) {
        LqHttpActKeepOnlyHeaders(c);
        LqSbufInit(&c->Response.Stream);
        c->ActionResult = LQHTTPACT_RES_BEGIN;
        c->ActionState = LQHTTPACT_STATE_RSP_STREAM;
    }
    return LqSbufWrite(&c->Response.Stream, Content, LenContent);
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspContentWritePrintf(LqHttpConn* c, const char* FormatStr, ...) {
    va_list Va;
    va_start(Va, FormatStr);
    intptr_t Res = LqHttpRspContentWritePrintfVa(c, FormatStr, Va);
    va_end(Va);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspContentWritePrintfVa(LqHttpConn* c, const char* FormatStr, va_list Va) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return -1;
    char LocalBuf[LQCONN_MAX_LOCAL_SIZE];
    auto Written = LqFwbuf_svnprintf(LocalBuf, sizeof(LocalBuf) - 1, FormatStr, Va);
    return LqHttpRspContentWrite(c, LocalBuf, Written);
}

LQ_EXTERN_C LqFileSz LQ_CALL LqHttpRspContentGetSz(LqHttpConn* c) {
    switch(c->ActionState) {
        case LQHTTPACT_STATE_RSP_STREAM: return c->Response.Stream.Len;
        case LQHTTPACT_STATE_RSP_FD:
        {
            LqFileStat Stat;
            if(LqFileGetStatByFd(c->Response.Fd, &Stat) != 0)
                return 0;
            return Stat.Size;
        }
        case LQHTTPACT_STATE_RSP_CACHE:
            return ((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator)->SizeFile;
    }
    return 0;
}

/* Insert hedder in @DestOffset */
static char* LqHttpRspHdrInsertInOffset(LqHttpConn* c, size_t DestOffset, const char* HeaderName, size_t HeaderNameLen, const char* HeaderVal, size_t HeaderValLen) {
    size_t NewHeaderLen = HeaderNameLen + HeaderValLen + 4;
    auto OldHeadersEnd = c->Response.HeadersEnd;
    if(LqHttpRspHdrAppendSize(c, NewHeaderLen) == nullptr)
        return nullptr;
    auto Headers = c->Buf + (c->Response.HeadersStart + DestOffset);
    char* Ret = Headers;
    memmove(Headers + NewHeaderLen, Headers, OldHeadersEnd - DestOffset);
    memcpy(Headers, HeaderName, HeaderNameLen);
    Headers += HeaderNameLen;
    memcpy(Headers, ": ", 2);
    Headers += 2;
    memcpy(Headers, HeaderVal, HeaderValLen);
    Headers += HeaderValLen;
    memcpy(Headers, "\r\n", 2);
    return Ret;
}

static char* LqHttpRspHdrInsertEx(LqHttpConn* c, size_t DestOffset, size_t DestOffsetEnd, const char* HeaderName, size_t HeaderNameLen, const char* HeaderVal, size_t HeaderValLen) {
    size_t NewHeaderLen = HeaderNameLen + HeaderValLen + 4;
    size_t NewEnd = DestOffset + NewHeaderLen;
    auto OldHeadersEnd = c->Response.HeadersEnd;
    char* Headers = c->Buf + c->Response.HeadersStart;
    if(NewEnd > DestOffsetEnd) {
        if(LqHttpRspHdrAppendSize(c, NewEnd - DestOffsetEnd) == nullptr)
            return nullptr;
        memmove(Headers + NewEnd, Headers + DestOffsetEnd, OldHeadersEnd - DestOffsetEnd);
    } else {
        memmove(Headers + NewEnd, Headers + DestOffsetEnd, OldHeadersEnd - DestOffsetEnd);
        LqHttpRspHdrSizeDecrease(c, DestOffsetEnd - NewEnd);
    }
    Headers = c->Buf + (c->Response.HeadersStart + DestOffset);
    char* Ret = Headers;
    memcpy(Headers, HeaderName, HeaderNameLen);
    Headers += HeaderNameLen;
    memcpy(Headers, ": ", 2);
    Headers += 2;
    memcpy(Headers, HeaderVal, HeaderValLen);
    Headers += HeaderValLen;
    memcpy(Headers, "\r\n", 2);
    return Ret;
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrInsertStrEx(LqHttpConn* c, size_t DestOffset, size_t DestOffsetEnd, const char* Val, size_t ValLen) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    size_t NewEnd = DestOffset + ValLen;
    char* Headers = c->Buf + c->Response.HeadersStart;
    if(NewEnd > DestOffsetEnd) {
        auto OldHeadersEnd = c->Response.HeadersEnd;
        if(LqHttpRspHdrAppendSize(c, NewEnd - DestOffsetEnd) == nullptr)
            return nullptr;
        memmove(Headers + NewEnd, Headers + DestOffsetEnd, OldHeadersEnd - DestOffsetEnd);
    } else {
        memmove(Headers + NewEnd, Headers + DestOffsetEnd, c->Response.HeadersEnd - DestOffsetEnd);
        LqHttpRspHdrSizeDecrease(c, DestOffsetEnd - NewEnd);
    }
    Headers = c->Buf + (c->Response.HeadersStart + DestOffset);
    memcpy(Headers, Val, ValLen);
    return Headers;
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrInsertStr(LqHttpConn* c, size_t DestOffset, size_t DestOffsetEnd, const char* Val) {
    return LqHttpRspHdrInsertStrEx(c, DestOffset, DestOffsetEnd, Val, LqStrLen(Val));
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspHdrSearchEx(const LqHttpConn* c, const char* HeaderName, size_t HeaderNameLen, char** HeaderNameResult, char** HeaderValResult, char** HeaderValEnd) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return -1;
    size_t SizeHeaders = c->Response.HeadersEnd - c->Response.HeadersStart;
    if(SizeHeaders == 0) return -1;
    char* Headers = c->Buf + c->Response.HeadersStart, *i = Headers, t = c->Buf[c->Response.HeadersEnd - 1];
    c->Buf[c->Response.HeadersEnd - 1] = '\0';
    if((i[0] == 'H') && (i[1] == 'T') && (i[2] == 'T') && (i[3] == 'P') && (i[4] == '/'))
        i += 4;
    else
        goto lblCheckName;

    for(; *i != '\0'; i++) {
        if((*i == '\r') && (i[1] == '\n')) {
            i += 2;
            if((*i == '\r') && (i[1] == '\n')) {
                c->Buf[c->Response.HeadersEnd - 1] = t;
                return -1;
            } else {
lblCheckName:
                for(; *i == ' '; i++);
                if(LqStrUtf8CmpCaseLen(HeaderName, i, HeaderNameLen) && ((i[HeaderNameLen] == ':') || (i[HeaderNameLen] == ' '))) {
                    intptr_t Res = i - Headers;
                    if(HeaderNameResult != nullptr) *HeaderNameResult = i;
                    i += HeaderNameLen;
                    if(HeaderValResult != nullptr) {
                        for(; ((*i == ' ') || (*i == ':')) && (*i != '0'); i++);
                        *HeaderValResult = i;
                    }
                    if(HeaderValEnd != nullptr) {
                        for(; !((*i == '\r') && (i[1] == '\n')) && (*i != '\0'); i++);
                        *HeaderValEnd = i;
                    }
                    c->Buf[c->Response.HeadersEnd - 1] = t;
                    return Res;
                }
            }
        }
    }
    c->Buf[c->Response.HeadersEnd - 1] = t;
    return -1;
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspHdrSearch(const LqHttpConn* c, const char* HeaderName, char** HeaderNameResult, char** HeaderValResult, char** HederValEnd) {
    return LqHttpRspHdrSearchEx(c, HeaderName, LqStrLen(HeaderName), HeaderNameResult, HeaderValResult, HederValEnd);
}

LQ_EXTERN_C intptr_t LQ_CALL LqHttpRspHdrEnum(const LqHttpConn* c, char** HeaderNameResult, char** HeaderNameResultEnd, char** HeaderValResult, char** HeaderValEnd) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return -1;
    return LqHttpConnHdrEnm(true, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart, HeaderNameResult, HeaderNameResultEnd, HeaderValResult, HeaderValEnd);
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrAddEx(LqHttpConn* c, const char* HeaderName, size_t HeaderNameLen, const char* HeaderVal, size_t HeaderValLen) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    size_t SizeHeaders = c->Response.HeadersEnd - c->Response.HeadersStart;
    char* Headers = c->Buf + c->Response.HeadersStart;
    size_t DestOffset = 0;
    for(intptr_t i = 0, m = SizeHeaders - 1; i < m; i++) {
        if((Headers[i] == '\r') && (Headers[i + 1] == '\n')) {
            i += 2;
            DestOffset = i;
            if((i >= m) || ((Headers[i] == '\r') && (Headers[i + 1] == '\n')))
                break;
        }
    }
    return LqHttpRspHdrInsertInOffset(c, DestOffset, HeaderName, HeaderNameLen, HeaderVal, HeaderValLen);
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrAdd(LqHttpConn* c, const char* HeaderName, const char* HeaderVal) {
    return LqHttpRspHdrAddEx(c, HeaderName, LqStrLen(HeaderName), HeaderVal, LqStrLen(HeaderVal));
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrAddPrintfVa(LqHttpConn* c, const char* HeaderName, const char* FormatStr, va_list Va) {
    char LocalBuf[LQCONN_MAX_LOCAL_SIZE];
    auto Written = LqFwbuf_svnprintf(LocalBuf, sizeof(LocalBuf) - 1, FormatStr, Va);
    if(Written >= (sizeof(LocalBuf) - 2))
        return nullptr;
    return LqHttpRspHdrAddEx(c, HeaderName, LqStrLen(HeaderName), LocalBuf, Written);
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrAddPrintf(LqHttpConn* c, const char* HeaderName, const char* FormatStr, ...) {
    va_list Va;
    va_start(Va, FormatStr);
    char* Result = LqHttpRspHdrAddPrintfVa(c, HeaderName, FormatStr, Va);
    va_end(Va);
    return Result;
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrChangeEx(LqHttpConn* c, const char* HeaderName, size_t HeaderNameLen, const char* HeaderVal, size_t HeaderValLen) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    size_t SizeHeaders = c->Response.HeadersEnd - c->Response.HeadersStart;

    if(SizeHeaders == 0)
        return LqHttpRspHdrInsertEx(c, 0, 0, HeaderName, HeaderNameLen, HeaderVal, HeaderValLen);
    char* Headers = c->Buf + c->Response.HeadersStart, *i = Headers, t = c->Buf[c->Response.HeadersEnd - 1];
    c->Buf[c->Response.HeadersEnd - 1] = '\0';
    size_t LastOffset = 0, FoundedOffset, FoundedOffsetEnd;
    if((i[0] == 'H') && (i[1] == 'T') && (i[2] == 'T') && (i[3] == 'P') && (i[4] == '/'))
        i += 4;
    else
        goto lblCheckName;

    for(; *i != '\0'; i++) {
        if((*i == '\r') && (i[1] == '\n')) {
            i += 2;
            LastOffset = i - Headers;
            if((*i == '\r') && (i[1] == '\n')) {
                c->Buf[c->Response.HeadersEnd - 1] = t;
                return LqHttpRspHdrInsertInOffset(c, i - Headers, HeaderName, HeaderNameLen, HeaderVal, HeaderValLen);
            } else {
lblCheckName:
                for(; *i == ' '; i++);
                if(LqStrUtf8CmpCaseLen(HeaderName, i, HeaderNameLen) && ((i[HeaderNameLen] == ':') || (i[HeaderNameLen] == ' '))) {
                    FoundedOffset = i - Headers;
                    for(;; i++)
                        if(*i == '\0') {
                            i++;
                            break;
                        } else if((*i == '\r') && (i[1] == '\n')) {
                            i += 2;
                            break;
                        }
                        FoundedOffsetEnd = i - Headers;
                        c->Buf[c->Response.HeadersEnd - 1] = t;
                        return LqHttpRspHdrInsertEx(c, FoundedOffset, FoundedOffsetEnd, HeaderName, HeaderNameLen, HeaderVal, HeaderValLen);
                }
            }
        }
    }
    c->Buf[c->Response.HeadersEnd - 1] = t;
    return LqHttpRspHdrInsertEx(c, LastOffset, LastOffset, HeaderName, HeaderNameLen, HeaderVal, HeaderValLen);
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrChange(LqHttpConn* c, const char* HeaderName, const char* HeaderVal) {
    return LqHttpRspHdrChangeEx(c, HeaderName, LqStrLen(HeaderName), HeaderVal, LqStrLen(HeaderVal));
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrAddStartLine(LqHttpConn* c, uint16_t StatusCode) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    char Buf[1024];
    auto Written = LqFwbuf_snprintf(Buf, sizeof(Buf) - 1, "HTTP/%s %q16u %s\r\n", LqHttpProtoGetByConn(c)->HTTPProtoVer, StatusCode, LqHttpPrsGetMsgByStatus(StatusCode));
    if(auto Res = LqHttpRspHdrInsertStrEx(c, 0, 0, Buf, Written)) {
        c->Response.Status = StatusCode;
        return Res;
    }
    return nullptr;
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrSetEnd(LqHttpConn* c) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    size_t SizeHeaders = c->Response.HeadersEnd - c->Response.HeadersStart;
    char* Headers = c->Buf + c->Response.HeadersStart;
    size_t DestOffset = 0;
    for(intptr_t i = 0, m = SizeHeaders - 1; i < m; i++) {
        if((Headers[i] == '\r') && (Headers[i + 1] == '\n')) {
            i += 2;
            DestOffset = i;
            if(i >= m)
                break;
            else if((Headers[i] == '\r') && (Headers[i + 1] == '\n'))
                return Headers + i;
        }
    }
    return LqHttpRspHdrInsertStrEx(c, DestOffset, DestOffset, "\r\n", 2);
}

LQ_EXTERN_C bool LQ_CALL LqHttpRspHdrRemoveEx(LqHttpConn* c, const char* HeaderName, size_t HeaderNameLen) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return false;
    size_t SizeHeaders = c->Response.HeadersEnd - c->Response.HeadersStart;
    if(SizeHeaders == 0) return false;
    char* Headers = c->Buf + c->Response.HeadersStart, *i = Headers, t = c->Buf[c->Response.HeadersEnd - 1];
    c->Buf[c->Response.HeadersEnd - 1] = '\0';
    if((i[0] == 'H') && (i[1] == 'T') && (i[2] == 'T') && (i[3] == 'P') && (i[4] == '/'))
        i += 4;
    else
        goto lblCheckName;

    for(; *i != '\0'; i++) {
        if((*i == '\r') && (i[1] == '\n')) {
            i += 2;
            if((*i == '\r') && (i[1] == '\n')) {
                c->Buf[c->Response.HeadersEnd - 1] = t;
                return false;
            } else {
lblCheckName:
                for(; *i == ' '; i++);
                if(LqStrUtf8CmpCaseLen(HeaderName, i, HeaderNameLen) && ((i[HeaderNameLen] == ':') || (i[HeaderNameLen] == ' '))) {
                    size_t FoundedOffset = i - Headers;
                    for(;; i++) {
                        if(*i == '\0') {
                            i++;
                            break;
                        } else if((*i == '\r') && (i[1] == '\n')) {
                            i += 2;
                            break;
                        }
                    }
                    size_t FoundedOffsetEnd = i - Headers;
                    c->Buf[c->Response.HeadersEnd - 1] = t;
                    return LqHttpRspHdrInsertStrEx(c, FoundedOffset, FoundedOffsetEnd, "", 0) != nullptr;
                }
            }
        }
    }
    c->Buf[c->Response.HeadersEnd - 1] = t;
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqHttpRspHdrRemove(LqHttpConn* c, const char* HeaderName) {
    return LqHttpRspHdrRemoveEx(c, HeaderName, LqStrLen(HeaderName));
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrAppendSize(LqHttpConn* c, size_t Size) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    size_t NewLen = c->Response.HeadersEnd + Size;
    if(c->BufSize < NewLen) {
        if(!LqHttpConnBufferRealloc(c, NewLen))
            return nullptr;
    }
    auto t = c->Response.HeadersEnd;
    c->Response.HeadersEnd = NewLen;
    return c->Buf + t;
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrResize(LqHttpConn* c, size_t Size) {
    if(LqHttpActGetClassByConn(c) != LQHTTPACT_CLASS_RSP)
        return nullptr;
    size_t NewLen = c->Response.HeadersStart + Size;
    if(NewLen > c->BufSize) {
        if(!LqHttpConnBufferRealloc(c, NewLen))
            return nullptr;
    }
    c->Response.HeadersEnd = NewLen;
    return c->Buf + c->Response.HeadersStart;
}

LQ_EXTERN_C char* LQ_CALL LqHttpRspHdrSizeDecrease(LqHttpConn* c, size_t CountDecreese) {
    c->Response.HeadersEnd -= CountDecreese;
    return c->Buf + c->Response.HeadersStart;
}

LQ_EXTERN_C bool LQ_CALL LqHttpRspGetFileMd5(LqHttpConn* c, const char* FileName, LqMd5* DestMd5) {
    if(FileName == nullptr) {
        if((c->ActionState == LQHTTPACT_STATE_RSP_CACHE) && (c->Response.CacheInterator != nullptr)) {
            ((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator)->GetMD5(c->Response.CacheInterator, DestMd5);
            return true;
        }
        switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP) {
            case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
                FileName = c->Pth->RealPath;
                break;
            default:
                return false;
        }
    }
    if(auto CachInter = LqHttpGetReg(c)->Cache.UpdateAndRead(FileName)) {
        CachInter->GetMD5(CachInter, DestMd5);
        LqHttpGetReg(c)->Cache.Release(CachInter);
    } else {
        int Fd;
        if(((Fd = LqFileOpen(FileName, LQ_O_RD | LQ_O_BIN, 0)) == -1))
            return false;
        char Buf[LQCONN_MAX_LOCAL_SIZE];
        LqMd5Ctx ctx;
        LqMd5Init(&ctx);
        while(true) {
            auto Count = LqFileRead(Fd, Buf, sizeof(Buf));
            if(Count < 0)
                break;
            LqMd5Update(&ctx, Buf, Count);
            if(Count < sizeof(Buf))
                break;
        }
        LqMd5Final((uchar*)DestMd5, &ctx);
    }
    return true;
}


LQ_EXTERN_C bool LQ_CALL LqHttpRspGetSbufMd5(LqHttpConn* c, LqSbuf* StreamBuf, LqMd5* DestMd5) {
    char Buf[LQCONN_MAX_LOCAL_SIZE];
    LqMd5Ctx ctx;
    LqSbufPtr Ptr;

    if(StreamBuf == nullptr) {
        if(c->ActionState == LQHTTPACT_STATE_RSP_STREAM)
            StreamBuf = &c->Response.Stream;
        else
            return false;
    }
    LqMd5Init(&ctx);
    LqSbufPtrSet(StreamBuf, &Ptr);
    while(true) {
        auto Count = LqSbufReadByPtr(&Ptr, Buf, sizeof(Buf));
        if(Count < 0)
            break;
        LqMd5Update(&ctx, Buf, Count);
        if(Count < sizeof(Buf))
            break;
    }
    LqMd5Final((uchar*)DestMd5, &ctx);
    return true;
}
