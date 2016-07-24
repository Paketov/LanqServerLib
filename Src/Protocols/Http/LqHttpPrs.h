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
#include "LqDef.hpp"

enum LqHttpPrsHdrStatEnm
{
	READ_HEADER_SUCCESS,
	READ_HEADER_ERR,
	READ_HEADER_END
};

enum LqHttpPrsStartLineStatEnm
{
	READ_START_LINE_SUCCESS,
	READ_START_LINE_ERR
};

enum LqHttpPrsUrlStatEnm
{
	READ_URL_SUCCESS,
	READ_URL_ERR_SYMBOLIC_HOST_NAME,
	READ_URL_ERR_IPv6_HOST_NAME,
	READ_URL_ERR_USER_INFO,
	READ_URL_ERR_PORT,
	READ_URL_ERR_DIR,
	READ_URL_ERR_QUERY,
};


LqHttpPrsUrlStatEnm LqHttpPrsUrl
(
	char* String,
	char*& SchemeStart, char*& SchemeEnd,
	char*& UserInfoStart, char*& UserInfoEnd,
	char*& HostStart, char*& HostEnd,
	char*& PortStart, char*& PortEnd,
	char*& DirStart, char*& DirEnd,
	char*& QueryStart, char*& QueryEnd,
	char*& FragmentStart, char*& FragmentEnd,
	char*& End, char& TypeHost,
	void(*AddQueryProc)(void* UserData, char* StartKey, char* EndKey, char* StartVal, char* EndVal)
	= [](void*, char*, char*, char*, char*) {},
	void* UserData = nullptr
);

LqHttpPrsStartLineStatEnm LqHttpPrsStartLine
(
	char* String,
	char*& sMethod, char*& eMethod,
	char*& sUri, char*& eUri,
	char*& sVer, char*& eVer,
	char*& End
);

LqHttpPrsHdrStatEnm LqHttpPrsHeader(char* String, char*& KeyStart, char*& KeyEnd, char*& ValStart, char*& ValEnd, char*& End);

LqString& LqHttpPrsEscapeDecode(LqString& Source);
char* LqHttpPrsEscapeDecode(char* Source, char*& NewEnd);
char* LqHttpPrsEscapeDecode(char* Source, char* EndSource, char*& NewEnd);
char* LqHttpPrsGetEndHeaders(char* Buf, size_t *CountLines);
const char* LqHttpPrsGetMsgByStatus(int Status);


#endif