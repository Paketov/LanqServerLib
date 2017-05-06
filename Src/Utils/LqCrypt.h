/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqCrypt... - Encryption/decryption functions.
* 
* Example of file encryption:
*  
  LqFbuf InFile, OutFile;
  char Md5Hash[16];
  LqFbuf_open(&InFile, "/usr/user_name/file_to_crypt.txt", LQ_O_RD | LQ_O_SEQ | LQ_O_BIN, 0, 50, 32768, 1024);
  LqFbuf_open(&OutFile, "/usr/user_name/encrypted_file.txt", LQ_O_WR | LQ_O_CREATE | LQ_O_BIN, 0, 50, 32768, 1024);
  CryptHashAppendLayerOnFbuf(&InFile, "md5", true);
  CryptCipherAppendLayerOnFbuf(&InFile, "ctr-aes", "\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x76\x78\x45\x23\x12",
    "\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x76\x78\x45\x23\x12",16, 10, 0);
  LqFbuf_transfer(&OutFile, &InFile, 0xffffffffffff);// Process encrypt and hash for all file
  LqFbuf_ctrl(&InFile, 1, LQCRYPTHASH_CTRL_REMOVE_LAYER, Md5Hash); // Get plain text hash
  LqFbuf_close(&InFile);
  LqFbuf_close(&OutFile);

  Support ciphers:
    aes - Key lengths: 16, 24, 32; Rounds: 0, 10, 12, 14; Block length 16
    twofish - Key lengths: 16, 24, 32; Rounds: 0, 16; Block length 16
    des3 - Key lengths: 24; Rounds: 0, 16; Block length 8
    des - Key lengths: 24; Rounds: 0, 16; Block length 8
    serpent - Key lengths: 16, 24, 32; Rounds: 16; Block length 16

  Cipher mode:
    cbc
    ctr
    ofb

  Hashes:
    md5 - Result length: 16;
    sha1 - Result length: 20;
    sha256 - Result length: 32;
    sha512 - Result length: 64;

  Signs:
    hmac
    pmac
    omac
    xcbc
*/



#ifndef __LQ_CRYPT_H__HAS_DEFINED__
#define __LQ_CRYPT_H__HAS_DEFINED__

#include "LqOs.h"
#include "LqDef.h"

/* Flags for ctr mode*/
# define CTR_COUNTER_LITTLE_ENDIAN    0
# define CTR_COUNTER_BIG_ENDIAN       1
# define LTC_CTR_RFC3686              2
/* - */

/* Control codes for LqFbuf_ctrl */
# define LQCRYPTCIPHER_CTRL_FLUSH           1

# define LQCRYPTHASH_CTRL_REMOVE_LAYER      1
# define LQCRYPTHASH_CTRL_GET_RES_LEN       2

# define LQCRYPTMAC_CTRL_REMOVE_LAYER       1
# define LQCRYPTMAC_CTRL_GET_RES_LEN        2
/* - */

# define LQCRYPT_MAXBLOCKSIZE               16

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct _LqCryptDes3Key;
struct _LqCryptTwofishKey;
struct _LqCryptAesKey;
struct _LqCryptDesKey;
union _LqCryptSymmetricKey;
struct _LqCryptSymmetricCbcKey;
struct _LqCryptSymmetricCtrKey;
struct _symmetric_OFB;
union LqCryptCipher;
struct LqCryptCipherMethods;
union LqCryptHash;
struct LqCryptHashMethods;
struct LqCryptHmac;
union LqCryptMac;
struct LqCryptMacMethods;
struct _LqCryptHmac;
struct _LqCryptPmac;
struct _LqCryptSerpentKey;
struct _LqCryptOmac;
struct _LqCryptXcbc;


