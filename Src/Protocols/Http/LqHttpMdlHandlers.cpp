/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpMdlHandlers... - Default module handlers.
*/

#include "LqConn.h"
#include "LqOs.h"
#include "LqFile.h"
#include "LqHttpMdlHandlers.h"
#include "LqStrSwitch.h"
#include "LqHttpPth.hpp"
#include "LqHttp.hpp"
#include "LqHttpPrs.h"
#include "LqHttpRsp.h"
#include "LqHttpRcv.h"
#include "LqHttpAct.h"
#include "LqHttpConn.h"
#include "LqHttpMdl.h"
#include "LqHttpAtz.h"
#include "LqAtm.hpp"
#include "LqDfltRef.hpp"
#include "LqTime.h"


int LQ_CALL LqHttpMdlHandlersStatus(LqHttpConn* c, int Code)
{
    const char* StatusMsg = LqHttpPrsGetMsgByStatus(Code);
    if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER)
        LqHttpActSwitchToRsp(c);
    if(char* Dest = LqHttpRspHdrResize(c, 4096))
    {
        tm ctm;
        LqTimeGetGmtTm(&ctm);
        char ServerNameBuf[1024];
        ServerNameBuf[0] = '\0';
        LqHttpMdlGetByConn(c)->ServerNameProc(c, ServerNameBuf, sizeof(ServerNameBuf));
        auto l = snprintf
        (
            Dest,
            c->BufSize,
            "HTTP/%s %u %s\r\n"
            "Connection: %s\r\n"
            "Accept-Ranges: bytes\r\n"
            "Date: " PRINTF_TIME_TM_FORMAT_GMT "\r\n"
            "%s%s%s"
            "\r\n",
            LqHttpGetReg(c)->Base.HTTPProtoVer, Code, StatusMsg,
            (char*)((c->Flags & LQHTTPCONN_FLAG_CLOSE) ? "close" : "Keep-Alive"),
            PRINTF_TIME_TM_ARG_GMT(ctm),
            ((ServerNameBuf[0] == '\0') ? "" : "Server: "), (char*)ServerNameBuf, ((ServerNameBuf[0] == '\0') ? "" : "\r\n")
        );
        LqHttpRspHdrResize(c, l);
        if((Code == 501) || (Code == 405))
        {
            char AllowBuf[1024];
            AllowBuf[0] = '\0';
            LqHttpMdlGetByConn(c)->AllowProc(c, AllowBuf, sizeof(AllowBuf) - 1);
            if(AllowBuf[0] != '\0')
                LqHttpRspHdrAdd(c, "Allow", AllowBuf);
        }
        c->Response.Status = Code;
        return 0;
    }
    return -1;
}


int LQ_CALL LqHttpMdlHandlersError(LqHttpConn* c, int Code)
{
    const char* StatusMsg = LqHttpPrsGetMsgByStatus(Code);
    LqHttpActSwitchToRsp(c);
    if(char* Dest = LqHttpRspHdrResize(c, 4096))
    {
        tm ctm;
        LqTimeGetGmtTm(&ctm);
        char ServerNameBuf[1024], BodyBuf[1024];
        ServerNameBuf[0] = '\0';
        auto ContentLen = snprintf(BodyBuf, sizeof(BodyBuf) - 1, "<html><head></head><body>%u %s</body></html>", Code, StatusMsg);
        LqHttpMdlGetByConn(c)->ServerNameProc(c, ServerNameBuf, sizeof(ServerNameBuf));
        auto l = snprintf
        (
            Dest,
            c->BufSize,
            "HTTP/%s %u %s\r\n"
            "Content-Length: %u\r\n"
            "Content-Type: text/html; charset=\"UTF-8\"\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: %s\r\n"
            "Date: " PRINTF_TIME_TM_FORMAT_GMT "\r\n"
            "%s%s%s"
            "\r\n",
            LqHttpGetReg(c)->Base.HTTPProtoVer, Code, StatusMsg,
            ContentLen,
            (char*)((c->Flags & LQHTTPCONN_FLAG_CLOSE) ? "close" : "Keep-Alive"),
            PRINTF_TIME_TM_ARG_GMT(ctm),
            ((ServerNameBuf[0] == '\0') ? "" : "Server: "), (char*)ServerNameBuf, ((ServerNameBuf[0] == '\0') ? "" : "\r\n")
        );
        LqHttpRspHdrResize(c, l);

        if((Code == 501) || (Code == 405))
        {
            char AllowBuf[1024];
            AllowBuf[0] = '\0';
            LqHttpMdlGetByConn(c)->AllowProc(c, AllowBuf, sizeof(AllowBuf) - 1);
            if(AllowBuf[0] != '\0')
                LqHttpRspHdrAdd(c, "Allow", AllowBuf);
        }

        if(!(c->Flags & LQHTTPCONN_FLAG_NO_BODY))
            LqHttpRspHdrAddSmallContent(c, BodyBuf, ContentLen);
        c->Response.Status = Code;
        return 0;
    }
    return -1;
}

