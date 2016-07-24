/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqStr... - Typical string implementation (basic for UTF-8 CP).
*/


#include "LqStr.hpp"
#include "LqDfltRef.hpp"
#include "LqCp.h"
#if defined(_MSC_VER)
#include "Windows.h"
#include <io.h>
#endif

#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>


LQ_EXTERN_C uint32_t LQ_CALL LqStrCharUtf16ToLower(uint32_t r)
{
#if defined(_MSC_VER)
	wchar_t Buf[3] = {0};
	if(r > 0xffff)
	{
		Buf[0] = r >> 16;
		Buf[1] = r & 0xffff;
	} else
	{
		Buf[0] = r;
	}
	CharLowerW(Buf);
	if(Buf[1])
		return ((uint32_t)Buf[0] << 16) | Buf[1];
	 else
		return Buf[0];
#else
	return towlower(r);
#endif
}

LQ_EXTERN_C uint32_t LQ_CALL LqStrCharUtf16ToUpper(uint32_t r)
{
#if defined(_MSC_VER)
	wchar_t Buf[3] = {0};
	if(r > 0xffff)
	{
		Buf[0] = r >> 16;
		Buf[1] = r & 0xffff;
	} else
	{
		Buf[0] = r;
	}
	CharUpperW(Buf);
	if(Buf[1])
		return ((uint32_t)Buf[0] << 16) | Buf[1];
	 else
		return Buf[0];
#else
	return towupper(r);
#endif
}

LQ_EXTERN_C uint32_t LQ_CALL LqStrCharRead(FILE* FileBuf)
{
#if defined(_MSC_VER)
	uint16_t Buf1[3] = {0};
	fflush(FileBuf);
	HANDLE h = (HANDLE)_get_osfhandle(_fileno(FileBuf));
	uint8_t Buf2[5] = {0};
	if(ReadConsoleW(h, Buf1, 1, LqDfltPtr(), nullptr) == FALSE)
		return -1;
	if(LqStrUtf16Count(Buf1[0]) >= 2)
	{
		if(ReadConsoleW(h, Buf1 + 1, 1, LqDfltPtr(), nullptr) == FALSE)
			return -1;
	}
	LqCpConvertFromWcs((wchar_t*)Buf1, (char*)Buf2, sizeof(Buf2) - 1);
	return LqStrUtf8StrToChar((char*)Buf2);
#else
	return getc(FileBuf);
#endif
}

LQ_EXTERN_C int LQ_CALL LqStrUtf8Count(char ch)
{
	if((uchar)ch <= 0x7f)
		return 1;
	 else if((uchar)ch <= 0xbf)
		return -1;
	else if((uchar)ch <= 0xdf)
		return 2;
	else if((uchar)ch <= 0xef)
		return 3;
	else
		return 4;
}

LQ_EXTERN_C int LQ_CALL LqStrUtf16Count(wchar_t ch)
{
	return (ch >= 0xd800 && ch <= 0xdbff) ? 2 : 1;
}

LQ_EXTERN_C char* LQ_CALL LqStrUtf8CharToStr(char* Dest, uint32_t ch)
{
	if(ch <= 0xff)
	{
		Dest[0] = ch;
		return Dest + 1;
	}
	if(ch <= 0xffff)
	{
		Dest[0] = (ch >> 8) & 0xff; Dest[1] = ch & 0xff;
		return Dest + 2;
	}
	if(ch <= 0xffffff)
	{
		Dest[0] = (ch >> 16) & 0xff; Dest[1] = (ch >> 8) & 0xff; Dest[2] = ch & 0xff;
		return Dest + 3;
	}
	Dest[0] = ch >> 24; Dest[1] = (ch >> 16) & 0xff; Dest[2] = (ch >> 8) & 0xff; Dest[3] = ch & 0xff;
	return Dest + 4;
}

