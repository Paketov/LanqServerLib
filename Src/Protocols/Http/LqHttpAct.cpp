
#include "LqHttp.h"
#include "LqHttp.hpp"
#include "LqHttpConn.h"
#include "LqHttpAct.h"
#include "LqFileTrd.h"
#include "LqFileChe.hpp"
#include "LqHttpMdlHandlers.h"
#include "LqHttpPrs.h"
#include "LqHttpRsp.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#include <string.h>
#include <memory.h>

LQ_EXTERN_C char* LQ_CALL LqHttpActSwitchToRspAndSetStartLine(LqHttpConn* c, int StatusCode)
{
	LqHttpActSwitchToRsp(c);
	char* Buf = LqHttpRspHdrResize(c, 200);
	if(Buf == nullptr)
		return nullptr;
	auto Written = snprintf(Buf, 199, "HTTP/%s %i %s\r\n\r\n", LqHttpProtoGetByConn(c)->HTTPProtoVer, StatusCode, LqHttpPrsGetMsgByStatus(StatusCode));
	c->Response.Status = StatusCode;
	return LqHttpRspHdrResize(c, Written);
}


void LqHttpActSwitchToRcvHdrs(LqHttpConn* c)
{
	LqHttpActKeepOnlyHeaders(c);
	LqHttpConnPthRemove(c);
	memset(&c->Query, 0, sizeof(c->Query));
	c->ActionResult = LQHTTPACT_RES_BEGIN;
	c->ActionState = LQHTTPACT_STATE_GET_HDRS;
	c->Flags &= ~LQHTTPCONN_FLAG_NO_BODY;
	c->ReadedBodySize = 0;
	c->WrittenBodySize = 0;
	LqHttpEvntCloseSet(c, LqHttpMdlHandlersEmpty);
	LqHttpEvntActSet(c, LqHttpMdlHandlersEmpty);
}

LQ_EXTERN_C void LQ_CALL LqHttpActSwitchToRsp(LqHttpConn* c)
{
	LqFileSz ReciveLen = (LqHttpActGetClassByConn(c) == LQHTTPACT_CLASS_QER) ? lq_max(c->Query.ContentLen - c->ReadedBodySize, 0) : 0;
	LqHttpActKeepOnlyHeaders(c);
	c->ActionResult = LQHTTPACT_RES_BEGIN;
	c->ActionState = LQHTTPACT_STATE_RSP;
	memset(&c->Response, 0, sizeof(c->Response));
	c->Response.CountNeedRecive = ReciveLen;
}

LQ_EXTERN_C void LQ_CALL LqHttpActSwitchToClose(LqHttpConn* c)
{
	LqHttpActKeepOnlyHeaders(c);
	c->ActionResult = LQHTTPACT_RES_BEGIN;
	c->ActionState = LQHTTPACT_STATE_CLS_CONNECTION;
}

LQ_EXTERN_C void LQ_CALL LqHttpActSwitchToManualRsp(LqHttpConn* c)
{
	LqHttpActSwitchToRsp(c);
	c->ActionResult = LQHTTPACT_RES_BEGIN;
	c->ActionState = LQHTTPACT_STATE_RSP_HANDLE_PROCESS;
}

LQ_EXTERN_C void LQ_CALL LqHttpActSwitchToManualRcv(LqHttpConn* c)
{
	LqHttpActKeepOnlyHeaders(c);
	c->ActionResult = LQHTTPACT_RES_BEGIN;
	c->ActionState = LQHTTPACT_STATE_RCV_HANDLE_PROCESS;
}

LQ_EXTERN_C void LQ_CALL LqHttpActKeepOnlyHeaders(LqHttpConn* c)
{
	switch(c->ActionState)
	{
		case LQHTTPACT_STATE_MULTIPART_RCV_STREAM:
		case LQHTTPACT_STATE_RCV_STREAM:
			LqSbufUninit(&c->Query.Stream);
			goto lblRcvClearMultipart;
		case LQHTTPACT_STATE_MULTIPART_RCV_FILE:
		case LQHTTPACT_STATE_RCV_FILE:
			LqFileTrdCancel(c->Query.OutFd);
			goto lblRcvClearMultipart;
		case LQHTTPACT_STATE_MULTIPART_RCV_HDRS:
			if(c->_Reserved != 0)
			{
				free((void*)c->_Reserved);
				c->_Reserved = 0;
			}
		case LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS:
		case LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS:
lblRcvClearMultipart:
			if(c->Query.MultipartHeaders != nullptr)
			{
				auto Cur = c->Query.MultipartHeaders;
				auto Next = Cur->Query.MultipartHeaders;
				while(true)
				{
					free(Cur);
					if(Next == nullptr)
						break;
					Cur = Next;
					Next = Cur->Query.MultipartHeaders;
				}
				c->Query.MultipartHeaders = nullptr;
			}
			c->ActionState = LQHTTPACT_STATE_CLS_CONNECTION; //Set close conn. If the user wants another he can set this after the call
			break;
		case LQHTTPACT_STATE_RSP_CACHE:
			LqHttpGetReg(c)->Cache.Release((LqFileChe<LqCachedFileHdr>::CachedFile*)c->Response.CacheInterator);
			c->ActionState = LQHTTPACT_STATE_RSP; //Set response only headers
			break;
		case LQHTTPACT_STATE_RSP_FD:
			LqFileClose(c->Response.Fd);
			c->ActionState = LQHTTPACT_STATE_RSP;
			break;
		case LQHTTPACT_STATE_RSP_STREAM:
			LqSbufUninit(&c->Response.Stream);
			c->ActionState = LQHTTPACT_STATE_RSP;
			break;
	}
}