void LQ_CALL LqHttpMdlHandlersResponseRedirection(LqHttpConn* c)
{
    if((c->Pth != nullptr) && ((c->Pth->Type & LQHTTPPTH_TYPE_SEP) == LQHTTPPTH_TYPE_FILE_REDIRECTION))
    {
        LqHttpRspError(c, c->Pth->StatusCode);
        if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_RSP)
        {
            LqHttpRspHdrChange(c, "Location", c->Pth->Location);
        }
    } else
    {
        LqHttpRspError(c, 500);
    }
    LqHttpEvntActSetIgnore(c);
}

void LQ_CALL LqHttpMdlHandlersServerName(LqHttpConn* c, char* NameBuf, size_t NameBufSize)
{
    auto Reg = LqHttpProtoGetByConn(c);
    LqAtmLkRd(Reg->ServNameLocker);
    LqStrCopyMax(NameBuf, Reg->ServName, NameBufSize);
    LqAtmUlkRd(Reg->ServNameLocker);
}

void LQ_CALL LqHttpMdlHandlersAllowMethods(LqHttpConn* c, char* MethodBuf, size_t MethodBufSize)
{
    LqStrCopyMax(MethodBuf, "GET, POST, PUT, HEAD, OPTIONS", MethodBufSize);
}


LqHttpEvntHandlerFn LQ_CALL LqHttpMdlHandlersGetMethod(LqHttpConn* c)
{
    if(c->Pth != nullptr)
        switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP)
        {
            case LQHTTPPTH_TYPE_FILE_REDIRECTION:
                return (!LqHttpAtzDo(c, LQHTTPATZ_PERM_CHECK)) ? LqHttpMdlHandlersEmpty : LqHttpMdlGetByConn(c)->ResponseRedirectionProc;
        }
    LQSTR_SWITCH_NI(c->Query.Method, c->Query.MethodLen)
    {
        LQSTR_CASE_I("delete")
        {
            if(c->Pth == nullptr)
            {
                LqHttpRspError(c, 404);
                return LqHttpMdlHandlersEmpty;
            }
            if(!LqHttpAtzDo(c, LQHTTPATZ_PERM_DELETE))
                return LqHttpMdlHandlersEmpty;

            switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP)
            {
                case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
                    if(LqFileRemove(c->Pth->RealPath) == -1)
                    {
                        LqHttpRspError(c, 500);
                    } else
                    {
                        LqHttpRspError(c, 200);
                    }
                    return LqHttpEvntDfltIgnoreAnotherEventHandler;
                case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
                    return c->Pth->ExecQueryProc;
            }
        }
        break;
        LQSTR_CASE_I("head")
        {
            c->Flags |= LQHTTPCONN_FLAG_NO_BODY;
        }
        LQSTR_CASE_I("get")
        {
            if(c->Pth == nullptr)
            {
                LqHttpRspError(c, 404);
                return LqHttpMdlHandlersEmpty;
            }
            if(!LqHttpAtzDo(c, LQHTTPATZ_PERM_READ))
                return LqHttpMdlHandlersEmpty;
            switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP)
            {
                case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
                    LqHttpRspFileAuto(c, nullptr);
                    return LqHttpEvntDfltIgnoreAnotherEventHandler;
                case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
                    return c->Pth->ExecQueryProc;
            }
        }
        break;
        LQSTR_CASE_I("post")
        LQSTR_CASE_I("put")
        {
            if(c->Pth == nullptr)
            {
                LqHttpRspError(c, 404);
                return LqHttpMdlHandlersEmpty;
            }
            auto q = &c->Query;
            auto np = c->Pth;
            uint8_t Authoriz = 0;
            switch(np->Type & LQHTTPPTH_TYPE_SEP)
            {
                case LQHTTPPTH_TYPE_DIR:
                case LQHTTPPTH_TYPE_FILE:
                {
                    if(q->ContentLen > 0)
                        Authoriz |= LQHTTPATZ_PERM_WRITE;
                    if(LqFileGetStat(np->RealPath, LqDfltPtr()) == 0)
                    {
                        Authoriz |= LQHTTPATZ_PERM_MODIFY;
                    } else
                    {
                        Authoriz |= LQHTTPATZ_PERM_CREATE;

                        int l = LqStrLen(np->RealPath);
                        for(; l >= 0; l--)
                        {
                            if(np->RealPath[l] == LQ_PATH_SEPARATOR)
                                break;
                        }
                        char UpDirPath[LQ_MAX_PATH];
                        if(l > sizeof(UpDirPath))
                            break;
                        memcpy(UpDirPath, np->RealPath, l);
                        char t = UpDirPath[l];
                        UpDirPath[l] = '\0';
                        LqFileStat Stat;
                        if(LqFileGetStat(UpDirPath, &Stat) != 0)
                        {
                            Authoriz |= LQHTTPATZ_PERM_CREATE_SUBDIR;
                        }
                    }
                }
                break;
                case LQHTTPPTH_TYPE_EXEC_DIR:
                case LQHTTPPTH_TYPE_EXEC_FILE:
                    Authoriz |= LQHTTPATZ_PERM_WRITE;
                    break;
            }
            if(!LqHttpAtzDo(c, Authoriz))
                return LqHttpMdlHandlersEmpty;
            switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP)
            {
                case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
                    return c->Pth->ExecQueryProc;
                case LQHTTPPTH_TYPE_DIR:
                    LqHttpRspError(c, 500);
                    return LqHttpMdlHandlersEmpty;
            }
            switch(LqHttpRcvFile(c, nullptr, LQ_MAX_CONTENT_LEN, 0666, true, Authoriz & LQHTTPATZ_PERM_CREATE_SUBDIR))
            {
                case LQHTTPRCV_ERR:
                    LqHttpRspError(c, 500);
                    return LqHttpMdlHandlersEmpty;
                case LQHTTPRCV_MULTIPART:
                    LqHttpRspError(c, 501);
                    return LqHttpMdlHandlersEmpty;
                case LQHTTPRCV_FILE_OK:
                    return LqHttpEvntDfltIgnoreAnotherEventHandler;
            }
        }
        break;
        LQSTR_CASE_I("options")
        {
            if(c->Pth != nullptr)
            {
                switch(c->Pth->Type & LQHTTPPTH_TYPE_SEP)
                {
                    case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
                        return c->Pth->ExecQueryProc;
                }
            }
            LqHttpRspError(c, 200);
            if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_RSP)
            {
                char AllowBuf[1024];
                AllowBuf[0] = '\0';
                LqHttpMdlGetByConn(c)->AllowProc(c, AllowBuf, sizeof(AllowBuf) - 1);
                if(AllowBuf[0] != '\0')
                    LqHttpRspHdrAdd(c, "Allow", AllowBuf);
            }
            return LqHttpMdlHandlersEmpty;
        }
        break;
    }
    return nullptr;
}

