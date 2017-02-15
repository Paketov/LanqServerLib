/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* HttpPrs ... - HTTP headers parsing functions.
*/

#include "LqOs.h"
#include "LqConn.h"
#include "LqDef.hpp"
#include "LqStr.hpp"
#include <ctype.h>

#include "LqHttpPrs.h"


#define IS_DIGIT(c)  ((*(c) >= '0') && (*(c) <= '9'))
#define IS_ENG_ALPHA(c) ((*(c) >= 'a') && (*(c) <= 'z') || (*(c) >= 'A') && (*(c) <= 'Z'))
#define IS_HEX(c) (IS_DIGIT(c) || (*(c) >= 'A') && (*(c) <= 'F') || (*(c) >= 'a') && (*(c) <= 'f'))

#define IS_RESERVED(c) (\
    (*(c) == ';') || (*(c) == '/') || \
    (*(c) == '?') || (*(c) == ':') || \
    (*(c) == '@') || (*(c) == '&') || \
    (*(c) == '=') || (*(c) == '+') || \
    (*(c) == '$') || (*(c) == ',') || \
    (*(c) == '#'))

#define IS_UNWISE(c) (\
    (*(c) == '{') || (*(c) == '}') ||\
    (*(c) == '|') || (*(c) == '\\') ||\
    (*(c) == '^') || (*(c) == '[') || \
    (*(c) == ']') || (*(c) == '`'))

#define IS_DELIMS(c) (\
    (*(c) == '<') || (*(c) == '>') ||\
    (*(c) == '#') || (*(c) == '%') ||\
    (*(c) == '"'))

static char* IS_ESCAPE(const char* c) {
	if((*c == '%') && IS_HEX(c + 1) && IS_HEX(c + 2))
		return (char*)c + 3;
	return nullptr;
}

static char* IS_UNRESERVED(const char* c) {
	if((*(c) == '-') ||
	   (*(c) == '.') || (*(c) == '_') ||
	   (*(c) == '~') || (*(c) == '!') ||
	   (*(c) == '*') || (*(c) == '\'') ||
	   (*(c) == '(') || (*(c) == ')'))
		return (char*)c + 1;
	return LqStrUtf8IsAlphaNum(c);
}

static char* IS_USER_INFO(const char* c) {
	if(auto r = IS_UNRESERVED(c))
		return r;
	if(auto r = IS_ESCAPE(c))
		return r;
	if((*(c) == ';') || (*(c) == ':') ||
	   (*(c) == '&') || (*(c) == '=') ||
	   (*(c) == '+') || (*(c) == '$') ||
	   (*(c) == ','))
		return (char*)c + 1;
	return nullptr;
}

static char* IS_PCHAR(const char* c) {
	if(auto r = IS_UNRESERVED(c))
		return r;
	if(auto r = IS_ESCAPE(c))
		return r;
	if((*(c) == ':') || (*(c) == '@') ||
	   (*(c) == '&') || (*(c) == '=') ||
	   (*(c) == '+') || (*(c) == '$') ||
	   (*(c) == ','))
		return (char*)c + 1;
	return nullptr;
}
static char* IS_URIC_WITHOUT_EQ_AMP(const char* c) {
	if(auto r = IS_UNRESERVED(c))
		return r;
	if(auto r = IS_ESCAPE(c))
		return r;
	if((*(c) == ';') || (*(c) == '/') ||
	   (*(c) == '?') || (*(c) == ':') ||
	   (*(c) == '@') || (*(c) == '+') ||
	   (*(c) == '$') || (*(c) == ','))
		return (char*)c + 1;
	return nullptr;
}
static char* IS_URIC(const char* c) {
	if(auto r = IS_UNRESERVED(c))
		return r;
	if(auto r = IS_ESCAPE(c))
		return r;
	if(IS_RESERVED(c))
		return (char*)c + 1;
	return nullptr;
}

static char* IS_DIR(const char* c) {
	if((*c == '/') || (*c == ';'))
		return (char*)c + 1;
	if(auto r = IS_PCHAR(c))
		return r;
	return nullptr;
}


