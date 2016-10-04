/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqStr... - Typical string implementation (basic for UTF-8 CP). (C version)
*/

#ifndef __LQ_STR_H_HAS_INCLUDED__
#define __LQ_STR_H_HAS_INCLUDED__

#include "LqDef.h"
#include "LqOs.h"
#include <stdio.h>

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqStrUtf8Count(char ch);
LQ_IMPORTEXPORT int LQ_CALL LqStrUtf16Count(wchar_t ch);

LQ_IMPORTEXPORT int LQ_CALL LqStrUtf8StringCount(const char* lqain lqautf8 Utf8String);
LQ_IMPORTEXPORT int LQ_CALL LqStrUtf16StringCount(const wchar_t* lqain lqautf16 Utf16String);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf8toUtf16(const char** lqain lqautf8 Source, size_t Size);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf16ToUtf8(char* lqaout lqautf8 Dest, uint32_t Source, size_t Size);
LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf16ToUtf8Char(uint32_t Source);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf8ToLowerChar(const char** lqaio lqautf8 Source, int SourceSize);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8ToLower(char* lqaout lqautf8 Dest, size_t DestSize, const char* lqain lqautf8 Source, int SourceSize);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8ToUpper(char* lqaout lqautf8 Dest, size_t DestSize, const char* lqain lqautf8 Source, int SourceSize);
LQ_IMPORTEXPORT bool LQ_CALL LqStrUtf8CmpCaseLen(const char* lqain lqautf8 s1, const char* lqain lqautf8 s2, size_t Len);
LQ_IMPORTEXPORT bool LQ_CALL LqStrUtf8CmpCase(const char* lqain lqautf8 s1, const char* lqain lqautf8 s2);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8IsAlpha(const char* lqain lqautf8 s1);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8IsAlphaNum(const char* lqain lqautf8 s1);

LQ_IMPORTEXPORT size_t LQ_CALL LqStrCopyMax(char* lqaout DestStr, const char* lqain SourceStr, size_t Max);
LQ_IMPORTEXPORT size_t LQ_CALL LqStrCopy(char* lqaout DestStr, const char* lqain SourceStr);
LQ_IMPORTEXPORT size_t LQ_CALL LqStrCat(char* lqaout DestStr, const char* lqain SourceStr);
LQ_IMPORTEXPORT size_t LQ_CALL LqStrLen(const char* lqain SourceStr);
LQ_IMPORTEXPORT char* LQ_CALL LqStrDuplicate(const char* lqain SourceStr);
LQ_IMPORTEXPORT char* LQ_CALL LqStrDuplicateMax(const char* lqain SourceStr, size_t Count);
LQ_IMPORTEXPORT bool LQ_CALL LqStrSame(const char* lqain Str1, const char* lqain Str2);
LQ_IMPORTEXPORT bool LQ_CALL LqStrSameMax(const char* lqain Str1, const char* lqain Str2, size_t Max);

LQ_IMPORTEXPORT void LQ_CALL LqStrToHex(char* lqaout lqacp Dest, const void* lqain SourceData, size_t LenSource);
LQ_IMPORTEXPORT int LQ_CALL LqStrFromHex(void* lqaout Dest, size_t DestLen, const char* lqain lqacp SourceStr);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharUtf16ToLower(uint32_t r);
LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharUtf16ToUpper(uint32_t r);
LQ_IMPORTEXPORT bool LQ_CALL LqStrCharUtf16IsAlpha(uint32_t r);


LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8CharToStr(char* lqautf8 lqaout Dest, uint32_t ch);
LQ_IMPORTEXPORT wchar_t* LQ_CALL LqStrUtf16CharToStr(wchar_t* lqautf16 lqaout Dest, uint32_t ch);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf8StrToChar(const char* lqautf8 lqain  Source);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharRead(FILE* FileBuf);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharReadUtf8File(FILE* FileBuf);

LQ_EXTERN_C_END

#endif