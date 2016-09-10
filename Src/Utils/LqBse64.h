/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqBase64... - Base64 functions.
*/

#ifndef __BASE64_H_HAS_INCLUDED__
#define __BASE64_H_HAS_INCLUDED__
#include "LqOs.h"
#include "LqDef.h"

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqBase64Decode(void* lqaout Dst, const char* lqain lqacp Src, size_t SrcLen);
LQ_IMPORTEXPORT int LQ_CALL LqBase64UrlDecode(void* lqaout Dst, const char* lqain lqacp Src, size_t SrcLen);

LQ_IMPORTEXPORT size_t LQ_CALL LqBase64Code(char* lqaout lqacp Dest, const void* lqain Src, size_t SrcLen);
LQ_IMPORTEXPORT size_t LQ_CALL LqBase64UrlCode(char* lqaout lqacp Dest, const void* lqain Src, size_t SrcLen);

LQ_EXTERN_C_END

#endif