LQ_EXTERN_C LqHttpPrsUrlStatEnm LQ_CALL LqHttpPrsHostPort
(
	char* String,
	char** HostStart, char** HostEnd,
	char** PortStart, char** PortEnd,
	char* HostType
) {
	char* r, *c = String, *t = c;

	*HostStart = NULL;
	*HostEnd = NULL;
	*PortStart = NULL;
	*PortEnd = NULL;

	if(LqStrUtf8IsAlpha(c)) {
		/*Read symbolic host name*/
lblRep:

		for(char* r;;) {
			if((r = LqStrUtf8IsAlphaNum(c)) != nullptr)
				c = r;
			else if(*c == '-')
				c++;
			else
				break;

		}
		if(*(c - 1) == '-')
			return LQPRS_URL_ERR_SYMBOLIC_HOST_NAME;
		if((*c == '.') && (LqStrUtf8IsAlphaNum(c + 1) != nullptr)) {
			c++;
			goto lblRep;
		} else {
			*HostStart = t;
			*HostEnd = c;
			*HostType = 's';
			goto lblPort;
		}
	} else if(*c == '[') {
		/*Read IPv6 host name*/
		c++;
		unsigned Count = 0;
		bool IsHaveReduct = false;
		if(*c == ':') {
			if(c[1] == ':') {
				c += 2;
				IsHaveReduct = true;
				if(*c == ']') goto lblIPv6Continue;
			} else
				return LQPRS_URL_ERR_IPv6_HOST_NAME;
		}
		while(true) {
			if(IS_HEX(c)) {
				Count++;
				char* t = c++;
				for(; IS_HEX(c); c++);
				if(c > (t + 4)) return LQPRS_URL_ERR_IPv6_HOST_NAME;
				if(*c == ':') {
					c++;
					if(*c == ':') {
						if(IsHaveReduct)
							return LQPRS_URL_ERR_IPv6_HOST_NAME;
						IsHaveReduct = true;
						c++;
						if(*c == ']')
							break;
					} else if(!IS_HEX(c))
						return LQPRS_URL_ERR_IPv6_HOST_NAME;
				} else if(*c == ']')
					break;
				else
					return LQPRS_URL_ERR_IPv6_HOST_NAME;
			} else
				return LQPRS_URL_ERR_IPv6_HOST_NAME;
		}
lblIPv6Continue:
		if(IsHaveReduct) {
			if(Count >= 8)
				return LQPRS_URL_ERR_IPv6_HOST_NAME;
		} else if(Count < 8)
			return LQPRS_URL_ERR_IPv6_HOST_NAME;
		*HostStart = t;
		*HostEnd = t = ++c;
		*HostType = '6';
		goto lblPort;
	} else
		return LQPRS_URL_ERR_SYMBOLIC_HOST_NAME;

	return LQPRS_URL_SUCCESS;
lblPort:
	/*Read port*/
	t = c;
	if(*c == ':') {
		c++;
		unsigned d = 0;
		for(char* m = c + 5; IS_DIGIT(c) && (c < m); c++)
			d = d * 10 + (*c - '0');
		if(d > 65535) return LQPRS_URL_ERR_PORT;
		*PortStart = t + 1;
		*PortEnd = c;
	}
	return LQPRS_URL_SUCCESS;
}

