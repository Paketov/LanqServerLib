/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpCore... - Main handlers of HTTP protocol.
*/

#include "LqOs.h"
#include "LqConn.h"
#include "LqStrSwitch.h"
#include "LqHttp.hpp"
#include "LqAlloc.hpp"
#include "LqLog.h"
#include "LqHttpPrs.h"
#include "LqHttpPth.hpp"
#include "LqHttpRsp.h"
#include "LqEvnt.h"
#include "LqTime.h"
#include "LqHttpMdl.h"
#include "LqAtm.hpp"
#include "LqHttpConn.h"
#include "LqHttpRcv.h"
#include "LqHttpMdlHandlers.h"
#include "LqFileTrd.h"
#include "LqHttpLogging.h"
#include "LqHttpAct.h"
#include "LqDfltRef.hpp"
#include "LqStr.hpp"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#define LqHttpConnGetRmtAddr(ConnectionPointer) ((sockaddr*)((LqHttpConn*)ConnectionPointer + 1))

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct HttpConnIp4
{
	LqHttpConn		Conn;
	sockaddr_in     Ip4Addr;
};

struct HttpConnIp6
{
	LqHttpConn		Conn;
	sockaddr_in6    Ip6Addr;
};

#pragma pack(pop)

/*
* Protocol managment procs.
*/
static LqConn* LQ_CALL LqHttpRegisterNewConnectionProc(LqProto* This, int ConnectionDescriptor, void* Address);
static void LQ_CALL LqHttpCoreWriteProc(LqConn* Connection);
static void LQ_CALL LqHttpCoreReadProc(LqConn* Connection);
static void LQ_CALL LqHttpCoreDisconnectProc(LqConn* Connection);
static bool LQ_CALL LqHttpCoreCmpIpAddress(LqConn* Connection, const void* Address);
static void LQ_CALL LqHttpFreeProtoNotifyProc(LqProto* This);
static char*LQ_CALL LqHttpCoreDbgInfoProc(LqConn* Connection);
static bool LQ_CALL LqHttpCoreKickByTimeOutProc(LqConn* Connection, LqTimeMillisec CurrentTimeMillisec, LqTimeMillisec EstimatedLiveTime);
static bool LqHttpMultipartAddHeaders(LqHttpMultipartHeaders** CurMultipart, const char* Buf, size_t BufLen);


static void LqHttpCoreRcvMultipartSkipToHdr(LqHttpConn* c);
static void LqHttpRcvMultipartReadHdr(LqHttpConn* c);
static void LqHttpRcvMultipartReadInFile(LqHttpConn* c);
static void LqHttpRcvMultipartReadInStream(LqHttpConn* c);

static void LqHttpCoreRspHdr(LqHttpConn* c);
static void LqHttpCoreRspCache(LqHttpConn* c);
static void LqHttpCoreRspFd(LqHttpConn* c);
static void LqHttpCoreRspStream(LqHttpConn* c);

static void LqHttpQurReadHeaders(LqHttpConn* Connection);
static void LqHttpParseHeader(LqHttpConn* c, LqHttpQuery* q, char* StartKey, char* StartVal, char* EndVal, size_t* CountHeders);

void LqCachedFileHdr::Cache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, LqFileStat const* Stat)
{
	Etag = nullptr;
	MimeType = nullptr;
	memset(&Hash, 0, sizeof(Hash));
}

void LqCachedFileHdr::Recache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, LqFileStat const* Stat)
{
	if(Etag != nullptr)
		free(Etag), Etag = nullptr;
	if(MimeType != nullptr)
		free(MimeType), MimeType = nullptr;
	memset(&Hash, 0, sizeof(Hash));
}

void LqCachedFileHdr::GetMD5(const void* CacheInterator, LqMd5* Dest)
{
	static const LqMd5 ZeroHash = {0};
	if(LqMd5Compare(&Hash, &ZeroHash) == 0)
	{
		Hash.data[0] = 1;
		LqMd5Gen(&Hash, ((LqFileChe<LqCachedFileHdr>::CachedFile*)CacheInterator)->Buf, ((LqFileChe<LqCachedFileHdr>::CachedFile*)CacheInterator)->SizeFile);
	}
	memcpy(Dest, &Hash, sizeof(Hash));
}

LQ_EXTERN_C bool LQ_CALL LqHttpProtoCreateSSL
(
	LqHttpProtoBase* Reg,
	const void* MethodSSL, /* Example SSLv23_method()*/
	const char* CertFile, /* Example: "server.pem"*/
	const char* KeyFile, /*Example: "server.key"*/
	int TypeCertFile,	   /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
	const char* CAFile,
	const char* CAPath,
	int ModeVerify,
	int VerifyDepth
)
{
#ifdef HAVE_OPENSSL
	bool r = false;
	static bool IsLoaded = false;
	LqAtmLkWr(Reg->sslLocker);
	LQ_BREAK_BLOCK_BEGIN
	if(!IsLoaded)
    {
	  IsLoaded = true;
      SSL_library_init();
      SSL_load_error_strings();
    }

	if((Reg->ssl_ctx = SSL_CTX_new((const SSL_METHOD*)MethodSSL)) == nullptr)
		break;
	if((SSL_CTX_use_certificate_file(Reg->ssl_ctx, CertFile, TypeCertFile) <= 0) ||
		(SSL_CTX_use_PrivateKey_file(Reg->ssl_ctx, KeyFile, TypeCertFile) <= 0))
	{
		SSL_CTX_free(Reg->ssl_ctx);
		Reg->ssl_ctx = nullptr;
		break;
	}
	if(CAFile && CAPath)
	{
		if(!SSL_CTX_load_verify_locations(Reg->ssl_ctx, CAFile, CAPath))
		{
			SSL_CTX_free(Reg->ssl_ctx);
			Reg->ssl_ctx = nullptr;
			break;
		}
		SSL_CTX_set_verify(Reg->ssl_ctx, ModeVerify, nullptr);
		SSL_CTX_set_verify_depth(Reg->ssl_ctx, VerifyDepth);
	}
	r = true;
	LQ_BREAK_BLOCK_END
	LqAtmUlkWr(Reg->sslLocker);
	return true;
#else
	return false;
#endif
}

LQ_EXTERN_C bool LQ_CALL LqHttpProtoSetSSL(LqHttpProtoBase* Reg, void* SSL_Ctx)
{
#ifdef HAVE_OPENSSL
	LqAtmLkWr(Reg->sslLocker);
	if(Reg->ssl_ctx != nullptr)
		SSL_CTX_free(Reg->ssl_ctx);
	Reg->ssl_ctx = SSL_Ctx;
	LqAtmUlkWr(Reg->sslLocker);
	return true;
#else
	return false;
#endif

}

LQ_EXTERN_C void LQ_CALL LqHttpProtoRemoveSSL(LqHttpProtoBase* Reg)
{
#ifdef HAVE_OPENSSL
	bool r = false;
	LqAtmLkWr(Reg->sslLocker);
	if(Reg->ssl_ctx != nullptr)
	{
		SSL_CTX_free(Reg->ssl_ctx);
		Reg->ssl_ctx = nullptr;
	}
	LqAtmUlkWr(Reg->sslLocker);
#endif
}

