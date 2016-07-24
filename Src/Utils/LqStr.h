/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqStr... - Typical string implementation (basic for UTF-8 CP). (C version)
*/

#ifndef __LQ_STR_H_HAS_INCLUDED__
#define __LQ_STR_H_HAS_INCLUDED__

#include "LqDef.hpp"
#include "LqOs.h"

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqStrUtf8Count(char ch);
LQ_IMPORTEXPORT int LQ_CALL LqStrUtf16Count(wchar_t ch);
LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf8toUtf16(const char **Source, size_t Size);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf16ToUtf8(char* Dest, uint32_t Source, size_t Size);
LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf16ToUtf8Char(uint32_t Source);

LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8ToLower(char* Dest, size_t DestSize, const char* Source, int SourceSize);
LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8ToUpper(char* Dest, size_t DestSize, const char* Source, int SourceSize);
LQ_IMPORTEXPORT bool LQ_CALL LqStrUtf8CmpCaseLen(const char* s1, const char* s2, size_t Len);
LQ_IMPORTEXPORT bool LQ_CALL LqStrUtf8CmpCase(const char* s1, const char* s2);

LQ_IMPORTEXPORT size_t LQ_CALL LqStrCopyMax(char* DestStr, const char* SourceStr, size_t Max);
LQ_IMPORTEXPORT size_t LQ_CALL LqStrCopy(char* DestStr, const char* SourceStr);
LQ_IMPORTEXPORT size_t LQ_CALL LqStrLen(const char* SourceStr);
LQ_IMPORTEXPORT char* LQ_CALL LqStrDuplicate(const char* SourceStr);
LQ_IMPORTEXPORT bool LQ_CALL LqStrSame(const char* Str1, const char* Str2);
LQ_IMPORTEXPORT bool LQ_CALL LqStrSameMax(const char* Str1, const char* Str2, size_t Max);

LQ_IMPORTEXPORT void LQ_CALL LqStrToHex(char* Dest, const void* SourceData, size_t LenSource);
LQ_IMPORTEXPORT int LQ_CALL LqStrFromHex(void* Dest, size_t DestLen, const char* SourceStr);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharUtf16ToLower(uint32_t r);
LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharUtf16ToUpper(uint32_t r);


LQ_IMPORTEXPORT char* LQ_CALL LqStrUtf8CharToStr(char* Dest, uint32_t ch);
LQ_IMPORTEXPORT wchar_t* LQ_CALL LqStrUtf16CharToStr(wchar_t* Dest, uint32_t ch);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrUtf8StrToChar(const char* Source);

LQ_IMPORTEXPORT uint32_t LQ_CALL LqStrCharRead(FILE* FileBuf);

LQ_EXTERN_C_END

#endif