#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqBase64... - Base64 functions.
*/

#include <string>
#include "LqDef.hpp"
#include "LqBse64.h"



LQ_IMPORTEXPORT LqString LQ_CALL LqBase64CodeToStlStr(const void *Src, size_t SrcLen);
LQ_IMPORTEXPORT LqString LQ_CALL LqBase64UrlCodeToStlStr(const void *Src, size_t SrcLen);

LQ_IMPORTEXPORT LqString LQ_CALL LqBase64DecodeToStlStr(const char *Src, size_t SrcLen);
LQ_IMPORTEXPORT LqString LQ_CALL LqBase64UrlDecodeToStlStr(const char *Src, size_t SrcLen);

