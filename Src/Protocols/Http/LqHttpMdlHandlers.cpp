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
#include "LqHttpMdl.h"
#include "LqHttpAtz.h"
#include "LqAtm.hpp"
#include "LqDfltRef.hpp"
#include "LqTime.h"
#include "LqLog.h"

void LQ_CALL LqHttpMdlHandlerSockErr(LqHttpConn* HttpConn) {
	int Fd;
	int Errno = 0;
	socklen_t Len;
	char Buf[1024];

	LqSockBufLock(HttpConn);
	Fd = LqSockBufGetFd(HttpConn);
	LqSockBufGetRemoteAddrStr(HttpConn, Buf, sizeof(Buf));
	Len = sizeof(Errno);
	getsockopt(Fd, SOL_SOCKET, SO_ERROR, (char*)&Errno, &Len);
	LqLogErr("LqHttp: Src %s:%i; Error when connetcted to %s. Error: %s", __FILE__, __LINE__, Buf, strerror(Errno));
	LqSockBufSetClose(HttpConn);
	LqSockBufUnlock(HttpConn);
}

void LQ_CALL LqHttpMdlHandlersError(LqHttpConn* HttpConn, int Code) {
	return;
}

void LQ_CALL LqHttpMdlHandlersResponseRedirection(LqHttpConn* HttpConn) {
	LqHttpConnData* HttpConnData;
	HttpConnData = LqHttpConnGetData(HttpConn);
	if(HttpConnData->Pth != NULL) {
		switch(HttpConnData->Pth->Type & LQHTTPPTH_TYPE_SEP) {
			case LQHTTPPTH_TYPE_FILE_REDIRECTION:
				break;
			default:
				LqHttpConnRspError(HttpConn, 500);
				return;
		}
	} else {
		LqHttpConnRspError(HttpConn, 500);
		return;
	}
	if(HttpConnData->BoundaryOrContentRangeOrLocation) {
		free(HttpConnData->BoundaryOrContentRangeOrLocation);
	}
	HttpConnData->Flags |= LQHTTPCONN_FLAG_LOCATION;
	HttpConnData->BoundaryOrContentRangeOrLocation = LqStrDuplicate(HttpConnData->Pth->Location);
	LqHttpConnRspError(HttpConn, HttpConnData->Pth->StatusCode);
}

void LQ_CALL LqHttpMdlHandlersServerName(LqHttpConn* HttpConn, char* NameBuf, size_t NameBufSize) {
	LqHttp* Http = LqHttpConnGetHttp(HttpConn);
	LqHttpGetNameServer(Http, NameBuf, NameBufSize);
}

void LQ_CALL LqHttpMdlHandlersAllowMethods(LqHttpConn* HttpConn, char* MethodBuf, size_t MethodBufSize) {
	LqStrCopyMax(MethodBuf, "GET, POST, PUT, HEAD, OPTIONS", MethodBufSize);
}