LQ_EXTERN_C LqHttpProtoBase* LQ_CALL LqHttpProtoCreate()
{
	LqHttpProto* r = LqFastAlloc::New<LqHttpProto>();
	if(r == nullptr)
		return nullptr;
	r->Base.Proto.NewConnProc = LqHttpRegisterNewConnectionProc;
	r->Base.Proto.ReciveProc = LqHttpCoreReadProc;
	r->Base.Proto.EndConnProc = LqHttpCoreDisconnectProc;
	r->Base.Proto.KickByTimeOutProc = LqHttpCoreKickByTimeOutProc;
	r->Base.Proto.CmpAddressProc = LqHttpCoreCmpIpAddress;
	r->Base.Proto.FreeProtoNotifyProc = LqHttpFreeProtoNotifyProc;
	r->Base.Proto.WriteProc = LqHttpCoreWriteProc;
	r->Base.Proto.DebugInfoProc = LqHttpCoreDbgInfoProc;

	r->Base.UseDefaultDmn = true;
	r->Base.IsResponse429 = false;
	r->Base.CountConnections = 0;
	r->Base.IsUnregister = false;

	r->Base.MaxHeadersSize = 32 * 1024;				//32 kByte
	r->Base.MaxMultipartHeadersSize = 32 * 1024;
	r->Base.Proto.MaxSendInTact = 32 * 1024;
	r->Base.Proto.MaxReciveInSingleTime = 32 * 1024 * 4;
	r->Base.Proto.MaxSendInSingleTime = 32 * 1024 * 4;
	r->Base.PeriodChangeDigestNonce = 5;			//5 Sec


	LqAtmLkInit(r->Base.ServNameLocker);
	LqHttpProtoSetNameServer(&r->Base, "Lanq(Lan Quick) 1.0");

	r->Cache.SetMaxSize(1024 * 1024 * 400);
	sprintf(r->Base.HTTPProtoVer, "1.1");
	LqHttpPthDmnCreate(&r->Base, "*");
#ifdef HAVE_OPENSSL
	r->Base.ssl_ctx = nullptr;
	LqAtmLkInit(r->Base.sslLocker);
#endif
	LqHttpMdlInit(&r->Base, &r->Base.StartModule, "StartModule", 0);
	return &r->Base;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpProtoSetNameServer(LqHttpProtoBase* Reg, const char* NewName)
{
	size_t SizeWritten = 0;
	LqAtmLkWr(Reg->ServNameLocker);
	LqStrCopyMax(Reg->ServName, NewName, sizeof(Reg->ServName));
	SizeWritten = LqStrLen(Reg->ServName);
	LqAtmUlkWr(Reg->ServNameLocker);
	return SizeWritten;
}

LQ_EXTERN_C size_t LQ_CALL LqHttpProtoGetNameServer(LqHttpProtoBase* Reg, char* Name, size_t SizeName)
{
	size_t SizeWritten = 0;
	LqAtmLkRd(Reg->ServNameLocker);
	LqStrCopyMax(Name, Reg->ServName, SizeName);
	SizeWritten = LqStrLen(Reg->ServName);
	LqAtmUlkRd(Reg->ServNameLocker);
	return SizeWritten;
}

LQ_EXTERN_C void LQ_CALL LqHttpEvntDfltIgnoreAnotherEventHandler(LqHttpConn* c)
{
	switch(c->ActionState)
	{
		case LQHTTPACT_STATE_MULTIPART_RCV_FILE:
		case LQHTTPACT_STATE_RCV_FILE: //Если закончился этап получения файла
		{
			if(c->ActionResult != LQHTTPACT_RES_OK)
			{
				LqHttpRcvFileCancel(c);
				LqHttpRspError(c, 500);
				break;
			}
			switch(LqHttpRcvFileCommit(c))
			{
				case LQHTTPRCV_UPDATED:
					LqHttpRspError(c, 200);
					break;
				case LQHTTPRCV_CREATED:
					LqHttpRspError(c, 201);
					break;
				default:
				case LQHTTPRCV_ERR:
					LqHttpRspError(c, 500);
					break;
			}
		}
		break;
		case LQHTTPACT_STATE_HANDLE_PROCESS:
			LqHttpRspError(c, 501);
			break;
		case LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS:
		case LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS:
		case LQHTTPACT_STATE_MULTIPART_RCV_HDRS:
		case LQHTTPACT_STATE_RCV_STREAM:
		case LQHTTPACT_STATE_MULTIPART_RCV_STREAM:
			LqHttpRspError(c, 500);
			break;
		case LQHTTPACT_STATE_SKIP_QUERY_BODY:
		case LQHTTPACT_STATE_RSP:
		case LQHTTPACT_STATE_RSP_CACHE:
		case LQHTTPACT_STATE_RSP_FD:
		case LQHTTPACT_STATE_RSP_STREAM:
			if(c->ActionResult != LQHTTPACT_RES_OK)
				LqHttpActSwitchToClose(c);
			break;
	}
}

/*
* C functions shell for cache
*/

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetMaxSize(LqHttpProtoBase* Reg)
{
	return ((LqHttpProto*)Reg)->Cache.GetMaxSize();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetMaxSize(LqHttpProtoBase* Reg, size_t NewVal)
{
	((LqHttpProto*)Reg)->Cache.SetMaxSize(NewVal);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetMaxSizeFile(LqHttpProtoBase* Reg)
{
	return ((LqHttpProto*)Reg)->Cache.GetMaxSizeFile();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetMaxSizeFile(LqHttpProtoBase* Reg, size_t NewVal)
{
	((LqHttpProto*)Reg)->Cache.SetMaxSizeFile(NewVal);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetEmployedSize(LqHttpProtoBase* Reg)
{
	return ((LqHttpProto*)Reg)->Cache.GetEmployedSize();
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqHttpCheGetPeriodUpdateStat(LqHttpProtoBase* Reg)
{
	return ((LqHttpProto*)Reg)->Cache.GetPeriodUpdateStat();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetPeriodUpdateStat(LqHttpProtoBase* Reg, LqTimeMillisec Millisec)
{
	((LqHttpProto*)Reg)->Cache.SetPeriodUpdateStat(Millisec);
}

LQ_EXTERN_C size_t LQ_CALL LqHttpCheGetMaxCountOfPrepared(LqHttpProtoBase* Reg)
{
	return ((LqHttpProto*)Reg)->Cache.GetMaxCountOfPrepared();
}

LQ_EXTERN_C void LQ_CALL LqHttpCheSetMaxCountOfPrepared(LqHttpProtoBase* Reg, size_t Count)
{
	((LqHttpProto*)Reg)->Cache.SetMaxCountOfPrepared(Count);
}


/*
* C shell for cache
*/

static LqConn* LQ_CALL LqHttpRegisterNewConnectionProc(LqProto* This, int SockDescriptor, void* Address)
{
	LqHttpConn* c;
	switch(((sockaddr*)Address)->sa_family)
	{
		case AF_INET:
		{
			auto r = LqFastAlloc::New<HttpConnIp4>();
			if(r == nullptr)
			{
				closesocket(SockDescriptor);
				return nullptr;
			}
			r->Ip4Addr = *(sockaddr_in*)Address;
			c = (LqHttpConn*)r;
		}
		break;
		case AF_INET6:
		{
			auto r = LqFastAlloc::New<HttpConnIp6>();
			if(r == nullptr)
			{
				closesocket(SockDescriptor);
				return nullptr;
			}
			r->Ip6Addr = *(sockaddr_in6*)Address;
			c = (LqHttpConn*)r;
		}
		break;
		default:
			closesocket(SockDescriptor);
			return nullptr;
	}

	c->CommonConn.SockDscr = SockDescriptor;
	c->CommonConn.Proto = This;
	c->CommonConn.Flag = 0;
	LqConnSetEvents(c, LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP | LQCONN_FLAG_RD);

	c->Buf = nullptr;
	c->BufSize = 0;
	c->_Reserved = 0;

	c->TimeStartMillisec =
	c->TimeLastRecivedMillisec = LqTimeGetLocMillisec();
	c->Pth = nullptr;
	LqHttpProto* r = (LqHttpProto*)This;
	LqAtmIntrlkInc(r->Base.CountConnections);
	LqHttpActSwitchToRcvHdrs(c);
#ifdef HAVE_OPENSSL
	c->ssl = nullptr;
	LqAtmLkRd(r->Base.sslLocker);
	if(r->Base.ssl_ctx != nullptr)
	{
		if((c->ssl = SSL_new(r->Base.ssl_ctx)) == nullptr)
		{
			LqAtmLkUlkRd(r->Base.sslLocker);
			LqHttpCoreDisconnectProc(&c->CommonConn);
			return nullptr;
		}
		LqAtmLkUlkRd(r->Base.sslLocker);
		if(SSL_set_fd(c->ssl, c->CommonConn.SockDscr) == 0)
		{
			SSL_free(c->ssl);
			c->ssl = nullptr;
			LqHttpCoreDisconnectProc(&c->CommonConn);
			return nullptr;
		}
		if(SSL_accept(c->ssl) <= 0)
		{
			LqHttpCoreDisconnectProc(&c->CommonConn);
			return nullptr;
		}
	} else
	{
		LqAtmLkUlkRd(r->Base.sslLocker);
	}
#endif
	return &c->CommonConn;
}

static void LQ_CALL LqHttpCoreDisconnectProc(LqConn* Connection)
{
	LqHttpConn* c = (LqHttpConn*)Connection;
	LqHttpProto* r = LqHttpGetReg(c);
	c->EventClose(c);
	LqHttpActKeepOnlyHeaders(c);
	LqHttpConnPthRemove(c);
	if(c->Buf != nullptr)
		___free(c->Buf);

	LqAtmIntrlkDec(r->Base.CountConnections);
	if(r->Base.IsUnregister && (r->Base.CountConnections == 0))
		LqFastAlloc::Delete(r);
#ifdef HAVE_OPENSSL
	if(c->ssl != nullptr)
	{
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
	}
#endif
	//shutdown(Connection->SockDscr, SHUT_RD | SHUT_WR | SHUT_RDWR);
	closesocket(Connection->SockDscr);
	switch(LqHttpConnGetRmtAddr(c)->sa_family)
	{
		case AF_INET:
			LqFastAlloc::Delete((HttpConnIp4*)Connection);
			break;
		case AF_INET6:
			LqFastAlloc::Delete((HttpConnIp6*)Connection);
			break;
		default:
			LqFastAlloc::JustDelete(Connection);
	}

}

static void LQ_CALL LqHttpCoreWriteProc(LqConn* Connection)
{
	LqHttpConn* c = (LqHttpConn*)Connection;
	switch(LqHttpActState OldAct = c->ActionState)
	{
		case LQHTTPACT_STATE_RSP:
			LqHttpCoreRspHdr(c);
lblResponseResult:
			switch(c->ActionResult)
			{
				case LQHTTPACT_RES_PARTIALLY: return;
				case LQHTTPACT_RES_OK:
					c->EventAct(c);
					if(c->Flags & LQHTTPCONN_FLAG_CLOSE)
					{
						LqConnSetClose(c);
						return;
					}
					LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);
					LqHttpActSwitchToRcvHdrs(c);
					LqConnSetEvents(c, LQCONN_FLAG_RD | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
					return;
				default:
					c->EventAct(c);
					LqConnSetClose(c);
					return;
			}
		case LQHTTPACT_STATE_RSP_CACHE:
			LqHttpCoreRspCache(c);
			goto lblResponseResult;
		case LQHTTPACT_STATE_RSP_FD:
			LqHttpCoreRspFd(c);
			goto lblResponseResult;
		case LQHTTPACT_STATE_RSP_STREAM:
			LqHttpCoreRspStream(c);
			goto lblResponseResult;
		case LQHTTPACT_STATE_RSP_HANDLE_PROCESS:
			c->EventAct(c);
			if(OldAct != c->ActionState)
				return LqHttpCoreReadProc(&c->CommonConn);
			LqConnSetEvents(c, LQCONN_FLAG_RD | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
			return;
	}
}

static void LQ_CALL LqHttpCoreReadProc(LqConn* Connection)
{
	LqHttpConn* c = (LqHttpConn*)Connection;
	c->TimeLastRecivedMillisec = LqTimeGetLocMillisec();
	LqHttpActState OldAct = (LqHttpActState)-1;
lblSwitch:
	if(LqConnIsLock(c))
	{
        if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_RSP)
            LqConnSetEvents(c, LQCONN_FLAG_WR | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
		return;
    }
	if((OldAct == c->ActionState) && (c->ActionResult != LQHTTPACT_RES_BEGIN))
	{
		//If EventAct don`t change state
		LQHTTPLOQ_ERR("module \"%s\" dont change action state", LqHttpMdlGetByConn(c)->Name);
		if(LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER)
		{
			static const char  ErrCode[] =
				"HTTP/1.1 500 Internal Server Error\r\n"
				"Connection: close\r\n"
				"Content-Type: text/html; charset=\"UTF-8\"\r\n"
				"Content-Length: 25\r\n"
				"\r\n"
				"500 Internal Server Error";
			LqHttpConnSend_Native(c, ErrCode, sizeof(ErrCode) - 1);
		}
		LqConnSetClose(c);
		return;
	}
	switch(OldAct = c->ActionState)
	{
		case LQHTTPACT_STATE_GET_HDRS:
			LqHttpQurReadHeaders(c);
			if(c->ActionResult == LQHTTPACT_RES_PARTIALLY) return;
			goto lblSwitch;
		case LQHTTPACT_STATE_HANDLE_PROCESS:
			c->EventAct(c);
			goto lblSwitch;
		case LQHTTPACT_STATE_RCV_FILE:
			if(LqHttpConnReciveInFile(c, c->Query.OutFd, c->Query.PartLen - c->ReadedBodySize) < 0)
			{
				c->ActionResult = LQHTTPACT_RES_FILE_WRITE_ERR;
			}else if(c->ReadedBodySize < c->Query.PartLen)
			{
				c->ActionResult = LQHTTPACT_RES_PARTIALLY;
				return;
			} else
			{
				c->ActionResult = LQHTTPACT_RES_OK;
			}
			c->EventAct(c);
			goto lblSwitch;
		case LQHTTPACT_STATE_RCV_STREAM:
			if(LqHttpConnReciveInStream(c, &c->Query.Stream, c->Query.PartLen - c->ReadedBodySize) < 0)
			{
				c->ActionResult = LQHTTPACT_RES_STREAM_WRITE_ERR;
			}else if(c->ReadedBodySize < c->Query.PartLen)
			{
				c->ActionResult = LQHTTPACT_RES_PARTIALLY;
				return;
			} else
			{
				c->ActionResult = LQHTTPACT_RES_OK;
			}
			c->EventAct(c);
			goto lblSwitch;
		////////
		case LQHTTPACT_STATE_RCV_HANDLE_PROCESS:
			c->EventAct(c);
			if(OldAct != c->ActionState)
				goto lblSwitch;
			return;
		case LQHTTPACT_STATE_RSP_HANDLE_PROCESS:
			c->EventAct(c);
			if(OldAct != c->ActionState)
				goto lblSwitch;
			LqConnSetEvents(c, LQCONN_FLAG_WR | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
			return;
		////////
		case LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS:
			LqHttpCoreRcvMultipartSkipToHdr(c);
			if(c->ActionResult == LQHTTPACT_RES_PARTIALLY) return;
			c->EventAct(c);
			goto lblSwitch;
		case LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS:
		{
			LqHttpCoreRcvMultipartSkipToHdr(c);
			switch(c->ActionResult)
			{
				case LQHTTPACT_RES_PARTIALLY: return;
				default:
					c->EventAct(c);
					goto lblSwitch;
				case LQHTTPACT_RES_OK: OldAct = c->ActionState = LQHTTPACT_STATE_MULTIPART_RCV_HDRS;
			}
		}
		case LQHTTPACT_STATE_MULTIPART_RCV_HDRS:
		{
			LqHttpRcvMultipartReadHdr(c);
			if(c->ActionResult == LQHTTPACT_RES_PARTIALLY) return;
			c->EventAct(c);
			goto lblSwitch;
		}
		case LQHTTPACT_STATE_MULTIPART_RCV_FILE:
		{
			LqHttpRcvMultipartReadInFile(c);
			if(c->ActionResult == LQHTTPACT_RES_PARTIALLY) return;
			c->EventAct(c);
			goto lblSwitch;
		}
		case LQHTTPACT_STATE_MULTIPART_RCV_STREAM:
		{
			LqHttpRcvMultipartReadInStream(c);
			if(c->ActionResult == LQHTTPACT_RES_PARTIALLY) return;
			c->EventAct(c);
			goto lblSwitch;
		}
		////////
		case LQHTTPACT_STATE_SKIP_QUERY_BODY: lblSkip:
		{
			c->Response.CountNeedRecive -= LqHttpConnSkip(c, c->Response.CountNeedRecive);
			if(c->Response.CountNeedRecive > 0)
			{
				if(c->ActionState == LQHTTPACT_STATE_RSP)
				{
					c->ActionState = LQHTTPACT_STATE_SKIP_QUERY_BODY;
					LqConnSetEvents(c, LQCONN_FLAG_RD | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
				}
				return;
			} else
			{
				if(c->Flags & LQHTTPCONN_FLAG_CLOSE)
				{
					LqConnSetClose(c);
					return;
				}
				LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);
				LqHttpActSwitchToRcvHdrs(c);
				LqConnSetEvents(c, LQCONN_FLAG_RD | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
				return;
			}
		}
		case LQHTTPACT_STATE_RSP:
			LqHttpCoreRspHdr(c);
			lblResponseResult:
			switch(c->ActionResult)
			{
				case LQHTTPACT_RES_OK:
					c->EventAct(c); //Send OK state event to user //If you dont want this event, then set empty function instead this
					if(c->Response.CountNeedRecive > 0)
                        goto lblSkip;
					if(c->Flags & LQHTTPCONN_FLAG_CLOSE)
					{
						LqConnSetClose(c);
						return;
					}
					LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);

					LqHttpActSwitchToRcvHdrs(c);
					LqConnSetEvents(c, LQCONN_FLAG_RD | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
					return;
				case LQHTTPACT_RES_PARTIALLY:
					LqConnSetEvents(c, LQCONN_FLAG_WR | LQCONN_FLAG_HUP | LQCONN_FLAG_RDHUP);
					return;
				default:
					c->EventAct(c); //Send Error event to user
					LqConnSetClose(c);
					return;
			}
		case LQHTTPACT_STATE_RSP_CACHE:
			LqHttpCoreRspCache(c);
			goto lblResponseResult;
		case LQHTTPACT_STATE_RSP_FD:
			LqHttpCoreRspFd(c);
			goto lblResponseResult;
		case LQHTTPACT_STATE_RSP_STREAM:
			LqHttpCoreRspStream(c);
			goto lblResponseResult;
		case LQHTTPACT_STATE_CLS_CONNECTION:
			LqConnSetClose(c);
			return;
	}
}

static bool LQ_CALL LqHttpCoreCmpIpAddress(LqConn* c, const void* Address)
{
	if(LqHttpConnGetRmtAddr(c)->sa_family != ((sockaddr*)Address)->sa_family) return false;
	switch(LqHttpConnGetRmtAddr(c)->sa_family)
	{
		case AF_INET: return memcmp(&((sockaddr_in*)LqHttpConnGetRmtAddr(c))->sin_addr, &((sockaddr_in*)Address)->sin_addr, sizeof(((sockaddr_in*)Address)->sin_addr)) == 0;
		case AF_INET6: return memcmp(&((sockaddr_in6*)LqHttpConnGetRmtAddr(c))->sin6_addr, &((sockaddr_in6*)Address)->sin6_addr, sizeof(((sockaddr_in6*)Address)->sin6_addr)) == 0;
	}
	return false;
}

static void LQ_CALL LqHttpFreeProtoNotifyProc(LqProto* This)
{
	LqHttpProto* CurReg = (LqHttpProto*)This;
	CurReg->Base.IsUnregister = true;
	LqHttpMdlFreeAll(&CurReg->Base);
	if(CurReg->Base.CountConnections == 0)
	{
#ifdef HAVE_OPENSSL
		if(CurReg->Base.ssl_ctx != nullptr)
			SSL_CTX_free(CurReg->Base.ssl_ctx);
#endif
		LqFastAlloc::Delete(CurReg);
	}
}

static bool LQ_CALL LqHttpCoreKickByTimeOutProc(LqConn* Connection, LqTimeMillisec CurrentTimeMillisec, LqTimeMillisec EstimatedLiveTime)
{
	LqHttpConn* c = (LqHttpConn*)Connection;
	LqTimeMillisec TimeDiff = CurrentTimeMillisec - c->TimeLastRecivedMillisec;
	if(TimeDiff > EstimatedLiveTime)
        return true;
	return false;
}

static char* LQ_CALL LqHttpCoreDbgInfoProc(LqConn* Connection)
{
	return nullptr;
}

/////////////////////////////////////////
//Start Rsp
/////////////////////////////////////////

static bool LqHttpCoreRspRangesRestruct(LqHttpConn* c)
{
	auto Module = LqHttpMdlGetByConn(c);
	c->Response.CurRange++;
	if(c->Response.CurRange < c->Response.CountRanges)
	{
		char* HedersBuf = LqHttpRspHdrResize(c, 4096);
		LqFileSz CommonLenFile;
		LqFileStat s;
		s.ModifTime = 0;
		if(c->Response.Fd != -1)
		{
			LqFileGetStatByFd(c->Response.Fd, &s);
			CommonLenFile = s.Size;
		} else if(c->Response.CacheInterator != nullptr)
		{
			CommonLenFile = ((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator)->SizeFile;
		} else
		{
			CommonLenFile = 0;
		}
		char MimeBuf[1024];
		MimeBuf[0] = '\0';
		Module->GetMimeProc(c->Pth->RealPath, c, MimeBuf, sizeof(MimeBuf), (s.ModifTime == 0) ? nullptr : &s);

		int LenWritten = LqHttpRspPrintRangeBoundaryHeaders
		(
			HedersBuf,
			c->BufSize,
			c,
			CommonLenFile,
			c->Response.Ranges[c->Response.CurRange].Start,
			c->Response.Ranges[c->Response.CurRange].End,
			(MimeBuf[0] == '\0') ? nullptr : MimeBuf
		);
		LqHttpRspHdrResize(c, LenWritten);
		return true;
	} else if(c->Response.CountRanges > 1)
	{
		char* HedersBuf = LqHttpRspHdrResize(c, 4096);
		int EndHederLen = LqHttpRspPrintEndRangeBoundaryHeader(HedersBuf, c->BufSize);
		LqHttpRspHdrResize(c, EndHederLen);
		return true;
	}
	return false;
}

static void LqHttpCoreRspHdr(LqHttpConn* c)
{
	if(c->Response.HeadersStart < c->Response.HeadersEnd)
	{
lblOutHeader:
		auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
		c->Response.HeadersStart += SendedSize;
		if(c->Response.HeadersStart < c->Response.HeadersEnd)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			return;
		}
	}
	c->ActionResult = LQHTTPACT_RES_OK;
}

static void LqHttpCoreRspCache(LqHttpConn* c)
{
	if(c->Response.HeadersStart < c->Response.HeadersEnd)
	{
lblOutHeader:
		auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
		if(c->Response.CurRange > 0)
			c->WrittenBodySize += SendedSize;
		c->Response.HeadersStart += SendedSize;
		if(c->Response.HeadersStart < c->Response.HeadersEnd)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			return;
		}
	}
	LqFileSz ResponseSize = c->Response.Ranges[c->Response.CurRange].End - c->Response.Ranges[c->Response.CurRange].Start;
	LqFileSz SendedSize = LqHttpConnSend
	(
		c,
		(const char*)((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator)->Buf +
		c->Response.Ranges[c->Response.CurRange].Start,
		ResponseSize
	);
	c->Response.Ranges[c->Response.CurRange].Start += SendedSize;
	if(c->Response.Ranges[c->Response.CurRange].End <= c->Response.Ranges[c->Response.CurRange].Start)
	{
		if(LqHttpCoreRspRangesRestruct(c))
			goto lblOutHeader;
	} else
	{
		c->ActionResult = LQHTTPACT_RES_PARTIALLY;
		return;
	}
	c->ActionResult = LQHTTPACT_RES_OK;
}

static void LqHttpCoreRspFd(LqHttpConn* c)
{
	if(c->Response.HeadersStart < c->Response.HeadersEnd)
	{
lblOutHeader:
		auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
		if(c->Response.CurRange > 0)
			c->WrittenBodySize += SendedSize;
		c->Response.HeadersStart += SendedSize;
		if(c->Response.HeadersStart < c->Response.HeadersEnd)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			return;
		}
	}
	LqFileSz ResponseSize = c->Response.Ranges[c->Response.CurRange].End - c->Response.Ranges[c->Response.CurRange].Start;
	LqFileSz SendedSize = LqHttpConnSendFromFile(c, c->Response.Fd, c->Response.Ranges[c->Response.CurRange].Start, ResponseSize);
	c->Response.Ranges[c->Response.CurRange].Start += SendedSize;
	if(c->Response.Ranges[c->Response.CurRange].End <= c->Response.Ranges[c->Response.CurRange].Start)
	{
		if(LqHttpCoreRspRangesRestruct(c)) goto lblOutHeader;
	} else
	{
		c->ActionResult = LQHTTPACT_RES_PARTIALLY;
		return;
	}
	c->ActionResult = LQHTTPACT_RES_OK;
}

static void LqHttpCoreRspStream(LqHttpConn* c)
{
	if(c->Response.HeadersStart < c->Response.HeadersEnd)
	{
lblOutHeader:
		auto SendedSize = LqHttpConnSend_Native(c, c->Buf + c->Response.HeadersStart, c->Response.HeadersEnd - c->Response.HeadersStart);
		if(c->Response.CurRange > 0)
			c->WrittenBodySize += SendedSize;
		c->Response.HeadersStart += SendedSize;
		if(c->Response.HeadersStart < c->Response.HeadersEnd)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			return;
		}
	}
	LqHttpConnSendFromStream(c, &c->Response.Stream, c->Response.Stream.Len);
	if(c->Response.Stream.Len > 0)
	{
		c->ActionResult = LQHTTPACT_RES_PARTIALLY;
		return;
	}
	c->ActionResult = LQHTTPACT_RES_OK;
}

/////////////////////////////////////////
//End Rsp
/////////////////////////////////////////


////////////////////////////////////////
// Multipart data
////////////////////////////////////////

static void LqHttpCoreRcvMultipartSkipToHdr(LqHttpConn* c)
{
	char Buf[LQCONN_MAX_LOCAL_SIZE];
	const static int16_t BeginChain = *(int16_t*)"--";
	const static int16_t EndChain = *(int16_t*)"\r\n";
	const static int16_t EndChain2 = *(int16_t*)"--";


	auto q = &c->Query;
	auto ReadedBodySize = c->ReadedBodySize;
	for(; q->MultipartHeaders != nullptr; q = &q->MultipartHeaders->Query)
		ReadedBodySize = q->MultipartHeaders->ReadedBodySize;

	LqFileSz LeftContentLen = q->ContentLen - ReadedBodySize;

	size_t BoundaryChainSize = (sizeof(BeginChain) + sizeof(EndChain)) + q->ContentBoundaryLen;
	LqFileSz InTactReaded = 0;
	LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;

	while(true)
	{
		LqFileSz CurSizeRead = LeftContentLen - InTactReaded;
		if(CurSizeRead <= BoundaryChainSize)
		{
			if(CurSizeRead > 0)
			{
				auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
				if(Skipped < 0)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblOut;
				}
				InTactReaded += Skipped;
				if(Skipped < CurSizeRead)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblOut;
				}
			}
			c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
			goto lblOut;
		}
		if(CurSizeRead > (sizeof(Buf) - sizeof(BeginChain)))
			CurSizeRead = (sizeof(Buf) - sizeof(BeginChain));
		auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
		if(PeekRecived == -1)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblOut;
		}
		*(int16_t*)(Buf + PeekRecived) = BeginChain;
		size_t Checked = 0;
		for(register char* i = Buf; ; i++)
		{
			if(*(uint16_t*)i == BeginChain)
			{
				if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived)
					break;
				if(memcmp(q->ContentBoundary, i + sizeof(BeginChain), q->ContentBoundaryLen) == 0)
				{
					if(EndChain == *(uint16_t*)(i + sizeof(BeginChain) + q->ContentBoundaryLen))
					{
						InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked + BoundaryChainSize, 0);
						c->ActionResult = LQHTTPACT_RES_OK;
						goto lblOut;
					} else if(EndChain2 == *(uint16_t*)(i + sizeof(BeginChain) + q->ContentBoundaryLen))
					{
						InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked + BoundaryChainSize, 0);
						c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
						goto lblOut;
					}
				}
			}
		}
		InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
		if(InTactReaded > MaxReciveSizeInTact)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblOut;
		}
	}

lblOut:
	c->ReadedBodySize += InTactReaded;
	for(auto mh = c->Query.MultipartHeaders; mh != nullptr; mh = mh->Query.MultipartHeaders)
		mh->ReadedBodySize += InTactReaded;
}

/*
*
* Returned errors: SNDWD_STAT_OK, SNDWD_STAT_PARTIALLY, SNDWD_STAT_FULL_MAX, SNDWD_STAT_MULTIPART_END, SNDWD_STAT_MULTIPART_INVALID_HEADER
*/

static void LqHttpRcvMultipartReadHdr(LqHttpConn* c)
{
	char Buf[LQCONN_MAX_LOCAL_SIZE];
	static const int32_t EndChain = *(int32_t*)"\r\n\r\n";
	static const size_t BoundaryChainSize = sizeof(EndChain);

	LqHttpMultipartHeaders* CurMultipart = (LqHttpMultipartHeaders*)c->_Reserved;
	LqFileSz HdrRecived = 0;
	if(CurMultipart != nullptr)
		HdrRecived = CurMultipart->BufSize;

	auto q = &c->Query;
	auto ReadedBodySize = c->ReadedBodySize;
	for(; q->MultipartHeaders != nullptr; q = &q->MultipartHeaders->Query)
		ReadedBodySize = q->MultipartHeaders->ReadedBodySize;
	LqFileSz LeftContentLen = q->ContentLen - ReadedBodySize;

	LqFileSz InTactReaded = 0;
	LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;
	LqFileSz MaxHeadersSize = LqHttpProtoGetByConn(c)->MaxMultipartHeadersSize;

	while(true)
	{
		LqFileSz CurSizeRead = LeftContentLen - InTactReaded;
		if(CurSizeRead <= BoundaryChainSize)
		{
			if(CurSizeRead > 0)
			{
				auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
				if(Skipped < 0)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblPartial;
				}
				InTactReaded += Skipped;
				if(Skipped < CurSizeRead)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblPartial;
				}
			}
			c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
			goto lblOutErr;
		}
		if(CurSizeRead > (sizeof(Buf) - sizeof(EndChain)))
			CurSizeRead = (sizeof(Buf) - sizeof(EndChain));
		if((HdrRecived + InTactReaded) > MaxHeadersSize)
		{
			CurSizeRead = MaxHeadersSize - (InTactReaded + HdrRecived);
			if(CurSizeRead <= 0)
			{
				c->ActionResult = LQHTTPACT_RES_HEADERS_READ_MAX;
				goto lblOutErr;
			}
		}
		auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
		if(PeekRecived == -1)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblPartial;
		}

		*(int32_t*)(Buf + PeekRecived) = EndChain;
		size_t Checked = 0;
		for(register char* i = Buf; ; i++)
		{
			if(EndChain == *(int32_t*)i)
			{
				if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived)
					break;
				if(!LqHttpMultipartAddHeaders(&CurMultipart, Buf, Checked + BoundaryChainSize))
				{
					c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
					goto lblOutErr;
				}
				InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked + BoundaryChainSize, 0);
				char* Start = CurMultipart->Buf;
				for(;;)
				{
					char *StartKey, *EndKey, *StartVal, *EndVal, *EndHeader;
					LqHttpPrsHdrStatEnm r = LqHttpPrsHeader(Start, StartKey, EndKey, StartVal, EndVal, EndHeader);
					switch(r)
					{
						case READ_HEADER_SUCCESS:
						{
							char t = *EndKey;
							*EndKey = '\0';
							LqHttpParseHeader(c, &CurMultipart->Query, StartKey, StartVal, EndVal, LqDfltRef());
							*EndKey = t;
							Start = EndHeader;
						}
						continue;
						case READ_HEADER_ERR:
							c->ActionResult = LQHTTPACT_RES_INVALID_HEADER;
							goto lblOutErr;
					}
					if(r == READ_HEADER_END) break;
				}
				c->ActionResult = LQHTTPACT_RES_OK;
				q->MultipartHeaders = CurMultipart;
				CurMultipart->Buf[CurMultipart->BufSize] = '\0';
				c->_Reserved = 0;
				goto lblOk;
			}
		}
		if(!LqHttpMultipartAddHeaders(&CurMultipart, Buf, Checked))
		{
			c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
			goto lblOutErr;
		}
		InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
		if(InTactReaded > MaxReciveSizeInTact)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblPartial;
		}
	}