LQ_EXTERN_C uint32_t LQ_CALL LqStrUtf8StrToChar(const char* Source)
{
	auto l = LqStrUtf8Count(Source[0]);
	const uint8_t * s = (uint8_t*)Source;
	switch(l)
	{
		case -1: return -1;
		case 1: return s[0];
		case 2: return ((uint32_t)s[0] << 8) | (uint32_t)s[1];
		case 3: return ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | (uint32_t)s[2];
		case 4: return ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | (uint32_t)s[3];
	}
}

LQ_EXTERN_C wchar_t* LQ_CALL LqStrUtf16CharToStr(wchar_t* Dest, uint32_t ch)
{
	if(ch <= 0xffff)
	{
		Dest[0] = ch;
		return Dest + 1;
	}
	Dest[0] = (ch >> 16) & 0xffff; Dest[1] = ch & 0xffff;
	return Dest + 2;
}

LQ_EXTERN_C uint32_t LQ_CALL LqStrUtf8toUtf16(const char **s, size_t Size)
{
	uint32_t CodePoint = 0;
	auto Fol = 0;
	for(const char* m = *s + Size; *s < m; )
	{
		unsigned char ch = **s;
		(*s)++;
		if(ch <= 0x7f)
		{
			CodePoint = ch;
			Fol = 0;
		} else if(ch <= 0xbf)
		{
			if(Fol > 0)
			{
				CodePoint = (CodePoint << 6) | (ch & 0x3f);
				--Fol;
			}
		} else if(ch <= 0xdf)
		{
			CodePoint = ch & 0x1f;
			Fol = 1;
		} else if(ch <= 0xef)
		{
			CodePoint = ch & 0x0f;
			Fol = 2;
		} else
		{
			CodePoint = ch & 0x07;
			Fol = 3;
		}
		if(Fol == 0)
		{
			if(CodePoint > 0xffff)
				return (uint32_t)(0xd800 + (CodePoint >> 10)) | ((0xdc00 + (CodePoint & 0x03ff)) << 16);
			 else
				return CodePoint;
			CodePoint = 0;
		}
	}
	return (uint32_t)-1;
}

LQ_EXTERN_C uint32_t LQ_CALL LqStrUtf16ToUtf8Char(uint32_t Source)
{
	uint8_t Buf[5] = {0};
	LqStrUtf16ToUtf8((char*)Buf, Source, 4);
	if(Buf[3])
		return (uint32_t)Buf[3] | ((uint32_t)Buf[2] << 8) | ((uint32_t)Buf[1] << 16) | ((uint32_t)Buf[0] << 24);
	if(Buf[2])
		return (uint32_t)Buf[2] | ((uint32_t)Buf[1] << 8) | ((uint32_t)Buf[0] << 16);
	if(Buf[1])
		return (uint32_t)Buf[1] | ((uint32_t)Buf[0] << 8);
	return (uint32_t)Buf[0];
}


LQ_EXTERN_C char* LQ_CALL LqStrUtf16ToUtf8(char* Dest, uint32_t Source, size_t Size)
{
	wchar_t In1 = Source >> 16, In2 = Source & 0xffff;
	uint32_t CodePoint = (In1 >= 0xd800 && In1 <= 0xdbff)? (((In1 - 0xd800) << 10) + 0x10000): 0;

	if(In2 >= 0xdc00 && In2 <= 0xdfff)
		CodePoint |= In2 - 0xdc00;
	else
		CodePoint = In2;
	if(CodePoint <= 0x7f)
	{
		if(Size < 1) return nullptr;
		*Dest = (char)CodePoint;
	} else if(CodePoint <= 0x7ff)
	{
		if(Size < 2) return nullptr;
		*Dest = (char)(0xc0 | ((CodePoint >> 6) & 0x1f));
		Dest++;
		*Dest = (char)(0x80 | (CodePoint & 0x3f));
		Dest++;
	} else if(CodePoint <= 0xffff)
	{
		if(Size < 3) return nullptr;
		*Dest = (char)(0xe0 | ((CodePoint >> 12) & 0x0f));
		Dest++;
		*Dest = (char)(0x80 | ((CodePoint >> 6) & 0x3f));
		Dest++;
		*Dest = (char)(0x80 | (CodePoint & 0x3f));
		Dest++;
	} else
	{
		if(Size < 4) return nullptr;
		*Dest = (char)(0xf0 | ((CodePoint >> 18) & 0x07));
		Dest++;
		*Dest = (char)(0x80 | ((CodePoint >> 12) & 0x3f));
		Dest++;
		*Dest = (char)(0x80 | ((CodePoint >> 6) & 0x3f));
		Dest++;
		*Dest = (char)(0x80 | (CodePoint & 0x3f));
		Dest++;
	}
	return Dest;
}


