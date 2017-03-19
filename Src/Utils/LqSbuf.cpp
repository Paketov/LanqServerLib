/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqSbuf... - (Lanq Stream Buffer) Implement faster stream buffer for async send or recive data from/in socket.
*/

#include "LqConn.h"
#include "LqFile.h"
#include "LqSbuf.h"
#include "LqAlloc.hpp"
#include "LqStr.h"
#include "LqErr.h"
#include "LqCp.h"
#include "LqAtm.hpp"


#ifndef _LQ_WITHOUT_STD_C_LIB
#include <string.h>
#include <math.h>
#endif

#define __METHOD_DECLS__
#include "LqAlloc.hpp"


#define LQFRWBUF_FAST_LOCK_INIT(Var)    LqAtmLkInit(Var)
#define LQFRWBUF_FAST_LOCK(Var)         LqAtmLkWr(Var)
#define LQFRWBUF_FAST_UNLOCK(Var)       LqAtmUlkWr(Var)
#define LQFRWBUF_FAST_LOCK_UNINIT(Var)  

#define LQFRWBUF_DEFAULT_LOCK_INIT(Var)     LqMutexCreate(&(Var))
#define LQFRWBUF_DEFAULT_LOCK(Var)          LqMutexLock(&(Var))
#define LQFRWBUF_DEFAULT_UNLOCK(Var)        LqMutexUnlock(&(Var))
#define LQFWRBUF_DEFAULT_LOCK_UNINIT(Var)   LqMutexClose(&(Var))


#ifdef LQ_DEBUG
# define LQSBUF_IS_DEBUG
#endif

#ifdef LQSBUF_IS_DEBUG
# define LqSbufTest(Sbuf) _LqSbufTest(Sbuf)
#else
# define LqSbufTest(Sbuf) ((void)0)
#endif

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct PageHeader {
    intptr_t SizePage;
	intptr_t StartOffset;
	intptr_t EndOffset;
	PageHeader* NextPage;
} PageHeader;

#pragma pack(pop)

#ifdef _LQ_WITHOUT_STD_C_LIB
static void* cust_memcpy(void *dstpp, const void *srcpp, size_t len) {
    register uintptr_t* dstp = (uintptr_t*)dstpp;
    register uintptr_t* srcp = (uintptr_t*)srcpp;
    uintptr_t* srcp_m = (uintptr_t*)((char*)srcpp + len);
    for(register uintptr_t* m = srcp_m - 1; srcp <= m; srcp++, dstp++)
        *dstp = *srcp;
    for(; srcp < srcp_m; srcp = (uintptr_t*)((char*)srcp + 1), dstp = (uintptr_t*)((char*)dstp + 1))
        *(char*)dstp = *(char*)srcp;
    return dstpp;
}

#define log2    0.693147180559945309e0
#define ln10o1  .4342944819032518276511
#define sqrto2  0.707106781186547524e0
#define p0      -.240139179559210510e2
#define p1      0.309572928215376501e2
#define p2      -.963769093377840513e1
#define p3      0.421087371217979714e0
#define q0      -.120069589779605255e2
#define q1      0.194809660700889731e2
#define q2      -.891110902798312337e1


static long double cust_log(long double x);

static long double cust_frexp(long double x, int *eptr);

static long double cust_log(long double arg) {
    static const long double Ind = (9999e+200 * 9999e+200 * 9999e+200) * 0;
    long double x, z, zsq, temp;
    int exp;
    if(arg <= 0.0)
        return Ind;
    x = cust_frexp(arg, &exp);
    while(x < 0.5) {
        x *= 2.0;
        exp--;
    }
    if(x < sqrto2) {
        x *= 2.0;
        exp--;
    }
    z = (x - 1.0) / (x + 1.0);
    zsq = z*z;
    temp = ((p3*zsq + p2)*zsq + p1)*zsq + p0;
    temp = temp / (((zsq + q2)*zsq + q1)*zsq + q0);
    temp = temp*z + exp*log2;
    return temp;
}

static long double cust_frexp(long double x, int *eptr) {
    static const long double two54 = 1.80143985094819840000e+16; /* 0x43500000, 0x00000000 */
    int32_t hx, ix, lx;
    uint64_t v = (*(uint64_t*)&x);
    hx = v >> 32;
    lx = v & 0xffff;
    ix = 0x7fffffff & hx;
    *eptr = 0;
    if(ix >= 0x7ff00000 || ((ix | lx) == 0))
        return x;   /* 0,inf,nan */
    if(ix < 0x00100000) {
        /* subnormal */
        x *= two54;
        hx = (*(uint64_t*)&x) >> 32;
        ix = hx & 0x7fffffff;
        *eptr = -54;
    }
    *eptr += (ix >> 20) - 1022;
    hx = (hx & 0x800fffff) | 0x3fe00000;
    (*(uint64_t*)&x) &= ((uint64_t)0x00000000ffffffff);
    (*(uint64_t*)&x) |= ((uint64_t)hx << 32);
    return x;
}

static long double cust_modf(long double d, long double *ip) {
    int e;
    long double x;
    if(d < 1) {
        if(d < 0) {
            x = cust_modf(-d, ip);
            *ip = -*ip;
            return -x;
        }
        *ip = 0;
        return d;
    }
    x = d;
    e = ((((*(uint64_t*)&x) & 0xffffffff00000000ULL) >> 52) & 0x7ffL) - 1022L;
    if(e <= 20 + 1) {
        (*(uint64_t*)&x) &= (~(0x1fffffULL >> e) << 32);
    } else if(e <= 20 + 33) {
        (*(uint64_t*)&x) &= 0xffffffff00000000 | ~(0x7fffffffULL >> (e - 20 - 2));
    }
    *ip = x;
    return d - x;
}

static long double cust_floor(long double d) {
    long double fract;
    if(d < 0) {
        fract = cust_modf(-d, &d);
        if(fract != 0.0)
            d += 1;
        d = -d;
    } else {
        cust_modf(d, &d);
    }
    return d;
}

static long double cust_ceil(long double d) {
    return -cust_floor(-d);
}

static long double cust_ldexp(long double d, int e) {
    static const long double Inf = 9999e+200 * 9999e+200 * 9999e+200;
    if(d == 0)
        return 0;
    e += ((*(uint64_t*)&d) >> 52) & 0x7ffL;
    if(e <= 0)
        return 0;   /* underflow */
    if(e >= 0x7ffL) {
        if(d < 0)
            return Inf;
        return Inf;
    }
    (*(uint64_t*)&d) &= (~(0x7ffULL << 52) | 0xffffffffULL);
    (*(uint64_t*)&d) |= (uint64_t)e << 52;
    return d;
}


#define p0  .2080384346694663001443843411e7
#define p1  .3028697169744036299076048876e5
#define p2  .6061485330061080841615584556e2
#define q0  .6002720360238832528230907598e7
#define q1  .3277251518082914423057964422e6
#define q2  .1749287689093076403844945335e4
#define log2e  1.4426950408889634073599247
#define sqrt2  1.4142135623730950488016887
#define maxf  10000

static long double cust_exp(long double arg) {
    static const long double Inf = 9999e+200 * 9999e+200 * 9999e+200;
    long double fract, temp1, temp2, xsq;
    int ent;

    if(arg == 0)
        return 1;
    if(arg < -maxf)
        return 0;
    if(arg > maxf)
        return Inf;
    arg *= log2e;
    ent = cust_floor(arg);
    fract = (arg - ent) - 0.5;
    xsq = fract*fract;
    temp1 = ((p2*xsq + p1)*xsq + p0)*fract;
    temp2 = ((xsq + q2)*xsq + q1)*xsq + q0;
    return cust_ldexp(sqrt2*(temp2 + temp1) / (temp2 - temp1), ent);
}

static long double cust_sqrt(long double arg) {
    long double x, temp;
    int exp, i;
    static const long double Inf = 9999e+200 * 9999e+200 * 9999e+200;
    static const long double Ind = (9999e+200 * 9999e+200 * 9999e+200) * 0;
    if(arg <= 0) {
        if(arg < 0)
            return Ind;
        return 0;
    }
    if(Inf == arg)
        return arg;
    x = cust_frexp(arg, &exp);
    while(x < 0.5) {
        x *= 2;
        exp--;
    }
    if(exp & 1) {
        x *= 2;
        exp--;
    }
    temp = 0.5 * (1.0 + x);

    while(exp > 60) {
        temp *= (1L << 30);
        exp -= 60;
    }
    while(exp < -60) {
        temp /= (1L << 30);
        exp += 60;
    }
    if(exp >= 0)
        temp *= 1L << (exp / 2);
    else
        temp /= 1L << (-exp / 2);
    for(i = 0; i <= 4; i++)
        temp = 0.5*(temp + arg / temp);
    return temp;
}

static long double cust_pow(long double x, long double y) {
    static const long double Ind = (9999e+200 * 9999e+200 * 9999e+200) * 0;
    long double xy, y1, ye;
    long i;
    int ex, ey, flip;

    if(y == 0.0)
        return 1.0;
    flip = 0;
    if(y < 0.) {
        y = -y;
        flip = 1;
    }
    y1 = cust_modf(y, &ye);
    if(y1 != 0.0) {
        if(x <= 0.)
            goto zreturn;
        if(y1 > 0.5) {
            y1 -= 1.;
            ye += 1.;
        }
        xy = cust_exp(y1 * cust_log(x));
    } else
        xy = 1.0;
    if(ye > 0x7FFFFFFF) {   /* should be ~0UL but compiler can't convert double to ulong */
        if(x <= 0.) {
zreturn:
            if(x == 0. && !flip)
                return 0.;
            return Ind;
        }
        if(flip) {
            if(y == .5)
                return 1. / cust_sqrt(x);
            y = -y;
        } else if(y == .5)
            return cust_sqrt(x);
        return cust_exp(y * cust_log(x));
    }
    x = cust_frexp(x, &ex);
    ey = 0;
    i = ye;
    if(i)
        for(;;) {
            if(i & 1) {
                xy *= x;
                ey += ex;
            }
            i >>= 1;
            if(i == 0)
                break;
            x *= x;
            ex <<= 1;
            if(x < .5) {
                x += x;
                ex -= 1;
            }
        }
    if(flip) {
        xy = 1. / xy;
        ey = -ey;
    }
    return cust_ldexp(xy, ey);
}
#define pow cust_pow
#define log cust_log
#define floor cust_floor
#define ceil cust_ceil
#define memcpy cust_memcpy
#endif

template<size_t LenPage>
static PageHeader* LqSbufCreatePage(LqSbuf* StreamBuf) {
    typedef struct PageSize { uint8_t v[LenPage]; }PageSize;
    auto NewPage = LqFastAlloc::New<PageSize>();
    if(NewPage == nullptr)
        return nullptr;
    PageHeader* Hdr = (PageHeader*)NewPage;
    if(StreamBuf->PageN == nullptr) {
        StreamBuf->Page0 = StreamBuf->PageN = Hdr;
    } else {
        ((PageHeader*)StreamBuf->PageN)->NextPage = Hdr;
        StreamBuf->PageN = Hdr;
    }
    Hdr->NextPage = nullptr;
    Hdr->EndOffset = Hdr->StartOffset = ((size_t)0);
    Hdr->SizePage = sizeof(PageSize) - sizeof(PageHeader);
    return Hdr;
}

static PageHeader* LqSbufCreatePage(LqSbuf* StreamBuf, intptr_t Size) {
    if(Size <= ((intptr_t)0)) return nullptr;
    if(Size <= ((intptr_t)(4096 - sizeof(PageHeader))))
        return LqSbufCreatePage<4096>(StreamBuf);
    else if(Size <= ((intptr_t)(16384 - sizeof(PageHeader))))
        return LqSbufCreatePage<16384>(StreamBuf);
    else
        return LqSbufCreatePage<32768>(StreamBuf);
}

template<size_t LenPage>
static void LqSbufRemovePage(LqSbuf* StreamBuf) {
    struct PageSize { uint8_t v[LenPage]; };
    PageHeader* Hdr = (PageHeader*)StreamBuf->Page0;
    StreamBuf->Page0 = Hdr->NextPage;
    if(StreamBuf->Page0 == nullptr)
        StreamBuf->PageN = nullptr;
    LqFastAlloc::Delete<PageSize>((PageSize*)Hdr);
}

static void LqSbufRemoveLastPage(LqSbuf* StreamBuf) {
    PageHeader* Hdr = (PageHeader*)StreamBuf->PageN;
    if(Hdr->SizePage <= (4096 - sizeof(PageHeader)))
        LqSbufRemovePage<4096>(StreamBuf);
    else if(Hdr->SizePage <= (16384 - sizeof(PageHeader)))
        LqSbufRemovePage<16384>(StreamBuf);
    else if(Hdr->SizePage <= (32768 - sizeof(PageHeader)))
        LqSbufRemovePage<32768>(StreamBuf);
}

static void LqSbufRemoveFirstPage(LqSbuf* StreamBuf) {
    PageHeader* Hdr = (PageHeader*)StreamBuf->Page0;
    if(Hdr->SizePage <= (4096 - sizeof(PageHeader)))
        LqSbufRemovePage<4096>(StreamBuf);
    else if(Hdr->SizePage <= (16384 - sizeof(PageHeader)))
        LqSbufRemovePage<16384>(StreamBuf);
    else if(Hdr->SizePage <= (32768 - sizeof(PageHeader)))
        LqSbufRemovePage<32768>(StreamBuf);
}

static intptr_t LqSbufAddPages(LqSbuf* StreamBuf, const void* Data, intptr_t Size) {
    intptr_t Written = ((intptr_t)0), SizeWrite;
    PageHeader* DestPage;
    do {
        if((DestPage = LqSbufCreatePage(StreamBuf, Size)) == nullptr)
            break;
        SizeWrite = lq_min(Size, DestPage->SizePage);
        memcpy(DestPage + 1, (char*)Data + Written, SizeWrite);
        DestPage->EndOffset = DestPage->StartOffset + SizeWrite;
        Written += SizeWrite;
        Size -= SizeWrite;
    } while(Size > ((intptr_t)0));
    StreamBuf->Len += Written;
    return Written;
}