/*


*/
LQ_EXTERN_C LqHttpPrsUrlStatEnm LQ_CALL LqHttpPrsUrl
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
	void* UserData
) {
	/*
	Based on RFC 3987 https://www.ietf.org/rfc/rfc3987.txt
	*/

	char* c = String, *t,
		*StartScheme = nullptr, *EndScheme = nullptr,
		*StartUserInfo = nullptr, *EndUserInfo = nullptr,
		*StartHost = nullptr, *EndHost = nullptr,
		*StartPort = nullptr, *EndPort = nullptr,
		*StartDir = nullptr, *EndDir = nullptr,
		*StartQuery = nullptr, *EndQuery = nullptr,
		*StartFragment = nullptr, *EndFragment = nullptr,
		HostType = ' ';
	for(; (*c == ' ') || (*c == '\t'); c++);
	t = c;
	/*Read scheme*/
	if(IS_ENG_ALPHA(c)) {
		for(; IS_ENG_ALPHA(c) || IS_DIGIT(c) || (*c == '.') || (*c == '+') || (*c == '-'); c++);
		if((c[0] == ':') && (c[1] == '/') && (c[2] == '/')) {
			StartScheme = t;
			EndScheme = c;
			c += 3;
		} else
			c = t;
	}
	/*Read user info*/
	t = c;
	for(; auto r = IS_USER_INFO(c); c = r);
	if((*c == '@') && (c != t)) {
		StartUserInfo = t;
		EndUserInfo = c;
		c++;
	} else
		c = t;
	t = c;
	/*Parse host name*/
	if(IS_DIGIT(c)) {
		/*Read IPv4 host name*/
		for(unsigned i = 0; ; i++) {
			unsigned d = 0;
			for(char* m = c + 3; IS_DIGIT(c) && (c < m); c++)
				d = d * 10 + (*c - '0');
			if(d > 255) goto lblHostName;
			if(i >= 3) break;
			if(*c != '.') goto lblHostName;
			c++;
		}
		StartHost = t;
		EndHost = t = c;
		HostType = '4';
		goto lblPort;
	}
lblHostName:
	c = t;
	if(LqStrUtf8IsAlpha(c)) {
		/*Read symbolic host name*/
lblRep:

		for(char* r;;) {
			if((r = LqStrUtf8IsAlphaNum(c)) != nullptr)
				c = r;
			else if(*c == '-')
				c++;
			else
				break;

		}
		if(*(c - 1) == '-')
			return LQPRS_URL_ERR_SYMBOLIC_HOST_NAME;
		if((*c == '.') && (LqStrUtf8IsAlphaNum(c + 1) != nullptr)) {
			c++;
			goto lblRep;
		} else {
			StartHost = t;
			EndHost = c;
			HostType = 's';
			goto lblPort;
		}
	} else if(*c == '[') {
		/*Read IPv6 host name*/
		c++;
		unsigned Count = 0;
		bool IsHaveReduct = false;
		if(*c == ':') {
			if(c[1] == ':') {
				c += 2;
				IsHaveReduct = true;
				if(*c == ']') goto lblIPv6Continue;
			} else
				return LQPRS_URL_ERR_IPv6_HOST_NAME;
		}
		while(true) {
			if(IS_HEX(c)) {
				Count++;
				char* t = c++;
				for(; IS_HEX(c); c++);
				if(c > (t + 4)) return LQPRS_URL_ERR_IPv6_HOST_NAME;
				if(*c == ':') {
					c++;
					if(*c == ':') {
						if(IsHaveReduct)
							return LQPRS_URL_ERR_IPv6_HOST_NAME;
						IsHaveReduct = true;
						c++;
						if(*c == ']')
							break;
					} else if(!IS_HEX(c))
						return LQPRS_URL_ERR_IPv6_HOST_NAME;
				} else if(*c == ']')
					break;
				else
					return LQPRS_URL_ERR_IPv6_HOST_NAME;
			} else
				return LQPRS_URL_ERR_IPv6_HOST_NAME;
		}
lblIPv6Continue:
		if(IsHaveReduct) {
			if(Count >= 8)
				return LQPRS_URL_ERR_IPv6_HOST_NAME;
		} else if(Count < 8)
			return LQPRS_URL_ERR_IPv6_HOST_NAME;
		StartHost = t;
		EndHost = t = ++c;
		HostType = '6';
		goto lblPort;
	} else if(StartUserInfo != nullptr)
		return LQPRS_URL_ERR_USER_INFO;

	goto lblDir;
lblPort:
	/*Read port*/
	t = c;
	if(*c == ':') {
		c++;
		unsigned d = 0;
		for(char* m = c + 5; IS_DIGIT(c) && (c < m); c++)
			d = d * 10 + (*c - '0');
		if(d > 65535) return LQPRS_URL_ERR_PORT;
		StartPort = t + 1;
		EndPort = c;
	}
lblDir:
	t = c;
	/*Read directory*/
	if(*c == '/') {
		for(; auto r = IS_DIR(c); c = r);
		StartDir = t;
		EndDir = t = c;
	} else
		return LQPRS_URL_ERR_DIR;
	/*Read query*/
	if(*c == '?') {
		char ForEmptyArg = '\0';
		for(char *StartKey, *EndKey, *StartVal, *EndVal;;) {
			c++;
			StartKey = c;
			for(; auto r = IS_URIC_WITHOUT_EQ_AMP(c); c = r);
			if((EndKey = c) == StartKey) return LQPRS_URL_ERR_QUERY;
			if(*c == '=') {
				StartVal = ++c;
				for(; auto r = IS_URIC_WITHOUT_EQ_AMP(c); c = r);
				EndVal = c;
			} else {
				EndVal = StartVal = &ForEmptyArg;
			}
			if(AddQueryProc != NULL)
				AddQueryProc(UserData, StartKey, EndKey, StartVal, EndVal);
			if(*c != '&')
				break;
		}
		StartQuery = t + 1;
		EndQuery = t = c;
	}
	/*Read fragment*/
	if(*c == '#') {
		c++;
		for(; auto r = IS_URIC(c); c = r);
		StartFragment = t;
		EndFragment = t = c;
	}

	*SchemeStart = StartScheme; *SchemeEnd = EndScheme;
	*UserInfoStart = StartUserInfo; *UserInfoEnd = EndUserInfo;
	*HostStart = StartHost;  *HostEnd = EndHost;
	*PortStart = StartPort; *PortEnd = EndPort;
	*DirStart = StartDir; *DirEnd = EndDir;
	*QueryStart = StartQuery; *QueryEnd = EndQuery;
	*FragmentStart = StartFragment; *FragmentEnd = EndFragment;
	*End = t;
	*TypeHost = HostType;
	return LQPRS_URL_SUCCESS;
}

