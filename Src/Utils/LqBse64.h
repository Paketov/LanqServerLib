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

LQ_IMPORTEXPORT int LQ_CALL LqBase64Decode(void *Dst, const char *Src, size_t SrcLen);
LQ_IMPORTEXPORT int LQ_CALL LqBase64UrlDecode(void *Dst, const char *Src, size_t SrcLen);

LQ_IMPORTEXPORT size_t LQ_CALL LqBase64Code(char *Dest, const void* Src, size_t SrcLen);
LQ_IMPORTEXPORT size_t LQ_CALL LqBase64UrlCode(char *Dest, const void* Src, size_t SrcLen);

LQ_EXTERN_C_END

#endif