lblOutErr:
	if(CurMultipart != nullptr)
	{
		free(CurMultipart);
		CurMultipart = nullptr;
	}
lblPartial:
	c->_Reserved = (uintptr_t)CurMultipart;
lblOk:
	c->ReadedBodySize += InTactReaded;
	for(auto mh = c->Query.MultipartHeaders; (mh != nullptr) && (q->MultipartHeaders != CurMultipart); mh = mh->Query.MultipartHeaders)
		mh->ReadedBodySize += InTactReaded;
}

static void LqHttpRcvMultipartReadInFile(LqHttpConn* c)
{
	char Buf[LQCONN_MAX_LOCAL_SIZE];
	static const int32_t BeginChain = *(int32_t*)"\r\n--";

	auto mph = c->Query.MultipartHeaders;
	if(mph == nullptr)
	{
		c->ActionResult = LQHTTPACT_RES_INVALID_HEADER;
		return;
	}
	auto bq = &c->Query;
	for(; ; mph = mph->Query.MultipartHeaders)
	{
		if(mph->Query.ContentBoundary)
			bq = &mph->Query;
		if(mph->Query.MultipartHeaders == nullptr)
			break;
	}
	int OutFd = c->Query.OutFd;
	size_t BoundaryChainSize = sizeof(BeginChain) + bq->ContentBoundaryLen;
	LqFileSz LeftContentLen = mph->Query.ContentLen - mph->ReadedBodySize;
	LqFileSz InTactReaded = 0;
	LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;

	while(true)
	{
		size_t CurSizeRead = LeftContentLen - InTactReaded;
		if(CurSizeRead <= BoundaryChainSize)
		{
			if(CurSizeRead > 0)
			{
				auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
				if(Skipped < 0)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblOut;
				}
				InTactReaded += Skipped;
				if(Skipped < CurSizeRead)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblOut;
				}
			}
			c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
			goto lblOut;
		}
		if(CurSizeRead > (sizeof(Buf) - sizeof(BeginChain)))
			CurSizeRead = (sizeof(Buf) - sizeof(BeginChain));
		if((CurSizeRead + InTactReaded) > LeftContentLen)
		{
			if((CurSizeRead = LeftContentLen - InTactReaded) <= 0)
			{
				c->ActionResult = LQHTTPACT_RES_OK;
				goto lblOut;
			}
		}
		auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
		if(PeekRecived == -1)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblOut;
		}
		*(int32_t*)(Buf + PeekRecived) = BeginChain;

		size_t Checked = 0;
		for(register char* i = Buf; ; i++)
		{
			if(BeginChain == *(int32_t*)i)
			{
				if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived)
					break;
				if(memcmp(bq->ContentBoundary, i + sizeof(BeginChain), bq->ContentBoundaryLen) == 0)
				{
					auto Written = LqFileWrite(OutFd, Buf, Checked);
					if(Written < 0)
					{
						c->ActionResult = LQHTTPACT_RES_FILE_WRITE_ERR;
						goto lblOut;
					}
					InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
					c->ActionResult = LQHTTPACT_RES_OK;
					goto lblOut;
				}
			}
		}
		auto Written = LqFileWrite(OutFd, Buf, Checked);
		if(Written < Checked)
		{
			c->ActionResult = LQHTTPACT_RES_FILE_WRITE_ERR;
			goto lblOut;
		}
		InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
		if(InTactReaded > MaxReciveSizeInTact)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblOut;
		}
	}