typedef struct _LqCryptDes3Key _LqCryptDes3Key;
typedef struct _LqCryptTwofishKey _LqCryptTwofishKey;
typedef struct _LqCryptAesKey _LqCryptAesKey;
typedef struct _LqCryptDesKey _LqCryptDesKey;
typedef union _LqCryptSymmetricKey _LqCryptSymmetricKey;
typedef struct _LqCryptSymmetricCbcKey _LqCryptSymmetricCbcKey;
typedef struct _LqCryptSymmetricCtrKey _LqCryptSymmetricCtrKey;
typedef union LqCryptCipher LqCryptCipher;
typedef struct LqCryptCipherMethods LqCryptCipherMethods;
typedef union LqCryptHash LqCryptHash;
typedef struct LqCryptHashMethods LqCryptHashMethods;
typedef struct LqCryptHmac LqCryptHmac;
typedef struct _symmetric_OFB _symmetric_OFB;
typedef union LqCryptMac LqCryptMac;
typedef struct LqCryptMacMethods LqCryptMacMethods;
typedef struct _LqCryptSerpentKey _LqCryptSerpentKey;
typedef struct _LqCryptHmac _LqCryptHmac;
typedef struct _LqCryptPmac _LqCryptPmac;
typedef struct _LqCryptOmac _LqCryptOmac;
typedef struct _LqCryptXcbc _LqCryptXcbc;

struct _LqCryptDes3Key {
    const LqCryptCipherMethods* methods;
    uint32_t ek[3][32], dk[3][32];
};

struct _LqCryptTwofishKey {
    const LqCryptCipherMethods* methods;
    uint32_t K[40];
    unsigned char S[32], start;
};

struct _LqCryptAesKey {
    const LqCryptCipherMethods* methods;
    uint32_t eK[60], dK[60];
    int Nr;
};

struct _LqCryptSerpentKey {
    const LqCryptCipherMethods* methods;
    uint32_t keys[132];
};

struct _LqCryptDesKey {
    const LqCryptCipherMethods* methods;
    uint32_t ek[32], dk[32];
};

union _LqCryptSymmetricKey {
    _LqCryptDes3Key des3;
    _LqCryptTwofishKey twofish;
    _LqCryptAesKey rijndael;
    _LqCryptDesKey des;
    _LqCryptSerpentKey serpent;
    const LqCryptCipherMethods* methods;
};

struct _LqCryptSymmetricCbcKey {
    const LqCryptCipherMethods* methods;
    unsigned char       IV[LQCRYPT_MAXBLOCKSIZE];
    _LqCryptSymmetricKey       key;
};

struct _LqCryptSymmetricCtrKey {
    const LqCryptCipherMethods* methods;
    intptr_t                   padlen, mode;
    unsigned char              ctr[LQCRYPT_MAXBLOCKSIZE];
    unsigned char              pad[LQCRYPT_MAXBLOCKSIZE];
    unsigned char              IV[LQCRYPT_MAXBLOCKSIZE];
    _LqCryptSymmetricKey       key;
};

struct _symmetric_OFB {
    const LqCryptCipherMethods* methods;
    intptr_t padlen;
    unsigned char       IV[LQCRYPT_MAXBLOCKSIZE];
    _LqCryptSymmetricKey key;
};


union LqCryptCipher {
    _LqCryptSymmetricKey ciphers;
    _LqCryptSymmetricCbcKey symmetric_cbc;
    _LqCryptSymmetricCtrKey symmetric_ctr;
    _symmetric_OFB  symmetric_ofb;
    const LqCryptCipherMethods* methods;
};


/* Implementation of Twofish by Tom St Denis */
struct LqCryptCipherMethods {
    bool(*Init)(LqCryptCipher* Dest, const LqCryptCipherMethods *Methods, const unsigned char *IV, const unsigned char *key, int keylen, int num_rounds, int Flags);
    bool(*Encrypt)(LqCryptCipher *skey, const unsigned char *pt, unsigned char *ct, intptr_t Len);
    bool(*Decrypt)(LqCryptCipher *skey, const unsigned char *ct, unsigned char *pt, intptr_t Len);
    void(*Seek)(LqCryptCipher *skey, int64_t NewOffset);
    intptr_t(*GetBlockLen)(LqCryptCipher* skey);
    void(*Copy)(LqCryptCipher *Dest, LqCryptCipher *Source);
    intptr_t block_length;
    intptr_t size;
    const char* Name;
};

struct LqCryptMacMethods {
    bool(*Init)(LqCryptMac* ctx, const void* CipherOrHashMethods, const unsigned char *key, size_t keylen);
    bool(*Update)(LqCryptMac *ctx, const unsigned char *InBuf, intptr_t LenBuf);
    intptr_t(*Final)(LqCryptMac *ctx, unsigned char *out);
    void(*Copy)(LqCryptMac *ctx, LqCryptMac *Source);
    intptr_t(*GetResLen)(LqCryptMac* ctx);
    int HashOrCipher;
    intptr_t size;
    const char* Name;
};