LQ_EXTERN_C char* LQ_CALL LqStrUtf8ToLower(char* Dest, size_t DestSize, const char* Source, int SourceSize)
{
	char *md = Dest + DestSize;
	const char *m = Source + SourceSize;
	if(SourceSize < 0)
		m = (const char*)-1;
	for(const char * i = Source; (i < m) && (*i != '\0');)
	{
		if((unsigned char)*i < 128)
		{
			if((md - Dest) < 1) return Dest;
			*Dest = (*i >= 'A' && *i <= 'Z') ? (*i | 0x20) : *i;
			Dest++;
			i++;
		} else
		{
			uint32_t r = LqStrUtf8toUtf16(&i, m - i);
			if(r > 0x10ffff)
				return nullptr;
			wchar_t NewRes = LqStrCharUtf16ToLower(r);
			Dest = LqStrUtf16ToUtf8(Dest, NewRes, md - Dest);
			if(Dest == nullptr)
				return Dest;
		}
	}
	if((md - Dest) >= 1) *Dest = '\0';
	return Dest;
}


LQ_EXTERN_C char* LQ_CALL LqStrUtf8ToUpper(char* Dest, size_t DestSize, const char* Source, int SourceSize)
{
	char *md = Dest + DestSize;
	const char *m = Source + SourceSize;
	if(SourceSize < 0)
		m = (const char*)-1;
	for(const char * i = Source; (i < m) && (*i != '\0'); )
	{
		if((unsigned char)*i < 128)
		{
			if((md - Dest) < 1) return Dest;
			*Dest = (*i >= 'a' && *i <= 'z') ? (*i & ~0x20) : *i;
			Dest++;
			i++;
		} else
		{
			uint32_t r = LqStrUtf8toUtf16(&i, m - i);
			if(r > 0x10ffff)
				return nullptr;
			auto NewRes = LqStrCharUtf16ToUpper(r);
			Dest = LqStrUtf16ToUtf8(Dest, NewRes, md - Dest);
			if(Dest == nullptr)
				return Dest;
		}
	}
	if((md - Dest) >= 1) *Dest = '\0';
	return Dest;
}

LQ_EXTERN_C bool LQ_CALL LqStrUtf8CmpCaseLen(const char* s1, const char* s2, size_t Len)
{
	intptr_t Diff = s1 - s2;
	for(const char *i = s1, *j = s2, *mi = i + Len, *mj = j + Len; i < mi;)
	{
		if(((unsigned char)*i < 128) && ((unsigned char)*j < 128))
		{
			if(((*i >= 'A' && *i <= 'Z') ? (*i | 0x20) : *i) != ((*j >= 'A' && *j <= 'Z') ? (*j | 0x20) : *j))
				return false;
			i++;
			j++;
		} else
		{
			uint32_t ri = LqStrUtf8toUtf16(&i, mi - i);
			uint32_t rj = LqStrUtf8toUtf16(&j, mj - j);
			if((ri > 0x10ffff) || (rj > 0x10ffff) || ((i - j) != Diff) || (LqStrCharUtf16ToLower(ri) != LqStrCharUtf16ToLower(rj)))
				return false;
		}
	}
	return true;
}

