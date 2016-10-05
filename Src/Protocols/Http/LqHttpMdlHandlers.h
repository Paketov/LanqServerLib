/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpMdlHandlers... - Default module handlers.
*/

#ifndef __LQ_HTTP_MDL_HANDLERS_H_HAS_INCLUDED__
#define __LQ_HTTP_MDL_HANDLERS_H_HAS_INCLUDED__

#include "LqHttp.h"
#include "LqOs.h"

#if defined(LANQBUILD)
void LQ_CALL LqHttpMdlHandlersEmpty(LqHttpConn*);
#endif



void LQ_CALL LqHttpMdlHandlersCacheInfo
(
    const char* lqain Path,
    LqHttpConn* lqain lqaopt Connection,

    char* lqaout lqaopt CacheControlDestBuf, /* If after call CacheControlDestBuf == "", then Cache-Control no include in response headers. */
    size_t CacheControlDestBufSize,

    char* lqaout lqaopt EtagDestBuf, /* If after call EtagDestBuf == "", then Etag no include in response headers. */
    size_t EtagDestBufSize,

    LqTimeSec* lqaout lqaopt LastModif, /* Local time. If after call LastModif == -1, then then no response Last-Modified. */

    LqTimeSec* lqaout lqaopt Expires,

    LqFileStat const* lqain lqaopt Stat /*(Something sends for optimizing)*/
);

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqHttpExtensionMime
{
    size_t  CountExt;
    char**  Ext;
    size_t  CountMime;
    char**  Mime;
} LqHttpExtensionMime;

#pragma pack(pop)

const LqHttpExtensionMime* LqHttpMimeExtension(const char* lqain Str);

void LQ_CALL LqHttpMdlHandlersMime
(
    const char* lqain Path,
    LqHttpConn* lqain lqaout Connection,
    char* lqaout lqaopt MimeDestBuf,
    size_t MimeDestBufLen,
    LqFileStat const* lqain lqaopt Stat/*(Something sends for optimizing)*/
);

int LQ_CALL LqHttpMdlHandlersError(LqHttpConn* lqain c, int lqain Code);
int LQ_CALL LqHttpMdlHandlersStatus(LqHttpConn* lqain c, int lqain Code);
void LQ_CALL LqHttpMdlHandlersServerName(LqHttpConn* lqain c, char* lqaout NameBuf, size_t NameBufSize);
void LQ_CALL LqHttpMdlHandlersResponseRedirection(LqHttpConn* lqain c);
LqHttpEvntHandlerFn LQ_CALL LqHttpMdlHandlersGetMethod(LqHttpConn* lqain c);
void LQ_CALL LqHttpMdlHandlersAllowMethods(LqHttpConn* lqain c, char* lqaout MethodBuf, size_t MethodBufSize);
void LQ_CALL LqHttpMdlHandlersNonce(LqHttpConn* lqain c, char* lqaout MethodBuf, size_t MethodBufSize);

#endif