struct LqCryptHashMethods {
    uintptr_t hashsize;
    uintptr_t blocksize;
    bool(*init)(LqCryptHash *ctx);
    bool(*process)(LqCryptHash *ctx, const unsigned char *in, intptr_t Len);
    intptr_t(*done)(LqCryptHash* HashDest, unsigned char * DestRes);
    void(*Copy)(LqCryptHash *ctx, LqCryptHash *Source);
    const char *name;
};

union LqCryptHash {
    struct {
        const LqCryptHashMethods* methods;
        uint64_t length;
        uint32_t state[5], curlen;
        unsigned char buf[64];
    } sha1;
    struct {
        const LqCryptHashMethods* methods;
        uint64_t length;
        uint32_t state[4], curlen;
        unsigned char buf[64];
    } md5;
    struct {
        const LqCryptHashMethods* methods;
        uint64_t length;
        uint64_t state[8], curlen;
        unsigned char buf[64];
    } sha256;
    struct {
        const LqCryptHashMethods* methods;
        uint64_t  length, state[8];
        unsigned long curlen;
        unsigned char buf[128];
    }sha512;
    const LqCryptHashMethods* methods;
};


struct _LqCryptHmac{
    const LqCryptMacMethods* Methods;
    LqCryptHash     md;
    unsigned char  *key;
};

struct _LqCryptPmac {
    const LqCryptMacMethods* Methods;
    unsigned char     Ls[32][LQCRYPT_MAXBLOCKSIZE],
        Li[LQCRYPT_MAXBLOCKSIZE],
        Lr[LQCRYPT_MAXBLOCKSIZE],
        block[LQCRYPT_MAXBLOCKSIZE],
        checksum[LQCRYPT_MAXBLOCKSIZE];

    LqCryptCipher     key;
    unsigned long     block_index;
    int               buflen;
};

struct _LqCryptOmac {
    const LqCryptMacMethods* Methods;
    int           cipher_idx,buflen;
    unsigned char   block[LQCRYPT_MAXBLOCKSIZE],
        prev[LQCRYPT_MAXBLOCKSIZE],
        Lu[2][LQCRYPT_MAXBLOCKSIZE];
    LqCryptCipher     key;
};

struct _LqCryptXcbc {
    const LqCryptMacMethods* Methods;
    unsigned char K[3][LQCRYPT_MAXBLOCKSIZE], IV[LQCRYPT_MAXBLOCKSIZE];
    LqCryptCipher     key;
    int buflen, blocksize;
};

union LqCryptMac {
    _LqCryptHmac hmac;
    _LqCryptPmac pmac;
    _LqCryptOmac omac;
    _LqCryptXcbc xcbc;
    const LqCryptMacMethods* Methods;
};

#if ( ! defined(MBEDTLS_HAVE_INT32) && \
        defined(_MSC_VER) && defined(_M_AMD64) )
#define MBEDTLS_HAVE_INT64
typedef  int64_t mbedtls_mpi_sint;
typedef uint64_t mbedtls_mpi_uint;
#else
#if ( ! defined(MBEDTLS_HAVE_INT32) &&               \
        defined(__GNUC__) && (                          \
        defined(__amd64__) || defined(__x86_64__)    || \
        defined(__ppc64__) || defined(__powerpc64__) || \
        defined(__ia64__)  || defined(__alpha__)     || \
        (defined(__sparc__) && defined(__arch64__))  || \
        defined(__s390x__) || defined(__mips64) ) )
#define MBEDTLS_HAVE_INT64
typedef  int64_t mbedtls_mpi_sint;
typedef uint64_t mbedtls_mpi_uint;
/* mbedtls_t_udbl defined as 128-bit unsigned int */
typedef unsigned int mbedtls_t_udbl __attribute__((mode(TI)));
#define MBEDTLS_HAVE_UDBL
#else
#define MBEDTLS_HAVE_INT32
typedef  int32_t mbedtls_mpi_sint;
typedef uint32_t mbedtls_mpi_uint;
typedef uint64_t mbedtls_t_udbl;
#define MBEDTLS_HAVE_UDBL
#endif /* !MBEDTLS_HAVE_INT32 && __GNUC__ && 64-bit platform */
#endif /* !MBEDTLS_HAVE_INT32 && _MSC_VER && _M_AMD64 */