void LQ_CALL LqHttpMdlMethodHandler(LqHttpConn* HttpConn) {
	LqHttpConnData* HttpConnData;
	LqHttpData* HttpData;
	LqHttpMdl* Mdl;
	LqHttpRcvHdrs* RcvHdr;
	LqHttpPth* Pth;
	int l;
	char UpDirPath[LQ_MAX_PATH], t;
	LqFileStat Stat;
	uint8_t Authoriz;
	char AllowBuf[1024];

	Mdl = LqHttpConnGetMdl(HttpConn);
	HttpConnData = LqHttpConnGetData(HttpConn);
	HttpData = LqHttpConnGetHttpData(HttpConn);
	Pth = HttpConnData->Pth;

	if(Pth != NULL)
		switch(Pth->Type & LQHTTPPTH_TYPE_SEP) {
			case LQHTTPPTH_TYPE_FILE_REDIRECTION:
				if(LqHttpAtzDo(HttpConn, LQHTTPATZ_PERM_CHECK)) {
					Mdl->ResponseRedirectionProc(HttpConn);
				}
				return;
		}
	LQSTR_SWITCH_I(HttpConnData->RcvHdr->Method) {
		LQSTR_CASE_I("delete") {
			if(Pth == NULL) {
				LqHttpConnRspError(HttpConn, 404);
				return;
			}
			if(!LqHttpAtzDo(HttpConn, LQHTTPATZ_PERM_DELETE))
				return;

			switch(Pth->Type & LQHTTPPTH_TYPE_SEP) {
				case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
					if(LqFileRemove(Pth->RealPath) == -1) {
						LqHttpConnRspError(HttpConn, 500);
					} else {
						LqHttpConnRspError(HttpConn, 200);
					}
					return;
				case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
					Pth->ExecQueryProc(HttpConn);
					return;
			}
		}
		break;
		LQSTR_CASE_I("head") {
			HttpConnData->Flags |= LQHTTPCONN_FLAG_NO_BODY;
		}
		LQSTR_CASE_I("get") {
			if(Pth == NULL) {
				LqHttpConnRspError(HttpConn, 404);
				return;
			}
			if(!LqHttpAtzDo(HttpConn, LQHTTPATZ_PERM_READ))
				return;
			switch(Pth->Type & LQHTTPPTH_TYPE_SEP) {
				case LQHTTPPTH_TYPE_DIR: case LQHTTPPTH_TYPE_FILE:
					LqHttpConnRspFileAuto(HttpConn, NULL, NULL);
					return;
				case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
					Pth->ExecQueryProc(HttpConn);
					return;
			}
		}
		break;
		LQSTR_CASE_I("post")
		LQSTR_CASE_I("put") {
			if(Pth == NULL) {
				LqHttpConnRspError(HttpConn, 404);
				return;
			}
			RcvHdr = HttpConnData->RcvHdr;
			Authoriz = 0;
			switch(Pth->Type & LQHTTPPTH_TYPE_SEP) {
				case LQHTTPPTH_TYPE_DIR:
				case LQHTTPPTH_TYPE_FILE:
				{
					if(HttpConnData->ContentLength > 0)
						Authoriz |= LQHTTPATZ_PERM_WRITE;
					if(LqFileGetStat(Pth->RealPath, LqDfltPtr()) == 0) {
						Authoriz |= LQHTTPATZ_PERM_MODIFY;
					} else {
						Authoriz |= LQHTTPATZ_PERM_CREATE;

						l = LqStrLen(Pth->RealPath);
						for(; l >= 0; l--) {
							if(Pth->RealPath[l] == LQ_PATH_SEPARATOR)
								break;
						}
						if(l > sizeof(UpDirPath))
							break;
						memcpy(UpDirPath, Pth->RealPath, l);
						t = UpDirPath[l];
						UpDirPath[l] = '\0';
						if(LqFileGetStat(UpDirPath, &Stat) != 0) {
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
			if(!LqHttpAtzDo(HttpConn, Authoriz))
				return;
			switch(HttpConnData->Pth->Type & LQHTTPPTH_TYPE_SEP) {
				case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
					HttpConnData->Pth->ExecQueryProc(HttpConn);
					return;
				case LQHTTPPTH_TYPE_DIR:
					LqHttpConnRspError(HttpConn, 500);
					return;
			}
			if(LqHttpConnRcvGetBoundary(HttpConn, NULL, 0) > 0) {
				LqHttpConnRspError(HttpConn, 501);
				return;
			}
			if(LqHttpConnRcvFile(HttpConn, NULL, NULL, NULL, -1, 0666, true, Authoriz & LQHTTPATZ_PERM_CREATE_SUBDIR)) {
				return;
			} else {
				LqHttpConnRspError(HttpConn, 500);
				return;
			}
		}
		break;
		LQSTR_CASE_I("options") {
			if(HttpConnData->Pth != NULL) {
				switch(HttpConnData->Pth->Type & LQHTTPPTH_TYPE_SEP) {
					case LQHTTPPTH_TYPE_EXEC_DIR: case LQHTTPPTH_TYPE_EXEC_FILE:
						HttpConnData->Pth->ExecQueryProc(HttpConn);
						return;
				}
			}
			LqHttpConnRspError(HttpConn, 200);
			AllowBuf[0] = '\0';
			Mdl->AllowProc(HttpConn, AllowBuf, sizeof(AllowBuf) - 1);
			if(AllowBuf[0] != '\0')
				LqHttpConnRspHdrInsert(HttpConn, "Allow", AllowBuf);
			return;
		}
		break;
	}
}

void LQ_CALL LqHttpMdlHandlersEmpty(LqHttpConn*) {}

enum {
	MTagLen = sizeof(LqTimeSec) * 2 + sizeof(LqFileSz) * 2 + sizeof(unsigned int) * 2 + 4
};

void LQ_CALL LqHttpMdlHandlersCacheInfo (
	const char* Path,
	LqHttpConn* Connection,

	char* CacheControlDestBuf, /*If after call CacheControlDestBuf == "", then Cache-Control no include in response headers.*/
	size_t CacheControlDestBufSize,

	char* EtagDestBuf, /*If after call EtagDestBuf == "", then Etag no include in response headers.*/
	size_t EtagDestBufSize,

	LqTimeSec* LastModif, /*Local time. If after call LastModif == -1, then then no response Last-Modified.*/

	LqTimeSec* Expires
) {
	LqFileStat Stat;
	if(LqFileGetStat(Path, &Stat) != 0) 
		return;
	if((EtagDestBuf != nullptr) && ((MTagLen + 1) < EtagDestBufSize)) {
		if(Stat.Type == LQ_F_REG) {
			static const char Hex[] = "0123456789abcdef";
			char* s = EtagDestBuf;
			*s++ = '\"';
			{
				auto t = Stat.Id;
				for(int i = 0; i < (sizeof(t) * 2); i++, t >>= 4)
					*s++ = Hex[t & 0xf];
			}
			*s++ = '-';
			{
				auto t = Stat.Size;
				for(int i = 0; i < (sizeof(t) * 2); i++, t >>= 4)
					*s++ = Hex[t & 0xf];
			}
			*s++ = '-';
			{
				auto t = Stat.ModifTime;
				for(int i = 0; i < (sizeof(t) * 2); i++, t >>= 4)
					*s++ = Hex[t & 0xf];
			}
			*s++ = '\"';
			*s++ = '\0';
		}
	}
	if(LastModif != nullptr)
		*LastModif = Stat.ModifTime;
}

void LQ_CALL LqHttpMdlHandlersMime(const char* Path, LqHttpConn* Connection, char* MimeDestBuf, size_t MimeDestBufLen) {
    if(MimeDestBuf == nullptr)
        return;
    const char* f, *e;
    if(((f = LqStrRchr(Path, LQ_PATH_SEPARATOR)) == nullptr) || ((e = LqStrRchr(f + 1, '.')) == nullptr) || (e == (f + 1)))
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

const LqHttpExtensionMime* LqHttpMimeExtension(const char* Str) {
    LQSTR_SWITCH_I(Str) {
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



