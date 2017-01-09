
#ifndef __LQ_MD5_H__HAS_DEFINED__
#define __LQ_MD5_H__HAS_DEFINED__

#include "LqOs.h"
#include <stddef.h>


LQ_EXTERN_C_BEGIN

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

typedef struct LqMd5Ctx {
    unsigned int lo, hi;
    unsigned int a, b, c, d;
    unsigned char buffer[64];
    unsigned int block[16];
    char __t[128];
} LqMd5Ctx;

#pragma pack(pop)


enum {
    LqMd5DigestLen = 16,
    LqMd5HexStringLen = (LqMd5DigestLen * 2)
};

#pragma pack(push)
#pragma pack(1)

typedef struct LqMd5 {
    unsigned char data[LqMd5DigestLen];
} LqMd5;

#pragma pack(pop)

LQ_IMPORTEXPORT void LQ_CALL LqMd5Init(LqMd5Ctx* lqaout Ctx);
LQ_IMPORTEXPORT void LQ_CALL LqMd5Update(LqMd5Ctx* lqaio Ctx, const void* lqain Data, size_t Len);
LQ_IMPORTEXPORT void LQ_CALL LqMd5Final(unsigned char* lqaout Result, LqMd5Ctx* lqaio Ctx);

LQ_IMPORTEXPORT int LQ_CALL LqMd5Compare(const LqMd5* lqain Hash1, const LqMd5* lqain Hash2);
LQ_IMPORTEXPORT void LQ_CALL LqMd5ToString(char* lqaout lqacp DestStr, const LqMd5* lqain Hash);
LQ_IMPORTEXPORT void LQ_CALL LqMd5Gen(LqMd5* lqaout Hash, const void* lqain Buf, size_t BufLen);
LQ_IMPORTEXPORT void LQ_CALL LqMd5GenToString(char* lqaout lqacp Dest, const void* lqain Buf, size_t BufLen);

LQ_EXTERN_C_END

#endif