void LQ_CALL LqHttpMdlHandlersEmpty(LqHttpConn*) {}

enum
{
    MTagLen = sizeof(LqTimeSec) * 2 + sizeof(LqFileSz) * 2 + sizeof(unsigned int) * 2 + 4
};

void LQ_CALL LqHttpMdlHandlersCacheInfo
(
    const char* Path,
    LqHttpConn* Connection,

    char* CacheControlDestBuf, /*If after call CacheControlDestBuf == "", then Cache-Control no include in response headers.*/
    size_t CacheControlDestBufSize,

    char* EtagDestBuf, /*If after call EtagDestBuf == "", then Etag no include in response headers.*/
    size_t EtagDestBufSize,

    LqTimeSec* LastModif, /*Local time. If after call LastModif == -1, then then no response Last-Modified.*/

    LqTimeSec* Expires,

    LqFileStat const* Stat /*(Something sends for optimizing)*/
)
{
    LqFileStat st;
    if(Stat == nullptr)
    {
        if(LqFileGetStat(Path, &st) != 0) return;
        Stat = &st;
    }
    if((EtagDestBuf != nullptr) && ((MTagLen + 1) < EtagDestBufSize))
    {
        if(Stat->Type == LQ_F_REG)
        {
            static const char Hex[] = "0123456789abcdef";
            char* s = EtagDestBuf;
            *s++ = '\"';
            {
                auto t = Stat->Id;
                for(int i = 0; i < (sizeof(t) * 2); i++, t >>= 4)
                    *s++ = Hex[t & 0xf];
            }
            *s++ = '-';
            {
                auto t = Stat->Size;
                for(int i = 0; i < (sizeof(t) * 2); i++, t >>= 4)
                    *s++ = Hex[t & 0xf];
            }
            *s++ = '-';
            {
                auto t = Stat->ModifTime;
                for(int i = 0; i < (sizeof(t) * 2); i++, t >>= 4)
                    *s++ = Hex[t & 0xf];
            }
            *s++ = '\"';
            *s++ = '\0';
        }
    }
    if(LastModif != nullptr)
        *LastModif = Stat->ModifTime;
}