typedef struct LqCryptRsaNumber {
    int s;              /*!<  integer sign      */
    size_t n;           /*!<  total # of limbs  */
    mbedtls_mpi_uint *p;          /*!<  pointer to limbs  */
} LqCryptRsaNumber;

typedef struct {
    size_t block_len;                 /*!<  block in chars  */
    LqCryptRsaNumber N;                      /*!<  public modulus    */
    LqCryptRsaNumber E;                      /*!<  public exponent   */

    LqCryptRsaNumber D;                      /*!<  private exponent  */
    LqCryptRsaNumber P;                      /*!<  1st prime factor  */
    LqCryptRsaNumber Q;                      /*!<  2nd prime factor  */
    LqCryptRsaNumber DP;                     /*!<  D % (P - 1)       */
    LqCryptRsaNumber DQ;                     /*!<  D % (Q - 1)       */
    LqCryptRsaNumber QP;                     /*!<  1 / (Q % P)       */

    LqCryptRsaNumber RN;                     /*!<  cached R^2 mod N  */
    LqCryptRsaNumber RP;                     /*!<  cached R^2 mod P  */
    LqCryptRsaNumber RQ;                     /*!<  cached R^2 mod Q  */
} LqCryptRsa;


#pragma pack(pop)

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT bool LQ_CALL LqCryptCipherOpen(LqCryptCipher* CipherDest, const char* Formula, const void* InitVector, const void *Key, size_t KeyLen, int NumRounds, int Flags);
LQ_IMPORTEXPORT bool LQ_CALL LqCryptCipherEncrypt(LqCryptCipher* CipherDest, const void* Src, void* Dst, intptr_t Len);
LQ_IMPORTEXPORT bool LQ_CALL LqCryptCipherDecrypt(LqCryptCipher* CipherDest, const void* Src, void* Dst, intptr_t Len);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptCipherLenBlock(LqCryptCipher* CipherDest);
LQ_IMPORTEXPORT bool LQ_CALL LqCryptCipherSeek(LqCryptCipher* CipherDest, int64_t NewOffset);
LQ_IMPORTEXPORT void LQ_CALL LqCryptCipherCopy(LqCryptCipher* CipherDest, LqCryptCipher* CipherSource);

LQ_IMPORTEXPORT bool LQ_CALL LqCryptMacOpen(LqCryptMac* MacDest, const char* Formula, const void* Key, intptr_t KeyLen);
LQ_IMPORTEXPORT bool LQ_CALL LqCryptMacUpdate(LqCryptMac* MacDest, const void* Src, intptr_t Len);
LQ_IMPORTEXPORT void LQ_CALL LqCryptMacCopy(LqCryptMac* MacDest, LqCryptMac* MacSource);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptMacFinal(LqCryptMac* MacDest, void* DestRes);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptMacGetLenResult(LqCryptMac* ctx);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptMacMemReg(const char* Formula, const void* Key, intptr_t KeyLen, const void* SrcReg, intptr_t Len, void* Res);

LQ_IMPORTEXPORT bool LQ_CALL LqCryptHashOpen(LqCryptHash* HashDest, const char* Formula);
LQ_IMPORTEXPORT bool LQ_CALL LqCryptHashUpdate(LqCryptHash* HashDest, const void* Src, intptr_t Len);
LQ_IMPORTEXPORT void LQ_CALL LqCryptHashCopy(LqCryptHash* HashDest, LqCryptHash* HashSource);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptHashFinal(LqCryptHash* HashDest, void* DestRes);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptHashGetLenResult(LqCryptHash* HashDest);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptHashMemReg(const char* Formula, const void* SrcReg, intptr_t Len, void* Res);

LQ_IMPORTEXPORT bool LQ_CALL CryptCipherAppendLayerOnFbuf(LqFbuf* lqaio lqats Context, const char* lqain Formula, const void* InitVector, const void *Key, size_t KeyLen, int NumRounds, int Flags);
LQ_IMPORTEXPORT bool LQ_CALL CryptHashAppendLayerOnFbuf(LqFbuf* lqaio lqats Context, const char* lqain Formula, bool Rd);
LQ_IMPORTEXPORT bool LQ_CALL CryptMacAppendLayerOnFbuf(LqFbuf* lqaio lqats Context, const char* lqain Formula, const void* Key, intptr_t KeyLen, bool Rd);

