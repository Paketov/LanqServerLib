/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* HttpPrs ... - Headers parsing functions.
*/

#ifndef __LQ_HTTP_PRS_H_HAS_INCLUDED__
#define __LQ_HTTP_PRS_H_HAS_INCLUDED__

#include "LqHttp.h"
#include "LqOs.h"
#include "LqDef.h"

LQ_EXTERN_C_BEGIN

enum LqHttpPrsHdrStatEnm
{
    LQPRS_HDR_SUCCESS,
    LQPRS_HDR_ERR,
    LQPRS_HDR_END
};

enum LqHttpPrsStartLineStatEnm
{
    LQPRS_START_LINE_SUCCESS,
    LQPRS_START_LINE_ERR
};

enum LqHttpPrsUrlStatEnm
{
    LQPRS_URL_SUCCESS,
    LQPRS_URL_ERR_SYMBOLIC_HOST_NAME,
    LQPRS_URL_ERR_IPv6_HOST_NAME,
    LQPRS_URL_ERR_USER_INFO,
    LQPRS_URL_ERR_PORT,
    LQPRS_URL_ERR_DIR,
    LQPRS_URL_ERR_QUERY,
};

LQ_IMPORTEXPORT LqHttpPrsUrlStatEnm LQ_CALL LqHttpPrsUrl
(
    char* String,
    char** SchemeStart, char** SchemeEnd,
    char** UserInfoStart, char** UserInfoEnd,
    char** HostStart, char** HostEnd,
    char** PortStart, char** PortEnd,
    char** DirStart, char** DirEnd,
    char** QueryStart, char** QueryEnd,
    char** FragmentStart, char** FragmentEnd,
    char** End, char* TypeHost,
    void(*AddQueryProc)(void* UserData, char* StartKey, char* EndKey, char* StartVal, char* EndVal),
    void* UserData = nullptr
);

LQ_IMPORTEXPORT LqHttpPrsStartLineStatEnm LQ_CALL LqHttpPrsStartLine
(
    char* String,
    char** sMethod, char** eMethod,
    char** sUri, char** eUri,
    char** sVer, char** eVer,
    char** End
);

LQ_IMPORTEXPORT LqHttpPrsHdrStatEnm LQ_CALL LqHttpPrsHeader(char* String, char** KeyStart, char** KeyEnd, char** ValStart, char** ValEnd, char** End);

LQ_IMPORTEXPORT char* LQ_CALL LqHttpPrsEscapeDecodeSz(char* Dest, const char* Source);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpPrsEscapeDecode(char* Source, char* EndSource, char** NewEnd);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpPrsGetEndHeaders(char* Buf, size_t *CountLines);
LQ_IMPORTEXPORT const char* LQ_CALL LqHttpPrsGetMsgByStatus(int Status);

LQ_EXTERN_C_END

#endif