LQ_EXTERN_C bool LQ_CALL LqStrUtf8CmpCase(const char* s1, const char* s2)
{
	for(const char *i = s1, *j = s2; (*i != '\0') && (*j != '\0');)
	{
		if(((unsigned char)*i < 128) && ((unsigned char)*j < 128))
		{
			if(((*i >= 'A' && *i <= 'Z') ? (*i | 0x20) : *i) != ((*j >= 'A' && *j <= 'Z') ? (*j | 0x20) : *j))
				return false;
			i++;
			j++;
		} else
		{
			uint32_t ri = LqStrUtf8toUtf16(&i, 4);
			uint32_t rj = LqStrUtf8toUtf16(&j, 4);
			if((ri > 0x10ffff) || (rj > 0x10ffff) || (ri != rj) && (LqStrCharUtf16ToLower(ri) != LqStrCharUtf16ToLower(rj)))
				return false;
		}
	}
	return true;
}

LQ_EXTERN_C size_t LQ_CALL LqStrCopyMax(char* DestStr, const char* SourceStr, size_t Max)
{
	const char *s = SourceStr, *m = s + Max;
	char *d = DestStr;
	for(; s < m; s++, d++)
		if((*d = *s) == '\0')
			break;
	if((s >= m) && (Max > 0))
		*(d - 1) = '\0';
	return s - SourceStr;
}

LQ_EXTERN_C size_t LQ_CALL LqStrCopy(char* DestStr, const char* SourceStr)
{
	const char *s = SourceStr;
	char *d = DestStr;
	for(; (*d = *s) != '\0'; d++, s++);
	return s - SourceStr;
}

LQ_EXTERN_C size_t LQ_CALL LqStrLen(const char* SourceStr)
{
	const char *s = SourceStr;
	for(; *s != '\0'; s++);
	return s - SourceStr;
}

LQ_EXTERN_C char* LQ_CALL LqStrDuplicate(const char* SourceStr)
{
	const char *s = SourceStr;
	for(; *s != '\0'; s++);
	size_t l = (s - SourceStr) + 1;
	char* r = (char*)malloc(l);
	if(r != nullptr)
		memcpy(r, SourceStr, l);
	return r;
}

LQ_EXTERN_C bool LQ_CALL LqStrSame(const char* Str1, const char* Str2)
{
	const char *s1 = Str1, *s2 = Str2;
	for(; (*s1 == *s2) && (*s1 != '\0'); s1++, s2++);
	return *s1 == *s2;
}

LQ_EXTERN_C bool LQ_CALL LqStrSameMax(const char* Str1, const char* Str2, size_t Max)
{
	const char *s1 = Str1, *s2 = Str2, *m = s1 + Max;
	for(; ; s1++, s2++)
	{
		if(s1 >= m)
			return true;
		if(*s1 != *s2)
			return false;
		if(*s1 == '\0')
			return true;
	}
}

LQ_EXTERN_C void LQ_CALL LqStrToHex(char* Dest, const void* SourceData, size_t LenSource)
{
	static const char c[] = "0123456789abcdef";
	const unsigned char* s = (const unsigned char*)SourceData;
	const unsigned char* m = s + LenSource;
	char *d = Dest;
	while(s < m)
	{
		*d++ = c[(*s & 0xf0) >> 4];
		*d++ = c[*s++ & 0x0f];
	}
	*d = '\0';
}

 LQ_EXTERN_C int LQ_CALL LqStrFromHex(void* Dest, size_t DestLen, const char* SourceStr)
{
	const char* s = (const char*)SourceStr;
	unsigned char* d = (unsigned char*)Dest;
	unsigned char* m = d + DestLen;
	int r = 0;
	while(d < m)
	{
		unsigned char a, b;
		if((*s >= '0') && (*s <= '9'))
			a = *s - '0';
		else if((*s >= 'A') && (*s <= 'F'))
			a = *s - 'A' + 10;
		else if((*s >= 'a') && (*s <= 'f'))
			a = *s - 'a' + 10;
		else
			break;
		s++;
		if((*s >= '0') && (*s <= '9'))
			b = *s - '0';
		else if((*s >= 'A') && (*s <= 'F'))
			b = *s - 'A' + 10;
		else if((*s >= 'a') && (*s <= 'f'))
			b = *s - 'a' + 10;
		else
			break;
		s++;
		*d++ = (a << 4) | b;
		r++;
	}
	return r;
}

