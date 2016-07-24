
#ifndef __LQ_MD5_H__HAS_DEFINED__
#define __LQ_MD5_H__HAS_DEFINED__

#include "LqOs.h"
 #include <stddef.h>


LQ_EXTERN_C_BEGIN

#pragma pack(push)
#pragma pack(LQCACHE_ALIGN_FAST)

typedef struct 
{
	unsigned int lo, hi;
	unsigned int a, b, c, d;
	unsigned char buffer[64];
	unsigned int block[16];
	char __t[128];
} LqMd5Ctx;

#pragma pack(pop)


enum
{
	LqMd5DigestLen = 16,
	LqMd5HexStringLen = (LqMd5DigestLen * 2)
};

#pragma pack(push)
#pragma pack(1)

typedef struct 
{ 
	unsigned char data[LqMd5DigestLen];
} LqMd5;

#pragma pack(pop)

LQ_IMPORTEXPORT void LQ_CALL LqMd5Init(LqMd5Ctx* Ctx);
LQ_IMPORTEXPORT void LQ_CALL LqMd5Update(LqMd5Ctx* Ctx, const void* Data, size_t Len);
LQ_IMPORTEXPORT void LQ_CALL LqMd5Final(unsigned char* Result, LqMd5Ctx* Ctx);


LQ_IMPORTEXPORT int LQ_CALL LqMd5Compare(const LqMd5* Hash1, const LqMd5* Hash2);
LQ_IMPORTEXPORT void LQ_CALL LqMd5ToString(char* DestStr, const LqMd5* Hash);
LQ_IMPORTEXPORT bool LQ_CALL LqMd5FromString(LqMd5* Hash, const char* SourceStr);
LQ_IMPORTEXPORT void LQ_CALL LqMd5Gen(LqMd5* Hash, const void* Buf, size_t BufLen);
LQ_IMPORTEXPORT void LQ_CALL LqMd5GenToString(char* Dest, const void* Buf, size_t BufLen);

LQ_EXTERN_C_END

#endif