/* Start RSA */

/*
    Generate public and private keys
        @RsaKeys - Dest rsa key context
        @f_rng - (Optional) User function for generate random. If null used LqCryptRnd func.
        @p_rng - (Optional) Data for @f_rng function
        @nbits - Key bit length. Usually used 1024.
        @exponent - User expanent. You can use 65537 for high-quality protection or another big number.
        @return - return false when have error, true when success
*/
LQ_IMPORTEXPORT bool LQ_CALL LqCryptRsaCreateKeys(LqCryptRsa lqaout *RsaKeys, int(*f_rng)(void *, unsigned char *, size_t), void *p_rng, unsigned int nbits, int exponent);

/*
    Encrypt/decrypt block by public key
        @RsaKeys - Rsa key context (must have public keys E and N)
        @pt - Input block address
        @ct - Dest block address 
        @Len - Length of all blocks
        @return - return false when have error, true when success
*/
LQ_IMPORTEXPORT bool LQ_CALL LqCryptRsaPublicEncDec(LqCryptRsa* lqain RsaKeys, const void* lqain pt, void* lqaout ct, size_t Len);

/*
    Encrypt/decrypt block by public key
        @RsaKeys - Rsa key context (must have public keys E and N)
        @WithoutCrt - Is not use accel.
        @pt - Input block address
        @ct - Dest block address
        @Len - Length of all blocks
        @return - return false when have error, true when success
*/
LQ_IMPORTEXPORT bool LQ_CALL LqCryptRsaPrivateEncDec(
    LqCryptRsa* lqain RsaKeys,
    bool WithoutCrt,
    const void* lqain pt,
    void* lqaout ct,
    size_t Len
);

/*
    Get length of one block
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqCryptRsaLenBlock(const LqCryptRsa* lqain RsaKeys);

/*
    Import keys
*/
LQ_IMPORTEXPORT bool LQ_CALL LqCryptRsaImportRaw(
    LqCryptRsa lqaout *RsaKeys,
    /* Public key */
    const void* lqain lqaopt BufE, size_t SizeE,
    /* For public and private key */
    const void* lqain BufN, size_t SizeN, 
    /* Private keys */
    const void* lqain lqaopt BufD, size_t SizeD,
    const void* lqain lqaopt BufP, size_t SizeP,
    const void* lqain lqaopt BufQ, size_t SizeQ,

    /* Optimal private keys (If it is not specified, then automatically calculated from P and Q) */
    const void* lqain lqaopt BufDP, size_t SizeDP,
    const void* lqain lqaopt BufDQ, size_t SizeDQ,
    const void* lqain lqaopt BufQP, size_t SizeQP,
    bool IsCheck /* Is need check key values */
);

/*
    Export keys
*/
LQ_IMPORTEXPORT bool LQ_CALL LqCryptRsaExportRaw(
    LqCryptRsa *RsaKeys,
    /* Public key */
    void* lqaout lqaopt BufE, size_t* lqaio lqaopt SizeE,
    /* For public and private key */
    void* lqaout lqaopt BufN, size_t* lqaio lqaopt SizeN,
    /* Private keys */
    void* lqaout lqaopt BufD, size_t* lqaio lqaopt SizeD,
    void* lqaout lqaopt BufP, size_t* lqaio lqaopt SizeP,
    void* lqaout lqaopt BufQ, size_t* lqaio lqaopt SizeQ,

    /* Optimal private keys  */
    void* lqaout lqaopt BufDP, size_t* lqaio lqaopt SizeDP,
    void* lqaout lqaopt BufDQ, size_t* lqaio lqaopt SizeDQ,
    void* lqaout lqaopt BufQP, size_t* lqaio lqaopt SizeQP
);

/* 
    Free keys context
*/
LQ_IMPORTEXPORT void LQ_CALL LqCryptRsaFree(LqCryptRsa *RsaKeys);

/* End RSA */

/*
    Cryptographic randomizer
        Used cipher chain. 
*/
LQ_IMPORTEXPORT void LQ_CALL LqCryptRnd(void* lqaout DestBuf, size_t BufSize);

LQ_EXTERN_C_END

#endif