LQ_EXTERN_C LqHttpPrsStartLineStatEnm LQ_CALL LqHttpPrsStartLine
(
	char* String,
	char** sMethod, char** eMethod,
	char** sUri, char** eUri,
	char** sVer, char** eVer,
	char** End
) {
	char* c = String, *StartMethod;
	for(; (*c == '\r') || (*c == '\n'); c++);
	StartMethod = c;
	for(; ((*c >= 'A') && (*c <= 'Z')) || (*c == '-') || ((*c >= 'a') && (*c <= 'z')); c++);
	switch(*c) {
		case ' ': case '\t':
		{
			if(StartMethod == c) return LQPRS_START_LINE_ERR;   //If len method eq. 0
			*eMethod = c;
			*sMethod = StartMethod;
			c++;
			break;
		}
		default: return LQPRS_START_LINE_ERR;
	}
	for(; (*c == ' ') || (*c == '\t'); c++);
	*sUri = c;
	for(;; c++) {
		switch(*c) {
			case ' ': case '\t': goto lblOutLoop;
			case '\r':
				if(c[1] == '\n') {
					*c = '\0';
					*End = c + 2;
					return LQPRS_START_LINE_SUCCESS;
				} else if(c[1] != '\0')
					return LQPRS_START_LINE_ERR;
		}
	}
lblOutLoop:
	if(c == *sUri) return LQPRS_START_LINE_ERR;
	*eUri = c;
	c++;
	for(;; c++) {
		switch(*c) {
			case ' ': case '\t': continue;
			case '\r':
				if(*++c != '\n') return LQPRS_START_LINE_ERR;
				*End = c + 1;
				return LQPRS_START_LINE_SUCCESS;
			case 'H':
			{
				if((c[1] != 'T') || (c[2] != 'T') || (c[3] != 'P') || (c[4] != '/'))
					return LQPRS_START_LINE_ERR;
				*sVer = (c += 5);
				for(; (*c >= '0') && (*c <= '9') || (*c == '.'); c++);
				if(*sVer == c) return LQPRS_START_LINE_ERR;
				for(; (*c == ' ') || (*c == '\t'); c++);
				if(*c != '\r') return LQPRS_START_LINE_ERR;
				*eVer = c;
				if(*++c != '\n') return LQPRS_START_LINE_ERR;
				*End = c + 1;
				return LQPRS_START_LINE_SUCCESS;
			}
			default: return LQPRS_START_LINE_ERR;
		}
	}
	return LQPRS_START_LINE_SUCCESS;
}

LQ_EXTERN_C LqHttpPrsHdrStatEnm LQ_CALL LqHttpPrsHeader(char* String, char** KeyStart, char** KeyEnd, char** ValStart, char** ValEnd, char** End) {
	char* c = String, *StartKey, *EndKey, *StartVal, *EndVal;

	if((c[0] == '\r') && (c[1] == '\n')) return LQPRS_HDR_END;

	for(; (*c == ' ') || (*c == '\t'); c++);
	StartKey = c;
	for(; ((*c >= 'a') && (*c <= 'z')) || ((*c >= 'A') && (*c <= 'Z')) || (*c == '-'); c++);
	if(StartKey == c) return LQPRS_HDR_ERR;
	EndKey = c;

	for(; (*c == ' ') || (*c == '\t'); c++);
	if(*c != ':') return LQPRS_HDR_ERR;
	c++;
	for(; (*c == ' ') || (*c == '\t'); c++);

	StartVal = c;
	for(; (*c != '\r') && (*c != '\n') && (*c != '\0'); c++);
	if(StartVal == c) return LQPRS_HDR_ERR;
	EndVal = c;
	if((c[0] != '\r') || (c[1] != '\n')) return LQPRS_HDR_ERR;

	*End = c + 2;
	*KeyStart = StartKey, *KeyEnd = EndKey, *ValStart = StartVal, *ValEnd = EndVal;
	return LQPRS_HDR_SUCCESS;
}