lblOut:
	c->ReadedBodySize += InTactReaded;
	for(auto mh = c->Query.MultipartHeaders; mh != nullptr; mh = mh->Query.MultipartHeaders)
		mh->ReadedBodySize += InTactReaded;
}

static void LqHttpRcvMultipartReadInStream(LqHttpConn* c)
{
	char Buf[LQCONN_MAX_LOCAL_SIZE];
	static const int32_t BeginChain = *(int32_t*)"\r\n--";

	auto mph = c->Query.MultipartHeaders;
	if(mph == nullptr)
	{
		c->ActionResult = LQHTTPACT_RES_INVALID_HEADER;
		return;
	}
	auto bq = &c->Query;
	for(; ; mph = mph->Query.MultipartHeaders)
	{
		if(mph->Query.ContentBoundary)
			bq = &mph->Query;
		if(mph->Query.MultipartHeaders == nullptr)
			break;
	}
	size_t BoundaryChainSize = sizeof(BeginChain) +  bq->ContentBoundaryLen;
	LqFileSz LeftContentLen = mph->Query.ContentLen - mph->ReadedBodySize;
	LqFileSz InTactReaded = 0;
	LqFileSz MaxReciveSizeInTact = c->CommonConn.Proto->MaxReciveInSingleTime;
	while(true)
	{
		size_t CurSizeRead = LeftContentLen - InTactReaded;
		if(CurSizeRead <= BoundaryChainSize)
		{
			if(CurSizeRead > 0)
			{
				auto Skipped = LqHttpConnRecive_Native(c, Buf, CurSizeRead, 0);
				if(Skipped < 0)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblOut;
				}
				InTactReaded += Skipped;
				if(Skipped < CurSizeRead)
				{
					c->ActionResult = LQHTTPACT_RES_PARTIALLY;
					goto lblOut;
				}
			}
			c->ActionResult = LQHTTPACT_RES_MULTIPART_END;
			goto lblOut;
		}
		if(CurSizeRead > (sizeof(Buf) - sizeof(BeginChain)))
			CurSizeRead = (sizeof(Buf) - sizeof(BeginChain));
		if((CurSizeRead + InTactReaded) > LeftContentLen)
		{
			if((CurSizeRead = LeftContentLen - InTactReaded) <= 0)
			{
				c->ActionResult = LQHTTPACT_RES_OK;
				goto lblOut;
			}
		}
		auto PeekRecived = LqHttpConnRecive_Native(c, Buf, CurSizeRead, MSG_PEEK);
		if(PeekRecived == -1)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblOut;
		}
		*(int32_t*)(Buf + PeekRecived) = BeginChain;  //Set terminator for loop
		size_t Checked = 0;
		for(register char* i = Buf; ; i++)
		{
			if(BeginChain == *(int32_t*)i)
			{
				if(((Checked = i - Buf) + BoundaryChainSize) > PeekRecived) //If have terninator
					break;
				if(memcmp(bq->ContentBoundary, i + sizeof(BeginChain), bq->ContentBoundaryLen) == 0)
				{
					auto Written = LqSbufWrite(&c->Query.Stream, Buf, Checked);
					if(Written < Checked)
					{
						c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
						goto lblOut;
					}
					InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
					c->ActionResult = LQHTTPACT_RES_OK;
					goto lblOut;
				}
			}
		}
		auto Written = LqSbufWrite(&c->Query.Stream, Buf, Checked);
		if(Written < Checked)
		{
			c->ActionResult = LQHTTPACT_RES_NOT_ALLOC_MEM;
			goto lblOut;
		}
		InTactReaded += LqHttpConnRecive_Native(c, Buf, Checked, 0);
		if(InTactReaded > MaxReciveSizeInTact)
		{
			c->ActionResult = LQHTTPACT_RES_PARTIALLY;
			goto lblOut;
		}
	}