void LQ_CALL LqHttpMdlHandlersMime(const char* Path, LqHttpConn* Connection, char* MimeDestBuf, size_t MimeDestBufLen, LqFileStat const* Stat)
{
    if(MimeDestBuf == nullptr)
        return;
    const char* f, *e;
    if(((f = strrchr(Path, LQ_PATH_SEPARATOR)) == nullptr) || ((e = strrchr(f + 1, '.')) == nullptr) || (e == (f + 1)))
        e = "";
    else
        e++;
    auto r = LqHttpMimeExtension(e);
    auto l = LqStrLen(r->Mime[0]) + 1;
    if(l > MimeDestBufLen)
        return;
    memcpy(MimeDestBuf, r->Mime[0], l);
}

#define MIME_RET(...) \
		{\
			static const char* Mime[] = {__VA_ARGS__};

#define EXTENSION_RET(...) \
			static const char* Ext[] = {__VA_ARGS__};\
			static const LqHttpExtensionMime h = \
			{\
				sizeof(Ext) / sizeof(Ext[0]),\
				(char**)&Ext,\
				sizeof(Mime) / sizeof(Mime[0]),\
				(char**)&Mime,\
			};\
			return &h;\
		}
#define EXTENSION_RET_OCT_STREAM \
			static const char* Ext[] = {""};\
			static const LqHttpExtensionMime h = \
			{\
				0,\
				(char**)&Ext,\
				sizeof(Mime) / sizeof(Mime[0]),\
				(char**)&Mime,\
			};\
			return &h;\
		}

