/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqCrypt... - Encryption/decryption functions.
*/

/*
* Ciphers: AES, TWOFISH, DES3, DES
* Ciphers Modes: CBC, CTR, OFB
* Hashes: MD5, SHA1, SHA256, SHA512, 
* Signs: HMAC, PMAC, OMAC, XCBC

* LibTomCrypt, modular cryptographic library -- Tom St Denis
*
* LibTomCrypt is a library that provides various cryptographic
* algorithms in a highly modular and flexible manner.
*
* The library is free for all purposes without any express
* guarantee it works.
*
* Tom St Denis, tomstdenis@gmail.com, http://libtomcrypt.com
*/


/*
* Ciphers: SERPENT

* Optimized implementation of the Serpent AES candidate algorithm
* Designed by Anderson, Biham and Knudsen and Implemented by
* Gisle Slensminde 2000.
*
* The implementation is based on the pentium optimised sboxes of
* Dag Arne Osvik. Even these sboxes are designed to be optimal for x86
* processors they are efficient on other processors as well, but the speedup
* isn't so impressive compared to other implementations.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public License
* as published by the Free Software Foundation; either version 2 of
* the License, or (at your option) any later version.
*
* Adapted to normal loop device transfer interface.
* Jari Ruusu, March 5 2002
*
* Fixed endianness bug.
* Jari Ruusu, December 26 2002
*
* Added support for MD5 IV computation and multi-key operation.
* Jari Ruusu, October 22 2003
*/


/**
*  Asymmetric Ciphers: RSA

*  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
*  SPDX-License-Identifier: Apache-2.0
*
*  Licensed under the Apache License, Version 2.0 (the "License"); you may
*  not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*  http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
*  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*  This file is part of mbed TLS (https://tls.mbed.org)
*/


#include "LqSbuf.h"
#include "LqAlloc.hpp"
#include "LqStr.h"
#include "LqCrypt.h"
#include "LqErr.h"
#include "LqAtm.hpp"
#include <string.h>
#include <stdlib.h>

#ifdef LQPLATFORM_POSIX
#include <unistd.h>
#endif

#if !(defined(ENDIAN_BIG) || defined(ENDIAN_LITTLE))
#define ENDIAN_NEUTRAL
#endif

#define LTC_FAST_TYPE   uintptr_t

/* max size of either a cipher/hash block or symmetric key [largest of the two] */
static intptr_t LqCrypt16BlockLen(LqCryptCipher* skey);
static intptr_t LqCrypt8BlockLen(LqCryptCipher* skey);
static intptr_t LqCrypt1BlockLen(LqCryptCipher* skey);
static intptr_t LqCryptCbcBlockLen(LqCryptCipher* cbc);

static void LqCryptCiphersSeek(LqCryptCipher *skey, int64_t NewOffset) {}

static bool LqCryptAesInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptAesEncrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len);
static bool LqCryptAesDecrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len);
static void LqCryptAesCopy(LqCryptCipher *Dest, LqCryptCipher *Source);

static bool LqCryptTwofishInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptTwofishEncrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len);
static bool LqCryptTwofishDecrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len);
static void LqCryptTwofishCopy(LqCryptCipher *Dest, LqCryptCipher *Source);

static bool LqCryptDes3Init(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptDes3Encrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len);
static bool LqCryptDes3Decrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len);
static void LqCryptDes3Copy(LqCryptCipher *Dest, LqCryptCipher *Source);

static bool LqCryptDesInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptDesEncrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len);
static bool LqCryptDesDecrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len);
static void LqCryptDesCopy(LqCryptCipher *Dest, LqCryptCipher *Source);

static bool LqCryptSerpentInit(LqCryptCipher* skey, const LqCryptCipherMethods *, const unsigned char *, const unsigned char *key, int key_len, int num_rounds, int);
static bool LqCryptSerpentEncrypt(LqCryptCipher *skey, const unsigned char *in, unsigned char *out, intptr_t Len);
static bool LqCryptSerpentDecrypt(LqCryptCipher *skey, const unsigned char *in, unsigned char *out, intptr_t Len);
static void LqCryptSerpentCopy(LqCryptCipher *Dest, LqCryptCipher *Source);

static bool LqCryptCbcInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptCbcEncrypt(LqCryptCipher *cbc, const unsigned char *pt, unsigned char *ct, intptr_t len);
static bool LqCryptCbcDecrypt(LqCryptCipher *cbc, const unsigned char *ct, unsigned char *pt, intptr_t len);
static void LqCryptCbcCopy(LqCryptCipher *Dest, LqCryptCipher *Source);

static bool LqCryptCtrInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptCtrEncrypt(LqCryptCipher *ctr, const unsigned char *pt, unsigned char *ct, intptr_t len);
static bool LqCryptCtrDecrypt(LqCryptCipher *cbc, const unsigned char *ct, unsigned char *pt, intptr_t len);
static void LqCryptCtrCopy(LqCryptCipher *Dest, LqCryptCipher *Source);
static void LqCryptCtrSeek(LqCryptCipher *skey, int64_t NewOffset);

static bool LqCryptOfbInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
static bool LqCryptOfbEncrypt(LqCryptCipher* skey, const unsigned char *pt, unsigned char *ct, intptr_t len);
static bool LqCryptOfbDecrypt(LqCryptCipher *ctr, const unsigned char *ct, unsigned char *pt, intptr_t len);
static void LqCryptOfbCopy(LqCryptCipher *Dest, LqCryptCipher *Source);



static bool LqCryptMd5Init(LqCryptHash * md);
static bool LqCryptMd5Update(LqCryptHash * md, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptMd5Final(LqCryptHash* HashDest, unsigned char* DestRes);
static void LqCryptMd5Copy(LqCryptHash *Dest, LqCryptHash *Source);

static bool LqCryptSha1Init(LqCryptHash * md);
static bool LqCryptSha1Update(LqCryptHash * md, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptSha1Final(LqCryptHash* HashDest, unsigned char* DestRes);
static void LqCryptSha1Copy(LqCryptHash *Dest, LqCryptHash *Source);

static bool LqCryptSha256Init(LqCryptHash * md);
static bool LqCryptSha256Update(LqCryptHash * md, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptSha256Final(LqCryptHash* HashDest, unsigned char* DestRes);
static void LqCryptSha256Copy(LqCryptHash *Dest, LqCryptHash *Source);

static bool LqCryptSha512Init(LqCryptHash * md);
static bool LqCryptSha512Update(LqCryptHash * md, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptSha512Final(LqCryptHash* HashDest, unsigned char * DestRes);
static void LqCryptSha512Copy(LqCryptHash *Dest, LqCryptHash *Source);


static bool LqCryptHmacUpdate(LqCryptMac *hmac, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptHmacFinal(LqCryptMac *hmac, unsigned char *out);
static bool LqCryptHmacInit(LqCryptMac* hmac, const void* HashMethods, const unsigned char *key, size_t keylen);
static void LqCryptHmacCopy(LqCryptMac *Dest, LqCryptMac *Source);
static intptr_t LqCryptHmacResLen(LqCryptMac* ctx);

static bool LqCryptPmacInit(LqCryptMac* pmac, const void* CipherMethods, const unsigned char *key, size_t keylen);
static bool LqCryptPmacUpdate(LqCryptMac* pmac, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptPmacFinal(LqCryptMac *pmac, unsigned char *out);
static void LqCryptPmacCopy(LqCryptMac *Dest, LqCryptMac *Source);
static intptr_t LqCryptPmacResLen(LqCryptMac* ctx);

static bool LqCryptOmacInit(LqCryptMac *omac, const void* CipherMethods, const unsigned char *key, size_t keylen);
static bool LqCryptOmacUpdate(LqCryptMac* omac, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptOmacFinal(LqCryptMac *omac, unsigned char *out);
static void LqCryptOmacCopy(LqCryptMac *Dest, LqCryptMac *Source);
static intptr_t LqCryptOmacResLen(LqCryptMac* ctx);

static bool LqCryptXcbcInit(LqCryptMac* xcbc, const void* CipherMethods, const unsigned char *key, size_t keylen);
static bool LqCryptXcbcUpdate(LqCryptMac* xcbc, const unsigned char *in, intptr_t inlen);
static intptr_t LqCryptXcbcFinal(LqCryptMac *xcbc, unsigned char *out);
static void LqCryptXcbcCopy(LqCryptMac *Dest, LqCryptMac *Source);
static intptr_t LqCryptXcbcResLen(LqCryptMac* ctx);

LQ_ALIGN(64) static const LqCryptCipherMethods aes_desc = {
    LqCryptAesInit,
    LqCryptAesEncrypt,
    LqCryptAesDecrypt,
    LqCryptCiphersSeek,
    LqCrypt16BlockLen,
    LqCryptAesCopy,
    16,
    sizeof(_LqCryptAesKey),
    "aes"
};

LQ_ALIGN(64) static const LqCryptCipherMethods twofish_desc = {
    LqCryptTwofishInit,
    LqCryptTwofishEncrypt,
    LqCryptTwofishDecrypt,
    LqCryptCiphersSeek,
    LqCrypt16BlockLen,
    LqCryptTwofishCopy,
    16,
    sizeof(_LqCryptTwofishKey),
    "twofish"
};

LQ_ALIGN(64) static const LqCryptCipherMethods des3_desc = {
    LqCryptDes3Init,
    LqCryptDes3Encrypt,
    LqCryptDes3Decrypt,
    LqCryptCiphersSeek,
    LqCrypt8BlockLen,
    LqCryptDes3Copy,
    8,
    sizeof(_LqCryptDes3Key),
    "des3"
};

LQ_ALIGN(64) static const LqCryptCipherMethods des_desc = {
    LqCryptDesInit,
    LqCryptDesEncrypt,
    LqCryptDesDecrypt,
    LqCryptCiphersSeek,
    LqCrypt8BlockLen,
    LqCryptDesCopy,
    8,
    sizeof(_LqCryptDesKey),
    "des"
};

LQ_ALIGN(64) static const LqCryptCipherMethods serpent_desc = {
    LqCryptSerpentInit,
    LqCryptSerpentEncrypt,
    LqCryptSerpentDecrypt,
    LqCryptCiphersSeek,
    LqCrypt16BlockLen,
    LqCryptSerpentCopy,
    16,
    sizeof(_LqCryptSerpentKey),
    "serpent"
};

LQ_ALIGN(64) static const LqCryptCipherMethods cbc_desc = {
    LqCryptCbcInit,
    LqCryptCbcEncrypt,
    LqCryptCbcDecrypt,
    NULL,
    LqCryptCbcBlockLen,
    LqCryptCbcCopy,
    -((intptr_t)1),
    sizeof(_LqCryptSymmetricCbcKey),
    "cbc"
};

LQ_ALIGN(64) static const LqCryptCipherMethods ctr_desc = {
    LqCryptCtrInit,
    LqCryptCtrEncrypt,
    LqCryptCtrDecrypt,
    LqCryptCtrSeek,
    LqCrypt1BlockLen,
    LqCryptCtrCopy,
    -((intptr_t)1),
    sizeof(_LqCryptSymmetricCtrKey),
    "ctr"
};

LQ_ALIGN(64) static const LqCryptCipherMethods ofb_desc = {
    LqCryptOfbInit,
    LqCryptOfbEncrypt,
    LqCryptOfbDecrypt,
    NULL,
    LqCrypt1BlockLen,
    LqCryptOfbCopy,
    -((intptr_t)1),
    sizeof(_symmetric_OFB),
    "ofb"
};

LQ_ALIGN(64) static const LqCryptHashMethods md5_desc = {
    16,
    64,
    LqCryptMd5Init,
    LqCryptMd5Update,
    LqCryptMd5Final,
    LqCryptMd5Copy,
    "md5"
};

LQ_ALIGN(64) static const LqCryptHashMethods sha1_desc = {
    20,
    64,
    LqCryptSha1Init,
    LqCryptSha1Update,
    LqCryptSha1Final,
    LqCryptSha1Copy,
    "sha1"
};

LQ_ALIGN(64) static const LqCryptHashMethods sha256_desc = {
    32,
    64,
    LqCryptSha256Init,
    LqCryptSha256Update,
    LqCryptSha256Final,
    LqCryptSha256Copy,
    "sha256"
};

LQ_ALIGN(64) static const LqCryptHashMethods sha512_desc = {
    64,
    128,
    LqCryptSha512Init,
    LqCryptSha512Update,
    LqCryptSha512Final,
    LqCryptSha512Copy,
    "sha512"
};


LQ_ALIGN(64) static const LqCryptMacMethods hmac_desc = {
    LqCryptHmacInit,
    LqCryptHmacUpdate,
    LqCryptHmacFinal,
    LqCryptHmacCopy,
    LqCryptHmacResLen,
    1,
    sizeof(_LqCryptHmac),
    "hmac"
};

LQ_ALIGN(64) static const LqCryptMacMethods pmac_desc = {
    LqCryptPmacInit,
    LqCryptPmacUpdate,
    LqCryptPmacFinal,
    LqCryptPmacCopy,
    LqCryptPmacResLen,
    2,
    sizeof(_LqCryptPmac),
    "pmac"
};

LQ_ALIGN(64) static const LqCryptMacMethods omac_desc = {
    LqCryptOmacInit,
    LqCryptOmacUpdate,
    LqCryptOmacFinal,
    LqCryptOmacCopy,
    LqCryptOmacResLen,
    2,
    sizeof(_LqCryptOmac),
    "omac"
};

LQ_ALIGN(64) static const LqCryptMacMethods xcbc_desc = {
    LqCryptXcbcInit,
    LqCryptXcbcUpdate,
    LqCryptXcbcFinal,
    LqCryptXcbcCopy,
    LqCryptXcbcResLen,
    2,
    sizeof(_LqCryptXcbc),
    "xcbc"
};


const LqCryptCipherMethods* CiphersMethods[] = {
    &aes_desc,
    &twofish_desc,
    &des3_desc,
    &des_desc,
    &serpent_desc,
    &cbc_desc,
    &ctr_desc,
    &ofb_desc,
    NULL
};

const LqCryptHashMethods* HashMethods[] = {
    &md5_desc,
    &sha1_desc,
    &sha256_desc,
    &sha512_desc,
    NULL
};

const LqCryptMacMethods* MacMethods[] = {
    &hmac_desc,
    &pmac_desc,
    &omac_desc,
    &xcbc_desc,
    NULL
};

/* Compile time optimized constatnts */
#define LQ_IS_BYTE_ENDIAN_LITTLE32 (*((uint32_t*)"\x01\x02\x03\x04") == 0x04030201ul)
#define LQ_IS_WORD_ENDIAN_LITTLE32 (*((uint32_t*)"\x01\x02\x03\x04") == 0x03040102ul)

#define LQ_IS_BYTE_ENDIAN_LITTLE64 (*((uint64_t*)"\x01\x02\x03\x04\x05\x06\x07\x08") == 0x0807060504030201ull)
#define LQ_IS_WORD_ENDIAN_LITTLE64 (*((uint64_t*)"\x01\x02\x03\x04\x05\x06\x07\x08") == 0x0708050603040102ull)
#define LQ_IS_DWORD_ENDIAN_LITTLE64 (*((uint64_t*)"\x01\x02\x03\x04\x05\x06\x07\x08") == 0x0506070801020304ull)

#if defined(LQPLATFORM_WINDOWS)
#define BYTE_SWAP32(x) _byteswap_ulong(x)
#define BYTE_SWAP64(x) _byteswap_uint64(x)
#define ROL(x, y)  _lrotl((x), (y))
#define ROLc(x, y) _lrotl((x), (y))

#define ROR(x, y) _lrotr((x), (y))
#define RORc(x, y) _lrotr((x), (y))

#elif 1

#include <byteswap.h>
#define BYTE_SWAP32(x) bswap_32(x)
#define BYTE_SWAP64(x) bswap_64(x)

#define ROL(x, y) ( (((unsigned long)(x)<<(unsigned long)((y)&31)) | (((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#define ROR(x, y) ( ((((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)((y)&31)) | ((unsigned long)(x)<<(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#define ROLc(x, y) ( (((unsigned long)(x)<<(unsigned long)((y)&31)) | (((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#define RORc(x, y) ( ((((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)((y)&31)) | ((unsigned long)(x)<<(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)

#else

#define BYTE_SWAP32(x) (((x) >> 24) | (((x) >> 8) & 0x0000ff00ul) | (((x) << 8) & 0x00ff0000ul) | ((x) << 24))
#define BYTE_SWAP64(x) (((x) >> 56) | (((x) >> 40) & 0x000000000000ff00ull) |\
                        (((x) >> 24) & 0x0000000000ff0000ull) | (((x) >> 8) & 0x00000000ff000000ull) |\
                        (((x) << 8) & 0x000000ff00000000ull) | (((x) << 24) & 0x0000ff0000000000ull) |\
                        (((x) << 40) & 0x00ff000000000000ull) | ((x) << 56))

#define ROL(x, y) ( (((unsigned long)(x)<<(unsigned long)((y)&31)) | (((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#define ROR(x, y) ( ((((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)((y)&31)) | ((unsigned long)(x)<<(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#define ROLc(x, y) ( (((unsigned long)(x)<<(unsigned long)((y)&31)) | (((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#define RORc(x, y) ( ((((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)((y)&31)) | ((unsigned long)(x)<<(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)
#endif

#define WORD_SWAP32(x) (((x) >> 16) | ((x) << 16))

#define WORD_SWAP32_TO_BYTE(x) ((((x) >> 8) & 0x00ff00fful) | (((x) << 8) & 0xff00ff00ul))

#define WORD_SWAP64(x) (((x) >> 48) | (((x) >> 16) & 0x00000000ffff0000ull) |\
                        (((x) << 16) & 0x0000ffff00000000ull) | ((x) << 48))

#define DWORD_SWAP64(x) (((x) >> 32) | ((x) << 32))

#define WORD_SWAP64_TO_BYTE(x) ((((x) >> 8) & 0x00ff00ff00ff00ffull) | (((x) << 8) & 0xff00ff00ff00ff00ull))
#define DWORD_SWAP64_TO_BYTE(x) ((((x) >> 24) & 0x000000ff000000ffull) | (((x) >> 8) & 0x0000ff000000ff00ull) |\
                                 (((x) << 24) & 0xff000000ff000000ull) | (((x) << 8) & 0x00ff000000ff0000ull))

#define STORE32L(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE32){\
        *((uint32_t*)(y)) = (x);\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE32){ \
        *((uint32_t*)(y)) = WORD_SWAP32_TO_BYTE(x);\
    }else{\
        *((uint32_t*)(y)) = BYTE_SWAP32(x);\
    }\
}

#define LOAD32L(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE32){\
        x = *((uint32_t*)(y));\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE32){ \
        register uint32_t t = *((uint32_t*)(y));\
        x = WORD_SWAP32_TO_BYTE(t);\
    }else{\
        register uint32_t t = *((uint32_t*)(y));\
        x = BYTE_SWAP32(t);\
    }\
 }

#define STORE32H(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE32){\
        *((uint32_t*)(y)) = BYTE_SWAP32(x);\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE32){ \
        *((uint32_t*)(y)) = WORD_SWAP32(x);\
    }else{\
        *((uint32_t*)(y)) = (x);\
    }\
}

#define LOAD32H(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE32){\
        register uint32_t t = *((uint32_t*)(y));\
        x = BYTE_SWAP32(t);\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE32){ \
        register uint32_t t = *((uint32_t*)(y));\
        x = WORD_SWAP32(t);\
    }else{\
        x = *((uint32_t*)(y));\
    }\
 }

#define STORE64L(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE64){\
        *((uint64_t*)(y)) = (x);\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE64){ \
        *((uint32_t*)(y)) = WORD_SWAP64_TO_BYTE(x);\
    }else if(LQ_IS_DWORD_ENDIAN_LITTLE64) {\
        *((uint32_t*)(y)) = DWORD_SWAP64_TO_BYTE(x);\
    }else{\
        *((uint64_t*)(y)) = BYTE_SWAP64(x);\
    }\
}

#define LOAD64L(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE64){\
        x = *((uint64_t*)(y));\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE64){ \
        register uint64_t t = *((uint64_t*)(y));\
        x = WORD_SWAP64_TO_BYTE(t);\
    }else if(LQ_IS_DWORD_ENDIAN_LITTLE64) {\
        register uint64_t t = *((uint64_t*)(y));\
        x = DWORD_SWAP64_TO_BYTE(t);\
    }else{\
        register uint64_t t = *((uint64_t*)(y));\
        x = BYTE_SWAP64(t);\
    }\
 }

#define STORE64H(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE64){\
        *((uint64_t*)(y)) = BYTE_SWAP64(x);\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE64){ \
        *((uint64_t*)(y)) = WORD_SWAP64(x);\
    }else if(LQ_IS_DWORD_ENDIAN_LITTLE64){ \
        *((uint64_t*)(y)) = WORD_SWAP64(x);\
    }else{\
        *((uint64_t*)(y)) = (x);\
    }\
}

#define LOAD64H(x, y) {\
    if(LQ_IS_BYTE_ENDIAN_LITTLE64){\
        register uint64_t t = *((uint64_t*)(y));\
        x = BYTE_SWAP64(t);\
    }else if(LQ_IS_WORD_ENDIAN_LITTLE64){ \
        register uint64_t t = *((uint64_t*)(y));\
        x = WORD_SWAP64(t);\
    }else if(LQ_IS_DWORD_ENDIAN_LITTLE64){ \
        register uint64_t t = *((uint64_t*)(y));\
        x = DWORD_SWAP64(t);\
    }else{\
        x = *((uint64_t*)(y));\
    }\
 }





#define byte(x, n) (((x) >> (8 * (n))) & 255)

LQ_ALIGN(128) static const uint32_t TE0[256] = {
    0xc66363a5UL, 0xf87c7c84UL, 0xee777799UL, 0xf67b7b8dUL, 0xfff2f20dUL, 0xd66b6bbdUL, 0xde6f6fb1UL, 0x91c5c554UL,
    0x60303050UL, 0x02010103UL, 0xce6767a9UL, 0x562b2b7dUL, 0xe7fefe19UL, 0xb5d7d762UL, 0x4dababe6UL, 0xec76769aUL,
    0x8fcaca45UL, 0x1f82829dUL, 0x89c9c940UL, 0xfa7d7d87UL, 0xeffafa15UL, 0xb25959ebUL, 0x8e4747c9UL, 0xfbf0f00bUL,
    0x41adadecUL, 0xb3d4d467UL, 0x5fa2a2fdUL, 0x45afafeaUL, 0x239c9cbfUL, 0x53a4a4f7UL, 0xe4727296UL, 0x9bc0c05bUL,
    0x75b7b7c2UL, 0xe1fdfd1cUL, 0x3d9393aeUL, 0x4c26266aUL, 0x6c36365aUL, 0x7e3f3f41UL, 0xf5f7f702UL, 0x83cccc4fUL,
    0x6834345cUL, 0x51a5a5f4UL, 0xd1e5e534UL, 0xf9f1f108UL, 0xe2717193UL, 0xabd8d873UL, 0x62313153UL, 0x2a15153fUL,
    0x0804040cUL, 0x95c7c752UL, 0x46232365UL, 0x9dc3c35eUL, 0x30181828UL, 0x379696a1UL, 0x0a05050fUL, 0x2f9a9ab5UL,
    0x0e070709UL, 0x24121236UL, 0x1b80809bUL, 0xdfe2e23dUL, 0xcdebeb26UL, 0x4e272769UL, 0x7fb2b2cdUL, 0xea75759fUL,
    0x1209091bUL, 0x1d83839eUL, 0x582c2c74UL, 0x341a1a2eUL, 0x361b1b2dUL, 0xdc6e6eb2UL, 0xb45a5aeeUL, 0x5ba0a0fbUL,
    0xa45252f6UL, 0x763b3b4dUL, 0xb7d6d661UL, 0x7db3b3ceUL, 0x5229297bUL, 0xdde3e33eUL, 0x5e2f2f71UL, 0x13848497UL,
    0xa65353f5UL, 0xb9d1d168UL, 0x00000000UL, 0xc1eded2cUL, 0x40202060UL, 0xe3fcfc1fUL, 0x79b1b1c8UL, 0xb65b5bedUL,
    0xd46a6abeUL, 0x8dcbcb46UL, 0x67bebed9UL, 0x7239394bUL, 0x944a4adeUL, 0x984c4cd4UL, 0xb05858e8UL, 0x85cfcf4aUL,
    0xbbd0d06bUL, 0xc5efef2aUL, 0x4faaaae5UL, 0xedfbfb16UL, 0x864343c5UL, 0x9a4d4dd7UL, 0x66333355UL, 0x11858594UL,
    0x8a4545cfUL, 0xe9f9f910UL, 0x04020206UL, 0xfe7f7f81UL, 0xa05050f0UL, 0x783c3c44UL, 0x259f9fbaUL, 0x4ba8a8e3UL,
    0xa25151f3UL, 0x5da3a3feUL, 0x804040c0UL, 0x058f8f8aUL, 0x3f9292adUL, 0x219d9dbcUL, 0x70383848UL, 0xf1f5f504UL,
    0x63bcbcdfUL, 0x77b6b6c1UL, 0xafdada75UL, 0x42212163UL, 0x20101030UL, 0xe5ffff1aUL, 0xfdf3f30eUL, 0xbfd2d26dUL,
    0x81cdcd4cUL, 0x180c0c14UL, 0x26131335UL, 0xc3ecec2fUL, 0xbe5f5fe1UL, 0x359797a2UL, 0x884444ccUL, 0x2e171739UL,
    0x93c4c457UL, 0x55a7a7f2UL, 0xfc7e7e82UL, 0x7a3d3d47UL, 0xc86464acUL, 0xba5d5de7UL, 0x3219192bUL, 0xe6737395UL,
    0xc06060a0UL, 0x19818198UL, 0x9e4f4fd1UL, 0xa3dcdc7fUL, 0x44222266UL, 0x542a2a7eUL, 0x3b9090abUL, 0x0b888883UL,
    0x8c4646caUL, 0xc7eeee29UL, 0x6bb8b8d3UL, 0x2814143cUL, 0xa7dede79UL, 0xbc5e5ee2UL, 0x160b0b1dUL, 0xaddbdb76UL,
    0xdbe0e03bUL, 0x64323256UL, 0x743a3a4eUL, 0x140a0a1eUL, 0x924949dbUL, 0x0c06060aUL, 0x4824246cUL, 0xb85c5ce4UL,
    0x9fc2c25dUL, 0xbdd3d36eUL, 0x43acacefUL, 0xc46262a6UL, 0x399191a8UL, 0x319595a4UL, 0xd3e4e437UL, 0xf279798bUL,
    0xd5e7e732UL, 0x8bc8c843UL, 0x6e373759UL, 0xda6d6db7UL, 0x018d8d8cUL, 0xb1d5d564UL, 0x9c4e4ed2UL, 0x49a9a9e0UL,
    0xd86c6cb4UL, 0xac5656faUL, 0xf3f4f407UL, 0xcfeaea25UL, 0xca6565afUL, 0xf47a7a8eUL, 0x47aeaee9UL, 0x10080818UL,
    0x6fbabad5UL, 0xf0787888UL, 0x4a25256fUL, 0x5c2e2e72UL, 0x381c1c24UL, 0x57a6a6f1UL, 0x73b4b4c7UL, 0x97c6c651UL,
    0xcbe8e823UL, 0xa1dddd7cUL, 0xe874749cUL, 0x3e1f1f21UL, 0x964b4bddUL, 0x61bdbddcUL, 0x0d8b8b86UL, 0x0f8a8a85UL,
    0xe0707090UL, 0x7c3e3e42UL, 0x71b5b5c4UL, 0xcc6666aaUL, 0x904848d8UL, 0x06030305UL, 0xf7f6f601UL, 0x1c0e0e12UL,
    0xc26161a3UL, 0x6a35355fUL, 0xae5757f9UL, 0x69b9b9d0UL, 0x17868691UL, 0x99c1c158UL, 0x3a1d1d27UL, 0x279e9eb9UL,
    0xd9e1e138UL, 0xebf8f813UL, 0x2b9898b3UL, 0x22111133UL, 0xd26969bbUL, 0xa9d9d970UL, 0x078e8e89UL, 0x339494a7UL,
    0x2d9b9bb6UL, 0x3c1e1e22UL, 0x15878792UL, 0xc9e9e920UL, 0x87cece49UL, 0xaa5555ffUL, 0x50282878UL, 0xa5dfdf7aUL,
    0x038c8c8fUL, 0x59a1a1f8UL, 0x09898980UL, 0x1a0d0d17UL, 0x65bfbfdaUL, 0xd7e6e631UL, 0x844242c6UL, 0xd06868b8UL,
    0x824141c3UL, 0x299999b0UL, 0x5a2d2d77UL, 0x1e0f0f11UL, 0x7bb0b0cbUL, 0xa85454fcUL, 0x6dbbbbd6UL, 0x2c16163aUL,
};

#ifndef PELI_TAB
LQ_ALIGN(128) static const uint32_t Te4[256] = {
    0x63636363UL, 0x7c7c7c7cUL, 0x77777777UL, 0x7b7b7b7bUL, 0xf2f2f2f2UL, 0x6b6b6b6bUL, 0x6f6f6f6fUL, 0xc5c5c5c5UL,
    0x30303030UL, 0x01010101UL, 0x67676767UL, 0x2b2b2b2bUL, 0xfefefefeUL, 0xd7d7d7d7UL, 0xababababUL, 0x76767676UL,
    0xcacacacaUL, 0x82828282UL, 0xc9c9c9c9UL, 0x7d7d7d7dUL, 0xfafafafaUL, 0x59595959UL, 0x47474747UL, 0xf0f0f0f0UL,
    0xadadadadUL, 0xd4d4d4d4UL, 0xa2a2a2a2UL, 0xafafafafUL, 0x9c9c9c9cUL, 0xa4a4a4a4UL, 0x72727272UL, 0xc0c0c0c0UL,
    0xb7b7b7b7UL, 0xfdfdfdfdUL, 0x93939393UL, 0x26262626UL, 0x36363636UL, 0x3f3f3f3fUL, 0xf7f7f7f7UL, 0xccccccccUL,
    0x34343434UL, 0xa5a5a5a5UL, 0xe5e5e5e5UL, 0xf1f1f1f1UL, 0x71717171UL, 0xd8d8d8d8UL, 0x31313131UL, 0x15151515UL,
    0x04040404UL, 0xc7c7c7c7UL, 0x23232323UL, 0xc3c3c3c3UL, 0x18181818UL, 0x96969696UL, 0x05050505UL, 0x9a9a9a9aUL,
    0x07070707UL, 0x12121212UL, 0x80808080UL, 0xe2e2e2e2UL, 0xebebebebUL, 0x27272727UL, 0xb2b2b2b2UL, 0x75757575UL,
    0x09090909UL, 0x83838383UL, 0x2c2c2c2cUL, 0x1a1a1a1aUL, 0x1b1b1b1bUL, 0x6e6e6e6eUL, 0x5a5a5a5aUL, 0xa0a0a0a0UL,
    0x52525252UL, 0x3b3b3b3bUL, 0xd6d6d6d6UL, 0xb3b3b3b3UL, 0x29292929UL, 0xe3e3e3e3UL, 0x2f2f2f2fUL, 0x84848484UL,
    0x53535353UL, 0xd1d1d1d1UL, 0x00000000UL, 0xededededUL, 0x20202020UL, 0xfcfcfcfcUL, 0xb1b1b1b1UL, 0x5b5b5b5bUL,
    0x6a6a6a6aUL, 0xcbcbcbcbUL, 0xbebebebeUL, 0x39393939UL, 0x4a4a4a4aUL, 0x4c4c4c4cUL, 0x58585858UL, 0xcfcfcfcfUL,
    0xd0d0d0d0UL, 0xefefefefUL, 0xaaaaaaaaUL, 0xfbfbfbfbUL, 0x43434343UL, 0x4d4d4d4dUL, 0x33333333UL, 0x85858585UL,
    0x45454545UL, 0xf9f9f9f9UL, 0x02020202UL, 0x7f7f7f7fUL, 0x50505050UL, 0x3c3c3c3cUL, 0x9f9f9f9fUL, 0xa8a8a8a8UL,
    0x51515151UL, 0xa3a3a3a3UL, 0x40404040UL, 0x8f8f8f8fUL, 0x92929292UL, 0x9d9d9d9dUL, 0x38383838UL, 0xf5f5f5f5UL,
    0xbcbcbcbcUL, 0xb6b6b6b6UL, 0xdadadadaUL, 0x21212121UL, 0x10101010UL, 0xffffffffUL, 0xf3f3f3f3UL, 0xd2d2d2d2UL,
    0xcdcdcdcdUL, 0x0c0c0c0cUL, 0x13131313UL, 0xececececUL, 0x5f5f5f5fUL, 0x97979797UL, 0x44444444UL, 0x17171717UL,
    0xc4c4c4c4UL, 0xa7a7a7a7UL, 0x7e7e7e7eUL, 0x3d3d3d3dUL, 0x64646464UL, 0x5d5d5d5dUL, 0x19191919UL, 0x73737373UL,
    0x60606060UL, 0x81818181UL, 0x4f4f4f4fUL, 0xdcdcdcdcUL, 0x22222222UL, 0x2a2a2a2aUL, 0x90909090UL, 0x88888888UL,
    0x46464646UL, 0xeeeeeeeeUL, 0xb8b8b8b8UL, 0x14141414UL, 0xdedededeUL, 0x5e5e5e5eUL, 0x0b0b0b0bUL, 0xdbdbdbdbUL,
    0xe0e0e0e0UL, 0x32323232UL, 0x3a3a3a3aUL, 0x0a0a0a0aUL, 0x49494949UL, 0x06060606UL, 0x24242424UL, 0x5c5c5c5cUL,
    0xc2c2c2c2UL, 0xd3d3d3d3UL, 0xacacacacUL, 0x62626262UL, 0x91919191UL, 0x95959595UL, 0xe4e4e4e4UL, 0x79797979UL,
    0xe7e7e7e7UL, 0xc8c8c8c8UL, 0x37373737UL, 0x6d6d6d6dUL, 0x8d8d8d8dUL, 0xd5d5d5d5UL, 0x4e4e4e4eUL, 0xa9a9a9a9UL,
    0x6c6c6c6cUL, 0x56565656UL, 0xf4f4f4f4UL, 0xeaeaeaeaUL, 0x65656565UL, 0x7a7a7a7aUL, 0xaeaeaeaeUL, 0x08080808UL,
    0xbabababaUL, 0x78787878UL, 0x25252525UL, 0x2e2e2e2eUL, 0x1c1c1c1cUL, 0xa6a6a6a6UL, 0xb4b4b4b4UL, 0xc6c6c6c6UL,
    0xe8e8e8e8UL, 0xddddddddUL, 0x74747474UL, 0x1f1f1f1fUL, 0x4b4b4b4bUL, 0xbdbdbdbdUL, 0x8b8b8b8bUL, 0x8a8a8a8aUL,
    0x70707070UL, 0x3e3e3e3eUL, 0xb5b5b5b5UL, 0x66666666UL, 0x48484848UL, 0x03030303UL, 0xf6f6f6f6UL, 0x0e0e0e0eUL,
    0x61616161UL, 0x35353535UL, 0x57575757UL, 0xb9b9b9b9UL, 0x86868686UL, 0xc1c1c1c1UL, 0x1d1d1d1dUL, 0x9e9e9e9eUL,
    0xe1e1e1e1UL, 0xf8f8f8f8UL, 0x98989898UL, 0x11111111UL, 0x69696969UL, 0xd9d9d9d9UL, 0x8e8e8e8eUL, 0x94949494UL,
    0x9b9b9b9bUL, 0x1e1e1e1eUL, 0x87878787UL, 0xe9e9e9e9UL, 0xcecececeUL, 0x55555555UL, 0x28282828UL, 0xdfdfdfdfUL,
    0x8c8c8c8cUL, 0xa1a1a1a1UL, 0x89898989UL, 0x0d0d0d0dUL, 0xbfbfbfbfUL, 0xe6e6e6e6UL, 0x42424242UL, 0x68686868UL,
    0x41414141UL, 0x99999999UL, 0x2d2d2d2dUL, 0x0f0f0f0fUL, 0xb0b0b0b0UL, 0x54545454UL, 0xbbbbbbbbUL, 0x16161616UL,
};
#endif

LQ_ALIGN(128) static const uint32_t TD0[256] = {
    0x51f4a750UL, 0x7e416553UL, 0x1a17a4c3UL, 0x3a275e96UL, 0x3bab6bcbUL, 0x1f9d45f1UL, 0xacfa58abUL, 0x4be30393UL,
    0x2030fa55UL, 0xad766df6UL, 0x88cc7691UL, 0xf5024c25UL, 0x4fe5d7fcUL, 0xc52acbd7UL, 0x26354480UL, 0xb562a38fUL,
    0xdeb15a49UL, 0x25ba1b67UL, 0x45ea0e98UL, 0x5dfec0e1UL, 0xc32f7502UL, 0x814cf012UL, 0x8d4697a3UL, 0x6bd3f9c6UL,
    0x038f5fe7UL, 0x15929c95UL, 0xbf6d7aebUL, 0x955259daUL, 0xd4be832dUL, 0x587421d3UL, 0x49e06929UL, 0x8ec9c844UL,
    0x75c2896aUL, 0xf48e7978UL, 0x99583e6bUL, 0x27b971ddUL, 0xbee14fb6UL, 0xf088ad17UL, 0xc920ac66UL, 0x7dce3ab4UL,
    0x63df4a18UL, 0xe51a3182UL, 0x97513360UL, 0x62537f45UL, 0xb16477e0UL, 0xbb6bae84UL, 0xfe81a01cUL, 0xf9082b94UL,
    0x70486858UL, 0x8f45fd19UL, 0x94de6c87UL, 0x527bf8b7UL, 0xab73d323UL, 0x724b02e2UL, 0xe31f8f57UL, 0x6655ab2aUL,
    0xb2eb2807UL, 0x2fb5c203UL, 0x86c57b9aUL, 0xd33708a5UL, 0x302887f2UL, 0x23bfa5b2UL, 0x02036abaUL, 0xed16825cUL,
    0x8acf1c2bUL, 0xa779b492UL, 0xf307f2f0UL, 0x4e69e2a1UL, 0x65daf4cdUL, 0x0605bed5UL, 0xd134621fUL, 0xc4a6fe8aUL,
    0x342e539dUL, 0xa2f355a0UL, 0x058ae132UL, 0xa4f6eb75UL, 0x0b83ec39UL, 0x4060efaaUL, 0x5e719f06UL, 0xbd6e1051UL,
    0x3e218af9UL, 0x96dd063dUL, 0xdd3e05aeUL, 0x4de6bd46UL, 0x91548db5UL, 0x71c45d05UL, 0x0406d46fUL, 0x605015ffUL,
    0x1998fb24UL, 0xd6bde997UL, 0x894043ccUL, 0x67d99e77UL, 0xb0e842bdUL, 0x07898b88UL, 0xe7195b38UL, 0x79c8eedbUL,
    0xa17c0a47UL, 0x7c420fe9UL, 0xf8841ec9UL, 0x00000000UL, 0x09808683UL, 0x322bed48UL, 0x1e1170acUL, 0x6c5a724eUL,
    0xfd0efffbUL, 0x0f853856UL, 0x3daed51eUL, 0x362d3927UL, 0x0a0fd964UL, 0x685ca621UL, 0x9b5b54d1UL, 0x24362e3aUL,
    0x0c0a67b1UL, 0x9357e70fUL, 0xb4ee96d2UL, 0x1b9b919eUL, 0x80c0c54fUL, 0x61dc20a2UL, 0x5a774b69UL, 0x1c121a16UL,
    0xe293ba0aUL, 0xc0a02ae5UL, 0x3c22e043UL, 0x121b171dUL, 0x0e090d0bUL, 0xf28bc7adUL, 0x2db6a8b9UL, 0x141ea9c8UL,
    0x57f11985UL, 0xaf75074cUL, 0xee99ddbbUL, 0xa37f60fdUL, 0xf701269fUL, 0x5c72f5bcUL, 0x44663bc5UL, 0x5bfb7e34UL,
    0x8b432976UL, 0xcb23c6dcUL, 0xb6edfc68UL, 0xb8e4f163UL, 0xd731dccaUL, 0x42638510UL, 0x13972240UL, 0x84c61120UL,
    0x854a247dUL, 0xd2bb3df8UL, 0xaef93211UL, 0xc729a16dUL, 0x1d9e2f4bUL, 0xdcb230f3UL, 0x0d8652ecUL, 0x77c1e3d0UL,
    0x2bb3166cUL, 0xa970b999UL, 0x119448faUL, 0x47e96422UL, 0xa8fc8cc4UL, 0xa0f03f1aUL, 0x567d2cd8UL, 0x223390efUL,
    0x87494ec7UL, 0xd938d1c1UL, 0x8ccaa2feUL, 0x98d40b36UL, 0xa6f581cfUL, 0xa57ade28UL, 0xdab78e26UL, 0x3fadbfa4UL,
    0x2c3a9de4UL, 0x5078920dUL, 0x6a5fcc9bUL, 0x547e4662UL, 0xf68d13c2UL, 0x90d8b8e8UL, 0x2e39f75eUL, 0x82c3aff5UL,
    0x9f5d80beUL, 0x69d0937cUL, 0x6fd52da9UL, 0xcf2512b3UL, 0xc8ac993bUL, 0x10187da7UL, 0xe89c636eUL, 0xdb3bbb7bUL,
    0xcd267809UL, 0x6e5918f4UL, 0xec9ab701UL, 0x834f9aa8UL, 0xe6956e65UL, 0xaaffe67eUL, 0x21bccf08UL, 0xef15e8e6UL,
    0xbae79bd9UL, 0x4a6f36ceUL, 0xea9f09d4UL, 0x29b07cd6UL, 0x31a4b2afUL, 0x2a3f2331UL, 0xc6a59430UL, 0x35a266c0UL,
    0x744ebc37UL, 0xfc82caa6UL, 0xe090d0b0UL, 0x33a7d815UL, 0xf104984aUL, 0x41ecdaf7UL, 0x7fcd500eUL, 0x1791f62fUL,
    0x764dd68dUL, 0x43efb04dUL, 0xccaa4d54UL, 0xe49604dfUL, 0x9ed1b5e3UL, 0x4c6a881bUL, 0xc12c1fb8UL, 0x4665517fUL,
    0x9d5eea04UL, 0x018c355dUL, 0xfa877473UL, 0xfb0b412eUL, 0xb3671d5aUL, 0x92dbd252UL, 0xe9105633UL, 0x6dd64713UL,
    0x9ad7618cUL, 0x37a10c7aUL, 0x59f8148eUL, 0xeb133c89UL, 0xcea927eeUL, 0xb761c935UL, 0xe11ce5edUL, 0x7a47b13cUL,
    0x9cd2df59UL, 0x55f2733fUL, 0x1814ce79UL, 0x73c737bfUL, 0x53f7cdeaUL, 0x5ffdaa5bUL, 0xdf3d6f14UL, 0x7844db86UL,
    0xcaaff381UL, 0xb968c43eUL, 0x3824342cUL, 0xc2a3405fUL, 0x161dc372UL, 0xbce2250cUL, 0x283c498bUL, 0xff0d9541UL,
    0x39a80171UL, 0x080cb3deUL, 0xd8b4e49cUL, 0x6456c190UL, 0x7bcb8461UL, 0xd532b670UL, 0x486c5c74UL, 0xd0b85742UL,
};

LQ_ALIGN(128) static const uint32_t Td4[256] = {
    0x52525252UL, 0x09090909UL, 0x6a6a6a6aUL, 0xd5d5d5d5UL, 0x30303030UL, 0x36363636UL, 0xa5a5a5a5UL, 0x38383838UL,
    0xbfbfbfbfUL, 0x40404040UL, 0xa3a3a3a3UL, 0x9e9e9e9eUL, 0x81818181UL, 0xf3f3f3f3UL, 0xd7d7d7d7UL, 0xfbfbfbfbUL,
    0x7c7c7c7cUL, 0xe3e3e3e3UL, 0x39393939UL, 0x82828282UL, 0x9b9b9b9bUL, 0x2f2f2f2fUL, 0xffffffffUL, 0x87878787UL,
    0x34343434UL, 0x8e8e8e8eUL, 0x43434343UL, 0x44444444UL, 0xc4c4c4c4UL, 0xdedededeUL, 0xe9e9e9e9UL, 0xcbcbcbcbUL,
    0x54545454UL, 0x7b7b7b7bUL, 0x94949494UL, 0x32323232UL, 0xa6a6a6a6UL, 0xc2c2c2c2UL, 0x23232323UL, 0x3d3d3d3dUL,
    0xeeeeeeeeUL, 0x4c4c4c4cUL, 0x95959595UL, 0x0b0b0b0bUL, 0x42424242UL, 0xfafafafaUL, 0xc3c3c3c3UL, 0x4e4e4e4eUL,
    0x08080808UL, 0x2e2e2e2eUL, 0xa1a1a1a1UL, 0x66666666UL, 0x28282828UL, 0xd9d9d9d9UL, 0x24242424UL, 0xb2b2b2b2UL,
    0x76767676UL, 0x5b5b5b5bUL, 0xa2a2a2a2UL, 0x49494949UL, 0x6d6d6d6dUL, 0x8b8b8b8bUL, 0xd1d1d1d1UL, 0x25252525UL,
    0x72727272UL, 0xf8f8f8f8UL, 0xf6f6f6f6UL, 0x64646464UL, 0x86868686UL, 0x68686868UL, 0x98989898UL, 0x16161616UL,
    0xd4d4d4d4UL, 0xa4a4a4a4UL, 0x5c5c5c5cUL, 0xccccccccUL, 0x5d5d5d5dUL, 0x65656565UL, 0xb6b6b6b6UL, 0x92929292UL,
    0x6c6c6c6cUL, 0x70707070UL, 0x48484848UL, 0x50505050UL, 0xfdfdfdfdUL, 0xededededUL, 0xb9b9b9b9UL, 0xdadadadaUL,
    0x5e5e5e5eUL, 0x15151515UL, 0x46464646UL, 0x57575757UL, 0xa7a7a7a7UL, 0x8d8d8d8dUL, 0x9d9d9d9dUL, 0x84848484UL,
    0x90909090UL, 0xd8d8d8d8UL, 0xababababUL, 0x00000000UL, 0x8c8c8c8cUL, 0xbcbcbcbcUL, 0xd3d3d3d3UL, 0x0a0a0a0aUL,
    0xf7f7f7f7UL, 0xe4e4e4e4UL, 0x58585858UL, 0x05050505UL, 0xb8b8b8b8UL, 0xb3b3b3b3UL, 0x45454545UL, 0x06060606UL,
    0xd0d0d0d0UL, 0x2c2c2c2cUL, 0x1e1e1e1eUL, 0x8f8f8f8fUL, 0xcacacacaUL, 0x3f3f3f3fUL, 0x0f0f0f0fUL, 0x02020202UL,
    0xc1c1c1c1UL, 0xafafafafUL, 0xbdbdbdbdUL, 0x03030303UL, 0x01010101UL, 0x13131313UL, 0x8a8a8a8aUL, 0x6b6b6b6bUL,
    0x3a3a3a3aUL, 0x91919191UL, 0x11111111UL, 0x41414141UL, 0x4f4f4f4fUL, 0x67676767UL, 0xdcdcdcdcUL, 0xeaeaeaeaUL,
    0x97979797UL, 0xf2f2f2f2UL, 0xcfcfcfcfUL, 0xcecececeUL, 0xf0f0f0f0UL, 0xb4b4b4b4UL, 0xe6e6e6e6UL, 0x73737373UL,
    0x96969696UL, 0xacacacacUL, 0x74747474UL, 0x22222222UL, 0xe7e7e7e7UL, 0xadadadadUL, 0x35353535UL, 0x85858585UL,
    0xe2e2e2e2UL, 0xf9f9f9f9UL, 0x37373737UL, 0xe8e8e8e8UL, 0x1c1c1c1cUL, 0x75757575UL, 0xdfdfdfdfUL, 0x6e6e6e6eUL,
    0x47474747UL, 0xf1f1f1f1UL, 0x1a1a1a1aUL, 0x71717171UL, 0x1d1d1d1dUL, 0x29292929UL, 0xc5c5c5c5UL, 0x89898989UL,
    0x6f6f6f6fUL, 0xb7b7b7b7UL, 0x62626262UL, 0x0e0e0e0eUL, 0xaaaaaaaaUL, 0x18181818UL, 0xbebebebeUL, 0x1b1b1b1bUL,
    0xfcfcfcfcUL, 0x56565656UL, 0x3e3e3e3eUL, 0x4b4b4b4bUL, 0xc6c6c6c6UL, 0xd2d2d2d2UL, 0x79797979UL, 0x20202020UL,
    0x9a9a9a9aUL, 0xdbdbdbdbUL, 0xc0c0c0c0UL, 0xfefefefeUL, 0x78787878UL, 0xcdcdcdcdUL, 0x5a5a5a5aUL, 0xf4f4f4f4UL,
    0x1f1f1f1fUL, 0xddddddddUL, 0xa8a8a8a8UL, 0x33333333UL, 0x88888888UL, 0x07070707UL, 0xc7c7c7c7UL, 0x31313131UL,
    0xb1b1b1b1UL, 0x12121212UL, 0x10101010UL, 0x59595959UL, 0x27272727UL, 0x80808080UL, 0xececececUL, 0x5f5f5f5fUL,
    0x60606060UL, 0x51515151UL, 0x7f7f7f7fUL, 0xa9a9a9a9UL, 0x19191919UL, 0xb5b5b5b5UL, 0x4a4a4a4aUL, 0x0d0d0d0dUL,
    0x2d2d2d2dUL, 0xe5e5e5e5UL, 0x7a7a7a7aUL, 0x9f9f9f9fUL, 0x93939393UL, 0xc9c9c9c9UL, 0x9c9c9c9cUL, 0xefefefefUL,
    0xa0a0a0a0UL, 0xe0e0e0e0UL, 0x3b3b3b3bUL, 0x4d4d4d4dUL, 0xaeaeaeaeUL, 0x2a2a2a2aUL, 0xf5f5f5f5UL, 0xb0b0b0b0UL,
    0xc8c8c8c8UL, 0xebebebebUL, 0xbbbbbbbbUL, 0x3c3c3c3cUL, 0x83838383UL, 0x53535353UL, 0x99999999UL, 0x61616161UL,
    0x17171717UL, 0x2b2b2b2bUL, 0x04040404UL, 0x7e7e7e7eUL, 0xbabababaUL, 0x77777777UL, 0xd6d6d6d6UL, 0x26262626UL,
    0xe1e1e1e1UL, 0x69696969UL, 0x14141414UL, 0x63636363UL, 0x55555555UL, 0x21212121UL, 0x0c0c0c0cUL, 0x7d7d7d7dUL,
};


#define Te0(x) TE0[x]
#define Te1(x) TE1[x]
#define Te2(x) TE2[x]
#define Te3(x) TE3[x]

#define Td0(x) TD0[x]
#define Td1(x) TD1[x]
#define Td2(x) TD2[x]
#define Td3(x) TD3[x]

LQ_ALIGN(128) static const uint32_t TE1[256] = {
    0xa5c66363UL, 0x84f87c7cUL, 0x99ee7777UL, 0x8df67b7bUL, 0x0dfff2f2UL, 0xbdd66b6bUL, 0xb1de6f6fUL, 0x5491c5c5UL,
    0x50603030UL, 0x03020101UL, 0xa9ce6767UL, 0x7d562b2bUL, 0x19e7fefeUL, 0x62b5d7d7UL, 0xe64dababUL, 0x9aec7676UL,
    0x458fcacaUL, 0x9d1f8282UL, 0x4089c9c9UL, 0x87fa7d7dUL, 0x15effafaUL, 0xebb25959UL, 0xc98e4747UL, 0x0bfbf0f0UL,
    0xec41adadUL, 0x67b3d4d4UL, 0xfd5fa2a2UL, 0xea45afafUL, 0xbf239c9cUL, 0xf753a4a4UL, 0x96e47272UL, 0x5b9bc0c0UL,
    0xc275b7b7UL, 0x1ce1fdfdUL, 0xae3d9393UL, 0x6a4c2626UL, 0x5a6c3636UL, 0x417e3f3fUL, 0x02f5f7f7UL, 0x4f83ccccUL,
    0x5c683434UL, 0xf451a5a5UL, 0x34d1e5e5UL, 0x08f9f1f1UL, 0x93e27171UL, 0x73abd8d8UL, 0x53623131UL, 0x3f2a1515UL,
    0x0c080404UL, 0x5295c7c7UL, 0x65462323UL, 0x5e9dc3c3UL, 0x28301818UL, 0xa1379696UL, 0x0f0a0505UL, 0xb52f9a9aUL,
    0x090e0707UL, 0x36241212UL, 0x9b1b8080UL, 0x3ddfe2e2UL, 0x26cdebebUL, 0x694e2727UL, 0xcd7fb2b2UL, 0x9fea7575UL,
    0x1b120909UL, 0x9e1d8383UL, 0x74582c2cUL, 0x2e341a1aUL, 0x2d361b1bUL, 0xb2dc6e6eUL, 0xeeb45a5aUL, 0xfb5ba0a0UL,
    0xf6a45252UL, 0x4d763b3bUL, 0x61b7d6d6UL, 0xce7db3b3UL, 0x7b522929UL, 0x3edde3e3UL, 0x715e2f2fUL, 0x97138484UL,
    0xf5a65353UL, 0x68b9d1d1UL, 0x00000000UL, 0x2cc1ededUL, 0x60402020UL, 0x1fe3fcfcUL, 0xc879b1b1UL, 0xedb65b5bUL,
    0xbed46a6aUL, 0x468dcbcbUL, 0xd967bebeUL, 0x4b723939UL, 0xde944a4aUL, 0xd4984c4cUL, 0xe8b05858UL, 0x4a85cfcfUL,
    0x6bbbd0d0UL, 0x2ac5efefUL, 0xe54faaaaUL, 0x16edfbfbUL, 0xc5864343UL, 0xd79a4d4dUL, 0x55663333UL, 0x94118585UL,
    0xcf8a4545UL, 0x10e9f9f9UL, 0x06040202UL, 0x81fe7f7fUL, 0xf0a05050UL, 0x44783c3cUL, 0xba259f9fUL, 0xe34ba8a8UL,
    0xf3a25151UL, 0xfe5da3a3UL, 0xc0804040UL, 0x8a058f8fUL, 0xad3f9292UL, 0xbc219d9dUL, 0x48703838UL, 0x04f1f5f5UL,
    0xdf63bcbcUL, 0xc177b6b6UL, 0x75afdadaUL, 0x63422121UL, 0x30201010UL, 0x1ae5ffffUL, 0x0efdf3f3UL, 0x6dbfd2d2UL,
    0x4c81cdcdUL, 0x14180c0cUL, 0x35261313UL, 0x2fc3ececUL, 0xe1be5f5fUL, 0xa2359797UL, 0xcc884444UL, 0x392e1717UL,
    0x5793c4c4UL, 0xf255a7a7UL, 0x82fc7e7eUL, 0x477a3d3dUL, 0xacc86464UL, 0xe7ba5d5dUL, 0x2b321919UL, 0x95e67373UL,
    0xa0c06060UL, 0x98198181UL, 0xd19e4f4fUL, 0x7fa3dcdcUL, 0x66442222UL, 0x7e542a2aUL, 0xab3b9090UL, 0x830b8888UL,
    0xca8c4646UL, 0x29c7eeeeUL, 0xd36bb8b8UL, 0x3c281414UL, 0x79a7dedeUL, 0xe2bc5e5eUL, 0x1d160b0bUL, 0x76addbdbUL,
    0x3bdbe0e0UL, 0x56643232UL, 0x4e743a3aUL, 0x1e140a0aUL, 0xdb924949UL, 0x0a0c0606UL, 0x6c482424UL, 0xe4b85c5cUL,
    0x5d9fc2c2UL, 0x6ebdd3d3UL, 0xef43acacUL, 0xa6c46262UL, 0xa8399191UL, 0xa4319595UL, 0x37d3e4e4UL, 0x8bf27979UL,
    0x32d5e7e7UL, 0x438bc8c8UL, 0x596e3737UL, 0xb7da6d6dUL, 0x8c018d8dUL, 0x64b1d5d5UL, 0xd29c4e4eUL, 0xe049a9a9UL,
    0xb4d86c6cUL, 0xfaac5656UL, 0x07f3f4f4UL, 0x25cfeaeaUL, 0xafca6565UL, 0x8ef47a7aUL, 0xe947aeaeUL, 0x18100808UL,
    0xd56fbabaUL, 0x88f07878UL, 0x6f4a2525UL, 0x725c2e2eUL, 0x24381c1cUL, 0xf157a6a6UL, 0xc773b4b4UL, 0x5197c6c6UL,
    0x23cbe8e8UL, 0x7ca1ddddUL, 0x9ce87474UL, 0x213e1f1fUL, 0xdd964b4bUL, 0xdc61bdbdUL, 0x860d8b8bUL, 0x850f8a8aUL,
    0x90e07070UL, 0x427c3e3eUL, 0xc471b5b5UL, 0xaacc6666UL, 0xd8904848UL, 0x05060303UL, 0x01f7f6f6UL, 0x121c0e0eUL,
    0xa3c26161UL, 0x5f6a3535UL, 0xf9ae5757UL, 0xd069b9b9UL, 0x91178686UL, 0x5899c1c1UL, 0x273a1d1dUL, 0xb9279e9eUL,
    0x38d9e1e1UL, 0x13ebf8f8UL, 0xb32b9898UL, 0x33221111UL, 0xbbd26969UL, 0x70a9d9d9UL, 0x89078e8eUL, 0xa7339494UL,
    0xb62d9b9bUL, 0x223c1e1eUL, 0x92158787UL, 0x20c9e9e9UL, 0x4987ceceUL, 0xffaa5555UL, 0x78502828UL, 0x7aa5dfdfUL,
    0x8f038c8cUL, 0xf859a1a1UL, 0x80098989UL, 0x171a0d0dUL, 0xda65bfbfUL, 0x31d7e6e6UL, 0xc6844242UL, 0xb8d06868UL,
    0xc3824141UL, 0xb0299999UL, 0x775a2d2dUL, 0x111e0f0fUL, 0xcb7bb0b0UL, 0xfca85454UL, 0xd66dbbbbUL, 0x3a2c1616UL,
};
LQ_ALIGN(128) static const uint32_t TE2[256] = {
    0x63a5c663UL, 0x7c84f87cUL, 0x7799ee77UL, 0x7b8df67bUL, 0xf20dfff2UL, 0x6bbdd66bUL, 0x6fb1de6fUL, 0xc55491c5UL,
    0x30506030UL, 0x01030201UL, 0x67a9ce67UL, 0x2b7d562bUL, 0xfe19e7feUL, 0xd762b5d7UL, 0xabe64dabUL, 0x769aec76UL,
    0xca458fcaUL, 0x829d1f82UL, 0xc94089c9UL, 0x7d87fa7dUL, 0xfa15effaUL, 0x59ebb259UL, 0x47c98e47UL, 0xf00bfbf0UL,
    0xadec41adUL, 0xd467b3d4UL, 0xa2fd5fa2UL, 0xafea45afUL, 0x9cbf239cUL, 0xa4f753a4UL, 0x7296e472UL, 0xc05b9bc0UL,
    0xb7c275b7UL, 0xfd1ce1fdUL, 0x93ae3d93UL, 0x266a4c26UL, 0x365a6c36UL, 0x3f417e3fUL, 0xf702f5f7UL, 0xcc4f83ccUL,
    0x345c6834UL, 0xa5f451a5UL, 0xe534d1e5UL, 0xf108f9f1UL, 0x7193e271UL, 0xd873abd8UL, 0x31536231UL, 0x153f2a15UL,
    0x040c0804UL, 0xc75295c7UL, 0x23654623UL, 0xc35e9dc3UL, 0x18283018UL, 0x96a13796UL, 0x050f0a05UL, 0x9ab52f9aUL,
    0x07090e07UL, 0x12362412UL, 0x809b1b80UL, 0xe23ddfe2UL, 0xeb26cdebUL, 0x27694e27UL, 0xb2cd7fb2UL, 0x759fea75UL,
    0x091b1209UL, 0x839e1d83UL, 0x2c74582cUL, 0x1a2e341aUL, 0x1b2d361bUL, 0x6eb2dc6eUL, 0x5aeeb45aUL, 0xa0fb5ba0UL,
    0x52f6a452UL, 0x3b4d763bUL, 0xd661b7d6UL, 0xb3ce7db3UL, 0x297b5229UL, 0xe33edde3UL, 0x2f715e2fUL, 0x84971384UL,
    0x53f5a653UL, 0xd168b9d1UL, 0x00000000UL, 0xed2cc1edUL, 0x20604020UL, 0xfc1fe3fcUL, 0xb1c879b1UL, 0x5bedb65bUL,
    0x6abed46aUL, 0xcb468dcbUL, 0xbed967beUL, 0x394b7239UL, 0x4ade944aUL, 0x4cd4984cUL, 0x58e8b058UL, 0xcf4a85cfUL,
    0xd06bbbd0UL, 0xef2ac5efUL, 0xaae54faaUL, 0xfb16edfbUL, 0x43c58643UL, 0x4dd79a4dUL, 0x33556633UL, 0x85941185UL,
    0x45cf8a45UL, 0xf910e9f9UL, 0x02060402UL, 0x7f81fe7fUL, 0x50f0a050UL, 0x3c44783cUL, 0x9fba259fUL, 0xa8e34ba8UL,
    0x51f3a251UL, 0xa3fe5da3UL, 0x40c08040UL, 0x8f8a058fUL, 0x92ad3f92UL, 0x9dbc219dUL, 0x38487038UL, 0xf504f1f5UL,
    0xbcdf63bcUL, 0xb6c177b6UL, 0xda75afdaUL, 0x21634221UL, 0x10302010UL, 0xff1ae5ffUL, 0xf30efdf3UL, 0xd26dbfd2UL,
    0xcd4c81cdUL, 0x0c14180cUL, 0x13352613UL, 0xec2fc3ecUL, 0x5fe1be5fUL, 0x97a23597UL, 0x44cc8844UL, 0x17392e17UL,
    0xc45793c4UL, 0xa7f255a7UL, 0x7e82fc7eUL, 0x3d477a3dUL, 0x64acc864UL, 0x5de7ba5dUL, 0x192b3219UL, 0x7395e673UL,
    0x60a0c060UL, 0x81981981UL, 0x4fd19e4fUL, 0xdc7fa3dcUL, 0x22664422UL, 0x2a7e542aUL, 0x90ab3b90UL, 0x88830b88UL,
    0x46ca8c46UL, 0xee29c7eeUL, 0xb8d36bb8UL, 0x143c2814UL, 0xde79a7deUL, 0x5ee2bc5eUL, 0x0b1d160bUL, 0xdb76addbUL,
    0xe03bdbe0UL, 0x32566432UL, 0x3a4e743aUL, 0x0a1e140aUL, 0x49db9249UL, 0x060a0c06UL, 0x246c4824UL, 0x5ce4b85cUL,
    0xc25d9fc2UL, 0xd36ebdd3UL, 0xacef43acUL, 0x62a6c462UL, 0x91a83991UL, 0x95a43195UL, 0xe437d3e4UL, 0x798bf279UL,
    0xe732d5e7UL, 0xc8438bc8UL, 0x37596e37UL, 0x6db7da6dUL, 0x8d8c018dUL, 0xd564b1d5UL, 0x4ed29c4eUL, 0xa9e049a9UL,
    0x6cb4d86cUL, 0x56faac56UL, 0xf407f3f4UL, 0xea25cfeaUL, 0x65afca65UL, 0x7a8ef47aUL, 0xaee947aeUL, 0x08181008UL,
    0xbad56fbaUL, 0x7888f078UL, 0x256f4a25UL, 0x2e725c2eUL, 0x1c24381cUL, 0xa6f157a6UL, 0xb4c773b4UL, 0xc65197c6UL,
    0xe823cbe8UL, 0xdd7ca1ddUL, 0x749ce874UL, 0x1f213e1fUL, 0x4bdd964bUL, 0xbddc61bdUL, 0x8b860d8bUL, 0x8a850f8aUL,
    0x7090e070UL, 0x3e427c3eUL, 0xb5c471b5UL, 0x66aacc66UL, 0x48d89048UL, 0x03050603UL, 0xf601f7f6UL, 0x0e121c0eUL,
    0x61a3c261UL, 0x355f6a35UL, 0x57f9ae57UL, 0xb9d069b9UL, 0x86911786UL, 0xc15899c1UL, 0x1d273a1dUL, 0x9eb9279eUL,
    0xe138d9e1UL, 0xf813ebf8UL, 0x98b32b98UL, 0x11332211UL, 0x69bbd269UL, 0xd970a9d9UL, 0x8e89078eUL, 0x94a73394UL,
    0x9bb62d9bUL, 0x1e223c1eUL, 0x87921587UL, 0xe920c9e9UL, 0xce4987ceUL, 0x55ffaa55UL, 0x28785028UL, 0xdf7aa5dfUL,
    0x8c8f038cUL, 0xa1f859a1UL, 0x89800989UL, 0x0d171a0dUL, 0xbfda65bfUL, 0xe631d7e6UL, 0x42c68442UL, 0x68b8d068UL,
    0x41c38241UL, 0x99b02999UL, 0x2d775a2dUL, 0x0f111e0fUL, 0xb0cb7bb0UL, 0x54fca854UL, 0xbbd66dbbUL, 0x163a2c16UL,
};

LQ_ALIGN(128) static const uint32_t TE3[256] = {
    0x6363a5c6UL, 0x7c7c84f8UL, 0x777799eeUL, 0x7b7b8df6UL, 0xf2f20dffUL, 0x6b6bbdd6UL, 0x6f6fb1deUL, 0xc5c55491UL,
    0x30305060UL, 0x01010302UL, 0x6767a9ceUL, 0x2b2b7d56UL, 0xfefe19e7UL, 0xd7d762b5UL, 0xababe64dUL, 0x76769aecUL,
    0xcaca458fUL, 0x82829d1fUL, 0xc9c94089UL, 0x7d7d87faUL, 0xfafa15efUL, 0x5959ebb2UL, 0x4747c98eUL, 0xf0f00bfbUL,
    0xadadec41UL, 0xd4d467b3UL, 0xa2a2fd5fUL, 0xafafea45UL, 0x9c9cbf23UL, 0xa4a4f753UL, 0x727296e4UL, 0xc0c05b9bUL,
    0xb7b7c275UL, 0xfdfd1ce1UL, 0x9393ae3dUL, 0x26266a4cUL, 0x36365a6cUL, 0x3f3f417eUL, 0xf7f702f5UL, 0xcccc4f83UL,
    0x34345c68UL, 0xa5a5f451UL, 0xe5e534d1UL, 0xf1f108f9UL, 0x717193e2UL, 0xd8d873abUL, 0x31315362UL, 0x15153f2aUL,
    0x04040c08UL, 0xc7c75295UL, 0x23236546UL, 0xc3c35e9dUL, 0x18182830UL, 0x9696a137UL, 0x05050f0aUL, 0x9a9ab52fUL,
    0x0707090eUL, 0x12123624UL, 0x80809b1bUL, 0xe2e23ddfUL, 0xebeb26cdUL, 0x2727694eUL, 0xb2b2cd7fUL, 0x75759feaUL,
    0x09091b12UL, 0x83839e1dUL, 0x2c2c7458UL, 0x1a1a2e34UL, 0x1b1b2d36UL, 0x6e6eb2dcUL, 0x5a5aeeb4UL, 0xa0a0fb5bUL,
    0x5252f6a4UL, 0x3b3b4d76UL, 0xd6d661b7UL, 0xb3b3ce7dUL, 0x29297b52UL, 0xe3e33eddUL, 0x2f2f715eUL, 0x84849713UL,
    0x5353f5a6UL, 0xd1d168b9UL, 0x00000000UL, 0xeded2cc1UL, 0x20206040UL, 0xfcfc1fe3UL, 0xb1b1c879UL, 0x5b5bedb6UL,
    0x6a6abed4UL, 0xcbcb468dUL, 0xbebed967UL, 0x39394b72UL, 0x4a4ade94UL, 0x4c4cd498UL, 0x5858e8b0UL, 0xcfcf4a85UL,
    0xd0d06bbbUL, 0xefef2ac5UL, 0xaaaae54fUL, 0xfbfb16edUL, 0x4343c586UL, 0x4d4dd79aUL, 0x33335566UL, 0x85859411UL,
    0x4545cf8aUL, 0xf9f910e9UL, 0x02020604UL, 0x7f7f81feUL, 0x5050f0a0UL, 0x3c3c4478UL, 0x9f9fba25UL, 0xa8a8e34bUL,
    0x5151f3a2UL, 0xa3a3fe5dUL, 0x4040c080UL, 0x8f8f8a05UL, 0x9292ad3fUL, 0x9d9dbc21UL, 0x38384870UL, 0xf5f504f1UL,
    0xbcbcdf63UL, 0xb6b6c177UL, 0xdada75afUL, 0x21216342UL, 0x10103020UL, 0xffff1ae5UL, 0xf3f30efdUL, 0xd2d26dbfUL,
    0xcdcd4c81UL, 0x0c0c1418UL, 0x13133526UL, 0xecec2fc3UL, 0x5f5fe1beUL, 0x9797a235UL, 0x4444cc88UL, 0x1717392eUL,
    0xc4c45793UL, 0xa7a7f255UL, 0x7e7e82fcUL, 0x3d3d477aUL, 0x6464acc8UL, 0x5d5de7baUL, 0x19192b32UL, 0x737395e6UL,
    0x6060a0c0UL, 0x81819819UL, 0x4f4fd19eUL, 0xdcdc7fa3UL, 0x22226644UL, 0x2a2a7e54UL, 0x9090ab3bUL, 0x8888830bUL,
    0x4646ca8cUL, 0xeeee29c7UL, 0xb8b8d36bUL, 0x14143c28UL, 0xdede79a7UL, 0x5e5ee2bcUL, 0x0b0b1d16UL, 0xdbdb76adUL,
    0xe0e03bdbUL, 0x32325664UL, 0x3a3a4e74UL, 0x0a0a1e14UL, 0x4949db92UL, 0x06060a0cUL, 0x24246c48UL, 0x5c5ce4b8UL,
    0xc2c25d9fUL, 0xd3d36ebdUL, 0xacacef43UL, 0x6262a6c4UL, 0x9191a839UL, 0x9595a431UL, 0xe4e437d3UL, 0x79798bf2UL,
    0xe7e732d5UL, 0xc8c8438bUL, 0x3737596eUL, 0x6d6db7daUL, 0x8d8d8c01UL, 0xd5d564b1UL, 0x4e4ed29cUL, 0xa9a9e049UL,
    0x6c6cb4d8UL, 0x5656faacUL, 0xf4f407f3UL, 0xeaea25cfUL, 0x6565afcaUL, 0x7a7a8ef4UL, 0xaeaee947UL, 0x08081810UL,
    0xbabad56fUL, 0x787888f0UL, 0x25256f4aUL, 0x2e2e725cUL, 0x1c1c2438UL, 0xa6a6f157UL, 0xb4b4c773UL, 0xc6c65197UL,
    0xe8e823cbUL, 0xdddd7ca1UL, 0x74749ce8UL, 0x1f1f213eUL, 0x4b4bdd96UL, 0xbdbddc61UL, 0x8b8b860dUL, 0x8a8a850fUL,
    0x707090e0UL, 0x3e3e427cUL, 0xb5b5c471UL, 0x6666aaccUL, 0x4848d890UL, 0x03030506UL, 0xf6f601f7UL, 0x0e0e121cUL,
    0x6161a3c2UL, 0x35355f6aUL, 0x5757f9aeUL, 0xb9b9d069UL, 0x86869117UL, 0xc1c15899UL, 0x1d1d273aUL, 0x9e9eb927UL,
    0xe1e138d9UL, 0xf8f813ebUL, 0x9898b32bUL, 0x11113322UL, 0x6969bbd2UL, 0xd9d970a9UL, 0x8e8e8907UL, 0x9494a733UL,
    0x9b9bb62dUL, 0x1e1e223cUL, 0x87879215UL, 0xe9e920c9UL, 0xcece4987UL, 0x5555ffaaUL, 0x28287850UL, 0xdfdf7aa5UL,
    0x8c8c8f03UL, 0xa1a1f859UL, 0x89898009UL, 0x0d0d171aUL, 0xbfbfda65UL, 0xe6e631d7UL, 0x4242c684UL, 0x6868b8d0UL,
    0x4141c382UL, 0x9999b029UL, 0x2d2d775aUL, 0x0f0f111eUL, 0xb0b0cb7bUL, 0x5454fca8UL, 0xbbbbd66dUL, 0x16163a2cUL,
};

#ifndef PELI_TAB
LQ_ALIGN(128) static const uint32_t Te4_0[] = {
    0x00000063UL, 0x0000007cUL, 0x00000077UL, 0x0000007bUL, 0x000000f2UL, 0x0000006bUL, 0x0000006fUL, 0x000000c5UL,
    0x00000030UL, 0x00000001UL, 0x00000067UL, 0x0000002bUL, 0x000000feUL, 0x000000d7UL, 0x000000abUL, 0x00000076UL,
    0x000000caUL, 0x00000082UL, 0x000000c9UL, 0x0000007dUL, 0x000000faUL, 0x00000059UL, 0x00000047UL, 0x000000f0UL,
    0x000000adUL, 0x000000d4UL, 0x000000a2UL, 0x000000afUL, 0x0000009cUL, 0x000000a4UL, 0x00000072UL, 0x000000c0UL,
    0x000000b7UL, 0x000000fdUL, 0x00000093UL, 0x00000026UL, 0x00000036UL, 0x0000003fUL, 0x000000f7UL, 0x000000ccUL,
    0x00000034UL, 0x000000a5UL, 0x000000e5UL, 0x000000f1UL, 0x00000071UL, 0x000000d8UL, 0x00000031UL, 0x00000015UL,
    0x00000004UL, 0x000000c7UL, 0x00000023UL, 0x000000c3UL, 0x00000018UL, 0x00000096UL, 0x00000005UL, 0x0000009aUL,
    0x00000007UL, 0x00000012UL, 0x00000080UL, 0x000000e2UL, 0x000000ebUL, 0x00000027UL, 0x000000b2UL, 0x00000075UL,
    0x00000009UL, 0x00000083UL, 0x0000002cUL, 0x0000001aUL, 0x0000001bUL, 0x0000006eUL, 0x0000005aUL, 0x000000a0UL,
    0x00000052UL, 0x0000003bUL, 0x000000d6UL, 0x000000b3UL, 0x00000029UL, 0x000000e3UL, 0x0000002fUL, 0x00000084UL,
    0x00000053UL, 0x000000d1UL, 0x00000000UL, 0x000000edUL, 0x00000020UL, 0x000000fcUL, 0x000000b1UL, 0x0000005bUL,
    0x0000006aUL, 0x000000cbUL, 0x000000beUL, 0x00000039UL, 0x0000004aUL, 0x0000004cUL, 0x00000058UL, 0x000000cfUL,
    0x000000d0UL, 0x000000efUL, 0x000000aaUL, 0x000000fbUL, 0x00000043UL, 0x0000004dUL, 0x00000033UL, 0x00000085UL,
    0x00000045UL, 0x000000f9UL, 0x00000002UL, 0x0000007fUL, 0x00000050UL, 0x0000003cUL, 0x0000009fUL, 0x000000a8UL,
    0x00000051UL, 0x000000a3UL, 0x00000040UL, 0x0000008fUL, 0x00000092UL, 0x0000009dUL, 0x00000038UL, 0x000000f5UL,
    0x000000bcUL, 0x000000b6UL, 0x000000daUL, 0x00000021UL, 0x00000010UL, 0x000000ffUL, 0x000000f3UL, 0x000000d2UL,
    0x000000cdUL, 0x0000000cUL, 0x00000013UL, 0x000000ecUL, 0x0000005fUL, 0x00000097UL, 0x00000044UL, 0x00000017UL,
    0x000000c4UL, 0x000000a7UL, 0x0000007eUL, 0x0000003dUL, 0x00000064UL, 0x0000005dUL, 0x00000019UL, 0x00000073UL,
    0x00000060UL, 0x00000081UL, 0x0000004fUL, 0x000000dcUL, 0x00000022UL, 0x0000002aUL, 0x00000090UL, 0x00000088UL,
    0x00000046UL, 0x000000eeUL, 0x000000b8UL, 0x00000014UL, 0x000000deUL, 0x0000005eUL, 0x0000000bUL, 0x000000dbUL,
    0x000000e0UL, 0x00000032UL, 0x0000003aUL, 0x0000000aUL, 0x00000049UL, 0x00000006UL, 0x00000024UL, 0x0000005cUL,
    0x000000c2UL, 0x000000d3UL, 0x000000acUL, 0x00000062UL, 0x00000091UL, 0x00000095UL, 0x000000e4UL, 0x00000079UL,
    0x000000e7UL, 0x000000c8UL, 0x00000037UL, 0x0000006dUL, 0x0000008dUL, 0x000000d5UL, 0x0000004eUL, 0x000000a9UL,
    0x0000006cUL, 0x00000056UL, 0x000000f4UL, 0x000000eaUL, 0x00000065UL, 0x0000007aUL, 0x000000aeUL, 0x00000008UL,
    0x000000baUL, 0x00000078UL, 0x00000025UL, 0x0000002eUL, 0x0000001cUL, 0x000000a6UL, 0x000000b4UL, 0x000000c6UL,
    0x000000e8UL, 0x000000ddUL, 0x00000074UL, 0x0000001fUL, 0x0000004bUL, 0x000000bdUL, 0x0000008bUL, 0x0000008aUL,
    0x00000070UL, 0x0000003eUL, 0x000000b5UL, 0x00000066UL, 0x00000048UL, 0x00000003UL, 0x000000f6UL, 0x0000000eUL,
    0x00000061UL, 0x00000035UL, 0x00000057UL, 0x000000b9UL, 0x00000086UL, 0x000000c1UL, 0x0000001dUL, 0x0000009eUL,
    0x000000e1UL, 0x000000f8UL, 0x00000098UL, 0x00000011UL, 0x00000069UL, 0x000000d9UL, 0x0000008eUL, 0x00000094UL,
    0x0000009bUL, 0x0000001eUL, 0x00000087UL, 0x000000e9UL, 0x000000ceUL, 0x00000055UL, 0x00000028UL, 0x000000dfUL,
    0x0000008cUL, 0x000000a1UL, 0x00000089UL, 0x0000000dUL, 0x000000bfUL, 0x000000e6UL, 0x00000042UL, 0x00000068UL,
    0x00000041UL, 0x00000099UL, 0x0000002dUL, 0x0000000fUL, 0x000000b0UL, 0x00000054UL, 0x000000bbUL, 0x00000016UL
};

LQ_ALIGN(128) static const uint32_t Te4_1[] = {
    0x00006300UL, 0x00007c00UL, 0x00007700UL, 0x00007b00UL, 0x0000f200UL, 0x00006b00UL, 0x00006f00UL, 0x0000c500UL,
    0x00003000UL, 0x00000100UL, 0x00006700UL, 0x00002b00UL, 0x0000fe00UL, 0x0000d700UL, 0x0000ab00UL, 0x00007600UL,
    0x0000ca00UL, 0x00008200UL, 0x0000c900UL, 0x00007d00UL, 0x0000fa00UL, 0x00005900UL, 0x00004700UL, 0x0000f000UL,
    0x0000ad00UL, 0x0000d400UL, 0x0000a200UL, 0x0000af00UL, 0x00009c00UL, 0x0000a400UL, 0x00007200UL, 0x0000c000UL,
    0x0000b700UL, 0x0000fd00UL, 0x00009300UL, 0x00002600UL, 0x00003600UL, 0x00003f00UL, 0x0000f700UL, 0x0000cc00UL,
    0x00003400UL, 0x0000a500UL, 0x0000e500UL, 0x0000f100UL, 0x00007100UL, 0x0000d800UL, 0x00003100UL, 0x00001500UL,
    0x00000400UL, 0x0000c700UL, 0x00002300UL, 0x0000c300UL, 0x00001800UL, 0x00009600UL, 0x00000500UL, 0x00009a00UL,
    0x00000700UL, 0x00001200UL, 0x00008000UL, 0x0000e200UL, 0x0000eb00UL, 0x00002700UL, 0x0000b200UL, 0x00007500UL,
    0x00000900UL, 0x00008300UL, 0x00002c00UL, 0x00001a00UL, 0x00001b00UL, 0x00006e00UL, 0x00005a00UL, 0x0000a000UL,
    0x00005200UL, 0x00003b00UL, 0x0000d600UL, 0x0000b300UL, 0x00002900UL, 0x0000e300UL, 0x00002f00UL, 0x00008400UL,
    0x00005300UL, 0x0000d100UL, 0x00000000UL, 0x0000ed00UL, 0x00002000UL, 0x0000fc00UL, 0x0000b100UL, 0x00005b00UL,
    0x00006a00UL, 0x0000cb00UL, 0x0000be00UL, 0x00003900UL, 0x00004a00UL, 0x00004c00UL, 0x00005800UL, 0x0000cf00UL,
    0x0000d000UL, 0x0000ef00UL, 0x0000aa00UL, 0x0000fb00UL, 0x00004300UL, 0x00004d00UL, 0x00003300UL, 0x00008500UL,
    0x00004500UL, 0x0000f900UL, 0x00000200UL, 0x00007f00UL, 0x00005000UL, 0x00003c00UL, 0x00009f00UL, 0x0000a800UL,
    0x00005100UL, 0x0000a300UL, 0x00004000UL, 0x00008f00UL, 0x00009200UL, 0x00009d00UL, 0x00003800UL, 0x0000f500UL,
    0x0000bc00UL, 0x0000b600UL, 0x0000da00UL, 0x00002100UL, 0x00001000UL, 0x0000ff00UL, 0x0000f300UL, 0x0000d200UL,
    0x0000cd00UL, 0x00000c00UL, 0x00001300UL, 0x0000ec00UL, 0x00005f00UL, 0x00009700UL, 0x00004400UL, 0x00001700UL,
    0x0000c400UL, 0x0000a700UL, 0x00007e00UL, 0x00003d00UL, 0x00006400UL, 0x00005d00UL, 0x00001900UL, 0x00007300UL,
    0x00006000UL, 0x00008100UL, 0x00004f00UL, 0x0000dc00UL, 0x00002200UL, 0x00002a00UL, 0x00009000UL, 0x00008800UL,
    0x00004600UL, 0x0000ee00UL, 0x0000b800UL, 0x00001400UL, 0x0000de00UL, 0x00005e00UL, 0x00000b00UL, 0x0000db00UL,
    0x0000e000UL, 0x00003200UL, 0x00003a00UL, 0x00000a00UL, 0x00004900UL, 0x00000600UL, 0x00002400UL, 0x00005c00UL,
    0x0000c200UL, 0x0000d300UL, 0x0000ac00UL, 0x00006200UL, 0x00009100UL, 0x00009500UL, 0x0000e400UL, 0x00007900UL,
    0x0000e700UL, 0x0000c800UL, 0x00003700UL, 0x00006d00UL, 0x00008d00UL, 0x0000d500UL, 0x00004e00UL, 0x0000a900UL,
    0x00006c00UL, 0x00005600UL, 0x0000f400UL, 0x0000ea00UL, 0x00006500UL, 0x00007a00UL, 0x0000ae00UL, 0x00000800UL,
    0x0000ba00UL, 0x00007800UL, 0x00002500UL, 0x00002e00UL, 0x00001c00UL, 0x0000a600UL, 0x0000b400UL, 0x0000c600UL,
    0x0000e800UL, 0x0000dd00UL, 0x00007400UL, 0x00001f00UL, 0x00004b00UL, 0x0000bd00UL, 0x00008b00UL, 0x00008a00UL,
    0x00007000UL, 0x00003e00UL, 0x0000b500UL, 0x00006600UL, 0x00004800UL, 0x00000300UL, 0x0000f600UL, 0x00000e00UL,
    0x00006100UL, 0x00003500UL, 0x00005700UL, 0x0000b900UL, 0x00008600UL, 0x0000c100UL, 0x00001d00UL, 0x00009e00UL,
    0x0000e100UL, 0x0000f800UL, 0x00009800UL, 0x00001100UL, 0x00006900UL, 0x0000d900UL, 0x00008e00UL, 0x00009400UL,
    0x00009b00UL, 0x00001e00UL, 0x00008700UL, 0x0000e900UL, 0x0000ce00UL, 0x00005500UL, 0x00002800UL, 0x0000df00UL,
    0x00008c00UL, 0x0000a100UL, 0x00008900UL, 0x00000d00UL, 0x0000bf00UL, 0x0000e600UL, 0x00004200UL, 0x00006800UL,
    0x00004100UL, 0x00009900UL, 0x00002d00UL, 0x00000f00UL, 0x0000b000UL, 0x00005400UL, 0x0000bb00UL, 0x00001600UL
};

LQ_ALIGN(128) static const uint32_t Te4_2[] = {
    0x00630000UL, 0x007c0000UL, 0x00770000UL, 0x007b0000UL, 0x00f20000UL, 0x006b0000UL, 0x006f0000UL, 0x00c50000UL,
    0x00300000UL, 0x00010000UL, 0x00670000UL, 0x002b0000UL, 0x00fe0000UL, 0x00d70000UL, 0x00ab0000UL, 0x00760000UL,
    0x00ca0000UL, 0x00820000UL, 0x00c90000UL, 0x007d0000UL, 0x00fa0000UL, 0x00590000UL, 0x00470000UL, 0x00f00000UL,
    0x00ad0000UL, 0x00d40000UL, 0x00a20000UL, 0x00af0000UL, 0x009c0000UL, 0x00a40000UL, 0x00720000UL, 0x00c00000UL,
    0x00b70000UL, 0x00fd0000UL, 0x00930000UL, 0x00260000UL, 0x00360000UL, 0x003f0000UL, 0x00f70000UL, 0x00cc0000UL,
    0x00340000UL, 0x00a50000UL, 0x00e50000UL, 0x00f10000UL, 0x00710000UL, 0x00d80000UL, 0x00310000UL, 0x00150000UL,
    0x00040000UL, 0x00c70000UL, 0x00230000UL, 0x00c30000UL, 0x00180000UL, 0x00960000UL, 0x00050000UL, 0x009a0000UL,
    0x00070000UL, 0x00120000UL, 0x00800000UL, 0x00e20000UL, 0x00eb0000UL, 0x00270000UL, 0x00b20000UL, 0x00750000UL,
    0x00090000UL, 0x00830000UL, 0x002c0000UL, 0x001a0000UL, 0x001b0000UL, 0x006e0000UL, 0x005a0000UL, 0x00a00000UL,
    0x00520000UL, 0x003b0000UL, 0x00d60000UL, 0x00b30000UL, 0x00290000UL, 0x00e30000UL, 0x002f0000UL, 0x00840000UL,
    0x00530000UL, 0x00d10000UL, 0x00000000UL, 0x00ed0000UL, 0x00200000UL, 0x00fc0000UL, 0x00b10000UL, 0x005b0000UL,
    0x006a0000UL, 0x00cb0000UL, 0x00be0000UL, 0x00390000UL, 0x004a0000UL, 0x004c0000UL, 0x00580000UL, 0x00cf0000UL,
    0x00d00000UL, 0x00ef0000UL, 0x00aa0000UL, 0x00fb0000UL, 0x00430000UL, 0x004d0000UL, 0x00330000UL, 0x00850000UL,
    0x00450000UL, 0x00f90000UL, 0x00020000UL, 0x007f0000UL, 0x00500000UL, 0x003c0000UL, 0x009f0000UL, 0x00a80000UL,
    0x00510000UL, 0x00a30000UL, 0x00400000UL, 0x008f0000UL, 0x00920000UL, 0x009d0000UL, 0x00380000UL, 0x00f50000UL,
    0x00bc0000UL, 0x00b60000UL, 0x00da0000UL, 0x00210000UL, 0x00100000UL, 0x00ff0000UL, 0x00f30000UL, 0x00d20000UL,
    0x00cd0000UL, 0x000c0000UL, 0x00130000UL, 0x00ec0000UL, 0x005f0000UL, 0x00970000UL, 0x00440000UL, 0x00170000UL,
    0x00c40000UL, 0x00a70000UL, 0x007e0000UL, 0x003d0000UL, 0x00640000UL, 0x005d0000UL, 0x00190000UL, 0x00730000UL,
    0x00600000UL, 0x00810000UL, 0x004f0000UL, 0x00dc0000UL, 0x00220000UL, 0x002a0000UL, 0x00900000UL, 0x00880000UL,
    0x00460000UL, 0x00ee0000UL, 0x00b80000UL, 0x00140000UL, 0x00de0000UL, 0x005e0000UL, 0x000b0000UL, 0x00db0000UL,
    0x00e00000UL, 0x00320000UL, 0x003a0000UL, 0x000a0000UL, 0x00490000UL, 0x00060000UL, 0x00240000UL, 0x005c0000UL,
    0x00c20000UL, 0x00d30000UL, 0x00ac0000UL, 0x00620000UL, 0x00910000UL, 0x00950000UL, 0x00e40000UL, 0x00790000UL,
    0x00e70000UL, 0x00c80000UL, 0x00370000UL, 0x006d0000UL, 0x008d0000UL, 0x00d50000UL, 0x004e0000UL, 0x00a90000UL,
    0x006c0000UL, 0x00560000UL, 0x00f40000UL, 0x00ea0000UL, 0x00650000UL, 0x007a0000UL, 0x00ae0000UL, 0x00080000UL,
    0x00ba0000UL, 0x00780000UL, 0x00250000UL, 0x002e0000UL, 0x001c0000UL, 0x00a60000UL, 0x00b40000UL, 0x00c60000UL,
    0x00e80000UL, 0x00dd0000UL, 0x00740000UL, 0x001f0000UL, 0x004b0000UL, 0x00bd0000UL, 0x008b0000UL, 0x008a0000UL,
    0x00700000UL, 0x003e0000UL, 0x00b50000UL, 0x00660000UL, 0x00480000UL, 0x00030000UL, 0x00f60000UL, 0x000e0000UL,
    0x00610000UL, 0x00350000UL, 0x00570000UL, 0x00b90000UL, 0x00860000UL, 0x00c10000UL, 0x001d0000UL, 0x009e0000UL,
    0x00e10000UL, 0x00f80000UL, 0x00980000UL, 0x00110000UL, 0x00690000UL, 0x00d90000UL, 0x008e0000UL, 0x00940000UL,
    0x009b0000UL, 0x001e0000UL, 0x00870000UL, 0x00e90000UL, 0x00ce0000UL, 0x00550000UL, 0x00280000UL, 0x00df0000UL,
    0x008c0000UL, 0x00a10000UL, 0x00890000UL, 0x000d0000UL, 0x00bf0000UL, 0x00e60000UL, 0x00420000UL, 0x00680000UL,
    0x00410000UL, 0x00990000UL, 0x002d0000UL, 0x000f0000UL, 0x00b00000UL, 0x00540000UL, 0x00bb0000UL, 0x00160000UL
};

LQ_ALIGN(128) static const uint32_t Te4_3[] = {
    0x63000000UL, 0x7c000000UL, 0x77000000UL, 0x7b000000UL, 0xf2000000UL, 0x6b000000UL, 0x6f000000UL, 0xc5000000UL,
    0x30000000UL, 0x01000000UL, 0x67000000UL, 0x2b000000UL, 0xfe000000UL, 0xd7000000UL, 0xab000000UL, 0x76000000UL,
    0xca000000UL, 0x82000000UL, 0xc9000000UL, 0x7d000000UL, 0xfa000000UL, 0x59000000UL, 0x47000000UL, 0xf0000000UL,
    0xad000000UL, 0xd4000000UL, 0xa2000000UL, 0xaf000000UL, 0x9c000000UL, 0xa4000000UL, 0x72000000UL, 0xc0000000UL,
    0xb7000000UL, 0xfd000000UL, 0x93000000UL, 0x26000000UL, 0x36000000UL, 0x3f000000UL, 0xf7000000UL, 0xcc000000UL,
    0x34000000UL, 0xa5000000UL, 0xe5000000UL, 0xf1000000UL, 0x71000000UL, 0xd8000000UL, 0x31000000UL, 0x15000000UL,
    0x04000000UL, 0xc7000000UL, 0x23000000UL, 0xc3000000UL, 0x18000000UL, 0x96000000UL, 0x05000000UL, 0x9a000000UL,
    0x07000000UL, 0x12000000UL, 0x80000000UL, 0xe2000000UL, 0xeb000000UL, 0x27000000UL, 0xb2000000UL, 0x75000000UL,
    0x09000000UL, 0x83000000UL, 0x2c000000UL, 0x1a000000UL, 0x1b000000UL, 0x6e000000UL, 0x5a000000UL, 0xa0000000UL,
    0x52000000UL, 0x3b000000UL, 0xd6000000UL, 0xb3000000UL, 0x29000000UL, 0xe3000000UL, 0x2f000000UL, 0x84000000UL,
    0x53000000UL, 0xd1000000UL, 0x00000000UL, 0xed000000UL, 0x20000000UL, 0xfc000000UL, 0xb1000000UL, 0x5b000000UL,
    0x6a000000UL, 0xcb000000UL, 0xbe000000UL, 0x39000000UL, 0x4a000000UL, 0x4c000000UL, 0x58000000UL, 0xcf000000UL,
    0xd0000000UL, 0xef000000UL, 0xaa000000UL, 0xfb000000UL, 0x43000000UL, 0x4d000000UL, 0x33000000UL, 0x85000000UL,
    0x45000000UL, 0xf9000000UL, 0x02000000UL, 0x7f000000UL, 0x50000000UL, 0x3c000000UL, 0x9f000000UL, 0xa8000000UL,
    0x51000000UL, 0xa3000000UL, 0x40000000UL, 0x8f000000UL, 0x92000000UL, 0x9d000000UL, 0x38000000UL, 0xf5000000UL,
    0xbc000000UL, 0xb6000000UL, 0xda000000UL, 0x21000000UL, 0x10000000UL, 0xff000000UL, 0xf3000000UL, 0xd2000000UL,
    0xcd000000UL, 0x0c000000UL, 0x13000000UL, 0xec000000UL, 0x5f000000UL, 0x97000000UL, 0x44000000UL, 0x17000000UL,
    0xc4000000UL, 0xa7000000UL, 0x7e000000UL, 0x3d000000UL, 0x64000000UL, 0x5d000000UL, 0x19000000UL, 0x73000000UL,
    0x60000000UL, 0x81000000UL, 0x4f000000UL, 0xdc000000UL, 0x22000000UL, 0x2a000000UL, 0x90000000UL, 0x88000000UL,
    0x46000000UL, 0xee000000UL, 0xb8000000UL, 0x14000000UL, 0xde000000UL, 0x5e000000UL, 0x0b000000UL, 0xdb000000UL,
    0xe0000000UL, 0x32000000UL, 0x3a000000UL, 0x0a000000UL, 0x49000000UL, 0x06000000UL, 0x24000000UL, 0x5c000000UL,
    0xc2000000UL, 0xd3000000UL, 0xac000000UL, 0x62000000UL, 0x91000000UL, 0x95000000UL, 0xe4000000UL, 0x79000000UL,
    0xe7000000UL, 0xc8000000UL, 0x37000000UL, 0x6d000000UL, 0x8d000000UL, 0xd5000000UL, 0x4e000000UL, 0xa9000000UL,
    0x6c000000UL, 0x56000000UL, 0xf4000000UL, 0xea000000UL, 0x65000000UL, 0x7a000000UL, 0xae000000UL, 0x08000000UL,
    0xba000000UL, 0x78000000UL, 0x25000000UL, 0x2e000000UL, 0x1c000000UL, 0xa6000000UL, 0xb4000000UL, 0xc6000000UL,
    0xe8000000UL, 0xdd000000UL, 0x74000000UL, 0x1f000000UL, 0x4b000000UL, 0xbd000000UL, 0x8b000000UL, 0x8a000000UL,
    0x70000000UL, 0x3e000000UL, 0xb5000000UL, 0x66000000UL, 0x48000000UL, 0x03000000UL, 0xf6000000UL, 0x0e000000UL,
    0x61000000UL, 0x35000000UL, 0x57000000UL, 0xb9000000UL, 0x86000000UL, 0xc1000000UL, 0x1d000000UL, 0x9e000000UL,
    0xe1000000UL, 0xf8000000UL, 0x98000000UL, 0x11000000UL, 0x69000000UL, 0xd9000000UL, 0x8e000000UL, 0x94000000UL,
    0x9b000000UL, 0x1e000000UL, 0x87000000UL, 0xe9000000UL, 0xce000000UL, 0x55000000UL, 0x28000000UL, 0xdf000000UL,
    0x8c000000UL, 0xa1000000UL, 0x89000000UL, 0x0d000000UL, 0xbf000000UL, 0xe6000000UL, 0x42000000UL, 0x68000000UL,
    0x41000000UL, 0x99000000UL, 0x2d000000UL, 0x0f000000UL, 0xb0000000UL, 0x54000000UL, 0xbb000000UL, 0x16000000UL
};
#endif /* pelimac */

LQ_ALIGN(128) static const uint32_t TD1[256] = {
    0x5051f4a7UL, 0x537e4165UL, 0xc31a17a4UL, 0x963a275eUL, 0xcb3bab6bUL, 0xf11f9d45UL, 0xabacfa58UL, 0x934be303UL,
    0x552030faUL, 0xf6ad766dUL, 0x9188cc76UL, 0x25f5024cUL, 0xfc4fe5d7UL, 0xd7c52acbUL, 0x80263544UL, 0x8fb562a3UL,
    0x49deb15aUL, 0x6725ba1bUL, 0x9845ea0eUL, 0xe15dfec0UL, 0x02c32f75UL, 0x12814cf0UL, 0xa38d4697UL, 0xc66bd3f9UL,
    0xe7038f5fUL, 0x9515929cUL, 0xebbf6d7aUL, 0xda955259UL, 0x2dd4be83UL, 0xd3587421UL, 0x2949e069UL, 0x448ec9c8UL,
    0x6a75c289UL, 0x78f48e79UL, 0x6b99583eUL, 0xdd27b971UL, 0xb6bee14fUL, 0x17f088adUL, 0x66c920acUL, 0xb47dce3aUL,
    0x1863df4aUL, 0x82e51a31UL, 0x60975133UL, 0x4562537fUL, 0xe0b16477UL, 0x84bb6baeUL, 0x1cfe81a0UL, 0x94f9082bUL,
    0x58704868UL, 0x198f45fdUL, 0x8794de6cUL, 0xb7527bf8UL, 0x23ab73d3UL, 0xe2724b02UL, 0x57e31f8fUL, 0x2a6655abUL,
    0x07b2eb28UL, 0x032fb5c2UL, 0x9a86c57bUL, 0xa5d33708UL, 0xf2302887UL, 0xb223bfa5UL, 0xba02036aUL, 0x5ced1682UL,
    0x2b8acf1cUL, 0x92a779b4UL, 0xf0f307f2UL, 0xa14e69e2UL, 0xcd65daf4UL, 0xd50605beUL, 0x1fd13462UL, 0x8ac4a6feUL,
    0x9d342e53UL, 0xa0a2f355UL, 0x32058ae1UL, 0x75a4f6ebUL, 0x390b83ecUL, 0xaa4060efUL, 0x065e719fUL, 0x51bd6e10UL,
    0xf93e218aUL, 0x3d96dd06UL, 0xaedd3e05UL, 0x464de6bdUL, 0xb591548dUL, 0x0571c45dUL, 0x6f0406d4UL, 0xff605015UL,
    0x241998fbUL, 0x97d6bde9UL, 0xcc894043UL, 0x7767d99eUL, 0xbdb0e842UL, 0x8807898bUL, 0x38e7195bUL, 0xdb79c8eeUL,
    0x47a17c0aUL, 0xe97c420fUL, 0xc9f8841eUL, 0x00000000UL, 0x83098086UL, 0x48322bedUL, 0xac1e1170UL, 0x4e6c5a72UL,
    0xfbfd0effUL, 0x560f8538UL, 0x1e3daed5UL, 0x27362d39UL, 0x640a0fd9UL, 0x21685ca6UL, 0xd19b5b54UL, 0x3a24362eUL,
    0xb10c0a67UL, 0x0f9357e7UL, 0xd2b4ee96UL, 0x9e1b9b91UL, 0x4f80c0c5UL, 0xa261dc20UL, 0x695a774bUL, 0x161c121aUL,
    0x0ae293baUL, 0xe5c0a02aUL, 0x433c22e0UL, 0x1d121b17UL, 0x0b0e090dUL, 0xadf28bc7UL, 0xb92db6a8UL, 0xc8141ea9UL,
    0x8557f119UL, 0x4caf7507UL, 0xbbee99ddUL, 0xfda37f60UL, 0x9ff70126UL, 0xbc5c72f5UL, 0xc544663bUL, 0x345bfb7eUL,
    0x768b4329UL, 0xdccb23c6UL, 0x68b6edfcUL, 0x63b8e4f1UL, 0xcad731dcUL, 0x10426385UL, 0x40139722UL, 0x2084c611UL,
    0x7d854a24UL, 0xf8d2bb3dUL, 0x11aef932UL, 0x6dc729a1UL, 0x4b1d9e2fUL, 0xf3dcb230UL, 0xec0d8652UL, 0xd077c1e3UL,
    0x6c2bb316UL, 0x99a970b9UL, 0xfa119448UL, 0x2247e964UL, 0xc4a8fc8cUL, 0x1aa0f03fUL, 0xd8567d2cUL, 0xef223390UL,
    0xc787494eUL, 0xc1d938d1UL, 0xfe8ccaa2UL, 0x3698d40bUL, 0xcfa6f581UL, 0x28a57adeUL, 0x26dab78eUL, 0xa43fadbfUL,
    0xe42c3a9dUL, 0x0d507892UL, 0x9b6a5fccUL, 0x62547e46UL, 0xc2f68d13UL, 0xe890d8b8UL, 0x5e2e39f7UL, 0xf582c3afUL,
    0xbe9f5d80UL, 0x7c69d093UL, 0xa96fd52dUL, 0xb3cf2512UL, 0x3bc8ac99UL, 0xa710187dUL, 0x6ee89c63UL, 0x7bdb3bbbUL,
    0x09cd2678UL, 0xf46e5918UL, 0x01ec9ab7UL, 0xa8834f9aUL, 0x65e6956eUL, 0x7eaaffe6UL, 0x0821bccfUL, 0xe6ef15e8UL,
    0xd9bae79bUL, 0xce4a6f36UL, 0xd4ea9f09UL, 0xd629b07cUL, 0xaf31a4b2UL, 0x312a3f23UL, 0x30c6a594UL, 0xc035a266UL,
    0x37744ebcUL, 0xa6fc82caUL, 0xb0e090d0UL, 0x1533a7d8UL, 0x4af10498UL, 0xf741ecdaUL, 0x0e7fcd50UL, 0x2f1791f6UL,
    0x8d764dd6UL, 0x4d43efb0UL, 0x54ccaa4dUL, 0xdfe49604UL, 0xe39ed1b5UL, 0x1b4c6a88UL, 0xb8c12c1fUL, 0x7f466551UL,
    0x049d5eeaUL, 0x5d018c35UL, 0x73fa8774UL, 0x2efb0b41UL, 0x5ab3671dUL, 0x5292dbd2UL, 0x33e91056UL, 0x136dd647UL,
    0x8c9ad761UL, 0x7a37a10cUL, 0x8e59f814UL, 0x89eb133cUL, 0xeecea927UL, 0x35b761c9UL, 0xede11ce5UL, 0x3c7a47b1UL,
    0x599cd2dfUL, 0x3f55f273UL, 0x791814ceUL, 0xbf73c737UL, 0xea53f7cdUL, 0x5b5ffdaaUL, 0x14df3d6fUL, 0x867844dbUL,
    0x81caaff3UL, 0x3eb968c4UL, 0x2c382434UL, 0x5fc2a340UL, 0x72161dc3UL, 0x0cbce225UL, 0x8b283c49UL, 0x41ff0d95UL,
    0x7139a801UL, 0xde080cb3UL, 0x9cd8b4e4UL, 0x906456c1UL, 0x617bcb84UL, 0x70d532b6UL, 0x74486c5cUL, 0x42d0b857UL,
};
LQ_ALIGN(128) static const uint32_t TD2[256] = {
    0xa75051f4UL, 0x65537e41UL, 0xa4c31a17UL, 0x5e963a27UL, 0x6bcb3babUL, 0x45f11f9dUL, 0x58abacfaUL, 0x03934be3UL,
    0xfa552030UL, 0x6df6ad76UL, 0x769188ccUL, 0x4c25f502UL, 0xd7fc4fe5UL, 0xcbd7c52aUL, 0x44802635UL, 0xa38fb562UL,
    0x5a49deb1UL, 0x1b6725baUL, 0x0e9845eaUL, 0xc0e15dfeUL, 0x7502c32fUL, 0xf012814cUL, 0x97a38d46UL, 0xf9c66bd3UL,
    0x5fe7038fUL, 0x9c951592UL, 0x7aebbf6dUL, 0x59da9552UL, 0x832dd4beUL, 0x21d35874UL, 0x692949e0UL, 0xc8448ec9UL,
    0x896a75c2UL, 0x7978f48eUL, 0x3e6b9958UL, 0x71dd27b9UL, 0x4fb6bee1UL, 0xad17f088UL, 0xac66c920UL, 0x3ab47dceUL,
    0x4a1863dfUL, 0x3182e51aUL, 0x33609751UL, 0x7f456253UL, 0x77e0b164UL, 0xae84bb6bUL, 0xa01cfe81UL, 0x2b94f908UL,
    0x68587048UL, 0xfd198f45UL, 0x6c8794deUL, 0xf8b7527bUL, 0xd323ab73UL, 0x02e2724bUL, 0x8f57e31fUL, 0xab2a6655UL,
    0x2807b2ebUL, 0xc2032fb5UL, 0x7b9a86c5UL, 0x08a5d337UL, 0x87f23028UL, 0xa5b223bfUL, 0x6aba0203UL, 0x825ced16UL,
    0x1c2b8acfUL, 0xb492a779UL, 0xf2f0f307UL, 0xe2a14e69UL, 0xf4cd65daUL, 0xbed50605UL, 0x621fd134UL, 0xfe8ac4a6UL,
    0x539d342eUL, 0x55a0a2f3UL, 0xe132058aUL, 0xeb75a4f6UL, 0xec390b83UL, 0xefaa4060UL, 0x9f065e71UL, 0x1051bd6eUL,
    0x8af93e21UL, 0x063d96ddUL, 0x05aedd3eUL, 0xbd464de6UL, 0x8db59154UL, 0x5d0571c4UL, 0xd46f0406UL, 0x15ff6050UL,
    0xfb241998UL, 0xe997d6bdUL, 0x43cc8940UL, 0x9e7767d9UL, 0x42bdb0e8UL, 0x8b880789UL, 0x5b38e719UL, 0xeedb79c8UL,
    0x0a47a17cUL, 0x0fe97c42UL, 0x1ec9f884UL, 0x00000000UL, 0x86830980UL, 0xed48322bUL, 0x70ac1e11UL, 0x724e6c5aUL,
    0xfffbfd0eUL, 0x38560f85UL, 0xd51e3daeUL, 0x3927362dUL, 0xd9640a0fUL, 0xa621685cUL, 0x54d19b5bUL, 0x2e3a2436UL,
    0x67b10c0aUL, 0xe70f9357UL, 0x96d2b4eeUL, 0x919e1b9bUL, 0xc54f80c0UL, 0x20a261dcUL, 0x4b695a77UL, 0x1a161c12UL,
    0xba0ae293UL, 0x2ae5c0a0UL, 0xe0433c22UL, 0x171d121bUL, 0x0d0b0e09UL, 0xc7adf28bUL, 0xa8b92db6UL, 0xa9c8141eUL,
    0x198557f1UL, 0x074caf75UL, 0xddbbee99UL, 0x60fda37fUL, 0x269ff701UL, 0xf5bc5c72UL, 0x3bc54466UL, 0x7e345bfbUL,
    0x29768b43UL, 0xc6dccb23UL, 0xfc68b6edUL, 0xf163b8e4UL, 0xdccad731UL, 0x85104263UL, 0x22401397UL, 0x112084c6UL,
    0x247d854aUL, 0x3df8d2bbUL, 0x3211aef9UL, 0xa16dc729UL, 0x2f4b1d9eUL, 0x30f3dcb2UL, 0x52ec0d86UL, 0xe3d077c1UL,
    0x166c2bb3UL, 0xb999a970UL, 0x48fa1194UL, 0x642247e9UL, 0x8cc4a8fcUL, 0x3f1aa0f0UL, 0x2cd8567dUL, 0x90ef2233UL,
    0x4ec78749UL, 0xd1c1d938UL, 0xa2fe8ccaUL, 0x0b3698d4UL, 0x81cfa6f5UL, 0xde28a57aUL, 0x8e26dab7UL, 0xbfa43fadUL,
    0x9de42c3aUL, 0x920d5078UL, 0xcc9b6a5fUL, 0x4662547eUL, 0x13c2f68dUL, 0xb8e890d8UL, 0xf75e2e39UL, 0xaff582c3UL,
    0x80be9f5dUL, 0x937c69d0UL, 0x2da96fd5UL, 0x12b3cf25UL, 0x993bc8acUL, 0x7da71018UL, 0x636ee89cUL, 0xbb7bdb3bUL,
    0x7809cd26UL, 0x18f46e59UL, 0xb701ec9aUL, 0x9aa8834fUL, 0x6e65e695UL, 0xe67eaaffUL, 0xcf0821bcUL, 0xe8e6ef15UL,
    0x9bd9bae7UL, 0x36ce4a6fUL, 0x09d4ea9fUL, 0x7cd629b0UL, 0xb2af31a4UL, 0x23312a3fUL, 0x9430c6a5UL, 0x66c035a2UL,
    0xbc37744eUL, 0xcaa6fc82UL, 0xd0b0e090UL, 0xd81533a7UL, 0x984af104UL, 0xdaf741ecUL, 0x500e7fcdUL, 0xf62f1791UL,
    0xd68d764dUL, 0xb04d43efUL, 0x4d54ccaaUL, 0x04dfe496UL, 0xb5e39ed1UL, 0x881b4c6aUL, 0x1fb8c12cUL, 0x517f4665UL,
    0xea049d5eUL, 0x355d018cUL, 0x7473fa87UL, 0x412efb0bUL, 0x1d5ab367UL, 0xd25292dbUL, 0x5633e910UL, 0x47136dd6UL,
    0x618c9ad7UL, 0x0c7a37a1UL, 0x148e59f8UL, 0x3c89eb13UL, 0x27eecea9UL, 0xc935b761UL, 0xe5ede11cUL, 0xb13c7a47UL,
    0xdf599cd2UL, 0x733f55f2UL, 0xce791814UL, 0x37bf73c7UL, 0xcdea53f7UL, 0xaa5b5ffdUL, 0x6f14df3dUL, 0xdb867844UL,
    0xf381caafUL, 0xc43eb968UL, 0x342c3824UL, 0x405fc2a3UL, 0xc372161dUL, 0x250cbce2UL, 0x498b283cUL, 0x9541ff0dUL,
    0x017139a8UL, 0xb3de080cUL, 0xe49cd8b4UL, 0xc1906456UL, 0x84617bcbUL, 0xb670d532UL, 0x5c74486cUL, 0x5742d0b8UL,
};
LQ_ALIGN(128) static const uint32_t TD3[256] = {
    0xf4a75051UL, 0x4165537eUL, 0x17a4c31aUL, 0x275e963aUL, 0xab6bcb3bUL, 0x9d45f11fUL, 0xfa58abacUL, 0xe303934bUL,
    0x30fa5520UL, 0x766df6adUL, 0xcc769188UL, 0x024c25f5UL, 0xe5d7fc4fUL, 0x2acbd7c5UL, 0x35448026UL, 0x62a38fb5UL,
    0xb15a49deUL, 0xba1b6725UL, 0xea0e9845UL, 0xfec0e15dUL, 0x2f7502c3UL, 0x4cf01281UL, 0x4697a38dUL, 0xd3f9c66bUL,
    0x8f5fe703UL, 0x929c9515UL, 0x6d7aebbfUL, 0x5259da95UL, 0xbe832dd4UL, 0x7421d358UL, 0xe0692949UL, 0xc9c8448eUL,
    0xc2896a75UL, 0x8e7978f4UL, 0x583e6b99UL, 0xb971dd27UL, 0xe14fb6beUL, 0x88ad17f0UL, 0x20ac66c9UL, 0xce3ab47dUL,
    0xdf4a1863UL, 0x1a3182e5UL, 0x51336097UL, 0x537f4562UL, 0x6477e0b1UL, 0x6bae84bbUL, 0x81a01cfeUL, 0x082b94f9UL,
    0x48685870UL, 0x45fd198fUL, 0xde6c8794UL, 0x7bf8b752UL, 0x73d323abUL, 0x4b02e272UL, 0x1f8f57e3UL, 0x55ab2a66UL,
    0xeb2807b2UL, 0xb5c2032fUL, 0xc57b9a86UL, 0x3708a5d3UL, 0x2887f230UL, 0xbfa5b223UL, 0x036aba02UL, 0x16825cedUL,
    0xcf1c2b8aUL, 0x79b492a7UL, 0x07f2f0f3UL, 0x69e2a14eUL, 0xdaf4cd65UL, 0x05bed506UL, 0x34621fd1UL, 0xa6fe8ac4UL,
    0x2e539d34UL, 0xf355a0a2UL, 0x8ae13205UL, 0xf6eb75a4UL, 0x83ec390bUL, 0x60efaa40UL, 0x719f065eUL, 0x6e1051bdUL, 
    0x218af93eUL, 0xdd063d96UL, 0x3e05aeddUL, 0xe6bd464dUL, 0x548db591UL, 0xc45d0571UL, 0x06d46f04UL, 0x5015ff60UL,
    0x98fb2419UL, 0xbde997d6UL, 0x4043cc89UL, 0xd99e7767UL, 0xe842bdb0UL, 0x898b8807UL, 0x195b38e7UL, 0xc8eedb79UL,
    0x7c0a47a1UL, 0x420fe97cUL, 0x841ec9f8UL, 0x00000000UL, 0x80868309UL, 0x2bed4832UL, 0x1170ac1eUL, 0x5a724e6cUL,
    0x0efffbfdUL, 0x8538560fUL, 0xaed51e3dUL, 0x2d392736UL, 0x0fd9640aUL, 0x5ca62168UL, 0x5b54d19bUL, 0x362e3a24UL,
    0x0a67b10cUL, 0x57e70f93UL, 0xee96d2b4UL, 0x9b919e1bUL, 0xc0c54f80UL, 0xdc20a261UL, 0x774b695aUL, 0x121a161cUL,
    0x93ba0ae2UL, 0xa02ae5c0UL, 0x22e0433cUL, 0x1b171d12UL, 0x090d0b0eUL, 0x8bc7adf2UL, 0xb6a8b92dUL, 0x1ea9c814UL,
    0xf1198557UL, 0x75074cafUL, 0x99ddbbeeUL, 0x7f60fda3UL, 0x01269ff7UL, 0x72f5bc5cUL, 0x663bc544UL, 0xfb7e345bUL,
    0x4329768bUL, 0x23c6dccbUL, 0xedfc68b6UL, 0xe4f163b8UL, 0x31dccad7UL, 0x63851042UL, 0x97224013UL, 0xc6112084UL,
    0x4a247d85UL, 0xbb3df8d2UL, 0xf93211aeUL, 0x29a16dc7UL, 0x9e2f4b1dUL, 0xb230f3dcUL, 0x8652ec0dUL, 0xc1e3d077UL,
    0xb3166c2bUL, 0x70b999a9UL, 0x9448fa11UL, 0xe9642247UL, 0xfc8cc4a8UL, 0xf03f1aa0UL, 0x7d2cd856UL, 0x3390ef22UL,
    0x494ec787UL, 0x38d1c1d9UL, 0xcaa2fe8cUL, 0xd40b3698UL, 0xf581cfa6UL, 0x7ade28a5UL, 0xb78e26daUL, 0xadbfa43fUL,
    0x3a9de42cUL, 0x78920d50UL, 0x5fcc9b6aUL, 0x7e466254UL, 0x8d13c2f6UL, 0xd8b8e890UL, 0x39f75e2eUL, 0xc3aff582UL,
    0x5d80be9fUL, 0xd0937c69UL, 0xd52da96fUL, 0x2512b3cfUL, 0xac993bc8UL, 0x187da710UL, 0x9c636ee8UL, 0x3bbb7bdbUL,
    0x267809cdUL, 0x5918f46eUL, 0x9ab701ecUL, 0x4f9aa883UL, 0x956e65e6UL, 0xffe67eaaUL, 0xbccf0821UL, 0x15e8e6efUL,
    0xe79bd9baUL, 0x6f36ce4aUL, 0x9f09d4eaUL, 0xb07cd629UL, 0xa4b2af31UL, 0x3f23312aUL, 0xa59430c6UL, 0xa266c035UL,
    0x4ebc3774UL, 0x82caa6fcUL, 0x90d0b0e0UL, 0xa7d81533UL, 0x04984af1UL, 0xecdaf741UL, 0xcd500e7fUL, 0x91f62f17UL,
    0x4dd68d76UL, 0xefb04d43UL, 0xaa4d54ccUL, 0x9604dfe4UL, 0xd1b5e39eUL, 0x6a881b4cUL, 0x2c1fb8c1UL, 0x65517f46UL,
    0x5eea049dUL, 0x8c355d01UL, 0x877473faUL, 0x0b412efbUL, 0x671d5ab3UL, 0xdbd25292UL, 0x105633e9UL, 0xd647136dUL,
    0xd7618c9aUL, 0xa10c7a37UL, 0xf8148e59UL, 0x133c89ebUL, 0xa927eeceUL, 0x61c935b7UL, 0x1ce5ede1UL, 0x47b13c7aUL,
    0xd2df599cUL, 0xf2733f55UL, 0x14ce7918UL, 0xc737bf73UL, 0xf7cdea53UL, 0xfdaa5b5fUL, 0x3d6f14dfUL, 0x44db8678UL,
    0xaff381caUL, 0x68c43eb9UL, 0x24342c38UL, 0xa3405fc2UL, 0x1dc37216UL, 0xe2250cbcUL, 0x3c498b28UL, 0x0d9541ffUL,
    0xa8017139UL, 0x0cb3de08UL, 0xb4e49cd8UL, 0x56c19064UL, 0xcb84617bUL, 0x32b670d5UL, 0x6c5c7448UL, 0xb85742d0UL,
};

LQ_ALIGN(128) static const uint32_t Tks0[] = {
    0x00000000UL, 0x0e090d0bUL, 0x1c121a16UL, 0x121b171dUL, 0x3824342cUL, 0x362d3927UL, 0x24362e3aUL, 0x2a3f2331UL,
    0x70486858UL, 0x7e416553UL, 0x6c5a724eUL, 0x62537f45UL, 0x486c5c74UL, 0x4665517fUL, 0x547e4662UL, 0x5a774b69UL,
    0xe090d0b0UL, 0xee99ddbbUL, 0xfc82caa6UL, 0xf28bc7adUL, 0xd8b4e49cUL, 0xd6bde997UL, 0xc4a6fe8aUL, 0xcaaff381UL,
    0x90d8b8e8UL, 0x9ed1b5e3UL, 0x8ccaa2feUL, 0x82c3aff5UL, 0xa8fc8cc4UL, 0xa6f581cfUL, 0xb4ee96d2UL, 0xbae79bd9UL,
    0xdb3bbb7bUL, 0xd532b670UL, 0xc729a16dUL, 0xc920ac66UL, 0xe31f8f57UL, 0xed16825cUL, 0xff0d9541UL, 0xf104984aUL,
    0xab73d323UL, 0xa57ade28UL, 0xb761c935UL, 0xb968c43eUL, 0x9357e70fUL, 0x9d5eea04UL, 0x8f45fd19UL, 0x814cf012UL,
    0x3bab6bcbUL, 0x35a266c0UL, 0x27b971ddUL, 0x29b07cd6UL, 0x038f5fe7UL, 0x0d8652ecUL, 0x1f9d45f1UL, 0x119448faUL,
    0x4be30393UL, 0x45ea0e98UL, 0x57f11985UL, 0x59f8148eUL, 0x73c737bfUL, 0x7dce3ab4UL, 0x6fd52da9UL, 0x61dc20a2UL,
    0xad766df6UL, 0xa37f60fdUL, 0xb16477e0UL, 0xbf6d7aebUL, 0x955259daUL, 0x9b5b54d1UL, 0x894043ccUL, 0x87494ec7UL,
    0xdd3e05aeUL, 0xd33708a5UL, 0xc12c1fb8UL, 0xcf2512b3UL, 0xe51a3182UL, 0xeb133c89UL, 0xf9082b94UL, 0xf701269fUL,
    0x4de6bd46UL, 0x43efb04dUL, 0x51f4a750UL, 0x5ffdaa5bUL, 0x75c2896aUL, 0x7bcb8461UL, 0x69d0937cUL, 0x67d99e77UL,
    0x3daed51eUL, 0x33a7d815UL, 0x21bccf08UL, 0x2fb5c203UL, 0x058ae132UL, 0x0b83ec39UL, 0x1998fb24UL, 0x1791f62fUL,
    0x764dd68dUL, 0x7844db86UL, 0x6a5fcc9bUL, 0x6456c190UL, 0x4e69e2a1UL, 0x4060efaaUL, 0x527bf8b7UL, 0x5c72f5bcUL,
    0x0605bed5UL, 0x080cb3deUL, 0x1a17a4c3UL, 0x141ea9c8UL, 0x3e218af9UL, 0x302887f2UL, 0x223390efUL, 0x2c3a9de4UL,
    0x96dd063dUL, 0x98d40b36UL, 0x8acf1c2bUL, 0x84c61120UL, 0xaef93211UL, 0xa0f03f1aUL, 0xb2eb2807UL, 0xbce2250cUL,
    0xe6956e65UL, 0xe89c636eUL, 0xfa877473UL, 0xf48e7978UL, 0xdeb15a49UL, 0xd0b85742UL, 0xc2a3405fUL, 0xccaa4d54UL,
    0x41ecdaf7UL, 0x4fe5d7fcUL, 0x5dfec0e1UL, 0x53f7cdeaUL, 0x79c8eedbUL, 0x77c1e3d0UL, 0x65daf4cdUL, 0x6bd3f9c6UL,
    0x31a4b2afUL, 0x3fadbfa4UL, 0x2db6a8b9UL, 0x23bfa5b2UL, 0x09808683UL, 0x07898b88UL, 0x15929c95UL, 0x1b9b919eUL,
    0xa17c0a47UL, 0xaf75074cUL, 0xbd6e1051UL, 0xb3671d5aUL, 0x99583e6bUL, 0x97513360UL, 0x854a247dUL, 0x8b432976UL,
    0xd134621fUL, 0xdf3d6f14UL, 0xcd267809UL, 0xc32f7502UL, 0xe9105633UL, 0xe7195b38UL, 0xf5024c25UL, 0xfb0b412eUL,
    0x9ad7618cUL, 0x94de6c87UL, 0x86c57b9aUL, 0x88cc7691UL, 0xa2f355a0UL, 0xacfa58abUL, 0xbee14fb6UL, 0xb0e842bdUL,
    0xea9f09d4UL, 0xe49604dfUL, 0xf68d13c2UL, 0xf8841ec9UL, 0xd2bb3df8UL, 0xdcb230f3UL, 0xcea927eeUL, 0xc0a02ae5UL,
    0x7a47b13cUL, 0x744ebc37UL, 0x6655ab2aUL, 0x685ca621UL, 0x42638510UL, 0x4c6a881bUL, 0x5e719f06UL, 0x5078920dUL,
    0x0a0fd964UL, 0x0406d46fUL, 0x161dc372UL, 0x1814ce79UL, 0x322bed48UL, 0x3c22e043UL, 0x2e39f75eUL, 0x2030fa55UL,
    0xec9ab701UL, 0xe293ba0aUL, 0xf088ad17UL, 0xfe81a01cUL, 0xd4be832dUL, 0xdab78e26UL, 0xc8ac993bUL, 0xc6a59430UL,
    0x9cd2df59UL, 0x92dbd252UL, 0x80c0c54fUL, 0x8ec9c844UL, 0xa4f6eb75UL, 0xaaffe67eUL, 0xb8e4f163UL, 0xb6edfc68UL,
    0x0c0a67b1UL, 0x02036abaUL, 0x10187da7UL, 0x1e1170acUL, 0x342e539dUL, 0x3a275e96UL, 0x283c498bUL, 0x26354480UL,
    0x7c420fe9UL, 0x724b02e2UL, 0x605015ffUL, 0x6e5918f4UL, 0x44663bc5UL, 0x4a6f36ceUL, 0x587421d3UL, 0x567d2cd8UL,
    0x37a10c7aUL, 0x39a80171UL, 0x2bb3166cUL, 0x25ba1b67UL, 0x0f853856UL, 0x018c355dUL, 0x13972240UL, 0x1d9e2f4bUL,
    0x47e96422UL, 0x49e06929UL, 0x5bfb7e34UL, 0x55f2733fUL, 0x7fcd500eUL, 0x71c45d05UL, 0x63df4a18UL, 0x6dd64713UL,
    0xd731dccaUL, 0xd938d1c1UL, 0xcb23c6dcUL, 0xc52acbd7UL, 0xef15e8e6UL, 0xe11ce5edUL, 0xf307f2f0UL, 0xfd0efffbUL,
    0xa779b492UL, 0xa970b999UL, 0xbb6bae84UL, 0xb562a38fUL, 0x9f5d80beUL, 0x91548db5UL, 0x834f9aa8UL, 0x8d4697a3UL
};

LQ_ALIGN(128) static const uint32_t Tks1[] = {
    0x00000000UL, 0x0b0e090dUL, 0x161c121aUL, 0x1d121b17UL, 0x2c382434UL, 0x27362d39UL, 0x3a24362eUL, 0x312a3f23UL,
    0x58704868UL, 0x537e4165UL, 0x4e6c5a72UL, 0x4562537fUL, 0x74486c5cUL, 0x7f466551UL, 0x62547e46UL, 0x695a774bUL,
    0xb0e090d0UL, 0xbbee99ddUL, 0xa6fc82caUL, 0xadf28bc7UL, 0x9cd8b4e4UL, 0x97d6bde9UL, 0x8ac4a6feUL, 0x81caaff3UL,
    0xe890d8b8UL, 0xe39ed1b5UL, 0xfe8ccaa2UL, 0xf582c3afUL, 0xc4a8fc8cUL, 0xcfa6f581UL, 0xd2b4ee96UL, 0xd9bae79bUL,
    0x7bdb3bbbUL, 0x70d532b6UL, 0x6dc729a1UL, 0x66c920acUL, 0x57e31f8fUL, 0x5ced1682UL, 0x41ff0d95UL, 0x4af10498UL,
    0x23ab73d3UL, 0x28a57adeUL, 0x35b761c9UL, 0x3eb968c4UL, 0x0f9357e7UL, 0x049d5eeaUL, 0x198f45fdUL, 0x12814cf0UL,
    0xcb3bab6bUL, 0xc035a266UL, 0xdd27b971UL, 0xd629b07cUL, 0xe7038f5fUL, 0xec0d8652UL, 0xf11f9d45UL, 0xfa119448UL,
    0x934be303UL, 0x9845ea0eUL, 0x8557f119UL, 0x8e59f814UL, 0xbf73c737UL, 0xb47dce3aUL, 0xa96fd52dUL, 0xa261dc20UL,
    0xf6ad766dUL, 0xfda37f60UL, 0xe0b16477UL, 0xebbf6d7aUL, 0xda955259UL, 0xd19b5b54UL, 0xcc894043UL, 0xc787494eUL,
    0xaedd3e05UL, 0xa5d33708UL, 0xb8c12c1fUL, 0xb3cf2512UL, 0x82e51a31UL, 0x89eb133cUL, 0x94f9082bUL, 0x9ff70126UL,
    0x464de6bdUL, 0x4d43efb0UL, 0x5051f4a7UL, 0x5b5ffdaaUL, 0x6a75c289UL, 0x617bcb84UL, 0x7c69d093UL, 0x7767d99eUL,
    0x1e3daed5UL, 0x1533a7d8UL, 0x0821bccfUL, 0x032fb5c2UL, 0x32058ae1UL, 0x390b83ecUL, 0x241998fbUL, 0x2f1791f6UL,
    0x8d764dd6UL, 0x867844dbUL, 0x9b6a5fccUL, 0x906456c1UL, 0xa14e69e2UL, 0xaa4060efUL, 0xb7527bf8UL, 0xbc5c72f5UL,
    0xd50605beUL, 0xde080cb3UL, 0xc31a17a4UL, 0xc8141ea9UL, 0xf93e218aUL, 0xf2302887UL, 0xef223390UL, 0xe42c3a9dUL,
    0x3d96dd06UL, 0x3698d40bUL, 0x2b8acf1cUL, 0x2084c611UL, 0x11aef932UL, 0x1aa0f03fUL, 0x07b2eb28UL, 0x0cbce225UL,
    0x65e6956eUL, 0x6ee89c63UL, 0x73fa8774UL, 0x78f48e79UL, 0x49deb15aUL, 0x42d0b857UL, 0x5fc2a340UL, 0x54ccaa4dUL,
    0xf741ecdaUL, 0xfc4fe5d7UL, 0xe15dfec0UL, 0xea53f7cdUL, 0xdb79c8eeUL, 0xd077c1e3UL, 0xcd65daf4UL, 0xc66bd3f9UL,
    0xaf31a4b2UL, 0xa43fadbfUL, 0xb92db6a8UL, 0xb223bfa5UL, 0x83098086UL, 0x8807898bUL, 0x9515929cUL, 0x9e1b9b91UL,
    0x47a17c0aUL, 0x4caf7507UL, 0x51bd6e10UL, 0x5ab3671dUL, 0x6b99583eUL, 0x60975133UL, 0x7d854a24UL, 0x768b4329UL,
    0x1fd13462UL, 0x14df3d6fUL, 0x09cd2678UL, 0x02c32f75UL, 0x33e91056UL, 0x38e7195bUL, 0x25f5024cUL, 0x2efb0b41UL,
    0x8c9ad761UL, 0x8794de6cUL, 0x9a86c57bUL, 0x9188cc76UL, 0xa0a2f355UL, 0xabacfa58UL, 0xb6bee14fUL, 0xbdb0e842UL,
    0xd4ea9f09UL, 0xdfe49604UL, 0xc2f68d13UL, 0xc9f8841eUL, 0xf8d2bb3dUL, 0xf3dcb230UL, 0xeecea927UL, 0xe5c0a02aUL,
    0x3c7a47b1UL, 0x37744ebcUL, 0x2a6655abUL, 0x21685ca6UL, 0x10426385UL, 0x1b4c6a88UL, 0x065e719fUL, 0x0d507892UL,
    0x640a0fd9UL, 0x6f0406d4UL, 0x72161dc3UL, 0x791814ceUL, 0x48322bedUL, 0x433c22e0UL, 0x5e2e39f7UL, 0x552030faUL,
    0x01ec9ab7UL, 0x0ae293baUL, 0x17f088adUL, 0x1cfe81a0UL, 0x2dd4be83UL, 0x26dab78eUL, 0x3bc8ac99UL, 0x30c6a594UL,
    0x599cd2dfUL, 0x5292dbd2UL, 0x4f80c0c5UL, 0x448ec9c8UL, 0x75a4f6ebUL, 0x7eaaffe6UL, 0x63b8e4f1UL, 0x68b6edfcUL,
    0xb10c0a67UL, 0xba02036aUL, 0xa710187dUL, 0xac1e1170UL, 0x9d342e53UL, 0x963a275eUL, 0x8b283c49UL, 0x80263544UL,
    0xe97c420fUL, 0xe2724b02UL, 0xff605015UL, 0xf46e5918UL, 0xc544663bUL, 0xce4a6f36UL, 0xd3587421UL, 0xd8567d2cUL,
    0x7a37a10cUL, 0x7139a801UL, 0x6c2bb316UL, 0x6725ba1bUL, 0x560f8538UL, 0x5d018c35UL, 0x40139722UL, 0x4b1d9e2fUL,
    0x2247e964UL, 0x2949e069UL, 0x345bfb7eUL, 0x3f55f273UL, 0x0e7fcd50UL, 0x0571c45dUL, 0x1863df4aUL, 0x136dd647UL,
    0xcad731dcUL, 0xc1d938d1UL, 0xdccb23c6UL, 0xd7c52acbUL, 0xe6ef15e8UL, 0xede11ce5UL, 0xf0f307f2UL, 0xfbfd0effUL,
    0x92a779b4UL, 0x99a970b9UL, 0x84bb6baeUL, 0x8fb562a3UL, 0xbe9f5d80UL, 0xb591548dUL, 0xa8834f9aUL, 0xa38d4697UL
};

LQ_ALIGN(128) static const uint32_t Tks2[] = {
    0x00000000UL, 0x0d0b0e09UL, 0x1a161c12UL, 0x171d121bUL, 0x342c3824UL, 0x3927362dUL, 0x2e3a2436UL, 0x23312a3fUL,
    0x68587048UL, 0x65537e41UL, 0x724e6c5aUL, 0x7f456253UL, 0x5c74486cUL, 0x517f4665UL, 0x4662547eUL, 0x4b695a77UL,
    0xd0b0e090UL, 0xddbbee99UL, 0xcaa6fc82UL, 0xc7adf28bUL, 0xe49cd8b4UL, 0xe997d6bdUL, 0xfe8ac4a6UL, 0xf381caafUL,
    0xb8e890d8UL, 0xb5e39ed1UL, 0xa2fe8ccaUL, 0xaff582c3UL, 0x8cc4a8fcUL, 0x81cfa6f5UL, 0x96d2b4eeUL, 0x9bd9bae7UL,
    0xbb7bdb3bUL, 0xb670d532UL, 0xa16dc729UL, 0xac66c920UL, 0x8f57e31fUL, 0x825ced16UL, 0x9541ff0dUL, 0x984af104UL,
    0xd323ab73UL, 0xde28a57aUL, 0xc935b761UL, 0xc43eb968UL, 0xe70f9357UL, 0xea049d5eUL, 0xfd198f45UL, 0xf012814cUL,
    0x6bcb3babUL, 0x66c035a2UL, 0x71dd27b9UL, 0x7cd629b0UL, 0x5fe7038fUL, 0x52ec0d86UL, 0x45f11f9dUL, 0x48fa1194UL,
    0x03934be3UL, 0x0e9845eaUL, 0x198557f1UL, 0x148e59f8UL, 0x37bf73c7UL, 0x3ab47dceUL, 0x2da96fd5UL, 0x20a261dcUL,
    0x6df6ad76UL, 0x60fda37fUL, 0x77e0b164UL, 0x7aebbf6dUL, 0x59da9552UL, 0x54d19b5bUL, 0x43cc8940UL, 0x4ec78749UL,
    0x05aedd3eUL, 0x08a5d337UL, 0x1fb8c12cUL, 0x12b3cf25UL, 0x3182e51aUL, 0x3c89eb13UL, 0x2b94f908UL, 0x269ff701UL,
    0xbd464de6UL, 0xb04d43efUL, 0xa75051f4UL, 0xaa5b5ffdUL, 0x896a75c2UL, 0x84617bcbUL, 0x937c69d0UL, 0x9e7767d9UL,
    0xd51e3daeUL, 0xd81533a7UL, 0xcf0821bcUL, 0xc2032fb5UL, 0xe132058aUL, 0xec390b83UL, 0xfb241998UL, 0xf62f1791UL,
    0xd68d764dUL, 0xdb867844UL, 0xcc9b6a5fUL, 0xc1906456UL, 0xe2a14e69UL, 0xefaa4060UL, 0xf8b7527bUL, 0xf5bc5c72UL,
    0xbed50605UL, 0xb3de080cUL, 0xa4c31a17UL, 0xa9c8141eUL, 0x8af93e21UL, 0x87f23028UL, 0x90ef2233UL, 0x9de42c3aUL,
    0x063d96ddUL, 0x0b3698d4UL, 0x1c2b8acfUL, 0x112084c6UL, 0x3211aef9UL, 0x3f1aa0f0UL, 0x2807b2ebUL, 0x250cbce2UL,
    0x6e65e695UL, 0x636ee89cUL, 0x7473fa87UL, 0x7978f48eUL, 0x5a49deb1UL, 0x5742d0b8UL, 0x405fc2a3UL, 0x4d54ccaaUL,
    0xdaf741ecUL, 0xd7fc4fe5UL, 0xc0e15dfeUL, 0xcdea53f7UL, 0xeedb79c8UL, 0xe3d077c1UL, 0xf4cd65daUL, 0xf9c66bd3UL,
    0xb2af31a4UL, 0xbfa43fadUL, 0xa8b92db6UL, 0xa5b223bfUL, 0x86830980UL, 0x8b880789UL, 0x9c951592UL, 0x919e1b9bUL,
    0x0a47a17cUL, 0x074caf75UL, 0x1051bd6eUL, 0x1d5ab367UL, 0x3e6b9958UL, 0x33609751UL, 0x247d854aUL, 0x29768b43UL,
    0x621fd134UL, 0x6f14df3dUL, 0x7809cd26UL, 0x7502c32fUL, 0x5633e910UL, 0x5b38e719UL, 0x4c25f502UL, 0x412efb0bUL,
    0x618c9ad7UL, 0x6c8794deUL, 0x7b9a86c5UL, 0x769188ccUL, 0x55a0a2f3UL, 0x58abacfaUL, 0x4fb6bee1UL, 0x42bdb0e8UL,
    0x09d4ea9fUL, 0x04dfe496UL, 0x13c2f68dUL, 0x1ec9f884UL, 0x3df8d2bbUL, 0x30f3dcb2UL, 0x27eecea9UL, 0x2ae5c0a0UL,
    0xb13c7a47UL, 0xbc37744eUL, 0xab2a6655UL, 0xa621685cUL, 0x85104263UL, 0x881b4c6aUL, 0x9f065e71UL, 0x920d5078UL,
    0xd9640a0fUL, 0xd46f0406UL, 0xc372161dUL, 0xce791814UL, 0xed48322bUL, 0xe0433c22UL, 0xf75e2e39UL, 0xfa552030UL,
    0xb701ec9aUL, 0xba0ae293UL, 0xad17f088UL, 0xa01cfe81UL, 0x832dd4beUL, 0x8e26dab7UL, 0x993bc8acUL, 0x9430c6a5UL,
    0xdf599cd2UL, 0xd25292dbUL, 0xc54f80c0UL, 0xc8448ec9UL, 0xeb75a4f6UL, 0xe67eaaffUL, 0xf163b8e4UL, 0xfc68b6edUL,
    0x67b10c0aUL, 0x6aba0203UL, 0x7da71018UL, 0x70ac1e11UL, 0x539d342eUL, 0x5e963a27UL, 0x498b283cUL, 0x44802635UL,
    0x0fe97c42UL, 0x02e2724bUL, 0x15ff6050UL, 0x18f46e59UL, 0x3bc54466UL, 0x36ce4a6fUL, 0x21d35874UL, 0x2cd8567dUL,
    0x0c7a37a1UL, 0x017139a8UL, 0x166c2bb3UL, 0x1b6725baUL, 0x38560f85UL, 0x355d018cUL, 0x22401397UL, 0x2f4b1d9eUL,
    0x642247e9UL, 0x692949e0UL, 0x7e345bfbUL, 0x733f55f2UL, 0x500e7fcdUL, 0x5d0571c4UL, 0x4a1863dfUL, 0x47136dd6UL,
    0xdccad731UL, 0xd1c1d938UL, 0xc6dccb23UL, 0xcbd7c52aUL, 0xe8e6ef15UL, 0xe5ede11cUL, 0xf2f0f307UL, 0xfffbfd0eUL,
    0xb492a779UL, 0xb999a970UL, 0xae84bb6bUL, 0xa38fb562UL, 0x80be9f5dUL, 0x8db59154UL, 0x9aa8834fUL, 0x97a38d46UL
};

LQ_ALIGN(128) static const uint32_t Tks3[] = {
    0x00000000UL, 0x090d0b0eUL, 0x121a161cUL, 0x1b171d12UL, 0x24342c38UL, 0x2d392736UL, 0x362e3a24UL, 0x3f23312aUL,
    0x48685870UL, 0x4165537eUL, 0x5a724e6cUL, 0x537f4562UL, 0x6c5c7448UL, 0x65517f46UL, 0x7e466254UL, 0x774b695aUL,
    0x90d0b0e0UL, 0x99ddbbeeUL, 0x82caa6fcUL, 0x8bc7adf2UL, 0xb4e49cd8UL, 0xbde997d6UL, 0xa6fe8ac4UL, 0xaff381caUL,
    0xd8b8e890UL, 0xd1b5e39eUL, 0xcaa2fe8cUL, 0xc3aff582UL, 0xfc8cc4a8UL, 0xf581cfa6UL, 0xee96d2b4UL, 0xe79bd9baUL,
    0x3bbb7bdbUL, 0x32b670d5UL, 0x29a16dc7UL, 0x20ac66c9UL, 0x1f8f57e3UL, 0x16825cedUL, 0x0d9541ffUL, 0x04984af1UL,
    0x73d323abUL, 0x7ade28a5UL, 0x61c935b7UL, 0x68c43eb9UL, 0x57e70f93UL, 0x5eea049dUL, 0x45fd198fUL, 0x4cf01281UL,
    0xab6bcb3bUL, 0xa266c035UL, 0xb971dd27UL, 0xb07cd629UL, 0x8f5fe703UL, 0x8652ec0dUL, 0x9d45f11fUL, 0x9448fa11UL,
    0xe303934bUL, 0xea0e9845UL, 0xf1198557UL, 0xf8148e59UL, 0xc737bf73UL, 0xce3ab47dUL, 0xd52da96fUL, 0xdc20a261UL,
    0x766df6adUL, 0x7f60fda3UL, 0x6477e0b1UL, 0x6d7aebbfUL, 0x5259da95UL, 0x5b54d19bUL, 0x4043cc89UL, 0x494ec787UL,
    0x3e05aeddUL, 0x3708a5d3UL, 0x2c1fb8c1UL, 0x2512b3cfUL, 0x1a3182e5UL, 0x133c89ebUL, 0x082b94f9UL, 0x01269ff7UL,
    0xe6bd464dUL, 0xefb04d43UL, 0xf4a75051UL, 0xfdaa5b5fUL, 0xc2896a75UL, 0xcb84617bUL, 0xd0937c69UL, 0xd99e7767UL,
    0xaed51e3dUL, 0xa7d81533UL, 0xbccf0821UL, 0xb5c2032fUL, 0x8ae13205UL, 0x83ec390bUL, 0x98fb2419UL, 0x91f62f17UL,
    0x4dd68d76UL, 0x44db8678UL, 0x5fcc9b6aUL, 0x56c19064UL, 0x69e2a14eUL, 0x60efaa40UL, 0x7bf8b752UL, 0x72f5bc5cUL,
    0x05bed506UL, 0x0cb3de08UL, 0x17a4c31aUL, 0x1ea9c814UL, 0x218af93eUL, 0x2887f230UL, 0x3390ef22UL, 0x3a9de42cUL,
    0xdd063d96UL, 0xd40b3698UL, 0xcf1c2b8aUL, 0xc6112084UL, 0xf93211aeUL, 0xf03f1aa0UL, 0xeb2807b2UL, 0xe2250cbcUL,
    0x956e65e6UL, 0x9c636ee8UL, 0x877473faUL, 0x8e7978f4UL, 0xb15a49deUL, 0xb85742d0UL, 0xa3405fc2UL, 0xaa4d54ccUL,
    0xecdaf741UL, 0xe5d7fc4fUL, 0xfec0e15dUL, 0xf7cdea53UL, 0xc8eedb79UL, 0xc1e3d077UL, 0xdaf4cd65UL, 0xd3f9c66bUL,
    0xa4b2af31UL, 0xadbfa43fUL, 0xb6a8b92dUL, 0xbfa5b223UL, 0x80868309UL, 0x898b8807UL, 0x929c9515UL, 0x9b919e1bUL,
    0x7c0a47a1UL, 0x75074cafUL, 0x6e1051bdUL, 0x671d5ab3UL, 0x583e6b99UL, 0x51336097UL, 0x4a247d85UL, 0x4329768bUL,
    0x34621fd1UL, 0x3d6f14dfUL, 0x267809cdUL, 0x2f7502c3UL, 0x105633e9UL, 0x195b38e7UL, 0x024c25f5UL, 0x0b412efbUL,
    0xd7618c9aUL, 0xde6c8794UL, 0xc57b9a86UL, 0xcc769188UL, 0xf355a0a2UL, 0xfa58abacUL, 0xe14fb6beUL, 0xe842bdb0UL,
    0x9f09d4eaUL, 0x9604dfe4UL, 0x8d13c2f6UL, 0x841ec9f8UL, 0xbb3df8d2UL, 0xb230f3dcUL, 0xa927eeceUL, 0xa02ae5c0UL,
    0x47b13c7aUL, 0x4ebc3774UL, 0x55ab2a66UL, 0x5ca62168UL, 0x63851042UL, 0x6a881b4cUL, 0x719f065eUL, 0x78920d50UL,
    0x0fd9640aUL, 0x06d46f04UL, 0x1dc37216UL, 0x14ce7918UL, 0x2bed4832UL, 0x22e0433cUL, 0x39f75e2eUL, 0x30fa5520UL,
    0x9ab701ecUL, 0x93ba0ae2UL, 0x88ad17f0UL, 0x81a01cfeUL, 0xbe832dd4UL, 0xb78e26daUL, 0xac993bc8UL, 0xa59430c6UL,
    0xd2df599cUL, 0xdbd25292UL, 0xc0c54f80UL, 0xc9c8448eUL, 0xf6eb75a4UL, 0xffe67eaaUL, 0xe4f163b8UL, 0xedfc68b6UL,
    0x0a67b10cUL, 0x036aba02UL, 0x187da710UL, 0x1170ac1eUL, 0x2e539d34UL, 0x275e963aUL, 0x3c498b28UL, 0x35448026UL,
    0x420fe97cUL, 0x4b02e272UL, 0x5015ff60UL, 0x5918f46eUL, 0x663bc544UL, 0x6f36ce4aUL, 0x7421d358UL, 0x7d2cd856UL,
    0xa10c7a37UL, 0xa8017139UL, 0xb3166c2bUL, 0xba1b6725UL, 0x8538560fUL, 0x8c355d01UL, 0x97224013UL, 0x9e2f4b1dUL,
    0xe9642247UL, 0xe0692949UL, 0xfb7e345bUL, 0xf2733f55UL, 0xcd500e7fUL, 0xc45d0571UL, 0xdf4a1863UL, 0xd647136dUL,
    0x31dccad7UL, 0x38d1c1d9UL, 0x23c6dccbUL, 0x2acbd7c5UL, 0x15e8e6efUL, 0x1ce5ede1UL, 0x07f2f0f3UL, 0x0efffbfdUL,
    0x79b492a7UL, 0x70b999a9UL, 0x6bae84bbUL, 0x62a38fb5UL, 0x5d80be9fUL, 0x548db591UL, 0x4f9aa883UL, 0x4697a38dUL
};



LQ_ALIGN(128) static const uint32_t rcon[] = {
    0x01000000UL, 0x02000000UL, 0x04000000UL, 0x08000000UL,
    0x10000000UL, 0x20000000UL, 0x40000000UL, 0x80000000UL,
    0x1B000000UL, 0x36000000UL, /* for 128-bit blocks, Rijndael never uses more than 10 rcon values */
};

static uint32_t setup_mix(uint32_t temp) {
    return (Te4_3[byte(temp, 2)]) ^ (Te4_2[byte(temp, 1)]) ^ (Te4_1[byte(temp, 0)]) ^ (Te4_0[byte(temp, 3)]);
}

static bool LqCryptAesInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags){
    int i, j;
    uint32_t temp, *rk;
    uint32_t *rrk;

    if((keylen != 16 && keylen != 24 && keylen != 32) || (num_rounds != 0 && num_rounds != (10 + ((keylen / 8) - 2) * 2))) {
        lq_errno_set(EINVAL);
        return false;
    }
    skey->methods = &aes_desc;
    skey->ciphers.rijndael.Nr = 10 + ((keylen / 8) - 2) * 2;
    i = 0;
    rk = skey->ciphers.rijndael.eK;
    LOAD32H(rk[0], key);
    LOAD32H(rk[1], key + 4);
    LOAD32H(rk[2], key + 8);
    LOAD32H(rk[3], key + 12);
    if(keylen == 16) {
        j = 44;
        for(;;) {
            temp = rk[3];
            rk[4] = rk[0] ^ setup_mix(temp) ^ rcon[i];
            rk[5] = rk[1] ^ rk[4];
            rk[6] = rk[2] ^ rk[5];
            rk[7] = rk[3] ^ rk[6];
            if(++i == 10) {
                break;
            }
            rk += 4;
        }
    } else if(keylen == 24) {
        j = 52;
        LOAD32H(rk[4], key + 16);
        LOAD32H(rk[5], key + 20);
        for(;;) {
            temp = rk[5];
            rk[6] = rk[0] ^ setup_mix(temp) ^ rcon[i];
            rk[7] = rk[1] ^ rk[6];
            rk[8] = rk[2] ^ rk[7];
            rk[9] = rk[3] ^ rk[8];
            if(++i == 8) {
                break;
            }
            rk[10] = rk[4] ^ rk[9];
            rk[11] = rk[5] ^ rk[10];
            rk += 6;
        }
    } else if(keylen == 32) {
        j = 60;
        LOAD32H(rk[4], key + 16);
        LOAD32H(rk[5], key + 20);
        LOAD32H(rk[6], key + 24);
        LOAD32H(rk[7], key + 28);
        for(;;) {
            temp = rk[7];
            rk[8] = rk[0] ^ setup_mix(temp) ^ rcon[i];
            rk[9] = rk[1] ^ rk[8];
            rk[10] = rk[2] ^ rk[9];
            rk[11] = rk[3] ^ rk[10];
            if(++i == 7) {
                break;
            }
            temp = rk[11];
            rk[12] = rk[4] ^ setup_mix(RORc(temp, 8));
            rk[13] = rk[5] ^ rk[12];
            rk[14] = rk[6] ^ rk[13];
            rk[15] = rk[7] ^ rk[14];
            rk += 8;
        }
    }
    rk = skey->ciphers.rijndael.dK;
    rrk = skey->ciphers.rijndael.eK + j - 4;
    *rk++ = *rrk++;
    *rk++ = *rrk++;
    *rk++ = *rrk++;
    *rk = *rrk;
    rk -= 3; rrk -= 3;

    for(i = 1; i < skey->ciphers.rijndael.Nr; i++) {
        rrk -= 4;
        rk += 4;
        temp = rrk[0];
        rk[0] =
            Tks0[byte(temp, 3)] ^
            Tks1[byte(temp, 2)] ^
            Tks2[byte(temp, 1)] ^
            Tks3[byte(temp, 0)];
        temp = rrk[1];
        rk[1] =
            Tks0[byte(temp, 3)] ^
            Tks1[byte(temp, 2)] ^
            Tks2[byte(temp, 1)] ^
            Tks3[byte(temp, 0)];
        temp = rrk[2];
        rk[2] =
            Tks0[byte(temp, 3)] ^
            Tks1[byte(temp, 2)] ^
            Tks2[byte(temp, 1)] ^
            Tks3[byte(temp, 0)];
        temp = rrk[3];
        rk[3] =
            Tks0[byte(temp, 3)] ^
            Tks1[byte(temp, 2)] ^
            Tks2[byte(temp, 1)] ^
            Tks3[byte(temp, 0)];
    }
    rrk -= 4;
    rk += 4;
    *rk++ = *rrk++;
    *rk++ = *rrk++;
    *rk++ = *rrk++;
    *rk = *rrk;
    return true;
}

static intptr_t LqCrypt16BlockLen(LqCryptCipher* skey) {
    return 16;
}

static intptr_t LqCrypt8BlockLen(LqCryptCipher* skey) {
    return 8;
}

static intptr_t LqCrypt1BlockLen(LqCryptCipher* skey) {
    return 1;
}

static bool LqCryptAesEncrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len) {
    register uint32_t s0, s1, s2, s3, t0, t1, t2, t3;
    const uint32_t *rk;
    int r;
    if(Len % 16) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        r = skey->ciphers.rijndael.Nr;
        rk = skey->ciphers.rijndael.eK;
        LOAD32H(s0, pt); s0 ^= rk[0]; LOAD32H(s1, pt + 4); s1 ^= rk[1];
        LOAD32H(s2, pt + 8); s2 ^= rk[2]; LOAD32H(s3, pt + 12); s3 ^= rk[3];

        r = r >> 1;
        for(;;) {
            t0 =
                Te0(byte(s0, 3)) ^
                Te1(byte(s1, 2)) ^
                Te2(byte(s2, 1)) ^
                Te3(byte(s3, 0)) ^
                rk[4];
            t1 =
                Te0(byte(s1, 3)) ^
                Te1(byte(s2, 2)) ^
                Te2(byte(s3, 1)) ^
                Te3(byte(s0, 0)) ^
                rk[5];
            t2 =
                Te0(byte(s2, 3)) ^
                Te1(byte(s3, 2)) ^
                Te2(byte(s0, 1)) ^
                Te3(byte(s1, 0)) ^
                rk[6];
            t3 =
                Te0(byte(s3, 3)) ^
                Te1(byte(s0, 2)) ^
                Te2(byte(s1, 1)) ^
                Te3(byte(s2, 0)) ^
                rk[7];
            rk += 8;
            if(--r == 0)
                break;
            s0 =
                Te0(byte(t0, 3)) ^
                Te1(byte(t1, 2)) ^
                Te2(byte(t2, 1)) ^
                Te3(byte(t3, 0)) ^
                rk[0];
            s1 =
                Te0(byte(t1, 3)) ^
                Te1(byte(t2, 2)) ^
                Te2(byte(t3, 1)) ^
                Te3(byte(t0, 0)) ^
                rk[1];
            s2 =
                Te0(byte(t2, 3)) ^
                Te1(byte(t3, 2)) ^
                Te2(byte(t0, 1)) ^
                Te3(byte(t1, 0)) ^
                rk[2];
            s3 =
                Te0(byte(t3, 3)) ^
                Te1(byte(t0, 2)) ^
                Te2(byte(t1, 1)) ^
                Te3(byte(t2, 0)) ^
                rk[3];
        }
        s0 =
            (Te4_3[byte(t0, 3)]) ^
            (Te4_2[byte(t1, 2)]) ^
            (Te4_1[byte(t2, 1)]) ^
            (Te4_0[byte(t3, 0)]) ^
            rk[0];
        STORE32H(s0, ct);
        s0 =
            (Te4_3[byte(t1, 3)]) ^
            (Te4_2[byte(t2, 2)]) ^
            (Te4_1[byte(t3, 1)]) ^
            (Te4_0[byte(t0, 0)]) ^
            rk[1];
        STORE32H(s0, ct + 4);
        s0 =
            (Te4_3[byte(t2, 3)]) ^
            (Te4_2[byte(t3, 2)]) ^
            (Te4_1[byte(t0, 1)]) ^
            (Te4_0[byte(t1, 0)]) ^
            rk[2];
        STORE32H(s0, ct + 8);
        s0 =
            (Te4_3[byte(t3, 3)]) ^
            (Te4_2[byte(t0, 2)]) ^
            (Te4_1[byte(t1, 1)]) ^
            (Te4_0[byte(t2, 0)]) ^
            rk[3];
        STORE32H(s0, ct + 12);
    }
    return true;
}

static bool LqCryptAesDecrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len) {
    register uint32_t s0, s1, s2, s3, t0, t1, t2, t3;
    const uint32_t *rk;
    int r;
    if(Len % 16) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        r = skey->ciphers.rijndael.Nr;
        rk = skey->ciphers.rijndael.dK;
        LOAD32H(s0, ct);LOAD32H(s1, ct + 4);
        LOAD32H(s2, ct + 8);LOAD32H(s3, ct + 12);
        s0 ^= rk[0];s1 ^= rk[1];
        s2 ^= rk[2];s3 ^= rk[3];
        r = r >> 1;
        for(;;) {

            t0 =
                Td0(byte(s0, 3)) ^
                Td1(byte(s3, 2)) ^
                Td2(byte(s2, 1)) ^
                Td3(byte(s1, 0)) ^
                rk[4];
            t1 =
                Td0(byte(s1, 3)) ^
                Td1(byte(s0, 2)) ^
                Td2(byte(s3, 1)) ^
                Td3(byte(s2, 0)) ^
                rk[5];
            t2 =
                Td0(byte(s2, 3)) ^
                Td1(byte(s1, 2)) ^
                Td2(byte(s0, 1)) ^
                Td3(byte(s3, 0)) ^
                rk[6];
            t3 =
                Td0(byte(s3, 3)) ^
                Td1(byte(s2, 2)) ^
                Td2(byte(s1, 1)) ^
                Td3(byte(s0, 0)) ^
                rk[7];
            rk += 8;
            if(--r == 0)
                break;
            s0 =
                Td0(byte(t0, 3)) ^
                Td1(byte(t3, 2)) ^
                Td2(byte(t2, 1)) ^
                Td3(byte(t1, 0)) ^
                rk[0];
            s1 =
                Td0(byte(t1, 3)) ^
                Td1(byte(t0, 2)) ^
                Td2(byte(t3, 1)) ^
                Td3(byte(t2, 0)) ^
                rk[1];
            s2 =
                Td0(byte(t2, 3)) ^
                Td1(byte(t1, 2)) ^
                Td2(byte(t0, 1)) ^
                Td3(byte(t3, 0)) ^
                rk[2];
            s3 =
                Td0(byte(t3, 3)) ^
                Td1(byte(t2, 2)) ^
                Td2(byte(t1, 1)) ^
                Td3(byte(t0, 0)) ^
                rk[3];
        }
        s0 =
            (Td4[byte(t0, 3)] & 0xff000000UL) ^
            (Td4[byte(t3, 2)] & 0x00ff0000UL) ^
            (Td4[byte(t2, 1)] & 0x0000ff00UL) ^
            (Td4[byte(t1, 0)] & 0x000000ffUL) ^
            rk[0];
        STORE32H(s0, pt);
        s0 =
            (Td4[byte(t1, 3)] & 0xff000000UL) ^
            (Td4[byte(t0, 2)] & 0x00ff0000UL) ^
            (Td4[byte(t3, 1)] & 0x0000ff00UL) ^
            (Td4[byte(t2, 0)] & 0x000000ffUL) ^
            rk[1];
        STORE32H(s0, pt + 4);
        s0 =
            (Td4[byte(t2, 3)] & 0xff000000UL) ^
            (Td4[byte(t1, 2)] & 0x00ff0000UL) ^
            (Td4[byte(t0, 1)] & 0x0000ff00UL) ^
            (Td4[byte(t3, 0)] & 0x000000ffUL) ^
            rk[2];
        STORE32H(s0, pt + 8);
        s0 =
            (Td4[byte(t3, 3)] & 0xff000000UL) ^
            (Td4[byte(t2, 2)] & 0x00ff0000UL) ^
            (Td4[byte(t1, 1)] & 0x0000ff00UL) ^
            (Td4[byte(t0, 0)] & 0x000000ffUL) ^
            rk[3];
        STORE32H(s0, pt + 12);
    }
    return true;
}

static void LqCryptAesCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->ciphers.rijndael, &Source->ciphers.rijndael, sizeof(Dest->ciphers.rijndael));
}

static void LqCryptTwofishCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->ciphers.twofish, &Source->ciphers.twofish, sizeof(Dest->ciphers.twofish));
}

static void LqCryptDes3Copy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->ciphers.des3, &Source->ciphers.des3, sizeof(Dest->ciphers.des3));
}

static void LqCryptDesCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->ciphers.des, &Source->ciphers.des, sizeof(Dest->ciphers.des));
}

#define EN0 0
#define DE1 1
#define CONST64(n) n ## ULL


LQ_ALIGN(128) static const uint32_t bytebit[8] = {
    0200, 0100, 040, 020, 010, 04, 02, 01
};

LQ_ALIGN(128) static const uint32_t bigbyte[24] = {
    0x800000UL,  0x400000UL,  0x200000UL,  0x100000UL,
    0x80000UL,   0x40000UL,   0x20000UL,   0x10000UL,
    0x8000UL,    0x4000UL,    0x2000UL,    0x1000UL,
    0x800UL,     0x400UL,     0x200UL,     0x100UL,
    0x80UL,      0x40UL,      0x20UL,      0x10UL,
    0x8UL,       0x4UL,       0x2UL,       0x1L
};

/* Use the key schedule specific in the standard (ANSI X3.92-1981) */

LQ_ALIGN(128) static const unsigned char pc1[56] = {
    56, 48, 40, 32, 24, 16,  8,  0, 57, 49, 41, 33, 25, 17,
    9,  1, 58, 50, 42, 34, 26, 18, 10,  2, 59, 51, 43, 35,
    62, 54, 46, 38, 30, 22, 14,  6, 61, 53, 45, 37, 29, 21,
    13,  5, 60, 52, 44, 36, 28, 20, 12,  4, 27, 19, 11,  3
};

LQ_ALIGN(128) static const unsigned char totrot[16] = {
    1,   2,  4,  6,
    8,  10, 12, 14,
    15, 17, 19, 21,
    23, 25, 27, 28
};

LQ_ALIGN(128) static const unsigned char pc2[48] = {
    13, 16, 10, 23,  0,  4,      2, 27, 14,  5, 20,  9,
    22, 18, 11,  3, 25,  7,     15,  6, 26, 19, 12,  1,
    40, 51, 30, 36, 46, 54,     29, 39, 50, 44, 32, 47,
    43, 48, 38, 55, 33, 52,     45, 41, 49, 35, 28, 31
};


LQ_ALIGN(128) static const uint32_t SP1[64] = {
    0x01010400UL, 0x00000000UL, 0x00010000UL, 0x01010404UL,
    0x01010004UL, 0x00010404UL, 0x00000004UL, 0x00010000UL,
    0x00000400UL, 0x01010400UL, 0x01010404UL, 0x00000400UL,
    0x01000404UL, 0x01010004UL, 0x01000000UL, 0x00000004UL,
    0x00000404UL, 0x01000400UL, 0x01000400UL, 0x00010400UL,
    0x00010400UL, 0x01010000UL, 0x01010000UL, 0x01000404UL,
    0x00010004UL, 0x01000004UL, 0x01000004UL, 0x00010004UL,
    0x00000000UL, 0x00000404UL, 0x00010404UL, 0x01000000UL,
    0x00010000UL, 0x01010404UL, 0x00000004UL, 0x01010000UL,
    0x01010400UL, 0x01000000UL, 0x01000000UL, 0x00000400UL,
    0x01010004UL, 0x00010000UL, 0x00010400UL, 0x01000004UL,
    0x00000400UL, 0x00000004UL, 0x01000404UL, 0x00010404UL,
    0x01010404UL, 0x00010004UL, 0x01010000UL, 0x01000404UL,
    0x01000004UL, 0x00000404UL, 0x00010404UL, 0x01010400UL,
    0x00000404UL, 0x01000400UL, 0x01000400UL, 0x00000000UL,
    0x00010004UL, 0x00010400UL, 0x00000000UL, 0x01010004UL
};

LQ_ALIGN(128) static const uint32_t SP2[64] = {
    0x80108020UL, 0x80008000UL, 0x00008000UL, 0x00108020UL,
    0x00100000UL, 0x00000020UL, 0x80100020UL, 0x80008020UL,
    0x80000020UL, 0x80108020UL, 0x80108000UL, 0x80000000UL,
    0x80008000UL, 0x00100000UL, 0x00000020UL, 0x80100020UL,
    0x00108000UL, 0x00100020UL, 0x80008020UL, 0x00000000UL,
    0x80000000UL, 0x00008000UL, 0x00108020UL, 0x80100000UL,
    0x00100020UL, 0x80000020UL, 0x00000000UL, 0x00108000UL,
    0x00008020UL, 0x80108000UL, 0x80100000UL, 0x00008020UL,
    0x00000000UL, 0x00108020UL, 0x80100020UL, 0x00100000UL,
    0x80008020UL, 0x80100000UL, 0x80108000UL, 0x00008000UL,
    0x80100000UL, 0x80008000UL, 0x00000020UL, 0x80108020UL,
    0x00108020UL, 0x00000020UL, 0x00008000UL, 0x80000000UL,
    0x00008020UL, 0x80108000UL, 0x00100000UL, 0x80000020UL,
    0x00100020UL, 0x80008020UL, 0x80000020UL, 0x00100020UL,
    0x00108000UL, 0x00000000UL, 0x80008000UL, 0x00008020UL,
    0x80000000UL, 0x80100020UL, 0x80108020UL, 0x00108000UL
};

LQ_ALIGN(128) static const uint32_t SP3[64] = {
    0x00000208UL, 0x08020200UL, 0x00000000UL, 0x08020008UL,
    0x08000200UL, 0x00000000UL, 0x00020208UL, 0x08000200UL,
    0x00020008UL, 0x08000008UL, 0x08000008UL, 0x00020000UL,
    0x08020208UL, 0x00020008UL, 0x08020000UL, 0x00000208UL,
    0x08000000UL, 0x00000008UL, 0x08020200UL, 0x00000200UL,
    0x00020200UL, 0x08020000UL, 0x08020008UL, 0x00020208UL,
    0x08000208UL, 0x00020200UL, 0x00020000UL, 0x08000208UL,
    0x00000008UL, 0x08020208UL, 0x00000200UL, 0x08000000UL,
    0x08020200UL, 0x08000000UL, 0x00020008UL, 0x00000208UL,
    0x00020000UL, 0x08020200UL, 0x08000200UL, 0x00000000UL,
    0x00000200UL, 0x00020008UL, 0x08020208UL, 0x08000200UL,
    0x08000008UL, 0x00000200UL, 0x00000000UL, 0x08020008UL,
    0x08000208UL, 0x00020000UL, 0x08000000UL, 0x08020208UL,
    0x00000008UL, 0x00020208UL, 0x00020200UL, 0x08000008UL,
    0x08020000UL, 0x08000208UL, 0x00000208UL, 0x08020000UL,
    0x00020208UL, 0x00000008UL, 0x08020008UL, 0x00020200UL
};

LQ_ALIGN(128) static const uint32_t SP4[64] = {
    0x00802001UL, 0x00002081UL, 0x00002081UL, 0x00000080UL,
    0x00802080UL, 0x00800081UL, 0x00800001UL, 0x00002001UL,
    0x00000000UL, 0x00802000UL, 0x00802000UL, 0x00802081UL,
    0x00000081UL, 0x00000000UL, 0x00800080UL, 0x00800001UL,
    0x00000001UL, 0x00002000UL, 0x00800000UL, 0x00802001UL,
    0x00000080UL, 0x00800000UL, 0x00002001UL, 0x00002080UL,
    0x00800081UL, 0x00000001UL, 0x00002080UL, 0x00800080UL,
    0x00002000UL, 0x00802080UL, 0x00802081UL, 0x00000081UL,
    0x00800080UL, 0x00800001UL, 0x00802000UL, 0x00802081UL,
    0x00000081UL, 0x00000000UL, 0x00000000UL, 0x00802000UL,
    0x00002080UL, 0x00800080UL, 0x00800081UL, 0x00000001UL,
    0x00802001UL, 0x00002081UL, 0x00002081UL, 0x00000080UL,
    0x00802081UL, 0x00000081UL, 0x00000001UL, 0x00002000UL,
    0x00800001UL, 0x00002001UL, 0x00802080UL, 0x00800081UL,
    0x00002001UL, 0x00002080UL, 0x00800000UL, 0x00802001UL,
    0x00000080UL, 0x00800000UL, 0x00002000UL, 0x00802080UL
};

LQ_ALIGN(128) static const uint32_t SP5[64] = {
    0x00000100UL, 0x02080100UL, 0x02080000UL, 0x42000100UL,
    0x00080000UL, 0x00000100UL, 0x40000000UL, 0x02080000UL,
    0x40080100UL, 0x00080000UL, 0x02000100UL, 0x40080100UL,
    0x42000100UL, 0x42080000UL, 0x00080100UL, 0x40000000UL,
    0x02000000UL, 0x40080000UL, 0x40080000UL, 0x00000000UL,
    0x40000100UL, 0x42080100UL, 0x42080100UL, 0x02000100UL,
    0x42080000UL, 0x40000100UL, 0x00000000UL, 0x42000000UL,
    0x02080100UL, 0x02000000UL, 0x42000000UL, 0x00080100UL,
    0x00080000UL, 0x42000100UL, 0x00000100UL, 0x02000000UL,
    0x40000000UL, 0x02080000UL, 0x42000100UL, 0x40080100UL,
    0x02000100UL, 0x40000000UL, 0x42080000UL, 0x02080100UL,
    0x40080100UL, 0x00000100UL, 0x02000000UL, 0x42080000UL,
    0x42080100UL, 0x00080100UL, 0x42000000UL, 0x42080100UL,
    0x02080000UL, 0x00000000UL, 0x40080000UL, 0x42000000UL,
    0x00080100UL, 0x02000100UL, 0x40000100UL, 0x00080000UL,
    0x00000000UL, 0x40080000UL, 0x02080100UL, 0x40000100UL
};

LQ_ALIGN(128) static const uint32_t SP6[64] = {
    0x20000010UL, 0x20400000UL, 0x00004000UL, 0x20404010UL,
    0x20400000UL, 0x00000010UL, 0x20404010UL, 0x00400000UL,
    0x20004000UL, 0x00404010UL, 0x00400000UL, 0x20000010UL,
    0x00400010UL, 0x20004000UL, 0x20000000UL, 0x00004010UL,
    0x00000000UL, 0x00400010UL, 0x20004010UL, 0x00004000UL,
    0x00404000UL, 0x20004010UL, 0x00000010UL, 0x20400010UL,
    0x20400010UL, 0x00000000UL, 0x00404010UL, 0x20404000UL,
    0x00004010UL, 0x00404000UL, 0x20404000UL, 0x20000000UL,
    0x20004000UL, 0x00000010UL, 0x20400010UL, 0x00404000UL,
    0x20404010UL, 0x00400000UL, 0x00004010UL, 0x20000010UL,
    0x00400000UL, 0x20004000UL, 0x20000000UL, 0x00004010UL,
    0x20000010UL, 0x20404010UL, 0x00404000UL, 0x20400000UL,
    0x00404010UL, 0x20404000UL, 0x00000000UL, 0x20400010UL,
    0x00000010UL, 0x00004000UL, 0x20400000UL, 0x00404010UL,
    0x00004000UL, 0x00400010UL, 0x20004010UL, 0x00000000UL,
    0x20404000UL, 0x20000000UL, 0x00400010UL, 0x20004010UL
};

LQ_ALIGN(128) static const uint32_t SP7[64] = {
    0x00200000UL, 0x04200002UL, 0x04000802UL, 0x00000000UL,
    0x00000800UL, 0x04000802UL, 0x00200802UL, 0x04200800UL,
    0x04200802UL, 0x00200000UL, 0x00000000UL, 0x04000002UL,
    0x00000002UL, 0x04000000UL, 0x04200002UL, 0x00000802UL,
    0x04000800UL, 0x00200802UL, 0x00200002UL, 0x04000800UL,
    0x04000002UL, 0x04200000UL, 0x04200800UL, 0x00200002UL,
    0x04200000UL, 0x00000800UL, 0x00000802UL, 0x04200802UL,
    0x00200800UL, 0x00000002UL, 0x04000000UL, 0x00200800UL,
    0x04000000UL, 0x00200800UL, 0x00200000UL, 0x04000802UL,
    0x04000802UL, 0x04200002UL, 0x04200002UL, 0x00000002UL,
    0x00200002UL, 0x04000000UL, 0x04000800UL, 0x00200000UL,
    0x04200800UL, 0x00000802UL, 0x00200802UL, 0x04200800UL,
    0x00000802UL, 0x04000002UL, 0x04200802UL, 0x04200000UL,
    0x00200800UL, 0x00000000UL, 0x00000002UL, 0x04200802UL,
    0x00000000UL, 0x00200802UL, 0x04200000UL, 0x00000800UL,
    0x04000002UL, 0x04000800UL, 0x00000800UL, 0x00200002UL
};

LQ_ALIGN(128) static const uint32_t SP8[64] = {
    0x10001040UL, 0x00001000UL, 0x00040000UL, 0x10041040UL,
    0x10000000UL, 0x10001040UL, 0x00000040UL, 0x10000000UL,
    0x00040040UL, 0x10040000UL, 0x10041040UL, 0x00041000UL,
    0x10041000UL, 0x00041040UL, 0x00001000UL, 0x00000040UL,
    0x10040000UL, 0x10000040UL, 0x10001000UL, 0x00001040UL,
    0x00041000UL, 0x00040040UL, 0x10040040UL, 0x10041000UL,
    0x00001040UL, 0x00000000UL, 0x00000000UL, 0x10040040UL,
    0x10000040UL, 0x10001000UL, 0x00041040UL, 0x00040000UL,
    0x00041040UL, 0x00040000UL, 0x10041000UL, 0x00001000UL,
    0x00000040UL, 0x10040040UL, 0x00001000UL, 0x00041040UL,
    0x10001000UL, 0x00000040UL, 0x10000040UL, 0x10040000UL,
    0x10040040UL, 0x10000000UL, 0x00040000UL, 0x10001040UL,
    0x00000000UL, 0x10041040UL, 0x00040040UL, 0x10000040UL,
    0x10040000UL, 0x10001000UL, 0x10001040UL, 0x00000000UL,
    0x10041040UL, 0x00041000UL, 0x00041000UL, 0x00001040UL,
    0x00001040UL, 0x00040040UL, 0x10000000UL, 0x10041000UL
};


LQ_ALIGN(128) static const uint64_t des_ip[8][256] = {

    {CONST64(0x0000000000000000), CONST64(0x0000001000000000), CONST64(0x0000000000000010), CONST64(0x0000001000000010),
    CONST64(0x0000100000000000), CONST64(0x0000101000000000), CONST64(0x0000100000000010), CONST64(0x0000101000000010),
    CONST64(0x0000000000001000), CONST64(0x0000001000001000), CONST64(0x0000000000001010), CONST64(0x0000001000001010),
    CONST64(0x0000100000001000), CONST64(0x0000101000001000), CONST64(0x0000100000001010), CONST64(0x0000101000001010),
    CONST64(0x0010000000000000), CONST64(0x0010001000000000), CONST64(0x0010000000000010), CONST64(0x0010001000000010),
    CONST64(0x0010100000000000), CONST64(0x0010101000000000), CONST64(0x0010100000000010), CONST64(0x0010101000000010),
    CONST64(0x0010000000001000), CONST64(0x0010001000001000), CONST64(0x0010000000001010), CONST64(0x0010001000001010),
    CONST64(0x0010100000001000), CONST64(0x0010101000001000), CONST64(0x0010100000001010), CONST64(0x0010101000001010),
    CONST64(0x0000000000100000), CONST64(0x0000001000100000), CONST64(0x0000000000100010), CONST64(0x0000001000100010),
    CONST64(0x0000100000100000), CONST64(0x0000101000100000), CONST64(0x0000100000100010), CONST64(0x0000101000100010),
    CONST64(0x0000000000101000), CONST64(0x0000001000101000), CONST64(0x0000000000101010), CONST64(0x0000001000101010),
    CONST64(0x0000100000101000), CONST64(0x0000101000101000), CONST64(0x0000100000101010), CONST64(0x0000101000101010),
    CONST64(0x0010000000100000), CONST64(0x0010001000100000), CONST64(0x0010000000100010), CONST64(0x0010001000100010),
    CONST64(0x0010100000100000), CONST64(0x0010101000100000), CONST64(0x0010100000100010), CONST64(0x0010101000100010),
    CONST64(0x0010000000101000), CONST64(0x0010001000101000), CONST64(0x0010000000101010), CONST64(0x0010001000101010),
    CONST64(0x0010100000101000), CONST64(0x0010101000101000), CONST64(0x0010100000101010), CONST64(0x0010101000101010),
    CONST64(0x1000000000000000), CONST64(0x1000001000000000), CONST64(0x1000000000000010), CONST64(0x1000001000000010),
    CONST64(0x1000100000000000), CONST64(0x1000101000000000), CONST64(0x1000100000000010), CONST64(0x1000101000000010),
    CONST64(0x1000000000001000), CONST64(0x1000001000001000), CONST64(0x1000000000001010), CONST64(0x1000001000001010),
    CONST64(0x1000100000001000), CONST64(0x1000101000001000), CONST64(0x1000100000001010), CONST64(0x1000101000001010),
    CONST64(0x1010000000000000), CONST64(0x1010001000000000), CONST64(0x1010000000000010), CONST64(0x1010001000000010),
    CONST64(0x1010100000000000), CONST64(0x1010101000000000), CONST64(0x1010100000000010), CONST64(0x1010101000000010),
    CONST64(0x1010000000001000), CONST64(0x1010001000001000), CONST64(0x1010000000001010), CONST64(0x1010001000001010),
    CONST64(0x1010100000001000), CONST64(0x1010101000001000), CONST64(0x1010100000001010), CONST64(0x1010101000001010),
    CONST64(0x1000000000100000), CONST64(0x1000001000100000), CONST64(0x1000000000100010), CONST64(0x1000001000100010),
    CONST64(0x1000100000100000), CONST64(0x1000101000100000), CONST64(0x1000100000100010), CONST64(0x1000101000100010),
    CONST64(0x1000000000101000), CONST64(0x1000001000101000), CONST64(0x1000000000101010), CONST64(0x1000001000101010),
    CONST64(0x1000100000101000), CONST64(0x1000101000101000), CONST64(0x1000100000101010), CONST64(0x1000101000101010),
    CONST64(0x1010000000100000), CONST64(0x1010001000100000), CONST64(0x1010000000100010), CONST64(0x1010001000100010),
    CONST64(0x1010100000100000), CONST64(0x1010101000100000), CONST64(0x1010100000100010), CONST64(0x1010101000100010),
    CONST64(0x1010000000101000), CONST64(0x1010001000101000), CONST64(0x1010000000101010), CONST64(0x1010001000101010),
    CONST64(0x1010100000101000), CONST64(0x1010101000101000), CONST64(0x1010100000101010), CONST64(0x1010101000101010),
    CONST64(0x0000000010000000), CONST64(0x0000001010000000), CONST64(0x0000000010000010), CONST64(0x0000001010000010),
    CONST64(0x0000100010000000), CONST64(0x0000101010000000), CONST64(0x0000100010000010), CONST64(0x0000101010000010),
    CONST64(0x0000000010001000), CONST64(0x0000001010001000), CONST64(0x0000000010001010), CONST64(0x0000001010001010),
    CONST64(0x0000100010001000), CONST64(0x0000101010001000), CONST64(0x0000100010001010), CONST64(0x0000101010001010),
    CONST64(0x0010000010000000), CONST64(0x0010001010000000), CONST64(0x0010000010000010), CONST64(0x0010001010000010),
    CONST64(0x0010100010000000), CONST64(0x0010101010000000), CONST64(0x0010100010000010), CONST64(0x0010101010000010),
    CONST64(0x0010000010001000), CONST64(0x0010001010001000), CONST64(0x0010000010001010), CONST64(0x0010001010001010),
    CONST64(0x0010100010001000), CONST64(0x0010101010001000), CONST64(0x0010100010001010), CONST64(0x0010101010001010),
    CONST64(0x0000000010100000), CONST64(0x0000001010100000), CONST64(0x0000000010100010), CONST64(0x0000001010100010),
    CONST64(0x0000100010100000), CONST64(0x0000101010100000), CONST64(0x0000100010100010), CONST64(0x0000101010100010),
    CONST64(0x0000000010101000), CONST64(0x0000001010101000), CONST64(0x0000000010101010), CONST64(0x0000001010101010),
    CONST64(0x0000100010101000), CONST64(0x0000101010101000), CONST64(0x0000100010101010), CONST64(0x0000101010101010),
    CONST64(0x0010000010100000), CONST64(0x0010001010100000), CONST64(0x0010000010100010), CONST64(0x0010001010100010),
    CONST64(0x0010100010100000), CONST64(0x0010101010100000), CONST64(0x0010100010100010), CONST64(0x0010101010100010),
    CONST64(0x0010000010101000), CONST64(0x0010001010101000), CONST64(0x0010000010101010), CONST64(0x0010001010101010),
    CONST64(0x0010100010101000), CONST64(0x0010101010101000), CONST64(0x0010100010101010), CONST64(0x0010101010101010),
    CONST64(0x1000000010000000), CONST64(0x1000001010000000), CONST64(0x1000000010000010), CONST64(0x1000001010000010),
    CONST64(0x1000100010000000), CONST64(0x1000101010000000), CONST64(0x1000100010000010), CONST64(0x1000101010000010),
    CONST64(0x1000000010001000), CONST64(0x1000001010001000), CONST64(0x1000000010001010), CONST64(0x1000001010001010),
    CONST64(0x1000100010001000), CONST64(0x1000101010001000), CONST64(0x1000100010001010), CONST64(0x1000101010001010),
    CONST64(0x1010000010000000), CONST64(0x1010001010000000), CONST64(0x1010000010000010), CONST64(0x1010001010000010),
    CONST64(0x1010100010000000), CONST64(0x1010101010000000), CONST64(0x1010100010000010), CONST64(0x1010101010000010),
    CONST64(0x1010000010001000), CONST64(0x1010001010001000), CONST64(0x1010000010001010), CONST64(0x1010001010001010),
    CONST64(0x1010100010001000), CONST64(0x1010101010001000), CONST64(0x1010100010001010), CONST64(0x1010101010001010),
    CONST64(0x1000000010100000), CONST64(0x1000001010100000), CONST64(0x1000000010100010), CONST64(0x1000001010100010),
    CONST64(0x1000100010100000), CONST64(0x1000101010100000), CONST64(0x1000100010100010), CONST64(0x1000101010100010),
    CONST64(0x1000000010101000), CONST64(0x1000001010101000), CONST64(0x1000000010101010), CONST64(0x1000001010101010),
    CONST64(0x1000100010101000), CONST64(0x1000101010101000), CONST64(0x1000100010101010), CONST64(0x1000101010101010),
    CONST64(0x1010000010100000), CONST64(0x1010001010100000), CONST64(0x1010000010100010), CONST64(0x1010001010100010),
    CONST64(0x1010100010100000), CONST64(0x1010101010100000), CONST64(0x1010100010100010), CONST64(0x1010101010100010),
    CONST64(0x1010000010101000), CONST64(0x1010001010101000), CONST64(0x1010000010101010), CONST64(0x1010001010101010),
    CONST64(0x1010100010101000), CONST64(0x1010101010101000), CONST64(0x1010100010101010), CONST64(0x1010101010101010)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000800000000), CONST64(0x0000000000000008), CONST64(0x0000000800000008),
    CONST64(0x0000080000000000), CONST64(0x0000080800000000), CONST64(0x0000080000000008), CONST64(0x0000080800000008),
    CONST64(0x0000000000000800), CONST64(0x0000000800000800), CONST64(0x0000000000000808), CONST64(0x0000000800000808),
    CONST64(0x0000080000000800), CONST64(0x0000080800000800), CONST64(0x0000080000000808), CONST64(0x0000080800000808),
    CONST64(0x0008000000000000), CONST64(0x0008000800000000), CONST64(0x0008000000000008), CONST64(0x0008000800000008),
    CONST64(0x0008080000000000), CONST64(0x0008080800000000), CONST64(0x0008080000000008), CONST64(0x0008080800000008),
    CONST64(0x0008000000000800), CONST64(0x0008000800000800), CONST64(0x0008000000000808), CONST64(0x0008000800000808),
    CONST64(0x0008080000000800), CONST64(0x0008080800000800), CONST64(0x0008080000000808), CONST64(0x0008080800000808),
    CONST64(0x0000000000080000), CONST64(0x0000000800080000), CONST64(0x0000000000080008), CONST64(0x0000000800080008),
    CONST64(0x0000080000080000), CONST64(0x0000080800080000), CONST64(0x0000080000080008), CONST64(0x0000080800080008),
    CONST64(0x0000000000080800), CONST64(0x0000000800080800), CONST64(0x0000000000080808), CONST64(0x0000000800080808),
    CONST64(0x0000080000080800), CONST64(0x0000080800080800), CONST64(0x0000080000080808), CONST64(0x0000080800080808),
    CONST64(0x0008000000080000), CONST64(0x0008000800080000), CONST64(0x0008000000080008), CONST64(0x0008000800080008),
    CONST64(0x0008080000080000), CONST64(0x0008080800080000), CONST64(0x0008080000080008), CONST64(0x0008080800080008),
    CONST64(0x0008000000080800), CONST64(0x0008000800080800), CONST64(0x0008000000080808), CONST64(0x0008000800080808),
    CONST64(0x0008080000080800), CONST64(0x0008080800080800), CONST64(0x0008080000080808), CONST64(0x0008080800080808),
    CONST64(0x0800000000000000), CONST64(0x0800000800000000), CONST64(0x0800000000000008), CONST64(0x0800000800000008),
    CONST64(0x0800080000000000), CONST64(0x0800080800000000), CONST64(0x0800080000000008), CONST64(0x0800080800000008),
    CONST64(0x0800000000000800), CONST64(0x0800000800000800), CONST64(0x0800000000000808), CONST64(0x0800000800000808),
    CONST64(0x0800080000000800), CONST64(0x0800080800000800), CONST64(0x0800080000000808), CONST64(0x0800080800000808),
    CONST64(0x0808000000000000), CONST64(0x0808000800000000), CONST64(0x0808000000000008), CONST64(0x0808000800000008),
    CONST64(0x0808080000000000), CONST64(0x0808080800000000), CONST64(0x0808080000000008), CONST64(0x0808080800000008),
    CONST64(0x0808000000000800), CONST64(0x0808000800000800), CONST64(0x0808000000000808), CONST64(0x0808000800000808),
    CONST64(0x0808080000000800), CONST64(0x0808080800000800), CONST64(0x0808080000000808), CONST64(0x0808080800000808),
    CONST64(0x0800000000080000), CONST64(0x0800000800080000), CONST64(0x0800000000080008), CONST64(0x0800000800080008),
    CONST64(0x0800080000080000), CONST64(0x0800080800080000), CONST64(0x0800080000080008), CONST64(0x0800080800080008),
    CONST64(0x0800000000080800), CONST64(0x0800000800080800), CONST64(0x0800000000080808), CONST64(0x0800000800080808),
    CONST64(0x0800080000080800), CONST64(0x0800080800080800), CONST64(0x0800080000080808), CONST64(0x0800080800080808),
    CONST64(0x0808000000080000), CONST64(0x0808000800080000), CONST64(0x0808000000080008), CONST64(0x0808000800080008),
    CONST64(0x0808080000080000), CONST64(0x0808080800080000), CONST64(0x0808080000080008), CONST64(0x0808080800080008),
    CONST64(0x0808000000080800), CONST64(0x0808000800080800), CONST64(0x0808000000080808), CONST64(0x0808000800080808),
    CONST64(0x0808080000080800), CONST64(0x0808080800080800), CONST64(0x0808080000080808), CONST64(0x0808080800080808),
    CONST64(0x0000000008000000), CONST64(0x0000000808000000), CONST64(0x0000000008000008), CONST64(0x0000000808000008),
    CONST64(0x0000080008000000), CONST64(0x0000080808000000), CONST64(0x0000080008000008), CONST64(0x0000080808000008),
    CONST64(0x0000000008000800), CONST64(0x0000000808000800), CONST64(0x0000000008000808), CONST64(0x0000000808000808),
    CONST64(0x0000080008000800), CONST64(0x0000080808000800), CONST64(0x0000080008000808), CONST64(0x0000080808000808),
    CONST64(0x0008000008000000), CONST64(0x0008000808000000), CONST64(0x0008000008000008), CONST64(0x0008000808000008),
    CONST64(0x0008080008000000), CONST64(0x0008080808000000), CONST64(0x0008080008000008), CONST64(0x0008080808000008),
    CONST64(0x0008000008000800), CONST64(0x0008000808000800), CONST64(0x0008000008000808), CONST64(0x0008000808000808),
    CONST64(0x0008080008000800), CONST64(0x0008080808000800), CONST64(0x0008080008000808), CONST64(0x0008080808000808),
    CONST64(0x0000000008080000), CONST64(0x0000000808080000), CONST64(0x0000000008080008), CONST64(0x0000000808080008),
    CONST64(0x0000080008080000), CONST64(0x0000080808080000), CONST64(0x0000080008080008), CONST64(0x0000080808080008),
    CONST64(0x0000000008080800), CONST64(0x0000000808080800), CONST64(0x0000000008080808), CONST64(0x0000000808080808),
    CONST64(0x0000080008080800), CONST64(0x0000080808080800), CONST64(0x0000080008080808), CONST64(0x0000080808080808),
    CONST64(0x0008000008080000), CONST64(0x0008000808080000), CONST64(0x0008000008080008), CONST64(0x0008000808080008),
    CONST64(0x0008080008080000), CONST64(0x0008080808080000), CONST64(0x0008080008080008), CONST64(0x0008080808080008),
    CONST64(0x0008000008080800), CONST64(0x0008000808080800), CONST64(0x0008000008080808), CONST64(0x0008000808080808),
    CONST64(0x0008080008080800), CONST64(0x0008080808080800), CONST64(0x0008080008080808), CONST64(0x0008080808080808),
    CONST64(0x0800000008000000), CONST64(0x0800000808000000), CONST64(0x0800000008000008), CONST64(0x0800000808000008),
    CONST64(0x0800080008000000), CONST64(0x0800080808000000), CONST64(0x0800080008000008), CONST64(0x0800080808000008),
    CONST64(0x0800000008000800), CONST64(0x0800000808000800), CONST64(0x0800000008000808), CONST64(0x0800000808000808),
    CONST64(0x0800080008000800), CONST64(0x0800080808000800), CONST64(0x0800080008000808), CONST64(0x0800080808000808),
    CONST64(0x0808000008000000), CONST64(0x0808000808000000), CONST64(0x0808000008000008), CONST64(0x0808000808000008),
    CONST64(0x0808080008000000), CONST64(0x0808080808000000), CONST64(0x0808080008000008), CONST64(0x0808080808000008),
    CONST64(0x0808000008000800), CONST64(0x0808000808000800), CONST64(0x0808000008000808), CONST64(0x0808000808000808),
    CONST64(0x0808080008000800), CONST64(0x0808080808000800), CONST64(0x0808080008000808), CONST64(0x0808080808000808),
    CONST64(0x0800000008080000), CONST64(0x0800000808080000), CONST64(0x0800000008080008), CONST64(0x0800000808080008),
    CONST64(0x0800080008080000), CONST64(0x0800080808080000), CONST64(0x0800080008080008), CONST64(0x0800080808080008),
    CONST64(0x0800000008080800), CONST64(0x0800000808080800), CONST64(0x0800000008080808), CONST64(0x0800000808080808),
    CONST64(0x0800080008080800), CONST64(0x0800080808080800), CONST64(0x0800080008080808), CONST64(0x0800080808080808),
    CONST64(0x0808000008080000), CONST64(0x0808000808080000), CONST64(0x0808000008080008), CONST64(0x0808000808080008),
    CONST64(0x0808080008080000), CONST64(0x0808080808080000), CONST64(0x0808080008080008), CONST64(0x0808080808080008),
    CONST64(0x0808000008080800), CONST64(0x0808000808080800), CONST64(0x0808000008080808), CONST64(0x0808000808080808),
    CONST64(0x0808080008080800), CONST64(0x0808080808080800), CONST64(0x0808080008080808), CONST64(0x0808080808080808)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000400000000), CONST64(0x0000000000000004), CONST64(0x0000000400000004),
    CONST64(0x0000040000000000), CONST64(0x0000040400000000), CONST64(0x0000040000000004), CONST64(0x0000040400000004),
    CONST64(0x0000000000000400), CONST64(0x0000000400000400), CONST64(0x0000000000000404), CONST64(0x0000000400000404),
    CONST64(0x0000040000000400), CONST64(0x0000040400000400), CONST64(0x0000040000000404), CONST64(0x0000040400000404),
    CONST64(0x0004000000000000), CONST64(0x0004000400000000), CONST64(0x0004000000000004), CONST64(0x0004000400000004),
    CONST64(0x0004040000000000), CONST64(0x0004040400000000), CONST64(0x0004040000000004), CONST64(0x0004040400000004),
    CONST64(0x0004000000000400), CONST64(0x0004000400000400), CONST64(0x0004000000000404), CONST64(0x0004000400000404),
    CONST64(0x0004040000000400), CONST64(0x0004040400000400), CONST64(0x0004040000000404), CONST64(0x0004040400000404),
    CONST64(0x0000000000040000), CONST64(0x0000000400040000), CONST64(0x0000000000040004), CONST64(0x0000000400040004),
    CONST64(0x0000040000040000), CONST64(0x0000040400040000), CONST64(0x0000040000040004), CONST64(0x0000040400040004),
    CONST64(0x0000000000040400), CONST64(0x0000000400040400), CONST64(0x0000000000040404), CONST64(0x0000000400040404),
    CONST64(0x0000040000040400), CONST64(0x0000040400040400), CONST64(0x0000040000040404), CONST64(0x0000040400040404),
    CONST64(0x0004000000040000), CONST64(0x0004000400040000), CONST64(0x0004000000040004), CONST64(0x0004000400040004),
    CONST64(0x0004040000040000), CONST64(0x0004040400040000), CONST64(0x0004040000040004), CONST64(0x0004040400040004),
    CONST64(0x0004000000040400), CONST64(0x0004000400040400), CONST64(0x0004000000040404), CONST64(0x0004000400040404),
    CONST64(0x0004040000040400), CONST64(0x0004040400040400), CONST64(0x0004040000040404), CONST64(0x0004040400040404),
    CONST64(0x0400000000000000), CONST64(0x0400000400000000), CONST64(0x0400000000000004), CONST64(0x0400000400000004),
    CONST64(0x0400040000000000), CONST64(0x0400040400000000), CONST64(0x0400040000000004), CONST64(0x0400040400000004),
    CONST64(0x0400000000000400), CONST64(0x0400000400000400), CONST64(0x0400000000000404), CONST64(0x0400000400000404),
    CONST64(0x0400040000000400), CONST64(0x0400040400000400), CONST64(0x0400040000000404), CONST64(0x0400040400000404),
    CONST64(0x0404000000000000), CONST64(0x0404000400000000), CONST64(0x0404000000000004), CONST64(0x0404000400000004),
    CONST64(0x0404040000000000), CONST64(0x0404040400000000), CONST64(0x0404040000000004), CONST64(0x0404040400000004),
    CONST64(0x0404000000000400), CONST64(0x0404000400000400), CONST64(0x0404000000000404), CONST64(0x0404000400000404),
    CONST64(0x0404040000000400), CONST64(0x0404040400000400), CONST64(0x0404040000000404), CONST64(0x0404040400000404),
    CONST64(0x0400000000040000), CONST64(0x0400000400040000), CONST64(0x0400000000040004), CONST64(0x0400000400040004),
    CONST64(0x0400040000040000), CONST64(0x0400040400040000), CONST64(0x0400040000040004), CONST64(0x0400040400040004),
    CONST64(0x0400000000040400), CONST64(0x0400000400040400), CONST64(0x0400000000040404), CONST64(0x0400000400040404),
    CONST64(0x0400040000040400), CONST64(0x0400040400040400), CONST64(0x0400040000040404), CONST64(0x0400040400040404),
    CONST64(0x0404000000040000), CONST64(0x0404000400040000), CONST64(0x0404000000040004), CONST64(0x0404000400040004),
    CONST64(0x0404040000040000), CONST64(0x0404040400040000), CONST64(0x0404040000040004), CONST64(0x0404040400040004),
    CONST64(0x0404000000040400), CONST64(0x0404000400040400), CONST64(0x0404000000040404), CONST64(0x0404000400040404),
    CONST64(0x0404040000040400), CONST64(0x0404040400040400), CONST64(0x0404040000040404), CONST64(0x0404040400040404),
    CONST64(0x0000000004000000), CONST64(0x0000000404000000), CONST64(0x0000000004000004), CONST64(0x0000000404000004),
    CONST64(0x0000040004000000), CONST64(0x0000040404000000), CONST64(0x0000040004000004), CONST64(0x0000040404000004),
    CONST64(0x0000000004000400), CONST64(0x0000000404000400), CONST64(0x0000000004000404), CONST64(0x0000000404000404),
    CONST64(0x0000040004000400), CONST64(0x0000040404000400), CONST64(0x0000040004000404), CONST64(0x0000040404000404),
    CONST64(0x0004000004000000), CONST64(0x0004000404000000), CONST64(0x0004000004000004), CONST64(0x0004000404000004),
    CONST64(0x0004040004000000), CONST64(0x0004040404000000), CONST64(0x0004040004000004), CONST64(0x0004040404000004),
    CONST64(0x0004000004000400), CONST64(0x0004000404000400), CONST64(0x0004000004000404), CONST64(0x0004000404000404),
    CONST64(0x0004040004000400), CONST64(0x0004040404000400), CONST64(0x0004040004000404), CONST64(0x0004040404000404),
    CONST64(0x0000000004040000), CONST64(0x0000000404040000), CONST64(0x0000000004040004), CONST64(0x0000000404040004),
    CONST64(0x0000040004040000), CONST64(0x0000040404040000), CONST64(0x0000040004040004), CONST64(0x0000040404040004),
    CONST64(0x0000000004040400), CONST64(0x0000000404040400), CONST64(0x0000000004040404), CONST64(0x0000000404040404),
    CONST64(0x0000040004040400), CONST64(0x0000040404040400), CONST64(0x0000040004040404), CONST64(0x0000040404040404),
    CONST64(0x0004000004040000), CONST64(0x0004000404040000), CONST64(0x0004000004040004), CONST64(0x0004000404040004),
    CONST64(0x0004040004040000), CONST64(0x0004040404040000), CONST64(0x0004040004040004), CONST64(0x0004040404040004),
    CONST64(0x0004000004040400), CONST64(0x0004000404040400), CONST64(0x0004000004040404), CONST64(0x0004000404040404),
    CONST64(0x0004040004040400), CONST64(0x0004040404040400), CONST64(0x0004040004040404), CONST64(0x0004040404040404),
    CONST64(0x0400000004000000), CONST64(0x0400000404000000), CONST64(0x0400000004000004), CONST64(0x0400000404000004),
    CONST64(0x0400040004000000), CONST64(0x0400040404000000), CONST64(0x0400040004000004), CONST64(0x0400040404000004),
    CONST64(0x0400000004000400), CONST64(0x0400000404000400), CONST64(0x0400000004000404), CONST64(0x0400000404000404),
    CONST64(0x0400040004000400), CONST64(0x0400040404000400), CONST64(0x0400040004000404), CONST64(0x0400040404000404),
    CONST64(0x0404000004000000), CONST64(0x0404000404000000), CONST64(0x0404000004000004), CONST64(0x0404000404000004),
    CONST64(0x0404040004000000), CONST64(0x0404040404000000), CONST64(0x0404040004000004), CONST64(0x0404040404000004),
    CONST64(0x0404000004000400), CONST64(0x0404000404000400), CONST64(0x0404000004000404), CONST64(0x0404000404000404),
    CONST64(0x0404040004000400), CONST64(0x0404040404000400), CONST64(0x0404040004000404), CONST64(0x0404040404000404),
    CONST64(0x0400000004040000), CONST64(0x0400000404040000), CONST64(0x0400000004040004), CONST64(0x0400000404040004),
    CONST64(0x0400040004040000), CONST64(0x0400040404040000), CONST64(0x0400040004040004), CONST64(0x0400040404040004),
    CONST64(0x0400000004040400), CONST64(0x0400000404040400), CONST64(0x0400000004040404), CONST64(0x0400000404040404),
    CONST64(0x0400040004040400), CONST64(0x0400040404040400), CONST64(0x0400040004040404), CONST64(0x0400040404040404),
    CONST64(0x0404000004040000), CONST64(0x0404000404040000), CONST64(0x0404000004040004), CONST64(0x0404000404040004),
    CONST64(0x0404040004040000), CONST64(0x0404040404040000), CONST64(0x0404040004040004), CONST64(0x0404040404040004),
    CONST64(0x0404000004040400), CONST64(0x0404000404040400), CONST64(0x0404000004040404), CONST64(0x0404000404040404),
    CONST64(0x0404040004040400), CONST64(0x0404040404040400), CONST64(0x0404040004040404), CONST64(0x0404040404040404)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000200000000), CONST64(0x0000000000000002), CONST64(0x0000000200000002),
    CONST64(0x0000020000000000), CONST64(0x0000020200000000), CONST64(0x0000020000000002), CONST64(0x0000020200000002),
    CONST64(0x0000000000000200), CONST64(0x0000000200000200), CONST64(0x0000000000000202), CONST64(0x0000000200000202),
    CONST64(0x0000020000000200), CONST64(0x0000020200000200), CONST64(0x0000020000000202), CONST64(0x0000020200000202),
    CONST64(0x0002000000000000), CONST64(0x0002000200000000), CONST64(0x0002000000000002), CONST64(0x0002000200000002),
    CONST64(0x0002020000000000), CONST64(0x0002020200000000), CONST64(0x0002020000000002), CONST64(0x0002020200000002),
    CONST64(0x0002000000000200), CONST64(0x0002000200000200), CONST64(0x0002000000000202), CONST64(0x0002000200000202),
    CONST64(0x0002020000000200), CONST64(0x0002020200000200), CONST64(0x0002020000000202), CONST64(0x0002020200000202),
    CONST64(0x0000000000020000), CONST64(0x0000000200020000), CONST64(0x0000000000020002), CONST64(0x0000000200020002),
    CONST64(0x0000020000020000), CONST64(0x0000020200020000), CONST64(0x0000020000020002), CONST64(0x0000020200020002),
    CONST64(0x0000000000020200), CONST64(0x0000000200020200), CONST64(0x0000000000020202), CONST64(0x0000000200020202),
    CONST64(0x0000020000020200), CONST64(0x0000020200020200), CONST64(0x0000020000020202), CONST64(0x0000020200020202),
    CONST64(0x0002000000020000), CONST64(0x0002000200020000), CONST64(0x0002000000020002), CONST64(0x0002000200020002),
    CONST64(0x0002020000020000), CONST64(0x0002020200020000), CONST64(0x0002020000020002), CONST64(0x0002020200020002),
    CONST64(0x0002000000020200), CONST64(0x0002000200020200), CONST64(0x0002000000020202), CONST64(0x0002000200020202),
    CONST64(0x0002020000020200), CONST64(0x0002020200020200), CONST64(0x0002020000020202), CONST64(0x0002020200020202),
    CONST64(0x0200000000000000), CONST64(0x0200000200000000), CONST64(0x0200000000000002), CONST64(0x0200000200000002),
    CONST64(0x0200020000000000), CONST64(0x0200020200000000), CONST64(0x0200020000000002), CONST64(0x0200020200000002),
    CONST64(0x0200000000000200), CONST64(0x0200000200000200), CONST64(0x0200000000000202), CONST64(0x0200000200000202),
    CONST64(0x0200020000000200), CONST64(0x0200020200000200), CONST64(0x0200020000000202), CONST64(0x0200020200000202),
    CONST64(0x0202000000000000), CONST64(0x0202000200000000), CONST64(0x0202000000000002), CONST64(0x0202000200000002),
    CONST64(0x0202020000000000), CONST64(0x0202020200000000), CONST64(0x0202020000000002), CONST64(0x0202020200000002),
    CONST64(0x0202000000000200), CONST64(0x0202000200000200), CONST64(0x0202000000000202), CONST64(0x0202000200000202),
    CONST64(0x0202020000000200), CONST64(0x0202020200000200), CONST64(0x0202020000000202), CONST64(0x0202020200000202),
    CONST64(0x0200000000020000), CONST64(0x0200000200020000), CONST64(0x0200000000020002), CONST64(0x0200000200020002),
    CONST64(0x0200020000020000), CONST64(0x0200020200020000), CONST64(0x0200020000020002), CONST64(0x0200020200020002),
    CONST64(0x0200000000020200), CONST64(0x0200000200020200), CONST64(0x0200000000020202), CONST64(0x0200000200020202),
    CONST64(0x0200020000020200), CONST64(0x0200020200020200), CONST64(0x0200020000020202), CONST64(0x0200020200020202),
    CONST64(0x0202000000020000), CONST64(0x0202000200020000), CONST64(0x0202000000020002), CONST64(0x0202000200020002),
    CONST64(0x0202020000020000), CONST64(0x0202020200020000), CONST64(0x0202020000020002), CONST64(0x0202020200020002),
    CONST64(0x0202000000020200), CONST64(0x0202000200020200), CONST64(0x0202000000020202), CONST64(0x0202000200020202),
    CONST64(0x0202020000020200), CONST64(0x0202020200020200), CONST64(0x0202020000020202), CONST64(0x0202020200020202),
    CONST64(0x0000000002000000), CONST64(0x0000000202000000), CONST64(0x0000000002000002), CONST64(0x0000000202000002),
    CONST64(0x0000020002000000), CONST64(0x0000020202000000), CONST64(0x0000020002000002), CONST64(0x0000020202000002),
    CONST64(0x0000000002000200), CONST64(0x0000000202000200), CONST64(0x0000000002000202), CONST64(0x0000000202000202),
    CONST64(0x0000020002000200), CONST64(0x0000020202000200), CONST64(0x0000020002000202), CONST64(0x0000020202000202),
    CONST64(0x0002000002000000), CONST64(0x0002000202000000), CONST64(0x0002000002000002), CONST64(0x0002000202000002),
    CONST64(0x0002020002000000), CONST64(0x0002020202000000), CONST64(0x0002020002000002), CONST64(0x0002020202000002),
    CONST64(0x0002000002000200), CONST64(0x0002000202000200), CONST64(0x0002000002000202), CONST64(0x0002000202000202),
    CONST64(0x0002020002000200), CONST64(0x0002020202000200), CONST64(0x0002020002000202), CONST64(0x0002020202000202),
    CONST64(0x0000000002020000), CONST64(0x0000000202020000), CONST64(0x0000000002020002), CONST64(0x0000000202020002),
    CONST64(0x0000020002020000), CONST64(0x0000020202020000), CONST64(0x0000020002020002), CONST64(0x0000020202020002),
    CONST64(0x0000000002020200), CONST64(0x0000000202020200), CONST64(0x0000000002020202), CONST64(0x0000000202020202),
    CONST64(0x0000020002020200), CONST64(0x0000020202020200), CONST64(0x0000020002020202), CONST64(0x0000020202020202),
    CONST64(0x0002000002020000), CONST64(0x0002000202020000), CONST64(0x0002000002020002), CONST64(0x0002000202020002),
    CONST64(0x0002020002020000), CONST64(0x0002020202020000), CONST64(0x0002020002020002), CONST64(0x0002020202020002),
    CONST64(0x0002000002020200), CONST64(0x0002000202020200), CONST64(0x0002000002020202), CONST64(0x0002000202020202),
    CONST64(0x0002020002020200), CONST64(0x0002020202020200), CONST64(0x0002020002020202), CONST64(0x0002020202020202),
    CONST64(0x0200000002000000), CONST64(0x0200000202000000), CONST64(0x0200000002000002), CONST64(0x0200000202000002),
    CONST64(0x0200020002000000), CONST64(0x0200020202000000), CONST64(0x0200020002000002), CONST64(0x0200020202000002),
    CONST64(0x0200000002000200), CONST64(0x0200000202000200), CONST64(0x0200000002000202), CONST64(0x0200000202000202),
    CONST64(0x0200020002000200), CONST64(0x0200020202000200), CONST64(0x0200020002000202), CONST64(0x0200020202000202),
    CONST64(0x0202000002000000), CONST64(0x0202000202000000), CONST64(0x0202000002000002), CONST64(0x0202000202000002),
    CONST64(0x0202020002000000), CONST64(0x0202020202000000), CONST64(0x0202020002000002), CONST64(0x0202020202000002),
    CONST64(0x0202000002000200), CONST64(0x0202000202000200), CONST64(0x0202000002000202), CONST64(0x0202000202000202),
    CONST64(0x0202020002000200), CONST64(0x0202020202000200), CONST64(0x0202020002000202), CONST64(0x0202020202000202),
    CONST64(0x0200000002020000), CONST64(0x0200000202020000), CONST64(0x0200000002020002), CONST64(0x0200000202020002),
    CONST64(0x0200020002020000), CONST64(0x0200020202020000), CONST64(0x0200020002020002), CONST64(0x0200020202020002),
    CONST64(0x0200000002020200), CONST64(0x0200000202020200), CONST64(0x0200000002020202), CONST64(0x0200000202020202),
    CONST64(0x0200020002020200), CONST64(0x0200020202020200), CONST64(0x0200020002020202), CONST64(0x0200020202020202),
    CONST64(0x0202000002020000), CONST64(0x0202000202020000), CONST64(0x0202000002020002), CONST64(0x0202000202020002),
    CONST64(0x0202020002020000), CONST64(0x0202020202020000), CONST64(0x0202020002020002), CONST64(0x0202020202020002),
    CONST64(0x0202000002020200), CONST64(0x0202000202020200), CONST64(0x0202000002020202), CONST64(0x0202000202020202),
    CONST64(0x0202020002020200), CONST64(0x0202020202020200), CONST64(0x0202020002020202), CONST64(0x0202020202020202)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000010000000000), CONST64(0x0000000000000100), CONST64(0x0000010000000100),
    CONST64(0x0001000000000000), CONST64(0x0001010000000000), CONST64(0x0001000000000100), CONST64(0x0001010000000100),
    CONST64(0x0000000000010000), CONST64(0x0000010000010000), CONST64(0x0000000000010100), CONST64(0x0000010000010100),
    CONST64(0x0001000000010000), CONST64(0x0001010000010000), CONST64(0x0001000000010100), CONST64(0x0001010000010100),
    CONST64(0x0100000000000000), CONST64(0x0100010000000000), CONST64(0x0100000000000100), CONST64(0x0100010000000100),
    CONST64(0x0101000000000000), CONST64(0x0101010000000000), CONST64(0x0101000000000100), CONST64(0x0101010000000100),
    CONST64(0x0100000000010000), CONST64(0x0100010000010000), CONST64(0x0100000000010100), CONST64(0x0100010000010100),
    CONST64(0x0101000000010000), CONST64(0x0101010000010000), CONST64(0x0101000000010100), CONST64(0x0101010000010100),
    CONST64(0x0000000001000000), CONST64(0x0000010001000000), CONST64(0x0000000001000100), CONST64(0x0000010001000100),
    CONST64(0x0001000001000000), CONST64(0x0001010001000000), CONST64(0x0001000001000100), CONST64(0x0001010001000100),
    CONST64(0x0000000001010000), CONST64(0x0000010001010000), CONST64(0x0000000001010100), CONST64(0x0000010001010100),
    CONST64(0x0001000001010000), CONST64(0x0001010001010000), CONST64(0x0001000001010100), CONST64(0x0001010001010100),
    CONST64(0x0100000001000000), CONST64(0x0100010001000000), CONST64(0x0100000001000100), CONST64(0x0100010001000100),
    CONST64(0x0101000001000000), CONST64(0x0101010001000000), CONST64(0x0101000001000100), CONST64(0x0101010001000100),
    CONST64(0x0100000001010000), CONST64(0x0100010001010000), CONST64(0x0100000001010100), CONST64(0x0100010001010100),
    CONST64(0x0101000001010000), CONST64(0x0101010001010000), CONST64(0x0101000001010100), CONST64(0x0101010001010100),
    CONST64(0x0000000100000000), CONST64(0x0000010100000000), CONST64(0x0000000100000100), CONST64(0x0000010100000100),
    CONST64(0x0001000100000000), CONST64(0x0001010100000000), CONST64(0x0001000100000100), CONST64(0x0001010100000100),
    CONST64(0x0000000100010000), CONST64(0x0000010100010000), CONST64(0x0000000100010100), CONST64(0x0000010100010100),
    CONST64(0x0001000100010000), CONST64(0x0001010100010000), CONST64(0x0001000100010100), CONST64(0x0001010100010100),
    CONST64(0x0100000100000000), CONST64(0x0100010100000000), CONST64(0x0100000100000100), CONST64(0x0100010100000100),
    CONST64(0x0101000100000000), CONST64(0x0101010100000000), CONST64(0x0101000100000100), CONST64(0x0101010100000100),
    CONST64(0x0100000100010000), CONST64(0x0100010100010000), CONST64(0x0100000100010100), CONST64(0x0100010100010100),
    CONST64(0x0101000100010000), CONST64(0x0101010100010000), CONST64(0x0101000100010100), CONST64(0x0101010100010100),
    CONST64(0x0000000101000000), CONST64(0x0000010101000000), CONST64(0x0000000101000100), CONST64(0x0000010101000100),
    CONST64(0x0001000101000000), CONST64(0x0001010101000000), CONST64(0x0001000101000100), CONST64(0x0001010101000100),
    CONST64(0x0000000101010000), CONST64(0x0000010101010000), CONST64(0x0000000101010100), CONST64(0x0000010101010100),
    CONST64(0x0001000101010000), CONST64(0x0001010101010000), CONST64(0x0001000101010100), CONST64(0x0001010101010100),
    CONST64(0x0100000101000000), CONST64(0x0100010101000000), CONST64(0x0100000101000100), CONST64(0x0100010101000100),
    CONST64(0x0101000101000000), CONST64(0x0101010101000000), CONST64(0x0101000101000100), CONST64(0x0101010101000100),
    CONST64(0x0100000101010000), CONST64(0x0100010101010000), CONST64(0x0100000101010100), CONST64(0x0100010101010100),
    CONST64(0x0101000101010000), CONST64(0x0101010101010000), CONST64(0x0101000101010100), CONST64(0x0101010101010100),
    CONST64(0x0000000000000001), CONST64(0x0000010000000001), CONST64(0x0000000000000101), CONST64(0x0000010000000101),
    CONST64(0x0001000000000001), CONST64(0x0001010000000001), CONST64(0x0001000000000101), CONST64(0x0001010000000101),
    CONST64(0x0000000000010001), CONST64(0x0000010000010001), CONST64(0x0000000000010101), CONST64(0x0000010000010101),
    CONST64(0x0001000000010001), CONST64(0x0001010000010001), CONST64(0x0001000000010101), CONST64(0x0001010000010101),
    CONST64(0x0100000000000001), CONST64(0x0100010000000001), CONST64(0x0100000000000101), CONST64(0x0100010000000101),
    CONST64(0x0101000000000001), CONST64(0x0101010000000001), CONST64(0x0101000000000101), CONST64(0x0101010000000101),
    CONST64(0x0100000000010001), CONST64(0x0100010000010001), CONST64(0x0100000000010101), CONST64(0x0100010000010101),
    CONST64(0x0101000000010001), CONST64(0x0101010000010001), CONST64(0x0101000000010101), CONST64(0x0101010000010101),
    CONST64(0x0000000001000001), CONST64(0x0000010001000001), CONST64(0x0000000001000101), CONST64(0x0000010001000101),
    CONST64(0x0001000001000001), CONST64(0x0001010001000001), CONST64(0x0001000001000101), CONST64(0x0001010001000101),
    CONST64(0x0000000001010001), CONST64(0x0000010001010001), CONST64(0x0000000001010101), CONST64(0x0000010001010101),
    CONST64(0x0001000001010001), CONST64(0x0001010001010001), CONST64(0x0001000001010101), CONST64(0x0001010001010101),
    CONST64(0x0100000001000001), CONST64(0x0100010001000001), CONST64(0x0100000001000101), CONST64(0x0100010001000101),
    CONST64(0x0101000001000001), CONST64(0x0101010001000001), CONST64(0x0101000001000101), CONST64(0x0101010001000101),
    CONST64(0x0100000001010001), CONST64(0x0100010001010001), CONST64(0x0100000001010101), CONST64(0x0100010001010101),
    CONST64(0x0101000001010001), CONST64(0x0101010001010001), CONST64(0x0101000001010101), CONST64(0x0101010001010101),
    CONST64(0x0000000100000001), CONST64(0x0000010100000001), CONST64(0x0000000100000101), CONST64(0x0000010100000101),
    CONST64(0x0001000100000001), CONST64(0x0001010100000001), CONST64(0x0001000100000101), CONST64(0x0001010100000101),
    CONST64(0x0000000100010001), CONST64(0x0000010100010001), CONST64(0x0000000100010101), CONST64(0x0000010100010101),
    CONST64(0x0001000100010001), CONST64(0x0001010100010001), CONST64(0x0001000100010101), CONST64(0x0001010100010101),
    CONST64(0x0100000100000001), CONST64(0x0100010100000001), CONST64(0x0100000100000101), CONST64(0x0100010100000101),
    CONST64(0x0101000100000001), CONST64(0x0101010100000001), CONST64(0x0101000100000101), CONST64(0x0101010100000101),
    CONST64(0x0100000100010001), CONST64(0x0100010100010001), CONST64(0x0100000100010101), CONST64(0x0100010100010101),
    CONST64(0x0101000100010001), CONST64(0x0101010100010001), CONST64(0x0101000100010101), CONST64(0x0101010100010101),
    CONST64(0x0000000101000001), CONST64(0x0000010101000001), CONST64(0x0000000101000101), CONST64(0x0000010101000101),
    CONST64(0x0001000101000001), CONST64(0x0001010101000001), CONST64(0x0001000101000101), CONST64(0x0001010101000101),
    CONST64(0x0000000101010001), CONST64(0x0000010101010001), CONST64(0x0000000101010101), CONST64(0x0000010101010101),
    CONST64(0x0001000101010001), CONST64(0x0001010101010001), CONST64(0x0001000101010101), CONST64(0x0001010101010101),
    CONST64(0x0100000101000001), CONST64(0x0100010101000001), CONST64(0x0100000101000101), CONST64(0x0100010101000101),
    CONST64(0x0101000101000001), CONST64(0x0101010101000001), CONST64(0x0101000101000101), CONST64(0x0101010101000101),
    CONST64(0x0100000101010001), CONST64(0x0100010101010001), CONST64(0x0100000101010101), CONST64(0x0100010101010101),
    CONST64(0x0101000101010001), CONST64(0x0101010101010001), CONST64(0x0101000101010101), CONST64(0x0101010101010101)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000008000000000), CONST64(0x0000000000000080), CONST64(0x0000008000000080),
    CONST64(0x0000800000000000), CONST64(0x0000808000000000), CONST64(0x0000800000000080), CONST64(0x0000808000000080),
    CONST64(0x0000000000008000), CONST64(0x0000008000008000), CONST64(0x0000000000008080), CONST64(0x0000008000008080),
    CONST64(0x0000800000008000), CONST64(0x0000808000008000), CONST64(0x0000800000008080), CONST64(0x0000808000008080),
    CONST64(0x0080000000000000), CONST64(0x0080008000000000), CONST64(0x0080000000000080), CONST64(0x0080008000000080),
    CONST64(0x0080800000000000), CONST64(0x0080808000000000), CONST64(0x0080800000000080), CONST64(0x0080808000000080),
    CONST64(0x0080000000008000), CONST64(0x0080008000008000), CONST64(0x0080000000008080), CONST64(0x0080008000008080),
    CONST64(0x0080800000008000), CONST64(0x0080808000008000), CONST64(0x0080800000008080), CONST64(0x0080808000008080),
    CONST64(0x0000000000800000), CONST64(0x0000008000800000), CONST64(0x0000000000800080), CONST64(0x0000008000800080),
    CONST64(0x0000800000800000), CONST64(0x0000808000800000), CONST64(0x0000800000800080), CONST64(0x0000808000800080),
    CONST64(0x0000000000808000), CONST64(0x0000008000808000), CONST64(0x0000000000808080), CONST64(0x0000008000808080),
    CONST64(0x0000800000808000), CONST64(0x0000808000808000), CONST64(0x0000800000808080), CONST64(0x0000808000808080),
    CONST64(0x0080000000800000), CONST64(0x0080008000800000), CONST64(0x0080000000800080), CONST64(0x0080008000800080),
    CONST64(0x0080800000800000), CONST64(0x0080808000800000), CONST64(0x0080800000800080), CONST64(0x0080808000800080),
    CONST64(0x0080000000808000), CONST64(0x0080008000808000), CONST64(0x0080000000808080), CONST64(0x0080008000808080),
    CONST64(0x0080800000808000), CONST64(0x0080808000808000), CONST64(0x0080800000808080), CONST64(0x0080808000808080),
    CONST64(0x8000000000000000), CONST64(0x8000008000000000), CONST64(0x8000000000000080), CONST64(0x8000008000000080),
    CONST64(0x8000800000000000), CONST64(0x8000808000000000), CONST64(0x8000800000000080), CONST64(0x8000808000000080),
    CONST64(0x8000000000008000), CONST64(0x8000008000008000), CONST64(0x8000000000008080), CONST64(0x8000008000008080),
    CONST64(0x8000800000008000), CONST64(0x8000808000008000), CONST64(0x8000800000008080), CONST64(0x8000808000008080),
    CONST64(0x8080000000000000), CONST64(0x8080008000000000), CONST64(0x8080000000000080), CONST64(0x8080008000000080),
    CONST64(0x8080800000000000), CONST64(0x8080808000000000), CONST64(0x8080800000000080), CONST64(0x8080808000000080),
    CONST64(0x8080000000008000), CONST64(0x8080008000008000), CONST64(0x8080000000008080), CONST64(0x8080008000008080),
    CONST64(0x8080800000008000), CONST64(0x8080808000008000), CONST64(0x8080800000008080), CONST64(0x8080808000008080),
    CONST64(0x8000000000800000), CONST64(0x8000008000800000), CONST64(0x8000000000800080), CONST64(0x8000008000800080),
    CONST64(0x8000800000800000), CONST64(0x8000808000800000), CONST64(0x8000800000800080), CONST64(0x8000808000800080),
    CONST64(0x8000000000808000), CONST64(0x8000008000808000), CONST64(0x8000000000808080), CONST64(0x8000008000808080),
    CONST64(0x8000800000808000), CONST64(0x8000808000808000), CONST64(0x8000800000808080), CONST64(0x8000808000808080),
    CONST64(0x8080000000800000), CONST64(0x8080008000800000), CONST64(0x8080000000800080), CONST64(0x8080008000800080),
    CONST64(0x8080800000800000), CONST64(0x8080808000800000), CONST64(0x8080800000800080), CONST64(0x8080808000800080),
    CONST64(0x8080000000808000), CONST64(0x8080008000808000), CONST64(0x8080000000808080), CONST64(0x8080008000808080),
    CONST64(0x8080800000808000), CONST64(0x8080808000808000), CONST64(0x8080800000808080), CONST64(0x8080808000808080),
    CONST64(0x0000000080000000), CONST64(0x0000008080000000), CONST64(0x0000000080000080), CONST64(0x0000008080000080),
    CONST64(0x0000800080000000), CONST64(0x0000808080000000), CONST64(0x0000800080000080), CONST64(0x0000808080000080),
    CONST64(0x0000000080008000), CONST64(0x0000008080008000), CONST64(0x0000000080008080), CONST64(0x0000008080008080),
    CONST64(0x0000800080008000), CONST64(0x0000808080008000), CONST64(0x0000800080008080), CONST64(0x0000808080008080),
    CONST64(0x0080000080000000), CONST64(0x0080008080000000), CONST64(0x0080000080000080), CONST64(0x0080008080000080),
    CONST64(0x0080800080000000), CONST64(0x0080808080000000), CONST64(0x0080800080000080), CONST64(0x0080808080000080),
    CONST64(0x0080000080008000), CONST64(0x0080008080008000), CONST64(0x0080000080008080), CONST64(0x0080008080008080),
    CONST64(0x0080800080008000), CONST64(0x0080808080008000), CONST64(0x0080800080008080), CONST64(0x0080808080008080),
    CONST64(0x0000000080800000), CONST64(0x0000008080800000), CONST64(0x0000000080800080), CONST64(0x0000008080800080),
    CONST64(0x0000800080800000), CONST64(0x0000808080800000), CONST64(0x0000800080800080), CONST64(0x0000808080800080),
    CONST64(0x0000000080808000), CONST64(0x0000008080808000), CONST64(0x0000000080808080), CONST64(0x0000008080808080),
    CONST64(0x0000800080808000), CONST64(0x0000808080808000), CONST64(0x0000800080808080), CONST64(0x0000808080808080),
    CONST64(0x0080000080800000), CONST64(0x0080008080800000), CONST64(0x0080000080800080), CONST64(0x0080008080800080),
    CONST64(0x0080800080800000), CONST64(0x0080808080800000), CONST64(0x0080800080800080), CONST64(0x0080808080800080),
    CONST64(0x0080000080808000), CONST64(0x0080008080808000), CONST64(0x0080000080808080), CONST64(0x0080008080808080),
    CONST64(0x0080800080808000), CONST64(0x0080808080808000), CONST64(0x0080800080808080), CONST64(0x0080808080808080),
    CONST64(0x8000000080000000), CONST64(0x8000008080000000), CONST64(0x8000000080000080), CONST64(0x8000008080000080),
    CONST64(0x8000800080000000), CONST64(0x8000808080000000), CONST64(0x8000800080000080), CONST64(0x8000808080000080),
    CONST64(0x8000000080008000), CONST64(0x8000008080008000), CONST64(0x8000000080008080), CONST64(0x8000008080008080),
    CONST64(0x8000800080008000), CONST64(0x8000808080008000), CONST64(0x8000800080008080), CONST64(0x8000808080008080),
    CONST64(0x8080000080000000), CONST64(0x8080008080000000), CONST64(0x8080000080000080), CONST64(0x8080008080000080),
    CONST64(0x8080800080000000), CONST64(0x8080808080000000), CONST64(0x8080800080000080), CONST64(0x8080808080000080),
    CONST64(0x8080000080008000), CONST64(0x8080008080008000), CONST64(0x8080000080008080), CONST64(0x8080008080008080),
    CONST64(0x8080800080008000), CONST64(0x8080808080008000), CONST64(0x8080800080008080), CONST64(0x8080808080008080),
    CONST64(0x8000000080800000), CONST64(0x8000008080800000), CONST64(0x8000000080800080), CONST64(0x8000008080800080),
    CONST64(0x8000800080800000), CONST64(0x8000808080800000), CONST64(0x8000800080800080), CONST64(0x8000808080800080),
    CONST64(0x8000000080808000), CONST64(0x8000008080808000), CONST64(0x8000000080808080), CONST64(0x8000008080808080),
    CONST64(0x8000800080808000), CONST64(0x8000808080808000), CONST64(0x8000800080808080), CONST64(0x8000808080808080),
    CONST64(0x8080000080800000), CONST64(0x8080008080800000), CONST64(0x8080000080800080), CONST64(0x8080008080800080),
    CONST64(0x8080800080800000), CONST64(0x8080808080800000), CONST64(0x8080800080800080), CONST64(0x8080808080800080),
    CONST64(0x8080000080808000), CONST64(0x8080008080808000), CONST64(0x8080000080808080), CONST64(0x8080008080808080),
    CONST64(0x8080800080808000), CONST64(0x8080808080808000), CONST64(0x8080800080808080), CONST64(0x8080808080808080)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000004000000000), CONST64(0x0000000000000040), CONST64(0x0000004000000040),
    CONST64(0x0000400000000000), CONST64(0x0000404000000000), CONST64(0x0000400000000040), CONST64(0x0000404000000040),
    CONST64(0x0000000000004000), CONST64(0x0000004000004000), CONST64(0x0000000000004040), CONST64(0x0000004000004040),
    CONST64(0x0000400000004000), CONST64(0x0000404000004000), CONST64(0x0000400000004040), CONST64(0x0000404000004040),
    CONST64(0x0040000000000000), CONST64(0x0040004000000000), CONST64(0x0040000000000040), CONST64(0x0040004000000040),
    CONST64(0x0040400000000000), CONST64(0x0040404000000000), CONST64(0x0040400000000040), CONST64(0x0040404000000040),
    CONST64(0x0040000000004000), CONST64(0x0040004000004000), CONST64(0x0040000000004040), CONST64(0x0040004000004040),
    CONST64(0x0040400000004000), CONST64(0x0040404000004000), CONST64(0x0040400000004040), CONST64(0x0040404000004040),
    CONST64(0x0000000000400000), CONST64(0x0000004000400000), CONST64(0x0000000000400040), CONST64(0x0000004000400040),
    CONST64(0x0000400000400000), CONST64(0x0000404000400000), CONST64(0x0000400000400040), CONST64(0x0000404000400040),
    CONST64(0x0000000000404000), CONST64(0x0000004000404000), CONST64(0x0000000000404040), CONST64(0x0000004000404040),
    CONST64(0x0000400000404000), CONST64(0x0000404000404000), CONST64(0x0000400000404040), CONST64(0x0000404000404040),
    CONST64(0x0040000000400000), CONST64(0x0040004000400000), CONST64(0x0040000000400040), CONST64(0x0040004000400040),
    CONST64(0x0040400000400000), CONST64(0x0040404000400000), CONST64(0x0040400000400040), CONST64(0x0040404000400040),
    CONST64(0x0040000000404000), CONST64(0x0040004000404000), CONST64(0x0040000000404040), CONST64(0x0040004000404040),
    CONST64(0x0040400000404000), CONST64(0x0040404000404000), CONST64(0x0040400000404040), CONST64(0x0040404000404040),
    CONST64(0x4000000000000000), CONST64(0x4000004000000000), CONST64(0x4000000000000040), CONST64(0x4000004000000040),
    CONST64(0x4000400000000000), CONST64(0x4000404000000000), CONST64(0x4000400000000040), CONST64(0x4000404000000040),
    CONST64(0x4000000000004000), CONST64(0x4000004000004000), CONST64(0x4000000000004040), CONST64(0x4000004000004040),
    CONST64(0x4000400000004000), CONST64(0x4000404000004000), CONST64(0x4000400000004040), CONST64(0x4000404000004040),
    CONST64(0x4040000000000000), CONST64(0x4040004000000000), CONST64(0x4040000000000040), CONST64(0x4040004000000040),
    CONST64(0x4040400000000000), CONST64(0x4040404000000000), CONST64(0x4040400000000040), CONST64(0x4040404000000040),
    CONST64(0x4040000000004000), CONST64(0x4040004000004000), CONST64(0x4040000000004040), CONST64(0x4040004000004040),
    CONST64(0x4040400000004000), CONST64(0x4040404000004000), CONST64(0x4040400000004040), CONST64(0x4040404000004040),
    CONST64(0x4000000000400000), CONST64(0x4000004000400000), CONST64(0x4000000000400040), CONST64(0x4000004000400040),
    CONST64(0x4000400000400000), CONST64(0x4000404000400000), CONST64(0x4000400000400040), CONST64(0x4000404000400040),
    CONST64(0x4000000000404000), CONST64(0x4000004000404000), CONST64(0x4000000000404040), CONST64(0x4000004000404040),
    CONST64(0x4000400000404000), CONST64(0x4000404000404000), CONST64(0x4000400000404040), CONST64(0x4000404000404040),
    CONST64(0x4040000000400000), CONST64(0x4040004000400000), CONST64(0x4040000000400040), CONST64(0x4040004000400040),
    CONST64(0x4040400000400000), CONST64(0x4040404000400000), CONST64(0x4040400000400040), CONST64(0x4040404000400040),
    CONST64(0x4040000000404000), CONST64(0x4040004000404000), CONST64(0x4040000000404040), CONST64(0x4040004000404040),
    CONST64(0x4040400000404000), CONST64(0x4040404000404000), CONST64(0x4040400000404040), CONST64(0x4040404000404040),
    CONST64(0x0000000040000000), CONST64(0x0000004040000000), CONST64(0x0000000040000040), CONST64(0x0000004040000040),
    CONST64(0x0000400040000000), CONST64(0x0000404040000000), CONST64(0x0000400040000040), CONST64(0x0000404040000040),
    CONST64(0x0000000040004000), CONST64(0x0000004040004000), CONST64(0x0000000040004040), CONST64(0x0000004040004040),
    CONST64(0x0000400040004000), CONST64(0x0000404040004000), CONST64(0x0000400040004040), CONST64(0x0000404040004040),
    CONST64(0x0040000040000000), CONST64(0x0040004040000000), CONST64(0x0040000040000040), CONST64(0x0040004040000040),
    CONST64(0x0040400040000000), CONST64(0x0040404040000000), CONST64(0x0040400040000040), CONST64(0x0040404040000040),
    CONST64(0x0040000040004000), CONST64(0x0040004040004000), CONST64(0x0040000040004040), CONST64(0x0040004040004040),
    CONST64(0x0040400040004000), CONST64(0x0040404040004000), CONST64(0x0040400040004040), CONST64(0x0040404040004040),
    CONST64(0x0000000040400000), CONST64(0x0000004040400000), CONST64(0x0000000040400040), CONST64(0x0000004040400040),
    CONST64(0x0000400040400000), CONST64(0x0000404040400000), CONST64(0x0000400040400040), CONST64(0x0000404040400040),
    CONST64(0x0000000040404000), CONST64(0x0000004040404000), CONST64(0x0000000040404040), CONST64(0x0000004040404040),
    CONST64(0x0000400040404000), CONST64(0x0000404040404000), CONST64(0x0000400040404040), CONST64(0x0000404040404040),
    CONST64(0x0040000040400000), CONST64(0x0040004040400000), CONST64(0x0040000040400040), CONST64(0x0040004040400040),
    CONST64(0x0040400040400000), CONST64(0x0040404040400000), CONST64(0x0040400040400040), CONST64(0x0040404040400040),
    CONST64(0x0040000040404000), CONST64(0x0040004040404000), CONST64(0x0040000040404040), CONST64(0x0040004040404040),
    CONST64(0x0040400040404000), CONST64(0x0040404040404000), CONST64(0x0040400040404040), CONST64(0x0040404040404040),
    CONST64(0x4000000040000000), CONST64(0x4000004040000000), CONST64(0x4000000040000040), CONST64(0x4000004040000040),
    CONST64(0x4000400040000000), CONST64(0x4000404040000000), CONST64(0x4000400040000040), CONST64(0x4000404040000040),
    CONST64(0x4000000040004000), CONST64(0x4000004040004000), CONST64(0x4000000040004040), CONST64(0x4000004040004040),
    CONST64(0x4000400040004000), CONST64(0x4000404040004000), CONST64(0x4000400040004040), CONST64(0x4000404040004040),
    CONST64(0x4040000040000000), CONST64(0x4040004040000000), CONST64(0x4040000040000040), CONST64(0x4040004040000040),
    CONST64(0x4040400040000000), CONST64(0x4040404040000000), CONST64(0x4040400040000040), CONST64(0x4040404040000040),
    CONST64(0x4040000040004000), CONST64(0x4040004040004000), CONST64(0x4040000040004040), CONST64(0x4040004040004040),
    CONST64(0x4040400040004000), CONST64(0x4040404040004000), CONST64(0x4040400040004040), CONST64(0x4040404040004040),
    CONST64(0x4000000040400000), CONST64(0x4000004040400000), CONST64(0x4000000040400040), CONST64(0x4000004040400040),
    CONST64(0x4000400040400000), CONST64(0x4000404040400000), CONST64(0x4000400040400040), CONST64(0x4000404040400040),
    CONST64(0x4000000040404000), CONST64(0x4000004040404000), CONST64(0x4000000040404040), CONST64(0x4000004040404040),
    CONST64(0x4000400040404000), CONST64(0x4000404040404000), CONST64(0x4000400040404040), CONST64(0x4000404040404040),
    CONST64(0x4040000040400000), CONST64(0x4040004040400000), CONST64(0x4040000040400040), CONST64(0x4040004040400040),
    CONST64(0x4040400040400000), CONST64(0x4040404040400000), CONST64(0x4040400040400040), CONST64(0x4040404040400040),
    CONST64(0x4040000040404000), CONST64(0x4040004040404000), CONST64(0x4040000040404040), CONST64(0x4040004040404040),
    CONST64(0x4040400040404000), CONST64(0x4040404040404000), CONST64(0x4040400040404040), CONST64(0x4040404040404040)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000002000000000), CONST64(0x0000000000000020), CONST64(0x0000002000000020),
    CONST64(0x0000200000000000), CONST64(0x0000202000000000), CONST64(0x0000200000000020), CONST64(0x0000202000000020),
    CONST64(0x0000000000002000), CONST64(0x0000002000002000), CONST64(0x0000000000002020), CONST64(0x0000002000002020),
    CONST64(0x0000200000002000), CONST64(0x0000202000002000), CONST64(0x0000200000002020), CONST64(0x0000202000002020),
    CONST64(0x0020000000000000), CONST64(0x0020002000000000), CONST64(0x0020000000000020), CONST64(0x0020002000000020),
    CONST64(0x0020200000000000), CONST64(0x0020202000000000), CONST64(0x0020200000000020), CONST64(0x0020202000000020),
    CONST64(0x0020000000002000), CONST64(0x0020002000002000), CONST64(0x0020000000002020), CONST64(0x0020002000002020),
    CONST64(0x0020200000002000), CONST64(0x0020202000002000), CONST64(0x0020200000002020), CONST64(0x0020202000002020),
    CONST64(0x0000000000200000), CONST64(0x0000002000200000), CONST64(0x0000000000200020), CONST64(0x0000002000200020),
    CONST64(0x0000200000200000), CONST64(0x0000202000200000), CONST64(0x0000200000200020), CONST64(0x0000202000200020),
    CONST64(0x0000000000202000), CONST64(0x0000002000202000), CONST64(0x0000000000202020), CONST64(0x0000002000202020),
    CONST64(0x0000200000202000), CONST64(0x0000202000202000), CONST64(0x0000200000202020), CONST64(0x0000202000202020),
    CONST64(0x0020000000200000), CONST64(0x0020002000200000), CONST64(0x0020000000200020), CONST64(0x0020002000200020),
    CONST64(0x0020200000200000), CONST64(0x0020202000200000), CONST64(0x0020200000200020), CONST64(0x0020202000200020),
    CONST64(0x0020000000202000), CONST64(0x0020002000202000), CONST64(0x0020000000202020), CONST64(0x0020002000202020),
    CONST64(0x0020200000202000), CONST64(0x0020202000202000), CONST64(0x0020200000202020), CONST64(0x0020202000202020),
    CONST64(0x2000000000000000), CONST64(0x2000002000000000), CONST64(0x2000000000000020), CONST64(0x2000002000000020),
    CONST64(0x2000200000000000), CONST64(0x2000202000000000), CONST64(0x2000200000000020), CONST64(0x2000202000000020),
    CONST64(0x2000000000002000), CONST64(0x2000002000002000), CONST64(0x2000000000002020), CONST64(0x2000002000002020),
    CONST64(0x2000200000002000), CONST64(0x2000202000002000), CONST64(0x2000200000002020), CONST64(0x2000202000002020),
    CONST64(0x2020000000000000), CONST64(0x2020002000000000), CONST64(0x2020000000000020), CONST64(0x2020002000000020),
    CONST64(0x2020200000000000), CONST64(0x2020202000000000), CONST64(0x2020200000000020), CONST64(0x2020202000000020),
    CONST64(0x2020000000002000), CONST64(0x2020002000002000), CONST64(0x2020000000002020), CONST64(0x2020002000002020),
    CONST64(0x2020200000002000), CONST64(0x2020202000002000), CONST64(0x2020200000002020), CONST64(0x2020202000002020),
    CONST64(0x2000000000200000), CONST64(0x2000002000200000), CONST64(0x2000000000200020), CONST64(0x2000002000200020),
    CONST64(0x2000200000200000), CONST64(0x2000202000200000), CONST64(0x2000200000200020), CONST64(0x2000202000200020),
    CONST64(0x2000000000202000), CONST64(0x2000002000202000), CONST64(0x2000000000202020), CONST64(0x2000002000202020),
    CONST64(0x2000200000202000), CONST64(0x2000202000202000), CONST64(0x2000200000202020), CONST64(0x2000202000202020),
    CONST64(0x2020000000200000), CONST64(0x2020002000200000), CONST64(0x2020000000200020), CONST64(0x2020002000200020),
    CONST64(0x2020200000200000), CONST64(0x2020202000200000), CONST64(0x2020200000200020), CONST64(0x2020202000200020),
    CONST64(0x2020000000202000), CONST64(0x2020002000202000), CONST64(0x2020000000202020), CONST64(0x2020002000202020),
    CONST64(0x2020200000202000), CONST64(0x2020202000202000), CONST64(0x2020200000202020), CONST64(0x2020202000202020),
    CONST64(0x0000000020000000), CONST64(0x0000002020000000), CONST64(0x0000000020000020), CONST64(0x0000002020000020),
    CONST64(0x0000200020000000), CONST64(0x0000202020000000), CONST64(0x0000200020000020), CONST64(0x0000202020000020),
    CONST64(0x0000000020002000), CONST64(0x0000002020002000), CONST64(0x0000000020002020), CONST64(0x0000002020002020),
    CONST64(0x0000200020002000), CONST64(0x0000202020002000), CONST64(0x0000200020002020), CONST64(0x0000202020002020),
    CONST64(0x0020000020000000), CONST64(0x0020002020000000), CONST64(0x0020000020000020), CONST64(0x0020002020000020),
    CONST64(0x0020200020000000), CONST64(0x0020202020000000), CONST64(0x0020200020000020), CONST64(0x0020202020000020),
    CONST64(0x0020000020002000), CONST64(0x0020002020002000), CONST64(0x0020000020002020), CONST64(0x0020002020002020),
    CONST64(0x0020200020002000), CONST64(0x0020202020002000), CONST64(0x0020200020002020), CONST64(0x0020202020002020),
    CONST64(0x0000000020200000), CONST64(0x0000002020200000), CONST64(0x0000000020200020), CONST64(0x0000002020200020),
    CONST64(0x0000200020200000), CONST64(0x0000202020200000), CONST64(0x0000200020200020), CONST64(0x0000202020200020),
    CONST64(0x0000000020202000), CONST64(0x0000002020202000), CONST64(0x0000000020202020), CONST64(0x0000002020202020),
    CONST64(0x0000200020202000), CONST64(0x0000202020202000), CONST64(0x0000200020202020), CONST64(0x0000202020202020),
    CONST64(0x0020000020200000), CONST64(0x0020002020200000), CONST64(0x0020000020200020), CONST64(0x0020002020200020),
    CONST64(0x0020200020200000), CONST64(0x0020202020200000), CONST64(0x0020200020200020), CONST64(0x0020202020200020),
    CONST64(0x0020000020202000), CONST64(0x0020002020202000), CONST64(0x0020000020202020), CONST64(0x0020002020202020),
    CONST64(0x0020200020202000), CONST64(0x0020202020202000), CONST64(0x0020200020202020), CONST64(0x0020202020202020),
    CONST64(0x2000000020000000), CONST64(0x2000002020000000), CONST64(0x2000000020000020), CONST64(0x2000002020000020),
    CONST64(0x2000200020000000), CONST64(0x2000202020000000), CONST64(0x2000200020000020), CONST64(0x2000202020000020),
    CONST64(0x2000000020002000), CONST64(0x2000002020002000), CONST64(0x2000000020002020), CONST64(0x2000002020002020),
    CONST64(0x2000200020002000), CONST64(0x2000202020002000), CONST64(0x2000200020002020), CONST64(0x2000202020002020),
    CONST64(0x2020000020000000), CONST64(0x2020002020000000), CONST64(0x2020000020000020), CONST64(0x2020002020000020),
    CONST64(0x2020200020000000), CONST64(0x2020202020000000), CONST64(0x2020200020000020), CONST64(0x2020202020000020),
    CONST64(0x2020000020002000), CONST64(0x2020002020002000), CONST64(0x2020000020002020), CONST64(0x2020002020002020),
    CONST64(0x2020200020002000), CONST64(0x2020202020002000), CONST64(0x2020200020002020), CONST64(0x2020202020002020),
    CONST64(0x2000000020200000), CONST64(0x2000002020200000), CONST64(0x2000000020200020), CONST64(0x2000002020200020),
    CONST64(0x2000200020200000), CONST64(0x2000202020200000), CONST64(0x2000200020200020), CONST64(0x2000202020200020),
    CONST64(0x2000000020202000), CONST64(0x2000002020202000), CONST64(0x2000000020202020), CONST64(0x2000002020202020),
    CONST64(0x2000200020202000), CONST64(0x2000202020202000), CONST64(0x2000200020202020), CONST64(0x2000202020202020),
    CONST64(0x2020000020200000), CONST64(0x2020002020200000), CONST64(0x2020000020200020), CONST64(0x2020002020200020),
    CONST64(0x2020200020200000), CONST64(0x2020202020200000), CONST64(0x2020200020200020), CONST64(0x2020202020200020),
    CONST64(0x2020000020202000), CONST64(0x2020002020202000), CONST64(0x2020000020202020), CONST64(0x2020002020202020),
    CONST64(0x2020200020202000), CONST64(0x2020202020202000), CONST64(0x2020200020202020), CONST64(0x2020202020202020)
    }};

LQ_ALIGN(128) static const uint64_t des_fp[8][256] = {

    {CONST64(0x0000000000000000), CONST64(0x0000008000000000), CONST64(0x0000000002000000), CONST64(0x0000008002000000),
    CONST64(0x0000000000020000), CONST64(0x0000008000020000), CONST64(0x0000000002020000), CONST64(0x0000008002020000),
    CONST64(0x0000000000000200), CONST64(0x0000008000000200), CONST64(0x0000000002000200), CONST64(0x0000008002000200),
    CONST64(0x0000000000020200), CONST64(0x0000008000020200), CONST64(0x0000000002020200), CONST64(0x0000008002020200),
    CONST64(0x0000000000000002), CONST64(0x0000008000000002), CONST64(0x0000000002000002), CONST64(0x0000008002000002),
    CONST64(0x0000000000020002), CONST64(0x0000008000020002), CONST64(0x0000000002020002), CONST64(0x0000008002020002),
    CONST64(0x0000000000000202), CONST64(0x0000008000000202), CONST64(0x0000000002000202), CONST64(0x0000008002000202),
    CONST64(0x0000000000020202), CONST64(0x0000008000020202), CONST64(0x0000000002020202), CONST64(0x0000008002020202),
    CONST64(0x0200000000000000), CONST64(0x0200008000000000), CONST64(0x0200000002000000), CONST64(0x0200008002000000),
    CONST64(0x0200000000020000), CONST64(0x0200008000020000), CONST64(0x0200000002020000), CONST64(0x0200008002020000),
    CONST64(0x0200000000000200), CONST64(0x0200008000000200), CONST64(0x0200000002000200), CONST64(0x0200008002000200),
    CONST64(0x0200000000020200), CONST64(0x0200008000020200), CONST64(0x0200000002020200), CONST64(0x0200008002020200),
    CONST64(0x0200000000000002), CONST64(0x0200008000000002), CONST64(0x0200000002000002), CONST64(0x0200008002000002),
    CONST64(0x0200000000020002), CONST64(0x0200008000020002), CONST64(0x0200000002020002), CONST64(0x0200008002020002),
    CONST64(0x0200000000000202), CONST64(0x0200008000000202), CONST64(0x0200000002000202), CONST64(0x0200008002000202),
    CONST64(0x0200000000020202), CONST64(0x0200008000020202), CONST64(0x0200000002020202), CONST64(0x0200008002020202),
    CONST64(0x0002000000000000), CONST64(0x0002008000000000), CONST64(0x0002000002000000), CONST64(0x0002008002000000),
    CONST64(0x0002000000020000), CONST64(0x0002008000020000), CONST64(0x0002000002020000), CONST64(0x0002008002020000),
    CONST64(0x0002000000000200), CONST64(0x0002008000000200), CONST64(0x0002000002000200), CONST64(0x0002008002000200),
    CONST64(0x0002000000020200), CONST64(0x0002008000020200), CONST64(0x0002000002020200), CONST64(0x0002008002020200),
    CONST64(0x0002000000000002), CONST64(0x0002008000000002), CONST64(0x0002000002000002), CONST64(0x0002008002000002),
    CONST64(0x0002000000020002), CONST64(0x0002008000020002), CONST64(0x0002000002020002), CONST64(0x0002008002020002),
    CONST64(0x0002000000000202), CONST64(0x0002008000000202), CONST64(0x0002000002000202), CONST64(0x0002008002000202),
    CONST64(0x0002000000020202), CONST64(0x0002008000020202), CONST64(0x0002000002020202), CONST64(0x0002008002020202),
    CONST64(0x0202000000000000), CONST64(0x0202008000000000), CONST64(0x0202000002000000), CONST64(0x0202008002000000),
    CONST64(0x0202000000020000), CONST64(0x0202008000020000), CONST64(0x0202000002020000), CONST64(0x0202008002020000),
    CONST64(0x0202000000000200), CONST64(0x0202008000000200), CONST64(0x0202000002000200), CONST64(0x0202008002000200),
    CONST64(0x0202000000020200), CONST64(0x0202008000020200), CONST64(0x0202000002020200), CONST64(0x0202008002020200),
    CONST64(0x0202000000000002), CONST64(0x0202008000000002), CONST64(0x0202000002000002), CONST64(0x0202008002000002),
    CONST64(0x0202000000020002), CONST64(0x0202008000020002), CONST64(0x0202000002020002), CONST64(0x0202008002020002),
    CONST64(0x0202000000000202), CONST64(0x0202008000000202), CONST64(0x0202000002000202), CONST64(0x0202008002000202),
    CONST64(0x0202000000020202), CONST64(0x0202008000020202), CONST64(0x0202000002020202), CONST64(0x0202008002020202),
    CONST64(0x0000020000000000), CONST64(0x0000028000000000), CONST64(0x0000020002000000), CONST64(0x0000028002000000),
    CONST64(0x0000020000020000), CONST64(0x0000028000020000), CONST64(0x0000020002020000), CONST64(0x0000028002020000),
    CONST64(0x0000020000000200), CONST64(0x0000028000000200), CONST64(0x0000020002000200), CONST64(0x0000028002000200),
    CONST64(0x0000020000020200), CONST64(0x0000028000020200), CONST64(0x0000020002020200), CONST64(0x0000028002020200),
    CONST64(0x0000020000000002), CONST64(0x0000028000000002), CONST64(0x0000020002000002), CONST64(0x0000028002000002),
    CONST64(0x0000020000020002), CONST64(0x0000028000020002), CONST64(0x0000020002020002), CONST64(0x0000028002020002),
    CONST64(0x0000020000000202), CONST64(0x0000028000000202), CONST64(0x0000020002000202), CONST64(0x0000028002000202),
    CONST64(0x0000020000020202), CONST64(0x0000028000020202), CONST64(0x0000020002020202), CONST64(0x0000028002020202),
    CONST64(0x0200020000000000), CONST64(0x0200028000000000), CONST64(0x0200020002000000), CONST64(0x0200028002000000),
    CONST64(0x0200020000020000), CONST64(0x0200028000020000), CONST64(0x0200020002020000), CONST64(0x0200028002020000),
    CONST64(0x0200020000000200), CONST64(0x0200028000000200), CONST64(0x0200020002000200), CONST64(0x0200028002000200),
    CONST64(0x0200020000020200), CONST64(0x0200028000020200), CONST64(0x0200020002020200), CONST64(0x0200028002020200),
    CONST64(0x0200020000000002), CONST64(0x0200028000000002), CONST64(0x0200020002000002), CONST64(0x0200028002000002),
    CONST64(0x0200020000020002), CONST64(0x0200028000020002), CONST64(0x0200020002020002), CONST64(0x0200028002020002),
    CONST64(0x0200020000000202), CONST64(0x0200028000000202), CONST64(0x0200020002000202), CONST64(0x0200028002000202),
    CONST64(0x0200020000020202), CONST64(0x0200028000020202), CONST64(0x0200020002020202), CONST64(0x0200028002020202),
    CONST64(0x0002020000000000), CONST64(0x0002028000000000), CONST64(0x0002020002000000), CONST64(0x0002028002000000),
    CONST64(0x0002020000020000), CONST64(0x0002028000020000), CONST64(0x0002020002020000), CONST64(0x0002028002020000),
    CONST64(0x0002020000000200), CONST64(0x0002028000000200), CONST64(0x0002020002000200), CONST64(0x0002028002000200),
    CONST64(0x0002020000020200), CONST64(0x0002028000020200), CONST64(0x0002020002020200), CONST64(0x0002028002020200),
    CONST64(0x0002020000000002), CONST64(0x0002028000000002), CONST64(0x0002020002000002), CONST64(0x0002028002000002),
    CONST64(0x0002020000020002), CONST64(0x0002028000020002), CONST64(0x0002020002020002), CONST64(0x0002028002020002),
    CONST64(0x0002020000000202), CONST64(0x0002028000000202), CONST64(0x0002020002000202), CONST64(0x0002028002000202),
    CONST64(0x0002020000020202), CONST64(0x0002028000020202), CONST64(0x0002020002020202), CONST64(0x0002028002020202),
    CONST64(0x0202020000000000), CONST64(0x0202028000000000), CONST64(0x0202020002000000), CONST64(0x0202028002000000),
    CONST64(0x0202020000020000), CONST64(0x0202028000020000), CONST64(0x0202020002020000), CONST64(0x0202028002020000),
    CONST64(0x0202020000000200), CONST64(0x0202028000000200), CONST64(0x0202020002000200), CONST64(0x0202028002000200),
    CONST64(0x0202020000020200), CONST64(0x0202028000020200), CONST64(0x0202020002020200), CONST64(0x0202028002020200),
    CONST64(0x0202020000000002), CONST64(0x0202028000000002), CONST64(0x0202020002000002), CONST64(0x0202028002000002),
    CONST64(0x0202020000020002), CONST64(0x0202028000020002), CONST64(0x0202020002020002), CONST64(0x0202028002020002),
    CONST64(0x0202020000000202), CONST64(0x0202028000000202), CONST64(0x0202020002000202), CONST64(0x0202028002000202),
    CONST64(0x0202020000020202), CONST64(0x0202028000020202), CONST64(0x0202020002020202), CONST64(0x0202028002020202)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000200000000), CONST64(0x0000000008000000), CONST64(0x0000000208000000),
    CONST64(0x0000000000080000), CONST64(0x0000000200080000), CONST64(0x0000000008080000), CONST64(0x0000000208080000),
    CONST64(0x0000000000000800), CONST64(0x0000000200000800), CONST64(0x0000000008000800), CONST64(0x0000000208000800),
    CONST64(0x0000000000080800), CONST64(0x0000000200080800), CONST64(0x0000000008080800), CONST64(0x0000000208080800),
    CONST64(0x0000000000000008), CONST64(0x0000000200000008), CONST64(0x0000000008000008), CONST64(0x0000000208000008),
    CONST64(0x0000000000080008), CONST64(0x0000000200080008), CONST64(0x0000000008080008), CONST64(0x0000000208080008),
    CONST64(0x0000000000000808), CONST64(0x0000000200000808), CONST64(0x0000000008000808), CONST64(0x0000000208000808),
    CONST64(0x0000000000080808), CONST64(0x0000000200080808), CONST64(0x0000000008080808), CONST64(0x0000000208080808),
    CONST64(0x0800000000000000), CONST64(0x0800000200000000), CONST64(0x0800000008000000), CONST64(0x0800000208000000),
    CONST64(0x0800000000080000), CONST64(0x0800000200080000), CONST64(0x0800000008080000), CONST64(0x0800000208080000),
    CONST64(0x0800000000000800), CONST64(0x0800000200000800), CONST64(0x0800000008000800), CONST64(0x0800000208000800),
    CONST64(0x0800000000080800), CONST64(0x0800000200080800), CONST64(0x0800000008080800), CONST64(0x0800000208080800),
    CONST64(0x0800000000000008), CONST64(0x0800000200000008), CONST64(0x0800000008000008), CONST64(0x0800000208000008),
    CONST64(0x0800000000080008), CONST64(0x0800000200080008), CONST64(0x0800000008080008), CONST64(0x0800000208080008),
    CONST64(0x0800000000000808), CONST64(0x0800000200000808), CONST64(0x0800000008000808), CONST64(0x0800000208000808),
    CONST64(0x0800000000080808), CONST64(0x0800000200080808), CONST64(0x0800000008080808), CONST64(0x0800000208080808),
    CONST64(0x0008000000000000), CONST64(0x0008000200000000), CONST64(0x0008000008000000), CONST64(0x0008000208000000),
    CONST64(0x0008000000080000), CONST64(0x0008000200080000), CONST64(0x0008000008080000), CONST64(0x0008000208080000),
    CONST64(0x0008000000000800), CONST64(0x0008000200000800), CONST64(0x0008000008000800), CONST64(0x0008000208000800),
    CONST64(0x0008000000080800), CONST64(0x0008000200080800), CONST64(0x0008000008080800), CONST64(0x0008000208080800),
    CONST64(0x0008000000000008), CONST64(0x0008000200000008), CONST64(0x0008000008000008), CONST64(0x0008000208000008),
    CONST64(0x0008000000080008), CONST64(0x0008000200080008), CONST64(0x0008000008080008), CONST64(0x0008000208080008),
    CONST64(0x0008000000000808), CONST64(0x0008000200000808), CONST64(0x0008000008000808), CONST64(0x0008000208000808),
    CONST64(0x0008000000080808), CONST64(0x0008000200080808), CONST64(0x0008000008080808), CONST64(0x0008000208080808),
    CONST64(0x0808000000000000), CONST64(0x0808000200000000), CONST64(0x0808000008000000), CONST64(0x0808000208000000),
    CONST64(0x0808000000080000), CONST64(0x0808000200080000), CONST64(0x0808000008080000), CONST64(0x0808000208080000),
    CONST64(0x0808000000000800), CONST64(0x0808000200000800), CONST64(0x0808000008000800), CONST64(0x0808000208000800),
    CONST64(0x0808000000080800), CONST64(0x0808000200080800), CONST64(0x0808000008080800), CONST64(0x0808000208080800),
    CONST64(0x0808000000000008), CONST64(0x0808000200000008), CONST64(0x0808000008000008), CONST64(0x0808000208000008),
    CONST64(0x0808000000080008), CONST64(0x0808000200080008), CONST64(0x0808000008080008), CONST64(0x0808000208080008),
    CONST64(0x0808000000000808), CONST64(0x0808000200000808), CONST64(0x0808000008000808), CONST64(0x0808000208000808),
    CONST64(0x0808000000080808), CONST64(0x0808000200080808), CONST64(0x0808000008080808), CONST64(0x0808000208080808),
    CONST64(0x0000080000000000), CONST64(0x0000080200000000), CONST64(0x0000080008000000), CONST64(0x0000080208000000),
    CONST64(0x0000080000080000), CONST64(0x0000080200080000), CONST64(0x0000080008080000), CONST64(0x0000080208080000),
    CONST64(0x0000080000000800), CONST64(0x0000080200000800), CONST64(0x0000080008000800), CONST64(0x0000080208000800),
    CONST64(0x0000080000080800), CONST64(0x0000080200080800), CONST64(0x0000080008080800), CONST64(0x0000080208080800),
    CONST64(0x0000080000000008), CONST64(0x0000080200000008), CONST64(0x0000080008000008), CONST64(0x0000080208000008),
    CONST64(0x0000080000080008), CONST64(0x0000080200080008), CONST64(0x0000080008080008), CONST64(0x0000080208080008),
    CONST64(0x0000080000000808), CONST64(0x0000080200000808), CONST64(0x0000080008000808), CONST64(0x0000080208000808),
    CONST64(0x0000080000080808), CONST64(0x0000080200080808), CONST64(0x0000080008080808), CONST64(0x0000080208080808),
    CONST64(0x0800080000000000), CONST64(0x0800080200000000), CONST64(0x0800080008000000), CONST64(0x0800080208000000),
    CONST64(0x0800080000080000), CONST64(0x0800080200080000), CONST64(0x0800080008080000), CONST64(0x0800080208080000),
    CONST64(0x0800080000000800), CONST64(0x0800080200000800), CONST64(0x0800080008000800), CONST64(0x0800080208000800),
    CONST64(0x0800080000080800), CONST64(0x0800080200080800), CONST64(0x0800080008080800), CONST64(0x0800080208080800),
    CONST64(0x0800080000000008), CONST64(0x0800080200000008), CONST64(0x0800080008000008), CONST64(0x0800080208000008),
    CONST64(0x0800080000080008), CONST64(0x0800080200080008), CONST64(0x0800080008080008), CONST64(0x0800080208080008),
    CONST64(0x0800080000000808), CONST64(0x0800080200000808), CONST64(0x0800080008000808), CONST64(0x0800080208000808),
    CONST64(0x0800080000080808), CONST64(0x0800080200080808), CONST64(0x0800080008080808), CONST64(0x0800080208080808),
    CONST64(0x0008080000000000), CONST64(0x0008080200000000), CONST64(0x0008080008000000), CONST64(0x0008080208000000),
    CONST64(0x0008080000080000), CONST64(0x0008080200080000), CONST64(0x0008080008080000), CONST64(0x0008080208080000),
    CONST64(0x0008080000000800), CONST64(0x0008080200000800), CONST64(0x0008080008000800), CONST64(0x0008080208000800),
    CONST64(0x0008080000080800), CONST64(0x0008080200080800), CONST64(0x0008080008080800), CONST64(0x0008080208080800),
    CONST64(0x0008080000000008), CONST64(0x0008080200000008), CONST64(0x0008080008000008), CONST64(0x0008080208000008),
    CONST64(0x0008080000080008), CONST64(0x0008080200080008), CONST64(0x0008080008080008), CONST64(0x0008080208080008),
    CONST64(0x0008080000000808), CONST64(0x0008080200000808), CONST64(0x0008080008000808), CONST64(0x0008080208000808),
    CONST64(0x0008080000080808), CONST64(0x0008080200080808), CONST64(0x0008080008080808), CONST64(0x0008080208080808),
    CONST64(0x0808080000000000), CONST64(0x0808080200000000), CONST64(0x0808080008000000), CONST64(0x0808080208000000),
    CONST64(0x0808080000080000), CONST64(0x0808080200080000), CONST64(0x0808080008080000), CONST64(0x0808080208080000),
    CONST64(0x0808080000000800), CONST64(0x0808080200000800), CONST64(0x0808080008000800), CONST64(0x0808080208000800),
    CONST64(0x0808080000080800), CONST64(0x0808080200080800), CONST64(0x0808080008080800), CONST64(0x0808080208080800),
    CONST64(0x0808080000000008), CONST64(0x0808080200000008), CONST64(0x0808080008000008), CONST64(0x0808080208000008),
    CONST64(0x0808080000080008), CONST64(0x0808080200080008), CONST64(0x0808080008080008), CONST64(0x0808080208080008),
    CONST64(0x0808080000000808), CONST64(0x0808080200000808), CONST64(0x0808080008000808), CONST64(0x0808080208000808),
    CONST64(0x0808080000080808), CONST64(0x0808080200080808), CONST64(0x0808080008080808), CONST64(0x0808080208080808)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000800000000), CONST64(0x0000000020000000), CONST64(0x0000000820000000),
    CONST64(0x0000000000200000), CONST64(0x0000000800200000), CONST64(0x0000000020200000), CONST64(0x0000000820200000),
    CONST64(0x0000000000002000), CONST64(0x0000000800002000), CONST64(0x0000000020002000), CONST64(0x0000000820002000),
    CONST64(0x0000000000202000), CONST64(0x0000000800202000), CONST64(0x0000000020202000), CONST64(0x0000000820202000),
    CONST64(0x0000000000000020), CONST64(0x0000000800000020), CONST64(0x0000000020000020), CONST64(0x0000000820000020),
    CONST64(0x0000000000200020), CONST64(0x0000000800200020), CONST64(0x0000000020200020), CONST64(0x0000000820200020),
    CONST64(0x0000000000002020), CONST64(0x0000000800002020), CONST64(0x0000000020002020), CONST64(0x0000000820002020),
    CONST64(0x0000000000202020), CONST64(0x0000000800202020), CONST64(0x0000000020202020), CONST64(0x0000000820202020),
    CONST64(0x2000000000000000), CONST64(0x2000000800000000), CONST64(0x2000000020000000), CONST64(0x2000000820000000),
    CONST64(0x2000000000200000), CONST64(0x2000000800200000), CONST64(0x2000000020200000), CONST64(0x2000000820200000),
    CONST64(0x2000000000002000), CONST64(0x2000000800002000), CONST64(0x2000000020002000), CONST64(0x2000000820002000),
    CONST64(0x2000000000202000), CONST64(0x2000000800202000), CONST64(0x2000000020202000), CONST64(0x2000000820202000),
    CONST64(0x2000000000000020), CONST64(0x2000000800000020), CONST64(0x2000000020000020), CONST64(0x2000000820000020),
    CONST64(0x2000000000200020), CONST64(0x2000000800200020), CONST64(0x2000000020200020), CONST64(0x2000000820200020),
    CONST64(0x2000000000002020), CONST64(0x2000000800002020), CONST64(0x2000000020002020), CONST64(0x2000000820002020),
    CONST64(0x2000000000202020), CONST64(0x2000000800202020), CONST64(0x2000000020202020), CONST64(0x2000000820202020),
    CONST64(0x0020000000000000), CONST64(0x0020000800000000), CONST64(0x0020000020000000), CONST64(0x0020000820000000),
    CONST64(0x0020000000200000), CONST64(0x0020000800200000), CONST64(0x0020000020200000), CONST64(0x0020000820200000),
    CONST64(0x0020000000002000), CONST64(0x0020000800002000), CONST64(0x0020000020002000), CONST64(0x0020000820002000),
    CONST64(0x0020000000202000), CONST64(0x0020000800202000), CONST64(0x0020000020202000), CONST64(0x0020000820202000),
    CONST64(0x0020000000000020), CONST64(0x0020000800000020), CONST64(0x0020000020000020), CONST64(0x0020000820000020),
    CONST64(0x0020000000200020), CONST64(0x0020000800200020), CONST64(0x0020000020200020), CONST64(0x0020000820200020),
    CONST64(0x0020000000002020), CONST64(0x0020000800002020), CONST64(0x0020000020002020), CONST64(0x0020000820002020),
    CONST64(0x0020000000202020), CONST64(0x0020000800202020), CONST64(0x0020000020202020), CONST64(0x0020000820202020),
    CONST64(0x2020000000000000), CONST64(0x2020000800000000), CONST64(0x2020000020000000), CONST64(0x2020000820000000),
    CONST64(0x2020000000200000), CONST64(0x2020000800200000), CONST64(0x2020000020200000), CONST64(0x2020000820200000),
    CONST64(0x2020000000002000), CONST64(0x2020000800002000), CONST64(0x2020000020002000), CONST64(0x2020000820002000),
    CONST64(0x2020000000202000), CONST64(0x2020000800202000), CONST64(0x2020000020202000), CONST64(0x2020000820202000),
    CONST64(0x2020000000000020), CONST64(0x2020000800000020), CONST64(0x2020000020000020), CONST64(0x2020000820000020),
    CONST64(0x2020000000200020), CONST64(0x2020000800200020), CONST64(0x2020000020200020), CONST64(0x2020000820200020),
    CONST64(0x2020000000002020), CONST64(0x2020000800002020), CONST64(0x2020000020002020), CONST64(0x2020000820002020),
    CONST64(0x2020000000202020), CONST64(0x2020000800202020), CONST64(0x2020000020202020), CONST64(0x2020000820202020),
    CONST64(0x0000200000000000), CONST64(0x0000200800000000), CONST64(0x0000200020000000), CONST64(0x0000200820000000),
    CONST64(0x0000200000200000), CONST64(0x0000200800200000), CONST64(0x0000200020200000), CONST64(0x0000200820200000),
    CONST64(0x0000200000002000), CONST64(0x0000200800002000), CONST64(0x0000200020002000), CONST64(0x0000200820002000),
    CONST64(0x0000200000202000), CONST64(0x0000200800202000), CONST64(0x0000200020202000), CONST64(0x0000200820202000),
    CONST64(0x0000200000000020), CONST64(0x0000200800000020), CONST64(0x0000200020000020), CONST64(0x0000200820000020),
    CONST64(0x0000200000200020), CONST64(0x0000200800200020), CONST64(0x0000200020200020), CONST64(0x0000200820200020),
    CONST64(0x0000200000002020), CONST64(0x0000200800002020), CONST64(0x0000200020002020), CONST64(0x0000200820002020),
    CONST64(0x0000200000202020), CONST64(0x0000200800202020), CONST64(0x0000200020202020), CONST64(0x0000200820202020),
    CONST64(0x2000200000000000), CONST64(0x2000200800000000), CONST64(0x2000200020000000), CONST64(0x2000200820000000),
    CONST64(0x2000200000200000), CONST64(0x2000200800200000), CONST64(0x2000200020200000), CONST64(0x2000200820200000),
    CONST64(0x2000200000002000), CONST64(0x2000200800002000), CONST64(0x2000200020002000), CONST64(0x2000200820002000),
    CONST64(0x2000200000202000), CONST64(0x2000200800202000), CONST64(0x2000200020202000), CONST64(0x2000200820202000),
    CONST64(0x2000200000000020), CONST64(0x2000200800000020), CONST64(0x2000200020000020), CONST64(0x2000200820000020),
    CONST64(0x2000200000200020), CONST64(0x2000200800200020), CONST64(0x2000200020200020), CONST64(0x2000200820200020),
    CONST64(0x2000200000002020), CONST64(0x2000200800002020), CONST64(0x2000200020002020), CONST64(0x2000200820002020),
    CONST64(0x2000200000202020), CONST64(0x2000200800202020), CONST64(0x2000200020202020), CONST64(0x2000200820202020),
    CONST64(0x0020200000000000), CONST64(0x0020200800000000), CONST64(0x0020200020000000), CONST64(0x0020200820000000),
    CONST64(0x0020200000200000), CONST64(0x0020200800200000), CONST64(0x0020200020200000), CONST64(0x0020200820200000),
    CONST64(0x0020200000002000), CONST64(0x0020200800002000), CONST64(0x0020200020002000), CONST64(0x0020200820002000),
    CONST64(0x0020200000202000), CONST64(0x0020200800202000), CONST64(0x0020200020202000), CONST64(0x0020200820202000),
    CONST64(0x0020200000000020), CONST64(0x0020200800000020), CONST64(0x0020200020000020), CONST64(0x0020200820000020),
    CONST64(0x0020200000200020), CONST64(0x0020200800200020), CONST64(0x0020200020200020), CONST64(0x0020200820200020),
    CONST64(0x0020200000002020), CONST64(0x0020200800002020), CONST64(0x0020200020002020), CONST64(0x0020200820002020),
    CONST64(0x0020200000202020), CONST64(0x0020200800202020), CONST64(0x0020200020202020), CONST64(0x0020200820202020),
    CONST64(0x2020200000000000), CONST64(0x2020200800000000), CONST64(0x2020200020000000), CONST64(0x2020200820000000),
    CONST64(0x2020200000200000), CONST64(0x2020200800200000), CONST64(0x2020200020200000), CONST64(0x2020200820200000),
    CONST64(0x2020200000002000), CONST64(0x2020200800002000), CONST64(0x2020200020002000), CONST64(0x2020200820002000),
    CONST64(0x2020200000202000), CONST64(0x2020200800202000), CONST64(0x2020200020202000), CONST64(0x2020200820202000),
    CONST64(0x2020200000000020), CONST64(0x2020200800000020), CONST64(0x2020200020000020), CONST64(0x2020200820000020),
    CONST64(0x2020200000200020), CONST64(0x2020200800200020), CONST64(0x2020200020200020), CONST64(0x2020200820200020),
    CONST64(0x2020200000002020), CONST64(0x2020200800002020), CONST64(0x2020200020002020), CONST64(0x2020200820002020),
    CONST64(0x2020200000202020), CONST64(0x2020200800202020), CONST64(0x2020200020202020), CONST64(0x2020200820202020)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000002000000000), CONST64(0x0000000080000000), CONST64(0x0000002080000000),
    CONST64(0x0000000000800000), CONST64(0x0000002000800000), CONST64(0x0000000080800000), CONST64(0x0000002080800000),
    CONST64(0x0000000000008000), CONST64(0x0000002000008000), CONST64(0x0000000080008000), CONST64(0x0000002080008000),
    CONST64(0x0000000000808000), CONST64(0x0000002000808000), CONST64(0x0000000080808000), CONST64(0x0000002080808000),
    CONST64(0x0000000000000080), CONST64(0x0000002000000080), CONST64(0x0000000080000080), CONST64(0x0000002080000080),
    CONST64(0x0000000000800080), CONST64(0x0000002000800080), CONST64(0x0000000080800080), CONST64(0x0000002080800080),
    CONST64(0x0000000000008080), CONST64(0x0000002000008080), CONST64(0x0000000080008080), CONST64(0x0000002080008080),
    CONST64(0x0000000000808080), CONST64(0x0000002000808080), CONST64(0x0000000080808080), CONST64(0x0000002080808080),
    CONST64(0x8000000000000000), CONST64(0x8000002000000000), CONST64(0x8000000080000000), CONST64(0x8000002080000000),
    CONST64(0x8000000000800000), CONST64(0x8000002000800000), CONST64(0x8000000080800000), CONST64(0x8000002080800000),
    CONST64(0x8000000000008000), CONST64(0x8000002000008000), CONST64(0x8000000080008000), CONST64(0x8000002080008000),
    CONST64(0x8000000000808000), CONST64(0x8000002000808000), CONST64(0x8000000080808000), CONST64(0x8000002080808000),
    CONST64(0x8000000000000080), CONST64(0x8000002000000080), CONST64(0x8000000080000080), CONST64(0x8000002080000080),
    CONST64(0x8000000000800080), CONST64(0x8000002000800080), CONST64(0x8000000080800080), CONST64(0x8000002080800080),
    CONST64(0x8000000000008080), CONST64(0x8000002000008080), CONST64(0x8000000080008080), CONST64(0x8000002080008080),
    CONST64(0x8000000000808080), CONST64(0x8000002000808080), CONST64(0x8000000080808080), CONST64(0x8000002080808080),
    CONST64(0x0080000000000000), CONST64(0x0080002000000000), CONST64(0x0080000080000000), CONST64(0x0080002080000000),
    CONST64(0x0080000000800000), CONST64(0x0080002000800000), CONST64(0x0080000080800000), CONST64(0x0080002080800000),
    CONST64(0x0080000000008000), CONST64(0x0080002000008000), CONST64(0x0080000080008000), CONST64(0x0080002080008000),
    CONST64(0x0080000000808000), CONST64(0x0080002000808000), CONST64(0x0080000080808000), CONST64(0x0080002080808000),
    CONST64(0x0080000000000080), CONST64(0x0080002000000080), CONST64(0x0080000080000080), CONST64(0x0080002080000080),
    CONST64(0x0080000000800080), CONST64(0x0080002000800080), CONST64(0x0080000080800080), CONST64(0x0080002080800080),
    CONST64(0x0080000000008080), CONST64(0x0080002000008080), CONST64(0x0080000080008080), CONST64(0x0080002080008080),
    CONST64(0x0080000000808080), CONST64(0x0080002000808080), CONST64(0x0080000080808080), CONST64(0x0080002080808080),
    CONST64(0x8080000000000000), CONST64(0x8080002000000000), CONST64(0x8080000080000000), CONST64(0x8080002080000000),
    CONST64(0x8080000000800000), CONST64(0x8080002000800000), CONST64(0x8080000080800000), CONST64(0x8080002080800000),
    CONST64(0x8080000000008000), CONST64(0x8080002000008000), CONST64(0x8080000080008000), CONST64(0x8080002080008000),
    CONST64(0x8080000000808000), CONST64(0x8080002000808000), CONST64(0x8080000080808000), CONST64(0x8080002080808000),
    CONST64(0x8080000000000080), CONST64(0x8080002000000080), CONST64(0x8080000080000080), CONST64(0x8080002080000080),
    CONST64(0x8080000000800080), CONST64(0x8080002000800080), CONST64(0x8080000080800080), CONST64(0x8080002080800080),
    CONST64(0x8080000000008080), CONST64(0x8080002000008080), CONST64(0x8080000080008080), CONST64(0x8080002080008080),
    CONST64(0x8080000000808080), CONST64(0x8080002000808080), CONST64(0x8080000080808080), CONST64(0x8080002080808080),
    CONST64(0x0000800000000000), CONST64(0x0000802000000000), CONST64(0x0000800080000000), CONST64(0x0000802080000000),
    CONST64(0x0000800000800000), CONST64(0x0000802000800000), CONST64(0x0000800080800000), CONST64(0x0000802080800000),
    CONST64(0x0000800000008000), CONST64(0x0000802000008000), CONST64(0x0000800080008000), CONST64(0x0000802080008000),
    CONST64(0x0000800000808000), CONST64(0x0000802000808000), CONST64(0x0000800080808000), CONST64(0x0000802080808000),
    CONST64(0x0000800000000080), CONST64(0x0000802000000080), CONST64(0x0000800080000080), CONST64(0x0000802080000080),
    CONST64(0x0000800000800080), CONST64(0x0000802000800080), CONST64(0x0000800080800080), CONST64(0x0000802080800080),
    CONST64(0x0000800000008080), CONST64(0x0000802000008080), CONST64(0x0000800080008080), CONST64(0x0000802080008080),
    CONST64(0x0000800000808080), CONST64(0x0000802000808080), CONST64(0x0000800080808080), CONST64(0x0000802080808080),
    CONST64(0x8000800000000000), CONST64(0x8000802000000000), CONST64(0x8000800080000000), CONST64(0x8000802080000000),
    CONST64(0x8000800000800000), CONST64(0x8000802000800000), CONST64(0x8000800080800000), CONST64(0x8000802080800000),
    CONST64(0x8000800000008000), CONST64(0x8000802000008000), CONST64(0x8000800080008000), CONST64(0x8000802080008000),
    CONST64(0x8000800000808000), CONST64(0x8000802000808000), CONST64(0x8000800080808000), CONST64(0x8000802080808000),
    CONST64(0x8000800000000080), CONST64(0x8000802000000080), CONST64(0x8000800080000080), CONST64(0x8000802080000080),
    CONST64(0x8000800000800080), CONST64(0x8000802000800080), CONST64(0x8000800080800080), CONST64(0x8000802080800080),
    CONST64(0x8000800000008080), CONST64(0x8000802000008080), CONST64(0x8000800080008080), CONST64(0x8000802080008080),
    CONST64(0x8000800000808080), CONST64(0x8000802000808080), CONST64(0x8000800080808080), CONST64(0x8000802080808080),
    CONST64(0x0080800000000000), CONST64(0x0080802000000000), CONST64(0x0080800080000000), CONST64(0x0080802080000000),
    CONST64(0x0080800000800000), CONST64(0x0080802000800000), CONST64(0x0080800080800000), CONST64(0x0080802080800000),
    CONST64(0x0080800000008000), CONST64(0x0080802000008000), CONST64(0x0080800080008000), CONST64(0x0080802080008000),
    CONST64(0x0080800000808000), CONST64(0x0080802000808000), CONST64(0x0080800080808000), CONST64(0x0080802080808000),
    CONST64(0x0080800000000080), CONST64(0x0080802000000080), CONST64(0x0080800080000080), CONST64(0x0080802080000080),
    CONST64(0x0080800000800080), CONST64(0x0080802000800080), CONST64(0x0080800080800080), CONST64(0x0080802080800080),
    CONST64(0x0080800000008080), CONST64(0x0080802000008080), CONST64(0x0080800080008080), CONST64(0x0080802080008080),
    CONST64(0x0080800000808080), CONST64(0x0080802000808080), CONST64(0x0080800080808080), CONST64(0x0080802080808080),
    CONST64(0x8080800000000000), CONST64(0x8080802000000000), CONST64(0x8080800080000000), CONST64(0x8080802080000000),
    CONST64(0x8080800000800000), CONST64(0x8080802000800000), CONST64(0x8080800080800000), CONST64(0x8080802080800000),
    CONST64(0x8080800000008000), CONST64(0x8080802000008000), CONST64(0x8080800080008000), CONST64(0x8080802080008000),
    CONST64(0x8080800000808000), CONST64(0x8080802000808000), CONST64(0x8080800080808000), CONST64(0x8080802080808000),
    CONST64(0x8080800000000080), CONST64(0x8080802000000080), CONST64(0x8080800080000080), CONST64(0x8080802080000080),
    CONST64(0x8080800000800080), CONST64(0x8080802000800080), CONST64(0x8080800080800080), CONST64(0x8080802080800080),
    CONST64(0x8080800000008080), CONST64(0x8080802000008080), CONST64(0x8080800080008080), CONST64(0x8080802080008080),
    CONST64(0x8080800000808080), CONST64(0x8080802000808080), CONST64(0x8080800080808080), CONST64(0x8080802080808080)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000004000000000), CONST64(0x0000000001000000), CONST64(0x0000004001000000),
    CONST64(0x0000000000010000), CONST64(0x0000004000010000), CONST64(0x0000000001010000), CONST64(0x0000004001010000),
    CONST64(0x0000000000000100), CONST64(0x0000004000000100), CONST64(0x0000000001000100), CONST64(0x0000004001000100),
    CONST64(0x0000000000010100), CONST64(0x0000004000010100), CONST64(0x0000000001010100), CONST64(0x0000004001010100),
    CONST64(0x0000000000000001), CONST64(0x0000004000000001), CONST64(0x0000000001000001), CONST64(0x0000004001000001),
    CONST64(0x0000000000010001), CONST64(0x0000004000010001), CONST64(0x0000000001010001), CONST64(0x0000004001010001),
    CONST64(0x0000000000000101), CONST64(0x0000004000000101), CONST64(0x0000000001000101), CONST64(0x0000004001000101),
    CONST64(0x0000000000010101), CONST64(0x0000004000010101), CONST64(0x0000000001010101), CONST64(0x0000004001010101),
    CONST64(0x0100000000000000), CONST64(0x0100004000000000), CONST64(0x0100000001000000), CONST64(0x0100004001000000),
    CONST64(0x0100000000010000), CONST64(0x0100004000010000), CONST64(0x0100000001010000), CONST64(0x0100004001010000),
    CONST64(0x0100000000000100), CONST64(0x0100004000000100), CONST64(0x0100000001000100), CONST64(0x0100004001000100),
    CONST64(0x0100000000010100), CONST64(0x0100004000010100), CONST64(0x0100000001010100), CONST64(0x0100004001010100),
    CONST64(0x0100000000000001), CONST64(0x0100004000000001), CONST64(0x0100000001000001), CONST64(0x0100004001000001),
    CONST64(0x0100000000010001), CONST64(0x0100004000010001), CONST64(0x0100000001010001), CONST64(0x0100004001010001),
    CONST64(0x0100000000000101), CONST64(0x0100004000000101), CONST64(0x0100000001000101), CONST64(0x0100004001000101),
    CONST64(0x0100000000010101), CONST64(0x0100004000010101), CONST64(0x0100000001010101), CONST64(0x0100004001010101),
    CONST64(0x0001000000000000), CONST64(0x0001004000000000), CONST64(0x0001000001000000), CONST64(0x0001004001000000),
    CONST64(0x0001000000010000), CONST64(0x0001004000010000), CONST64(0x0001000001010000), CONST64(0x0001004001010000),
    CONST64(0x0001000000000100), CONST64(0x0001004000000100), CONST64(0x0001000001000100), CONST64(0x0001004001000100),
    CONST64(0x0001000000010100), CONST64(0x0001004000010100), CONST64(0x0001000001010100), CONST64(0x0001004001010100),
    CONST64(0x0001000000000001), CONST64(0x0001004000000001), CONST64(0x0001000001000001), CONST64(0x0001004001000001),
    CONST64(0x0001000000010001), CONST64(0x0001004000010001), CONST64(0x0001000001010001), CONST64(0x0001004001010001),
    CONST64(0x0001000000000101), CONST64(0x0001004000000101), CONST64(0x0001000001000101), CONST64(0x0001004001000101),
    CONST64(0x0001000000010101), CONST64(0x0001004000010101), CONST64(0x0001000001010101), CONST64(0x0001004001010101),
    CONST64(0x0101000000000000), CONST64(0x0101004000000000), CONST64(0x0101000001000000), CONST64(0x0101004001000000),
    CONST64(0x0101000000010000), CONST64(0x0101004000010000), CONST64(0x0101000001010000), CONST64(0x0101004001010000),
    CONST64(0x0101000000000100), CONST64(0x0101004000000100), CONST64(0x0101000001000100), CONST64(0x0101004001000100),
    CONST64(0x0101000000010100), CONST64(0x0101004000010100), CONST64(0x0101000001010100), CONST64(0x0101004001010100),
    CONST64(0x0101000000000001), CONST64(0x0101004000000001), CONST64(0x0101000001000001), CONST64(0x0101004001000001),
    CONST64(0x0101000000010001), CONST64(0x0101004000010001), CONST64(0x0101000001010001), CONST64(0x0101004001010001),
    CONST64(0x0101000000000101), CONST64(0x0101004000000101), CONST64(0x0101000001000101), CONST64(0x0101004001000101),
    CONST64(0x0101000000010101), CONST64(0x0101004000010101), CONST64(0x0101000001010101), CONST64(0x0101004001010101),
    CONST64(0x0000010000000000), CONST64(0x0000014000000000), CONST64(0x0000010001000000), CONST64(0x0000014001000000),
    CONST64(0x0000010000010000), CONST64(0x0000014000010000), CONST64(0x0000010001010000), CONST64(0x0000014001010000),
    CONST64(0x0000010000000100), CONST64(0x0000014000000100), CONST64(0x0000010001000100), CONST64(0x0000014001000100),
    CONST64(0x0000010000010100), CONST64(0x0000014000010100), CONST64(0x0000010001010100), CONST64(0x0000014001010100),
    CONST64(0x0000010000000001), CONST64(0x0000014000000001), CONST64(0x0000010001000001), CONST64(0x0000014001000001),
    CONST64(0x0000010000010001), CONST64(0x0000014000010001), CONST64(0x0000010001010001), CONST64(0x0000014001010001),
    CONST64(0x0000010000000101), CONST64(0x0000014000000101), CONST64(0x0000010001000101), CONST64(0x0000014001000101),
    CONST64(0x0000010000010101), CONST64(0x0000014000010101), CONST64(0x0000010001010101), CONST64(0x0000014001010101),
    CONST64(0x0100010000000000), CONST64(0x0100014000000000), CONST64(0x0100010001000000), CONST64(0x0100014001000000),
    CONST64(0x0100010000010000), CONST64(0x0100014000010000), CONST64(0x0100010001010000), CONST64(0x0100014001010000),
    CONST64(0x0100010000000100), CONST64(0x0100014000000100), CONST64(0x0100010001000100), CONST64(0x0100014001000100),
    CONST64(0x0100010000010100), CONST64(0x0100014000010100), CONST64(0x0100010001010100), CONST64(0x0100014001010100),
    CONST64(0x0100010000000001), CONST64(0x0100014000000001), CONST64(0x0100010001000001), CONST64(0x0100014001000001),
    CONST64(0x0100010000010001), CONST64(0x0100014000010001), CONST64(0x0100010001010001), CONST64(0x0100014001010001),
    CONST64(0x0100010000000101), CONST64(0x0100014000000101), CONST64(0x0100010001000101), CONST64(0x0100014001000101),
    CONST64(0x0100010000010101), CONST64(0x0100014000010101), CONST64(0x0100010001010101), CONST64(0x0100014001010101),
    CONST64(0x0001010000000000), CONST64(0x0001014000000000), CONST64(0x0001010001000000), CONST64(0x0001014001000000),
    CONST64(0x0001010000010000), CONST64(0x0001014000010000), CONST64(0x0001010001010000), CONST64(0x0001014001010000),
    CONST64(0x0001010000000100), CONST64(0x0001014000000100), CONST64(0x0001010001000100), CONST64(0x0001014001000100),
    CONST64(0x0001010000010100), CONST64(0x0001014000010100), CONST64(0x0001010001010100), CONST64(0x0001014001010100),
    CONST64(0x0001010000000001), CONST64(0x0001014000000001), CONST64(0x0001010001000001), CONST64(0x0001014001000001),
    CONST64(0x0001010000010001), CONST64(0x0001014000010001), CONST64(0x0001010001010001), CONST64(0x0001014001010001),
    CONST64(0x0001010000000101), CONST64(0x0001014000000101), CONST64(0x0001010001000101), CONST64(0x0001014001000101),
    CONST64(0x0001010000010101), CONST64(0x0001014000010101), CONST64(0x0001010001010101), CONST64(0x0001014001010101),
    CONST64(0x0101010000000000), CONST64(0x0101014000000000), CONST64(0x0101010001000000), CONST64(0x0101014001000000),
    CONST64(0x0101010000010000), CONST64(0x0101014000010000), CONST64(0x0101010001010000), CONST64(0x0101014001010000),
    CONST64(0x0101010000000100), CONST64(0x0101014000000100), CONST64(0x0101010001000100), CONST64(0x0101014001000100),
    CONST64(0x0101010000010100), CONST64(0x0101014000010100), CONST64(0x0101010001010100), CONST64(0x0101014001010100),
    CONST64(0x0101010000000001), CONST64(0x0101014000000001), CONST64(0x0101010001000001), CONST64(0x0101014001000001),
    CONST64(0x0101010000010001), CONST64(0x0101014000010001), CONST64(0x0101010001010001), CONST64(0x0101014001010001),
    CONST64(0x0101010000000101), CONST64(0x0101014000000101), CONST64(0x0101010001000101), CONST64(0x0101014001000101),
    CONST64(0x0101010000010101), CONST64(0x0101014000010101), CONST64(0x0101010001010101), CONST64(0x0101014001010101)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000100000000), CONST64(0x0000000004000000), CONST64(0x0000000104000000),
    CONST64(0x0000000000040000), CONST64(0x0000000100040000), CONST64(0x0000000004040000), CONST64(0x0000000104040000),
    CONST64(0x0000000000000400), CONST64(0x0000000100000400), CONST64(0x0000000004000400), CONST64(0x0000000104000400),
    CONST64(0x0000000000040400), CONST64(0x0000000100040400), CONST64(0x0000000004040400), CONST64(0x0000000104040400),
    CONST64(0x0000000000000004), CONST64(0x0000000100000004), CONST64(0x0000000004000004), CONST64(0x0000000104000004),
    CONST64(0x0000000000040004), CONST64(0x0000000100040004), CONST64(0x0000000004040004), CONST64(0x0000000104040004),
    CONST64(0x0000000000000404), CONST64(0x0000000100000404), CONST64(0x0000000004000404), CONST64(0x0000000104000404),
    CONST64(0x0000000000040404), CONST64(0x0000000100040404), CONST64(0x0000000004040404), CONST64(0x0000000104040404),
    CONST64(0x0400000000000000), CONST64(0x0400000100000000), CONST64(0x0400000004000000), CONST64(0x0400000104000000),
    CONST64(0x0400000000040000), CONST64(0x0400000100040000), CONST64(0x0400000004040000), CONST64(0x0400000104040000),
    CONST64(0x0400000000000400), CONST64(0x0400000100000400), CONST64(0x0400000004000400), CONST64(0x0400000104000400),
    CONST64(0x0400000000040400), CONST64(0x0400000100040400), CONST64(0x0400000004040400), CONST64(0x0400000104040400),
    CONST64(0x0400000000000004), CONST64(0x0400000100000004), CONST64(0x0400000004000004), CONST64(0x0400000104000004),
    CONST64(0x0400000000040004), CONST64(0x0400000100040004), CONST64(0x0400000004040004), CONST64(0x0400000104040004),
    CONST64(0x0400000000000404), CONST64(0x0400000100000404), CONST64(0x0400000004000404), CONST64(0x0400000104000404),
    CONST64(0x0400000000040404), CONST64(0x0400000100040404), CONST64(0x0400000004040404), CONST64(0x0400000104040404),
    CONST64(0x0004000000000000), CONST64(0x0004000100000000), CONST64(0x0004000004000000), CONST64(0x0004000104000000),
    CONST64(0x0004000000040000), CONST64(0x0004000100040000), CONST64(0x0004000004040000), CONST64(0x0004000104040000),
    CONST64(0x0004000000000400), CONST64(0x0004000100000400), CONST64(0x0004000004000400), CONST64(0x0004000104000400),
    CONST64(0x0004000000040400), CONST64(0x0004000100040400), CONST64(0x0004000004040400), CONST64(0x0004000104040400),
    CONST64(0x0004000000000004), CONST64(0x0004000100000004), CONST64(0x0004000004000004), CONST64(0x0004000104000004),
    CONST64(0x0004000000040004), CONST64(0x0004000100040004), CONST64(0x0004000004040004), CONST64(0x0004000104040004),
    CONST64(0x0004000000000404), CONST64(0x0004000100000404), CONST64(0x0004000004000404), CONST64(0x0004000104000404),
    CONST64(0x0004000000040404), CONST64(0x0004000100040404), CONST64(0x0004000004040404), CONST64(0x0004000104040404),
    CONST64(0x0404000000000000), CONST64(0x0404000100000000), CONST64(0x0404000004000000), CONST64(0x0404000104000000),
    CONST64(0x0404000000040000), CONST64(0x0404000100040000), CONST64(0x0404000004040000), CONST64(0x0404000104040000),
    CONST64(0x0404000000000400), CONST64(0x0404000100000400), CONST64(0x0404000004000400), CONST64(0x0404000104000400),
    CONST64(0x0404000000040400), CONST64(0x0404000100040400), CONST64(0x0404000004040400), CONST64(0x0404000104040400),
    CONST64(0x0404000000000004), CONST64(0x0404000100000004), CONST64(0x0404000004000004), CONST64(0x0404000104000004),
    CONST64(0x0404000000040004), CONST64(0x0404000100040004), CONST64(0x0404000004040004), CONST64(0x0404000104040004),
    CONST64(0x0404000000000404), CONST64(0x0404000100000404), CONST64(0x0404000004000404), CONST64(0x0404000104000404),
    CONST64(0x0404000000040404), CONST64(0x0404000100040404), CONST64(0x0404000004040404), CONST64(0x0404000104040404),
    CONST64(0x0000040000000000), CONST64(0x0000040100000000), CONST64(0x0000040004000000), CONST64(0x0000040104000000),
    CONST64(0x0000040000040000), CONST64(0x0000040100040000), CONST64(0x0000040004040000), CONST64(0x0000040104040000),
    CONST64(0x0000040000000400), CONST64(0x0000040100000400), CONST64(0x0000040004000400), CONST64(0x0000040104000400),
    CONST64(0x0000040000040400), CONST64(0x0000040100040400), CONST64(0x0000040004040400), CONST64(0x0000040104040400),
    CONST64(0x0000040000000004), CONST64(0x0000040100000004), CONST64(0x0000040004000004), CONST64(0x0000040104000004),
    CONST64(0x0000040000040004), CONST64(0x0000040100040004), CONST64(0x0000040004040004), CONST64(0x0000040104040004),
    CONST64(0x0000040000000404), CONST64(0x0000040100000404), CONST64(0x0000040004000404), CONST64(0x0000040104000404),
    CONST64(0x0000040000040404), CONST64(0x0000040100040404), CONST64(0x0000040004040404), CONST64(0x0000040104040404),
    CONST64(0x0400040000000000), CONST64(0x0400040100000000), CONST64(0x0400040004000000), CONST64(0x0400040104000000),
    CONST64(0x0400040000040000), CONST64(0x0400040100040000), CONST64(0x0400040004040000), CONST64(0x0400040104040000),
    CONST64(0x0400040000000400), CONST64(0x0400040100000400), CONST64(0x0400040004000400), CONST64(0x0400040104000400),
    CONST64(0x0400040000040400), CONST64(0x0400040100040400), CONST64(0x0400040004040400), CONST64(0x0400040104040400),
    CONST64(0x0400040000000004), CONST64(0x0400040100000004), CONST64(0x0400040004000004), CONST64(0x0400040104000004),
    CONST64(0x0400040000040004), CONST64(0x0400040100040004), CONST64(0x0400040004040004), CONST64(0x0400040104040004),
    CONST64(0x0400040000000404), CONST64(0x0400040100000404), CONST64(0x0400040004000404), CONST64(0x0400040104000404),
    CONST64(0x0400040000040404), CONST64(0x0400040100040404), CONST64(0x0400040004040404), CONST64(0x0400040104040404),
    CONST64(0x0004040000000000), CONST64(0x0004040100000000), CONST64(0x0004040004000000), CONST64(0x0004040104000000),
    CONST64(0x0004040000040000), CONST64(0x0004040100040000), CONST64(0x0004040004040000), CONST64(0x0004040104040000),
    CONST64(0x0004040000000400), CONST64(0x0004040100000400), CONST64(0x0004040004000400), CONST64(0x0004040104000400),
    CONST64(0x0004040000040400), CONST64(0x0004040100040400), CONST64(0x0004040004040400), CONST64(0x0004040104040400),
    CONST64(0x0004040000000004), CONST64(0x0004040100000004), CONST64(0x0004040004000004), CONST64(0x0004040104000004),
    CONST64(0x0004040000040004), CONST64(0x0004040100040004), CONST64(0x0004040004040004), CONST64(0x0004040104040004),
    CONST64(0x0004040000000404), CONST64(0x0004040100000404), CONST64(0x0004040004000404), CONST64(0x0004040104000404),
    CONST64(0x0004040000040404), CONST64(0x0004040100040404), CONST64(0x0004040004040404), CONST64(0x0004040104040404),
    CONST64(0x0404040000000000), CONST64(0x0404040100000000), CONST64(0x0404040004000000), CONST64(0x0404040104000000),
    CONST64(0x0404040000040000), CONST64(0x0404040100040000), CONST64(0x0404040004040000), CONST64(0x0404040104040000),
    CONST64(0x0404040000000400), CONST64(0x0404040100000400), CONST64(0x0404040004000400), CONST64(0x0404040104000400),
    CONST64(0x0404040000040400), CONST64(0x0404040100040400), CONST64(0x0404040004040400), CONST64(0x0404040104040400),
    CONST64(0x0404040000000004), CONST64(0x0404040100000004), CONST64(0x0404040004000004), CONST64(0x0404040104000004),
    CONST64(0x0404040000040004), CONST64(0x0404040100040004), CONST64(0x0404040004040004), CONST64(0x0404040104040004),
    CONST64(0x0404040000000404), CONST64(0x0404040100000404), CONST64(0x0404040004000404), CONST64(0x0404040104000404),
    CONST64(0x0404040000040404), CONST64(0x0404040100040404), CONST64(0x0404040004040404), CONST64(0x0404040104040404)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000000400000000), CONST64(0x0000000010000000), CONST64(0x0000000410000000),
    CONST64(0x0000000000100000), CONST64(0x0000000400100000), CONST64(0x0000000010100000), CONST64(0x0000000410100000),
    CONST64(0x0000000000001000), CONST64(0x0000000400001000), CONST64(0x0000000010001000), CONST64(0x0000000410001000),
    CONST64(0x0000000000101000), CONST64(0x0000000400101000), CONST64(0x0000000010101000), CONST64(0x0000000410101000),
    CONST64(0x0000000000000010), CONST64(0x0000000400000010), CONST64(0x0000000010000010), CONST64(0x0000000410000010),
    CONST64(0x0000000000100010), CONST64(0x0000000400100010), CONST64(0x0000000010100010), CONST64(0x0000000410100010),
    CONST64(0x0000000000001010), CONST64(0x0000000400001010), CONST64(0x0000000010001010), CONST64(0x0000000410001010),
    CONST64(0x0000000000101010), CONST64(0x0000000400101010), CONST64(0x0000000010101010), CONST64(0x0000000410101010),
    CONST64(0x1000000000000000), CONST64(0x1000000400000000), CONST64(0x1000000010000000), CONST64(0x1000000410000000),
    CONST64(0x1000000000100000), CONST64(0x1000000400100000), CONST64(0x1000000010100000), CONST64(0x1000000410100000),
    CONST64(0x1000000000001000), CONST64(0x1000000400001000), CONST64(0x1000000010001000), CONST64(0x1000000410001000),
    CONST64(0x1000000000101000), CONST64(0x1000000400101000), CONST64(0x1000000010101000), CONST64(0x1000000410101000),
    CONST64(0x1000000000000010), CONST64(0x1000000400000010), CONST64(0x1000000010000010), CONST64(0x1000000410000010),
    CONST64(0x1000000000100010), CONST64(0x1000000400100010), CONST64(0x1000000010100010), CONST64(0x1000000410100010),
    CONST64(0x1000000000001010), CONST64(0x1000000400001010), CONST64(0x1000000010001010), CONST64(0x1000000410001010),
    CONST64(0x1000000000101010), CONST64(0x1000000400101010), CONST64(0x1000000010101010), CONST64(0x1000000410101010),
    CONST64(0x0010000000000000), CONST64(0x0010000400000000), CONST64(0x0010000010000000), CONST64(0x0010000410000000),
    CONST64(0x0010000000100000), CONST64(0x0010000400100000), CONST64(0x0010000010100000), CONST64(0x0010000410100000),
    CONST64(0x0010000000001000), CONST64(0x0010000400001000), CONST64(0x0010000010001000), CONST64(0x0010000410001000),
    CONST64(0x0010000000101000), CONST64(0x0010000400101000), CONST64(0x0010000010101000), CONST64(0x0010000410101000),
    CONST64(0x0010000000000010), CONST64(0x0010000400000010), CONST64(0x0010000010000010), CONST64(0x0010000410000010),
    CONST64(0x0010000000100010), CONST64(0x0010000400100010), CONST64(0x0010000010100010), CONST64(0x0010000410100010),
    CONST64(0x0010000000001010), CONST64(0x0010000400001010), CONST64(0x0010000010001010), CONST64(0x0010000410001010),
    CONST64(0x0010000000101010), CONST64(0x0010000400101010), CONST64(0x0010000010101010), CONST64(0x0010000410101010),
    CONST64(0x1010000000000000), CONST64(0x1010000400000000), CONST64(0x1010000010000000), CONST64(0x1010000410000000),
    CONST64(0x1010000000100000), CONST64(0x1010000400100000), CONST64(0x1010000010100000), CONST64(0x1010000410100000),
    CONST64(0x1010000000001000), CONST64(0x1010000400001000), CONST64(0x1010000010001000), CONST64(0x1010000410001000),
    CONST64(0x1010000000101000), CONST64(0x1010000400101000), CONST64(0x1010000010101000), CONST64(0x1010000410101000),
    CONST64(0x1010000000000010), CONST64(0x1010000400000010), CONST64(0x1010000010000010), CONST64(0x1010000410000010),
    CONST64(0x1010000000100010), CONST64(0x1010000400100010), CONST64(0x1010000010100010), CONST64(0x1010000410100010),
    CONST64(0x1010000000001010), CONST64(0x1010000400001010), CONST64(0x1010000010001010), CONST64(0x1010000410001010),
    CONST64(0x1010000000101010), CONST64(0x1010000400101010), CONST64(0x1010000010101010), CONST64(0x1010000410101010),
    CONST64(0x0000100000000000), CONST64(0x0000100400000000), CONST64(0x0000100010000000), CONST64(0x0000100410000000),
    CONST64(0x0000100000100000), CONST64(0x0000100400100000), CONST64(0x0000100010100000), CONST64(0x0000100410100000),
    CONST64(0x0000100000001000), CONST64(0x0000100400001000), CONST64(0x0000100010001000), CONST64(0x0000100410001000),
    CONST64(0x0000100000101000), CONST64(0x0000100400101000), CONST64(0x0000100010101000), CONST64(0x0000100410101000),
    CONST64(0x0000100000000010), CONST64(0x0000100400000010), CONST64(0x0000100010000010), CONST64(0x0000100410000010),
    CONST64(0x0000100000100010), CONST64(0x0000100400100010), CONST64(0x0000100010100010), CONST64(0x0000100410100010),
    CONST64(0x0000100000001010), CONST64(0x0000100400001010), CONST64(0x0000100010001010), CONST64(0x0000100410001010),
    CONST64(0x0000100000101010), CONST64(0x0000100400101010), CONST64(0x0000100010101010), CONST64(0x0000100410101010),
    CONST64(0x1000100000000000), CONST64(0x1000100400000000), CONST64(0x1000100010000000), CONST64(0x1000100410000000),
    CONST64(0x1000100000100000), CONST64(0x1000100400100000), CONST64(0x1000100010100000), CONST64(0x1000100410100000),
    CONST64(0x1000100000001000), CONST64(0x1000100400001000), CONST64(0x1000100010001000), CONST64(0x1000100410001000),
    CONST64(0x1000100000101000), CONST64(0x1000100400101000), CONST64(0x1000100010101000), CONST64(0x1000100410101000),
    CONST64(0x1000100000000010), CONST64(0x1000100400000010), CONST64(0x1000100010000010), CONST64(0x1000100410000010),
    CONST64(0x1000100000100010), CONST64(0x1000100400100010), CONST64(0x1000100010100010), CONST64(0x1000100410100010),
    CONST64(0x1000100000001010), CONST64(0x1000100400001010), CONST64(0x1000100010001010), CONST64(0x1000100410001010),
    CONST64(0x1000100000101010), CONST64(0x1000100400101010), CONST64(0x1000100010101010), CONST64(0x1000100410101010),
    CONST64(0x0010100000000000), CONST64(0x0010100400000000), CONST64(0x0010100010000000), CONST64(0x0010100410000000),
    CONST64(0x0010100000100000), CONST64(0x0010100400100000), CONST64(0x0010100010100000), CONST64(0x0010100410100000),
    CONST64(0x0010100000001000), CONST64(0x0010100400001000), CONST64(0x0010100010001000), CONST64(0x0010100410001000),
    CONST64(0x0010100000101000), CONST64(0x0010100400101000), CONST64(0x0010100010101000), CONST64(0x0010100410101000),
    CONST64(0x0010100000000010), CONST64(0x0010100400000010), CONST64(0x0010100010000010), CONST64(0x0010100410000010),
    CONST64(0x0010100000100010), CONST64(0x0010100400100010), CONST64(0x0010100010100010), CONST64(0x0010100410100010),
    CONST64(0x0010100000001010), CONST64(0x0010100400001010), CONST64(0x0010100010001010), CONST64(0x0010100410001010),
    CONST64(0x0010100000101010), CONST64(0x0010100400101010), CONST64(0x0010100010101010), CONST64(0x0010100410101010),
    CONST64(0x1010100000000000), CONST64(0x1010100400000000), CONST64(0x1010100010000000), CONST64(0x1010100410000000),
    CONST64(0x1010100000100000), CONST64(0x1010100400100000), CONST64(0x1010100010100000), CONST64(0x1010100410100000),
    CONST64(0x1010100000001000), CONST64(0x1010100400001000), CONST64(0x1010100010001000), CONST64(0x1010100410001000),
    CONST64(0x1010100000101000), CONST64(0x1010100400101000), CONST64(0x1010100010101000), CONST64(0x1010100410101000),
    CONST64(0x1010100000000010), CONST64(0x1010100400000010), CONST64(0x1010100010000010), CONST64(0x1010100410000010),
    CONST64(0x1010100000100010), CONST64(0x1010100400100010), CONST64(0x1010100010100010), CONST64(0x1010100410100010),
    CONST64(0x1010100000001010), CONST64(0x1010100400001010), CONST64(0x1010100010001010), CONST64(0x1010100410001010),
    CONST64(0x1010100000101010), CONST64(0x1010100400101010), CONST64(0x1010100010101010), CONST64(0x1010100410101010)
    },
    {CONST64(0x0000000000000000), CONST64(0x0000001000000000), CONST64(0x0000000040000000), CONST64(0x0000001040000000),
    CONST64(0x0000000000400000), CONST64(0x0000001000400000), CONST64(0x0000000040400000), CONST64(0x0000001040400000),
    CONST64(0x0000000000004000), CONST64(0x0000001000004000), CONST64(0x0000000040004000), CONST64(0x0000001040004000),
    CONST64(0x0000000000404000), CONST64(0x0000001000404000), CONST64(0x0000000040404000), CONST64(0x0000001040404000),
    CONST64(0x0000000000000040), CONST64(0x0000001000000040), CONST64(0x0000000040000040), CONST64(0x0000001040000040),
    CONST64(0x0000000000400040), CONST64(0x0000001000400040), CONST64(0x0000000040400040), CONST64(0x0000001040400040),
    CONST64(0x0000000000004040), CONST64(0x0000001000004040), CONST64(0x0000000040004040), CONST64(0x0000001040004040),
    CONST64(0x0000000000404040), CONST64(0x0000001000404040), CONST64(0x0000000040404040), CONST64(0x0000001040404040),
    CONST64(0x4000000000000000), CONST64(0x4000001000000000), CONST64(0x4000000040000000), CONST64(0x4000001040000000),
    CONST64(0x4000000000400000), CONST64(0x4000001000400000), CONST64(0x4000000040400000), CONST64(0x4000001040400000),
    CONST64(0x4000000000004000), CONST64(0x4000001000004000), CONST64(0x4000000040004000), CONST64(0x4000001040004000),
    CONST64(0x4000000000404000), CONST64(0x4000001000404000), CONST64(0x4000000040404000), CONST64(0x4000001040404000),
    CONST64(0x4000000000000040), CONST64(0x4000001000000040), CONST64(0x4000000040000040), CONST64(0x4000001040000040),
    CONST64(0x4000000000400040), CONST64(0x4000001000400040), CONST64(0x4000000040400040), CONST64(0x4000001040400040),
    CONST64(0x4000000000004040), CONST64(0x4000001000004040), CONST64(0x4000000040004040), CONST64(0x4000001040004040),
    CONST64(0x4000000000404040), CONST64(0x4000001000404040), CONST64(0x4000000040404040), CONST64(0x4000001040404040),
    CONST64(0x0040000000000000), CONST64(0x0040001000000000), CONST64(0x0040000040000000), CONST64(0x0040001040000000),
    CONST64(0x0040000000400000), CONST64(0x0040001000400000), CONST64(0x0040000040400000), CONST64(0x0040001040400000),
    CONST64(0x0040000000004000), CONST64(0x0040001000004000), CONST64(0x0040000040004000), CONST64(0x0040001040004000),
    CONST64(0x0040000000404000), CONST64(0x0040001000404000), CONST64(0x0040000040404000), CONST64(0x0040001040404000),
    CONST64(0x0040000000000040), CONST64(0x0040001000000040), CONST64(0x0040000040000040), CONST64(0x0040001040000040),
    CONST64(0x0040000000400040), CONST64(0x0040001000400040), CONST64(0x0040000040400040), CONST64(0x0040001040400040),
    CONST64(0x0040000000004040), CONST64(0x0040001000004040), CONST64(0x0040000040004040), CONST64(0x0040001040004040),
    CONST64(0x0040000000404040), CONST64(0x0040001000404040), CONST64(0x0040000040404040), CONST64(0x0040001040404040),
    CONST64(0x4040000000000000), CONST64(0x4040001000000000), CONST64(0x4040000040000000), CONST64(0x4040001040000000),
    CONST64(0x4040000000400000), CONST64(0x4040001000400000), CONST64(0x4040000040400000), CONST64(0x4040001040400000),
    CONST64(0x4040000000004000), CONST64(0x4040001000004000), CONST64(0x4040000040004000), CONST64(0x4040001040004000),
    CONST64(0x4040000000404000), CONST64(0x4040001000404000), CONST64(0x4040000040404000), CONST64(0x4040001040404000),
    CONST64(0x4040000000000040), CONST64(0x4040001000000040), CONST64(0x4040000040000040), CONST64(0x4040001040000040),
    CONST64(0x4040000000400040), CONST64(0x4040001000400040), CONST64(0x4040000040400040), CONST64(0x4040001040400040),
    CONST64(0x4040000000004040), CONST64(0x4040001000004040), CONST64(0x4040000040004040), CONST64(0x4040001040004040),
    CONST64(0x4040000000404040), CONST64(0x4040001000404040), CONST64(0x4040000040404040), CONST64(0x4040001040404040),
    CONST64(0x0000400000000000), CONST64(0x0000401000000000), CONST64(0x0000400040000000), CONST64(0x0000401040000000),
    CONST64(0x0000400000400000), CONST64(0x0000401000400000), CONST64(0x0000400040400000), CONST64(0x0000401040400000),
    CONST64(0x0000400000004000), CONST64(0x0000401000004000), CONST64(0x0000400040004000), CONST64(0x0000401040004000),
    CONST64(0x0000400000404000), CONST64(0x0000401000404000), CONST64(0x0000400040404000), CONST64(0x0000401040404000),
    CONST64(0x0000400000000040), CONST64(0x0000401000000040), CONST64(0x0000400040000040), CONST64(0x0000401040000040),
    CONST64(0x0000400000400040), CONST64(0x0000401000400040), CONST64(0x0000400040400040), CONST64(0x0000401040400040),
    CONST64(0x0000400000004040), CONST64(0x0000401000004040), CONST64(0x0000400040004040), CONST64(0x0000401040004040),
    CONST64(0x0000400000404040), CONST64(0x0000401000404040), CONST64(0x0000400040404040), CONST64(0x0000401040404040),
    CONST64(0x4000400000000000), CONST64(0x4000401000000000), CONST64(0x4000400040000000), CONST64(0x4000401040000000),
    CONST64(0x4000400000400000), CONST64(0x4000401000400000), CONST64(0x4000400040400000), CONST64(0x4000401040400000),
    CONST64(0x4000400000004000), CONST64(0x4000401000004000), CONST64(0x4000400040004000), CONST64(0x4000401040004000),
    CONST64(0x4000400000404000), CONST64(0x4000401000404000), CONST64(0x4000400040404000), CONST64(0x4000401040404000),
    CONST64(0x4000400000000040), CONST64(0x4000401000000040), CONST64(0x4000400040000040), CONST64(0x4000401040000040),
    CONST64(0x4000400000400040), CONST64(0x4000401000400040), CONST64(0x4000400040400040), CONST64(0x4000401040400040),
    CONST64(0x4000400000004040), CONST64(0x4000401000004040), CONST64(0x4000400040004040), CONST64(0x4000401040004040),
    CONST64(0x4000400000404040), CONST64(0x4000401000404040), CONST64(0x4000400040404040), CONST64(0x4000401040404040),
    CONST64(0x0040400000000000), CONST64(0x0040401000000000), CONST64(0x0040400040000000), CONST64(0x0040401040000000),
    CONST64(0x0040400000400000), CONST64(0x0040401000400000), CONST64(0x0040400040400000), CONST64(0x0040401040400000),
    CONST64(0x0040400000004000), CONST64(0x0040401000004000), CONST64(0x0040400040004000), CONST64(0x0040401040004000),
    CONST64(0x0040400000404000), CONST64(0x0040401000404000), CONST64(0x0040400040404000), CONST64(0x0040401040404000),
    CONST64(0x0040400000000040), CONST64(0x0040401000000040), CONST64(0x0040400040000040), CONST64(0x0040401040000040),
    CONST64(0x0040400000400040), CONST64(0x0040401000400040), CONST64(0x0040400040400040), CONST64(0x0040401040400040),
    CONST64(0x0040400000004040), CONST64(0x0040401000004040), CONST64(0x0040400040004040), CONST64(0x0040401040004040),
    CONST64(0x0040400000404040), CONST64(0x0040401000404040), CONST64(0x0040400040404040), CONST64(0x0040401040404040),
    CONST64(0x4040400000000000), CONST64(0x4040401000000000), CONST64(0x4040400040000000), CONST64(0x4040401040000000),
    CONST64(0x4040400000400000), CONST64(0x4040401000400000), CONST64(0x4040400040400000), CONST64(0x4040401040400000),
    CONST64(0x4040400000004000), CONST64(0x4040401000004000), CONST64(0x4040400040004000), CONST64(0x4040401040004000),
    CONST64(0x4040400000404000), CONST64(0x4040401000404000), CONST64(0x4040400040404000), CONST64(0x4040401040404000),
    CONST64(0x4040400000000040), CONST64(0x4040401000000040), CONST64(0x4040400040000040), CONST64(0x4040401040000040),
    CONST64(0x4040400000400040), CONST64(0x4040401000400040), CONST64(0x4040400040400040), CONST64(0x4040401040400040),
    CONST64(0x4040400000004040), CONST64(0x4040401000004040), CONST64(0x4040400040004040), CONST64(0x4040401040004040),
    CONST64(0x4040400000404040), CONST64(0x4040401000404040), CONST64(0x4040400040404040), CONST64(0x4040401040404040)
    }};

static void cookey(const uint32_t *raw1, uint32_t *keyout) {
    uint32_t *cook;
    const uint32_t *raw0;
    uint32_t dough[32];
    int i;

    cook = dough;
    for(i = 0; i < 16; i++, raw1++) {
        raw0 = raw1++;
        *cook = (*raw0 & 0x00fc0000L) << 6;
        *cook |= (*raw0 & 0x00000fc0L) << 10;
        *cook |= (*raw1 & 0x00fc0000L) >> 10;
        *cook++ |= (*raw1 & 0x00000fc0L) >> 6;
        *cook = (*raw0 & 0x0003f000L) << 12;
        *cook |= (*raw0 & 0x0000003fL) << 16;
        *cook |= (*raw1 & 0x0003f000L) >> 4;
        *cook++ |= (*raw1 & 0x0000003fL);
    }
    memcpy(keyout, dough, sizeof(dough));
}


static void deskey(const unsigned char *key, short edf, uint32_t *keyout) {
    uint32_t i, j, l, m, n, kn[32];
    unsigned char pc1m[56], pcr[56];

    for(j = 0; j < 56; j++) {
        l = (uint32_t)pc1[j];
        m = l & 7;
        pc1m[j] = (unsigned char)((key[l >> 3U] & bytebit[m]) == bytebit[m] ? 1 : 0);
    }

    for(i = 0; i < 16; i++) {
        if(edf == DE1) {
            m = (15 - i) << 1;
        } else {
            m = i << 1;
        }
        n = m + 1;
        kn[m] = kn[n] = 0L;
        for(j = 0; j < 28; j++) {
            l = j + (uint32_t)totrot[i];
            if(l < 28) {
                pcr[j] = pc1m[l];
            } else {
                pcr[j] = pc1m[l - 28];
            }
        }
        for(/*j = 28*/; j < 56; j++) {
            l = j + (uint32_t)totrot[i];
            if(l < 56) {
                pcr[j] = pc1m[l];
            } else {
                pcr[j] = pc1m[l - 28];
            }
        }
        for(j = 0; j < 24; j++) {
            if((int)pcr[(int)pc2[j]] != 0) {
                kn[m] |= bigbyte[j];
            }
            if((int)pcr[(int)pc2[j + 24]] != 0) {
                kn[n] |= bigbyte[j];
            }
        }
    }

    cookey(kn, keyout);
}


static void desfunc(uint32_t *block, const uint32_t *keys) {
    uint32_t work, right, leftt;
    int cur_round;

    leftt = block[0];
    right = block[1];

    {
        const uint64_t tmp = des_ip[0][byte(leftt, 0)] ^
            des_ip[1][byte(leftt, 1)] ^
            des_ip[2][byte(leftt, 2)] ^
            des_ip[3][byte(leftt, 3)] ^
            des_ip[4][byte(right, 0)] ^
            des_ip[5][byte(right, 1)] ^
            des_ip[6][byte(right, 2)] ^
            des_ip[7][byte(right, 3)];
        leftt = (uint32_t)(tmp >> 32);
        right = (uint32_t)(tmp & 0xFFFFFFFFUL);
    }

    for(cur_round = 0; cur_round < 8; cur_round++) {
        work = RORc(right, 4) ^ *keys++;
        leftt ^= SP7[work & 0x3fL]
            ^ SP5[(work >> 8) & 0x3fL]
            ^ SP3[(work >> 16) & 0x3fL]
            ^ SP1[(work >> 24) & 0x3fL];
        work = right ^ *keys++;
        leftt ^= SP8[work & 0x3fL]
            ^ SP6[(work >> 8) & 0x3fL]
            ^ SP4[(work >> 16) & 0x3fL]
            ^ SP2[(work >> 24) & 0x3fL];

        work = RORc(leftt, 4) ^ *keys++;
        right ^= SP7[work & 0x3fL]
            ^ SP5[(work >> 8) & 0x3fL]
            ^ SP3[(work >> 16) & 0x3fL]
            ^ SP1[(work >> 24) & 0x3fL];
        work = leftt ^ *keys++;
        right ^= SP8[work & 0x3fL]
            ^ SP6[(work >> 8) & 0x3fL]
            ^ SP4[(work >> 16) & 0x3fL]
            ^ SP2[(work >> 24) & 0x3fL];
    }

    {
        const uint64_t tmp = des_fp[0][byte(leftt, 0)] ^
            des_fp[1][byte(leftt, 1)] ^
            des_fp[2][byte(leftt, 2)] ^
            des_fp[3][byte(leftt, 3)] ^
            des_fp[4][byte(right, 0)] ^
            des_fp[5][byte(right, 1)] ^
            des_fp[6][byte(right, 2)] ^
            des_fp[7][byte(right, 3)];
        leftt = (uint32_t)(tmp >> 32);
        right = (uint32_t)(tmp & 0xFFFFFFFFUL);
    }

    block[0] = right;
    block[1] = leftt;
}

static bool LqCryptDes3Init(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags) {
    if(((num_rounds != 0) && (num_rounds != 16)) || (keylen != 24)) {
        lq_errno_set(EINVAL);
        return false;
    }
    skey->methods = &des3_desc;
    deskey(key, EN0, skey->ciphers.des3.ek[0]);
    deskey(key + 8, DE1, skey->ciphers.des3.ek[1]);
    deskey(key + 16, EN0, skey->ciphers.des3.ek[2]);

    deskey(key, DE1, skey->ciphers.des3.dk[2]);
    deskey(key + 8, EN0, skey->ciphers.des3.dk[1]);
    deskey(key + 16, DE1, skey->ciphers.des3.dk[0]);
    return true;
}

static bool LqCryptDesInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags) {
    if(((num_rounds != 0) && (num_rounds != 16)) || (keylen != 8)) {
        lq_errno_set(EINVAL);
        return false;
    }
    skey->methods = &des_desc;
    deskey(key, EN0, skey->ciphers.des.ek);
    deskey(key, DE1, skey->ciphers.des.dk);
    return true;
}

static bool LqCryptDes3Encrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len) {
    uint32_t work[2];
    if(Len % 8) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        LOAD32H(work[0], pt + 0);
        LOAD32H(work[1], pt + 4);
        desfunc(work, skey->ciphers.des3.ek[0]);
        desfunc(work, skey->ciphers.des3.ek[1]);
        desfunc(work, skey->ciphers.des3.ek[2]);
        STORE32H(work[0], ct + 0);
        STORE32H(work[1], ct + 4);
    }
    return true;
}

static bool LqCryptDesEncrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len) {
    uint32_t work[2];
    if(Len % 8) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        LOAD32H(work[0], pt + 0);
        LOAD32H(work[1], pt + 4);
        desfunc(work, skey->ciphers.des.ek);
        STORE32H(work[0], ct + 0);
        STORE32H(work[1], ct + 4);
    }
    return true;
}

static bool LqCryptDes3Decrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len) {
    uint32_t work[2];
    if(Len % 8) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        LOAD32H(work[0], ct + 0);
        LOAD32H(work[1], ct + 4);
        desfunc(work, skey->ciphers.des3.dk[0]);
        desfunc(work, skey->ciphers.des3.dk[1]);
        desfunc(work, skey->ciphers.des3.dk[2]);
        STORE32H(work[0], pt + 0);
        STORE32H(work[1], pt + 4);
    }
    return true;
}

static bool LqCryptDesDecrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len) {
    uint32_t work[2];
    if(Len % 8) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        LOAD32H(work[0], ct + 0);
        LOAD32H(work[1], ct + 4);
        desfunc(work, skey->ciphers.des.dk);
        STORE32H(work[0], pt + 0);
        STORE32H(work[1], pt + 4);
    }
    return true;
}

#define MDS_POLY          0x169
#define RS_POLY           0x14D

LQ_ALIGN(64) static const unsigned char RS[4][8] = {
    {0x01, 0xA4, 0x55, 0x87, 0x5A, 0x58, 0xDB, 0x9E},
    {0xA4, 0x56, 0x82, 0xF3, 0X1E, 0XC6, 0X68, 0XE5},
    {0X02, 0XA1, 0XFC, 0XC1, 0X47, 0XAE, 0X3D, 0X19},
    {0XA4, 0X55, 0X87, 0X5A, 0X58, 0XDB, 0X9E, 0X03}
};

LQ_ALIGN(64) static const unsigned char qbox[2][4][16] = {
    {
        {0x8, 0x1, 0x7, 0xD, 0x6, 0xF, 0x3, 0x2, 0x0, 0xB, 0x5, 0x9, 0xE, 0xC, 0xA, 0x4},
        {0xE, 0XC, 0XB, 0X8, 0X1, 0X2, 0X3, 0X5, 0XF, 0X4, 0XA, 0X6, 0X7, 0X0, 0X9, 0XD},
        {0XB, 0XA, 0X5, 0XE, 0X6, 0XD, 0X9, 0X0, 0XC, 0X8, 0XF, 0X3, 0X2, 0X4, 0X7, 0X1},
        {0XD, 0X7, 0XF, 0X4, 0X1, 0X2, 0X6, 0XE, 0X9, 0XB, 0X3, 0X0, 0X8, 0X5, 0XC, 0XA}
    },
    {
        {0X2, 0X8, 0XB, 0XD, 0XF, 0X7, 0X6, 0XE, 0X3, 0X1, 0X9, 0X4, 0X0, 0XA, 0XC, 0X5},
        {0X1, 0XE, 0X2, 0XB, 0X4, 0XC, 0X3, 0X7, 0X6, 0XD, 0XA, 0X5, 0XF, 0X9, 0X0, 0X8},
        {0X4, 0XC, 0X7, 0X5, 0X1, 0X6, 0X9, 0XA, 0X0, 0XE, 0XD, 0X8, 0X2, 0XB, 0X3, 0XF},
        {0xB, 0X9, 0X5, 0X1, 0XC, 0X3, 0XD, 0XE, 0X6, 0X4, 0X7, 0XF, 0X2, 0X0, 0X8, 0XA}
    }
};

LQ_ALIGN(64) static const unsigned char qord[4][5] = {
    {1, 1, 0, 0, 1},
    {0, 1, 1, 0, 0},
    {0, 0, 0, 1, 1},
    {1, 0, 1, 1, 0}
};

static uint32_t sbox(int i, uint32_t x) {
    unsigned char a0, b0, a1, b1, a2, b2, a3, b3, a4, b4, y;
    a0 = (unsigned char)((x >> 4) & 15);
    b0 = (unsigned char)((x) & 15);
    a1 = a0 ^ b0;
    b1 = (a0 ^ ((b0 << 3) | (b0 >> 1)) ^ (a0 << 3)) & 15;
    a2 = qbox[i][0][(int)a1];
    b2 = qbox[i][1][(int)b1];
    a3 = a2 ^ b2;
    b3 = (a2 ^ ((b2 << 3) | (b2 >> 1)) ^ (a2 << 3)) & 15;
    a4 = qbox[i][2][(int)a3];
    b4 = qbox[i][3][(int)b3];
    y = (b4 << 4) + a4;
    return (uint32_t)y;
}

static uint32_t gf_mult(uint32_t a, uint32_t b, uint32_t p) {
    uint32_t result, B[2], P[2];
    P[1] = p; B[1] = b;
    result = P[0] = B[0] = 0;
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1]; a >>= 1;  B[1] = P[B[1] >> 7] ^ (B[1] << 1);
    result ^= B[a & 1];
    return result;
}

static uint32_t mds_column_mult(unsigned char in, int col) {
    uint32_t x01, x5B, xEF;
    x01 = in;
    x5B = gf_mult(in, 0x5B, MDS_POLY);
    xEF = gf_mult(in, 0xEF, MDS_POLY);
    switch(col) {
        case 0: return (x01 << 0) | (x5B << 8) | (xEF << 16) | (xEF << 24);
        case 1: return (xEF << 0) | (xEF << 8) | (x5B << 16) | (x01 << 24);
        case 2: return (x5B << 0) | (xEF << 8) | (x01 << 16) | (xEF << 24);
        case 3: return (x5B << 0) | (x01 << 8) | (xEF << 16) | (x5B << 24);
    }
    return 0;
}

static void rs_mult(const unsigned char *in, unsigned char *out) {
    int x, y;
    for(x = 0; x < 4; x++) {
        out[x] = 0;
        for(y = 0; y < 8; y++)
            out[x] ^= gf_mult(in[y], RS[x][y], RS_POLY);
    }
}

static void mds_mult(const unsigned char *in, unsigned char *out) {
    int x;
    uint32_t tmp;
    for(tmp = x = 0; x < 4; x++)
        tmp ^= mds_column_mult(in[x], x);
    STORE32L(tmp, out);
}

static void h_func(const unsigned char *in, unsigned char *out, unsigned char *M, const int k, const int offset) {
    int x;
    unsigned char y[4];
    for(x = 0; x < 4; x++) {
        y[x] = in[x];
    }
    switch(k) {
        case 4:
            y[0] = (unsigned char)(sbox(1, (uint32_t)y[0]) ^ M[4 * (6 + offset) + 0]);
            y[1] = (unsigned char)(sbox(0, (uint32_t)y[1]) ^ M[4 * (6 + offset) + 1]);
            y[2] = (unsigned char)(sbox(0, (uint32_t)y[2]) ^ M[4 * (6 + offset) + 2]);
            y[3] = (unsigned char)(sbox(1, (uint32_t)y[3]) ^ M[4 * (6 + offset) + 3]);
        case 3:
            y[0] = (unsigned char)(sbox(1, (uint32_t)y[0]) ^ M[4 * (4 + offset) + 0]);
            y[1] = (unsigned char)(sbox(1, (uint32_t)y[1]) ^ M[4 * (4 + offset) + 1]);
            y[2] = (unsigned char)(sbox(0, (uint32_t)y[2]) ^ M[4 * (4 + offset) + 2]);
            y[3] = (unsigned char)(sbox(0, (uint32_t)y[3]) ^ M[4 * (4 + offset) + 3]);
        case 2:
            y[0] = (unsigned char)(sbox(1, sbox(0, sbox(0, (uint32_t)y[0]) ^ M[4 * (2 + offset) + 0]) ^ M[4 * (0 + offset) + 0]));
            y[1] = (unsigned char)(sbox(0, sbox(0, sbox(1, (uint32_t)y[1]) ^ M[4 * (2 + offset) + 1]) ^ M[4 * (0 + offset) + 1]));
            y[2] = (unsigned char)(sbox(1, sbox(1, sbox(0, (uint32_t)y[2]) ^ M[4 * (2 + offset) + 2]) ^ M[4 * (0 + offset) + 2]));
            y[3] = (unsigned char)(sbox(0, sbox(1, sbox(1, (uint32_t)y[3]) ^ M[4 * (2 + offset) + 3]) ^ M[4 * (0 + offset) + 3]));
    }
    mds_mult(y, out);
}

static uint32_t g_func(uint32_t x, const LqCryptCipher *key) {
    unsigned char g, i, y, z;
    uint32_t res;
    res = 0;
    for(y = 0; y < 4; y++) {
        z = key->ciphers.twofish.start;
        g = sbox(qord[y][z++], (x >> (8 * y)) & 255);
        i = 0;
        while(z != 5) {
            g = g ^ key->ciphers.twofish.S[4 * i++ + y];
            g = sbox(qord[y][z++], g);
        }
        res ^= mds_column_mult(g, y);
    }
    return res;
}

#define g1_func(x, key) g_func(ROLc(x, 8), key)

static bool LqCryptTwofishInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags) {
    int k, x, y;
    unsigned char tmp[4], tmp2[4], M[8 * 4];
    uint32_t A, B;

    if((keylen != 16 && keylen != 24 && keylen != 32) || (num_rounds != 16 && num_rounds != 0)) {
        lq_errno_set(EINVAL);
        return false;
    }
    skey->methods = &twofish_desc;
    k = keylen / 8;
    for(x = 0; x < keylen; x++)
        M[x] = key[x] & 255;
    for(x = 0; x < k; x++)
        rs_mult(M + (x * 8), skey->ciphers.twofish.S + (x * 4));
    for(x = 0; x < 20; x++) {
        for(y = 0; y < 4; y++)
            tmp[y] = x + x;
        h_func(tmp, tmp2, M, k, 0);
        LOAD32L(A, tmp2);
        for(y = 0; y < 4; y++)
            tmp[y] = (unsigned char)(x + x + 1);
        h_func(tmp, tmp2, M, k, 1);
        LOAD32L(B, tmp2);
        B = ROLc(B, 8);
        skey->ciphers.twofish.K[x + x] = (A + B) & 0xFFFFFFFFUL;
        skey->ciphers.twofish.K[x + x + 1] = ROLc(B + B + A, 9);
    }
    switch(k) {
        case 4: skey->ciphers.twofish.start = 0; break;
        case 3: skey->ciphers.twofish.start = 1; break;
        default: skey->ciphers.twofish.start = 2; break;
    }
    return true;
}

static bool LqCryptTwofishEncrypt(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len) {
    register uint32_t a, b, c, d, t1, t2;
    register const uint32_t* k;
    register uint32_t const* const f = skey->ciphers.twofish.K;
    int r;
    if(Len % ((intptr_t)16)) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        LOAD32L(a, &pt[0]); LOAD32L(b, &pt[4]);
        LOAD32L(c, &pt[8]); LOAD32L(d, &pt[12]);
        a ^= f[0]; b ^= f[1];
        c ^= f[2]; d ^= f[3];

        k = f + 8;
        for(r = 8; r != 0; --r) {
            t2 = g1_func(b, skey);
            t1 = g_func(a, skey) + t2;
            c = RORc(c ^ (t1 + k[0]), 1);
            d = ROLc(d, 1) ^ (t2 + t1 + k[1]);
            t2 = g1_func(d, skey);
            t1 = g_func(c, skey) + t2;
            a = RORc(a ^ (t1 + k[2]), 1);
            b = ROLc(b, 1) ^ (t2 + t1 + k[3]);
            k += 4;
        }
        c ^= f[4]; d ^= f[5];
        a ^= f[6]; b ^= f[7];
        STORE32L(c, &ct[0]); STORE32L(d, &ct[4]);
        STORE32L(a, &ct[8]); STORE32L(b, &ct[12]);
    }
    return true;
}

static bool LqCryptTwofishDecrypt(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len) {
    register uint32_t a, b, c, d, t1, t2;
    const uint32_t *k;
    register uint32_t const* const f = skey->ciphers.twofish.K;
    int r;
    if(Len % ((intptr_t)16)) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = pt + Len; pt < m; pt += skey->methods->block_length, ct += skey->methods->block_length) {
        LOAD32L(c, &ct[0]); LOAD32L(d, &ct[4]);
        LOAD32L(a, &ct[8]); LOAD32L(b, &ct[12]);
        a ^= f[6]; b ^= f[7];
        c ^= f[4]; d ^= f[5];
        k = skey->ciphers.twofish.K + 36;
        for(r = 8; r != 0; --r) {
            t2 = g1_func(d, skey);
            t1 = g_func(c, skey) + t2;
            a = ROLc(a, 1) ^ (t1 + k[2]);
            b = RORc(b ^ (t2 + t1 + k[3]), 1);

            t2 = g1_func(b, skey);
            t1 = g_func(a, skey) + t2;
            c = ROLc(c, 1) ^ (t1 + k[0]);
            d = RORc(d ^ (t2 + t1 + k[1]), 1);
            k -= 4;
        }
        a ^= f[0]; b ^= f[1];
        c ^= f[2]; d ^= f[3];
        STORE32L(a, &pt[0]); STORE32L(b, &pt[4]);
        STORE32L(c, &pt[8]); STORE32L(d, &pt[12]);
    }
    return true;
}

#define S0(r0, r1, r2, r3, r4) \
      r3 = r3 ^ r0; r4 = r1; r1 = r1 & r3; \
      r4 = r4 ^ r2; r1 = r1 ^ r0; r0 = r0 | r3; \
      r0 = r0 ^ r4; r4 = r4 ^ r3; r3 = r3 ^ r2; \
      r2 = r2 | r1; r2 = r2 ^ r4; r4 = -1 ^ r4; \
      r4 = r4 | r1; r1 = r1 ^ r3; r1 = r1 ^ r4; \
      r3 = r3 | r0; r1 = r1 ^ r3; r4 = r4 ^ r3; 

#define S1(r0, r1, r2, r3, r4) \
      r1 = -1 ^ r1; r4 = r0; r0 = r0 ^ r1; \
      r4 = r4 | r1; r4 = r4 ^ r3; r3 = r3 & r0; \
      r2 = r2 ^ r4; r3 = r3 ^ r1; r3 = r3 | r2; \
      r0 = r0 ^ r4; r3 = r3 ^ r0; r1 = r1 & r2; \
      r0 = r0 | r1; r1 = r1 ^ r4; r0 = r0 ^ r2; \
      r4 = r4 | r3; r0 = r0 ^ r4; r4 = -1 ^ r4; \
      r1 = r1 ^ r3; r4 = r4 & r2; r1 = -1 ^ r1; \
      r4 = r4 ^ r0; r1 = r1 ^ r4; 

#define S2(r0, r1, r2, r3, r4) \
      r4 = r0; r0 = r0 & r2; r0 = r0 ^ r3; \
      r2 = r2 ^ r1; r2 = r2 ^ r0; r3 = r3 | r4; \
      r3 = r3 ^ r1; r4 = r4 ^ r2; r1 = r3; \
      r3 = r3 | r4; r3 = r3 ^ r0; r0 = r0 & r1; \
      r4 = r4 ^ r0; r1 = r1 ^ r3; r1 = r1 ^ r4; \
      r4 = -1 ^ r4; 

#define S3(r0, r1, r2, r3, r4) \
      r4 = r0; r0 = r0 | r3; r3 = r3 ^ r1; \
      r1 = r1 & r4; r4 = r4 ^ r2; r2 = r2 ^ r3; \
      r3 = r3 & r0; r4 = r4 | r1; r3 = r3 ^ r4; \
      r0 = r0 ^ r1; r4 = r4 & r0; r1 = r1 ^ r3; \
      r4 = r4 ^ r2; r1 = r1 | r0; r1 = r1 ^ r2; \
      r0 = r0 ^ r3; r2 = r1; r1 = r1 | r3; \
      r1 = r1 ^ r0; 

#define S4(r0, r1, r2, r3, r4) \
      r1 = r1 ^ r3; r3 = -1 ^ r3; r2 = r2 ^ r3; \
      r3 = r3 ^ r0; r4 = r1; r1 = r1 & r3; \
      r1 = r1 ^ r2; r4 = r4 ^ r3; r0 = r0 ^ r4; \
      r2 = r2 & r4; r2 = r2 ^ r0; r0 = r0 & r1; \
      r3 = r3 ^ r0; r4 = r4 | r1; r4 = r4 ^ r0; \
      r0 = r0 | r3; r0 = r0 ^ r2; r2 = r2 & r3; \
      r0 = -1 ^ r0; r4 = r4 ^ r2; 

#define S5(r0, r1, r2, r3, r4) \
      r0 = r0 ^ r1; r1 = r1 ^ r3; r3 = -1 ^ r3; \
      r4 = r1;r1 = r1 & r0; r2 = r2 ^ r3; \
      r1 = r1 ^ r2; r2 = r2 | r4; r4 = r4 ^ r3; \
      r3 = r3 & r1; r3 = r3 ^ r0; r4 = r4 ^ r1; \
      r4 = r4 ^ r2; r2 = r2 ^ r0;r0 = r0 & r3; \
      r2 = -1 ^ r2; r0 = r0 ^ r4; r4 = r4 | r3; \
      r2 = r2 ^ r4; 

#define S6(r0, r1, r2, r3, r4) \
      r2 = -1 ^ r2; r4 = r3; r3 = r3 & r0; \
      r0 = r0 ^ r4; r3 = r3 ^ r2; r2 = r2 | r4; \
      r1 = r1 ^ r3; r2 = r2 ^ r0; r0 = r0 | r1; \
      r2 = r2 ^ r1; r4 = r4 ^ r0; r0 = r0 | r3; \
      r0 = r0 ^ r2; r4 = r4 ^ r3; r4 = r4 ^ r0; \
      r3 = -1 ^ r3; r2 = r2 & r4; r2 = r2 ^ r3; 

#define S7(r0, r1, r2, r3, r4) \
      r4 = r2; r2 = r2 & r1; r2 = r2 ^ r3; \
      r3 = r3 & r1; r4 = r4 ^ r2; r2 = r2 ^ r1; \
      r1 = r1 ^ r0; r0 = r0 | r4; \
      r0 = r0 ^ r2; r3 = r3 ^ r1; \
      r2 = r2 ^ r3; r3 = r3 & r0; r3 = r3 ^ r4; \
      r4 = r4 ^ r2; r2 = r2 & r0; r4 = -1 ^ r4; \
      r2 = r2 ^ r4; r4 = r4 & r0; r1 = r1 ^ r3; \
      r4 = r4 ^ r1; 

#define I0(r0, r1, r2, r3, r4) \
      r2 = r2 ^ -1; r4 = r1; r1 = r1 | r0; \
      r4 = r4 ^ -1; r1 = r1 ^ r2; r2 = r2 | r4; \
      r1 = r1 ^ r3; r0 = r0 ^ r4; r2 = r2 ^ r0; \
      r0 = r0 & r3; r4 = r4 ^ r0; r0 = r0 | r1; \
      r0 = r0 ^ r2; r3 = r3 ^ r4; r2 = r2 ^ r1; \
      r3 = r3 ^ r0; r3 = r3 ^ r1; r2 = r2 & r3; \
      r4 = r4 ^ r2; 

#define I1(r0, r1, r2, r3, r4) \
      r4 = r1; r1 = r1 ^ r3; r3 = r3 & r1; \
      r4 = r4 ^ r2; r3 = r3 ^ r0; r0 = r0 | r1; \
      r2 = r2 ^ r3; r0 = r0 ^ r4; r0 = r0 | r2; \
      r1 = r1 ^ r3; r0 = r0 ^ r1; r1 = r1 | r3; \
      r1 = r1 ^ r0; r4 = r4 ^ -1; r4 = r4 ^ r1; \
      r1 = r1 | r0; r1 = r1 ^ r0; r1 = r1 | r4; \
      r3 = r3 ^ r1; 

#define I2(r0, r1, r2, r3, r4) \
      r2 = r2 ^ r3; r3 = r3 ^ r0; r4 = r3; \
      r3 = r3 & r2; r3 = r3 ^ r1; r1 = r1 | r2; \
      r1 = r1 ^ r4; r4 = r4 & r3; r2 = r2 ^ r3; \
      r4 = r4 & r0; r4 = r4 ^ r2; r2 = r2 & r1; \
      r2 = r2 | r0; r3 = r3 ^ -1; r2 = r2 ^ r3; \
      r0 = r0 ^ r3; r0 = r0 & r1; r3 = r3 ^ r4; \
      r3 = r3 ^ r0; 

#define I3(r0, r1, r2, r3, r4) \
      r4 =  r2; r2 = r2 ^ r1; r0 = r0 ^ r2; \
      r4 = r4 & r2; r4 = r4 ^ r0; r0 = r0 & r1; \
      r1 = r1 ^ r3; r3 = r3 | r4; r2 = r2 ^ r3; \
      r0 = r0 ^ r3; r1 = r1 ^ r4; r3 = r3 & r2; \
      r3 = r3 ^ r1; r1 = r1 ^ r0; r1 = r1 | r2; \
      r0 = r0 ^ r3; r1 = r1 ^ r4; r0 = r0 ^ r1; 

#define I4(r0, r1, r2, r3, r4) \
      r4 = r2; r2 = r2 & r3; r2 = r2 ^ r1; \
      r1 = r1 | r3; r1 = r1 & r0; r4 = r4 ^ r2; \
      r4 = r4 ^ r1; r1 = r1 & r2; r0 = r0 ^ -1; \
      r3 = r3 ^ r4; r1 = r1 ^ r3; r3 = r3 & r0; \
      r3 = r3 ^ r2; r0 = r0 ^ r1; r2 = r2 & r0; \
      r3 = r3 ^ r0; r2 = r2 ^ r4; r2 = r2 | r3; \
      r3 = r3 ^ r0; r2 = r2 ^ r1; 

#define I5(r0, r1, r2, r3, r4) \
      r1 = r1 ^ -1; r4 = r3; r2 = r2 ^ r1; \
      r3 = r3 | r0; r3 = r3 ^ r2; r2 = r2 | r1; \
      r2 = r2 & r0; r4 = r4 ^ r3; r2 = r2 ^ r4; \
      r4 = r4 | r0; r4 = r4 ^ r1; r1 = r1 & r2; \
      r1 = r1 ^ r3; r4 = r4 ^ r2; r3 = r3 & r4; \
      r4 = r4 ^ r1; r3 = r3 ^ r0; r3 = r3 ^ r4; \
      r4 = r4 ^ -1; 


#define I6(r0, r1, r2, r3, r4) \
      r0 = r0 ^ r2; r4 = r2; r2 = r2 & r0; \
      r4 = r4 ^ r3; r2 = r2 ^ -1; r3 = r3 ^ r1; \
      r2 = r2 ^ r3; r4 = r4 | r0; r0 = r0 ^ r2; \
      r3 = r3 ^ r4; r4 = r4 ^ r1; r1 = r1 & r3; \
      r1 = r1 ^ r0; r0 = r0 ^ r3; r0 = r0 | r2; \
      r3 = r3 ^ r1; r4 = r4 ^ r0; 

#define I7(r0, r1, r2, r3, r4) \
      r4 = r2; r2 = r2 ^ r0; r0 = r0 & r3; \
      r4 = r4 | r3; r2 = r2 ^ -1; r3 = r3 ^ r1; \
      r1 = r1 | r0; r0 = r0 ^ r2; r2 = r2 & r4; \
      r3 = r3 & r4; r1 = r1 ^ r2; r2 = r2 ^ r0; \
      r0 = r0 | r2; r4 = r4 ^ r1; r0 = r0 ^ r3; \
      r3 = r3 ^ r4; r4 = r4 | r0; r3 = r3 ^ r2; \
      r4 = r4 ^ r2; 

#define LINTRANS(r0, r1, r2, r3, r4) \
      r0 =  ROL(r0, 13); r2 = ROL(r2, 3); r3 = r3 ^ r2; \
      r4 = r0 << 3; r1 = r1 ^ r0; r3 = r3 ^ r4;         \
      r1 = r1 ^ r2; r3 = ROL(r3, 7); r1 = ROL(r1, 1);   \
      r2 = r2 ^ r3; r4 = r1 << 7; r0 = r0 ^ r1;         \
      r2 = r2 ^ r4; r0 = r0 ^ r3; r2 = ROL(r2, 22);     \
      r0 = ROL(r0, 5);

#define ILINTRANS(r0, r1, r2, r3, r4) \
      r2 = ROR(r2, 22); r0 = ROR(r0, 5); r2 = r2 ^ r3; \
      r4 = r1 << 7; r0 = r0 ^ r1; r2 = r2 ^ r4;        \
      r0 = r0 ^ r3; r3 = ROR(r3, 7); r1 = ROR(r1, 1);  \
      r3 = r3 ^ r2; r4 = r0 << 3; r1 = r1 ^ r0;        \
      r3 = r3 ^ r4; r1 = r1 ^ r2; r2 = ROR(r2, 3);     \
      r0 = ROR(r0, 13); 


#define KEYMIX(l_key, r0, r1, r2, r3, r4, IN) \
      r0  = r0 ^ l_key[IN+8]; r1  = r1 ^ l_key[IN+9];     \
      r2  = r2 ^ l_key[IN+10]; r3  = r3 ^ l_key[IN+11]; 

#define GETKEY(l_key, r0, r1, r2, r3, IN) \
      r0 = l_key[IN+8]; r1 = l_key[IN+9];            \
      r2 = l_key[IN+10]; r3 = l_key[IN+11]; 

#define SETKEY(l_key, r0, r1, r2, r3, IN) \
      l_key[IN+8] = r0; l_key[IN+9] = r1;            \
      l_key[IN+10] = r2; l_key[IN+11] = r3;

static bool LqCryptSerpentInit(LqCryptCipher* skey, const LqCryptCipherMethods *, const unsigned char *, const unsigned char *key, int key_len, int, int) {
    uint32_t i, lk;
    register uint32_t r0, r1, r2, r3, r4;;
    register uint32_t* k = skey->ciphers.serpent.keys;

    if(key_len != 16 && key_len != 24 && key_len != 32) {
        lq_errno_set(EINVAL);
        return false;
    }
    skey->methods = &serpent_desc;
    key_len *= 8;
    i = 0; lk = (key_len + 31) / 32;
    while(i < lk) {
        LOAD32H(k[i], ((uint32_t *)key) + (lk - i - 1))
        i++;
    }
    if(key_len < 256) {
        while(i < 8)
            k[i++] = 0;
        i = key_len / 32; lk = 1 << key_len % 32;
        k[i] &= lk - 1;
        k[i] |= lk;
    }

    for(i = 0; i < 132; ++i) {
        lk = k[i] ^ k[i + 3] ^ k[i + 5] ^ k[i + 7] ^ 0x9e3779b9 ^ i;
        k[i + 8] = (lk << 11) | (lk >> 21);
    }

    GETKEY(k, r0, r1, r2, r3, 0); S3(r0, r1, r2, r3, r4); SETKEY(k, r1, r2, r3, r4, 0);
    GETKEY(k, r0, r1, r2, r3, 4); S2(r0, r1, r2, r3, r4); SETKEY(k, r2, r3, r1, r4, 4);
    GETKEY(k, r0, r1, r2, r3, 8); S1(r0, r1, r2, r3, r4); SETKEY(k, r3, r1, r2, r0, 8);
    GETKEY(k, r0, r1, r2, r3, 12); S0(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r2, r0, 12);
    GETKEY(k, r0, r1, r2, r3, 16); S7(r0, r1, r2, r3, r4); SETKEY(k, r2, r4, r3, r0, 16);
    GETKEY(k, r0, r1, r2, r3, 20); S6(r0, r1, r2, r3, r4); SETKEY(k, r0, r1, r4, r2, 20);
    GETKEY(k, r0, r1, r2, r3, 24); S5(r0, r1, r2, r3, r4); SETKEY(k, r1, r3, r0, r2, 24);
    GETKEY(k, r0, r1, r2, r3, 28); S4(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r0, r3, 28);
    GETKEY(k, r0, r1, r2, r3, 32); S3(r0, r1, r2, r3, r4); SETKEY(k, r1, r2, r3, r4, 32);
    GETKEY(k, r0, r1, r2, r3, 36); S2(r0, r1, r2, r3, r4); SETKEY(k, r2, r3, r1, r4, 36);
    GETKEY(k, r0, r1, r2, r3, 40); S1(r0, r1, r2, r3, r4); SETKEY(k, r3, r1, r2, r0, 40);
    GETKEY(k, r0, r1, r2, r3, 44); S0(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r2, r0, 44);
    GETKEY(k, r0, r1, r2, r3, 48); S7(r0, r1, r2, r3, r4); SETKEY(k, r2, r4, r3, r0, 48);
    GETKEY(k, r0, r1, r2, r3, 52); S6(r0, r1, r2, r3, r4); SETKEY(k, r0, r1, r4, r2, 52);
    GETKEY(k, r0, r1, r2, r3, 56); S5(r0, r1, r2, r3, r4); SETKEY(k, r1, r3, r0, r2, 56);
    GETKEY(k, r0, r1, r2, r3, 60); S4(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r0, r3, 60);
    GETKEY(k, r0, r1, r2, r3, 64); S3(r0, r1, r2, r3, r4); SETKEY(k, r1, r2, r3, r4, 64);
    GETKEY(k, r0, r1, r2, r3, 68); S2(r0, r1, r2, r3, r4); SETKEY(k, r2, r3, r1, r4, 68);
    GETKEY(k, r0, r1, r2, r3, 72); S1(r0, r1, r2, r3, r4); SETKEY(k, r3, r1, r2, r0, 72);
    GETKEY(k, r0, r1, r2, r3, 76); S0(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r2, r0, 76);
    GETKEY(k, r0, r1, r2, r3, 80); S7(r0, r1, r2, r3, r4); SETKEY(k, r2, r4, r3, r0, 80);
    GETKEY(k, r0, r1, r2, r3, 84); S6(r0, r1, r2, r3, r4); SETKEY(k, r0, r1, r4, r2, 84);
    GETKEY(k, r0, r1, r2, r3, 88); S5(r0, r1, r2, r3, r4); SETKEY(k, r1, r3, r0, r2, 88);
    GETKEY(k, r0, r1, r2, r3, 92); S4(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r0, r3, 92);
    GETKEY(k, r0, r1, r2, r3, 96); S3(r0, r1, r2, r3, r4); SETKEY(k, r1, r2, r3, r4, 96);
    GETKEY(k, r0, r1, r2, r3, 100); S2(r0, r1, r2, r3, r4); SETKEY(k, r2, r3, r1, r4, 100);
    GETKEY(k, r0, r1, r2, r3, 104); S1(r0, r1, r2, r3, r4); SETKEY(k, r3, r1, r2, r0, 104);
    GETKEY(k, r0, r1, r2, r3, 108); S0(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r2, r0, 108);
    GETKEY(k, r0, r1, r2, r3, 112); S7(r0, r1, r2, r3, r4); SETKEY(k, r2, r4, r3, r0, 112);
    GETKEY(k, r0, r1, r2, r3, 116); S6(r0, r1, r2, r3, r4); SETKEY(k, r0, r1, r4, r2, 116);
    GETKEY(k, r0, r1, r2, r3, 120); S5(r0, r1, r2, r3, r4); SETKEY(k, r1, r3, r0, r2, 120);
    GETKEY(k, r0, r1, r2, r3, 124); S4(r0, r1, r2, r3, r4); SETKEY(k, r1, r4, r0, r3, 124);
    GETKEY(k, r0, r1, r2, r3, 128); S3(r0, r1, r2, r3, r4); SETKEY(k, r1, r2, r3, r4, 128);
    return true;
}

static bool LqCryptSerpentEncrypt(LqCryptCipher *skey, const unsigned char *in, unsigned char *out, intptr_t Len) {
    register uint32_t r0, r1, r2, r3, r4;
    register uint32_t const* const k = skey->ciphers.serpent.keys;
    if(Len % 16) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = in + Len; in < m; in += skey->methods->block_length, out += skey->methods->block_length) {
        LOAD32H(r3, in); LOAD32H(r2, in + 4);
        LOAD32H(r1, in + 8); LOAD32H(r0, in + 12);
        KEYMIX(k, r0, r1, r2, r3, r4, 0); S0(r0, r1, r2, r3, r4); LINTRANS(r1, r4, r2, r0, r3); /* round 1 */
        KEYMIX(k, r1, r4, r2, r0, r3, 4); S1(r1, r4, r2, r0, r3); LINTRANS(r0, r4, r2, r1, r3); /* round 2 */
        KEYMIX(k, r0, r4, r2, r1, r3, 8); S2(r0, r4, r2, r1, r3); LINTRANS(r2, r1, r4, r3, r0); /* round 3 */
        KEYMIX(k, r2, r1, r4, r3, r0, 12); S3(r2, r1, r4, r3, r0); LINTRANS(r1, r4, r3, r0, r2);/* round 4 */
        KEYMIX(k, r1, r4, r3, r0, r2, 16); S4(r1, r4, r3, r0, r2); LINTRANS(r4, r2, r1, r0, r3);/* round 5 */
        KEYMIX(k, r4, r2, r1, r0, r3, 20); S5(r4, r2, r1, r0, r3); LINTRANS(r2, r0, r4, r1, r3);/* round 6 */
        KEYMIX(k, r2, r0, r4, r1, r3, 24); S6(r2, r0, r4, r1, r3); LINTRANS(r2, r0, r3, r4, r1);/* round 7 */
        KEYMIX(k, r2, r0, r3, r4, r1, 28); S7(r2, r0, r3, r4, r1); LINTRANS(r3, r1, r4, r2, r0);/* round 8 */
        KEYMIX(k, r3, r1, r4, r2, r0, 32); S0(r3, r1, r4, r2, r0); LINTRANS(r1, r0, r4, r3, r2);/* round 9 */
        KEYMIX(k, r1, r0, r4, r3, r2, 36); S1(r1, r0, r4, r3, r2); LINTRANS(r3, r0, r4, r1, r2);/* round 10 */
        KEYMIX(k, r3, r0, r4, r1, r2, 40); S2(r3, r0, r4, r1, r2); LINTRANS(r4, r1, r0, r2, r3);/* round 11 */
        KEYMIX(k, r4, r1, r0, r2, r3, 44); S3(r4, r1, r0, r2, r3); LINTRANS(r1, r0, r2, r3, r4);/* round 12 */
        KEYMIX(k, r1, r0, r2, r3, r4, 48); S4(r1, r0, r2, r3, r4); LINTRANS(r0, r4, r1, r3, r2);/* round 13 */
        KEYMIX(k, r0, r4, r1, r3, r2, 52); S5(r0, r4, r1, r3, r2); LINTRANS(r4, r3, r0, r1, r2);/* round 14 */
        KEYMIX(k, r4, r3, r0, r1, r2, 56); S6(r4, r3, r0, r1, r2); LINTRANS(r4, r3, r2, r0, r1);/* round 15 */
        KEYMIX(k, r4, r3, r2, r0, r1, 60); S7(r4, r3, r2, r0, r1); LINTRANS(r2, r1, r0, r4, r3);/* round 16 */
        KEYMIX(k, r2, r1, r0, r4, r3, 64); S0(r2, r1, r0, r4, r3); LINTRANS(r1, r3, r0, r2, r4);/* round 17 */
        KEYMIX(k, r1, r3, r0, r2, r4, 68); S1(r1, r3, r0, r2, r4); LINTRANS(r2, r3, r0, r1, r4);/* round 18 */
        KEYMIX(k, r2, r3, r0, r1, r4, 72); S2(r2, r3, r0, r1, r4); LINTRANS(r0, r1, r3, r4, r2);/* round 19 */
        KEYMIX(k, r0, r1, r3, r4, r2, 76); S3(r0, r1, r3, r4, r2); LINTRANS(r1, r3, r4, r2, r0);/* round 20 */
        KEYMIX(k, r1, r3, r4, r2, r0, 80); S4(r1, r3, r4, r2, r0); LINTRANS(r3, r0, r1, r2, r4);/* round 21 */
        KEYMIX(k, r3, r0, r1, r2, r4, 84); S5(r3, r0, r1, r2, r4); LINTRANS(r0, r2, r3, r1, r4);/* round 22 */
        KEYMIX(k, r0, r2, r3, r1, r4, 88); S6(r0, r2, r3, r1, r4); LINTRANS(r0, r2, r4, r3, r1);/* round 23 */
        KEYMIX(k, r0, r2, r4, r3, r1, 92); S7(r0, r2, r4, r3, r1); LINTRANS(r4, r1, r3, r0, r2);/* round 24 */
        KEYMIX(k, r4, r1, r3, r0, r2, 96); S0(r4, r1, r3, r0, r2); LINTRANS(r1, r2, r3, r4, r0);/* round 25 */
        KEYMIX(k, r1, r2, r3, r4, r0, 100); S1(r1, r2, r3, r4, r0); LINTRANS(r4, r2, r3, r1, r0);/* round 26 */
        KEYMIX(k, r4, r2, r3, r1, r0, 104); S2(r4, r2, r3, r1, r0); LINTRANS(r3, r1, r2, r0, r4);/* round 27 */
        KEYMIX(k, r3, r1, r2, r0, r4, 108); S3(r3, r1, r2, r0, r4); LINTRANS(r1, r2, r0, r4, r3);/* round 28 */
        KEYMIX(k, r1, r2, r0, r4, r3, 112); S4(r1, r2, r0, r4, r3); LINTRANS(r2, r3, r1, r4, r0);/* round 29 */
        KEYMIX(k, r2, r3, r1, r4, r0, 116); S5(r2, r3, r1, r4, r0); LINTRANS(r3, r4, r2, r1, r0);/* round 30 */
        KEYMIX(k, r3, r4, r2, r1, r0, 120); S6(r3, r4, r2, r1, r0); LINTRANS(r3, r4, r0, r2, r1);/* round 31 */
        KEYMIX(k, r3, r4, r0, r2, r1, 124); S7(r3, r4, r0, r2, r1); KEYMIX(k, r0, r1, r2, r3, r4, 128);/* round 32 */
        STORE32H(r3, out); STORE32H(r2, out + 4);
        STORE32H(r1, out + 8); STORE32H(r0, out + 12);
    }
    return true;
}

static bool LqCryptSerpentDecrypt(LqCryptCipher *skey, const unsigned char *in, unsigned char *out, intptr_t Len) {
    register uint32_t  r0, r1, r2, r3, r4;
    register uint32_t const * const k = skey->ciphers.serpent.keys;
    if(Len % 16) {
        lq_errno_set(EINVAL);
        return false;
    }
    for(const unsigned char* m = in + Len; in < m; in += skey->methods->block_length, out += skey->methods->block_length) {
        LOAD32H(r3, in); LOAD32H(r2, in + 4);
        LOAD32H(r1, in + 8); LOAD32H(r0, in + 12);
        KEYMIX(k, r0, r1, r2, r3, r4, 128); I7(r0, r1, r2, r3, r4); KEYMIX(k, r3, r0, r1, r4, r2, 124);/* round 1 */
        ILINTRANS(r3, r0, r1, r4, r2); I6(r3, r0, r1, r4, r2); KEYMIX(k, r0, r1, r2, r4, r3, 120);/* round 2 */
        ILINTRANS(r0, r1, r2, r4, r3); I5(r0, r1, r2, r4, r3); KEYMIX(k, r1, r3, r4, r2, r0, 116);/* round 3 */
        ILINTRANS(r1, r3, r4, r2, r0); I4(r1, r3, r4, r2, r0); KEYMIX(k, r1, r2, r4, r0, r3, 112);/* round 4 */
        ILINTRANS(r1, r2, r4, r0, r3); I3(r1, r2, r4, r0, r3); KEYMIX(k, r4, r2, r0, r1, r3, 108);/* round 5 */
        ILINTRANS(r4, r2, r0, r1, r3); I2(r4, r2, r0, r1, r3); KEYMIX(k, r2, r3, r0, r1, r4, 104);/* round 6 */
        ILINTRANS(r2, r3, r0, r1, r4); I1(r2, r3, r0, r1, r4); KEYMIX(k, r4, r2, r1, r0, r3, 100);/* round 7 */
        ILINTRANS(r4, r2, r1, r0, r3); I0(r4, r2, r1, r0, r3); KEYMIX(k, r4, r3, r2, r0, r1, 96);/* round 8 */
        ILINTRANS(r4, r3, r2, r0, r1); I7(r4, r3, r2, r0, r1); KEYMIX(k, r0, r4, r3, r1, r2, 92);/* round 9 */
        ILINTRANS(r0, r4, r3, r1, r2); I6(r0, r4, r3, r1, r2); KEYMIX(k, r4, r3, r2, r1, r0, 88);/* round 10 */
        ILINTRANS(r4, r3, r2, r1, r0); I5(r4, r3, r2, r1, r0); KEYMIX(k, r3, r0, r1, r2, r4, 84);/* round 11 */
        ILINTRANS(r3, r0, r1, r2, r4); I4(r3, r0, r1, r2, r4); KEYMIX(k, r3, r2, r1, r4, r0, 80);/* round 12 */
        ILINTRANS(r3, r2, r1, r4, r0); I3(r3, r2, r1, r4, r0); KEYMIX(k, r1, r2, r4, r3, r0, 76);/* round 13 */
        ILINTRANS(r1, r2, r4, r3, r0); I2(r1, r2, r4, r3, r0); KEYMIX(k, r2, r0, r4, r3, r1, 72);/* round 14 */
        ILINTRANS(r2, r0, r4, r3, r1); I1(r2, r0, r4, r3, r1); KEYMIX(k, r1, r2, r3, r4, r0, 68);/* round 15 */
        ILINTRANS(r1, r2, r3, r4, r0); I0(r1, r2, r3, r4, r0); KEYMIX(k, r1, r0, r2, r4, r3, 64);/* round 16 */
        ILINTRANS(r1, r0, r2, r4, r3); I7(r1, r0, r2, r4, r3); KEYMIX(k, r4, r1, r0, r3, r2, 60);/* round 17 */
        ILINTRANS(r4, r1, r0, r3, r2); I6(r4, r1, r0, r3, r2); KEYMIX(k, r1, r0, r2, r3, r4, 56);/* round 18 */
        ILINTRANS(r1, r0, r2, r3, r4); I5(r1, r0, r2, r3, r4); KEYMIX(k, r0, r4, r3, r2, r1, 52);/* round 19 */
        ILINTRANS(r0, r4, r3, r2, r1); I4(r0, r4, r3, r2, r1); KEYMIX(k, r0, r2, r3, r1, r4, 48);/* round 20 */
        ILINTRANS(r0, r2, r3, r1, r4); I3(r0, r2, r3, r1, r4); KEYMIX(k, r3, r2, r1, r0, r4, 44);/* round 21 */
        ILINTRANS(r3, r2, r1, r0, r4); I2(r3, r2, r1, r0, r4); KEYMIX(k, r2, r4, r1, r0, r3, 40);/* round 22 */
        ILINTRANS(r2, r4, r1, r0, r3); I1(r2, r4, r1, r0, r3); KEYMIX(k, r3, r2, r0, r1, r4, 36);/* round 23 */
        ILINTRANS(r3, r2, r0, r1, r4); I0(r3, r2, r0, r1, r4); KEYMIX(k, r3, r4, r2, r1, r0, 32);/* round 24 */
        ILINTRANS(r3, r4, r2, r1, r0); I7(r3, r4, r2, r1, r0); KEYMIX(k, r1, r3, r4, r0, r2, 28);/* round 25 */
        ILINTRANS(r1, r3, r4, r0, r2); I6(r1, r3, r4, r0, r2); KEYMIX(k, r3, r4, r2, r0, r1, 24);/* round 26 */
        ILINTRANS(r3, r4, r2, r0, r1); I5(r3, r4, r2, r0, r1); KEYMIX(k, r4, r1, r0, r2, r3, 20);/* round 27 */
        ILINTRANS(r4, r1, r0, r2, r3); I4(r4, r1, r0, r2, r3); KEYMIX(k, r4, r2, r0, r3, r1, 16);/* round 28 */
        ILINTRANS(r4, r2, r0, r3, r1); I3(r4, r2, r0, r3, r1); KEYMIX(k, r0, r2, r3, r4, r1, 12);/* round 29 */
        ILINTRANS(r0, r2, r3, r4, r1); I2(r0, r2, r3, r4, r1); KEYMIX(k, r2, r1, r3, r4, r0, 8);/* round 30 */
        ILINTRANS(r2, r1, r3, r4, r0); I1(r2, r1, r3, r4, r0); KEYMIX(k, r0, r2, r4, r3, r1, 4);/* round 31 */
        ILINTRANS(r0, r2, r4, r3, r1); I0(r0, r2, r4, r3, r1); KEYMIX(k, r0, r1, r2, r3, r4, 0);/* round 32 */
        STORE32H(r3, out); STORE32H(r2, out + 4);
        STORE32H(r1, out + 8); STORE32H(r0, out + 12);
    }
    return true;
}

static void LqCryptSerpentCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->ciphers.serpent, &Source->ciphers.serpent, sizeof(Source->ciphers.serpent));
}


/* end serphent */

#define F(x,y,z)  (z ^ (x & (y ^ z)))
#define G(x,y,z)  (y ^ (z & (y ^ x)))
#define H(x,y,z)  (x^y^z)
#define I(x,y,z)  (y^(x|(~z)))

#define FF(a,b,c,d,M,s,t) \
    a = (a + F(b,c,d) + M + t); a = ROLc(a, s) + b;

#define GG(a,b,c,d,M,s,t) \
    a = (a + G(b,c,d) + M + t); a = ROLc(a, s) + b;

#define HH(a,b,c,d,M,s,t) \
    a = (a + H(b,c,d) + M + t); a = ROLc(a, s) + b;

#define II(a,b,c,d,M,s,t) \
    a = (a + I(b,c,d) + M + t); a = ROLc(a, s) + b;


#define HASH_PROCESS(func_name, compress_name, state_var, block_size)                       \
static bool func_name (LqCryptHash * md, const unsigned char *in, intptr_t inlen)            \
{                                                                                           \
    unsigned long n;                                                                        \
    while (inlen > ((intptr_t)0)) {                                                         \
    if (md-> state_var .curlen == 0 && inlen >= block_size) {                           \
       compress_name (md, (unsigned char *)in);                                         \
       md-> state_var .length += block_size * 8;                                        \
       in             += block_size;                                                    \
       inlen          -= block_size;                                                    \
    } else {                                                                            \
       n = lq_min(inlen, (block_size - md-> state_var .curlen));                           \
       memcpy(md-> state_var .buf + md-> state_var.curlen, in, (size_t)n);              \
       md-> state_var .curlen += n;                                                     \
       in             += n;                                                             \
       inlen          -= n;                                                             \
       if (md-> state_var .curlen == block_size) {                                      \
          compress_name (md, md-> state_var .buf);                                      \
          md-> state_var .length += 8*block_size;                                       \
          md-> state_var .curlen = 0;                                                   \
       }                                                                                \
       }                                                                                    \
    }                                                                                       \
    return true;                                                                        \
}

static bool LqCryptMd5Init(LqCryptHash * md) {
    md->methods = &md5_desc;
    md->md5.state[0] = 0x67452301UL;
    md->md5.state[1] = 0xefcdab89UL;
    md->md5.state[2] = 0x98badcfeUL;
    md->md5.state[3] = 0x10325476UL;
    md->md5.curlen = 0;
    md->md5.length = 0;
    return true;
}

static bool LqCryptMd5Compress(LqCryptHash *md, unsigned char *buf) {
    uint32_t i, W[16], a, b, c, d;
    for(i = 0; i < 16; i++) 
        LOAD32L(W[i], buf + (4 * i));
    a = md->md5.state[0];
    b = md->md5.state[1];
    c = md->md5.state[2];
    d = md->md5.state[3];

    FF(a, b, c, d, W[0], 7, 0xd76aa478UL) FF(d, a, b, c, W[1], 12, 0xe8c7b756UL)
    FF(c, d, a, b, W[2], 17, 0x242070dbUL) FF(b, c, d, a, W[3], 22, 0xc1bdceeeUL)
    FF(a, b, c, d, W[4], 7, 0xf57c0fafUL) FF(d, a, b, c, W[5], 12, 0x4787c62aUL)
    FF(c, d, a, b, W[6], 17, 0xa8304613UL) FF(b, c, d, a, W[7], 22, 0xfd469501UL)
    FF(a, b, c, d, W[8], 7, 0x698098d8UL) FF(d, a, b, c, W[9], 12, 0x8b44f7afUL)
    FF(c, d, a, b, W[10], 17, 0xffff5bb1UL) FF(b, c, d, a, W[11], 22, 0x895cd7beUL)
    FF(a, b, c, d, W[12], 7, 0x6b901122UL) FF(d, a, b, c, W[13], 12, 0xfd987193UL)
    FF(c, d, a, b, W[14], 17, 0xa679438eUL) FF(b, c, d, a, W[15], 22, 0x49b40821UL)
    GG(a, b, c, d, W[1], 5, 0xf61e2562UL) GG(d, a, b, c, W[6], 9, 0xc040b340UL)
    GG(c, d, a, b, W[11], 14, 0x265e5a51UL) GG(b, c, d, a, W[0], 20, 0xe9b6c7aaUL)
    GG(a, b, c, d, W[5], 5, 0xd62f105dUL) GG(d, a, b, c, W[10], 9, 0x02441453UL)
    GG(c, d, a, b, W[15], 14, 0xd8a1e681UL) GG(b, c, d, a, W[4], 20, 0xe7d3fbc8UL)
    GG(a, b, c, d, W[9], 5, 0x21e1cde6UL) GG(d, a, b, c, W[14], 9, 0xc33707d6UL)
    GG(c, d, a, b, W[3], 14, 0xf4d50d87UL) GG(b, c, d, a, W[8], 20, 0x455a14edUL)
    GG(a, b, c, d, W[13], 5, 0xa9e3e905UL) GG(d, a, b, c, W[2], 9, 0xfcefa3f8UL)
    GG(c, d, a, b, W[7], 14, 0x676f02d9UL) GG(b, c, d, a, W[12], 20, 0x8d2a4c8aUL)
    HH(a, b, c, d, W[5], 4, 0xfffa3942UL) HH(d, a, b, c, W[8], 11, 0x8771f681UL)
    HH(c, d, a, b, W[11], 16, 0x6d9d6122UL) HH(b, c, d, a, W[14], 23, 0xfde5380cUL)
    HH(a, b, c, d, W[1], 4, 0xa4beea44UL) HH(d, a, b, c, W[4], 11, 0x4bdecfa9UL)
    HH(c, d, a, b, W[7], 16, 0xf6bb4b60UL) HH(b, c, d, a, W[10], 23, 0xbebfbc70UL)
    HH(a, b, c, d, W[13], 4, 0x289b7ec6UL) HH(d, a, b, c, W[0], 11, 0xeaa127faUL)
    HH(c, d, a, b, W[3], 16, 0xd4ef3085UL) HH(b, c, d, a, W[6], 23, 0x04881d05UL)
    HH(a, b, c, d, W[9], 4, 0xd9d4d039UL) HH(d, a, b, c, W[12], 11, 0xe6db99e5UL)
    HH(c, d, a, b, W[15], 16, 0x1fa27cf8UL) HH(b, c, d, a, W[2], 23, 0xc4ac5665UL)
    II(a, b, c, d, W[0], 6, 0xf4292244UL) II(d, a, b, c, W[7], 10, 0x432aff97UL)
    II(c, d, a, b, W[14], 15, 0xab9423a7UL) II(b, c, d, a, W[5], 21, 0xfc93a039UL)
    II(a, b, c, d, W[12], 6, 0x655b59c3UL) II(d, a, b, c, W[3], 10, 0x8f0ccc92UL)
    II(c, d, a, b, W[10], 15, 0xffeff47dUL) II(b, c, d, a, W[1], 21, 0x85845dd1UL)
    II(a, b, c, d, W[8], 6, 0x6fa87e4fUL) II(d, a, b, c, W[15], 10, 0xfe2ce6e0UL)
    II(c, d, a, b, W[6], 15, 0xa3014314UL) II(b, c, d, a, W[13], 21, 0x4e0811a1UL)
    II(a, b, c, d, W[4], 6, 0xf7537e82UL) II(d, a, b, c, W[11], 10, 0xbd3af235UL)
    II(c, d, a, b, W[2], 15, 0x2ad7d2bbUL) II(b, c, d, a, W[9], 21, 0xeb86d391UL)
    md->md5.state[0] = md->md5.state[0] + a;
    md->md5.state[1] = md->md5.state[1] + b;
    md->md5.state[2] = md->md5.state[2] + c;
    md->md5.state[3] = md->md5.state[3] + d;
    return true;
}

HASH_PROCESS(LqCryptMd5Update, LqCryptMd5Compress, md5, 64)

static intptr_t LqCryptMd5Final(LqCryptHash * md, unsigned char *out) {
    int i;
    md->md5.length += md->md5.curlen * 8;
    md->md5.buf[md->md5.curlen++] = (unsigned char)0x80;
    if(md->md5.curlen > 56) {
        while(md->md5.curlen < 64)
            md->md5.buf[md->md5.curlen++] = (unsigned char)0;
        LqCryptMd5Compress(md, md->md5.buf);
        md->md5.curlen = 0;
    }
    while(md->md5.curlen < 56)
        md->md5.buf[md->md5.curlen++] = (unsigned char)0;
    STORE64L(md->md5.length, md->md5.buf + 56);
    LqCryptMd5Compress(md, md->md5.buf);
    for(i = 0; i < 4; i++)
        STORE32L(md->md5.state[i], out + (4 * i));
    return 16;
}

static void LqCryptMd5Copy(LqCryptHash *Dest, LqCryptHash *Source) {
    memcpy(&Dest->md5, &Source->md5, sizeof(Source->md5));
}

#define F0(x,y,z)  (z ^ (x & (y ^ z)))
#define F1(x,y,z)  (x ^ y ^ z)
#define F2(x,y,z)  ((x & y) | (z & (x | y)))
#define F3(x,y,z)  (x ^ y ^ z)

static void LqCryptSha1Compress(LqCryptHash *md, unsigned char *buf) {
    register uint32_t a, b, c, d, e, i;
    uint32_t W[80];
    uint32_t* src = (uint32_t*)buf, *m = src + 16, *src2 = W;

    for(; src < m; src++, src2++)
        LOAD32H(*src2, src);
    a = md->sha1.state[0]; b = md->sha1.state[1];
    c = md->sha1.state[2]; d = md->sha1.state[3];
    e = md->sha1.state[4];
    src = W + 16;
    m = W + 80;
    for( ; src < m; src++)
        *src = ROL(*(src - 3) ^ *(src - 8) ^ *(src - 14) ^ *(src - 16), 1);

#define SHA1_FF0(a,b,c,d,e,i) e = (ROLc(a, 5) + F0(b,c,d) + e + W[i] + 0x5a827999UL); b = ROLc(b, 30);
#define SHA1_FF1(a,b,c,d,e,i) e = (ROLc(a, 5) + F1(b,c,d) + e + W[i] + 0x6ed9eba1UL); b = ROLc(b, 30);
#define SHA1_FF2(a,b,c,d,e,i) e = (ROLc(a, 5) + F2(b,c,d) + e + W[i] + 0x8f1bbcdcUL); b = ROLc(b, 30);
#define SHA1_FF3(a,b,c,d,e,i) e = (ROLc(a, 5) + F3(b,c,d) + e + W[i] + 0xca62c1d6UL); b = ROLc(b, 30);

    for(i = 0; i < 20; ) {
        SHA1_FF0(a, b, c, d, e, i++);
        SHA1_FF0(e, a, b, c, d, i++);
        SHA1_FF0(d, e, a, b, c, i++);
        SHA1_FF0(c, d, e, a, b, i++);
        SHA1_FF0(b, c, d, e, a, i++);
    }
    for(; i < 40; ) {
        SHA1_FF1(a, b, c, d, e, i++);
        SHA1_FF1(e, a, b, c, d, i++);
        SHA1_FF1(d, e, a, b, c, i++);
        SHA1_FF1(c, d, e, a, b, i++);
        SHA1_FF1(b, c, d, e, a, i++);
    }
    for(; i < 60; ) {
        SHA1_FF2(a, b, c, d, e, i++);
        SHA1_FF2(e, a, b, c, d, i++);
        SHA1_FF2(d, e, a, b, c, i++);
        SHA1_FF2(c, d, e, a, b, i++);
        SHA1_FF2(b, c, d, e, a, i++);
    }
    for(; i < 80; ) {
        SHA1_FF3(a, b, c, d, e, i++);
        SHA1_FF3(e, a, b, c, d, i++);
        SHA1_FF3(d, e, a, b, c, i++);
        SHA1_FF3(c, d, e, a, b, i++);
        SHA1_FF3(b, c, d, e, a, i++);
    }
    md->sha1.state[0] = md->sha1.state[0] + a;
    md->sha1.state[1] = md->sha1.state[1] + b;
    md->sha1.state[2] = md->sha1.state[2] + c;
    md->sha1.state[3] = md->sha1.state[3] + d;
    md->sha1.state[4] = md->sha1.state[4] + e;
}

static bool LqCryptSha1Init(LqCryptHash * md) {
    md->methods = &sha1_desc;
    md->sha1.state[0] = 0x67452301UL;
    md->sha1.state[1] = 0xefcdab89UL;
    md->sha1.state[2] = 0x98badcfeUL;
    md->sha1.state[3] = 0x10325476UL;
    md->sha1.state[4] = 0xc3d2e1f0UL;
    md->sha1.curlen = 0;
    md->sha1.length = 0;
    return true;
}

HASH_PROCESS(LqCryptSha1Update, LqCryptSha1Compress, sha1, 64)

static intptr_t LqCryptSha1Final(LqCryptHash * md, unsigned char *out) {
    int i;

    md->sha1.length += md->sha1.curlen * 8;
    md->sha1.buf[md->sha1.curlen++] = (unsigned char)0x80;
    if(md->sha1.curlen > 56) {
        while(md->sha1.curlen < 64)
            md->sha1.buf[md->sha1.curlen++] = (unsigned char)0;
        LqCryptSha1Compress(md, md->sha1.buf);
        md->sha1.curlen = 0;
    }
    while(md->sha1.curlen < 56)
        md->sha1.buf[md->sha1.curlen++] = (unsigned char)0;
    STORE64H(md->sha1.length, md->sha1.buf + 56);
    LqCryptSha1Compress(md, md->sha1.buf);
    for(i = 0; i < 5; i++)
        STORE32H(md->sha1.state[i], out + (4 * i));
    return 20;
}

static void LqCryptSha1Copy(LqCryptHash *Dest, LqCryptHash *Source) {
    memcpy(&Dest->sha1, &Source->sha1, sizeof(Source->sha1));
}

#define Ch(x,y,z)       (z ^ (x & (y ^ z)))
#define Maj(x,y,z)      (((x | y) & z) | (x & y)) 
#define S(x, n)         RORc((x),(n))
#define R(x, n)         (((x)&0xFFFFFFFFUL)>>(n))
#define Sigma0(x)       (S(x, 2) ^ S(x, 13) ^ S(x, 22))
#define Sigma1(x)       (S(x, 6) ^ S(x, 11) ^ S(x, 25))
#define Gamma0(x)       (S(x, 7) ^ S(x, 18) ^ R(x, 3))
#define Gamma1(x)       (S(x, 17) ^ S(x, 19) ^ R(x, 10))


static void sha256_compress(LqCryptHash* md, unsigned char *buf)
{
    uint32_t S[8], W[64];
    register uint32_t t0, t1;
    int i;

    for(i = 0; i < 8; i++)
        S[i] = md->sha256.state[i];
    for(i = 0; i < 16; i++)
        LOAD32H(W[i], buf + (4 * i));
    for(i = 16; i < 64; i++)
        W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];


#define RND(a,b,c,d,e,f,g,h,i,ki)                    \
     t0 = h + Sigma1(e) + Ch(e, f, g) + ki + W[i];   \
     t1 = Sigma0(a) + Maj(a, b, c);                  \
     d += t0;                                        \
     h  = t0 + t1;

    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 0, 0x428a2f98);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 1, 0x71374491);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 2, 0xb5c0fbcf);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 3, 0xe9b5dba5);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 4, 0x3956c25b);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 5, 0x59f111f1);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 6, 0x923f82a4);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 7, 0xab1c5ed5);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 8, 0xd807aa98);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 9, 0x12835b01);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 10, 0x243185be);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 11, 0x550c7dc3);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 12, 0x72be5d74);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 13, 0x80deb1fe);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 14, 0x9bdc06a7);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 15, 0xc19bf174);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 16, 0xe49b69c1);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 17, 0xefbe4786);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 18, 0x0fc19dc6);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 19, 0x240ca1cc);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 20, 0x2de92c6f);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 21, 0x4a7484aa);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 22, 0x5cb0a9dc);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 23, 0x76f988da);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 24, 0x983e5152);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 25, 0xa831c66d);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 26, 0xb00327c8);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 27, 0xbf597fc7);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 28, 0xc6e00bf3);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 29, 0xd5a79147);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 30, 0x06ca6351);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 31, 0x14292967);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 32, 0x27b70a85);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 33, 0x2e1b2138);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 34, 0x4d2c6dfc);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 35, 0x53380d13);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 36, 0x650a7354);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 37, 0x766a0abb);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 38, 0x81c2c92e);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 39, 0x92722c85);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 40, 0xa2bfe8a1);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 41, 0xa81a664b);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 42, 0xc24b8b70);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 43, 0xc76c51a3);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 44, 0xd192e819);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 45, 0xd6990624);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 46, 0xf40e3585);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 47, 0x106aa070);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 48, 0x19a4c116);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 49, 0x1e376c08);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 50, 0x2748774c);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 51, 0x34b0bcb5);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 52, 0x391c0cb3);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 53, 0x4ed8aa4a);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 54, 0x5b9cca4f);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 55, 0x682e6ff3);
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], 56, 0x748f82ee);
    RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], 57, 0x78a5636f);
    RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], 58, 0x84c87814);
    RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], 59, 0x8cc70208);
    RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], 60, 0x90befffa);
    RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], 61, 0xa4506ceb);
    RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], 62, 0xbef9a3f7);
    RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], 63, 0xc67178f2);

#undef RND     
    /* feedback */
    for(i = 0; i < 8; i++)
        md->sha256.state[i] = md->sha256.state[i] + S[i];
}

static bool LqCryptSha256Init(LqCryptHash * md) {
    md->methods = &sha256_desc;
    md->sha256.curlen = 0;
    md->sha256.length = 0;
    md->sha256.state[0] = 0x6A09E667UL;
    md->sha256.state[1] = 0xBB67AE85UL;
    md->sha256.state[2] = 0x3C6EF372UL;
    md->sha256.state[3] = 0xA54FF53AUL;
    md->sha256.state[4] = 0x510E527FUL;
    md->sha256.state[5] = 0x9B05688CUL;
    md->sha256.state[6] = 0x1F83D9ABUL;
    md->sha256.state[7] = 0x5BE0CD19UL;
    return true;
}

HASH_PROCESS(LqCryptSha256Update, sha256_compress, sha256, 64)

static intptr_t LqCryptSha256Final(LqCryptHash * md, unsigned char *out) {
    int i;

    md->sha256.length += md->sha256.curlen * 8;
    md->sha256.buf[md->sha256.curlen++] = (unsigned char)0x80;
    if(md->sha256.curlen > 56) {
        while(md->sha256.curlen < 64) {
            md->sha256.buf[md->sha256.curlen++] = (unsigned char)0;
        }
        sha256_compress(md, md->sha256.buf);
        md->sha256.curlen = 0;
    }
    while(md->sha256.curlen < 56)
        md->sha256.buf[md->sha256.curlen++] = (unsigned char)0;
    STORE64H(md->sha256.length, md->sha256.buf + 56);
    sha256_compress(md, md->sha256.buf);
    for(i = 0; i < 8; i++)
        STORE32H(md->sha256.state[i], out + (4 * i));
    return 32;
}

static void LqCryptSha256Copy(LqCryptHash *Dest, LqCryptHash *Source) {
    memcpy(&Dest->sha256, &Source->sha256, sizeof(Source->sha256));
}

/* Various logical functions */
#define ROL64(x, y) \
    ( (((x)<<((uint64_t)(y)&63)) | \
      (((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64_t)64-((y)&63)))) & CONST64(0xFFFFFFFFFFFFFFFF))

#define ROR64(x, y) \
    ( ((((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64_t)(y)&CONST64(63))) | \
      ((x)<<((uint64_t)(64-((y)&CONST64(63)))))) & CONST64(0xFFFFFFFFFFFFFFFF))

#define ROL64c(x, y) \
    ( (((x)<<((uint64_t)(y)&63)) | \
      (((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64_t)64-((y)&63)))) & CONST64(0xFFFFFFFFFFFFFFFF))

#define ROR64c(x, y) \
    ( ((((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64_t)(y)&CONST64(63))) | \
      ((x)<<((uint64_t)(64-((y)&CONST64(63)))))) & CONST64(0xFFFFFFFFFFFFFFFF))


#define Ch(x,y,z)       (z ^ (x & (y ^ z)))
#define Maj(x,y,z)      (((x | y) & z) | (x & y)) 
#define S(x, n)         ROR64c(x, n)
#define R(x, n)         (((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64_t)n))
#define Sigma0(x)       (S(x, 28) ^ S(x, 34) ^ S(x, 39))
#define Sigma1(x)       (S(x, 14) ^ S(x, 18) ^ S(x, 41))
#define Gamma0(x)       (S(x, 1) ^ S(x, 8) ^ R(x, 7))
#define Gamma1(x)       (S(x, 19) ^ S(x, 61) ^ R(x, 6))

LQ_ALIGN(128) static const uint64_t K[80] = {
    CONST64(0x428a2f98d728ae22), CONST64(0x7137449123ef65cd), CONST64(0xb5c0fbcfec4d3b2f), CONST64(0xe9b5dba58189dbbc),
    CONST64(0x3956c25bf348b538), CONST64(0x59f111f1b605d019), CONST64(0x923f82a4af194f9b), CONST64(0xab1c5ed5da6d8118),
    CONST64(0xd807aa98a3030242), CONST64(0x12835b0145706fbe), CONST64(0x243185be4ee4b28c), CONST64(0x550c7dc3d5ffb4e2),
    CONST64(0x72be5d74f27b896f), CONST64(0x80deb1fe3b1696b1), CONST64(0x9bdc06a725c71235), CONST64(0xc19bf174cf692694),
    CONST64(0xe49b69c19ef14ad2), CONST64(0xefbe4786384f25e3), CONST64(0x0fc19dc68b8cd5b5), CONST64(0x240ca1cc77ac9c65),
    CONST64(0x2de92c6f592b0275), CONST64(0x4a7484aa6ea6e483), CONST64(0x5cb0a9dcbd41fbd4), CONST64(0x76f988da831153b5),
    CONST64(0x983e5152ee66dfab), CONST64(0xa831c66d2db43210), CONST64(0xb00327c898fb213f), CONST64(0xbf597fc7beef0ee4),
    CONST64(0xc6e00bf33da88fc2), CONST64(0xd5a79147930aa725), CONST64(0x06ca6351e003826f), CONST64(0x142929670a0e6e70),
    CONST64(0x27b70a8546d22ffc), CONST64(0x2e1b21385c26c926), CONST64(0x4d2c6dfc5ac42aed), CONST64(0x53380d139d95b3df),
    CONST64(0x650a73548baf63de), CONST64(0x766a0abb3c77b2a8), CONST64(0x81c2c92e47edaee6), CONST64(0x92722c851482353b),
    CONST64(0xa2bfe8a14cf10364), CONST64(0xa81a664bbc423001), CONST64(0xc24b8b70d0f89791), CONST64(0xc76c51a30654be30),
    CONST64(0xd192e819d6ef5218), CONST64(0xd69906245565a910), CONST64(0xf40e35855771202a), CONST64(0x106aa07032bbd1b8),
    CONST64(0x19a4c116b8d2d0c8), CONST64(0x1e376c085141ab53), CONST64(0x2748774cdf8eeb99), CONST64(0x34b0bcb5e19b48a8),
    CONST64(0x391c0cb3c5c95a63), CONST64(0x4ed8aa4ae3418acb), CONST64(0x5b9cca4f7763e373), CONST64(0x682e6ff3d6b2b8a3),
    CONST64(0x748f82ee5defb2fc), CONST64(0x78a5636f43172f60), CONST64(0x84c87814a1f0ab72), CONST64(0x8cc702081a6439ec),
    CONST64(0x90befffa23631e28), CONST64(0xa4506cebde82bde9), CONST64(0xbef9a3f7b2c67915), CONST64(0xc67178f2e372532b),
    CONST64(0xca273eceea26619c), CONST64(0xd186b8c721c0c207), CONST64(0xeada7dd6cde0eb1e), CONST64(0xf57d4f7fee6ed178),
    CONST64(0x06f067aa72176fba), CONST64(0x0a637dc5a2c898a6), CONST64(0x113f9804bef90dae), CONST64(0x1b710b35131c471b),
    CONST64(0x28db77f523047d84), CONST64(0x32caab7b40c72493), CONST64(0x3c9ebe0a15c9bebc), CONST64(0x431d67c49c100d4c),
    CONST64(0x4cc5d4becb3e42b6), CONST64(0x597f299cfc657e2a), CONST64(0x5fcb6fab3ad6faec), CONST64(0x6c44198c4a475817)
};


static void sha512_compress(LqCryptHash * md, unsigned char *buf) {
    uint64_t S[8], W[80], t0, t1;
    int i;

    for(i = 0; i < 8; i++)
        S[i] = md->sha512.state[i];
    for(i = 0; i < 16; i++)
        LOAD64H(W[i], buf + (8 * i));
    for(i = 16; i < 80; i++)
        W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];

#define RND(a,b,c,d,e,f,g,h,i)                    \
     t0 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];   \
     t1 = Sigma0(a) + Maj(a, b, c);                  \
     d += t0;                                        \
     h  = t0 + t1;

    for(i = 0; i < 80; i += 8) {
        RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i + 0);
        RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], i + 1);
        RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], i + 2);
        RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], i + 3);
        RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], i + 4);
        RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], i + 5);
        RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], i + 6);
        RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], i + 7);
    }
    for(i = 0; i < 8; i++)
        md->sha512.state[i] = md->sha512.state[i] + S[i];
}

static bool LqCryptSha512Init(LqCryptHash * md) {
    md->methods = &sha512_desc;
    md->sha512.curlen = 0;
    md->sha512.length = 0;
    md->sha512.state[0] = CONST64(0x6a09e667f3bcc908);
    md->sha512.state[1] = CONST64(0xbb67ae8584caa73b);
    md->sha512.state[2] = CONST64(0x3c6ef372fe94f82b);
    md->sha512.state[3] = CONST64(0xa54ff53a5f1d36f1);
    md->sha512.state[4] = CONST64(0x510e527fade682d1);
    md->sha512.state[5] = CONST64(0x9b05688c2b3e6c1f);
    md->sha512.state[6] = CONST64(0x1f83d9abfb41bd6b);
    md->sha512.state[7] = CONST64(0x5be0cd19137e2179);
    return true;
}

HASH_PROCESS(LqCryptSha512Update, sha512_compress, sha512, 128)

static intptr_t LqCryptSha512Final(LqCryptHash * md, unsigned char *out) {
    int i;

    md->sha512.length += md->sha512.curlen * CONST64(8);
    md->sha512.buf[md->sha512.curlen++] = (unsigned char)0x80;
    if(md->sha512.curlen > 112) {
        while(md->sha512.curlen < 128)
            md->sha512.buf[md->sha512.curlen++] = (unsigned char)0;
        sha512_compress(md, md->sha512.buf);
        md->sha512.curlen = 0;
    }
    while(md->sha512.curlen < 120)
        md->sha512.buf[md->sha512.curlen++] = (unsigned char)0;
    STORE64H(md->sha512.length, md->sha512.buf + 120);
    sha512_compress(md, md->sha512.buf);
    for(i = 0; i < 8; i++)
        STORE64H(md->sha512.state[i], out + (8 * i));
    return 64;
}

static void LqCryptSha512Copy(LqCryptHash *Dest, LqCryptHash *Source) {
    memcpy(&Dest->sha512, &Source->sha512, sizeof(Source->sha512));
}

static bool LqCryptCbcDecrypt(LqCryptCipher *cbc, const unsigned char *ct, unsigned char *pt, intptr_t len) {
    intptr_t x;
    const intptr_t BlockLen = cbc->symmetric_cbc.key.methods->block_length;
    unsigned char tmp[16];
    LTC_FAST_TYPE tmpy;
    if(len % BlockLen) {
        lq_errno_set(EINVAL);
        return false;
    }
    while(len) {
        if(!cbc->symmetric_cbc.key.methods->Decrypt((LqCryptCipher*)&cbc->symmetric_cbc.key, ct, tmp, BlockLen))
            return false;
        for(x = 0; x < BlockLen; x += sizeof(LTC_FAST_TYPE)) {
            tmpy = *((LTC_FAST_TYPE*)((unsigned char*)cbc->symmetric_cbc.IV + x)) ^ *((LTC_FAST_TYPE*)((unsigned char*)tmp + x));
            *((LTC_FAST_TYPE*)((unsigned char*)cbc->symmetric_cbc.IV + x)) = *((LTC_FAST_TYPE*)((unsigned char*)ct + x));
            *((LTC_FAST_TYPE*)((unsigned char*)pt + x)) = tmpy;
        }
        ct += BlockLen;
        pt += BlockLen;
        len -= BlockLen;
    }
    return true;
}

static bool LqCryptCbcEncrypt(LqCryptCipher *cbc, const unsigned char *pt, unsigned char *ct, intptr_t len) {
    intptr_t x;
    const intptr_t BlockLen = cbc->symmetric_cbc.key.methods->block_length;
    LTC_FAST_TYPE* a, *m = (LTC_FAST_TYPE*)(cbc->symmetric_cbc.IV + BlockLen);
    const LTC_FAST_TYPE* b;
    if(len % BlockLen) {
        lq_errno_set(EINVAL);
        return false;
    }
    while(len) {
        for(a = ((LTC_FAST_TYPE*)cbc->symmetric_cbc.IV), b = ((const LTC_FAST_TYPE*)pt); a < m; a++, b++)
            *a ^= *b;
        cbc->symmetric_cbc.key.methods->Encrypt((LqCryptCipher*)&cbc->symmetric_cbc.key, cbc->symmetric_cbc.IV, ct, BlockLen);
        memcpy(cbc->symmetric_cbc.IV, ct, BlockLen);
        ct += BlockLen;
        pt += BlockLen;
        len -= BlockLen;
    }
    return true;
}

static intptr_t LqCryptCbcBlockLen(LqCryptCipher* cbc) {
    return cbc->symmetric_cbc.key.methods->block_length;
}

static void LqCryptCbcCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->symmetric_cbc, &Source->symmetric_cbc, sizeof(Source->symmetric_cbc) - sizeof(_LqCryptSymmetricKey));
    Source->symmetric_cbc.key.methods->Copy((LqCryptCipher *)&Dest->symmetric_cbc.key, (LqCryptCipher *)&Source->symmetric_cbc.key);
}

static void LqCryptCtrCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->symmetric_ctr, &Source->symmetric_ctr, sizeof(Source->symmetric_ctr) - sizeof(_LqCryptSymmetricKey));
    Source->symmetric_ctr.key.methods->Copy((LqCryptCipher *)&Dest->symmetric_ctr.key, (LqCryptCipher *)&Source->symmetric_ctr.key);
}

static void LqCryptOfbCopy(LqCryptCipher *Dest, LqCryptCipher *Source) {
    memcpy(&Dest->symmetric_ofb, &Source->symmetric_ofb, sizeof(Source->symmetric_ofb) - sizeof(_LqCryptSymmetricKey));
    Source->symmetric_ofb.key.methods->Copy((LqCryptCipher *)&Dest->symmetric_ofb.key, (LqCryptCipher *)&Source->symmetric_ofb.key);
}


static bool LqCryptCbcInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags) {
    if(!Methods->Init((LqCryptCipher*)&skey->symmetric_cbc.key, NULL, NULL, key, keylen, num_rounds, 0))
        return false;
    skey->methods = &cbc_desc;
    memcpy(skey->symmetric_cbc.IV, IV, Methods->block_length);
    return true;
}

/* CTR crypt block mode */

static inline void LqCryptCtrIncrement(LqCryptCipher* skey) {
    const intptr_t BlockLen = skey->symmetric_ctr.key.methods->block_length;
    register unsigned char *Ctr, *mCtr = skey->symmetric_ctr.ctr + BlockLen;
    if(skey->symmetric_ctr.mode == CTR_COUNTER_LITTLE_ENDIAN) {
        /* little-endian */
        for(Ctr = skey->symmetric_ctr.ctr; Ctr < mCtr; Ctr++) {
            *Ctr = (*Ctr + (unsigned char)1) & (unsigned char)255;
            if(*Ctr != (unsigned char)0)
                break;
        }
    } else {
        /* big-endian */
        for(Ctr = mCtr - 1; Ctr >= skey->symmetric_ctr.ctr; Ctr--) {
            *Ctr = (*Ctr + (unsigned char)1) & (unsigned char)255;
            if(*Ctr != (unsigned char)0)
                break;
        }
    }
}

static bool LqCryptCtrEncrypt(LqCryptCipher *ctr, const unsigned char *pt, unsigned char *ct, intptr_t len) {
    const intptr_t BlockLen = ctr->symmetric_ctr.key.methods->block_length;
    register unsigned char *Pad2 = ctr->symmetric_ctr.pad + ctr->symmetric_ctr.padlen, *mPad = ctr->symmetric_ctr.pad + BlockLen, *mCtr = ctr->symmetric_ctr.ctr + BlockLen;
    register intptr_t l = len;

    while(l) {
        if(Pad2 == mPad) {
            LqCryptCtrIncrement(ctr);
            ctr->symmetric_ctr.key.methods->Encrypt(
                (LqCryptCipher*)&ctr->symmetric_ctr.key,
                ctr->symmetric_ctr.ctr,
                ctr->symmetric_ctr.pad,
                BlockLen
            );
            Pad2 = ctr->symmetric_ctr.pad;
        }
        *ct++ = *pt++ ^ *(Pad2++);
        --l;
    }
    ctr->symmetric_ctr.padlen = Pad2 - ctr->symmetric_ctr.pad;
    return true;
}

static bool LqCryptCtrDecrypt(LqCryptCipher *ctr, const unsigned char *ct, unsigned char *pt, intptr_t len) {
    return LqCryptCtrEncrypt(ctr, ct, pt, len);
}

static bool LqCryptCtrInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags) {
    const intptr_t BlockLen = Methods->block_length;

    if(!Methods->Init((LqCryptCipher*)&skey->symmetric_ctr.key,  NULL, NULL, key, keylen, num_rounds, 0))
        return false;
    skey->methods = &ctr_desc;
    skey->symmetric_ctr.padlen = 0;
    skey->symmetric_ctr.mode = Flags & 1;
    memcpy(skey->symmetric_ctr.IV, IV, BlockLen);
    memcpy(skey->symmetric_ctr.ctr, IV, BlockLen);
    if(Flags & LTC_CTR_RFC3686)
        LqCryptCtrIncrement(skey);
    return skey->symmetric_ctr.key.methods->Encrypt(
        (LqCryptCipher*)&skey->symmetric_ctr.key,
        skey->symmetric_ctr.ctr,
        skey->symmetric_ctr.pad, 
        BlockLen
    );
}

static void LqCryptCtrSeek(LqCryptCipher *ctr, int64_t NewOffset) {
    const intptr_t BlockLen = ctr->symmetric_ctr.key.methods->block_length;
    register unsigned char
        *Ctr,
        *mCtr = ctr->symmetric_ctr.ctr + BlockLen,
        *IV;
    uint64_t BlockOffset = NewOffset / BlockLen;
    uint32_t v;
    if(ctr->symmetric_ctr.mode == CTR_COUNTER_LITTLE_ENDIAN) {
        /* little-endian */
        Ctr = ctr->symmetric_ctr.ctr;
        IV = ctr->symmetric_ctr.IV;
        v = 0;
        for(; Ctr < mCtr; Ctr++, IV++) {
            v += (((uint32_t)*IV) + (uint32_t)(BlockOffset & 0xffULL));
            *Ctr = v & 0xffUL;
            v = v >> 8;
            BlockOffset = BlockOffset >> 8;
        }
    } else {
        /* big-endian */
        Ctr = mCtr - 1;
        IV = ctr->symmetric_ctr.IV + BlockLen;
        v = 0;
        for(; Ctr >= ctr->symmetric_ctr.ctr; Ctr--, IV--) {
            v += (((uint32_t)*IV) + (uint32_t)(BlockOffset & 0xffULL));
            *Ctr = v & 0xffUL;
            v = v >> 8;
            BlockOffset = BlockOffset >> 8;
        }
    }
    ctr->symmetric_ctr.key.methods->Encrypt(
        (LqCryptCipher*)&ctr->symmetric_ctr.key,
        ctr->symmetric_ctr.ctr,
        ctr->symmetric_ctr.pad,
        BlockLen
    );
    ctr->symmetric_ctr.padlen = NewOffset % BlockLen;
}

/* CTR crypt block mode */

/* OFB crypt block mode */

static bool LqCryptOfbInit(LqCryptCipher* skey, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags) {
    memcpy(skey->symmetric_ofb.IV, IV, Methods->block_length);
    skey->methods = &ofb_desc;
    skey->symmetric_ofb.padlen = Methods->block_length;
    return Methods->Init((LqCryptCipher*)&(skey->symmetric_ofb.key), NULL, NULL, key, keylen, num_rounds, Flags);
}

static bool LqCryptOfbEncrypt(LqCryptCipher* skey, const unsigned char *pt, unsigned char *ct, intptr_t len) {
    const intptr_t BlockLen = skey->symmetric_ofb.key.methods->block_length;
    while(len-- > 0) {
        if(skey->symmetric_ofb.padlen == BlockLen) {
            LqCryptCipherEncrypt((LqCryptCipher*)&skey->symmetric_ofb.key, skey->symmetric_ofb.IV, skey->symmetric_ofb.IV, BlockLen);
            skey->symmetric_ofb.padlen = 0;
        }
        *ct++ = *pt++ ^ skey->symmetric_ofb.IV[(skey->symmetric_ofb.padlen)++];
    }
    return true;
}

static bool LqCryptOfbDecrypt(LqCryptCipher *ctr, const unsigned char *ct, unsigned char *pt, intptr_t len) {
    return LqCryptOfbEncrypt(ctr, ct, pt, len);
}

/* Hmac */
static bool LqCryptHmacUpdate(LqCryptMac *hmac, const unsigned char *in, intptr_t inlen) {
    return hmac->hmac.md.methods->process(&hmac->hmac.md, in, inlen);
}

static intptr_t LqCryptHmacFinal(LqCryptMac *hmac, unsigned char *out) {
    unsigned char isha[128], buf[128];
    unsigned long hashsize, i;
    intptr_t err = -((intptr_t)1);
    const LqCryptHashMethods* Methods = hmac->hmac.md.methods;
    hashsize = Methods->hashsize;
    if(Methods->done(&hmac->hmac.md, isha) <= (intptr_t)0)
        goto lblErr;
    for(i = 0; i < Methods->blocksize; i++)
        buf[i] = hmac->hmac.key[i] ^ 0x5C;
    if(!Methods->init(&hmac->hmac.md))
        goto lblErr;
    if(!Methods->process(&hmac->hmac.md, buf, Methods->blocksize))
        goto lblErr;
    if(!Methods->process(&hmac->hmac.md, isha, hashsize))
        goto lblErr;
    if(Methods->done(&hmac->hmac.md, out) <= (intptr_t)0)
        goto lblErr;
    err = hashsize;
lblErr:
    LqMemFree(hmac->hmac.key);
    return err;
}

static bool LqCryptHmacInit(LqCryptMac* hmac, const void* HashMethods, const unsigned char *key, size_t keylen) {
    unsigned char buf[128];
    unsigned long hashsize;
    unsigned long i;
    bool err = false;
    LqCryptHash md;
    hmac->hmac.Methods = &hmac_desc;
    hashsize = ((LqCryptHashMethods*)HashMethods)->hashsize;
    if((hmac->hmac.key = (unsigned char*)LqMemAlloc(((LqCryptHashMethods*)HashMethods)->blocksize)) == NULL) {
        lq_errno_set(ENOMEM);
        return false;
    }
    if(keylen > ((LqCryptHashMethods*)HashMethods)->blocksize) {
        if(((LqCryptHashMethods*)HashMethods)->blocksize < ((LqCryptHashMethods*)HashMethods)->hashsize)
            goto LBL_ERR;
        if(!((LqCryptHashMethods*)HashMethods)->init(&md))
            goto LBL_ERR;
        if(!((LqCryptHashMethods*)HashMethods)->process(&md, key, keylen))
            goto LBL_ERR;
        if(((LqCryptHashMethods*)HashMethods)->done(&md, hmac->hmac.key) <= ((intptr_t)0))
            goto LBL_ERR;
        if(hashsize < ((LqCryptHashMethods*)HashMethods)->blocksize)
            memset((hmac->hmac.key) + hashsize, 0, (size_t)(((LqCryptHashMethods*)HashMethods)->blocksize - hashsize));
        keylen = hashsize;
    } else {
        memcpy(hmac->hmac.key, key, (size_t)keylen);
        if(keylen < ((LqCryptHashMethods*)HashMethods)->blocksize)
            memset((hmac->hmac.key) + keylen, 0, (size_t)(((LqCryptHashMethods*)HashMethods)->blocksize - keylen));
    }
    for(i = 0; i < ((LqCryptHashMethods*)HashMethods)->blocksize; i++)
        buf[i] = hmac->hmac.key[i] ^ 0x36;
    if(!((LqCryptHashMethods*)HashMethods)->init(&hmac->hmac.md))
        goto LBL_ERR;
    if(!((LqCryptHashMethods*)HashMethods)->process(&hmac->hmac.md, buf, ((LqCryptHashMethods*)HashMethods)->blocksize))
        goto LBL_ERR;
    err = true;
    goto done;
LBL_ERR:
    LqMemFree(hmac->hmac.key);
done:
    return err;
}

static void LqCryptHmacCopy(LqCryptMac *Dest, LqCryptMac *Source) {
    Source->hmac.md.methods->Copy(&Dest->hmac.md, &Source->hmac.md);
    Dest->hmac.key = (unsigned char*)LqMemAlloc(Source->hmac.md.methods->blocksize);
    memcpy(Dest->hmac.key, Source->hmac.key, Source->hmac.md.methods->blocksize);
    Dest->hmac.Methods = Source->hmac.Methods;
}

static intptr_t LqCryptHmacResLen(LqCryptMac* ctx) {
    return ctx->hmac.md.methods->hashsize;
}

/* Hmac */

/* Pmac */

static const struct {
    int           len;
    unsigned char poly_div[LQCRYPT_MAXBLOCKSIZE], poly_mul[LQCRYPT_MAXBLOCKSIZE];
} polys[] = {
    {
        8,
        {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B}
    },{
        16,
        {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87}
    }
};

static bool LqCryptPmacInit(LqCryptMac* pmac, const void* CipherMethods, const unsigned char *key, size_t keylen) {
    int poly, x, y, m;
    bool err = false;
    unsigned char L[128];

    const intptr_t block_len = ((LqCryptCipherMethods*)CipherMethods)->block_length;
    /* determine which polys to use */
    pmac->Methods = &pmac_desc;
    for(poly = 0; poly < (int)(sizeof(polys) / sizeof(polys[0])); poly++) {
        if(polys[poly].len == block_len) {
            break;
        }
    }
    if(polys[poly].len != block_len) {
        lq_errno_set(EINVAL);
        return false;
    }
    if(!((LqCryptCipherMethods*)CipherMethods)->Init(&pmac->pmac.key, NULL, NULL, key, keylen, 0, 0))
        return false;
    memset(L, 0, block_len);
    pmac->pmac.key.methods->Encrypt(&pmac->pmac.key, L, L, block_len);
    memcpy(pmac->pmac.Ls[0], L, block_len);
    for(x = 1; x < 32; x++) {
        m = pmac->pmac.Ls[x - 1][0] >> 7;
        for(y = 0; y < block_len - 1; y++) {
            pmac->pmac.Ls[x][y] = ((pmac->pmac.Ls[x - 1][y] << 1) | (pmac->pmac.Ls[x - 1][y + 1] >> 7)) & 255;
        }
        pmac->pmac.Ls[x][block_len - 1] = (pmac->pmac.Ls[x - 1][block_len - 1] << 1) & 255;
        if(m == 1) {
            for(y = 0; y < block_len; y++)
                pmac->pmac.Ls[x][y] ^= polys[poly].poly_mul[y];
        }
    }
    m = L[block_len - 1] & 1;
    for(x = block_len - 1; x > 0; x--)
        pmac->pmac.Lr[x] = ((L[x] >> 1) | (L[x - 1] << 7)) & 255;
    pmac->pmac.Lr[0] = L[0] >> 1;
    if(m == 1) {
        for(x = 0; x < block_len; x++) 
            pmac->pmac.Lr[x] ^= polys[poly].poly_div[x];
    }
    pmac->pmac.block_index = 1;
    pmac->pmac.buflen = 0;
    memset(pmac->pmac.block, 0, sizeof(pmac->pmac.block));
    memset(pmac->pmac.Li, 0, sizeof(pmac->pmac.Li));
    memset(pmac->pmac.checksum, 0, sizeof(pmac->pmac.checksum));
    err = true;
error:
    return err;
}

static inline int pmac_ntz(unsigned long x) {
    int c;
    x &= 0xFFFFFFFFUL;
    c = 0;
    while((x & 1) == 0) {
        ++c;
        x >>= 1;
    }
    return c;
}

static inline void pmac_shift_xor(LqCryptMac* pmac, const intptr_t block_len) {
    intptr_t x, y;
    y = pmac_ntz(pmac->pmac.block_index++);
    for(x = 0; x < block_len; x++)
        pmac->pmac.Li[x] ^= pmac->pmac.Ls[y][x];
}

static bool LqCryptPmacUpdate(LqCryptMac* pmac, const unsigned char *in, intptr_t inlen) {
    int n;
    intptr_t x;
    unsigned char Z[LQCRYPT_MAXBLOCKSIZE];

    const intptr_t block_len = LqCryptCipherLenBlock(&pmac->pmac.key);
    while(inlen != 0) {
        if(pmac->pmac.buflen == block_len) {
            pmac_shift_xor(pmac, block_len);
            for(x = 0; x < block_len; x++)
                Z[x] = pmac->pmac.Li[x] ^ pmac->pmac.block[x];
            LqCryptCipherEncrypt(&pmac->pmac.key, Z, Z, block_len);
            for(x = 0; x < block_len; x++)
                pmac->pmac.checksum[x] ^= Z[x];
            pmac->pmac.buflen = 0;
        }
        n = lq_min(inlen, (unsigned long)(block_len - pmac->pmac.buflen));
        memcpy(pmac->pmac.block + pmac->pmac.buflen, in, n);
        pmac->pmac.buflen += n;
        inlen -= n;
        in += n;
    }
    return true;
}

static intptr_t LqCryptPmacFinal(LqCryptMac *pmac, unsigned char *out) {
    int x;
    const intptr_t block_len = LqCryptCipherLenBlock(&pmac->pmac.key);
    if(pmac->pmac.buflen == block_len) {
        for(x = 0; x < block_len; x++)
            pmac->pmac.checksum[x] ^= pmac->pmac.block[x] ^ pmac->pmac.Lr[x];
    } else {
        for(x = 0; x < pmac->pmac.buflen; x++)
            pmac->pmac.checksum[x] ^= pmac->pmac.block[x];
        pmac->pmac.checksum[x] ^= 0x80;
    }
    LqCryptCipherEncrypt(&pmac->pmac.key, pmac->pmac.checksum, pmac->pmac.checksum, block_len);
    memcpy(out, pmac->pmac.checksum, block_len);
    return block_len;
}

static void LqCryptPmacCopy(LqCryptMac *Dest, LqCryptMac *Source) {
    memcpy(&Dest->pmac, &Source->pmac, sizeof(Source->pmac));
    LqCryptCipherCopy(&Dest->pmac.key, &Source->pmac.key);
}

static intptr_t LqCryptPmacResLen(LqCryptMac* ctx) {
    return LqCryptCipherLenBlock(&ctx->pmac.key);
}

/* Pmac */

/* Omac */

static bool LqCryptOmacInit(LqCryptMac *omac, const void* CipherMethods, const unsigned char *key, size_t keylen) {
    int err, x, y, mask, msb, len;

    const intptr_t block_len = ((LqCryptCipherMethods*)CipherMethods)->block_length;
    omac->Methods = &omac_desc;
    switch(block_len) {
        case 8:  mask = 0x1B; len = 8; break;
        case 16: mask = 0x87; len = 16; break;
        default: lq_errno_set(EINVAL); return false;
    }

    if(!((LqCryptCipherMethods*)CipherMethods)->Init(&omac->omac.key, NULL, NULL, key, keylen, 0, 0))
        return false;
    memset(omac->omac.Lu[0], 0, block_len);
    LqCryptCipherEncrypt(&omac->omac.key, omac->omac.Lu[0], omac->omac.Lu[0], block_len);
    for(x = 0; x < 2; x++) {
        msb = omac->omac.Lu[x][0] >> 7;
        for(y = 0; y < (len - 1); y++) 
            omac->omac.Lu[x][y] = ((omac->omac.Lu[x][y] << 1) | (omac->omac.Lu[x][y + 1] >> 7)) & 255;
        omac->omac.Lu[x][len - 1] = ((omac->omac.Lu[x][len - 1] << 1) ^ (msb ? mask : 0)) & 255;
        if(x == 0)
            memcpy(omac->omac.Lu[1], omac->omac.Lu[0], sizeof(omac->omac.Lu[0]));
    }
    omac->omac.buflen = 0;
    memset(omac->omac.prev, 0, sizeof(omac->omac.prev));
    memset(omac->omac.block, 0, sizeof(omac->omac.block));
    return true;
}

static bool LqCryptOmacUpdate(LqCryptMac* omac, const unsigned char *in, intptr_t inlen) {
    unsigned long n, x;
    const intptr_t block_len = LqCryptCipherLenBlock(&omac->omac.key);

    while(inlen != 0) {
        if(omac->omac.buflen == block_len) {
            for(x = 0; x < block_len; x++)
                omac->omac.block[x] ^= omac->omac.prev[x];
            LqCryptCipherEncrypt(&omac->omac.key, omac->omac.block, omac->omac.prev, block_len);
            omac->omac.buflen = 0;
        }
        n = lq_min(inlen, (unsigned long)(block_len - omac->omac.buflen));
        memcpy(omac->omac.block + omac->omac.buflen, in, n);
        omac->omac.buflen += n;
        inlen -= n;
        in += n;
    }
    return true;
}

static intptr_t LqCryptOmacFinal(LqCryptMac *omac, unsigned char *out) {
    int       mode;
    intptr_t  x;
    const intptr_t block_len = LqCryptCipherLenBlock(&omac->omac.key);
    if(omac->omac.buflen != block_len) {
        omac->omac.block[omac->omac.buflen++] = 0x80;
        while(omac->omac.buflen < block_len)
            omac->omac.block[omac->omac.buflen++] = 0x00;
        mode = 1;
    } else {
        mode = 0;
    }
    for(x = 0; x < block_len; x++) 
        omac->omac.block[x] ^= omac->omac.prev[x] ^ omac->omac.Lu[mode][x];
    LqCryptCipherEncrypt(&omac->omac.key, omac->omac.block, omac->omac.block, block_len);
    memcpy(out, omac->omac.block, block_len);
    return block_len;
}

static void LqCryptOmacCopy(LqCryptMac *Dest, LqCryptMac *Source) {
    memcpy(&Dest->omac, &Source->omac, sizeof(Source->omac));
    LqCryptCipherCopy(&Dest->omac.key, &Source->omac.key);
}

static intptr_t LqCryptOmacResLen(LqCryptMac* ctx) {
    return LqCryptCipherLenBlock(&ctx->omac.key);
}

/* Omac */

/* start xcbc */

static bool LqCryptXcbcInit(LqCryptMac* xcbc, const void* CipherMethods, const unsigned char *key, size_t keylen) {
    int            x, y;
    bool err;
    LqCryptCipher  skey;
    const intptr_t block_len = ((LqCryptCipherMethods*)CipherMethods)->block_length;
    xcbc->Methods = &xcbc_desc;
    if(!((LqCryptCipherMethods*)CipherMethods)->Init(&skey, NULL, NULL, key, keylen, 0, 0))
        return false;
    for(y = 0; y < 3; y++) {
        for(x = 0; x < block_len; x++)
            xcbc->xcbc.K[y][x] = y + 1;
        LqCryptCipherEncrypt(&skey, xcbc->xcbc.K[y], xcbc->xcbc.K[y], block_len);
    }
    err = ((LqCryptCipherMethods*)CipherMethods)->Init(&xcbc->xcbc.key, NULL, NULL, xcbc->xcbc.K[0], block_len, 0, 0);
    memset(xcbc->xcbc.IV, 0, block_len);
    xcbc->xcbc.buflen = 0;
    return err;
}

static bool LqCryptXcbcUpdate(LqCryptMac* xcbc, const unsigned char *in, intptr_t inlen) {
    const intptr_t block_len = LqCryptCipherLenBlock(&xcbc->xcbc.key);
    while(inlen) {
        if(xcbc->xcbc.buflen == block_len) {
            LqCryptCipherEncrypt(&xcbc->xcbc.key, xcbc->xcbc.IV, xcbc->xcbc.IV, block_len);
            xcbc->xcbc.buflen = 0;
        }
        xcbc->xcbc.IV[xcbc->xcbc.buflen++] ^= *in++;
        --inlen;
    }
    return true;
}

static intptr_t LqCryptXcbcFinal(LqCryptMac *xcbc, unsigned char *out) {
    int err;
    intptr_t x;
    const intptr_t block_len = LqCryptCipherLenBlock(&xcbc->xcbc.key);
    if(xcbc->xcbc.buflen == block_len) {
        for(x = 0; x < block_len; x++)
            xcbc->xcbc.IV[x] ^= xcbc->xcbc.K[1][x];
    } else {
        xcbc->xcbc.IV[xcbc->xcbc.buflen] ^= 0x80;
        for(x = 0; x < block_len; x++)
            xcbc->xcbc.IV[x] ^= xcbc->xcbc.K[2][x];
    }
    LqCryptCipherEncrypt(&xcbc->xcbc.key, xcbc->xcbc.IV, xcbc->xcbc.IV, block_len);
    for(x = 0; x < block_len; x++)
        out[x] = xcbc->xcbc.IV[x];
    return block_len;
}

static void LqCryptXcbcCopy(LqCryptMac *Dest, LqCryptMac *Source) {
    memcpy(&Dest->xcbc, &Source->xcbc, sizeof(Source->xcbc));
    LqCryptCipherCopy(&Dest->xcbc.key, &Source->xcbc.key);
}

static intptr_t LqCryptXcbcResLen(LqCryptMac* ctx) {
    return LqCryptCipherLenBlock(&ctx->xcbc.key);
}

/* end xcbc */

/* start rsa */


#define ciL    (sizeof(mbedtls_mpi_uint))         /* chars in limb  */
#define biL    (ciL << 3)               /* bits  in limb  */
#define biH    (ciL << 2)               /* half limb size */

#define MPI_SIZE_T_MAX  ( (size_t) -1 ) /* SIZE_T_MAX is not standard */

#define MBEDTLS_ERR_MPI_FILE_IO_ERROR                     -0x0002  /**< An error occurred while reading from or writing to a file. */
#define MBEDTLS_ERR_MPI_BAD_INPUT_DATA                    -0x0004  /**< Bad input parameters to function. */
#define MBEDTLS_ERR_MPI_INVALID_CHARACTER                 -0x0006  /**< There is an invalid character in the digit string. */
#define MBEDTLS_ERR_MPI_BUFFER_TOO_SMALL                  -0x0008  /**< The buffer is too small to write to. */
#define MBEDTLS_ERR_MPI_NEGATIVE_VALUE                    -0x000A  /**< The input arguments are negative or result in illegal output. */
#define MBEDTLS_ERR_MPI_DIVISION_BY_ZERO                  -0x000C  /**< The input argument for division is zero, which is not allowed. */
#define MBEDTLS_ERR_MPI_NOT_ACCEPTABLE                    -0x000E  /**< The input arguments are not acceptable. */
#define MBEDTLS_ERR_MPI_ALLOC_FAILED                      -0x0010  /**< Memory allocation failed. */

#define MBEDTLS_MPI_MAX_LIMBS                             10000
#define MBEDTLS_MPI_MAX_SIZE                              1024     /**< Maximum number of bytes for usable MPIs. */
#define BITS_TO_LIMBS(i)  ( (i) / biL + ( (i) % biL != 0 ) )
#define CHARS_TO_LIMBS(i) ( (i) / ciL + ( (i) % ciL != 0 ) )


#if !defined(MBEDTLS_MPI_WINDOW_SIZE)
#define MBEDTLS_MPI_WINDOW_SIZE                           6        /**< Maximum windows size used. */
#endif 

static void mbedtls_mpi_gcd(LqCryptRsaNumber *G, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B);

static void mbedtls_mpi_init(LqCryptRsaNumber *X) {
    if(X == NULL)
        return;
    X->s = 1;
    X->n = 0;
    X->p = NULL;
}

static void mbedtls_mpi_grow(LqCryptRsaNumber *X, size_t nblimbs) {
    if(X->n < nblimbs) {
        size_t t = nblimbs * ciL;
        X->p = (mbedtls_mpi_uint*)LqMemRealloc(X->p, t);
        memset(X->p + X->n, 0, t - (X->n * ciL));
        X->n = nblimbs;
    }
}

static void mbedtls_mpi_free(LqCryptRsaNumber *X) {
    if(X == NULL)
        return;
    if(X->p != NULL) {
        memset(X->p, 0, X->n * sizeof(mbedtls_mpi_uint));
        LqMemFree(X->p);
    }
    X->s = 1;
    X->n = 0;
    X->p = NULL;
}

static void mbedtls_mpi_lset(LqCryptRsaNumber *X, mbedtls_mpi_sint z) {
    mbedtls_mpi_grow(X, 1);
    memset(X->p, 0, X->n * ciL);
    X->p[0] = (z < 0) ? -z : z;
    X->s = (z < 0) ? -1 : 1;
}

static void mbedtls_mpi_read_binary(LqCryptRsaNumber *X, const unsigned char *buf, size_t buflen) {
    size_t i, j, n;
    for(n = 0; n < buflen; n++)
        if(buf[n] != 0)
            break;
    mbedtls_mpi_grow(X, CHARS_TO_LIMBS(buflen - n));
    mbedtls_mpi_lset(X, 0);
    for(i = buflen, j = 0; i > n; i--, j++)
        X->p[j / ciL] |= ((mbedtls_mpi_uint)buf[i - 1]) << ((j % ciL) << 3);
}

static bool mbedtls_mpi_fill_random(LqCryptRsaNumber *X, size_t size, int(*f_rng)(void *, unsigned char *, size_t), void *p_rng) {
    unsigned char buf[MBEDTLS_MPI_MAX_SIZE];
    if(size > MBEDTLS_MPI_MAX_SIZE)
        return false;
    f_rng(p_rng, buf, size);
    mbedtls_mpi_read_binary(X, buf, size);
    return true;
}

static size_t mbedtls_clz(const mbedtls_mpi_uint x) {
    size_t j;
    mbedtls_mpi_uint mask = (mbedtls_mpi_uint)1 << (biL - 1);
    for(j = 0; j < biL; j++) {
        if(x & mask) break;
        mask >>= 1;
    }
    return j;
}

static size_t mbedtls_mpi_bitlen(const LqCryptRsaNumber *X) {
    size_t i, j;
    if(X->n == 0)
        return(0);
    for(i = X->n - 1; i > 0; i--)
        if(X->p[i] != 0)
            break;
    j = biL - mbedtls_clz(X->p[i]);
    return((i * biL) + j);
}

static void mbedtls_mpi_shift_r(LqCryptRsaNumber *X, size_t count) {
    size_t i, v0, v1;
    mbedtls_mpi_uint r0 = 0, r1;
    v0 = count / biL;
    v1 = count & (biL - 1);
    if(v0 > X->n || (v0 == X->n && v1 > 0)) { mbedtls_mpi_lset(X, 0); return; }
    if(v0 > 0) {
        for(i = 0; i < X->n - v0; i++)
            X->p[i] = X->p[i + v0];
        for(; i < X->n; i++)
            X->p[i] = 0;
    }
    if(v1 > 0) {
        for(i = X->n; i > 0; i--) {
            r1 = X->p[i - 1] << (biL - v1);
            X->p[i - 1] >>= v1;
            X->p[i - 1] |= r0;
            r0 = r1;
        }
    }
}

static void mbedtls_mpi_set_bit(LqCryptRsaNumber *X, size_t pos, unsigned char val) {
    int ret = 0;
    size_t off = pos / biL;
    size_t idx = pos % biL;
    if(X->n * biL <= pos) {
        if(val == 0) return;
        mbedtls_mpi_grow(X, off + 1);
    }
    X->p[off] &= ~((mbedtls_mpi_uint)0x01 << idx);
    X->p[off] |= (mbedtls_mpi_uint)val << idx;
}

static int mbedtls_mpi_cmp_mpi(const LqCryptRsaNumber *X, const LqCryptRsaNumber *Y) {
    size_t i, j;
    for(i = X->n; i > 0; i--)
        if(X->p[i - 1] != 0)
            break;
    for(j = Y->n; j > 0; j--)
        if(Y->p[j - 1] != 0)
            break;
    if(i == 0 && j == 0)
        return(0);
    if(i > j) return(X->s);
    if(j > i) return(-Y->s);
    if(X->s > 0 && Y->s < 0) return(1);
    if(Y->s > 0 && X->s < 0) return(-1);
    for(; i > 0; i--) {
        if(X->p[i - 1] > Y->p[i - 1]) return(X->s);
        if(X->p[i - 1] < Y->p[i - 1]) return(-X->s);
    }
    return(0);
}

static int mbedtls_mpi_cmp_int(const LqCryptRsaNumber *X, mbedtls_mpi_sint z) {
    LqCryptRsaNumber Y;
    mbedtls_mpi_uint p[1];
    *p = (z < 0) ? -z : z;
    Y.s = (z < 0) ? -1 : 1;
    Y.n = 1; Y.p = p;
    return(mbedtls_mpi_cmp_mpi(X, &Y));
}

LQ_ALIGN(128) static const int small_prime[] = {
    3,    5,    7,   11,   13,   17,   19,   23,
    29,   31,   37,   41,   43,   47,   53,   59,
    61,   67,   71,   73,   79,   83,   89,   97,
    101,  103,  107,  109,  113,  127,  131,  137,
    139,  149,  151,  157,  163,  167,  173,  179,
    181,  191,  193,  197,  199,  211,  223,  227,
    229,  233,  239,  241,  251,  257,  263,  269,
    271,  277,  281,  283,  293,  307,  311,  313,
    317,  331,  337,  347,  349,  353,  359,  367,
    373,  379,  383,  389,  397,  401,  409,  419,
    421,  431,  433,  439,  443,  449,  457,  461,
    463,  467,  479,  487,  491,  499,  503,  509,
    521,  523,  541,  547,  557,  563,  569,  571,
    577,  587,  593,  599,  601,  607,  613,  617,
    619,  631,  641,  643,  647,  653,  659,  661,
    673,  677,  683,  691,  701,  709,  719,  727,
    733,  739,  743,  751,  757,  761,  769,  773,
    787,  797,  809,  811,  821,  823,  827,  829,
    839,  853,  857,  859,  863,  877,  881,  883,
    887,  907,  911,  919,  929,  937,  941,  947,
    953,  967,  971,  977,  983,  991,  997, -103
};

static void mbedtls_mpi_mod_int(mbedtls_mpi_uint *r, const LqCryptRsaNumber *A, mbedtls_mpi_sint b) {
    size_t i;
    mbedtls_mpi_uint x, y, z;
    if(b == 1) {
        *r = 0;
        return;
    }
    if(b == 2) {
        *r = A->p[0] & 1;
        return;
    }
    for(i = A->n, y = 0; i > 0; i--) {
        x = A->p[i - 1];
        y = (y << biH) | (x >> biH);
        z = y / b;
        y -= z * b;
        x <<= biH;
        y = (y << biH) | (x >> biH);
        z = y / b;
        y -= z * b;
    }
    if(A->s < 0 && y != 0)
        y = b - y;
    *r = y;
}

static int mpi_check_small_factors(const LqCryptRsaNumber *X) {
    size_t i;
    mbedtls_mpi_uint r;
    if((X->p[0] & 1) == 0)
        return(MBEDTLS_ERR_MPI_NOT_ACCEPTABLE);
    for(i = 0; small_prime[i] > 0; i++) {
        if(mbedtls_mpi_cmp_int(X, small_prime[i]) <= 0)
            return(1);
        mbedtls_mpi_mod_int(&r, X, small_prime[i]);
        if(r == 0)
            return(MBEDTLS_ERR_MPI_NOT_ACCEPTABLE);
    }
    return 0;
}

static int mbedtls_mpi_cmp_abs(const LqCryptRsaNumber *X, const LqCryptRsaNumber *Y) {
    size_t i, j;
    for(i = X->n; i > 0; i--)
        if(X->p[i - 1] != 0)
            break;
    for(j = Y->n; j > 0; j--)
        if(Y->p[j - 1] != 0)
            break;
    if(i == 0 && j == 0)
        return(0);
    if(i > j) return(1);
    if(j > i) return(-1);
    for(; i > 0; i--) {
        if(X->p[i - 1] > Y->p[i - 1]) return(1);
        if(X->p[i - 1] < Y->p[i - 1]) return(-1);
    }
    return(0);
}

static void mbedtls_mpi_copy(LqCryptRsaNumber *X, const LqCryptRsaNumber *Y) {
    size_t i;
    if(X == Y)
        return;
    if(Y->p == NULL) {
        mbedtls_mpi_free(X);
        return;
    }
    for(i = Y->n - 1; i > 0; i--)
        if(Y->p[i] != 0)
            break;
    i++;
    X->s = Y->s;
    mbedtls_mpi_grow(X, i);
    memset(X->p, 0, X->n * ciL);
    memcpy(X->p, Y->p, i * ciL);
}

static void mpi_sub_hlp(size_t n, mbedtls_mpi_uint *s, mbedtls_mpi_uint *d) {
    size_t i;
    mbedtls_mpi_uint c, z;
    for(i = c = 0; i < n; i++, s++, d++) {
        z = (*d <  c);     *d -= c;
        c = (*d < *s) + z; *d -= *s;
    }
    while(c != 0) {
        z = (*d < c); *d -= c;
        c = z; i++; d++;
    }
}

static void mbedtls_mpi_sub_abs(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    LqCryptRsaNumber TB;
    size_t n;
    //if(mbedtls_mpi_cmp_abs(A, B) < 0)
    //  return(MBEDTLS_ERR_MPI_NEGATIVE_VALUE);
    mbedtls_mpi_init(&TB);
    if(X == B) {
        mbedtls_mpi_copy(&TB, B);
        B = &TB;
    }
    if(X != A)
        mbedtls_mpi_copy(X, A);
    X->s = 1;
    for(n = B->n; n > 0; n--)
        if(B->p[n - 1] != 0)
            break;
    mpi_sub_hlp(n, B->p, X->p);
    mbedtls_mpi_free(&TB);
}

static void mbedtls_mpi_add_abs(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    size_t i, j;
    mbedtls_mpi_uint *o, *p, c, tmp;
    if(X == B) { const LqCryptRsaNumber *T = A; A = X; B = T; }
    if(X != A) mbedtls_mpi_copy(X, A);
    X->s = 1;
    for(j = B->n; j > 0; j--)
        if(B->p[j - 1] != 0)
            break;
    mbedtls_mpi_grow(X, j);
    o = B->p; p = X->p; c = 0;
    for(i = 0; i < j; i++, o++, p++) {
        tmp = *o;
        *p += c; c = (*p <  c);
        *p += tmp; c += (*p < tmp);
    }
    while(c != 0) {
        if(i >= X->n) {
            mbedtls_mpi_grow(X, i + 1);
            p = X->p + i;
        }
        *p += c; c = (*p < c); i++; p++;
    }
}

static void mbedtls_mpi_sub_mpi(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    int s = A->s;
    if(A->s * B->s > 0) {
        if(mbedtls_mpi_cmp_abs(A, B) >= 0) {
            mbedtls_mpi_sub_abs(X, A, B);
            X->s = s;
        } else {
            mbedtls_mpi_sub_abs(X, B, A);
            X->s = -s;
        }
    } else {
        mbedtls_mpi_add_abs(X, A, B);
        X->s = s;
    }
}

static void mbedtls_mpi_sub_int(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, mbedtls_mpi_sint b) {
    LqCryptRsaNumber B;
    mbedtls_mpi_uint p[1];
    p[0] = (b < 0) ? -b : b;
    B.s = (b < 0) ? -1 : 1;
    B.n = 1; B.p = p;
    mbedtls_mpi_sub_mpi(X, A, &B);
}

static size_t mbedtls_mpi_lsb(const LqCryptRsaNumber *X) {
    size_t i, j, count = 0;
    for(i = 0; i < X->n; i++)
        for(j = 0; j < biL; j++, count++)
            if(((X->p[i] >> j) & 1) != 0)
                return(count);
    return(0);
}

static void mpi_montg_init(mbedtls_mpi_uint *mm, const LqCryptRsaNumber *N) {
    mbedtls_mpi_uint x, m0 = N->p[0];
    unsigned int i;
    x = m0;
    x += ((m0 + 2) & 4) << 1;
    for(i = biL; i >= 8; i /= 2)
        x *= (2 - (m0 * x));
    *mm = ~x + 1;
}

static void mbedtls_mpi_shift_l(LqCryptRsaNumber *X, size_t count) {
    size_t i, v0, t1;
    mbedtls_mpi_uint r0 = 0, r1;
    v0 = count / (biL);
    t1 = count & (biL - 1);
    i = mbedtls_mpi_bitlen(X) + count;
    if(X->n * biL < i)
        mbedtls_mpi_grow(X, BITS_TO_LIMBS(i));
    if(v0 > 0) {
        for(i = X->n; i > v0; i--)
            X->p[i - 1] = X->p[i - v0 - 1];
        for(; i > 0; i--)
            X->p[i - 1] = 0;
    }
    if(t1 > 0) {
        for(i = v0; i < X->n; i++) {
            r1 = X->p[i] >> (biL - t1);
            X->p[i] <<= t1;
            X->p[i] |= r0;
            r0 = r1;
        }
    }
}

#if !defined(MULADDC_CORE)
#if defined(MBEDTLS_HAVE_UDBL)

#define MULADDC_INIT                    \
{                                       \
    mbedtls_t_udbl r;                           \
    mbedtls_mpi_uint r0, r1;

#define MULADDC_CORE                    \
    r   = *(s++) * (mbedtls_t_udbl) b;          \
    r0  = (mbedtls_mpi_uint) r;                   \
    r1  = (mbedtls_mpi_uint)( r >> biL );         \
    r0 += c;  r1 += (r0 <  c);          \
    r0 += *d; r1 += (r0 < *d);          \
    c = r1; *(d++) = r0;

#define MULADDC_STOP                    \
}

#else
#define MULADDC_INIT                    \
{                                       \
    mbedtls_mpi_uint s0, s1, b0, b1;              \
    mbedtls_mpi_uint r0, r1, rx, ry;              \
    b0 = ( b << biH ) >> biH;           \
    b1 = ( b >> biH );

#define MULADDC_CORE                    \
    s0 = ( *s << biH ) >> biH;          \
    s1 = ( *s >> biH ); s++;            \
    rx = s0 * b1; r0 = s0 * b0;         \
    ry = s1 * b0; r1 = s1 * b1;         \
    r1 += ( rx >> biH );                \
    r1 += ( ry >> biH );                \
    rx <<= biH; ry <<= biH;             \
    r0 += rx; r1 += (r0 < rx);          \
    r0 += ry; r1 += (r0 < ry);          \
    r0 +=  c; r1 += (r0 <  c);          \
    r0 += *d; r1 += (r0 < *d);          \
    c = r1; *(d++) = r0;

#define MULADDC_STOP                    \
}

#endif /* C (generic)  */
#endif /* C (longlong) */

static void mpi_mul_hlp(size_t i, mbedtls_mpi_uint *s, mbedtls_mpi_uint *d, mbedtls_mpi_uint b) { 
    mbedtls_mpi_uint c = 0, t = 0;
#if defined(MULADDC_HUIT)
    for(; i >= 8; i -= 8) {
        MULADDC_INIT
            MULADDC_HUIT
            MULADDC_STOP
    }

    for(; i > 0; i--) {
        MULADDC_INIT
        MULADDC_CORE
        MULADDC_STOP
    }
#else /* MULADDC_HUIT */
    for(; i >= 16; i -= 16) {
        MULADDC_INIT
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_STOP
    }
    for(; i >= 8; i -= 8) {
        MULADDC_INIT
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_STOP
    }
    for(; i > 0; i--) {
        MULADDC_INIT
        MULADDC_CORE
        MULADDC_STOP
    }
#endif /* MULADDC_HUIT */
    t++;
    do {
        *d += c; c = (*d < c); d++;
    } while(c != 0);
}


static void mbedtls_mpi_mul_mpi(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    int ret;
    size_t i, j;
    LqCryptRsaNumber TA, TB;
    mbedtls_mpi_init(&TA); mbedtls_mpi_init(&TB);
    if(X == A) { mbedtls_mpi_copy(&TA, A); A = &TA; }
    if(X == B) { mbedtls_mpi_copy(&TB, B); B = &TB; }
    for(i = A->n; i > 0; i--)
        if(A->p[i - 1] != 0)
            break;
    for(j = B->n; j > 0; j--)
        if(B->p[j - 1] != 0)
            break;
    mbedtls_mpi_grow(X, i + j);
    mbedtls_mpi_lset(X, 0);
    for(i++; j > 0; j--)
        mpi_mul_hlp(i - 1, A->p, X->p + j - 1, B->p[j - 1]);
    X->s = A->s * B->s;
    mbedtls_mpi_free(&TB); mbedtls_mpi_free(&TA);
}

static void mbedtls_mpi_mul_int(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, mbedtls_mpi_uint b) {
    LqCryptRsaNumber B;
    mbedtls_mpi_uint p[1];
    B.s = 1;
    B.n = 1;
    B.p = p;
    p[0] = b;
    mbedtls_mpi_mul_mpi(X, A, &B);
}

static mbedtls_mpi_uint mbedtls_int_div_int(mbedtls_mpi_uint u1,
                                            mbedtls_mpi_uint u0, mbedtls_mpi_uint d, mbedtls_mpi_uint *r) {
#if defined(MBEDTLS_HAVE_UDBL)
    mbedtls_t_udbl dividend, quotient;
#else
    const mbedtls_mpi_uint radix = (mbedtls_mpi_uint)1 << biH;
    const mbedtls_mpi_uint uint_halfword_mask = ((mbedtls_mpi_uint)1 << biH) - 1;
    mbedtls_mpi_uint d0, d1, q0, q1, rAX, r0, quotient;
    mbedtls_mpi_uint u0_msw, u0_lsw;
    size_t s;
#endif
    if(0 == d || u1 >= d) {
        if(r != NULL) *r = ~0;

        return (~0);
    }
#if defined(MBEDTLS_HAVE_UDBL)
    dividend = (mbedtls_t_udbl)u1 << biL;
    dividend |= (mbedtls_t_udbl)u0;
    quotient = dividend / d;
    if(quotient > ((mbedtls_t_udbl)1 << biL) - 1)
        quotient = ((mbedtls_t_udbl)1 << biL) - 1;

    if(r != NULL)
        *r = (mbedtls_mpi_uint)(dividend - (quotient * d));

    return (mbedtls_mpi_uint)quotient;
#else
    s = mbedtls_clz(d);
    d = d << s;
    u1 = u1 << s;
    u1 |= (u0 >> (biL - s)) & (-(mbedtls_mpi_sint)s >> (biL - 1));
    u0 = u0 << s;
    d1 = d >> biH;
    d0 = d & uint_halfword_mask;
    u0_msw = u0 >> biH;
    u0_lsw = u0 & uint_halfword_mask;
    q1 = u1 / d1;
    r0 = u1 - d1 * q1;
    while(q1 >= radix || (q1 * d0 > radix * r0 + u0_msw)) {
        q1 -= 1;
        r0 += d1;
        if(r0 >= radix) break;
    }
    rAX = (u1 * radix) + (u0_msw - q1 * d);
    q0 = rAX / d1;
    r0 = rAX - q0 * d1;
    while(q0 >= radix || (q0 * d0 > radix * r0 + u0_lsw)) {
        q0 -= 1;
        r0 += d1;
        if(r0 >= radix) break;
    }
    if(r != NULL)
        *r = (rAX * radix + u0_lsw - q0 * d) >> s;
    quotient = q1 * radix + q0;
    return quotient;
#endif
}

static void mbedtls_mpi_add_mpi(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    int s = A->s;
    if(A->s * B->s < 0) {
        if(mbedtls_mpi_cmp_abs(A, B) >= 0) {
            mbedtls_mpi_sub_abs(X, A, B);
            X->s = s;
        } else {
            mbedtls_mpi_sub_abs(X, B, A);
            X->s = -s;
        }
    } else {
        mbedtls_mpi_add_abs(X, A, B);
        X->s = s;
    }
}

static void mbedtls_mpi_div_mpi(LqCryptRsaNumber *Q, LqCryptRsaNumber *R, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    size_t i, n, t, k;
    LqCryptRsaNumber X, Y, Z, T1, T2;

    mbedtls_mpi_init(&X); mbedtls_mpi_init(&Y); mbedtls_mpi_init(&Z);
    mbedtls_mpi_init(&T1); mbedtls_mpi_init(&T2);
    if(mbedtls_mpi_cmp_abs(A, B) < 0) {
        if(Q != NULL) mbedtls_mpi_lset(Q, 0);
        if(R != NULL) mbedtls_mpi_copy(R, A);
        return;
    }
    mbedtls_mpi_copy(&X, A);
    mbedtls_mpi_copy(&Y, B);
    X.s = Y.s = 1;
    mbedtls_mpi_grow(&Z, A->n + 2);
    mbedtls_mpi_lset(&Z, 0);
    mbedtls_mpi_grow(&T1, 2);
    mbedtls_mpi_grow(&T2, 3);
    k = mbedtls_mpi_bitlen(&Y) % biL;
    if(k < biL - 1) {
        k = biL - 1 - k;
        mbedtls_mpi_shift_l(&X, k);
        mbedtls_mpi_shift_l(&Y, k);
    } else k = 0;
    n = X.n - 1;
    t = Y.n - 1;
    mbedtls_mpi_shift_l(&Y, biL * (n - t));
    while(mbedtls_mpi_cmp_mpi(&X, &Y) >= 0) {
        Z.p[n - t]++;
        mbedtls_mpi_sub_mpi(&X, &X, &Y);
    }
    mbedtls_mpi_shift_r(&Y, biL * (n - t));
    for(i = n; i > t; i--) {
        if(X.p[i] >= Y.p[t])
            Z.p[i - t - 1] = ~0;
        else {
            Z.p[i - t - 1] = mbedtls_int_div_int(X.p[i], X.p[i - 1], Y.p[t], NULL);
        }
        Z.p[i - t - 1]++;
        do {
            Z.p[i - t - 1]--;
            mbedtls_mpi_lset(&T1, 0);
            T1.p[0] = (t < 1) ? 0 : Y.p[t - 1];
            T1.p[1] = Y.p[t];
            mbedtls_mpi_mul_int(&T1, &T1, Z.p[i - t - 1]);
            mbedtls_mpi_lset(&T2, 0);
            T2.p[0] = (i < 2) ? 0 : X.p[i - 2];
            T2.p[1] = (i < 1) ? 0 : X.p[i - 1];
            T2.p[2] = X.p[i];
        } while(mbedtls_mpi_cmp_mpi(&T1, &T2) > 0);
        mbedtls_mpi_mul_int(&T1, &Y, Z.p[i - t - 1]);
        mbedtls_mpi_shift_l(&T1, biL * (i - t - 1));
        mbedtls_mpi_sub_mpi(&X, &X, &T1);
        if(mbedtls_mpi_cmp_int(&X, 0) < 0) {
            mbedtls_mpi_copy(&T1, &Y);
            mbedtls_mpi_shift_l(&T1, biL * (i - t - 1));
            mbedtls_mpi_add_mpi(&X, &X, &T1);
            Z.p[i - t - 1]--;
        }
    }
    if(Q != NULL) {
        mbedtls_mpi_copy(Q, &Z);
        Q->s = A->s * B->s;
    }
    if(R != NULL) {
        mbedtls_mpi_shift_r(&X, k);
        X.s = A->s;
        mbedtls_mpi_copy(R, &X);
        if(mbedtls_mpi_cmp_int(R, 0) == 0)
            R->s = 1;
    }
    mbedtls_mpi_free(&X); mbedtls_mpi_free(&Y); mbedtls_mpi_free(&Z);
    mbedtls_mpi_free(&T1); mbedtls_mpi_free(&T2);
}

static void mbedtls_mpi_mod_mpi(LqCryptRsaNumber *R, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    mbedtls_mpi_div_mpi(NULL, R, A, B);
    while(mbedtls_mpi_cmp_int(R, 0) < 0)
        mbedtls_mpi_add_mpi(R, R, B);
    while(mbedtls_mpi_cmp_mpi(R, B) >= 0)
        mbedtls_mpi_sub_mpi(R, R, B);
}

static void mpi_montmul(LqCryptRsaNumber *A, const LqCryptRsaNumber *B, const LqCryptRsaNumber *N, mbedtls_mpi_uint mm, const LqCryptRsaNumber *T) {
    size_t i, n, m;
    mbedtls_mpi_uint u0, u1, *d;
    memset(T->p, 0, T->n * ciL);
    d = T->p;
    n = N->n;
    m = (B->n < n) ? B->n : n;
    for(i = 0; i < n; i++) {
        u0 = A->p[i];
        u1 = (d[0] + u0 * B->p[0]) * mm;
        mpi_mul_hlp(m, B->p, d, u0);
        mpi_mul_hlp(n, N->p, d, u1);
        *d++ = u0; d[n + 1] = 0;
    }
    memcpy(A->p, d, (n + 1) * ciL);
    if(mbedtls_mpi_cmp_abs(A, N) >= 0)
        mpi_sub_hlp(n, N->p, A->p);
    else
        mpi_sub_hlp(n, A->p, T->p);
}

static void mpi_montred(LqCryptRsaNumber *A, const LqCryptRsaNumber *N, mbedtls_mpi_uint mm, const LqCryptRsaNumber *T) {
    mbedtls_mpi_uint z = 1;
    LqCryptRsaNumber U;
    U.n = U.s = (int)z;
    U.p = &z;
    mpi_montmul(A, &U, N, mm, T);
}

static void mbedtls_mpi_exp_mod(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *E, const LqCryptRsaNumber *N, LqCryptRsaNumber *_RR) {
    size_t wbits, wsize, one = 1;
    size_t i, j, nblimbs;
    size_t bufsize, nbits;
    mbedtls_mpi_uint ei, mm, state;
    LqCryptRsaNumber RR, T, W[2 << MBEDTLS_MPI_WINDOW_SIZE], Apos;
    int neg;
    mpi_montg_init(&mm, N);
    mbedtls_mpi_init(&RR); mbedtls_mpi_init(&T);
    mbedtls_mpi_init(&Apos);
    memset(W, 0, sizeof(W));
    i = mbedtls_mpi_bitlen(E);
    wsize = (i > 671) ? 6 : (i > 239) ? 5 :
        (i >  79) ? 4 : (i >  23) ? 3 : 1;
    if(wsize > MBEDTLS_MPI_WINDOW_SIZE)
        wsize = MBEDTLS_MPI_WINDOW_SIZE;
    j = N->n + 1;
    mbedtls_mpi_grow(X, j);
    mbedtls_mpi_grow(&W[1], j);
    mbedtls_mpi_grow(&T, j * 2);
    neg = (A->s == -1);
    if(neg) {
        mbedtls_mpi_copy(&Apos, A);
        Apos.s = 1;
        A = &Apos;
    }
    if(_RR == NULL || _RR->p == NULL) {
        mbedtls_mpi_lset(&RR, 1);
        mbedtls_mpi_shift_l(&RR, N->n * 2 * biL);
        mbedtls_mpi_mod_mpi(&RR, &RR, N);
        if(_RR != NULL)
            memcpy(_RR, &RR, sizeof(LqCryptRsaNumber));
    } else
        memcpy(&RR, _RR, sizeof(LqCryptRsaNumber));
    if(mbedtls_mpi_cmp_mpi(A, N) >= 0)
        mbedtls_mpi_mod_mpi(&W[1], A, N);
    else
        mbedtls_mpi_copy(&W[1], A);
    mpi_montmul(&W[1], &RR, N, mm, &T);
    mbedtls_mpi_copy(X, &RR);
    mpi_montred(X, N, mm, &T);
    if(wsize > 1) {
        j = one << (wsize - 1);
        mbedtls_mpi_grow(&W[j], N->n + 1);
        mbedtls_mpi_copy(&W[j], &W[1]);
        for(i = 0; i < wsize - 1; i++)
            mpi_montmul(&W[j], &W[j], N, mm, &T);
        for(i = j + 1; i < (one << wsize); i++) {
            mbedtls_mpi_grow(&W[i], N->n + 1);
            mbedtls_mpi_copy(&W[i], &W[i - 1]);
            mpi_montmul(&W[i], &W[1], N, mm, &T);
        }
    }
    nblimbs = E->n;
    bufsize = wbits = nbits = 0;
    state = 0;
    while(1) {
        if(bufsize == 0) {
            if(nblimbs == 0)
                break;
            nblimbs--;
            bufsize = sizeof(mbedtls_mpi_uint) << 3;
        }
        bufsize--;
        ei = (E->p[nblimbs] >> bufsize) & 1;
        if(ei == 0 && state == 0)
            continue;
        if(ei == 0 && state == 1) {
            mpi_montmul(X, X, N, mm, &T);
            continue;
        }
        state = 2;
        nbits++;
        wbits |= (ei << (wsize - nbits));
        if(nbits == wsize) {
            for(i = 0; i < wsize; i++)
                mpi_montmul(X, X, N, mm, &T);
            mpi_montmul(X, &W[wbits], N, mm, &T);
            state--;
            wbits = nbits = 0;
        }
    }
    for(i = 0; i < nbits; i++) {
        mpi_montmul(X, X, N, mm, &T);
        wbits <<= 1;
        if((wbits & (one << wsize)) != 0)
            mpi_montmul(X, &W[1], N, mm, &T);
    }
    mpi_montred(X, N, mm, &T);
    if(neg) {
        X->s = -1;
        mbedtls_mpi_add_mpi(X, N, X);
    }
    for(i = (one << (wsize - 1)); i < (one << wsize); i++)
        mbedtls_mpi_free(&W[i]);
    mbedtls_mpi_free(&W[1]); mbedtls_mpi_free(&T); mbedtls_mpi_free(&Apos);
    if(_RR == NULL || _RR->p == NULL)
        mbedtls_mpi_free(&RR);
}

static int mpi_miller_rabin(const LqCryptRsaNumber *X, int(*f_rng)(void *, unsigned char *, size_t), void *p_rng) {
    int ret, count;
    size_t i, j, k, n, s;
    LqCryptRsaNumber W, R, T, A, RR;
    mbedtls_mpi_init(&W); mbedtls_mpi_init(&R); mbedtls_mpi_init(&T); mbedtls_mpi_init(&A);
    mbedtls_mpi_init(&RR);
    mbedtls_mpi_sub_int(&W, X, 1);
    s = mbedtls_mpi_lsb(&W);
    mbedtls_mpi_copy(&R, &W);
    mbedtls_mpi_shift_r(&R, s);
    i = mbedtls_mpi_bitlen(X);
    n = ((i >= 1300) ? 2 : (i >= 850) ? 3 :
        (i >= 650) ? 4 : (i >= 350) ? 8 :
         (i >= 250) ? 12 : (i >= 150) ? 18 : 27);
    for(i = 0; i < n; i++) {
        mbedtls_mpi_fill_random(&A, X->n * ciL, f_rng, p_rng);
        if(mbedtls_mpi_cmp_mpi(&A, &W) >= 0) {
            j = mbedtls_mpi_bitlen(&A) - mbedtls_mpi_bitlen(&W);
            mbedtls_mpi_shift_r(&A, j + 1);
        }
        A.p[0] |= 3;
        count = 0;
        do {
            mbedtls_mpi_fill_random(&A, X->n * ciL, f_rng, p_rng);
            j = mbedtls_mpi_bitlen(&A);
            k = mbedtls_mpi_bitlen(&W);
            if(j > k) mbedtls_mpi_shift_r(&A, j - k);
            if(count++ > 30) {
                ret = MBEDTLS_ERR_MPI_NOT_ACCEPTABLE;
                goto clear;
            }
        } while((mbedtls_mpi_cmp_mpi(&A, &W) >= 0) || (mbedtls_mpi_cmp_int(&A, 1) <= 0));
        mbedtls_mpi_exp_mod(&A, &A, &R, X, &RR);
        if((mbedtls_mpi_cmp_mpi(&A, &W) == 0) || (mbedtls_mpi_cmp_int(&A, 1) == 0))
            continue;
        j = 1;
        while(j < s && mbedtls_mpi_cmp_mpi(&A, &W) != 0) {
            mbedtls_mpi_mul_mpi(&T, &A, &A);
            mbedtls_mpi_mod_mpi(&A, &T, X);
            if(mbedtls_mpi_cmp_int(&A, 1) == 0) break;
            j++;
        }
        if((mbedtls_mpi_cmp_mpi(&A, &W) != 0) || (mbedtls_mpi_cmp_int(&A, 1) == 0)) {
            ret = MBEDTLS_ERR_MPI_NOT_ACCEPTABLE;
            goto clear;
        }
    }
    ret = 0;
clear:
    mbedtls_mpi_free(&W); mbedtls_mpi_free(&R); mbedtls_mpi_free(&T); mbedtls_mpi_free(&A);
    mbedtls_mpi_free(&RR);
    return(ret);
}

static int mbedtls_mpi_is_prime(const LqCryptRsaNumber *X, int(*f_rng)(void *, unsigned char *, size_t), void *p_rng) {
    int ret;
    LqCryptRsaNumber XX;
    XX.s = 1;
    XX.n = X->n;
    XX.p = X->p;
    if(mbedtls_mpi_cmp_int(&XX, 0) == 0 ||  mbedtls_mpi_cmp_int(&XX, 1) == 0)
        return(MBEDTLS_ERR_MPI_NOT_ACCEPTABLE);
    if(mbedtls_mpi_cmp_int(&XX, 2) == 0)
        return(0);
    if((ret = mpi_check_small_factors(&XX)) != 0) {
        if(ret == 1)
            return(0);
        return(ret);
    }
    return(mpi_miller_rabin(&XX, f_rng, p_rng));
}

static void mbedtls_mpi_add_int(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, mbedtls_mpi_sint b) {
    LqCryptRsaNumber B;
    mbedtls_mpi_uint p[1];
    p[0] = (b < 0) ? -b : b;
    B.s = (b < 0) ? -1 : 1;
    B.n = 1;
    B.p = p;
    mbedtls_mpi_add_mpi(X, A, &B);
}

static void mbedtls_mpi_inv_mod(LqCryptRsaNumber *X, const LqCryptRsaNumber *A, const LqCryptRsaNumber *N) {
    LqCryptRsaNumber G, TA, TU, U1, U2, TB, TV, V1, V2;
    mbedtls_mpi_init(&TA); mbedtls_mpi_init(&TU); mbedtls_mpi_init(&U1); mbedtls_mpi_init(&U2);
    mbedtls_mpi_init(&G); mbedtls_mpi_init(&TB); mbedtls_mpi_init(&TV);
    mbedtls_mpi_init(&V1); mbedtls_mpi_init(&V2);
    mbedtls_mpi_gcd(&G, A, N);
    mbedtls_mpi_mod_mpi(&TA, A, N);mbedtls_mpi_copy(&TU, &TA);
    mbedtls_mpi_copy(&TB, N);mbedtls_mpi_copy(&TV, N);
    mbedtls_mpi_lset(&U1, 1);mbedtls_mpi_lset(&U2, 0);
    mbedtls_mpi_lset(&V1, 0);mbedtls_mpi_lset(&V2, 1);
    do {
        while((TU.p[0] & 1) == 0) {
            mbedtls_mpi_shift_r(&TU, 1);
            if((U1.p[0] & 1) != 0 || (U2.p[0] & 1) != 0) {
                mbedtls_mpi_add_mpi(&U1, &U1, &TB); mbedtls_mpi_sub_mpi(&U2, &U2, &TA);
            }
            mbedtls_mpi_shift_r(&U1, 1); mbedtls_mpi_shift_r(&U2, 1);
        }
        while((TV.p[0] & 1) == 0) {
            mbedtls_mpi_shift_r(&TV, 1);
            if((V1.p[0] & 1) != 0 || (V2.p[0] & 1) != 0) {
                mbedtls_mpi_add_mpi(&V1, &V1, &TB);
                mbedtls_mpi_sub_mpi(&V2, &V2, &TA);
            }
            mbedtls_mpi_shift_r(&V1, 1);
            mbedtls_mpi_shift_r(&V2, 1);
        }
        if(mbedtls_mpi_cmp_mpi(&TU, &TV) >= 0) {
            mbedtls_mpi_sub_mpi(&TU, &TU, &TV);mbedtls_mpi_sub_mpi(&U1, &U1, &V1);
            mbedtls_mpi_sub_mpi(&U2, &U2, &V2);
        } else {
            mbedtls_mpi_sub_mpi(&TV, &TV, &TU);mbedtls_mpi_sub_mpi(&V1, &V1, &U1);
            mbedtls_mpi_sub_mpi(&V2, &V2, &U2);
        }
    } while(mbedtls_mpi_cmp_int(&TU, 0) != 0);
    while(mbedtls_mpi_cmp_int(&V1, 0) < 0)
        mbedtls_mpi_add_mpi(&V1, &V1, N);
    while(mbedtls_mpi_cmp_mpi(&V1, N) >= 0)
        mbedtls_mpi_sub_mpi(&V1, &V1, N);
    mbedtls_mpi_copy(X, &V1);
    mbedtls_mpi_free(&TA); mbedtls_mpi_free(&TU); mbedtls_mpi_free(&U1); mbedtls_mpi_free(&U2);
    mbedtls_mpi_free(&G); mbedtls_mpi_free(&TB); mbedtls_mpi_free(&TV);
    mbedtls_mpi_free(&V1); mbedtls_mpi_free(&V2);
}

static void mbedtls_mpi_gcd(LqCryptRsaNumber *G, const LqCryptRsaNumber *A, const LqCryptRsaNumber *B) {
    size_t lz, lzt;
    LqCryptRsaNumber TG, TA, TB;
    mbedtls_mpi_init(&TG); mbedtls_mpi_init(&TA); mbedtls_mpi_init(&TB);
    mbedtls_mpi_copy(&TA, A);
    mbedtls_mpi_copy(&TB, B);
    lz = mbedtls_mpi_lsb(&TA);
    lzt = mbedtls_mpi_lsb(&TB);
    if(lzt < lz)
        lz = lzt;
    mbedtls_mpi_shift_r(&TA, lz);
    mbedtls_mpi_shift_r(&TB, lz);
    TA.s = TB.s = 1;
    while(mbedtls_mpi_cmp_int(&TA, 0) != 0) {
        mbedtls_mpi_shift_r(&TA, mbedtls_mpi_lsb(&TA));
        mbedtls_mpi_shift_r(&TB, mbedtls_mpi_lsb(&TB));

        if(mbedtls_mpi_cmp_mpi(&TA, &TB) >= 0) {
            mbedtls_mpi_sub_abs(&TA, &TA, &TB);
            mbedtls_mpi_shift_r(&TA, 1);
        } else {
            mbedtls_mpi_sub_abs(&TB, &TB, &TA);
            mbedtls_mpi_shift_r(&TB, 1);
        }
    }
    mbedtls_mpi_shift_l(&TB, lz);
    mbedtls_mpi_copy(G, &TB);
    mbedtls_mpi_free(&TG); mbedtls_mpi_free(&TA); mbedtls_mpi_free(&TB);
}

int mbedtls_mpi_gen_prime(LqCryptRsaNumber *X, size_t nbits, int dh_flag,int(*f_rng)(void *, unsigned char *, size_t),void *p_rng) {
    int ret;
    size_t k, n;
    mbedtls_mpi_uint r;
    LqCryptRsaNumber Y;
    mbedtls_mpi_init(&Y);
    n = BITS_TO_LIMBS(nbits);
    mbedtls_mpi_fill_random(X, n * ciL, f_rng, p_rng);
    k = mbedtls_mpi_bitlen(X);
    if(k > nbits) mbedtls_mpi_shift_r(X, k - nbits + 1);
    mbedtls_mpi_set_bit(X, nbits - 1, 1);
    X->p[0] |= 1;
    if(dh_flag == 0) {
        while((ret = mbedtls_mpi_is_prime(X, f_rng, p_rng)) != 0) {
            if(ret != MBEDTLS_ERR_MPI_NOT_ACCEPTABLE)
                goto cleanup;
            mbedtls_mpi_add_int(X, X, 2);
        }
    } else {
        X->p[0] |= 2;
        mbedtls_mpi_mod_int(&r, X, 3);
        if(r == 0)
            mbedtls_mpi_add_int(X, X, 8);
        else if(r == 1)
            mbedtls_mpi_add_int(X, X, 4);
        mbedtls_mpi_copy(&Y, X);
        mbedtls_mpi_shift_r(&Y, 1);
        while(1) {
            if((ret = mpi_check_small_factors(X)) == 0 &&
                (ret = mpi_check_small_factors(&Y)) == 0 &&
               (ret = mpi_miller_rabin(X, f_rng, p_rng)) == 0 &&
               (ret = mpi_miller_rabin(&Y, f_rng, p_rng)) == 0) {
                break;
            }
            if(ret != MBEDTLS_ERR_MPI_NOT_ACCEPTABLE)
                goto cleanup;
            mbedtls_mpi_add_int(X, X, 12);
            mbedtls_mpi_add_int(&Y, &Y, 6);
        }
    }
cleanup:
    mbedtls_mpi_free(&Y);
    return(ret);
}

inline static size_t mbedtls_mpi_size(const LqCryptRsaNumber *X) {
    return((mbedtls_mpi_bitlen(X) + 7) >> 3);
}

static size_t mbedtls_mpi_write_binary(const LqCryptRsaNumber *X, unsigned char *buf, size_t buflen) {
    size_t i, j, n = mbedtls_mpi_size(X);
    memset(buf, 0, buflen);
    for(i = buflen - 1, j = 0; n > 0; i--, j++, n--)
        buf[i] = (unsigned char)(X->p[j / ciL] >> ((j % ciL) << 3));
    return j;
}

static size_t mbedtls_mpi_write_binary2(const LqCryptRsaNumber *X, unsigned char *buf, size_t buflen) {
    intptr_t i, j = mbedtls_mpi_size(X);
    for(i = 0, j = lq_min(buflen, j) - 1; j >= 0; i++, j--)
        buf[i] = (unsigned char)(X->p[j / ciL] >> ((j % ciL) << 3));
    return i;
}


/* End RSA*/


static int LqCryptRsaDefaultRand(void *, unsigned char * DestBuf, size_t Len) {
    LqCryptRnd(DestBuf, Len);
    return 0;
}

LQ_EXTERN_C bool LQ_CALL LqCryptRsaCreateKeys(LqCryptRsa *RsaKeys, int(*f_rng)(void *, unsigned char *, size_t), void *p_rng, unsigned int nbits, int exponent) {
    LqCryptRsaNumber P1, Q1, H, G;
    if(nbits < 128 || exponent < 3)
        return false;
    if(f_rng == NULL)
        f_rng = LqCryptRsaDefaultRand;
    mbedtls_mpi_init(&P1); mbedtls_mpi_init(&Q1);
    mbedtls_mpi_init(&H); mbedtls_mpi_init(&G);
    mbedtls_mpi_init(&RsaKeys->N); mbedtls_mpi_init(&RsaKeys->E);  
    mbedtls_mpi_init(&RsaKeys->D); mbedtls_mpi_init(&RsaKeys->P);
    mbedtls_mpi_init(&RsaKeys->Q); mbedtls_mpi_init(&RsaKeys->DP);
    mbedtls_mpi_init(&RsaKeys->DQ); mbedtls_mpi_init(&RsaKeys->QP);
    mbedtls_mpi_init(&RsaKeys->RN); mbedtls_mpi_init(&RsaKeys->RP);
    mbedtls_mpi_init(&RsaKeys->RQ);
    mbedtls_mpi_lset(&RsaKeys->E, exponent);
    do {
        mbedtls_mpi_gen_prime(&RsaKeys->P, nbits >> 1, 0, f_rng, p_rng);
        if(nbits % 2)
            mbedtls_mpi_gen_prime(&RsaKeys->Q, (nbits >> 1) + 1, 0, f_rng, p_rng);
        else
            mbedtls_mpi_gen_prime(&RsaKeys->Q, nbits >> 1, 0, f_rng, p_rng);
        if(mbedtls_mpi_cmp_mpi(&RsaKeys->P, &RsaKeys->Q) == 0)
            continue;
        mbedtls_mpi_mul_mpi(&RsaKeys->N, &RsaKeys->P, &RsaKeys->Q);
        if(mbedtls_mpi_bitlen(&RsaKeys->N) != nbits)
            continue;
        mbedtls_mpi_sub_int(&P1, &RsaKeys->P, 1);
        mbedtls_mpi_sub_int(&Q1, &RsaKeys->Q, 1);
        mbedtls_mpi_mul_mpi(&H, &P1, &Q1);
        mbedtls_mpi_gcd(&G, &RsaKeys->E, &H);
    } while(mbedtls_mpi_cmp_int(&G, 1) != 0);
    mbedtls_mpi_inv_mod(&RsaKeys->D, &RsaKeys->E, &H);
    mbedtls_mpi_mod_mpi(&RsaKeys->DP, &RsaKeys->D, &P1);
    mbedtls_mpi_mod_mpi(&RsaKeys->DQ, &RsaKeys->D, &Q1);
    mbedtls_mpi_inv_mod(&RsaKeys->QP, &RsaKeys->Q, &RsaKeys->P);
    RsaKeys->block_len = (mbedtls_mpi_bitlen(&RsaKeys->N) + 7) >> 3;
    mbedtls_mpi_free(&P1); mbedtls_mpi_free(&Q1); mbedtls_mpi_free(&H); mbedtls_mpi_free(&G);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqCryptRsaPublicEncDec(LqCryptRsa *RsaKeys, const void *pt, void *ct, size_t Len) {
    bool ret = false;
    LqCryptRsaNumber T;
    if(RsaKeys->E.p == NULL || RsaKeys->N.p == NULL  || ((Len % RsaKeys->block_len) != 0)) {
        lq_errno_set(EINVAL);
        return false;
    }
    mbedtls_mpi_init(&T);
    for(const unsigned char* m = ((unsigned char*)pt) + Len; pt < m; pt = ((unsigned char*)pt) +  RsaKeys->block_len, ct = ((unsigned char*)ct) + RsaKeys->block_len) {
        mbedtls_mpi_read_binary(&T, (unsigned char*)pt, RsaKeys->block_len);
        if(mbedtls_mpi_cmp_mpi(&T, &RsaKeys->N) >= 0)
            goto cleanup;
        mbedtls_mpi_exp_mod(&T, &T, &RsaKeys->E, &RsaKeys->N, &RsaKeys->RN);
        ret = mbedtls_mpi_write_binary(&T, (unsigned char*)ct, RsaKeys->block_len);
    }
    ret = true;
cleanup:
    mbedtls_mpi_free(&T);
    return ret;
}

LQ_EXTERN_C bool LQ_CALL LqCryptRsaPrivateEncDec(
    LqCryptRsa *RsaKeys,
    bool WithoutCrt, 
    const void *pt,
    void *ct,
    size_t Len
) {
    bool ret = false;
    LqCryptRsaNumber T, T1, T2;
    if(RsaKeys->P.p == NULL || RsaKeys->Q.p == NULL || RsaKeys->D.p == NULL || ((Len % RsaKeys->block_len) != 0)) {
        lq_errno_set(EINVAL);
        return false;
    }
    mbedtls_mpi_init(&T); mbedtls_mpi_init(&T1); mbedtls_mpi_init(&T2);
    for(const unsigned char* m = ((unsigned char*)pt) + Len; pt < m; pt = ((unsigned char*)pt) + RsaKeys->block_len, ct = ((unsigned char*)ct) + RsaKeys->block_len) {
        mbedtls_mpi_read_binary(&T, (unsigned char*)pt, RsaKeys->block_len);
        if(mbedtls_mpi_cmp_mpi(&T, &RsaKeys->N) >= 0)
            goto cleanup;
        if(WithoutCrt) {
            mbedtls_mpi_exp_mod(&T, &T, &RsaKeys->D, &RsaKeys->N, &RsaKeys->RN);
        } else {
            mbedtls_mpi_exp_mod(&T1, &T, &RsaKeys->DP, &RsaKeys->P, &RsaKeys->RP);
            mbedtls_mpi_exp_mod(&T2, &T, &RsaKeys->DQ, &RsaKeys->Q, &RsaKeys->RQ);
            mbedtls_mpi_sub_mpi(&T, &T1, &T2);
            mbedtls_mpi_mul_mpi(&T1, &T, &RsaKeys->QP);
            mbedtls_mpi_mod_mpi(&T, &T1, &RsaKeys->P);
            mbedtls_mpi_mul_mpi(&T1, &T, &RsaKeys->Q);
            mbedtls_mpi_add_mpi(&T, &T2, &T1);
        }
        mbedtls_mpi_write_binary(&T, (unsigned char*)ct, RsaKeys->block_len);
    }
    ret = true;
cleanup:
    mbedtls_mpi_free(&T); mbedtls_mpi_free(&T1); mbedtls_mpi_free(&T2);
    return ret;
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptRsaLenBlock(const LqCryptRsa *RsaKeys) {
    return RsaKeys->block_len;
}

static bool mbedtls_rsa_check_pubkey(const LqCryptRsa *RsaKeys) {
    if((RsaKeys->N.p[0] & 1) == 0 ||
        (RsaKeys->E.p[0] & 1) == 0)
        return false;
    if(mbedtls_mpi_bitlen(&RsaKeys->E) < 2 ||
       mbedtls_mpi_cmp_mpi(&RsaKeys->E, &RsaKeys->N) >= 0)
        return false;
    return true;
}

static bool mbedtls_rsa_check_privkey(const LqCryptRsa *RsaKeys) {
    int ret = true;
    LqCryptRsaNumber PQ, DE, P1, Q1, H, I, G, G2, L1, L2, DP, DQ, QP;
    mbedtls_mpi_init(&PQ); mbedtls_mpi_init(&DE); mbedtls_mpi_init(&P1); mbedtls_mpi_init(&Q1);
    mbedtls_mpi_init(&H); mbedtls_mpi_init(&I); mbedtls_mpi_init(&G); mbedtls_mpi_init(&G2);
    mbedtls_mpi_init(&L1); mbedtls_mpi_init(&L2); mbedtls_mpi_init(&DP); mbedtls_mpi_init(&DQ);
    mbedtls_mpi_init(&QP);
    mbedtls_mpi_mul_mpi(&PQ, &RsaKeys->P, &RsaKeys->Q);
    mbedtls_mpi_mul_mpi(&DE, &RsaKeys->D, &RsaKeys->E);
    mbedtls_mpi_sub_int(&P1, &RsaKeys->P, 1);
    mbedtls_mpi_sub_int(&Q1, &RsaKeys->Q, 1);
    mbedtls_mpi_mul_mpi(&H, &P1, &Q1);
    mbedtls_mpi_gcd(&G, &RsaKeys->E, &H);
    mbedtls_mpi_gcd(&G2, &P1, &Q1);
    mbedtls_mpi_div_mpi(&L1, &L2, &H, &G2);
    mbedtls_mpi_mod_mpi(&I, &DE, &L1);
    mbedtls_mpi_mod_mpi(&DP, &RsaKeys->D, &P1);
    mbedtls_mpi_mod_mpi(&DQ, &RsaKeys->D, &Q1);
    mbedtls_mpi_inv_mod(&QP, &RsaKeys->Q, &RsaKeys->P);
    if(mbedtls_mpi_cmp_mpi(&PQ, &RsaKeys->N) != 0 ||
       mbedtls_mpi_cmp_mpi(&DP, &RsaKeys->DP) != 0 ||
       mbedtls_mpi_cmp_mpi(&DQ, &RsaKeys->DQ) != 0 ||
       mbedtls_mpi_cmp_mpi(&QP, &RsaKeys->QP) != 0 ||
       mbedtls_mpi_cmp_int(&L2, 0) != 0 ||
       mbedtls_mpi_cmp_int(&I, 1) != 0 ||
       mbedtls_mpi_cmp_int(&G, 1) != 0) {
        ret = false;
    }
    mbedtls_mpi_free(&PQ); mbedtls_mpi_free(&DE); mbedtls_mpi_free(&P1); mbedtls_mpi_free(&Q1);
    mbedtls_mpi_free(&H); mbedtls_mpi_free(&I); mbedtls_mpi_free(&G); mbedtls_mpi_free(&G2);
    mbedtls_mpi_free(&L1); mbedtls_mpi_free(&L2); mbedtls_mpi_free(&DP); mbedtls_mpi_free(&DQ);
    mbedtls_mpi_free(&QP);
    return ret;
}


LQ_EXTERN_C bool LQ_CALL LqCryptRsaImportRaw(
    LqCryptRsa *RsaKeys, 
    /* Public key */ 
    const void* BufE, size_t SizeE,
    /* For public and private key */ 
    const void* BufN, size_t SizeN,
    /* Private keys */
    const void* BufD, size_t SizeD,
    const void* BufP, size_t SizeP,
    const void* BufQ, size_t SizeQ,

    /* Optimal private keys (If it is not specified, it is automatically calculated) */
    const void* BufDP, size_t SizeDP,
    const void* BufDQ, size_t SizeDQ,
    const void* BufQP, size_t SizeQP,
    bool IsCheck
) {
    if(BufN == NULL)
        return false;
    if((BufD != NULL) || (BufP != NULL) || (BufQ != NULL)) {
        if((BufD == NULL) || (BufP == NULL) || (BufQ == NULL))
            return false;
        if((BufDP != NULL) || (BufDQ != NULL) || (BufQP != NULL)) {
            if((BufDP == NULL) || (BufDQ == NULL) || (BufQP == NULL))
                return false;
        }
    } else {
        if(BufE == NULL)
            return false;
    }
    mbedtls_mpi_init(&RsaKeys->N); mbedtls_mpi_init(&RsaKeys->E);
    mbedtls_mpi_init(&RsaKeys->D); mbedtls_mpi_init(&RsaKeys->P);
    mbedtls_mpi_init(&RsaKeys->Q); mbedtls_mpi_init(&RsaKeys->DP);
    mbedtls_mpi_init(&RsaKeys->DQ); mbedtls_mpi_init(&RsaKeys->QP);
    mbedtls_mpi_init(&RsaKeys->RN); mbedtls_mpi_init(&RsaKeys->RP);
    mbedtls_mpi_init(&RsaKeys->RQ);
    mbedtls_mpi_read_binary(&RsaKeys->N, (unsigned char*)BufN, SizeN);
    /* Load public key */
    if(BufE != NULL) mbedtls_mpi_read_binary(&RsaKeys->E, (unsigned char*)BufE, SizeE);
    /* Load private key */
    if(BufD != NULL) {
        mbedtls_mpi_read_binary(&RsaKeys->D, (unsigned char*)BufD, SizeD);
        mbedtls_mpi_read_binary(&RsaKeys->P, (unsigned char*)BufP, SizeP);
        mbedtls_mpi_read_binary(&RsaKeys->Q, (unsigned char*)BufQ, SizeQ);
        if(BufDP != NULL) {
            mbedtls_mpi_read_binary(&RsaKeys->DP, (unsigned char*)BufDP, SizeDP);
            mbedtls_mpi_read_binary(&RsaKeys->DQ, (unsigned char*)BufDQ, SizeDQ);
            mbedtls_mpi_read_binary(&RsaKeys->QP, (unsigned char*)BufQP, SizeQP);
        } else {
            LqCryptRsaNumber T;
            mbedtls_mpi_init(&T);
            mbedtls_mpi_sub_int(&T, &RsaKeys->P, 1);
            mbedtls_mpi_mod_mpi(&RsaKeys->DP, &RsaKeys->D, &T);
            mbedtls_mpi_sub_int(&T, &RsaKeys->Q, 1);
            mbedtls_mpi_mod_mpi(&RsaKeys->DQ, &RsaKeys->D, &T);
            mbedtls_mpi_inv_mod(&RsaKeys->QP, &RsaKeys->Q, &RsaKeys->P);
            mbedtls_mpi_free(&T);
        }
    }
    if(IsCheck) {
        if(BufE != NULL)
            if(!mbedtls_rsa_check_pubkey(RsaKeys))
                goto lblErr;
        if(BufD != NULL)
            if(!mbedtls_rsa_check_privkey(RsaKeys))
                goto lblErr;
    }
    RsaKeys->block_len = (mbedtls_mpi_bitlen(&RsaKeys->N) + 7) >> 3;
    return true;
lblErr:
    mbedtls_mpi_free(&RsaKeys->N); mbedtls_mpi_free(&RsaKeys->E); mbedtls_mpi_free(&RsaKeys->D); mbedtls_mpi_free(&RsaKeys->P);
    mbedtls_mpi_free(&RsaKeys->Q); mbedtls_mpi_free(&RsaKeys->DP); mbedtls_mpi_free(&RsaKeys->DQ); mbedtls_mpi_free(&RsaKeys->QP);
    mbedtls_mpi_free(&RsaKeys->RN); mbedtls_mpi_free(&RsaKeys->RP); mbedtls_mpi_free(&RsaKeys->RQ);
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqCryptRsaExportRaw(
    LqCryptRsa *RsaKeys,
    /* Public key */
    void* BufE, size_t* SizeE,
    /* For public and private key */
    void* BufN, size_t* SizeN,
    /* Private keys */
    void* BufD, size_t* SizeD,
    void* BufP, size_t* SizeP,
    void* BufQ, size_t* SizeQ,

    /* Optimal private keys  */
    void* BufDP, size_t* SizeDP,
    void* BufDQ, size_t* SizeDQ,
    void* BufQP, size_t* SizeQP
) {

    if((BufE != NULL) && (RsaKeys->E.p == NULL) ||
       (BufN != NULL) && (RsaKeys->N.p == NULL) ||
       (BufD != NULL) && (RsaKeys->D.p == NULL) || 
       (BufP != NULL) && (RsaKeys->P.p == NULL) ||
       (BufQ != NULL) && (RsaKeys->Q.p == NULL) ||
       (BufDP != NULL) && (RsaKeys->DP.p == NULL) ||
       (BufDQ != NULL) && (RsaKeys->DQ.p == NULL) ||
       (BufQP != NULL) && (RsaKeys->QP.p == NULL)
       )
        return false;
    if(BufE != NULL) *SizeE = mbedtls_mpi_write_binary2(&RsaKeys->E, (unsigned char*)BufE, *SizeE);
    if(BufN != NULL) *SizeN = mbedtls_mpi_write_binary2(&RsaKeys->N, (unsigned char*)BufN, *SizeN);
    if(BufD != NULL) *SizeD = mbedtls_mpi_write_binary2(&RsaKeys->D, (unsigned char*)BufD, *SizeD);
    if(BufP != NULL) *SizeP = mbedtls_mpi_write_binary2(&RsaKeys->P, (unsigned char*)BufP, *SizeP);
    if(BufQ != NULL) *SizeQ = mbedtls_mpi_write_binary2(&RsaKeys->Q, (unsigned char*)BufQ, *SizeQ);
    if(BufDP != NULL) *SizeDP = mbedtls_mpi_write_binary2(&RsaKeys->DP, (unsigned char*)BufDP, *SizeDP);
    if(BufDQ != NULL) *SizeDQ = mbedtls_mpi_write_binary2(&RsaKeys->DQ, (unsigned char*)BufDQ, *SizeDQ);
    if(BufQP != NULL) *SizeQP = mbedtls_mpi_write_binary2(&RsaKeys->QP, (unsigned char*)BufQP, *SizeQP);
    return true;
}

LQ_EXTERN_C void LQ_CALL LqCryptRsaFree(LqCryptRsa *RsaKeys) {
    mbedtls_mpi_free(&RsaKeys->E); mbedtls_mpi_free(&RsaKeys->N); mbedtls_mpi_free(&RsaKeys->D); mbedtls_mpi_free(&RsaKeys->P);
    mbedtls_mpi_free(&RsaKeys->Q); mbedtls_mpi_free(&RsaKeys->DP); mbedtls_mpi_free(&RsaKeys->DQ); mbedtls_mpi_free(&RsaKeys->QP);
    mbedtls_mpi_free(&RsaKeys->RN); mbedtls_mpi_free(&RsaKeys->RP); mbedtls_mpi_free(&RsaKeys->RQ);
}

/* end rsa */

/* start random */
#ifndef LQ_CRYPT_RND_UPDATE_NUMBER
# define LQ_CRYPT_RND_UPDATE_NUMBER 128
#endif
#define LQ_CRYPT_RND_BLOCK_SIZE 16
#define LQ_CRYPT_RND_MARK_SIZE 16 //Must be <= LQ_CRYPT_RND_KEY_SIZE and >= LQ_CRYPT_RND_BLOCK_SIZE
#define LQ_CRYPT_RND_KEY_SIZE 32

#define LQ_CRYPT_RND_CIPHER_INIT(Cipher, Key); LqCryptTwofishInit(&(Cipher), NULL, NULL, (Key), LQ_CRYPT_RND_KEY_SIZE, 16, 0)
#define LQ_CRYPT_RND_CIPHER_ENCRYPT LqCryptTwofishEncrypt

static int _RndUpdtCounter = LQ_CRYPT_RND_UPDATE_NUMBER + 1;
static LqCryptCipher _RandCipherKey; 
LQ_ALIGN(128) static unsigned char _RandBlock[LQ_CRYPT_RND_MARK_SIZE];
static size_t _RandPadlen = 0;
static size_t _RandLocker = 1;

static void _LqCryptRndGenerate(void* Dest) {
    LqCryptHash Hash;
    
#ifdef LQPLATFORM_WINDOWS
    struct {
        MEMORYSTATUSEX MemoryStatEx;
        DWORD Process;
        ULONGLONG Tick;
        LARGE_INTEGER PerfCounter;
        FILETIME ft1[4];

        void* AddressInHeap;
        void* AddressInStack;
        unsigned char buf[10];
    } Data;
    memset(&Data, 0, sizeof(Data));
    GlobalMemoryStatusEx(&Data.MemoryStatEx);
    Data.Process = GetCurrentProcessId();
    Data.Tick = GetTickCount64();
    QueryPerformanceCounter(&Data.PerfCounter);
    GetThreadTimes(GetCurrentThread(), Data.ft1, Data.ft1 + 1, Data.ft1 + 2, Data.ft1 + 3);

#else
    struct {
        unsigned int Uid;
        unsigned int Pid;
        unsigned int Time;
        struct timespec SpecRealTime;
        struct timespec SpecProcessCpuTime;
        struct timespec SpecMonotonicRow;

        void* AddressInHeap;
        void* AddressInStack;
        unsigned char buf[10];
    } Data;
    memset(&Data, 0, sizeof(Data));

    Data.Uid  = getuid();
    Data.Pid = getpid();
    Data.Time = time(NULL);
    clock_gettime(CLOCK_REALTIME, &Data.SpecRealTime);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &Data.SpecProcessCpuTime);
    clock_gettime(CLOCK_MONOTONIC_RAW, &Data.SpecMonotonicRow);
#endif
    Data.AddressInHeap = LqMemAlloc(10);
    Data.AddressInStack = &Data.AddressInHeap;
    memcpy(Data.buf, Data.AddressInHeap, 10);
    LqMemFree(Data.AddressInHeap);

    LqCryptSha256Init(&Hash);
    LqCryptSha256Update(&Hash, (unsigned char*)&Data, sizeof(Data));
    LqCryptSha256Final(&Hash, (unsigned char*)Dest);
}

static void _LqCryptRndUpdate() {
    unsigned char CurRandom[LQ_CRYPT_RND_KEY_SIZE];
    _LqCryptRndGenerate(CurRandom);
    LQ_CRYPT_RND_CIPHER_INIT(_RandCipherKey, CurRandom);
    _LqCryptRndGenerate(CurRandom);
    LQ_CRYPT_RND_CIPHER_ENCRYPT(&_RandCipherKey, CurRandom, CurRandom, LQ_CRYPT_RND_MARK_SIZE);
    memcpy(_RandBlock, CurRandom, LQ_CRYPT_RND_MARK_SIZE);
}

LQ_EXTERN_C void LQ_CALL LqCryptRnd(void* DestBuf, size_t BufSize) {
    for(size_t v = 1; !LqAtmCmpXchg(_RandLocker, v, (size_t)0); v = 1);
    if(_RndUpdtCounter > LQ_CRYPT_RND_UPDATE_NUMBER) {
        _LqCryptRndUpdate();
        _RndUpdtCounter = 0;
    } else {
        _RndUpdtCounter++;
    }
    register unsigned char* d = (unsigned char*)DestBuf, *md = d + BufSize, *p = _RandBlock + _RandPadlen;
    for(; d < md; d++, p++) {
        if(p == (_RandBlock + LQ_CRYPT_RND_MARK_SIZE)) {
            LQ_CRYPT_RND_CIPHER_ENCRYPT(&_RandCipherKey, _RandBlock, _RandBlock, LQ_CRYPT_RND_MARK_SIZE);
            p = _RandBlock;
        }
        *d = *p;
    }
    _RandPadlen = p - _RandBlock;
    _RandLocker = 1;
}


/* end random */

LQ_EXTERN_C bool LQ_CALL LqCryptCipherOpen(LqCryptCipher* CipherDest, const char* Formula, const void* InitVector, const void *Key, size_t KeyLen, int NumRounds, int Flags) {
    const char *c, *r, *y;
    uintptr_t j, i;
    const LqCryptCipherMethods* Methods = NULL;
    for(i = 0; CiphersMethods[i]; i++) {
        c = Formula;
        r = CiphersMethods[i]->Name;
        for(; ; c++, r++) {
            if(*r == '\0') {
                if(CiphersMethods[i]->block_length == -((intptr_t)1)) {
                    y = c + 1;
                    if(*c == '\0')
                        return false;
                    for(j = 0; CiphersMethods[j]; j++) {
                        c = y;
                        r = CiphersMethods[j]->Name;
                        for(; ; c++, r++) {
                            if(*r == '\0') {
                                Methods = CiphersMethods[j];
                                goto lblInit;
                            }
                            if(*c != *r) {
                                break;
                            }
                        }
                    }
                    lq_errno_set(ENOENT);
                    return false;
                }
lblInit:
                return CiphersMethods[i]->Init(CipherDest, Methods, (const unsigned char*)InitVector, (const unsigned char*)Key, KeyLen, NumRounds, Flags);
            }
            if(*c != *r) {
                break;
            }
        }
    }
    lq_errno_set(ENOENT);
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqCryptCipherEncrypt(LqCryptCipher* CipherDest, const void* Src, void* Dst, intptr_t Len) {
    return CipherDest->methods->Encrypt(CipherDest, (const unsigned char*)Src, (unsigned char*)Dst, Len);
}

LQ_EXTERN_C bool LQ_CALL LqCryptCipherDecrypt(LqCryptCipher* CipherDest, const void* Src, void* Dst, intptr_t Len) {
    return CipherDest->methods->Decrypt(CipherDest, (const unsigned char*)Src, (unsigned char*)Dst, Len);
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptCipherLenBlock(LqCryptCipher* CipherDest) {
    return CipherDest->methods->GetBlockLen(CipherDest);
}

LQ_EXTERN_C void LQ_CALL LqCryptCipherCopy(LqCryptCipher* CipherDest, LqCryptCipher* CipherSource) {
    CipherSource->methods->Copy(CipherDest, CipherSource);
}

LQ_EXTERN_C bool LQ_CALL LqCryptCipherSeek(LqCryptCipher* CipherDest, int64_t NewOffset) {
    if(CipherDest->methods->Seek == NULL) {
        lq_errno_set(ENOSYS);
        return false;
    }
    CipherDest->methods->Seek(CipherDest, NewOffset);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqCryptHashOpen(LqCryptHash* HashDest, const char* Formula) {
    const char *c, *r, *y;
    uintptr_t j, i;
    const LqCryptCipherMethods* Methods = NULL;
    for(i = 0; HashMethods[i]; i++) {
        c = Formula;
        r = HashMethods[i]->name;
        for(; ; c++, r++) {
            if(*r == '\0') {
                return HashMethods[i]->init(HashDest);
            }
            if(*c != *r) {
                break;
            }
        }
    }
    lq_errno_set(ENOENT);
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqCryptHashUpdate(LqCryptHash* HashDest, const void* Src, intptr_t Len) {
    return HashDest->methods->process(HashDest, (const unsigned char*)Src, Len);
}

LQ_EXTERN_C void LQ_CALL LqCryptHashCopy(LqCryptHash* HashDest, LqCryptHash* HashSource) {
    HashSource->methods->Copy(HashDest, HashSource);
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptHashFinal(LqCryptHash* HashDest, void* DestRes) {
    return HashDest->methods->done(HashDest, (unsigned char*)DestRes);
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptHashGetLenResult(LqCryptHash* HashDest) {
    return HashDest->methods->hashsize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptHashMemReg(const char* Formula, const void* SrcReg, intptr_t Len, void* Res) {
    LqCryptHash Hash;
    if(!LqCryptHashOpen(&Hash, Formula))
        return -((intptr_t)1);
    LqCryptHashUpdate(&Hash, SrcReg, Len);
    return LqCryptHashFinal(&Hash, Res);
}
/*          */
LQ_EXTERN_C bool LQ_CALL LqCryptMacOpen(LqCryptMac* MacDest, const char* Formula, const void* Key, intptr_t KeyLen) {
    const char *c, *r, *y;
    uintptr_t j, i;
    const void* Methods = NULL;
    for(i = 0; MacMethods[i]; i++) {
        c = Formula;
        r = MacMethods[i]->Name;
        for(; ; c++, r++) {
            if(*r == '\0') {
                if(MacMethods[i]->HashOrCipher > 0) {
                    y = c + 1;
                    if(*c == '\0')
                        return false;
                    if(MacMethods[i]->HashOrCipher == 1) { //If hash
                        for(j = 0; HashMethods[j]; j++) {
                            c = y;
                            r = HashMethods[j]->name;
                            for(; ; c++, r++) {
                                if(*r == '\0') {
                                    Methods = HashMethods[j];
                                    goto lblInit;
                                }
                                if(*c != *r) {
                                    break;
                                }
                            }
                        }
                    } else {
                        for(j = 0; CiphersMethods[j]; j++) {
                            c = y;
                            r = CiphersMethods[j]->Name;
                            for(; ; c++, r++) {
                                if(*r == '\0') {
                                    Methods = CiphersMethods[j];
                                    goto lblInit;
                                }
                                if(*c != *r) {
                                    break;
                                }
                            }
                        }
                    }

                    lq_errno_set(ENOENT);
                    return false;
                }
lblInit:
                return MacMethods[i]->Init(MacDest, Methods, (const unsigned char*)Key, KeyLen);
            }
            if(*c != *r) {
                break;
            }
        }
    }
    lq_errno_set(ENOENT);
    return false;
}

LQ_EXTERN_C bool LQ_CALL LqCryptMacUpdate(LqCryptMac* MacDest, const void* Src, intptr_t Len) {
    return MacDest->Methods->Update(MacDest, (const unsigned char*)Src, Len);
}

LQ_EXTERN_C void LQ_CALL LqCryptMacCopy(LqCryptMac* MacDest, LqCryptMac* MacSource) {
    MacSource->Methods->Copy(MacDest, MacSource);
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptMacFinal(LqCryptMac* MacDest, void* DestRes) {
    return MacDest->Methods->Final(MacDest, (unsigned char*)DestRes);
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptMacGetLenResult(LqCryptMac* ctx) {
    return ctx->Methods->GetResLen(ctx);
}

LQ_EXTERN_C intptr_t LQ_CALL LqCryptMacMemReg(const char* Formula, const void* Key, intptr_t KeyLen, const void* SrcReg, intptr_t Len, void* Res) {
    LqCryptMac Mac;
    if(!LqCryptMacOpen(&Mac, Formula, Key, KeyLen))
        return -((intptr_t)1);
    LqCryptMacUpdate(&Mac, SrcReg, Len);
    return LqCryptMacFinal(&Mac, Res);
}

static intptr_t LQ_CALL _CryptWriteProc(LqFbuf* Context, char* Buf, size_t Size);
static intptr_t LQ_CALL _CryptReadProc(LqFbuf* Context, char* Buf, size_t Size);
static int64_t LQ_CALL _CryptSeekProc(LqFbuf* Context, int64_t Offset, int Flags);
static bool LQ_CALL _CryptCopyProc(LqFbuf* Dest, LqFbuf* Source);
static intptr_t LQ_CALL _CryptCloseProc(LqFbuf* Context);
static int LQ_CALL _CryptLayerCtrlProc(LqFbuf* Context, int CurLayer, int Code, va_list Va);

LQ_ALIGN(128) static LqFbufCookie _CipherCookie = {
    _CryptReadProc,
    _CryptWriteProc,
    _CryptSeekProc,
    _CryptCopyProc,
    _CryptCloseProc,
    _CryptLayerCtrlProc,
    "cipher"
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)
typedef struct LqCryptCipherLayerData {
    LqSbuf OutStream;
    char RspLen, RcvLen;
    unsigned char Rsp[LQCRYPT_MAXBLOCKSIZE];
    unsigned char Recv[LQCRYPT_MAXBLOCKSIZE];
    void* NextProtoData;
    LqFbufCookie* NextProtoCookie;
    LqCryptCipher InCipher;
    LqCryptCipher OutCipher;
} LqCryptCipherLayerData;

#pragma pop(pack)

static intptr_t LqCryptCipherCallNextLayerWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    intptr_t Res = Context->Cookie->WriteProc(Context, Buf, Size);
    Context->UserData = LayerData;
    Context->Cookie = &_CipherCookie;
    return Res;
}

static intptr_t LqCryptCipherCallNextLayerReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    intptr_t Res = Context->Cookie->ReadProc(Context, Buf, Size);
    Context->UserData = LayerData;
    Context->Cookie = &_CipherCookie;
    return Res;
}

#define lq_mod(a, b)  (((a) - ((a) % (b))) / (b))

typedef struct  _CryptForArrRegForAlloc { char __v[32768]; } _CryptForArrRegForAlloc;

static void* _CryptAllocFastReg(intptr_t Len) {
    void* Res;
    if(Len < 32765) {
        if((Res = LqFastAlloc::New<_CryptForArrRegForAlloc>()) == NULL)
            return NULL;
        ((char*)Res)[0] = 1;
        return ((char*)Res) + 1;
    }
    if((Res = LqMemAlloc(Len)) == NULL)
        return NULL;
    ((char*)Res)[0] = 0;
    return ((char*)Res) + 1;
}

static void _CryptFreeFastReg(void* Reg) {
    if(*(((char*)Reg) - 1) == 1) {
        LqFastAlloc::Delete<_CryptForArrRegForAlloc>((_CryptForArrRegForAlloc*)(((char*)Reg) - 1));
    } else {
        LqMemFree(((char*)Reg) - 1);
    }
}

static int LQ_CALL _CryptLayerCtrlProc(LqFbuf* Context, int CurLayer, int Code, va_list Va) {
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    char TmpBuf[sizeof(LayerData->Rsp)] = {0};
    if(CurLayer <= 0) {
        if(Code == LQCRYPTCIPHER_CTRL_FLUSH) {
            const intptr_t BlockLen = LqCryptCipherLenBlock(&LayerData->InCipher);
            _CryptWriteProc(Context, TmpBuf, 0);
            return 0;
        } else if(Code == LQFBUF_CTR_REMOVE_LAYER) {
            const intptr_t BlockLen = LqCryptCipherLenBlock(&LayerData->InCipher);
            _CryptWriteProc(Context, TmpBuf, (LayerData->RspLen > 0) ? (BlockLen - LayerData->RspLen) : 0);
            Context->UserData = LayerData->NextProtoData;
            Context->Cookie = LayerData->NextProtoCookie;
            LqSbufUninit(&LayerData->OutStream);
            LqFastAlloc::Delete(LayerData);
            return 0;
        }
    } else {
        if(LayerData->NextProtoCookie->LayerCtrlProc == NULL)
            return -1;
        Context->UserData = LayerData->NextProtoData;
        Context->Cookie = LayerData->NextProtoCookie;
        int Res = Context->Cookie->LayerCtrlProc(Context, CurLayer - 1, Code, Va);
        LayerData->NextProtoData = Context->UserData;
        LayerData->NextProtoCookie = Context->Cookie;
        Context->UserData = LayerData;
        Context->Cookie = &_CipherCookie;
        return Res;
    }
    return -1;
}

static intptr_t LQ_CALL _CryptWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqSbufReadRegion ReadReg;
    LqSbufWriteRegion WriteReg;
    intptr_t WriteRes, DestSize, Res = 0;
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    const intptr_t BlockLen = LqCryptCipherLenBlock(&LayerData->OutCipher);
    unsigned char TmpBuf[sizeof(LayerData->Rsp)];
    LqCryptCipher TmpCipher;
    void* Reg;
    if(BlockLen == ((intptr_t)1)) {
        if((Reg = _CryptAllocFastReg(Size)) == NULL) {
            Context->Flags |= LQFBUF_WRITE_ERROR;
            return -((intptr_t)1);
        }
        LqCryptCipherCopy(&TmpCipher, &LayerData->OutCipher);
        LqCryptCipherEncrypt(&LayerData->OutCipher, Buf, Reg, Size);
        WriteRes = LqCryptCipherCallNextLayerWriteProc(Context, (char*)Reg, Size);
        if(WriteRes < ((intptr_t)Size)) {
            LqCryptCipherCopy(&LayerData->OutCipher, &TmpCipher);
            if(WriteRes > ((intptr_t)0))
                LqCryptCipherEncrypt(&LayerData->OutCipher, Buf, Reg, WriteRes);
        }
        _CryptFreeFastReg(Reg);
        return WriteRes;
    }
    if(LayerData->OutStream.Len > ((intptr_t)0)) {
        for(bool r = LqSbufReadRegionFirst(&LayerData->OutStream, &ReadReg, LayerData->OutStream.Len); r; r = LqSbufReadRegionNext(&ReadReg)) {
            if((WriteRes = LqCryptCipherCallNextLayerWriteProc(Context, (char*)ReadReg.Source, ReadReg.SourceLen)) < ((intptr_t)0)) {
                ReadReg.Written = ((intptr_t)0);
                ReadReg.Fin = true;
            } else {
                ReadReg.Written = WriteRes;
                ReadReg.Fin = Context->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK);
            }
        }
        if(Context->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK))
            return -((intptr_t)1);
    }
    if(BlockLen > (LayerData->RspLen + Size)) {
        memcpy(LayerData->Rsp + LayerData->RspLen, Buf, Size);
        LayerData->RspLen += Size;
        return Size;
    }
    while(true) {
        if(LayerData->RspLen > 0) {
            DestSize = BlockLen - ((intptr_t)LayerData->RspLen);
            DestSize = lq_min(DestSize, Size - Res);
            memcpy(LayerData->Rsp + LayerData->RspLen, Buf, DestSize);
            LayerData->RspLen += DestSize;
            Res += DestSize;
            Buf += DestSize;
            if(((intptr_t)LayerData->RspLen) >= BlockLen) {
                LqCryptCipherEncrypt(&LayerData->OutCipher, LayerData->Rsp, TmpBuf, BlockLen);
                LqSbufWrite(&LayerData->OutStream, TmpBuf, BlockLen);
                LayerData->RspLen = 0;
            } else {
                break;
            }
        }
        if(Res >= Size)
            break;
        for(bool r = LqSbufWriteRegionFirst(&LayerData->OutStream, &WriteReg, Size - Res); r; r = LqSbufWriteRegionNext(&WriteReg)) {
            DestSize = lq_mod(WriteReg.DestLen, BlockLen) * BlockLen;
            WriteReg.Readed = ((intptr_t)0);
            if(DestSize > ((intptr_t)0)) {
                LqCryptCipherEncrypt(&LayerData->OutCipher, Buf, WriteReg.Dest, DestSize);
                Res += DestSize;
                Buf += DestSize;
                WriteReg.Readed = DestSize;
            }
            DestSize = WriteReg.DestLen - DestSize;
            if(DestSize != ((intptr_t)0)) {
                memcpy(LayerData->Rsp, Buf, DestSize);
                LayerData->RspLen = DestSize;
                Res += DestSize;
                Buf += DestSize;
                WriteReg.Fin = true;
            }
        }
    }
    if(LayerData->OutStream.Len > ((intptr_t)0)) {
        for(bool r = LqSbufReadRegionFirst(&LayerData->OutStream, &ReadReg, LayerData->OutStream.Len); r; r = LqSbufReadRegionNext(&ReadReg)) {
            if((WriteRes = LqCryptCipherCallNextLayerWriteProc(Context, (char*)ReadReg.Source, ReadReg.SourceLen)) < ((intptr_t)0)) {
                ReadReg.Written = 0;
                ReadReg.Fin = true;
            } else {
                ReadReg.Written = WriteRes;
                ReadReg.Fin = Context->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK);
            }
        }
    }
    return Res;
}

static intptr_t LQ_CALL _CryptReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    const intptr_t BlockLen = LqCryptCipherLenBlock(&LayerData->InCipher);
    void* RcvBuf;
    intptr_t DestSize, ReadRes, DestSize2;
    if((RcvBuf = _CryptAllocFastReg(Size + BlockLen)) == NULL) {
        Context->Flags |= LQFBUF_READ_ERROR;
        return -((intptr_t)1);
    }
    DestSize = Size;
    if(LayerData->RcvLen > 0) {
        memcpy((char*)RcvBuf, LayerData->Recv, LayerData->RcvLen);
        if(DestSize > LayerData->RcvLen)
            DestSize -= LayerData->RcvLen;
        else
            DestSize = BlockLen - LayerData->RcvLen;
    }
    ReadRes = LqCryptCipherCallNextLayerReadProc(Context, ((char*)RcvBuf) + LayerData->RcvLen, DestSize);
    if(ReadRes <= ((intptr_t)0)) {
        _CryptFreeFastReg(RcvBuf);
        return ReadRes;
    }
    ReadRes += LayerData->RcvLen;
    DestSize2 = lq_mod(ReadRes, BlockLen) * BlockLen;
    if(DestSize2 > ((intptr_t)0)) 
        LqCryptCipherDecrypt(&LayerData->InCipher, (char*)RcvBuf, Buf, DestSize2);
    DestSize = ReadRes - DestSize2;
    if(DestSize > ((intptr_t)0))
        memcpy(LayerData->Recv, ((char*)RcvBuf) + DestSize2, DestSize);
    LayerData->RcvLen = DestSize;
    _CryptFreeFastReg(RcvBuf);
    return DestSize2;
}

static int64_t LQ_CALL _CryptSeekProc(LqFbuf* Context, int64_t Offset, int Flags) {
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    intptr_t InBlockLen = LqCryptCipherLenBlock(&LayerData->InCipher);
    intptr_t OutBlockLen = LqCryptCipherLenBlock(&LayerData->OutCipher);
    /* Go to next leyers */
    if((LayerData->NextProtoCookie->SeekProc == NULL) ||
       (LayerData->InCipher.methods->Seek == NULL) ||
       (LayerData->OutCipher.methods->Seek == NULL)) {
        lq_errno_set(ENOSYS);
        return -1ll;
    }
    if(((Offset % (int64_t)InBlockLen) != ((int64_t)0)) ||
        ((Offset % (int64_t)OutBlockLen) != ((int64_t)0))) {
        lq_errno_set(EINVAL);
        return -1ll;
    }
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    int64_t Res = Context->Cookie->SeekProc(Context, Offset, Flags);
    Context->UserData = LayerData;
    Context->Cookie = &_CipherCookie;

    if((Res != -1ll) && (Flags != LQ_SEEK_CUR) && (Offset != 0ll)) {
        LqSbufRead(&LayerData->OutStream, NULL, LayerData->OutStream.Len);
        LayerData->RspLen = 0;
        LayerData->RcvLen = 0;  
        LqCryptCipherSeek(&LayerData->InCipher, Res);
        LqCryptCipherSeek(&LayerData->OutCipher, Res);
    }
    return Res;
}

static bool LQ_CALL _CryptCopyProc(LqFbuf* Dest, LqFbuf* Source) {
    LqCryptCipherLayerData* SourceLayer = ((LqCryptCipherLayerData*)Source->UserData);
    LqCryptCipherLayerData* DestLayer = NULL;
    if(SourceLayer->NextProtoCookie->CopyProc == NULL)
        return false;
    DestLayer = LqFastAlloc::New<LqCryptCipherLayerData>();
    if(DestLayer == NULL)
        return false;
    Source->UserData = SourceLayer->NextProtoData;
    Source->Cookie = SourceLayer->NextProtoCookie;
    bool Res = Source->Cookie->CopyProc(Dest, Source);
    Source->UserData = SourceLayer;
    Source->Cookie = &_CipherCookie;
    if(!Res) {
        LqFastAlloc::Delete(DestLayer);
        return false;
    }
    memcpy(DestLayer, SourceLayer, sizeof(LqCryptCipherLayerData));
    LqSbufCopy(&DestLayer->OutStream, &SourceLayer->OutStream);
    LqCryptCipherCopy(&DestLayer->InCipher, &SourceLayer->InCipher);
    LqCryptCipherCopy(&DestLayer->OutCipher, &SourceLayer->OutCipher);
    DestLayer->NextProtoCookie = Dest->Cookie;
    DestLayer->NextProtoData = Dest->UserData;
    Dest->Cookie = &_CipherCookie;
    Dest->UserData = DestLayer;
    return true;
}

static intptr_t LQ_CALL _CryptCloseProc(LqFbuf* Context) {
    LqCryptCipherLayerData* LayerData = ((LqCryptCipherLayerData*)Context->UserData);
    const intptr_t BlockLen = LqCryptCipherLenBlock(&LayerData->InCipher);
    intptr_t Res = -((intptr_t)1);
    char TmpBuf[sizeof(LayerData->Rsp)] = {0};
    _CryptWriteProc(Context, TmpBuf, (LayerData->RspLen > 0)? (BlockLen - LayerData->RspLen) : 0);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    if(Context->Cookie->CloseProc != NULL) {
        Res = Context->Cookie->CloseProc(Context);
    }
    Context->UserData = LayerData;
    Context->Cookie = &_CipherCookie;
    LqSbufUninit(&LayerData->OutStream);
    LqFastAlloc::Delete(LayerData);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL CryptCipherAppendLayerOnFbuf(LqFbuf* Context, const char* Formula, const void* InitVector, const void *Key, size_t KeyLen, int NumRounds, int Flags) {
    LqCryptCipherLayerData* DestLayer = LqFastAlloc::New<LqCryptCipherLayerData>();
    if(DestLayer == NULL) {
        lq_errno_set(ENOMEM);
        return false;
    }
    LqFbuf_lock(Context);
    if(!LqCryptCipherOpen(&DestLayer->InCipher, Formula, InitVector, Key, KeyLen, NumRounds, Flags)) {
        LqFbuf_unlock(Context);
        LqFastAlloc::Delete(DestLayer);
        return false;
    }
    LqCryptCipherOpen(&DestLayer->OutCipher, Formula, InitVector, Key, KeyLen, NumRounds, Flags);
    LqSbufInit(&DestLayer->OutStream);
    DestLayer->RcvLen = 0;
    DestLayer->RspLen = 0;
    DestLayer->NextProtoData = Context->UserData;
    DestLayer->NextProtoCookie = Context->Cookie;
    Context->Cookie = &_CipherCookie;
    Context->UserData = DestLayer;
    LqFbuf_unlock(Context);
    return true;
}

static intptr_t LQ_CALL _HashWriteProc(LqFbuf* Context, char* Buf, size_t Size);
static intptr_t LQ_CALL _HashReadProc(LqFbuf* Context, char* Buf, size_t Size);
static int64_t LQ_CALL _HashSeekProc(LqFbuf* Context, int64_t Offset, int Flags);
static bool LQ_CALL _HashCopyProc(LqFbuf* Dest, LqFbuf* Source);
static intptr_t LQ_CALL _HashCloseProc(LqFbuf* Context);
static int LQ_CALL _HashLayerCtrlProc(LqFbuf* Context, int CurLayer, int Code, va_list Va);

LQ_ALIGN(128) static LqFbufCookie _HashCookie = {
    _HashReadProc,
    _HashWriteProc,
    _HashSeekProc,
    _HashCopyProc,
    _HashCloseProc,
    _HashLayerCtrlProc,
    "hash"
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqCryptHashLayerData {
    LqCryptHash Hash;
    bool Rd;
    void* NextProtoData;
    LqFbufCookie* NextProtoCookie;
} LqCryptHashLayerData;

#pragma pop(pack)

static intptr_t LqCryptHashCallNextLayerWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptHashLayerData* LayerData = ((LqCryptHashLayerData*)Context->UserData);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    intptr_t Res = Context->Cookie->WriteProc(Context, Buf, Size);
    Context->UserData = LayerData;
    Context->Cookie = &_HashCookie;
    return Res;
}

static intptr_t LqCryptHashCallNextLayerReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptHashLayerData* LayerData = ((LqCryptHashLayerData*)Context->UserData);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    intptr_t Res = Context->Cookie->ReadProc(Context, Buf, Size);
    Context->UserData = LayerData;
    Context->Cookie = &_HashCookie;
    return Res;
}

static intptr_t LQ_CALL _HashWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptHashLayerData* LayerData = ((LqCryptHashLayerData*)Context->UserData);
    intptr_t Res = LqCryptHashCallNextLayerWriteProc(Context, Buf, Size);
    if(!LayerData->Rd && (Res > ((intptr_t)0)))
        LqCryptHashUpdate(&LayerData->Hash, Buf, Res);
    return Res;
}

static intptr_t LQ_CALL _HashReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptHashLayerData* LayerData = ((LqCryptHashLayerData*)Context->UserData);
    intptr_t Res = LqCryptHashCallNextLayerReadProc(Context, Buf, Size);
    if(LayerData->Rd && (Res > ((intptr_t)0)))
        LqCryptHashUpdate(&LayerData->Hash, Buf, Res);
    return Res;
}

static int64_t LQ_CALL _HashSeekProc(LqFbuf* Context, int64_t Offset, int Flags) {
    lq_errno_set(ENOSYS);
    return -((intptr_t)1);
}

static bool LQ_CALL _HashCopyProc(LqFbuf* Dest, LqFbuf* Source) {
    LqCryptHashLayerData* SourceLayer = ((LqCryptHashLayerData*)Source->UserData);
    LqCryptHashLayerData* DestLayer = NULL;

    if(SourceLayer->NextProtoCookie->CopyProc == NULL)
        return false;
    DestLayer = LqFastAlloc::New<LqCryptHashLayerData>();
    if(DestLayer == NULL)
        return false;
    Source->UserData = SourceLayer->NextProtoData;
    Source->Cookie = SourceLayer->NextProtoCookie;
    bool Res = Source->Cookie->CopyProc(Dest, Source);
    Source->UserData = SourceLayer;
    Source->Cookie = &_HashCookie;
    if(!Res) {
        LqFastAlloc::Delete(DestLayer);
        return false;
    }
    memcpy(DestLayer, SourceLayer, sizeof(LqCryptHashLayerData));
    LqCryptHashCopy(&DestLayer->Hash, &DestLayer->Hash);
    DestLayer->NextProtoCookie = Dest->Cookie;
    DestLayer->NextProtoData = Dest->UserData;
    Dest->Cookie = &_HashCookie;
    Dest->UserData = DestLayer;
    return true;
}

static intptr_t LQ_CALL _HashCloseProc(LqFbuf* Context) {
    LqCryptHashLayerData* LayerData = ((LqCryptHashLayerData*)Context->UserData);
    intptr_t Res = -((intptr_t)1);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    if(Context->Cookie->CloseProc != NULL)
        Res = Context->Cookie->CloseProc(Context);
    Context->UserData = LayerData;
    Context->Cookie = &_HashCookie;
    LqFastAlloc::Delete(LayerData);
    return Res;
}

static int LQ_CALL _HashLayerCtrlProc(LqFbuf* Context, int CurLayer, int Code, va_list Va) {
    LqCryptHashLayerData* LayerData = ((LqCryptHashLayerData*)Context->UserData);
    void* DestKey;
    char buf[1024];
    if(CurLayer <= 0) {
        if(Code == LQFBUF_CTR_REMOVE_LAYER) {
            int Res = LqCryptHashFinal(&LayerData->Hash, buf);
            Context->UserData = LayerData->NextProtoData;
            Context->Cookie = LayerData->NextProtoCookie;
            LqFastAlloc::Delete(LayerData);
            return Res;
        } else if(Code == LQCRYPTHASH_CTRL_REMOVE_LAYER) {
            DestKey = va_arg(Va, void*);
            int Res = LqCryptHashGetLenResult(&LayerData->Hash);
            LqCryptHashFinal(&LayerData->Hash, DestKey);
            Context->UserData = LayerData->NextProtoData;
            Context->Cookie = LayerData->NextProtoCookie;
            LqFastAlloc::Delete(LayerData);
            return Res;
        } else if(Code == LQCRYPTHASH_CTRL_GET_RES_LEN) {
            return LqCryptHashGetLenResult(&LayerData->Hash);
        }
    } else {
        if(LayerData->NextProtoCookie->LayerCtrlProc == NULL)
            return -1;
        Context->UserData = LayerData->NextProtoData;
        Context->Cookie = LayerData->NextProtoCookie;
        int Res = Context->Cookie->LayerCtrlProc(Context, CurLayer - 1, Code, Va);
        LayerData->NextProtoData = Context->UserData;
        LayerData->NextProtoCookie = Context->Cookie;
        Context->UserData = LayerData;
        Context->Cookie = &_HashCookie;
        return Res;
    }
    return -1;
}

LQ_EXTERN_C bool LQ_CALL CryptHashAppendLayerOnFbuf(LqFbuf* Context, const char* Formula, bool Rd) {
    LqCryptHashLayerData* DestLayer = LqFastAlloc::New<LqCryptHashLayerData>();
    if(DestLayer == NULL) {
        lq_errno_set(ENOMEM);
        return false;
    }
    LqFbuf_lock(Context);
    if(!LqCryptHashOpen(&DestLayer->Hash, Formula)) {
        LqFbuf_unlock(Context);
        LqFastAlloc::Delete(DestLayer);
        return false;
    }
    DestLayer->Rd = Rd;
    DestLayer->NextProtoData = Context->UserData;
    DestLayer->NextProtoCookie = Context->Cookie;
    Context->Cookie = &_HashCookie;
    Context->UserData = DestLayer;
    LqFbuf_unlock(Context);
    return true;
}

static intptr_t LQ_CALL _MacWriteProc(LqFbuf* Context, char* Buf, size_t Size);
static intptr_t LQ_CALL _MacReadProc(LqFbuf* Context, char* Buf, size_t Size);
static int64_t LQ_CALL _MacSeekProc(LqFbuf* Context, int64_t Offset, int Flags);
static bool LQ_CALL _MacCopyProc(LqFbuf* Dest, LqFbuf* Source);
static intptr_t LQ_CALL _MacCloseProc(LqFbuf* Context);
static int LQ_CALL _MacLayerCtrlProc(LqFbuf* Context, int CurLayer, int Code, va_list Va);

LQ_ALIGN(128) static LqFbufCookie _MacCookie = {
    _MacReadProc,
    _MacWriteProc,
    _MacSeekProc,
    _MacCopyProc,
    _MacCloseProc,
    _MacLayerCtrlProc,
    "mac"
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqCryptMacLayerData {
    LqCryptMac Mac;
    bool Rd;
    void* NextProtoData;
    LqFbufCookie* NextProtoCookie;
} LqCryptMacLayerData;

#pragma pop(pack)

static intptr_t LqCryptMacCallNextLayerWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptMacLayerData* LayerData = ((LqCryptMacLayerData*)Context->UserData);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    intptr_t Res = Context->Cookie->WriteProc(Context, Buf, Size);
    Context->UserData = LayerData;
    Context->Cookie = &_MacCookie;
    return Res;
}

static intptr_t LqCryptMacCallNextLayerReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptMacLayerData* LayerData = ((LqCryptMacLayerData*)Context->UserData);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    intptr_t Res = Context->Cookie->ReadProc(Context, Buf, Size);
    Context->UserData = LayerData;
    Context->Cookie = &_MacCookie;
    return Res;
}


static intptr_t LQ_CALL _MacWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptMacLayerData* LayerData = ((LqCryptMacLayerData*)Context->UserData);
    intptr_t Res = LqCryptMacCallNextLayerWriteProc(Context, Buf, Size);
    if(!LayerData->Rd && (Res > ((intptr_t)0)))
        LqCryptMacUpdate(&LayerData->Mac, Buf, Res);
    return Res;
}

static intptr_t LQ_CALL _MacReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    LqCryptMacLayerData* LayerData = ((LqCryptMacLayerData*)Context->UserData);
    intptr_t Res = LqCryptMacCallNextLayerReadProc(Context, Buf, Size);
    if(LayerData->Rd && (Res > ((intptr_t)0)))
        LqCryptMacUpdate(&LayerData->Mac, Buf, Res);
    return Res;
}

static int64_t LQ_CALL _MacSeekProc(LqFbuf* Context, int64_t Offset, int Flags) {
    lq_errno_set(ENOSYS);
    return -((intptr_t)1);
}

static bool LQ_CALL _MacCopyProc(LqFbuf* Dest, LqFbuf* Source) {
    LqCryptMacLayerData* SourceLayer = ((LqCryptMacLayerData*)Source->UserData);
    LqCryptMacLayerData* DestLayer = NULL;

    if(SourceLayer->NextProtoCookie->CopyProc == NULL)
        return false;
    DestLayer = LqFastAlloc::New<LqCryptMacLayerData>();
    if(DestLayer == NULL)
        return false;
    Source->UserData = SourceLayer->NextProtoData;
    Source->Cookie = SourceLayer->NextProtoCookie;
    bool Res = Source->Cookie->CopyProc(Dest, Source);
    Source->UserData = SourceLayer;
    Source->Cookie = &_MacCookie;
    if(!Res) {
        LqFastAlloc::Delete(DestLayer);
        return false;
    }
    memcpy(DestLayer, SourceLayer, sizeof(LqCryptMacLayerData));
    LqCryptMacCopy(&DestLayer->Mac, &DestLayer->Mac);
    DestLayer->NextProtoCookie = Dest->Cookie;
    DestLayer->NextProtoData = Dest->UserData;
    Dest->Cookie = &_MacCookie;
    Dest->UserData = DestLayer;
    return true;
}

static intptr_t LQ_CALL _MacCloseProc(LqFbuf* Context) {
    unsigned char buf[1024];
    LqCryptMacLayerData* LayerData = ((LqCryptMacLayerData*)Context->UserData);
    intptr_t Res = -((intptr_t)1);
    
    LqCryptMacFinal(&LayerData->Mac, buf);
    Context->UserData = LayerData->NextProtoData;
    Context->Cookie = LayerData->NextProtoCookie;
    if(Context->Cookie->CloseProc != NULL)
        Res = Context->Cookie->CloseProc(Context);
    Context->UserData = LayerData;
    Context->Cookie = &_MacCookie;
    LqFastAlloc::Delete(LayerData);
    return Res;
}

static int LQ_CALL _MacLayerCtrlProc(LqFbuf* Context, int CurLayer, int Code, va_list Va) {
    LqCryptMacLayerData* LayerData = ((LqCryptMacLayerData*)Context->UserData);
    void* DestKey;
    unsigned char buf[1024];
    if(CurLayer <= 0) {
        if(Code == LQFBUF_CTR_REMOVE_LAYER) {
            int Res = LqCryptMacFinal(&LayerData->Mac, buf);
            Context->UserData = LayerData->NextProtoData;
            Context->Cookie = LayerData->NextProtoCookie;
            LqFastAlloc::Delete(LayerData);
            return Res;
        } else if(Code == LQCRYPTMAC_CTRL_REMOVE_LAYER) {
            DestKey = va_arg(Va, void*);
            int Res = LqCryptMacFinal(&LayerData->Mac, DestKey);
            Context->UserData = LayerData->NextProtoData;
            Context->Cookie = LayerData->NextProtoCookie;
            LqFastAlloc::Delete(LayerData);
            return Res;
        } else if(Code == LQCRYPTMAC_CTRL_GET_RES_LEN) {
            return LqCryptMacGetLenResult(&LayerData->Mac);
        }
    } else {
        if(LayerData->NextProtoCookie->LayerCtrlProc == NULL)
            return -1;
        Context->UserData = LayerData->NextProtoData;
        Context->Cookie = LayerData->NextProtoCookie;
        int Res = Context->Cookie->LayerCtrlProc(Context, CurLayer - 1, Code, Va);
        LayerData->NextProtoData = Context->UserData;
        LayerData->NextProtoCookie = Context->Cookie;
        Context->UserData = LayerData;
        Context->Cookie = &_HashCookie;
        return Res;
    }
    return -1;
}

LQ_EXTERN_C bool LQ_CALL CryptMacAppendLayerOnFbuf(LqFbuf* Context, const char* Formula, const void* Key, intptr_t KeyLen, bool Rd) {
    LqCryptMacLayerData* DestLayer = LqFastAlloc::New<LqCryptMacLayerData>();
    if(DestLayer == NULL) {
        lq_errno_set(ENOMEM);
        return false;
    }
    LqFbuf_lock(Context);
    if(!LqCryptMacOpen(&DestLayer->Mac, Formula, Key, KeyLen)) {
        LqFbuf_unlock(Context);
        LqFastAlloc::Delete(DestLayer);
        return false;
    }
    DestLayer->Rd = Rd;
    DestLayer->NextProtoData = Context->UserData;
    DestLayer->NextProtoCookie = Context->Cookie;
    Context->Cookie = &_HashCookie;
    Context->UserData = DestLayer;
    LqFbuf_unlock(Context);
    return true;
}

#define __METHOD_DECLS__
#include "LqAlloc.hpp"