lblOut:
	c->ReadedBodySize += InTactReaded;
	for(auto mh = c->Query.MultipartHeaders; mh != nullptr; mh = mh->Query.MultipartHeaders)
		mh->ReadedBodySize += InTactReaded;
}

static bool LqHttpMultipartAddHeaders(LqHttpMultipartHeaders** CurMultipart, const char* Buf, size_t BufLen)
{
	LqHttpMultipartHeaders* New;
	if(*CurMultipart == nullptr)
	{
		New = (LqHttpMultipartHeaders*)malloc(sizeof(LqHttpMultipartHeaders) + BufLen + 1);
		if(New == nullptr)
			return false;
		memset(New, 0, sizeof(LqHttpMultipartHeaders));
		New->Query.OutFd = -1;
		New->Query.ContentLen = LQ_MAX_CONTENT_LEN;
	} else
	{
		auto Old = CurMultipart[0];
		New = (LqHttpMultipartHeaders*)realloc(Old, sizeof(LqHttpMultipartHeaders) + Old->BufSize + BufLen + 1);
		if(New == nullptr)
			return false;
	}
	*CurMultipart = New;
	memcpy(New->Buf + New->BufSize, Buf, BufLen);
	New->BufSize += BufLen;
	return true;
}

static void LqHttpQurReadHeaders(LqHttpConn* c)
{
	int CountReaded;
	size_t NewAllocSize, ReadSize, SizeAllReaded, CountLines;
	char *EndQuery, *EndStartLine, *StartMethod = "", *EndMethod = StartMethod,
		*StartUri = StartMethod, *EndUri = StartMethod,
		*StartVer = StartMethod, *EndVer = StartMethod,
		*SchemeStart, *SchemeEnd, *UserInfoStart, *UserInfoEnd,
		*HostStart, *HostEnd, *PortStart, *PortEnd,
		*DirStart, *DirEnd, *QueryStart, *QueryEnd,
		*FragmentStart, *FragmentEnd, *End, TypeHost;

	LqHttpProto* CurReg = LqHttpGetReg(c);
	auto q = &c->Query;


lblContinueRead:
	NewAllocSize = q->PartLen + 2045;
	if(c->BufSize < NewAllocSize)
	{
		if(!LqHttpConnBufferRealloc(c, (NewAllocSize > CurReg->Base.MaxHeadersSize)? CurReg->Base.MaxHeadersSize: NewAllocSize))
		{
			//Error allocate memory
			c->Flags = LQHTTPCONN_FLAG_CLOSE;
			LqHttpRspError(c, 500);
			return;
		}
	}

	ReadSize = c->BufSize - q->PartLen - 2;
	if((CountReaded = LqHttpConnRecive_Native(c, c->Buf + q->PartLen, ReadSize, MSG_PEEK)) <= 0)
	{
		if((CountReaded < 0) && !LQCONN_IS_WOULD_BLOCK)
		{
			//Error reading
			c->Flags = LQHTTPCONN_FLAG_CLOSE;
			LqHttpRspError(c, 500);
			return;
		}
		//Is empty buffer
		c->ActionResult = LQHTTPACT_RES_PARTIALLY;
		return;
	}

	SizeAllReaded = q->PartLen + CountReaded;
	c->Buf[SizeAllReaded] = '\0';
	if((EndQuery = LqHttpPrsGetEndHeaders(c->Buf + q->PartLen, &CountLines)) == nullptr)
	{
		if((SizeAllReaded + 4) >= CurReg->Base.MaxHeadersSize)
		{
			//Error request to large
			q->PartLen = 0;
			c->Flags = LQHTTPCONN_FLAG_CLOSE;
			LqHttpRspError(c, 413);
			return;
		} else
		{
			LqHttpConnRecive_Native(c, c->Buf + q->PartLen, CountReaded, 0);
			q->PartLen = SizeAllReaded;
			//If data spawn in buffer, while we reading.
			if(LqHttpConnCountPendingData(c) > 0)
				goto lblContinueRead;
			//Is read part
			return;
		}
	}

	LqHttpConnRecive_Native(c, c->Buf + q->PartLen, EndQuery - (c->Buf + q->PartLen), 0);
	*EndQuery = '\0';
	q->PartLen = 0;

	/*
	Read start line.
	*/

	switch(
	LqHttpPrsStartLine
		(
			c->Buf,
			StartMethod, EndMethod,
			StartUri, EndUri,
			StartVer, EndVer,
			EndStartLine
		)
	)
	{
		case READ_START_LINE_ERR:
			//Err invalid start line
			c->Flags = LQHTTPCONN_FLAG_CLOSE;
			LqHttpRspError(c, 400);
			return;
	}
	if(
		LqHttpPrsUrl
		(
			StartUri,
			SchemeStart, SchemeEnd,
			UserInfoStart, UserInfoEnd,
			HostStart, HostEnd,
			PortStart, PortEnd,
			DirStart, DirEnd,
			QueryStart, QueryEnd,
			FragmentStart, FragmentEnd,
			End, TypeHost,
			[](void* QueryData, char* StartKey, char* EndKey, char* StartVal, char* EndVal)
			{
				if(((LqHttpQuery*)QueryData)->Arg == nullptr)
					((LqHttpQuery*)QueryData)->Arg = StartKey;
				((LqHttpQuery*)QueryData)->ArgLen = EndVal - ((LqHttpQuery*)QueryData)->Arg;
			},
			q
		) != READ_URL_SUCCESS
	)
	{
		if(*StartUri == '*')
			DirEnd = (DirStart = StartUri) + 1;
		else
		{
			//Err invalid uri in start line
			LqHttpRspError(c, 400);
			return;
		}
	} else
	{
		char* NewEnd;
		if(UserInfoStart != nullptr)
		{
			q->UserInfo = LqHttpPrsEscapeDecode(UserInfoStart, UserInfoEnd, NewEnd);
			q->UserInfoLen = NewEnd - UserInfoStart;
		}
		if(HostStart != nullptr)
		{
			q->Host = LqHttpPrsEscapeDecode(HostStart, HostEnd, NewEnd);
			q->HostLen = NewEnd - HostStart;
		}
		if(FragmentStart != nullptr)
		{
			q->Fragment = LqHttpPrsEscapeDecode(FragmentStart, FragmentEnd, NewEnd);
			q->FragmentLen = NewEnd - FragmentStart;
		}
		if(DirStart != nullptr)
		{
			q->Path = LqHttpPrsEscapeDecode(DirStart, DirEnd, NewEnd);
			q->PathLen = NewEnd - DirStart;
		}
	}
	if(StartVer < EndVer)
	{
		q->ProtoVer = StartVer;
		q->ProtoVerLen = EndVer - StartVer;
	}
	q->Method = StartMethod;
	q->MethodLen = EndMethod - StartMethod;


	/*
	Read all headers
	*/

	q->HeadersEnd = SizeAllReaded + 1;
	size_t RecognizedHeadersCount = 0;
 	for(;;)
	{
		char *StartKey, *EndKey, *StartVal, *EndVal, *EndHeader;
		LqHttpPrsHdrStatEnm r = LqHttpPrsHeader(EndStartLine, StartKey, EndKey, StartVal, EndVal, EndHeader);
		switch(r)
		{
			case READ_HEADER_SUCCESS:
			{
				char t = *EndKey;
				*EndKey = '\0';
				LqHttpParseHeader(c, &c->Query, StartKey, StartVal, EndVal, &RecognizedHeadersCount);
				*EndKey = t;
				EndStartLine = EndHeader;
			}
			continue;
			case READ_HEADER_ERR:
			{
				//Err invalid header in query
				LqHttpRspError(c, 400);
				return;
			}
		}
		if(r == READ_HEADER_END) break;
	}
	LqHttpPthRecognize(c);
	c->ActionState = LQHTTPACT_STATE_HANDLE_PROCESS;
	c->ActionResult = LQHTTPACT_RES_OK;
	if(auto MethodHandler = LqHttpMdlGetByConn(c)->GetActEvntHandlerProc(c))
		c->EventAct = MethodHandler;
	else
		LqHttpRspError(c, 405);
}