const LqHttpExtensionMime* LqHttpMimeExtension(const char* Str)
{
    LQSTR_SWITCH_I(Str)
    {
        LQSTR_CASE_I("html");
        LQSTR_CASE_I("htm");
        LQSTR_CASE_I("shtml");
        LQSTR_CASE_I("text/html");
            MIME_RET("text/html")EXTENSION_RET("shtml", "htm", "shtml");
        LQSTR_CASE_I("css");
        LQSTR_CASE_I("text/css")
            MIME_RET("text/css")EXTENSION_RET("css");
        LQSTR_CASE_I("xml");
        LQSTR_CASE_I("text/xml");
            MIME_RET("text/xml")EXTENSION_RET("xml");
        LQSTR_CASE_I("mml");
        LQSTR_CASE_I("text/mathml");
            MIME_RET("text/mathml")EXTENSION_RET("mml");
        LQSTR_CASE_I("jad");
        LQSTR_CASE_I("text/vnd.sun.j2me.app-descriptor");
            MIME_RET("text/vnd.sun.j2me.app-descriptor")EXTENSION_RET("jad");
        LQSTR_CASE_I("wml");
        LQSTR_CASE_I("text/vnd.wap.wml");
            MIME_RET("text/vnd.wap.wml")EXTENSION_RET("wml");
        LQSTR_CASE_I("htc");
        LQSTR_CASE_I("text/x-component");
            MIME_RET("text/x-component")EXTENSION_RET("htc");
        LQSTR_CASE_I("c");
        LQSTR_CASE_I("text/x-csrc");
            MIME_RET("text/x-csrc")EXTENSION_RET("c");
        LQSTR_CASE_I("h");
        LQSTR_CASE_I("text/x-chdr");
            MIME_RET("text/x-chdr")EXTENSION_RET("h");
        LQSTR_CASE_I("o");
        LQSTR_CASE_I("ko");
        LQSTR_CASE_I("text/x-object");
            MIME_RET("text/x-object")EXTENSION_RET("ko", "o");
        LQSTR_CASE_I("log");
        LQSTR_CASE_I("cfg");
        LQSTR_CASE_I("conf");
        LQSTR_CASE_I("txt");
        LQSTR_CASE_I("text/plain");
            MIME_RET("text/plain")EXTENSION_RET("log", "cfg", "conf", "txt");
        LQSTR_CASE_I("patch")
        LQSTR_CASE_I("diff");
        LQSTR_CASE_I("text/x-patch");
            MIME_RET("text/x-patch")EXTENSION_RET("diff", "patch");
        LQSTR_CASE_I("jpeg");
        LQSTR_CASE_I("jpg");
        LQSTR_CASE_I("image/jpeg");
            MIME_RET("image/jpeg")EXTENSION_RET("jpg", "jpeg");
        LQSTR_CASE_I("svg");
        LQSTR_CASE_I("svgz");
        LQSTR_CASE_I("image/svg+xml");
            MIME_RET("image/svg+xml")EXTENSION_RET("svgz", "svg");
        LQSTR_CASE_I("tif");
        LQSTR_CASE_I("tiff");
        LQSTR_CASE_I("image/tiff");
            MIME_RET("image/tiff")EXTENSION_RET("tiff", "tif");
        LQSTR_CASE_I("gif");
        LQSTR_CASE_I("image/gif");
            MIME_RET("image/gif")EXTENSION_RET("gif");
        LQSTR_CASE_I("png");
        LQSTR_CASE_I("image/png");
            MIME_RET("image/png")EXTENSION_RET("png");
        LQSTR_CASE_I("wbmp");
        LQSTR_CASE_I("image/vnd.wap.wbmp");
            MIME_RET("image/vnd.wap.wbmp")EXTENSION_RET("wbmp");
        LQSTR_CASE_I("ico");
        LQSTR_CASE_I("image/x-icon");
            MIME_RET("image/x-icon")EXTENSION_RET("ico");
        LQSTR_CASE_I("jng");
        LQSTR_CASE_I("image/x-jng");
            MIME_RET("image/x-jng")EXTENSION_RET("jng");
        LQSTR_CASE_I("bmp");
        LQSTR_CASE_I("image/x-ms-bmp");
            MIME_RET("image/x-ms-bmp")EXTENSION_RET("bmp");
        LQSTR_CASE_I("webp");
        LQSTR_CASE_I("image/webp");
            MIME_RET("image/webp")EXTENSION_RET("webp");
        LQSTR_CASE_I("ear");
        LQSTR_CASE_I("war");
        LQSTR_CASE_I("jar");
        LQSTR_CASE_I("application/java-archive");
            MIME_RET("application/java-archive")EXTENSION_RET("ear", "war", "ear");
        LQSTR_CASE_I("ai");
        LQSTR_CASE_I("eps");
        LQSTR_CASE_I("ps");
        LQSTR_CASE_I("application/postscript");
            MIME_RET("application/postscript")EXTENSION_RET("ai", "eps", "ps");
        LQSTR_CASE_I("js");
        LQSTR_CASE_I("application/javascript");
            MIME_RET("application/javascript")EXTENSION_RET("js");
        LQSTR_CASE_I("atom");
        LQSTR_CASE_I("application/atom+xml");
            MIME_RET("application/atom+xml")EXTENSION_RET("atom");
        LQSTR_CASE_I("rss");
        LQSTR_CASE_I("application/rss+xml");
            MIME_RET("application/rss+xml")EXTENSION_RET("rss");
        LQSTR_CASE_I("woff");
        LQSTR_CASE_I("application/font-woff");
            MIME_RET("application/font-woff")EXTENSION_RET("woff");
        LQSTR_CASE_I("json");
        LQSTR_CASE_I("application/json");
            MIME_RET("application/json")EXTENSION_RET("json");
        LQSTR_CASE_I("hqx");
        LQSTR_CASE_I("application/mac-binhex40");
            MIME_RET("application/mac-binhex40")EXTENSION_RET("hqx");
        LQSTR_CASE_I("sh");
        LQSTR_CASE_I("application/x-shellscript");
            MIME_RET("application/x-shellscript")EXTENSION_RET("sh");
        LQSTR_CASE_I("doc");
        LQSTR_CASE_I("application/msword");
            MIME_RET("application/msword")EXTENSION_RET("doc");
        LQSTR_CASE_I("pdf");
        LQSTR_CASE_I("application/pdf");
            MIME_RET("application/pdf")EXTENSION_RET("pdf");
        LQSTR_CASE_I("rtf");
        LQSTR_CASE_I("application/rtf");
            MIME_RET("application/rtf")EXTENSION_RET("rtf");
        LQSTR_CASE_I("m3u8");
        LQSTR_CASE_I("application/vnd.apple.mpegurl");
            MIME_RET("application/vnd.apple.mpegurl")EXTENSION_RET("m3u8");
        LQSTR_CASE_I("xls");
        LQSTR_CASE_I("application/vnd.ms-excel");
            MIME_RET("application/vnd.ms-excel")EXTENSION_RET("xls");
        LQSTR_CASE_I("eot");
        LQSTR_CASE_I("application/vnd.ms-fontobject");
            MIME_RET("application/vnd.ms-fontobject")EXTENSION_RET("eot");
        LQSTR_CASE_I("ppt");
        LQSTR_CASE_I("application/vnd.ms-powerpoint");
            MIME_RET("application/vnd.ms-powerpoint")EXTENSION_RET("ppt");
        LQSTR_CASE_I("wmlc");
        LQSTR_CASE_I("application/vnd.wap.wmlc");
            MIME_RET("application/vnd.wap.wmlc")EXTENSION_RET("wmlc");
        LQSTR_CASE_I("kml");
        LQSTR_CASE_I("application/vnd.google-earth.kml+xml");
            MIME_RET("application/vnd.google-earth.kml+xml")EXTENSION_RET("kml");
        LQSTR_CASE_I("kmz");
        LQSTR_CASE_I("application/vnd.google-earth.kmz");
            MIME_RET("application/vnd.google-earth.kmz")EXTENSION_RET("kmz");
        LQSTR_CASE_I("application/vnd.android.package-archive")
        LQSTR_CASE_I("apk")
            MIME_RET("application/vnd.android.package-archive")EXTENSION_RET("apk");
        LQSTR_CASE_I("7z");
        LQSTR_CASE_I("application/x-7z-compressed");
            MIME_RET("application/x-7z-compressed")EXTENSION_RET("7z");
        LQSTR_CASE_I("cco");
        LQSTR_CASE_I("application/x-cocoa");
            MIME_RET("application/x-cocoa")EXTENSION_RET("cco");
        LQSTR_CASE_I("jardiff");
        LQSTR_CASE_I("application/x-java-archive-diff");
            MIME_RET("application/x-java-archive-diff")EXTENSION_RET("jardiff");
        LQSTR_CASE_I("jnlp");
        LQSTR_CASE_I("application/x-java-jnlp-file");
            MIME_RET("application/x-java-jnlp-file")EXTENSION_RET("jnlp");
        LQSTR_CASE_I("run");
        LQSTR_CASE_I("application/x-makeself");
            MIME_RET("application/x-makeself")EXTENSION_RET("run");
        LQSTR_CASE_I("pl");
        LQSTR_CASE_I("pm");
        LQSTR_CASE_I("application/x-perl");
            MIME_RET("application/x-perl")EXTENSION_RET("pl", "pm");
        LQSTR_CASE_I("pdb");
        LQSTR_CASE_I("prc");
        LQSTR_CASE_I("application/x-pilot");
            MIME_RET("application/x-pilot")EXTENSION_RET("pdb", "prc");
        LQSTR_CASE_I("rar");
        LQSTR_CASE_I("application/x-rar-compressed");
            MIME_RET("application/x-rar-compressed")EXTENSION_RET("rar");
        LQSTR_CASE_I("rpm");
        LQSTR_CASE_I("application/x-redhat-package-manager");
            MIME_RET("application/x-redhat-package-manager")EXTENSION_RET("rpm");
        LQSTR_CASE_I("sea");
        LQSTR_CASE_I("application/x-sea");
            MIME_RET("application/x-sea")EXTENSION_RET("sea");
        LQSTR_CASE_I("swf");
        LQSTR_CASE_I("application/x-shockwave-flash");
            MIME_RET("application/x-shockwave-flash")EXTENSION_RET("swf");
        LQSTR_CASE_I("sit");
        LQSTR_CASE_I("application/x-stuffit");
            MIME_RET("application/x-stuffit")EXTENSION_RET("sit");
        LQSTR_CASE_I("tcl");
        LQSTR_CASE_I("tk");
        LQSTR_CASE_I("application/x-tcl");
            MIME_RET("application/x-tcl")EXTENSION_RET("tcl", "tk");
        LQSTR_CASE_I("der");
        LQSTR_CASE_I("pem");
        LQSTR_CASE_I("crt");
        LQSTR_CASE_I("application/x-x509-ca-cert");
            MIME_RET("application/x-x509-ca-cert")EXTENSION_RET("der", "pem", "crt");
        LQSTR_CASE_I("xpi");
        LQSTR_CASE_I("application/x-xpinstall");
            MIME_RET("application/x-xpinstall")EXTENSION_RET("xpi");
        LQSTR_CASE_I("xhtml");
        LQSTR_CASE_I("application/xhtml+xml");
            MIME_RET("application/xhtml+xml")EXTENSION_RET("xhtml");
        LQSTR_CASE_I("xspf");
        LQSTR_CASE_I("application/xspf+xml");
            MIME_RET("application/xspf+xml")EXTENSION_RET("xspf");
        LQSTR_CASE_I("zip");
        LQSTR_CASE_I("application/zip");
            MIME_RET("application/zip")EXTENSION_RET("zip");
        LQSTR_CASE_I("gz");
        LQSTR_CASE_I("application/x-gzip");
            MIME_RET("application/x-gzip")EXTENSION_RET("gz");
        LQSTR_CASE_I("bz2");
        LQSTR_CASE_I("application/x-bzip");
            MIME_RET("application/x-bzip")EXTENSION_RET("bz2");
        LQSTR_CASE_I("tgz");
        LQSTR_CASE_I("application/x-compressed-tar");
            MIME_RET("application/x-compressed-tar")EXTENSION_RET("tgz");
        LQSTR_CASE_I("php");
        LQSTR_CASE_I("application/x-php");
            MIME_RET("application/x-php")EXTENSION_RET("php");
        LQSTR_CASE_I("deb");
        LQSTR_CASE_I("application/x-deb");
            MIME_RET("application/x-deb")EXTENSION_RET("deb");
        LQSTR_CASE_I("tar");
        LQSTR_CASE_I("application/x-tar");
            MIME_RET("application/x-tar")EXTENSION_RET("tar");
        LQSTR_CASE_I("odt");
        LQSTR_CASE_I("application/vnd.oasis.opendocument.text");
            MIME_RET("application/vnd.oasis.opendocument.text")EXTENSION_RET("odt");
        LQSTR_CASE_I("odp");
        LQSTR_CASE_I("application/vnd.oasis.opendocument.presentation");
            MIME_RET("application/vnd.oasis.opendocument.presentation")EXTENSION_RET("odp");
        LQSTR_CASE_I("docx");
        LQSTR_CASE_I("application/vnd.openxmlformats-officedocument.wordprocessingml.document");
            MIME_RET("application/vnd.openxmlformats-officedocument.wordprocessingml.document")EXTENSION_RET("docx");
        LQSTR_CASE_I("xlsx");
        LQSTR_CASE_I("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
            MIME_RET("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")EXTENSION_RET("xlsx");
        LQSTR_CASE_I("pptx");
        LQSTR_CASE_I("application/vnd.openxmlformats-officedocument.presentationml.presentation");
            MIME_RET("application/vnd.openxmlformats-officedocument.presentationml.presentation")EXTENSION_RET("pptx");
        LQSTR_CASE_I("kar");
        LQSTR_CASE_I("midi");
        LQSTR_CASE_I("mid");
        LQSTR_CASE_I("audio/midi");
            MIME_RET("audio/midi")EXTENSION_RET("mid", "midi", "kar");
        LQSTR_CASE_I("mp3");
        LQSTR_CASE_I("audio/mpeg");
            MIME_RET("audio/mpeg")EXTENSION_RET("mp3");
        LQSTR_CASE_I("ogg");
        LQSTR_CASE_I("audio/ogg");
            MIME_RET("audio/ogg")EXTENSION_RET("ogg");
        LQSTR_CASE_I("m4a");
        LQSTR_CASE_I("audio/x-m4a");
            MIME_RET("audio/x-m4a")EXTENSION_RET("m4a");
        LQSTR_CASE_I("ra");
        LQSTR_CASE_I("audio/x-realaudio");
            MIME_RET("audio/x-realaudio")EXTENSION_RET("ra");
        LQSTR_CASE_I("3gpp");
        LQSTR_CASE_I("3gp");
        LQSTR_CASE_I("video/3gpp");
            MIME_RET("video/3gpp")EXTENSION_RET("3gp", "3gpp");
        LQSTR_CASE_I("ts");
        LQSTR_CASE_I("video/mp2t");
            MIME_RET("video/mp2t")EXTENSION_RET("ts");
        LQSTR_CASE_I("mp4");
        LQSTR_CASE_I("video/mp4");
            MIME_RET("video/mp4")EXTENSION_RET("mp4");
        LQSTR_CASE_I("mpeg");
        LQSTR_CASE_I("mpg");
        LQSTR_CASE_I("video/mpeg");
            MIME_RET("video/mpeg")EXTENSION_RET("mpg", "mpeg");
        LQSTR_CASE_I("mov");
        LQSTR_CASE_I("video/quicktime");
            MIME_RET("video/quicktime")EXTENSION_RET("mov");
        LQSTR_CASE_I("webm");
        LQSTR_CASE_I("video/webm");
            MIME_RET("video/webm")EXTENSION_RET("webm");
        LQSTR_CASE_I("flv");
        LQSTR_CASE_I("video/x-flv");
            MIME_RET("video/x-flv")EXTENSION_RET("flv");
        LQSTR_CASE_I("m4v");
        LQSTR_CASE_I("video/x-m4v");
            MIME_RET("video/x-m4v")EXTENSION_RET("m4v");
        LQSTR_CASE_I("mng");
        LQSTR_CASE_I("video/x-mng");
            MIME_RET("video/x-mng")EXTENSION_RET("mng");
        LQSTR_CASE_I("asx");
        LQSTR_CASE_I("asf");
        LQSTR_CASE_I("video/x-ms-asf");
            MIME_RET("video/x-ms-asf")EXTENSION_RET("asx", "asf");
        LQSTR_CASE_I("wmv");
        LQSTR_CASE_I("video/x-ms-wmv");
            MIME_RET("video/x-ms-wmv")EXTENSION_RET("wmv");
        LQSTR_CASE_I("avi");
        LQSTR_CASE_I("video/x-msvideo");
            MIME_RET("video/x-msvideo")EXTENSION_RET("avi");
    }
    MIME_RET("application/octet-stream")EXTENSION_RET_OCT_STREAM;
}