LQ_EXTERN_C char* LQ_CALL LqHttpPrsEscapeDecode(char* Source, char* EndSource, char** NewEnd) {
	char c = *EndSource;
	*EndSource = '\0';
	*NewEnd = LqHttpPrsEscapeDecodeSz(Source, Source);
	*EndSource = c;
	return Source;
}

LQ_EXTERN_C char* LQ_CALL LqHttpPrsEscapeDecodeSz(char* Dest, const char* Source) {
	char* d = Dest;
	const char* c = Source;
	for(;; c++, d++) {
		if(*c == '%') {
			unsigned char v = c[1] - '0';
			if(v > 9) {
				v = c[1] - 'a';
				if(v > 5) {
					v = c[1] - 'A';
					if(v > 5)
						continue;
				}
				v += 10;
			}
			unsigned char b = c[2] - '0';
			if(b > 9) {
				b = c[2] - 'a';
				if(b > 5) {
					b = c[2] - 'A';
					if(b > 5)
						continue;
				}
				b += 10;
			}
			*d = char(b | (v << 4));
			c += 2;
		} else if((*d = *c) == '\0') {
			break;
		}
	}
	return d;
}

const static uint16_t TestEndLine = *(uint16_t*)"\r\n";

LQ_EXTERN_C char* LQ_CALL LqHttpPrsGetEndHeaders(char* Buf, size_t *CountLines) {
	size_t cl = 1;
	if((Buf[0] == '\0') || (Buf[1] == '\0') || (Buf[2] == '\0'))
		return nullptr;
	for(char* c = Buf; ; c++) {
		if(c[3] == '\0')
			return nullptr;
		if(*(uint16_t*)c == TestEndLine) {
			if(*(uint16_t*)(c + 2) == TestEndLine) {
				*CountLines = cl;
				return c + 4;
			}
			cl++;
		}

	}
}

LQ_EXTERN_C const char* LQ_CALL LqHttpPrsGetMsgByStatus(int Status) {
	switch(Status) {
		//Informational
		case 100:   return "Continue";
		case 101:   return "Switching Protocols";
		case 102:   return "Processing";
		case 105:   return "Name Not Resolved";

			//Success
		case 200:   return "OK";
		case 201:   return "Created";
		case 202:   return "Accepted";
		case 203:   return "Non-Authoritative Information";
		case 204:   return "No Content";
		case 205:   return "Reset Content";
		case 206:   return "Partial Content";
		case 207:   return "Multi-Status";
		case 226:   return "IM Used";

			//Redirection 
		case 301:   return "Moved Permanently";
		case 302:   return "Moved Temporarily";
		case 303:   return "See Other";
		case 304:   return "Not Modified";
		case 305:   return "Use Proxy";
		case 307:   return "Temporary Redirect";

			//Client Error 
		case 400:   return "Bad Request";
		case 401:   return "Unauthorized";
		case 402:   return "Payment Required";
		case 403:   return "Forbidden";
		case 404:   return "Not Found";
		case 405:   return "Method Not Allowed";
		case 406:   return "Not Acceptable";
		case 407:   return "Proxy Authentication Required";
		case 408:   return "Request Timeout";
		case 409:   return "Conflict";
		case 410:   return "Gone";
		case 411:   return "Length Required";
		case 412:   return "Precondition Failed";
		case 413:   return "Request Entity Too Large";
		case 414:   return "Request-URI Too Large";
		case 415:   return "Unsupported Media Type";
		case 416:   return "Requested Range Not Satisfiable";
		case 417:   return "Expectation Failed";
		case 418:   return "I'm a teapot"; //:)
		case 422:   return "Unprocessable Entity";
		case 423:   return "Locked";
		case 424:   return "Failed Dependency";
		case 425:   return "Unordered Collection";
		case 426:   return "Upgrade Required";
		case 428:   return "Precondition Required";
		case 429:   return "Too Many Requests";
		case 431:   return "Request Header Fields Too Large";
		case 434:   return "Requested host unavailable";
		case 449:   return "Retry With";
		case 451:   return "Unavailable For Legal Reasons";
		case 456:   return "Unrecoverable Error";

			//Server Error
		case 500:   return "Internal Server Error";
		case 501:   return "Not Implemented";
		case 502:   return "Bad Gateway";
		case 503:   return "Task Unavailable";
		case 504:   return "Gateway Timeout";
		case 505:   return "HTTP Version Not Supported";
		case 506:   return "Variant Also Negotiates";
		case 507:   return "Insufficient Storage";
		case 508:   return "Loop Detected";
		case 509:   return "Bandwidth Limit Exceeded";
		case 510:   return "Not Extended";
		case 511:   return "Network Authentication Required";
	}
	return "";
}