static void LqHttpParseHeader(LqHttpConn* c, LqHttpQuery* q, char* StartKey, char* StartVal, char* EndVal, size_t* CountHeders)
{
	LQSTR_SWITCH_I(StartKey)
	{
		LQSTR_CASE_I("connection");
		{
			if(LqStrUtf8CmpCaseLen(StartVal, "close", sizeof("close") - 1))
				c->Flags |= LQHTTPCONN_FLAG_CLOSE;
		}
		break;
		LQSTR_CASE_I("content-length");
		{
			unsigned long long InputLen = (unsigned long long)0;
			sscanf(StartVal, "%llu", &InputLen);
			q->ContentLen = InputLen;
			//!!!!!!!!!!
		}
		break;
		LQSTR_CASE_I("host");
		{
			q->Host = StartVal;
			q->HostLen = EndVal - StartVal;
		}
		break;
		LQSTR_CASE_I("content-type");
		{
			if(LqStrUtf8CmpCaseLen(StartVal, "multipart/form-data", sizeof("multipart/form-data") - 1))
			{
				for(char* i = StartVal + sizeof("multipart/form-data"), *m = EndVal - (sizeof("boundary=") - 1); i < m; i++)
				{
					if(LqStrUtf8CmpCaseLen(i, "boundary=", sizeof("boundary=") - 1))
					{
						i += (sizeof("boundary=") - 1);
						for(; *i == ' '; i++);
						char* v = i;
						for(; (*i != ';') && (*i != ' ') && (*i != '\0') && !((*i == '\r') && (i[1] == '\n')); i++);
						if((i - v) <= 0)
							break;
						q->ContentBoundaryLen = i - (q->ContentBoundary = v);
						q->ContentBoundary = v;
						break;
					}
				}
			}
		}
		break;
	}
}