static void _LqSbufTest(LqSbuf* StreamBuf) {
	size_t CommonSize = 0;
	int* InvalidPtr = NULL;
	for(PageHeader* Page = (PageHeader*)StreamBuf->Page0; Page; Page = (PageHeader*)Page->NextPage) {
		CommonSize += (Page->EndOffset - Page->StartOffset);
	}
	if(CommonSize != StreamBuf->Len) {
		*InvalidPtr = 0;
	}
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufWrite(LqSbuf* StreamBuf, const void* DataSource, intptr_t Size) {
    intptr_t CommonWritten, SizeWrite;
    PageHeader* Hdr;
	if(StreamBuf->PageN == NULL) {
		CommonWritten = LqSbufAddPages(StreamBuf, DataSource, Size);
		LqSbufTest(StreamBuf);
		return CommonWritten;
	}
    CommonWritten = ((intptr_t)0);
    if(((PageHeader*)StreamBuf->PageN)->EndOffset < ((PageHeader*)StreamBuf->PageN)->SizePage) {
        Hdr = (PageHeader*)StreamBuf->PageN;
        SizeWrite = lq_min(Hdr->SizePage - Hdr->EndOffset, Size);
        memcpy((char*)(Hdr + 1) + Hdr->EndOffset, DataSource, SizeWrite);
        DataSource = (char*)DataSource + SizeWrite;
        Size -= SizeWrite;
        Hdr->EndOffset += SizeWrite;
        CommonWritten += SizeWrite;
        StreamBuf->Len += SizeWrite;
    }
    if(Size >((intptr_t)0))
        CommonWritten += LqSbufAddPages(StreamBuf, DataSource, Size);
	LqSbufTest(StreamBuf);
    return CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufRead(LqSbuf* StreamBuf, void* DestBuf, intptr_t Size) {
    PageHeader* HdrPage;
    intptr_t CommonReadedSize = ((intptr_t)0), PageDataSize, ReadSize;

    while(StreamBuf->Page0 != NULL) {
        HdrPage = (PageHeader*)StreamBuf->Page0;
        PageDataSize = HdrPage->EndOffset - HdrPage->StartOffset;
        ReadSize = lq_min(PageDataSize, Size);
        if(DestBuf != NULL)
            memcpy((char*)DestBuf + CommonReadedSize, (char*)(HdrPage + 1) + HdrPage->StartOffset, ReadSize);
        CommonReadedSize += ReadSize;
        Size -= ReadSize;
        if(Size <= ((intptr_t)0)) {
            HdrPage->StartOffset += ReadSize;
            break;
        } else {
            LqSbufRemoveFirstPage(StreamBuf);
        }
    }
    StreamBuf->Len -= CommonReadedSize;
    StreamBuf->GlobOffset += CommonReadedSize;
	LqSbufTest(StreamBuf);
    return CommonReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufPeek(const LqSbuf* StreamBuf, void* DataDest, intptr_t Size) {
    intptr_t ReadedSize = ((intptr_t)0), LenRead;
    PageHeader* Hdr;
    for(Hdr = (PageHeader*)StreamBuf->Page0; (Size > ReadedSize) && (Hdr != NULL); Hdr = (PageHeader*)Hdr->NextPage) {
        LenRead = lq_min(Size - ReadedSize, Hdr->EndOffset - Hdr->StartOffset);
        memcpy((char*)DataDest + ReadedSize, (char*)(Hdr + 1) + Hdr->StartOffset, LenRead);
        ReadedSize += LenRead;
    }
    return ReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufTransfer(LqSbuf* StreamBufDest, LqSbuf* StreamBufSource, intptr_t Size) {
    LqSbufReadRegion Region;
    for(bool __r = LqSbufReadRegionFirst(StreamBufSource, &Region, Size); __r; LqSbufReadRegionNext(&Region)) {
        if((Region.Written = LqSbufWrite(StreamBufDest, Region.Source, Region.SourceLen)) < ((intptr_t)0)) {
            Region.Written = ((intptr_t)0);
            Region.Fin = true;
        }
    }
    return Region.CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufCopy(LqSbuf* StreamDest, const LqSbuf* StreamSource) {
    PageHeader* Hdr, *NewHdr;
    for(Hdr = (PageHeader*)StreamSource->Page0; (Hdr != NULL); Hdr = (PageHeader*)Hdr->NextPage) {
        NewHdr = LqSbufCreatePage(StreamDest, Hdr->SizePage);
        if(NewHdr == NULL)
            return -((intptr_t)1);
        memcpy(((char*)(NewHdr + 1)) + Hdr->StartOffset, ((char*)(Hdr + 1)) + Hdr->StartOffset, Hdr->EndOffset - Hdr->StartOffset);
        NewHdr->StartOffset = Hdr->StartOffset;
        NewHdr->EndOffset = Hdr->EndOffset;
    }
    StreamDest->Len = StreamSource->Len;
    StreamDest->GlobOffset = StreamSource->GlobOffset;
    return StreamDest->Len;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionFirst(LqSbuf* StreamBuf, LqSbufReadRegion* Reg, intptr_t Size) {
    Reg->CommonWritten = ((intptr_t)0);
    Reg->Fin = false;
	if(StreamBuf->Page0 == NULL) {
		LqSbufTest(StreamBuf);
		return false;
	}
    Reg->_Size = Size;
    Reg->_StreamBuf = StreamBuf;
    Reg->_Hdr = (PageHeader*)StreamBuf->Page0;
    Reg->_PageDataSize = ((PageHeader*)Reg->_Hdr)->EndOffset - ((PageHeader*)Reg->_Hdr)->StartOffset;
    Reg->SourceLen = lq_min(Reg->_PageDataSize, Reg->_Size);
    Reg->Source = (char*)(((PageHeader*)Reg->_Hdr) + 1) + ((PageHeader*)Reg->_Hdr)->StartOffset;
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionNext(LqSbufReadRegion* Reg) {
    Reg->CommonWritten += Reg->Written;
    Reg->_Size -= Reg->Written;
    if(Reg->Written < Reg->_PageDataSize) {
        ((PageHeader*)Reg->_Hdr)->StartOffset += Reg->Written;  
lblOut:
        Reg->_StreamBuf->Len -= Reg->CommonWritten;
        Reg->_StreamBuf->GlobOffset += Reg->CommonWritten;
		LqSbufTest(Reg->_StreamBuf);
        return false;
    }
    LqSbufRemoveFirstPage(Reg->_StreamBuf);
    if((Reg->Written < Reg->SourceLen) || ((PageHeader*)Reg->_StreamBuf->Page0 == NULL) || Reg->Fin)
        goto lblOut;
    Reg->_Hdr = (PageHeader*)Reg->_StreamBuf->Page0;
    Reg->_PageDataSize = ((PageHeader*)Reg->_Hdr)->EndOffset - ((PageHeader*)Reg->_Hdr)->StartOffset;
    Reg->SourceLen = lq_min(Reg->_PageDataSize, Reg->_Size);
    Reg->Source = (char*)(((PageHeader*)Reg->_Hdr) + 1) + ((PageHeader*)Reg->_Hdr)->StartOffset;
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionIsEos(LqSbufReadRegion* Reg) {
    return ((PageHeader*)Reg->_Hdr)->NextPage == NULL;
}

LQ_EXTERN_C bool LQ_CALL LqSbufWriteRegionFirst(LqSbuf* StreamBuf, LqSbufWriteRegion* Reg, intptr_t Size) {
    Reg->CommonReaded = ((intptr_t)0);
    Reg->_Size = Size;
    Reg->Fin = false;
    Reg->_StreamBuf = StreamBuf;
    if(StreamBuf->PageN == NULL) {
lblOperationZero:
		if((Reg->_Hdr = LqSbufCreatePage(StreamBuf, Reg->_Size)) == NULL) {
			LqSbufTest(Reg->_StreamBuf);
			return false;
		}
        Reg->DestLen = lq_min(Reg->_Size, ((PageHeader*)Reg->_Hdr)->SizePage);
        Reg->Dest = ((PageHeader*)Reg->_Hdr) + 1;
        Reg->_TypeOperat = ((intptr_t)0);
        return true;
    }
    if(((PageHeader*)StreamBuf->PageN)->EndOffset < ((PageHeader*)StreamBuf->PageN)->SizePage) {
        Reg->_Hdr = (PageHeader*)StreamBuf->PageN;
        Reg->DestLen = lq_min(((PageHeader*)Reg->_Hdr)->SizePage - ((PageHeader*)Reg->_Hdr)->EndOffset, Reg->_Size);
		if(Reg->DestLen < ((intptr_t)0)) {
			LqSbufTest(Reg->_StreamBuf);
			return false;
		}
        if(Reg->DestLen == ((intptr_t)0))
            goto lblOperationZero;
        Reg->Dest = (char*)(((PageHeader*)Reg->_Hdr) + 1) + ((PageHeader*)Reg->_Hdr)->EndOffset;
        Reg->_TypeOperat = ((intptr_t)1);
        return true;
    }
    goto lblOperationZero;
}

LQ_EXTERN_C bool LQ_CALL LqSbufWriteRegionNext(LqSbufWriteRegion* Reg) {
    Reg->CommonReaded += Reg->Readed;
    ((PageHeader*)Reg->_Hdr)->EndOffset += Reg->Readed;
    Reg->_Size -= Reg->Readed;
    if(Reg->_TypeOperat == ((intptr_t)0)) {
        if((Reg->Readed < Reg->DestLen) || (Reg->_Size <= ((intptr_t)0)) || Reg->Fin) {
           // if(Reg->Readed == ((intptr_t)0))
              //  LqSbufRemoveLastPage(Reg->_StreamBuf);
            goto lblOut;
        }
lblOpZero:
        if((Reg->_Hdr = LqSbufCreatePage(Reg->_StreamBuf, Reg->_Size)) == NULL)
            goto lblOut;
        Reg->DestLen = lq_min(Reg->_Size, ((PageHeader*)Reg->_Hdr)->SizePage);
        Reg->Dest = ((PageHeader*)Reg->_Hdr) + 1;
        return true;
    }
    if(Reg->_TypeOperat == ((intptr_t)1)) {
        if((Reg->Readed < Reg->DestLen) || (Reg->_Size <= ((intptr_t)0)) || Reg->Fin)
            goto lblOut;
        Reg->_TypeOperat = ((intptr_t)0);
        goto lblOpZero;
    }
lblOut:
    Reg->_StreamBuf->Len += Reg->CommonReaded;
	LqSbufTest(Reg->_StreamBuf);
    return false;
}

LQ_EXTERN_C void LQ_CALL LqSbufPtrSet(LqSbufPtr* StreamPointerDest, LqSbuf* StreamBuf) {
    StreamPointerDest->StreamBuf = StreamBuf;
    if(StreamBuf->Len <= ((size_t)0)) {
        static char buf;
        LqSbufWrite(StreamBuf, &buf, ((intptr_t)1));
        LqSbufRead(StreamBuf, &buf, ((intptr_t)1));
    }
    StreamPointerDest->GlobOffset = StreamBuf->GlobOffset;
    StreamPointerDest->Page = StreamBuf->Page0;
    StreamPointerDest->OffsetInPage = (StreamBuf->Page0 != NULL) ? ((PageHeader*)StreamBuf->Page0)->StartOffset : ((size_t)0);
}

LQ_EXTERN_C void LQ_CALL LqSbufPtrCopy(LqSbufPtr* StreamPointerDest, const LqSbufPtr* StreamPointerSource) {
    *StreamPointerDest = *StreamPointerSource;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufReadByPtr(LqSbufPtr* StreamPointer, void* DataDest, intptr_t Size) {
    LqSbufReadRegionPtr RegionPtr;
    void* CurPos = DataDest;
    for(bool __r = LqSbufReadRegionPtrFirst(StreamPointer, &RegionPtr, Size); __r; __r = LqSbufReadRegionPtrNext(&RegionPtr)) {
        if(DataDest != NULL) {
            memcpy(CurPos, RegionPtr.Source, RegionPtr.SourceLen);
            CurPos = ((char*)CurPos + RegionPtr.SourceLen);
        }
        RegionPtr.Written = RegionPtr.SourceLen;
    }
    return RegionPtr.CommonWritten;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionPtrFirst(LqSbufPtr* StreamPointer, LqSbufReadRegionPtr* Reg, intptr_t Size) {
    Reg->Fin = false;
    Reg->CommonWritten = ((intptr_t)0);
    Reg->_StreamPointer = (LqSbufPtr*)StreamPointer;
    if((StreamPointer->GlobOffset < StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamPointer->StreamBuf->GlobOffset + StreamPointer->StreamBuf->Len)))
        return false;
    if(StreamPointer->StreamBuf->Len <= ((size_t)0))
        return false;
    if((StreamPointer->GlobOffset == StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->Page == NULL)) {
        StreamPointer->Page = StreamPointer->StreamBuf->Page0;
        StreamPointer->OffsetInPage = ((PageHeader*)StreamPointer->Page)->StartOffset;
    }
    Reg->_Size = Size;
    Reg->SourceLen = lq_min((size_t)Reg->_Size, ((PageHeader*)StreamPointer->Page)->EndOffset - StreamPointer->OffsetInPage);
    Reg->Source = (char*)(((PageHeader*)StreamPointer->Page) + 1) + StreamPointer->OffsetInPage;
    if(Reg->SourceLen == ((intptr_t)0)) {
        Reg->Written = ((intptr_t)0);
        return LqSbufReadRegionPtrNext(Reg);
    }
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionPtrNext(LqSbufReadRegionPtr* Reg) {
    Reg->CommonWritten += Reg->Written;
    Reg->_StreamPointer->GlobOffset += Reg->Written;
    if((((PageHeader*)Reg->_StreamPointer->Page)->NextPage == NULL) || (Reg->CommonWritten >= Reg->_Size) || (Reg->Written < Reg->SourceLen) || Reg->Fin) {
        Reg->_StreamPointer->OffsetInPage += Reg->Written;
        return false;
    }
    Reg->_StreamPointer->Page = ((PageHeader*)Reg->_StreamPointer->Page)->NextPage;
    Reg->_StreamPointer->OffsetInPage = ((PageHeader*)Reg->_StreamPointer->Page)->StartOffset;
    Reg->SourceLen = lq_min((size_t)Reg->_Size - (size_t)Reg->CommonWritten, ((PageHeader*)Reg->_StreamPointer->Page)->EndOffset - Reg->_StreamPointer->OffsetInPage);
    Reg->Source = (char*)(((PageHeader*)Reg->_StreamPointer->Page) + 1) + Reg->_StreamPointer->OffsetInPage;
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionPtrIsEos(LqSbufReadRegionPtr* Reg) {
    return ((PageHeader*)Reg->_StreamPointer->Page)->NextPage == NULL;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufTransferByPtr(LqSbufPtr* StreamPointer, LqSbuf* StreamBufDest, intptr_t Size) {
    LqSbufReadRegionPtr RegionPtr;
    for(bool __r = LqSbufReadRegionPtrFirst(StreamPointer, &RegionPtr, Size); __r; __r = LqSbufReadRegionPtrNext(&RegionPtr)) {
        RegionPtr.Written = LqSbufWrite(StreamBufDest, RegionPtr.Source, RegionPtr.SourceLen);
        if(RegionPtr.Written < ((intptr_t)0)) {
            RegionPtr.Written = ((intptr_t)0);
            RegionPtr.Fin = true;
        }
    }
    return RegionPtr.CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufPeekByPtr(LqSbufPtr* StreamPointer, void* DataDest, intptr_t Size) {
    LqSbufPtr Ptr;
    LqSbufPtrCopy(&Ptr, StreamPointer);
    return LqSbufReadByPtr(&Ptr, DataDest, Size);
}

LQ_EXTERN_C void LQ_CALL LqSbufInit(LqSbuf* StreamBuf) {
    memset(StreamBuf, 0, sizeof(LqSbuf));
}

LQ_EXTERN_C void LQ_CALL LqSbufUninit(LqSbuf* StreamBuf) {
    for(PageHeader* Hdr; (Hdr = (PageHeader*)StreamBuf->Page0) != NULL;)
        LqSbufRemoveFirstPage(StreamBuf);
}

////////////////////////////




#define FL_MINUS ((uintptr_t)0x01)
#define FL_PLUS ((uintptr_t)0x02)
#define FL_SPACE ((uintptr_t)0x04)
#define FL_SHARP ((uintptr_t)0x08)
#define FL_ZERO ((uintptr_t)0x10)
#define FL_QUE ((uintptr_t)0x20)
#define FL_DOLLAR ((uintptr_t)0x40)

#define SZ_CHAR ((uintptr_t)0)
#define SZ_SHORT ((uintptr_t)1)
#define SZ_INT ((uintptr_t)2)
#define SZ_LONG ((uintptr_t)3)
#define SZ_LONGLONG ((uintptr_t)4)

#define _DTOS_SCALE_EPS 0x01
#define _DTOS_PRINT_SIGN 0x02
#define _DTOS_UPPER_CASE 0x08
#define _DTOS_PRINT_EXP 0x10
#define _DTOS_PRINT_EXP_AUTO 0x20

#define STR_TYPE(_Type, _Str)       ((sizeof(_Type) != sizeof(char))?(_Type*)(L ## _Str):(_Type*)(_Str))
#define CHAR_TYPE(_Type, _Char)     ((_Type)((sizeof(_Type) == sizeof(char))?(_Char):(L ## _Char)))
#define DIGIT_TO_ALPHA(TypeChar, Dig) ((Dig > 9)? (Dig + (CHAR_TYPE(TypeChar,'a') - 10)): (Dig + CHAR_TYPE(TypeChar,'0')))
#define DIGIT_TO_ALPHA_UPR(TypeChar, Dig) ((Dig > 9)? (Dig + (CHAR_TYPE(TypeChar,'A') - 10)): (Dig + CHAR_TYPE(TypeChar,'0')))
#define CMP_I(Val, c)               (((Val) == (c)) || ((Val) == ('A' + ((c) - 'a'))))

#define _IntegerToString(TypeNumber, TypeChar, IsSigned, IsUpper, Number, Buf, Len, Radix, ResStart) \
{ \
    unsigned TypeNumber __AbsNum = (((Number) < 0) && ((IsSigned) >= 0))? (-Number): (Number);\
    TypeChar *__c = ((TypeChar*)(Buf)) + (Len);\
    if(IsUpper){\
    do{\
        size_t __digval = __AbsNum % (Radix);\
        *(--__c) = DIGIT_TO_ALPHA_UPR(TypeChar, __digval);\
    } while((__AbsNum /= (Radix)) > 0);}else{\
    do{\
        size_t __digval = __AbsNum % (Radix);\
        *(--__c) = DIGIT_TO_ALPHA(TypeChar, __digval);\
    } while((__AbsNum /= (Radix)) > 0);}\
    if(((IsSigned) >= 0) && ((Number) < 0))\
        *(--__c) = CHAR_TYPE(TypeChar, '-');\
    else if((IsSigned) == 1)\
        *(--__c) = CHAR_TYPE(TypeChar, '+');\
    (ResStart) = __c;\
}

static char* _DoubleToString(
    long double Number,
    char* Str,
    uintptr_t Radix,
    long double Eps1,
    unsigned int Flags,
    char Sep,
    size_t FractWidth
) {
    static const long double MinExp = 1.0e-5;
    static const long double MaxExp = 1.0e+15;
    static const long double Inf = 9999e+200 * 9999e+200 * 9999e+200;
    static const long double Ind = Inf * 0;
    static const long double Qnan = -Ind;

    long double uNumber = (Number < 0) ? -Number : Number;
    intptr_t Exp = 0;
    char*s = Str;

    if(Number < 0.0)
        *(s++) = CHAR_TYPE(char, '-');
    else if(Flags & _DTOS_PRINT_SIGN)
        *(s++) = CHAR_TYPE(char, '+');
    if(Number != Number) {   //Is NaN
        for(char* c = (Flags & _DTOS_UPPER_CASE) ? STR_TYPE(char, "1.#IND00") : STR_TYPE(char, "1.#ind00"); *s = *c; s++, c++);
        return s;
    } else if(uNumber == Inf) { //Is infinity
        for(char* c = (Flags & _DTOS_UPPER_CASE) ? STR_TYPE(char, "1.#INF00") : STR_TYPE(char, "1.#inf00"); *s = *c; s++, c++);
        return s;
    } else if(uNumber == Qnan) { //Is qnan
        for(char* c = (Flags & _DTOS_UPPER_CASE) ? STR_TYPE(char, "1.#QNAN00") : STR_TYPE(char, "1.#qnan00"); *s = *c; s++, c++);
        return s;
    } else if((Flags & _DTOS_PRINT_EXP) || ((Flags & _DTOS_PRINT_EXP_AUTO) && (uNumber != 0.0) && ((uNumber < MinExp) || (uNumber > MaxExp)))) {//Is have expanent
        long double ex = log(uNumber) / log((long double)Radix);
        Exp = (ex < 0.0) ? floor(ex) : ceil(ex);
        uNumber *= pow(Radix, -Exp);
    }

    unsigned long long Integer = (unsigned long long)uNumber;
    uNumber = uNumber - Integer + 1.0;
    if(uNumber >= 2.0) {
        uNumber--;
        Integer++;
    }

    char Buf[40];
    char *m = Buf + 40, *c = m;

    if(Flags & _DTOS_SCALE_EPS) {
        for(; Integer > 0; Integer /= Radix, Eps1 *= Radix) {
            unsigned char digval = Integer % Radix;
            *(--c) = DIGIT_TO_ALPHA(char, digval);
        }
    } else {
        for(; Integer > 0; Integer /= Radix) {
            unsigned char digval = Integer % Radix;
            *(--c) = DIGIT_TO_ALPHA(char, digval);
        }
    }
    if(c == m)
        *(--c) = CHAR_TYPE(char, '0');

    for(; c < m; c++, s++) *s = *c;
    *(s++) = Sep;

    for(long double Eps2 = 1.0 - Eps1; ((uNumber - (unsigned long long)uNumber) > Eps1) &&
        ((uNumber - (unsigned long long)uNumber) < Eps2);) {
        Eps2 = 1.0 - (Eps1 *= Radix);
        uNumber *= Radix;
    }

    Integer = (unsigned long long)uNumber;
    if((uNumber - Integer) >= 0.5)
        Integer++;

    for(c = m; Integer > 1; Integer /= Radix) {
        unsigned char digval = Integer % Radix;
        *(--c) = DIGIT_TO_ALPHA(char, digval);
    }
    if((c == m) && (FractWidth > 0))
        *(--c) = CHAR_TYPE(char, '0');
    for(size_t i = FractWidth; (c < m) && (i > 0); c++, s++, i--) *s = *c;
    if((Flags & _DTOS_PRINT_EXP) || ((Flags & _DTOS_PRINT_EXP_AUTO) && Exp)) {
        if(Radix <= 10)
            *(s++) = (Flags & _DTOS_UPPER_CASE) ? CHAR_TYPE(char, 'E') : CHAR_TYPE(char, 'e');
        else
            *(s++) = (Flags & _DTOS_UPPER_CASE) ? CHAR_TYPE(char, 'P') : CHAR_TYPE(char, 'p');
        if(Exp > 0) {
            *(s++) = CHAR_TYPE(char, '+');
        } else {
            *(s++) = CHAR_TYPE(char, '-');
            Exp = -Exp;
        }
        for(c = m; Exp > 0; Exp /= Radix) {
            unsigned char digval = Exp % Radix;
            *(--c) = DIGIT_TO_ALPHA(char, digval);
        }
        for(; c < m; c++, s++) *s = *c;
    }
    *(s++) = CHAR_TYPE(char, '\0');
    return s;
}

static inline bool _StringToInteger(const char** Str, void* Dest, unsigned Radix, bool Signed) {
    char Negative = 1;
    const char* c = *Str;
    if(Signed) {
        switch(*c) {
            case '-':
                Negative = -1;
            case '+':
                c++;
        }
    }
    long long Ret = 0LL;
    if(Radix <= 10) {
        for(; ; c++) {
            register unsigned Digit = *c - '0';
            if(Digit >= Radix)
                break;
            Ret = Ret * Radix + Digit;
        }
    } else {
        register unsigned Radx2 = Radix - 10;
        for(; ; c++) {
            register unsigned Digit = *c - '0';
            if(Digit > 9) {
                Digit = *c - 'a';
                if(Digit >= Radx2)
                    Digit = *c - 'A';
                if(Digit >= Radx2)
                    break;
                Digit += 10;
            }
            Ret = Ret * Radix + Digit;
        }
    }
    if((Signed && ((c - 1) == *Str) && ((**Str == '-') || (**Str == '+'))) || (c <= *Str))
        return false;
    *((long long*)Dest) = Ret * Negative;
    *Str = c;
    return true;
}

static char* _StringToDouble(const char* Str, long double * Dest, unsigned char Radix, bool InfInd, char Sep) {
    static const long double Inf = 9999e+200 * 9999e+200 * 9999e+200;
    static const long double Ind = Inf * 0;
    static const long double Qnan = -Ind;
    const char* c = Str, *tc;
    long long IntegerPart = 0;

    if(!_StringToInteger(&c, &IntegerPart, Radix, true))
        return NULL;

    if(*c != Sep) {
        *Dest = (long double)IntegerPart;
        return (char*)c;
    }
    c++;
    long double Result = IntegerPart;

    if(InfInd && (*c == '#')) {
        if(CMP_I(*(c + 1), 'i') && CMP_I(*(c + 2), 'n')) {
            switch(*(c + 3)) {
                case 'F':
                case 'f'://#INF
                    Result *= Inf;
                    c += 4;
                    goto lblSingOut;
                case 'D':
                case 'd'://#IND
                    Result *= Ind;
                    c += 4;
                    goto lblSingOut;
            }
        } else if(
            CMP_I(*(c + 1), 'q') &&
            CMP_I(*(c + 2), 'n') &&
            CMP_I(*(c + 3), 'a') &&
            CMP_I(*(c + 4), 'n')
            ) {//#QNAN
            Result *= Qnan;
            c += 5;
            goto lblSingOut;
        }
        return NULL;
    }

lblSingOut:

    //Get fraction part
    unsigned long long  FractPart = 0;
    _StringToInteger(&c, &FractPart, Radix, false);

    long double DoubleFract = 0.0;
    for(; FractPart > 1; FractPart /= Radix)
        DoubleFract = (DoubleFract + (long double)(FractPart % Radix)) * (long double)0.1;
    if(Result < 0.0)
        Result -= DoubleFract;
    else
        Result += DoubleFract;
    if(CMP_I(*c, 'e')) {
        long long Exp;
        c++;
        if(_StringToInteger(&c, &Exp, Radix, true))
            Result *= pow((long double)Radix, Exp);
        else
            c--;
    }
    *Dest = (long double)Result;
    return (char*)c;
}

//////////////////////////////////////////////////////////////
///////LqFbuf
/////////////////////////////////////////////////////////////


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqFbufVirtual {
    LqSbuf Buf;
    intptr_t CountPtr;
} LqFbufVirtual;

#pragma pack(pop)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)
typedef struct LqFbuf_state {
    union {
        void* StringPos;
        LqSbufPtr BufPtr;
    };
    bool IsString;
    LqFbuf* Buf;
} LqFbuf_state;

#pragma pack(pop)

#ifdef LQPLATFORM_WINDOWS

static intptr_t LQ_CALL _TermWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    DWORD Written;
    if(WriteConsoleA((HANDLE)Context->UserData, Buf, Size, &Written, NULL) == FALSE) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_WRITE_WOULD_BLOCK : LQFBUF_WRITE_ERROR);
        return ((intptr_t)0);
    }
    return Size;
}

static intptr_t LQ_CALL _TermReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    wchar_t Buf1[3] = {0};
    DWORD Readed;
    if(Size <= ((size_t)0))
        return ((intptr_t)0);
    if(ReadConsoleW((HANDLE)Context->UserData, Buf1, 1, &Readed, NULL) == FALSE) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
        return ((intptr_t)0);
    }
    if(LqStrUtf16Count(Buf1[0]) >= 2) {
        if(ReadConsoleW((HANDLE)Context->UserData, Buf1 + 1, 1, &Readed, NULL) == FALSE) {
            Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
            return -((intptr_t)1);
        }
    }
    int Res = LqCpConvertFromWcs(Buf1, Buf, Size);
    return (Res > 0) ? (Res - 1) : Res;
}

#endif

static intptr_t LQ_CALL _SockWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    int Written;
    if((Written = send((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_WRITE_WOULD_BLOCK : LQFBUF_WRITE_ERROR);
        return ((intptr_t)0);
    }
    return Written;
}

static intptr_t LQ_CALL _SockReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    int Readed;
    if((Readed = recv((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
        return ((intptr_t)0);
    }
    return Readed;
}

static intptr_t LQ_CALL _SockCloseProc(LqFbuf* Context) {
    return closesocket((int)Context->UserData);
}

static intptr_t LQ_CALL _FileWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    intptr_t Written;
    if((Written = LqFileWrite((int)Context->UserData, Buf, Size)) == -((intptr_t)1)) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_WRITE_WOULD_BLOCK : LQFBUF_WRITE_ERROR);
        return -((intptr_t)1);
    }
    return Written;
}

static intptr_t LQ_CALL _FileReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    intptr_t Readed;
    if(Size == 0)
        return ((intptr_t)0);
    if((Readed = LqFileRead((int)Context->UserData, Buf, Size)) <= 0) {
        if(Readed == ((intptr_t)0))
            Context->Flags |= LQFBUF_READ_EOF;
        else
            Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFBUF_READ_WOULD_BLOCK : LQFBUF_READ_ERROR);
        return ((intptr_t)0);
    }
    return Readed;
}

static intptr_t LQ_CALL _FileSeekProc(LqFbuf* Context, int64_t Offset, int Flags) {
    return LqFileSeek((int)Context->UserData, Offset, Flags);
}

static bool LQ_CALL _FileCopyProc(LqFbuf* Dest, LqFbuf*Source) {
    Dest->UserData = (void*)LqDescrDup((int)Source->UserData, LQ_O_NOINHERIT);
    return true;
}

static intptr_t LQ_CALL _FileCloseProc(LqFbuf* Context) {
    return LqFileClose((int)Context->UserData);
}

static intptr_t LQ_CALL _EmptySeekProc(LqFbuf*, int64_t, int) {
    return -((intptr_t)1);
}

static intptr_t LQ_CALL _EmptyReadProc(LqFbuf* Context, char*, size_t) {
    Context->Flags |= LQFBUF_READ_ERROR;
    return ((intptr_t)0);
}

static intptr_t LQ_CALL _EmptyWriteProc(LqFbuf* Context, char*, size_t) {
    Context->Flags |= LQFBUF_WRITE_ERROR;
    return ((intptr_t)0);
}

static intptr_t LQ_CALL _EmptyCloseProc(LqFbuf* Context) {
    return ((intptr_t)0);
}

static intptr_t LQ_CALL _VirtPipeWriteProc(LqFbuf* Context, char* Buf, size_t Size) {
    return LqSbufWrite(&Context->InBuf, Buf, Size);
}

static intptr_t LQ_CALL _VirtPipeReadProc(LqFbuf* Context, char* Buf, size_t Size) {
    Context->Flags |= LQFBUF_READ_EOF;
    return -((intptr_t)1);
}

static void _LqFbuf_lock(LqFbuf* Context) {
    if(Context->Flags & LQFBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK(Context->Locker);
    }
}

static void _LqFbuf_unlock(LqFbuf* Context) {
    if(Context->Flags & LQFBUF_FAST_LK) {
        LQFRWBUF_FAST_UNLOCK(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_UNLOCK(Context->Locker);
    }
}

static intptr_t LQ_CALL _NullReadProc(LqFbuf*, char* Dest, size_t Size) {
    memset(Dest, 0, Size);
    return Size;
}

static intptr_t LQ_CALL _NullWriteProc(LqFbuf*, char*, size_t Size) {
    return Size;
}

static intptr_t LQ_CALL _NullSeekProc(LqFbuf*, int64_t, int) {
    return 0;
}

static intptr_t LQ_CALL _NullCloseProc(LqFbuf*) {
    return 0;
}

static intptr_t _LqFbuf_vscanf(LqFbuf_state* State, int Flags, const char* Fmt, va_list va);
static intptr_t _LqFbuf_vprintf(LqFbuf* Context, const char* Fmt, va_list va);
static intptr_t _LqFbuf_write(LqFbuf* Context, const void* Buf, intptr_t Size);
static intptr_t _LqFbuf_flush(LqFbuf* Context);
static intptr_t _LqFbuf_read(LqFbuf* Context, void* Buf, size_t Len);

static LqFbufCookie _EmptyCookie = {
    _EmptyReadProc,
    _EmptyWriteProc,
    _EmptySeekProc,
    NULL,
    _EmptyCloseProc
};

static LqFbufCookie _NullCookie = {
    _NullReadProc,
    _NullWriteProc,
    _NullSeekProc,
    NULL,
    _NullCloseProc
};
static LqFbufCookie _SocketCookie = {
    _SockReadProc,
    _SockWriteProc,
    _EmptySeekProc,
    _FileCopyProc,
    _SockCloseProc
};

#ifdef LQPLATFORM_WINDOWS
static LqFbufCookie _TerminalCookie = {
    _TermReadProc,
    _TermWriteProc,
    _EmptySeekProc,
    _FileCopyProc,
    _FileCloseProc
};
#endif

static LqFbufCookie _FileCookie = {
    _FileReadProc,
    _FileWriteProc,
    _FileSeekProc,
    _FileCopyProc,
    _FileCloseProc
};

static LqFbufCookie _VirtPipeCookie = {
    _VirtPipeReadProc,
    _VirtPipeWriteProc,
    _EmptySeekProc,
    NULL,
    _EmptyCloseProc
};

static LqFbufCookie* _GetCookie(int Fd) {
    if(LqDescrIsSocket(Fd)) {
        return &_SocketCookie;
    }
#ifdef LQPLATFORM_WINDOWS
    else if(LqDescrIsTerminal(Fd)) {
        return &_TerminalCookie;
    }
#endif
    else {
        return &_FileCookie;
    }
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_open_cookie(
    LqFbuf* Context,
    void* UserData,
    LqFbufCookie* Cookie,
    LqFbufFlag Flags,
    intptr_t WriteMinBuffSize,
    intptr_t WriteMaxBuffSize,
    intptr_t ReadPortionSize
) {
    if(Flags & LQFBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_INIT(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK_INIT(Context->Locker);
    }
    LqSbufInit(&Context->OutBuf);
    LqSbufInit(&Context->InBuf);
    Context->Flags = Flags;

    Context->MinFlush = WriteMinBuffSize;
    Context->MaxFlush = WriteMaxBuffSize;
    Context->PortionSize = ReadPortionSize;

    Context->UserData = UserData;
    Context->Cookie = Cookie;
    return ((intptr_t)0);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_fdopen(
    LqFbuf* Context,
    LqFbufFlag Flags,
    int Fd,
    intptr_t WriteMinBuffSize,
    intptr_t WriteMaxBuffSize,
    intptr_t ReadPortionSize
) {
    LqFbufCookie * Cookie = _GetCookie(Fd);
    return LqFbuf_open_cookie(
        Context,
        (void*)Fd,
        Cookie,
        Flags & ~(LQFBUF_POINTER),
        WriteMinBuffSize,
        WriteMaxBuffSize,
        ReadPortionSize
    );
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_open(
    LqFbuf* Context,
    const char* FileName,
    uint32_t FileFlags,
    int Access,
    intptr_t WriteMinBuffSize,
    intptr_t WriteMaxBuffSize,
    intptr_t ReadPortionSize
) {
    int Fd = LqFileOpen(FileName, FileFlags, Access);
    if(Fd == -1)
        return -((intptr_t)1);
    return LqFbuf_fdopen(Context, LQFBUF_FAST_LK | LQFBUF_PUT_FLUSH | LQFBUF_PRINTF_FLUSH, Fd, WriteMinBuffSize, WriteMaxBuffSize, ReadPortionSize);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_stream(LqFbuf* Context) {
    return LqFbuf_open_cookie(Context, NULL, &_VirtPipeCookie, LQFBUF_FAST_LK | LQFBUF_STREAM, (intptr_t)0, (intptr_t)0, (intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_null(LqFbuf* Context) {
    return LqFbuf_open_cookie(Context, NULL, &_NullCookie, LQFBUF_FAST_LK, (intptr_t)0, (intptr_t)0, (intptr_t)1);
}

LQ_EXTERN_C bool LQ_CALL LqFbuf_eof(LqFbuf* Context) {
    if(Context->Flags & LQFBUF_POINTER)
        return (Context->BufPtr.StreamBuf->GlobOffset + Context->BufPtr.StreamBuf->Len) <= Context->BufPtr.GlobOffset;
    return (Context->Flags & (LQFBUF_READ_EOF | LQFBUF_STREAM)) && (Context->InBuf.Len <= 0);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_make_ptr(LqFbuf* DestSource) {
    LqFbufVirtual* NewBuf;
    intptr_t Res = ((intptr_t)0);
    _LqFbuf_lock(DestSource);

    if(!(DestSource->Flags & LQFBUF_POINTER)) {
        NewBuf = LqFastAlloc::New<LqFbufVirtual>();
        if(NewBuf == NULL) {
            Res = -((intptr_t)1);
            goto lblOut;
        }
        NewBuf->CountPtr = ((intptr_t)1);
    }

    if(DestSource->Cookie->CloseProc)
        DestSource->Cookie->CloseProc(DestSource);

    if(DestSource->Flags & LQFBUF_POINTER) {
        NewBuf = (LqFbufVirtual*)((char*)DestSource->BufPtr.StreamBuf - ((off_t)&((LqFbufVirtual*)0)->Buf));
    } else if(DestSource->OutBuf.Len < DestSource->InBuf.Len) {
        memcpy(&NewBuf->Buf, &DestSource->InBuf, sizeof(LqSbuf));
        LqSbufUninit(&DestSource->OutBuf);
    } else {
        memcpy(&NewBuf->Buf, &DestSource->OutBuf, sizeof(LqSbuf));
        LqSbufUninit(&DestSource->InBuf);
    }

    DestSource->Cookie = &_EmptyCookie;

    DestSource->Flags = LQFBUF_POINTER | (LQFBUF_FAST_LK & DestSource->Flags);
    LqSbufPtrSet(&DestSource->BufPtr, &NewBuf->Buf);
lblOut:
    _LqFbuf_unlock(DestSource);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_copy(LqFbuf* Dest, LqFbuf* Source) {
    LqFbufVirtual* VirtBuf;
    _LqFbuf_lock((LqFbuf*)Source);
    memcpy(Dest, Source, sizeof(LqFbuf));
    if(Dest->Cookie->CopyProc) {
        if(!Dest->Cookie->CopyProc(Dest, Source)) {
            _LqFbuf_unlock((LqFbuf*)Source);
            return -((intptr_t)1);
        }
    }
    if(Source->Flags & LQFBUF_POINTER) {
        LqSbufPtrCopy(&Dest->BufPtr, &Source->BufPtr);
        VirtBuf = (LqFbufVirtual*)((char*)Dest->BufPtr.StreamBuf - ((off_t)&((LqFbufVirtual*)0)->Buf));
        LqAtmIntrlkInc(VirtBuf->CountPtr);
    } else {
        LqSbufCopy(&Dest->OutBuf, &Source->OutBuf);
        LqSbufCopy(&Dest->InBuf, &Source->InBuf);
    }
    _LqFbuf_unlock((LqFbuf*)Source);
    if(Source->Flags & LQFBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_INIT(Dest->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK_INIT(Dest->Locker);
    }
    return ((intptr_t)0);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_set_ptr_cookie(LqFbuf* Dest, void* UserData, LqFbufCookie* Cookie) {
    intptr_t Res = ((intptr_t)0);
    _LqFbuf_lock((LqFbuf*)Dest);
    if(Dest->Flags & LQFBUF_POINTER) {
        Dest->Cookie = Cookie;
        Dest->UserData = UserData;
    } else {
        Res = -((intptr_t)1);
    }
    _LqFbuf_unlock((LqFbuf*)Dest);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_set_ptr_fd(LqFbuf* Dest, int Fd) {
    return LqFbuf_set_ptr_cookie(Dest, (void*)Fd, _GetCookie(Fd));
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_close(LqFbuf* Context) {
    LqFbufVirtual* DelBuf;
    intptr_t Expected, Result = ((intptr_t)0);

    _LqFbuf_lock(Context);
    _LqFbuf_flush(Context);
    if(Context->Cookie->CloseProc)
        Result = Context->Cookie->CloseProc(Context);
    if(Context->Flags & LQFBUF_POINTER) {
        DelBuf = (LqFbufVirtual*)((char*)Context->BufPtr.StreamBuf - ((off_t)&((LqFbufVirtual*)0)->Buf));
        for(Expected = DelBuf->CountPtr; !LqAtmCmpXchg(DelBuf->CountPtr, Expected, Expected - ((intptr_t)1)); Expected = DelBuf->CountPtr);
        if(Expected == ((intptr_t)1)) {
            LqSbufUninit(&DelBuf->Buf);
            LqFastAlloc::Delete(DelBuf);
        }
    } else {
        LqSbufUninit(&Context->OutBuf);
        LqSbufUninit(&Context->InBuf);
    }
    if(Context->Flags & LQFBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_UNINIT(Context->FastLocker);
    } else {
        LQFWRBUF_DEFAULT_LOCK_UNINIT(Context->Locker);
    }
    return Result;
}

LQ_EXTERN_C size_t LQ_CALL LqFbuf_sizes(const LqFbuf* Context, size_t* OutBuf, size_t* InBuf) {
    size_t a, b;
    if(Context->Flags & LQFBUF_POINTER) {
        a = (Context->BufPtr.StreamBuf->GlobOffset + Context->BufPtr.StreamBuf->Len) - Context->BufPtr.GlobOffset;
        if(OutBuf)
            *OutBuf = a;
        if(InBuf)
            *InBuf = a;
        return a;
    }
    a = Context->OutBuf.Len;
    b = Context->InBuf.Len;
    if(OutBuf)
        *OutBuf = a;
    if(InBuf)
        *InBuf = b;
    return a + b;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFbuf_transfer(LqFbuf* Dest, LqFbuf* Source, LqFileSz Size) {
    intptr_t Size3;
    LqSbufReadRegion RegionR;
    LqSbufWriteRegion RegionW;
    LqSbufReadRegionPtr RegionPtr;
    LqFileSz Size2, WriteMax;
    if(Dest->Flags & LQFBUF_POINTER)
        return -((LqFileSz)1);
    _LqFbuf_lock(Dest);
    _LqFbuf_lock(Source);
    Dest->Flags &= ~(LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
    Source->Flags &= ~(LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF);

    if(Source->Flags & LQFBUF_POINTER) {
        for(bool __r = LqSbufReadRegionPtrFirst(&Source->BufPtr, &RegionPtr, lq_min(Size, ((LqFileSz)INTPTR_MAX))); __r; __r = LqSbufReadRegionPtrNext(&RegionPtr)) {
            RegionPtr.Written = _LqFbuf_write(Dest, RegionPtr.Source, RegionPtr.SourceLen);
            RegionPtr.Fin = Dest->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
        }
        if((Source->BufPtr.StreamBuf->GlobOffset + Source->BufPtr.StreamBuf->Len) <= Source->BufPtr.GlobOffset)
            Source->Flags |= LQFBUF_READ_EOF;
        Size2 = RegionPtr.CommonWritten;
    } else {
        Size2 = Size;
lblWrite:
        for(bool __r = LqSbufReadRegionFirst(&Source->InBuf, &RegionR, lq_min(Size,  ((LqFileSz)INTPTR_MAX))); __r; __r = LqSbufReadRegionNext(&RegionR)) {
            RegionR.Written = _LqFbuf_write(Dest, RegionR.Source, RegionR.SourceLen);
            RegionR.Fin = Dest->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
        }
        Size -= ((LqFileSz)RegionR.CommonWritten);
        if(RegionR.Fin || (Size <= ((LqFileSz)0)))
            goto lblOut2;
        if(Dest->MaxFlush < ((intptr_t)4096)) {
            if(Source->InBuf.Len < ((size_t)(32768 - sizeof(PageHeader)) * 2)) {
                for(bool __r = LqSbufWriteRegionFirst(&Source->InBuf, &RegionW, lq_min(Size, ((LqFileSz)(32768 - sizeof(PageHeader)) * 2))); __r; __r = LqSbufWriteRegionNext(&RegionW)) {
                    if((RegionW.Readed = Source->Cookie->ReadProc(Source, (char*)RegionW.Dest, RegionW.DestLen)) < ((intptr_t)0)) {
                        RegionW.Readed = ((intptr_t)0);
                        RegionW.Fin = true;
                    }
                }
                if(RegionW.CommonReaded > ((intptr_t)0))
                    goto lblWrite;
            }else
                goto lblOut2;
        } else {
            while(true) {
                WriteMax = lq_max(Dest->MaxFlush - Dest->OutBuf.Len, 0);
                for(bool __r = LqSbufWriteRegionFirst(&Dest->OutBuf, &RegionW, ((intptr_t)lq_min(Size, WriteMax))); __r; __r = LqSbufWriteRegionNext(&RegionW))
                    if((RegionW.Readed = Source->Cookie->ReadProc(Source, (char*)RegionW.Dest, RegionW.DestLen)) < ((intptr_t)0)) {
                        RegionW.Readed = ((intptr_t)0);
                        RegionW.Fin = true;
                    }
                Size -= ((LqFileSz)RegionW.CommonReaded);
                if(Dest->OutBuf.Len > Dest->MinFlush) {
                    Size3 = Dest->OutBuf.Len;
                    if(_LqFbuf_flush(Dest) < Size3)
                        goto lblOut2;
                }
                if((Size <= ((LqFileSz)0)) ||
                   (Dest->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK)) ||
                   (Source->Flags & (LQFBUF_READ_EOF | LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
                    goto lblOut2;
            }
        }
lblOut2:
        Size2 -= Size;
    }
lblOut:
    _LqFbuf_unlock(Source);
    _LqFbuf_unlock(Dest);
    return Size2;
}

#define TO_LOWER(c) ((((c) >= 'A') && ((c) <= 'Z'))? (((c) - 'A') + 'a') : (c))

#define LqFbuf_transfer_save_state_ptr() \
{\
    SavedMs = ms;\
    SavedSs = ss;\
    SavedMaxReg = MaxReg;\
    memcpy(&SavedRegionPtr, &RegionPtr, sizeof(SavedRegionPtr));\
    LqSbufPtrCopy(&SavedBufPtr, SbufPtr);\
}

#define LqFbuf_transfer_restore_state_ptr() \
{\
    ms = SavedMs;\
    ss = SavedSs;\
    MaxReg = SavedMaxReg;\
    memcpy(&RegionPtr, &SavedRegionPtr, sizeof(SavedRegionPtr));\
    LqSbufPtrCopy(SbufPtr, &SavedBufPtr);\
}

static intptr_t LqFbuf_transfer_by_ptr(LqFbuf* Dest, LqSbufPtr* SbufPtr, LqFileSz Size, const char* Seq, size_t SeqSize, const bool IsCaseIndependet, bool* _Fin) {
    char*ss, *MaxReg, *SavedMaxReg, *ms, *MaxSeq = ((char*)Seq) + SeqSize, *SavedSs, *SavedMs, *LastSeq;
    register char *s, *c;
    bool Fin = false;
    LqSbufReadRegionPtr RegionPtr, SavedRegionPtr;
    LqSbufPtr SavedBufPtr;
    LastSeq = (char*)Seq;
    intptr_t sz;
    for(bool __r = LqSbufReadRegionPtrFirst(SbufPtr, &RegionPtr, ((intptr_t)lq_min(Size, ((LqFileSz)INTPTR_MAX)))); __r; __r = LqSbufReadRegionPtrNext(&RegionPtr)) {
        ss = (char*)RegionPtr.Source;
        MaxReg = ss + RegionPtr.SourceLen;
        ms = MaxReg - SeqSize + ((size_t)1);

        if(LastSeq > Seq) {
            for(s = ss, c = LastSeq; ; s++, c++) {
				if(c >= MaxSeq) {/* ƒостигнут конец контрольной последовательности*/
                    Fin |= true;
                    LqFbuf_transfer_restore_state_ptr();
                    RegionPtr.Written = _LqFbuf_write(Dest, RegionPtr.Source, ss - (char*)RegionPtr.Source);
                    RegionPtr.Fin = true;
                    Fin |= true;
                    goto lblContinue1;
                }
                if(s >= MaxReg) {/* ƒостигнут конец региона*/
                    if(LqSbufReadRegionPtrIsEos(&RegionPtr) || ((RegionPtr.CommonWritten + RegionPtr.SourceLen) >= Size)) {
                        LqFbuf_transfer_restore_state_ptr();
                        goto lblContinue4;
                    }
                    RegionPtr.Written = RegionPtr.SourceLen;
                    LastSeq = c;
                    goto lblContinue1;
                }
                if((IsCaseIndependet)? (TO_LOWER(*c) != TO_LOWER(*s)): (*c != *s)) {
                    /* Ќеправильна€ последовательность в текущей странице, откатываемс€ на страницу с которой начинаетс€ последовательность*/
                    LqFbuf_transfer_restore_state_ptr();
                    LastSeq = (char*)Seq;
                    ss++;
                    goto lblContinue3;
                }
            }
        }
        if(IsCaseIndependet) {
            for(; ss < ms; ss++) {
                for(s = ss, c = (char*)Seq; ; s++, c++) {
                    if(c >= MaxSeq) {
                        RegionPtr.Written = _LqFbuf_write(Dest, RegionPtr.Source, ss - (char*)RegionPtr.Source);
                        RegionPtr.Fin = true;
                        Fin |= true;
                        goto lblContinue1;
                    }
                    if(TO_LOWER(*c) != TO_LOWER(*s))
                        break;
                }
            }
        }else {
            for(; ss < ms; ss++) {
                for(s = ss, c = (char*)Seq; ; s++, c++) {
                    if(c >= MaxSeq) {
                        RegionPtr.Written = _LqFbuf_write(Dest, RegionPtr.Source, ss - (char*)RegionPtr.Source);
                        RegionPtr.Fin = true;
                        Fin |= true;
                        goto lblContinue1;
                    }
                    if(*c != *s)
                        break;
                }
            }
        }
lblContinue3:
        for(; ss < MaxReg; ss++) {
            if(IsCaseIndependet) {
                for(s = ss, c = (char*)Seq; s < MaxReg; s++, c++) {
                    if(TO_LOWER(*c) != TO_LOWER(*s)) goto lblContinue2;
                }
            } else {
                for(s = ss, c = (char*)Seq; s < MaxReg; s++, c++) {
                    if(*c != *s) goto lblContinue2;
                }
            }
            if(LqSbufReadRegionPtrIsEos(&RegionPtr) || ((RegionPtr.CommonWritten + RegionPtr.SourceLen) >= Size)) {
lblContinue4:
                if(ss > (char*)RegionPtr.Source)
                    RegionPtr.Written = _LqFbuf_write(Dest, RegionPtr.Source, ss - (char*)RegionPtr.Source);
                else
                    RegionPtr.Written = ((intptr_t)0);
                RegionPtr.Fin = true;
                goto lblContinue1;
            }
            LqFbuf_transfer_save_state_ptr();
            RegionPtr.Written = RegionPtr.SourceLen;
            RegionPtr.Fin = false;
            LastSeq = c;
            goto lblContinue1;
lblContinue2:;
        }
        LastSeq = (char*)Seq;
        RegionPtr.Written = _LqFbuf_write(Dest, RegionPtr.Source, RegionPtr.SourceLen);
        RegionPtr.Fin = Dest->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
lblContinue1:;
    }
    *_Fin = Fin;
    return RegionPtr.CommonWritten;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFbuf_transfer_while_not_same(LqFbuf* Dest, LqFbuf* Source, LqFileSz Size, const char* Seq, size_t SeqSize, bool IsCaseIndependet, bool* IsFound) {
    intptr_t Size3;
    LqSbufWriteRegion RegionW;
    LqSbufPtr BufPtr;
    LqFileSz Size2, WriteMax, Size1;
    bool Fin = false;
    if(Dest->Flags & LQFBUF_POINTER)
        return -((LqFileSz)1);
    _LqFbuf_lock(Dest);
    _LqFbuf_lock(Source);
    Dest->Flags &= ~(LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
    Source->Flags &= ~(LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF);
    if(Source->Flags & LQFBUF_POINTER) {
        Size2 = LqFbuf_transfer_by_ptr(Dest, &Source->BufPtr, Size, Seq, SeqSize, IsCaseIndependet, &Fin);
        if((Source->BufPtr.StreamBuf->GlobOffset + Source->BufPtr.StreamBuf->Len) <= Source->BufPtr.GlobOffset)
            Source->Flags |= LQFBUF_READ_EOF;
    } else {
        Size2 = Size;
lblWrite:
        LqSbufPtrSet(&BufPtr, &Source->InBuf);
        Size1 = LqFbuf_transfer_by_ptr(Dest, &BufPtr, Size, Seq, SeqSize, IsCaseIndependet, &Fin);
        if(Size1 > ((LqFileSz)0))
            LqSbufRead(&Source->InBuf, NULL, Size1);
        Size -= Size1;
        if(Fin || (Size <= ((LqFileSz)0)))
            goto lblOut2;
        if(Dest->MaxFlush < ((intptr_t)4096)) {
            if(Source->InBuf.Len < ((size_t)(32768 - sizeof(PageHeader)) * 2)) {
                for(bool __r = LqSbufWriteRegionFirst(&Source->InBuf, &RegionW, lq_min(Size, ((LqFileSz)(32768 - sizeof(PageHeader)) * 2))); __r; __r = LqSbufWriteRegionNext(&RegionW)) {
                    if((RegionW.Readed = Source->Cookie->ReadProc(Source, (char*)RegionW.Dest, RegionW.DestLen)) < ((intptr_t)0)) {
                        RegionW.Readed = ((intptr_t)0);
                        RegionW.Fin = true;
                    }
                }
                if(RegionW.CommonReaded > ((intptr_t)0))
                    goto lblWrite;
            }else
                goto lblOut2;
        } else {
            while(true) {
                WriteMax = lq_max(Dest->MaxFlush - Dest->OutBuf.Len, ((intptr_t)0));
                for(bool __r = LqSbufWriteRegionFirst(&Dest->OutBuf, &RegionW, lq_min(Size, WriteMax)); __r; __r = LqSbufWriteRegionNext(&RegionW))
                    if((RegionW.Readed = Source->Cookie->ReadProc(Source, (char*)RegionW.Dest, RegionW.DestLen)) < ((intptr_t)0)) {
                        RegionW.Readed = ((intptr_t)0);
                        RegionW.Fin = true;
                    }
                Size -= ((LqFileSz)RegionW.CommonReaded);
                if(Dest->OutBuf.Len > Dest->MinFlush) {
                    Size3 = Dest->OutBuf.Len;
                    if(_LqFbuf_flush(Dest) < Size3)
                        goto lblOut2;
                }
                if((Size <= ((LqFileSz)0)) ||
                    (Dest->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK)) ||
                   (Source->Flags & (LQFBUF_READ_EOF | LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK)))
                    goto lblOut2;
            }
        }
lblOut2:
        Size2 -= Size;
    }
lblOut:
    _LqFbuf_unlock(Source);
    _LqFbuf_unlock(Dest);
    *IsFound = Fin;
    return Size2;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_printf(LqFbuf* Context, const char* Fmt, ...) {
    va_list arp;
    va_start(arp, Fmt);
    int Res = LqFbuf_vprintf(Context, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_svnprintf(char* Dest, size_t DestSize, const char* Fmt, va_list va) {
    typedef struct ProcData {
        char* Dest, *MaxDest;
        static intptr_t WriteProc(LqFbuf* Context, char* Buf, size_t Size) {
            ProcData* Param = (ProcData*)Context->UserData;
            if((Param->MaxDest - Param->Dest) <= 0) {
                Context->Flags |= LQFBUF_WRITE_ERROR;
                return 0;
            }
            intptr_t TargetSize = lq_min(((intptr_t)Size), Param->MaxDest - Param->Dest);
            memcpy(Param->Dest, Buf, TargetSize);
            Param->Dest += TargetSize;
            return TargetSize;
        }
    } ProcData;
    static LqFbufCookie Cookie = {
        _EmptyReadProc,
        ProcData::WriteProc,
        _EmptySeekProc,
        NULL,
        _EmptyCloseProc
    };
    ProcData Param = {Dest, Dest + DestSize};
    LqFbuf Context;
    LqSbufInit(&Context.OutBuf);
    LqSbufInit(&Context.InBuf);
    Context.Flags = ((LqFbufFlag)0);
    Context.MinFlush = ((intptr_t)0);
    Context.MaxFlush = ((intptr_t)0);
    Context.Cookie = &Cookie;
    Context.UserData = &Param;
    intptr_t Res = _LqFbuf_vprintf(&Context, Fmt, va);
    if((Param.Dest + 1) < Param.MaxDest)
        Param.Dest[0] = '\0';
    LqSbufUninit(&Context.OutBuf);
    LqSbufUninit(&Context.InBuf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_snprintf(char* Dest, size_t DestSize, const char* Fmt, ...) {
    va_list arp;
    va_start(arp, Fmt);
    int Res = LqFbuf_svnprintf(Dest, DestSize, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_putc(LqFbuf* Context, int Val) {
    char Buf = Val;
    return LqFbuf_putsn(Context, &Buf, 1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_puts(LqFbuf* Context, const char* Val) {
    return LqFbuf_putsn(Context, Val, LqStrLen(Val));
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_putsn(LqFbuf* Context, const char* Val, size_t Len) {
    return LqFbuf_write(Context, Val, Len);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_write(LqFbuf* Context, const void* Buf, size_t Size) {
    _LqFbuf_lock(Context);
    intptr_t Res = _LqFbuf_write(Context, Buf, Size);
    _LqFbuf_unlock(Context);
    return Res;
}

static intptr_t _LqFbuf_flush(LqFbuf* Context) {
    LqSbufReadRegion Region;
    LqSbufReadRegionPtr RegionPtr;
    size_t OutBufSize;

    Context->Flags &= ~(LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
    if(Context->Flags & LQFBUF_POINTER) {
        OutBufSize = (Context->BufPtr.StreamBuf->GlobOffset + Context->BufPtr.StreamBuf->Len) - Context->BufPtr.GlobOffset;
        for(bool __r = LqSbufReadRegionPtrFirst(&Context->BufPtr, &RegionPtr, OutBufSize); __r; __r = LqSbufReadRegionPtrNext(&RegionPtr))
            if((RegionPtr.Written = Context->Cookie->WriteProc(Context, (char*)RegionPtr.Source, RegionPtr.SourceLen)) < ((intptr_t)0)) {
                RegionPtr.Written = ((intptr_t)0);
                RegionPtr.Fin = true;
            }
        return RegionPtr.CommonWritten;
    }
    for(bool __r = LqSbufReadRegionFirst(&Context->OutBuf, &Region, Context->OutBuf.Len); __r; __r = LqSbufReadRegionNext(&Region))
        if((Region.Written = Context->Cookie->WriteProc(Context, (char*)Region.Source, Region.SourceLen)) < ((intptr_t)0)) {
            Region.Written = ((intptr_t)0);
            Region.Fin = true;
        }
    return Region.CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_flush(LqFbuf* Context) {
    intptr_t Res;
    _LqFbuf_lock(Context);
    Res = _LqFbuf_flush(Context);
    _LqFbuf_unlock(Context);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_seek(LqFbuf* Context, int64_t Offset, int Flags) {
    intptr_t Res = -((intptr_t)1);
    if(Context->Flags & (LQFBUF_POINTER | LQFBUF_STREAM))
        return Res;
    _LqFbuf_lock(Context);
    _LqFbuf_flush(Context);
    if(Context->OutBuf.Len > ((size_t)0))
        goto lblOut;
    Res = Context->Cookie->SeekProc(Context, Offset, Flags);
    if(Res >= ((intptr_t)0)) {
        Context->Flags &= ~(LQFBUF_READ_EOF | LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK);
        LqSbufRead(&Context->OutBuf, NULL, Context->OutBuf.Len);
        LqSbufRead(&Context->InBuf, NULL, Context->InBuf.Len);
    }
lblOut:
    _LqFbuf_unlock(Context);
    return Res;
}

static intptr_t _LqFbuf_putc(LqFbuf* Context, int Val) {
    char Buf = Val;
    return _LqFbuf_write(Context, &Buf, (size_t)1);
}

static intptr_t _LqFbuf_write(LqFbuf* Context, const void* Buf, intptr_t Size) {
    intptr_t Written1, Written2, Written3, WriteLen;
    if(Context->Flags & LQFBUF_POINTER) {
        Context->Flags |= LQFBUF_WRITE_ERROR;
        return ((intptr_t)0);
    }
    Context->Flags &= ~(LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);
    if(((Context->OutBuf.Len + Size) > Context->MinFlush) || (Context->Flags & LQFBUF_PUT_FLUSH)) {
        WriteLen = Context->OutBuf.Len;
        Written1 = (WriteLen > ((intptr_t)0))? _LqFbuf_flush(Context): WriteLen;
        if(Context->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK))
            return ((intptr_t)0);
        if(Written1 >= WriteLen) {
            Written2 = Context->Cookie->WriteProc(Context, (char*)Buf, Size);
            Written2 = lq_max(Written2, ((intptr_t)0));
            if(Written2 < Size) {
                if((Context->Flags & (LQFBUF_WRITE_ERROR | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK)) && ((Context->OutBuf.Len + Size - Written2) > Context->MaxFlush))
                    return Written2;
                Written3 = LqSbufWrite(&Context->OutBuf, (char*)Buf + Written2, Size - Written2);
                return lq_max(Written3, ((intptr_t)0)) + Written2;
            }
            return Written2;
        } else if((Context->OutBuf.Len + Size) < Context->MaxFlush) {
            return LqSbufWrite(&Context->OutBuf, (char*)Buf, Size);
        }
    } else {
        return LqSbufWrite(&Context->OutBuf, (char*)Buf, Size);
    }
    return ((intptr_t)0);
}

#define lq_putsn(Context, Buf, Size) {\
    if(_LqFbuf_write((Context), (Buf), (Size)) < (Size)){lq_errno_set(EOVERFLOW); goto lblOut;}\
    if((Context)->Flags & LQFBUF_WRITE_ERROR) { goto lblOut;}\
    if((Context)->Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK)) { (Context)->Flags &= ~(LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);\
        (Context)->MinFlush = (Context)->MaxFlush; }\
}

#define lq_putc(Context, Val) {\
    if(_LqFbuf_putc((Context), (Val)) <= 0) {lq_errno_set(EOVERFLOW); goto lblOut;}\
    if((Context)->Flags & LQFBUF_WRITE_ERROR){ goto lblOut;}\
    if((Context)->Flags & (LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK)) { (Context)->Flags &= ~(LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_WOULD_BLOCK);\
        (Context)->MinFlush = (Context)->MaxFlush; }\
}
static const unsigned char CodeChainBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const unsigned char CodeChainBase64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"; //-_-
static const unsigned char CodeChainHexLower[] = "0123456789abcdef";
static const unsigned char CodeChainHexUpper[] = "0123456789ABCDEF";

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_vprintf(LqFbuf* Context, const char* Fmt, va_list va) {
    intptr_t Res;
    _LqFbuf_lock(Context);
    Res = _LqFbuf_vprintf(Context, Fmt, va);
    _LqFbuf_unlock(Context);
    return Res;
}

static intptr_t _LqFbuf_vprintf(LqFbuf* Context, const char* Fmt, va_list va) {
    const char* c = Fmt;
    const char* s;
    char ct;
    uintptr_t Flag, ArgSize;
    intptr_t Written = (intptr_t)0;
    bool IsUpper;
    size_t ArgSize2;
    int exp, CountPrinted = 0, width, width2, Radix;
    LqFbufFlag Tflags = Context->Flags;
    intptr_t MinFlush = Context->MinFlush;
    char buf[64];

    if(Context->Flags & LQFBUF_PRINTF_FLUSH)
        Context->Flags &= ~(LQFBUF_PUT_FLUSH);
    for(; ;) {
        for(s = c; (*c != '\0') && (*c != '%'); c++);
        if(c != s) {
            lq_putsn(Context, s, c - s);
            Written += (c - s);
        }
        if(*c == '\0') goto lblOut2;
        c++;
        Flag = ((uintptr_t)0);
        for( ; ;c++) {
            switch(*c) {
                case '-': Flag |= FL_MINUS; break;
                case '+': Flag |= FL_PLUS; break;
                case ' ': Flag |= FL_SPACE; break;
                case '#': Flag |= FL_SHARP; break;
                case '0': Flag |= FL_ZERO; break;
                default: goto lblFlagOut;
            }
        }
lblFlagOut:
        width2 = width = -1;
        if(*c == '*') {
            c++;
            width = va_arg(va, int);
        } else if(*c >= '0' && *c <= '9') {
            for(width = 0; *c >= '0' && *c <= '9'; c++)
                width = width * 10 + (*c - '0');
        }
        if(*c == '.') {
            c++;
            if(*c == '*') {
                c++;
                width2 = va_arg(va, int);
            } else if(*c >= '0' && *c <= '9') {
                for(width2 = 0; *c >= '0' && *c <= '9'; c++)
                    width2 = width2 * 10 + (*c - '0');
            }
        }
        ArgSize = SZ_INT;
        switch(*c) {
            case 'l':
                c++;
                if(*c == 'l') {
                    c++;
                    ArgSize = SZ_LONGLONG;
                } else {
                    ArgSize = SZ_LONG;
                }
                break;
            case 'h':
                c++;
                if(*c == 'h') {
                    c++;
                    ArgSize = SZ_CHAR;
                } else {
                    ArgSize = SZ_SHORT;
                }
                break;
            case 'j': c++; ArgSize2 = sizeof(intmax_t); goto lblGetSzBit;
            case 'z': c++; ArgSize2 = sizeof(size_t); goto lblGetSzBit;
            case 't': c++; ArgSize2 = sizeof(ptrdiff_t); goto lblGetSzBit;
            case 'L': c++; ArgSize2 = sizeof(int64_t); goto lblGetSzBit;
            case 'r': c++; ArgSize2 = sizeof(intptr_t); goto lblGetSzBit;
            case 'q':
                c++;
                for(ArgSize2 = ((size_t)0); *c >= '0' && *c <= '9'; c++)
                    ArgSize2 = ArgSize2 * ((size_t)10) + (((size_t)*c) - ((size_t)'0'));
                ArgSize2 /= 8;
lblGetSzBit:
                if(ArgSize2 == sizeof(char))
                    ArgSize = SZ_CHAR;
                else if(ArgSize2 == sizeof(short))
                    ArgSize = SZ_SHORT;
                else if(ArgSize2 == sizeof(long))
                    ArgSize = SZ_LONG;
                else if(ArgSize2 == sizeof(int))
                    ArgSize = SZ_INT;
                else if(ArgSize2 == sizeof(long long))
                    ArgSize = SZ_LONGLONG;
                break;
        }
        switch(*c) {
            case 's':
                c++;
                s = va_arg(va, const char*);
                if(width2 == -1) {
                    const char* end = s;
                    for(; *end; end++);
                    width2 = end - s;
                }
                if(width2 < width) {
                    ct = (Flag & FL_ZERO) ? '0' : ' ';
                    for(exp = width - width2; exp > 0; exp--) {
                        lq_putsn(Context, &ct, 1);
                        Written++;
                    }
                }
                lq_putsn(Context, s, (size_t)width2);
                Written += width2;
                CountPrinted++;
                continue;
            case 'c':
                c++;
                lq_putc(Context, va_arg(va, int));
                Written++;
                CountPrinted++;
                continue;
            case 'b':
            case 'B':
            {
                const unsigned char *CodeChain = (*c == 'B') ? CodeChainBase64URL : CodeChainBase64;
                c++;
                const unsigned char *s = (const unsigned char*)va_arg(va, const char*);
                const unsigned char *sm;
                unsigned char *d;
                unsigned char *md = (unsigned char*)buf + sizeof(buf) - 4;
                if(width2 != -1)
                    sm = s + width2;
                else
                    for(sm = s; *sm; sm++);

                do {
                    d = (unsigned char*)buf;
                    while(((sm - s) > 2) && (d < md)) {
                        *d++ = CodeChain[(s[0] >> 2) & 0x3f];
                        *d++ = CodeChain[((s[0] & 3) << 4) | (s[1] >> 4)];
                        *d++ = CodeChain[((s[1] & 0x0f) << 2) | (s[2] >> 6)];
                        *d++ = CodeChain[s[2] & 0x3f];
                        s += 3;
                    }

                    lq_putsn(Context, buf, d - (unsigned char*)buf);
                    Written += (d - (unsigned char*)buf);
                } while((sm - s) > 2);
                d = (unsigned char*)buf;
                if((sm - s) > 0) {
                    *d++ = CodeChain[(s[0] >> 2) & 0x3f];
                    if((sm - s) == 1) {
                        *d++ = CodeChain[(s[0] & 3) << 4];
                        if(Flag & FL_SHARP) *d++ = '=';
                    } else {
                        *d++ = CodeChain[((s[0] & 3) << 4) | (s[1] >> 4)];
                        *d++ = CodeChain[(s[1] & 0x0f) << 2];
                    }
                    if(Flag & FL_SHARP) *d++ = '=';
                    lq_putsn(Context, buf, d - (unsigned char*)buf);
                    Written += (d - (unsigned char*)buf);
                }
                CountPrinted++;
            }
            break;
            case 'v':
            case 'V':
            {
                const unsigned char *CodeChain = (*c == 'v') ? CodeChainHexLower : CodeChainHexUpper;
                c++;
                const unsigned char *s = va_arg(va, const unsigned char*);
                unsigned char *md = (unsigned char*)buf + sizeof(buf);
                const unsigned char *sm;
                unsigned char *d;
                if(width2 != -1)
                    sm = s + width2;
                else
                    for(sm = s; *sm; sm++);
                do {
                    d = (unsigned char*)buf;
                    for(; (s < sm) && (d < md); s++) {
                        *d++ = CodeChain[*s >> 4];
                        *d++ = CodeChain[*s & 0x0f];
                    }
                    lq_putsn(Context, buf, d - (unsigned char*)buf);
                    Written += (d - (unsigned char*)buf);
                } while(s < sm);
                CountPrinted++;
            }
            continue;
            case 'X': c++; IsUpper = true; exp = -1; Radix = 16; goto lblPrintInteger;
            case 'x': c++; IsUpper = false; exp = -1; Radix = 16; goto lblPrintInteger;
            case 'o': c++; exp = -1; Radix = 8; goto lblPrintInteger;
            case 'u': c++; exp = -1; Radix = 10; goto lblPrintInteger;
            case 'd':
            case 'i': c++; exp = Flag & FL_PLUS; Radix = 10;
lblPrintInteger:
            {
                char* start;
                const char* end = buf + sizeof(buf);
                long l;
                long long ll;
                unsigned long ul;
                unsigned long long ull;
                if(exp >= 0) {
                    switch(ArgSize) {
                        case SZ_CHAR:
                        case SZ_SHORT:
                        case SZ_INT: l = va_arg(va, int); goto lblIntToString1;
                        case SZ_LONG: l = va_arg(va, long);
lblIntToString1:
                            _IntegerToString(long, char, exp, IsUpper, l, buf, sizeof(buf), Radix, start);
                            break;
                        case SZ_LONGLONG: ll = va_arg(va, long long);
                            _IntegerToString(long long, char, exp, IsUpper, ll, buf, sizeof(buf), Radix, start);
                            break;
                    }
                } else {
                    switch(ArgSize) {
                        case SZ_CHAR:
                        case SZ_SHORT:
                        case SZ_INT: ul = va_arg(va, unsigned int); goto lblIntToString3;
                        case SZ_LONG: ul = va_arg(va, unsigned long);
lblIntToString3:
                            _IntegerToString(long, char, exp, IsUpper, ul, buf, sizeof(buf), Radix, start);
                            break;
                        case SZ_LONGLONG: ull = va_arg(va, unsigned long long);
                            _IntegerToString(long long, char, exp, IsUpper, ull, buf, sizeof(buf), Radix, start);
                            break;
                    }
                }

                if((width == -1) || ((end - start) >= width)) {
                    lq_putsn(Context, start, end - start);
                    Written += (end - start);
                } else {
                    char* j = start;
                    size_t z = width - (end - start);
                    if(!(Flag & FL_ZERO)) {
                        ct = ' ';
                        for(; z > 0; z--) {
                            lq_putsn(Context, &ct, 1);
                            Written++;
                        }
                    }
                    if(*j == '+' || *j == '-') {
                        lq_putsn(Context, j++, 1);
                    }
                    if(Flag & FL_ZERO) {
                        ct = '0';
                        for(; z > 0; z--) {
                            lq_putsn(Context, &ct, 1);
                            Written++;
                        }
                    }
                    lq_putsn(Context, j, end - j);
                    Written += (end - j);
                }
                CountPrinted++;
            }
            continue;
            case 'A': c++; IsUpper = true; exp = 0; Radix = 16; goto lblPrintDouble;
            case 'a': c++; IsUpper = false; exp = 0; Radix = 16; goto lblPrintDouble;
            case 'E': c++; IsUpper = true; exp = 1; Radix = 10; goto lblPrintDouble;
            case 'e': c++; IsUpper = false; exp = 1; Radix = 10; goto lblPrintDouble;
            case 'G': c++; IsUpper = true; exp = 0; Radix = 10; goto lblPrintDouble;
            case 'g': c++; IsUpper = false; exp = 0; Radix = 10; goto lblPrintDouble;
            case 'F': c++; IsUpper = true; exp = -1; Radix = 10; goto lblPrintDouble;
            case 'f': c++; IsUpper = false; exp = -1; Radix = 10;
lblPrintDouble:
            {
                long double f = va_arg(va, long double);
                char* end = _DoubleToString(
                    f,
                    buf,
                    Radix,
                    0.00000000000000001,
                    _DTOS_SCALE_EPS |
                    ((IsUpper) ? _DTOS_UPPER_CASE : 0) |
                    ((Flag & FL_PLUS) ? _DTOS_PRINT_SIGN : 0) |
                    ((exp == 0) ? _DTOS_PRINT_EXP_AUTO : ((exp == 1) ? _DTOS_PRINT_EXP : 0)),
                    (Context->Flags & LQFBUF_SEP_COMMA) ? ',' : '.',
                    width2
                );
                end--;
                if((width == -1) || ((end - buf) >= width)) {
                    lq_putsn(Context, buf, end - buf);
                    Written += (end - buf);
                } else {
                    char* j = buf;
                    size_t z = width - (end - buf);
                    if(!(Flag & FL_ZERO)) {
                        ct = ' ';
                        for(; z > 0; z--) {
                            lq_putsn(Context, &ct, 1);
                            Written++;
                        }
                    }
                    if(*j == '+' || *j == '-') {
                        lq_putsn(Context, j++, 1);
                    }
                    if(Flag & FL_ZERO) {
                        ct = '0';
                        for(; z > 0; z--) {
                            lq_putsn(Context, &ct, 1);
                            Written++;
                        }
                    }
                    lq_putsn(Context, j, end - j);
                    Written += (end - j);
                }
                CountPrinted++;
            }
            continue;
            case 'n':
                c++;
                *(va_arg(va, int*)) = Written;
                continue;
            case '%':
                c++;
                ct = '%';
                lq_putsn(Context, &ct, 1);
                continue;
        }
    }
lblOut2:
    Context->Flags = Tflags;
    Context->MinFlush = MinFlush;
    if(Context->Flags & LQFBUF_PRINTF_FLUSH)
        _LqFbuf_flush(Context);
lblOut:
    Context->MinFlush = MinFlush;
    Context->Flags = Tflags;
    return Written;
}

static inline void LqFbuf_SaveState(LqFbuf_state* Dest, LqFbuf_state* Source) {
    Dest->Buf = Source->Buf;
    if(Dest->IsString = Source->IsString)
        Dest->StringPos = Source->StringPos;
    else
        LqSbufPtrCopy(&Dest->BufPtr, &Source->BufPtr);
}

static inline void LqFbuf_RestoreState(LqFbuf_state* Dest, LqFbuf_state* Source) {
    if(Source->IsString)
        Dest->StringPos = Source->StringPos;
    else
        LqSbufPtrCopy(&Dest->BufPtr, &Source->BufPtr);
}

#define ReadPortionBegin(ProcName, Args, ...) \
static intptr_t ProcName(LqFbuf_state* State, char* Dst, intptr_t DstLen, ##__VA_ARGS__ ){\
    bool Fin = false, Eof, SavedEof, WithoutCopy = Dst == NULL, __r, t = true; \
    char* Dest = Dst, *MaxDest = (DstLen == -((intptr_t)1))?((char*)INTPTR_MAX): (Dst + DstLen), *Source, *SavedSource; Args;\
    LqSbufPtr SavedPtr;\
    LqSbufReadRegionPtr Region, _SavedReg;\
    LqSbufWriteRegion RegionW;\
    LqFbuf_state __MainState;\
    size_t Res = 0, SavedRes;\
    LqFbuf_SaveState(&__MainState, State);\
    if(State->IsString){\
        Eof = __r = true;\
        State->Buf->Flags |= LQFBUF_READ_EOF;\
        Region.SourceLen = lq_min((char*)State->Buf->UserData - (char*)State->StringPos, MaxDest - Dest);\
        Region.CommonWritten = 0;\
        Region.Source = (char*)State->StringPos + Region.CommonWritten;\
    } else {\
__lblContinueRead:\
        __r = LqSbufReadRegionPtrFirst(&State->BufPtr, &Region, MaxDest - Dest);\
        Eof = (State->Buf->Flags & LQFBUF_READ_EOF) && LqSbufReadRegionPtrIsEos(&Region);\
        if(Eof && !__r) __r = true, t = false, Region.SourceLen = 0, Region.Source = "";\
    }\
    while(__r){\
        Source = (char*)Region.Source;\
        {

#define ReadPortionEnd(WhenOutExec) }\
        Region.Written = Source - (char*)Region.Source;\
        if(State->IsString){\
            Region.CommonWritten += Region.Written;\
            State->StringPos = (char*)State->StringPos + Region.CommonWritten;\
            __r = false;\
        } else if (t) {\
            Region.Fin = Fin;\
            __r = LqSbufReadRegionPtrNext(&Region); \
            Eof = (State->Buf->Flags & LQFBUF_READ_EOF) && LqSbufReadRegionPtrIsEos(&Region);\
        }else { break; }\
    }\
    Res += Region.CommonWritten;\
    if(!Fin) {\
        if(State->Buf->Flags & (LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF)) {\
            goto __lblNotMatch2;\
        }else{\
            if(State->Buf->Flags & LQFBUF_POINTER){\
                 State->Buf->Flags |= LQFBUF_READ_EOF;\
            } else {\
                for(bool __r = LqSbufWriteRegionFirst(&State->Buf->InBuf, &RegionW, State->Buf->PortionSize); __r; __r = LqSbufWriteRegionNext(&RegionW)) {\
                        if((RegionW.Readed = State->Buf->Cookie->ReadProc(State->Buf, (char*)RegionW.Dest, RegionW.DestLen)) < 0) {\
                            RegionW.Readed = 0; \
                            RegionW.Fin = true; \
                        }}\
				if(RegionW.CommonReaded <= ((intptr_t)0)) State->Buf->Flags |= LQFBUF_READ_WOULD_BLOCK; \
            }\
            goto __lblContinueRead;\
        }\
    }\
    WhenOutExec; \
    if((!WithoutCopy) && ((DstLen == -((intptr_t)1)) || ((Dest - Dst) < DstLen))) \
        *Dest = '\0';\
    return Res;\
lblNotMatch:\
    if(!State->IsString){\
        Region.Fin = true;\
        Region.Written = 0;\
        __r = LqSbufReadRegionPtrNext(&Region);\
    }\
__lblNotMatch2:\
    LqFbuf_RestoreState(State ,&__MainState);\
    return -((intptr_t)1);\
}

#define PortionSave() {\
    SavedSource = Source;\
    SavedRes = Res;\
    SavedEof = Eof;\
    LqSbufPtrCopy(&SavedPtr, &State->BufPtr);\
    _SavedReg = Region;\
  }
    
#define PortionRestore() {\
    Source = SavedSource;\
    Res = SavedRes;\
    Eof = SavedEof;\
    LqSbufPtrCopy(&State->BufPtr, &SavedPtr);\
    Region = _SavedReg;\
  }

ReadPortionBegin(LqFbuf_ReadWhile, ((void)0), const char* ControlSeq, size_t ControlSeqSize)
    register char *Control = (char*)ControlSeq,
    *MaxControl = Control + ControlSeqSize,
    *MaxSource = Source + lq_min(Region.SourceLen, MaxDest - Dest);

    for(; Source < MaxSource; Source++, Dest++) {
        register char CurChar = *Source;
        register char* ControlInterator = Control;
        for(; ControlInterator < MaxControl; ControlInterator++)
            if((*ControlInterator == '-') && (ControlInterator > Control) && (ControlInterator < (MaxControl - 1))) {
                if((*(ControlInterator - 1) < CurChar) && (*(ControlInterator + 1) > CurChar))
                    break;
            } else if(*ControlInterator == CurChar) {
                break;
            }
            if(ControlInterator >= MaxControl) {
                Fin = true;
                break;
            }
            if(!WithoutCopy)
                *Dest = CurChar;
    }
    Fin |= ((Dest >= MaxDest) || Eof);
ReadPortionEnd(((void)0))

ReadPortionBegin(LqFbuf_ReadWhileSame, char *MaxControl = (char*)ControlSeq + ControlSeqSize; , const char* ControlSeq, size_t ControlSeqSize, const bool IsCase)
    char *Control = (char*)ControlSeq,
    *MaxSource = Source + lq_min(lq_min(Region.SourceLen, MaxDest - Dest), MaxControl - Control);
    if(WithoutCopy) {
        if(IsCase) {
            for(; (Source < MaxSource) && (TO_LOWER(*Source) == TO_LOWER(*Control)); Source++, Control++, Dest++);
        } else {
            for(; (Source < MaxSource) && (*Source == *Control); Source++, Control++, Dest++);
        }
    } else {
        if(IsCase) {
            for(; (Source < MaxSource) && (TO_LOWER(*Source) == TO_LOWER(*Control)); Source++, Control++, Dest++)
                *Dest = *Source;
        } else {
            for(; (Source < MaxSource) && (*Source == *Control); Source++, Control++, Dest++)
                *Dest = *Source;
        }
    }
    Fin |= ((Dest >= MaxDest) || (Control >= MaxControl));
    if(!Fin && ((Source < MaxSource) && ((IsCase)? (TO_LOWER(*Source) == TO_LOWER(*Control)): (*Source != *Control)) || Eof))
        goto lblNotMatch;
    ControlSeq = Control;
ReadPortionEnd(((void)0))


 ///////////////////////////////
ReadPortionBegin(LqFbuf_ReadWhileNotSame, char* LastSeq = (char*)ControlSeq; char *MaxControl = (char*)ControlSeq + ControlSeqSize; , const char* ControlSeq, size_t ControlSeqSize, const bool IsCase)
    char *MaxSource, *ms, *r, *k;
    register char *s, *c;
    MaxSource = (char*)Region.Source + lq_min(Region.SourceLen, MaxDest - Dest);
    ms = MaxSource - ControlSeqSize + 1;

    if(WithoutCopy) {
        if(LastSeq > ControlSeq) {
            for(s = Source, c = LastSeq; ; s++, c++) {
				if(c >= MaxControl) {/* ƒостигнут конец контрольной последовательности*/
                    Fin |= true;
                    PortionRestore();
                    goto lblOut;
                }
                if(s >= MaxSource) {/* ƒостигнут конец региона*/
                    Source = s;
                    if(Eof) {
                        for(s = (char*)ControlSeq; (Dest < MaxDest) && (s < c); Dest++, s++);
                        Fin |= true;
                        goto lblOut2;
                    }
                    LastSeq = c;
                    goto lblOut;
                }
                if((IsCase) ? (TO_LOWER(*c) != TO_LOWER(*s)) : (*c != *s)) {
                    PortionRestore();
                    MaxSource = (char*)Region.Source + lq_min(Region.SourceLen, MaxDest - Dest);
                    ms = MaxSource - ControlSeqSize + 1;
                    LastSeq = (char*)ControlSeq;
                    if(Dest < MaxDest)
                        Dest++;
                    Source++;
                    goto lblContinue8;
                }
            }
        }
        if(IsCase) {
            for(; Source < ms; Source++, Dest++) {
                for(s = Source, c = (char*)ControlSeq; ; s++, c++) {
                    if(c >= MaxControl) {
                        Fin |= true; goto lblOut;
                    }
                    if(TO_LOWER(*c) != TO_LOWER(*s))
                        break;
                }
            }
        } else {
            for(; Source < ms; Source++, Dest++) {
                for(s = Source, c = (char*)ControlSeq; ; s++, c++) {
                    if(c >= MaxControl) {
                        Fin |= true; goto lblOut;
                    }
                    if(*c != *s)
                        break;
                }
            }
        }
        if(!State->IsString && !Eof) {
lblContinue8:
            for(; Source < MaxSource; Source++) {
                if(IsCase) {
                    for(s = Source, c = (char*)ControlSeq; s < MaxSource; s++, c++) {
                        if(TO_LOWER(*c) != TO_LOWER(*s)) goto lblContinue9;
                    }
                } else {
                    for(s = Source, c = (char*)ControlSeq; s < MaxSource; s++, c++) {
                        if(*c != *s) goto lblContinue9;
                    }
                }
                PortionSave();
                Source = MaxSource;
                LastSeq = c;
                goto lblOut;
lblContinue9:
                Dest++;
            }
            LastSeq = (char*)ControlSeq;
        }
    } else {
        if(LastSeq > ControlSeq) {
            for(s = Source, c = LastSeq; ; s++, c++) {
				if(c >= MaxControl) {/* ƒостигнут конец контрольной последовательности*/
                    Fin |= true;
                    PortionRestore();
                    goto lblOut;
                }
                if(s >= MaxSource) {/* ƒостигнут конец региона*/
                    Source = s;
                    if(Eof) {
                        for(s = (char*)ControlSeq; (Dest < MaxDest) && (s < c); Dest++, s++)
                            *(Dest++) = *s;
                        Fin |= true;
                        goto lblOut2;
                    }
                    LastSeq = c;
                    goto lblOut;
                }
                if((IsCase)? (TO_LOWER(*c) != TO_LOWER(*s)): (*c != *s)) {
                    PortionRestore();
                    MaxSource = (char*)Region.Source + lq_min(Region.SourceLen, MaxDest - Dest);
                    ms = MaxSource - ControlSeqSize + 1;
                    LastSeq = (char*)ControlSeq;
                    if(Dest < MaxDest)
                        *(Dest++) = *Source;
                    Source++;
                    goto lblContinue6;
                }
            }
        }
        if(IsCase) {
            for(; Source < ms; Source++, Dest++) {
                for(s = Source, c = (char*)ControlSeq; ; s++, c++) {
                    if(c >= MaxControl) {
                        Fin |= true; goto lblOut;
                    }
                    if(TO_LOWER(*c) != TO_LOWER(*s))
                        break;
                }
                *Dest = *Source;
            }
        } else {
            for(; Source < ms; Source++, Dest++) {
                for(s = Source, c = (char*)ControlSeq; ; s++, c++) {
                    if(c >= MaxControl) {
                        Fin |= true; goto lblOut;
                    }
                    if(*c != *s)
                        break;
                }
                *Dest = *Source;
            }
        }

        if(!State->IsString && !Eof) {
lblContinue6:
            for(; Source < MaxSource; Source++) {
                if(IsCase) {
                    for(s = Source, c = (char*)ControlSeq; s < MaxSource; s++, c++) {
                        if(TO_LOWER(*c) != TO_LOWER(*s)) goto lblContinue5;
                    }
                } else {
                    for(s = Source, c = (char*)ControlSeq; s < MaxSource; s++, c++) {
                        if(*c != *s) goto lblContinue5;
                    }
                }

                PortionSave();
                Source = MaxSource;
                LastSeq = c;
                goto lblOut;
lblContinue5:
                *Dest = *Source;
                Dest++;
            }
            LastSeq = (char*)ControlSeq;
        }
    }
lblOut:
    Fin |= (Dest >= MaxDest);
    if(Eof && !Fin && (Source >= ms)) {
        Fin |= true;
        for(; Source < MaxSource; Source++, Dest++)
            if(!WithoutCopy)
                *Dest = *Source;
    }
lblOut2:;
ReadPortionEnd(((void)0))


ReadPortionBegin(LqFbuf_ReadTo, ((void)0), const char* ControlSeq, size_t ControlSeqSize)
    char *Control = (char*)ControlSeq,
    *MaxControl = Control + ControlSeqSize,
    *MaxSource = Source + lq_min(Region.SourceLen, MaxDest - Dest);

    for(; Source < MaxSource; Source++, Dest++) {
        register char CurChar = *Source;
        register char* ControlInterator = Control;
        for(; ControlInterator < MaxControl; ControlInterator++)
            if((*ControlInterator == '-') && (ControlInterator > Control) && (ControlInterator < (MaxControl - 1))) {
                if((*(ControlInterator - 1) < CurChar) && (*(ControlInterator + 1) > CurChar))
                    break;
            } else if(*ControlInterator == CurChar) {
                break;
            }
            if(ControlInterator < MaxControl) { Fin = true; break; }
            if(!WithoutCopy)
                *Dest = CurChar;
    }
    Fin |= ((Dest >= MaxDest) || Eof);
ReadPortionEnd(((void)0))

ReadPortionBegin(LqFbuf_ReadChar, ((void)0), void*)
    *Dest = *Source;
    Source++;
    Dest++;
    Fin = true;
ReadPortionEnd(((void)0))

ReadPortionBegin(LqFbuf_ReadInt, bool Signed2 = !Signed; int HaveSign = 0, unsigned Radx, bool Signed)
    char *MaxSource = Source + lq_min(Region.SourceLen, MaxDest - Dest);

    if(Source >= MaxSource)
    return 0;

    if(!Signed2) {
        Signed2 = true;
        if((*Source == '-') || (*Source == '+')) {
            if(!WithoutCopy)
                *Dest = *Source;
            HaveSign = 1;
            Dest++; Source++;
        }
    }
    if(Radx <= 10) {
        for(; Source < MaxSource; Source++, Dest++) {
            register unsigned Digit = *Source - '0';
            if(Digit >= Radx) {
                Fin = true;
                break;
            }
            if(!WithoutCopy)
                *Dest = *Source;
        }
    } else {
        register unsigned Radx2 = Radx - 10;
        for(; Source < MaxSource; Source++, Dest++) {
            register unsigned Digit = *Source - '0';
            if(Digit > 9) {
                Digit = *Source - 'a';
                if(Digit >= Radx2)
                    Digit = *Source - 'A';
                if(Digit >= Radx2) {
                    Fin = true;
                    break;
                }
            }
            if(!WithoutCopy)
                *Dest = *Source;
        }
    }
    if((Dest - Dst) > 30)
        goto lblNotMatch;
    Fin |= (Dest >= MaxDest) || (Eof && ((Dest - Dst) > HaveSign));
ReadPortionEnd(((void)0))

static const unsigned char _DecodeChain[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

static const unsigned char _DecodeSeqURL[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};


ReadPortionBegin(LqFbuf_ReadBase64, unsigned char *Chain = ((IsUrl) ? (unsigned char*)_DecodeSeqURL : (unsigned char*)_DecodeChain),/*Args*/bool IsUrl, bool IsReadEq, int* Written)
    char
    *MaxSource = Source + Region.SourceLen,
    *Dest1 = Dest;

    if(WithoutCopy) {
        while(1) {
            if(((MaxSource - Source) < 4) || ((MaxDest - Dest1) < 3) ||
                (Chain[Source[0]] == 77) || (Chain[Source[1]] == 77) ||
               (Chain[Source[2]] == 77) || (Chain[Source[3]] == 77))
                break;
            Source += 4;
            Dest1 += 3;
        }
    } else {
        while(1) {
            if(((MaxSource - Source) < 4) || ((MaxDest - Dest1) < 3) ||
                (Chain[Source[0]] == 77) || (Chain[Source[1]] == 77) ||
               (Chain[Source[2]] == 77) || (Chain[Source[3]] == 77))
                break;
            *Dest1++ = (unsigned char)(Chain[Source[0]] << 2 | Chain[Source[1]] >> 4);
            *Dest1++ = (unsigned char)(Chain[Source[1]] << 4 | Chain[Source[2]] >> 2);
            *Dest1++ = (unsigned char)(Chain[Source[2]] << 6 | Chain[Source[3]]);
            Source += 4;
        }
    }
    char* Dest2 = Dest1, *Source2 = Source;
    if((MaxSource - Source) > 0) {
        if((Chain[Source[0]] == 77) || ((MaxDest - Dest1) < 1)) {
            Fin |= true; goto lblOut;
        }
    }

    if((MaxSource - Source) > 1) {
        if(Chain[Source[1]] == 77) {
            Fin |= true; goto lblOut;
        } else {
            if(WithoutCopy)
                Dest2++;
            else
                *Dest2++ = (unsigned char)(Chain[Source[0]] << 2 | Chain[Source[1]] >> 4);
            Source2 = Source + 2;
        }
    }
    if((MaxSource - Source) > 2) {
        if((Chain[Source[2]] == 77) || ((MaxDest - Dest2) < 1)) {
            Fin |= true; goto lblOut;
        } else {
            if(WithoutCopy)
                Dest2++;
            else
                *Dest2++ = (unsigned char)(Chain[Source[1]] << 4 | Chain[Source[2]] >> 2);
            Source2 = Source + 3;
        }
    }
    lblOut:
    Fin |= (Dest1 >= MaxDest) || ((Source2 < MaxSource) && (Chain[*Source2] == 77)) || Eof;
    if(Fin) {
        if((Source2 < MaxSource) && (*Source2 == '='))
            Source2++;
        Dest = Dest2;
        Source = Source2;
    } else {
        Dest = Dest1;
    }
ReadPortionEnd(*Written = (Dest - Dst))


ReadPortionBegin(LqFbuf_ReadHex, ((void)0), int* Written)
    char *MaxSource = Source + Region.SourceLen;

    if(WithoutCopy) {
        while(1) {
            if(((MaxSource - Source) < 2) || (MaxDest < Dest))
                break;
            unsigned char Digit1 = *Source - '0';
            if(Digit1 > 9) {
                Digit1 = *Source - ('a' - 10);
                if(Digit1 >= 16) Digit1 = *Source - ('A' - 10);
                if(Digit1 >= 16) { Fin = true; break; }
            }
            unsigned char Digit2 = Source[1] - '0';
            if(Digit2 > 9) {
                Digit2 = Source[1] - ('a' - 10);
                if(Digit2 >= 16) Digit2 = Source[1] - ('A' - 10);
                if(Digit2 >= 16) { Fin = true; break; }
            }
            Source += 2;
            Dest++;
        }
    } else {
        while(1) {
            if(((MaxSource - Source) < 2) || (MaxDest < Dest))
                break;
            unsigned char Digit1 = *Source - '0';
            if(Digit1 > 9) {
                Digit1 = *Source - ('a' - 10);
                if(Digit1 >= 16) Digit1 = *Source - ('A' - 10);
                if(Digit1 >= 16) { Fin = true; break; }
            }
            unsigned char Digit2 = Source[1] - '0';
            if(Digit2 > 9) {
                Digit2 = Source[1] - ('a' - 10);
                if(Digit2 >= 16) Digit2 = Source[1] - ('A' - 10);
                if(Digit2 >= 16) { Fin = true; break; }
            }
            Source += 2;
            *(Dest++) = Digit1 << 4 | Digit2;
        }
    }
    Fin |= ((Dest >= MaxDest) || Eof);
ReadPortionEnd(*Written = (Dest - Dst))


static intptr_t LqFbuf_peek_double(
    LqFbuf_state* State,
    long double* Dest,
    unsigned Radx,
    bool InfInd
) {
    LqFbuf_state MainState, ExpState;
    char Buf[128];
    char* c, *m;
    const char* cp;
    char Sep;
    int r;

    LqFbuf_SaveState(&MainState, State);
    c = Buf;
    m = c + sizeof(Buf);
    r = LqFbuf_ReadInt(State, c, m - c, Radx, true);
    if(r <= 0)
        goto lblExit;
    c += r;
    Sep = (State->Buf->Flags & LQFBUF_SEP_COMMA) ? ',' : '.';
    r = LqFbuf_ReadWhileSame(State, c, m - c, &Sep, 1, false);
    if(r <= 0)
        goto lblProcessDouble;
    c += r;
    r = LqFbuf_ReadInt(State, c, m - c, Radx, false);
    if(r > 0) c += r;

    LqFbuf_SaveState(&ExpState, State);
    r = LqFbuf_ReadWhileSame(State, c, m - c, (Radx <= 10) ? "e" : "p", 1, true);
    if(r > 0) {
        r = LqFbuf_ReadInt(State, c + 1, m - c, Radx, true);
        if(r <= 0)
            LqFbuf_RestoreState(State, &ExpState);
        else
            c += (r + 1);
    }
lblProcessDouble:
    *c = '\0';
    cp = _StringToDouble(Buf, Dest, Radx, InfInd, Sep);
    if(cp == NULL)
        goto lblExit;
    if(cp != c) {
        LqFbuf_RestoreState(&MainState, State);
        LqFbuf_ReadTo(State, "\0", ((intptr_t)1), Buf, cp - Buf);
        c = (char*)cp;
    }
    return c - Buf;
lblExit:
    LqFbuf_RestoreState(&MainState, State);
    return -((intptr_t)1);
}

static intptr_t LqFbuf_peek_int(LqFbuf_state* State, void* Dest, unsigned Radx, bool Signed) {
    LqFbuf_state MainState;
    char Buf[128];
    const char* EndPointer;
    intptr_t Readed;

    LqFbuf_SaveState(&MainState, State);
    Readed = LqFbuf_ReadInt(State, Buf, sizeof(Buf), Radx, Signed);
    if(Readed <= ((intptr_t)0))
        return -((intptr_t)1);
    Buf[Readed] = '\0';
    EndPointer = Buf;

    if(!_StringToInteger(&EndPointer, Dest, Radx, Signed)) {
        LqFbuf_RestoreState(State, &MainState);
        return -((intptr_t)1);
    }
    if(EndPointer != (Buf + Readed)) {
        LqFbuf_RestoreState(State, &MainState);
        LqFbuf_ReadTo(State, "\0", ((intptr_t)1), Buf, EndPointer - Buf);
    }
    return EndPointer - Buf;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_vssncanf(const char* Source, size_t LenSource, const char* Fmt, va_list va) {
    LqFbuf_state MainState;
    LqFbuf Context;
    Context.Cookie = &_EmptyCookie;
    Context.Flags = ((LqFbufFlag)0);
    Context.PortionSize = ((intptr_t)1);
    Context.UserData = (char*)Source + LenSource;
    MainState.StringPos = (void*)Source;
    MainState.Buf = &Context;
    MainState.IsString = true;
    return _LqFbuf_vscanf(&MainState, LQFBUF_SCANF_PEEK, Fmt, va);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_snscanf(const char* Source, size_t LenSource, const char* Fmt, ...) {
    va_list arp;
    intptr_t Res;
    va_start(arp, Fmt);
    Res = LqFbuf_vssncanf(Source, LenSource, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_vscanf(LqFbuf* Context, int Flags, const char* Fmt, va_list va) {
    LqFbuf_state MainState;
    intptr_t Res;

    _LqFbuf_lock(Context);
    if(Context->Flags & LQFBUF_POINTER) {
        LqSbufPtrCopy(&MainState.BufPtr, &Context->BufPtr);
    } else {
        LqSbufPtrSet(&MainState.BufPtr, &Context->InBuf);
    }
    MainState.Buf = Context;
    MainState.IsString = false;
    Res = _LqFbuf_vscanf(&MainState, Flags, Fmt, va);
    if((Context->Flags & LQFBUF_POINTER) && !(LQFBUF_SCANF_PEEK & Context->Flags)) {
        LqSbufPtrCopy(&Context->BufPtr, &MainState.BufPtr);
        if(((Context->BufPtr.StreamBuf->GlobOffset + Context->BufPtr.StreamBuf->Len) - Context->BufPtr.GlobOffset) <= 0)
            Context->Flags |= LQFBUF_READ_EOF;
    }
    _LqFbuf_unlock(Context);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_scanf(LqFbuf* Context, int Flags, const char* Fmt, ...) {
    va_list arp;
    intptr_t Res;
    va_start(arp, Fmt);
    Res = LqFbuf_vscanf(Context, Flags, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_getc(LqFbuf* Context, int* Dest) {
    char Val;
    intptr_t Res;
    Res = LqFbuf_read(Context, &Val, 1);
    *Dest = Val;
    return Res;
}

static intptr_t _LqFbuf_read(LqFbuf* Context, void* Buf, size_t Len) {
    LqSbufReadRegion ReginR;
    LqSbufWriteRegion RegionW;
    LqSbufReadRegionPtr RegionPtr;
    intptr_t Res = 0, Len2;
    bool WithoutCopy = Buf == NULL;
    char *Dest = (char*)Buf, *MaxDest = (char*)Buf + Len;
    char TempBuf[1000];
    Context->Flags &= ~(LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF);
    if(Context->Flags & LQFBUF_POINTER) {
        for(bool __r = LqSbufReadRegionPtrFirst(&Context->BufPtr, &RegionPtr, MaxDest - Dest); __r; __r = LqSbufReadRegionPtrNext(&RegionPtr)) {
            Len2 = lq_min(RegionPtr.SourceLen, MaxDest - Dest);
            if(!WithoutCopy)
                memcpy(Dest, (char*)RegionPtr.Source, Len2);
            Dest += Len2;
            RegionPtr.Fin |= (Dest >= MaxDest);
            RegionPtr.Written = Len2;
        }
        Res = (Context->BufPtr.StreamBuf->GlobOffset + Context->BufPtr.StreamBuf->Len) - Context->BufPtr.GlobOffset;
        if(Res <= ((intptr_t)0))
            Context->Flags |= LQFBUF_READ_EOF;
        return RegionPtr.CommonWritten;
    }
    do {
        for(bool __r = LqSbufReadRegionFirst(&Context->InBuf, &ReginR, MaxDest - Dest); __r; __r = LqSbufReadRegionNext(&ReginR)) {
            Len2 = lq_min(ReginR.SourceLen, MaxDest - Dest);
            if(!WithoutCopy)
                memcpy(Dest, (char*)ReginR.Source, Len2);
            Dest += Len2;
            ReginR.Fin |= (Dest >= MaxDest);
            ReginR.Written = Len2;
        }
        Res += ReginR.CommonWritten;
        if(ReginR.Fin)
            break;
        if(WithoutCopy) {
            while((Dest < MaxDest) && !(Context->Flags & (LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF))) {
                Len2 = Context->Cookie->ReadProc(Context, TempBuf, lq_min(MaxDest - Dest, sizeof(TempBuf)));
                Dest += lq_max(Len2, ((intptr_t)0));
            }
        } else {
            Len2 = Context->Cookie->ReadProc(Context, Dest, MaxDest - Dest);
            Res += lq_max(Len2, ((intptr_t)0));
        }
    } while(false);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_read(LqFbuf* Context, void* Buf, size_t Len) {
    _LqFbuf_lock(Context);
    intptr_t ResLen = _LqFbuf_read(Context, Buf, Len);
    _LqFbuf_unlock(Context);
    return ResLen;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_peek(LqFbuf* Context, void* Buf, size_t Len) {
    LqSbufWriteRegion RegionW;
    LqSbufReadRegionPtr  RegionPtrR;
    intptr_t Res, Len2;
    char *Dest, *MaxDest;
    LqFbuf_state MainState;
    bool WithoutCopy = Buf == NULL; //Used for load file in memory(virtual file)

    Res = ((intptr_t)0);
    Dest = (char*)Buf;
    MaxDest = (char*)Buf + Len;
    _LqFbuf_lock(Context);
    Context->Flags &= ~(LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF);
    if(Context->Flags & LQFBUF_POINTER) {
        LqSbufPtrCopy(&MainState.BufPtr, &Context->BufPtr);
        for(bool __r = LqSbufReadRegionPtrFirst(&MainState.BufPtr, &RegionPtrR, MaxDest - Dest); __r; __r = LqSbufReadRegionPtrNext(&RegionPtrR)) {
            Len2 = lq_min(RegionPtrR.SourceLen, MaxDest - Dest);
            if(!WithoutCopy)
                memcpy(Dest, (char*)RegionPtrR.Source, Len2);
            Dest += Len2;
            RegionPtrR.Fin |= (Dest >= MaxDest);
            RegionPtrR.Written = Len2;
        }
        Res = (MainState.BufPtr.StreamBuf->GlobOffset + MainState.BufPtr.StreamBuf->Len) - MainState.BufPtr.GlobOffset;
        if(Res <= ((intptr_t)0))
            Context->Flags |= LQFBUF_READ_EOF;
        _LqFbuf_unlock(Context);
        return RegionPtrR.CommonWritten;
    }
    LqSbufPtrSet(&MainState.BufPtr, &Context->InBuf);
    MainState.Buf = Context;
    while(true) {
        for(bool __r = LqSbufReadRegionPtrFirst(&MainState.BufPtr, &RegionPtrR, MaxDest - Dest); __r; __r = LqSbufReadRegionPtrNext(&RegionPtrR)) {
            Len2 = lq_min(RegionPtrR.SourceLen, MaxDest - Dest);
            if(!WithoutCopy)
                memcpy(Dest, (char*)RegionPtrR.Source, Len2);
            Dest += Len2;
            RegionPtrR.Fin |= (Dest >= MaxDest);
            RegionPtrR.Written = Len2;
        }
        Res += RegionPtrR.CommonWritten;
        if(RegionPtrR.Fin) break;
        if(Context->Flags & (LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF)) {
            _LqFbuf_unlock(Context);
            if(Res == ((intptr_t)0))
                return -((intptr_t)1);
            return Res;
        }
        for(bool __r = LqSbufWriteRegionFirst(&Context->InBuf, &RegionW, Context->PortionSize); __r; __r = LqSbufWriteRegionNext(&RegionW))
            if((RegionW.Readed = Context->Cookie->ReadProc(Context, (char*)RegionW.Dest, RegionW.DestLen)) < 0) {
                RegionW.Readed = ((intptr_t)0);
                RegionW.Fin = true;
            }
    }
    _LqFbuf_unlock(Context);
    return Res;
}

static intptr_t _LqFbuf_vscanf(LqFbuf_state* State, int FunFlags, const char* Fmt, va_list va) {
    const char* c = Fmt;
    const char* s;
    uintptr_t Flags;
    intptr_t CountScanned, TempReaded, Readed;
    uintptr_t ArgSize;
    size_t ArgSize2;
    int Exp, Width, Width2, Radix;
    LqFbufFlag Tflags;
    char buf[64];
    long long ll;
    unsigned long long ull;
    long double f;
    const char* SeqEnd;
    const char* SeqStart;
    char* Dest;
    int* Written;
    int t;

    Readed = ((intptr_t)0);
    CountScanned = ((intptr_t)0);
    Tflags = State->Buf->Flags;

    if(FunFlags & LQFBUF_SCANF_PEEK_WHEN_ERR)
        FunFlags |= LQFBUF_SCANF_PEEK;
    State->Buf->Flags &= ~(LQFBUF_READ_ERROR | LQFBUF_READ_WOULD_BLOCK | LQFBUF_WRITE_WOULD_BLOCK | LQFBUF_READ_EOF);
    for(; ;) {
        for(s = c; (*c != '\0') && (*c != '%'); c++);
        if(c != s) {
            TempReaded = LqFbuf_ReadWhileSame(State, NULL, 0xffff, s, (c - s), false);
            if(TempReaded < (c - s))
                goto lblOut;
            Readed += TempReaded;
        }
        if(*c == '\0') {
            if(FunFlags & LQFBUF_SCANF_PEEK_WHEN_ERR)
                FunFlags &= ~LQFBUF_SCANF_PEEK;
            goto lblOut;
        }
        c++;
        Flags = ((uintptr_t)0);
        for(;;c++) {
            switch(*c) {
                case '-': Flags |= FL_MINUS; break;
                case '+': Flags |= FL_PLUS; break;
                case ' ': Flags |= FL_SPACE; break;
                case '#': Flags |= FL_SHARP; break;
                case '0': Flags |= FL_ZERO; break;
                case '?': Flags |= FL_QUE; break;
                case '$': Flags |= FL_DOLLAR; break;
                default: goto lblFlagOut;
            }
        }
lblFlagOut:
        Width2 = Width = -1;
        if(*c == '*') {
            c++;
            Width = -2;
        } else if(*c >= '0' && *c <= '9') {
            for(Width = 0; *c >= '0' && *c <= '9'; c++)
                Width = Width * 10 + (*c - '0');
        }
        if(*c == '.') {
            c++;
            if(*c == '*') {
                c++;
                Width2 = va_arg(va, int);
            } else if(*c >= '0' && *c <= '9') {
                for(Width2 = 0; *c >= '0' && *c <= '9'; c++)
                    Width2 = Width2 * 10 + (*c - '0');
            }
        }
        ArgSize = SZ_INT;
        switch(*c) {
            case 'l':
                c++;
                if(*c == 'l') {
                    c++;
                    ArgSize = SZ_LONGLONG;
                } else {
                    ArgSize = SZ_LONG;
                }
                break;
            case 'h':
                c++;
                if(*c == 'h') {
                    c++;
                    ArgSize = SZ_CHAR;
                } else {
                    ArgSize = SZ_SHORT;
                }
                break;
            case 'j': c++; ArgSize2 = sizeof(intmax_t); goto lblGetSzBit;
            case 'z': c++; ArgSize2 = sizeof(size_t); goto lblGetSzBit;
            case 't': c++; ArgSize2 = sizeof(ptrdiff_t); goto lblGetSzBit;
            case 'L': c++; ArgSize2 = sizeof(int64_t); goto lblGetSzBit;
            case 'r': c++; ArgSize2 = sizeof(intptr_t); goto lblGetSzBit;
            case 'q':
                c++;
                for(ArgSize2 = ((size_t)0); (*c >= '0') && (*c <= '9'); c++)
                    ArgSize2 = ArgSize2 * ((size_t)10) + (((size_t)*c) - ((size_t)'0'));
                ArgSize2 /= 8;
lblGetSzBit:
                if(ArgSize2 == sizeof(char))
                    ArgSize = SZ_CHAR;
                else if(ArgSize2 == sizeof(short))
                    ArgSize = SZ_SHORT;
                else if(ArgSize2 == sizeof(long))
                    ArgSize = SZ_LONG;
                else if(ArgSize2 == sizeof(int))
                    ArgSize = SZ_INT;
                else if(ArgSize2 == sizeof(long long))
                    ArgSize = SZ_LONGLONG;
                break;
        }
        switch(*c) {
            case '[':
                c++;
                if(*c == '^') {
                    c++;
                    SeqEnd = c + 1; /*For use ']' in control sequence, be careful when written fmt(not use empty seq)*/
                    for(; *SeqEnd != ']' && *SeqEnd != '\0'; SeqEnd++);
                    if(*SeqEnd == '\0')
                        goto lblOut;
                    TempReaded = LqFbuf_ReadTo(State, (Width == -2) ? NULL : va_arg(va, char*), (Width2 == -1) ? -((intptr_t)1) : Width2, c, SeqEnd - c);
                } else {
                    SeqEnd = c + 1; /*For use ']' in control sequence, be careful when written fmt*/
                    for(; *SeqEnd != ']' && *SeqEnd != '\0'; SeqEnd++);
                    if(*SeqEnd == '\0')
                        goto lblOut;
                    TempReaded = LqFbuf_ReadWhile(State, (Width == -2) ? NULL : va_arg(va, char*), (Width2 == -1) ? -((intptr_t)1) : Width2, c, SeqEnd - c);
                }
                if(TempReaded <= ((intptr_t)0)) {
                    if(Flags & FL_QUE) {
                        TempReaded = ((intptr_t)0);
                    } else {
                        goto lblOut;
                    }
                }
                c = SeqEnd + 1;
                Readed += TempReaded;
                CountScanned++;
                continue;
            case '{':
                Dest = (Width == -2) ? NULL : va_arg(va, char*);
                if(c[1] == '^') {
                    c += 2;
                    SeqEnd = c;
                    if(*c == '}') {/* Used for define any seq (Ex. "%{^}6:hello\0" )*/
                        c++;
                        for(ArgSize2 = ((size_t)0); (*c >= '0') && (*c <= '9'); c++)
                            ArgSize2 = ArgSize2 * ((size_t)10) + (((size_t)*c) - ((size_t)'0'));
                        c++;
                        SeqEnd = c + ArgSize2;
                    } else {
                        for(; (*SeqEnd != '}') && (*SeqEnd != '\0'); SeqEnd++);
                    }
                    TempReaded = LqFbuf_ReadWhileNotSame(State, Dest, (Width2 == -1) ? -((intptr_t)1) : Width2, c, SeqEnd - c, Flags & FL_SHARP);
                    if(TempReaded <= ((intptr_t)0)) {
                        if(Flags & FL_QUE) {
                            TempReaded = ((intptr_t)0);
                        } else {
                            goto lblOut;
                        }
                    }
                    if(*SeqEnd == '}')
                        c = SeqEnd + 1;
                    else
                        c = SeqEnd;
                    Readed += TempReaded;
                } else {
                    SeqEnd = c;
                    while(true) {
                        SeqStart = ++SeqEnd;
                        for(; (*SeqEnd != '}') && (*SeqEnd != '\0') && (*SeqEnd != '|'); SeqEnd++);
                        TempReaded = LqFbuf_ReadWhileSame(State, Dest, (Width2 == -1) ? -((intptr_t)1) : Width2, SeqStart, SeqEnd - SeqStart, Flags & FL_SHARP);
                        if(TempReaded >= (SeqEnd - SeqStart)) {
                            Readed += TempReaded;
                            break;
                        } else if(*SeqEnd == '}') {
                            if(!(Flags & FL_QUE)) {
                                goto lblOut;
                            }
                            break;
                        }
                    }
                    for(; (*SeqEnd != '}') && (*SeqEnd != '\0'); SeqEnd++);
                    if(*SeqEnd == '}')
                        c = SeqEnd + 1;
                    else
                        c = SeqEnd;
                }
                CountScanned++;
                continue;
            case 'b':
            case 'B':
                /*Base64 now here*/
                c++;
                Written = (Flags & FL_DOLLAR) ? va_arg(va, int*) : &Exp;
                TempReaded = LqFbuf_ReadBase64(State, (Width == -2) ? NULL : va_arg(va, char*), (Width2 == -1) ? -((intptr_t)1) : Width2, *c == 'B', Flags & FL_SHARP, Written);
                if(TempReaded <= ((intptr_t)0)) {
                    if(Flags & FL_QUE) {
                        TempReaded = ((intptr_t)0);
                    } else {
                        goto lblOut;
                    }
                }
                
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'v':
            case 'V':
                /*hex too*/
                c++;
                Written = (Flags & FL_DOLLAR) ? va_arg(va, int*) : &Exp;
                TempReaded = LqFbuf_ReadHex(State, (Width == -2) ? NULL : va_arg(va, char*), (Width2 == -1) ? -((intptr_t)1) : Width2, Written);
                if(TempReaded <= ((intptr_t)0)) {
                    if(Flags & FL_QUE) {
                        TempReaded = ((intptr_t)0);
                    } else {
                        goto lblOut;
                    }
                }
                
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 's':
                c++;
                TempReaded = LqFbuf_ReadTo(State, (Width == -2) ? NULL : va_arg(va, char*), (Width2 == -1) ? -((intptr_t)1) : Width2, " \n\r\t\0", sizeof(" \n\r\t\0") - 1);
                if(TempReaded <= ((intptr_t)0)) {
                    if(Flags & FL_QUE) {
                        TempReaded = ((intptr_t)0);
                    } else {
                        goto lblOut;
                    }
                }
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'c':
                c++;
                TempReaded = LqFbuf_ReadChar(State, buf, 1, 0);
                if(TempReaded <= ((intptr_t)0)) {
                    if(Flags & FL_QUE) {
                        TempReaded = ((intptr_t)0);
                    } else {
                        goto lblOut;
                    }
                }
                if(Width != -2)
                    *(va_arg(va, int*)) = buf[0];
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'X':
            case 'x': c++; Exp = -1; Radix = 16; goto lblPrintInteger;
            case 'o': c++; Exp = -1; Radix = 8; goto lblPrintInteger;
            case 'u': c++; Exp = -1; Radix = 10; goto lblPrintInteger;
            case 'd':
            case 'i': c++; Exp = Flags & FL_PLUS; Radix = 10;
lblPrintInteger:
                if(Exp >= 0) {
                    TempReaded = LqFbuf_peek_int(State, &ll, Radix, true);
                    if(TempReaded <= ((intptr_t)0)) {
                        if(Flags & FL_QUE) {
                            TempReaded = ((intptr_t)0);
                        } else {
                            goto lblOut;
                        }
                    }
                    if(Width != -2)
                        switch(ArgSize) {
                            case SZ_CHAR: *(va_arg(va, char*)) = ll; break;
                            case SZ_SHORT: *(va_arg(va, short*)) = ll; break;
                            case SZ_INT: *(va_arg(va, int*)) = ll; break;
                            case SZ_LONG: *(va_arg(va, long*)) = ll; break;
                            case SZ_LONGLONG: *(va_arg(va, long long*)) = ll; break;
                        }
                } else {
                    TempReaded = LqFbuf_peek_int(State, &ull, Radix, false);
                    if(TempReaded <= ((intptr_t)0)) {
                        if(Flags & FL_QUE) {
                            TempReaded = ((intptr_t)0);
                        } else {
                            goto lblOut;
                        }
                    }
                    if(Width != -2)
                        switch(ArgSize) {
                            case SZ_CHAR:  *(va_arg(va, unsigned char*)) = ull; break;
                            case SZ_SHORT: *(va_arg(va, unsigned short*)) = ull; break;
                            case SZ_INT: *(va_arg(va, unsigned int*)) = ull; break;
                            case SZ_LONG: *(va_arg(va, unsigned long*)) = ull; break;
                            case SZ_LONGLONG: *(va_arg(va, unsigned long long*)) = ull; break;
                        }
                }
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'A':
            case 'a': c++; Exp = 0; Radix = 16; goto lblPrintDouble;
            case 'E':
            case 'e': c++; Exp = 1; Radix = 10; goto lblPrintDouble;
            case 'G':
            case 'g': c++; Exp = 0; Radix = 10; goto lblPrintDouble;
            case 'F':
            case 'f': c++; Exp = -1; Radix = 10;
lblPrintDouble:
                TempReaded = LqFbuf_peek_double(State, &f, Radix, Exp >= 0);
                if(TempReaded <= ((intptr_t)0)) {
                    if(Flags & FL_QUE) {
                        TempReaded = 0;
                    } else {
                        goto lblOut;
                    }
                }
                if(Width != -2) {
                    if(ArgSize < SZ_LONG)
                        *(va_arg(va, float*)) = f;
                    else if(ArgSize == SZ_LONG)
                        *(va_arg(va, double*)) = f;
                    else
                        *(va_arg(va, long double*)) = f;
                }
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'n':
                c++;
                *(va_arg(va, int*)) = Readed;
                continue;
        }
    }
lblOut:
    if(!(State->Buf->Flags & LQFBUF_POINTER) && !(FunFlags & LQFBUF_SCANF_PEEK)) {
        LqSbufRead(&State->Buf->InBuf, NULL, Readed);
    }
    return CountScanned;
}


LQ_EXTERN_C intptr_t LQ_CALL LqStrToInt(int* Dest, const char* Source, unsigned char Radix) {
    long long v;
    const char* s = Source;
    if(!_StringToInteger(&s, &v, Radix, Radix == 10))
        return -((intptr_t)1);
    *Dest = v;
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToLl(long long* Dest, const char* Source, unsigned char Radix) {
    const char* s = Source;
    if(!_StringToInteger(&s, Dest, Radix, Radix == 10))
        return -((intptr_t)1);
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToUint(unsigned int* Dest, const char* Source, unsigned char Radix) {
    unsigned long long v;
    const char* s = Source;
    if(!_StringToInteger(&s, &v, Radix, false))
        return -((intptr_t)1);
    *Dest = v;
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToUll(unsigned long long* Dest, const char* Source, unsigned char Radix) {
    const char* s = Source;
    if(!_StringToInteger(&s, Dest, Radix, false))
        return -((intptr_t)1);
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromInt(char* Dest, int Source, unsigned char Radix) {
    char* s, *d;
    char Buf[64];
    _IntegerToString(int, char, (Radix == 10) ? 0 : -1, false, Source, Buf, sizeof(Buf), Radix, s);
    for(d = Dest; s < (Buf + sizeof(Buf)); s++, d++)
        *d = *s;
    *d = '\0';
    return d - Dest;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromLl(char* Dest, long long Source, unsigned char Radix) {
    char* s, *d;
    char Buf[64];
    _IntegerToString(long long, char, (Radix == 10) ? 0 : -1, false, Source, Buf, sizeof(Buf), Radix, s);
    for(d = Dest; s < (Buf + sizeof(Buf)); s++, d++)
        *d = *s;
    *d = '\0';
    return d - Dest;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromUint(char* Dest, unsigned int Source, unsigned char Radix) {
    char* s, *d;
    char Buf[64];
    _IntegerToString(int, char, -1, false, Source, Buf, sizeof(Buf), Radix, s);
    for(d = Dest; s < (Buf + sizeof(Buf)); s++, d++)
        *d = *s;
    *d = '\0';
    return d - Dest;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromUll(char* Dest, unsigned long long Source, unsigned char Radix) {
    char* s, *d;
    char Buf[64];
    _IntegerToString(long long, char, -1, false, Source, Buf, sizeof(Buf), Radix, s);
    for(d = Dest; s < (Buf + sizeof(Buf)); s++, d++)
        *d = *s;
    *d = '\0';
    return d - Dest;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToFloat(float* Dest, const char* Source, unsigned char Radix) {
    long double Res;
    if(char* ResOff = _StringToDouble(Source, &Res, Radix, true, '.')) {
        *Dest = Res;
        return ResOff - Source;
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToDouble(double* Dest, const char* Source, unsigned char Radix) {
    long double Res;
    if(char* ResOff = _StringToDouble(Source, &Res, Radix, true, '.')) {
        *Dest = Res;
        return ResOff - Source;
    }
    return -((intptr_t)1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromFloat(char* Dest, float Source, unsigned char Radix) {
    char * ResOff = _DoubleToString(Source, Dest, Radix, 0.00000000000000001, _DTOS_SCALE_EPS | _DTOS_PRINT_EXP_AUTO, '.', 30);
    return ResOff - Dest;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromDouble(char* Dest, double Source, unsigned char Radix) {
    char * ResOff = _DoubleToString(Source, Dest, Radix, 0.00000000000000001, _DTOS_SCALE_EPS | _DTOS_PRINT_EXP_AUTO, '.', 30);
    return ResOff - Dest;
}
