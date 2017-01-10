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


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct PageHeader {
    size_t SizePage;
    size_t StartOffset;
    size_t EndOffset;
    void* NextPage;
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
    Hdr->EndOffset = Hdr->StartOffset = 0;
    Hdr->SizePage = sizeof(PageSize) - sizeof(PageHeader);
    return Hdr;
}

static PageHeader* LqSbufCreatePage(LqSbuf* StreamBuf, intptr_t Size) {
    if(Size <= 0) return nullptr;
    if(Size < (4096 - sizeof(PageHeader)))
        return LqSbufCreatePage<4096>(StreamBuf);
    else if(Size < (16384 - sizeof(PageHeader)))
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
    intptr_t Written = 0, SizeWrite;
    PageHeader* DestPage;
    do {
        if((DestPage = LqSbufCreatePage(StreamBuf, Size)) == nullptr)
            break;
        SizeWrite = lq_min(Size, DestPage->SizePage);
        memcpy(DestPage + 1, (char*)Data + Written, SizeWrite);
        DestPage->EndOffset = DestPage->StartOffset + SizeWrite;
        Written += SizeWrite;
        Size -= SizeWrite;
    } while(Size > 0);
    StreamBuf->Len += Written;
    return Written;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufWrite(LqSbuf* StreamBuf, const void* DataSource, intptr_t Size) {
    intptr_t CommonWritten, SizeWrite;
    PageHeader* Hdr;
    if(StreamBuf->PageN == nullptr)
        return LqSbufAddPages(StreamBuf, DataSource, Size);
    CommonWritten = 0;
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
    if(Size > 0)
        CommonWritten += LqSbufAddPages(StreamBuf, DataSource, Size);
    return CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufRead(LqSbuf* StreamBuf, void* DestBuf, intptr_t Size) {
    PageHeader* HdrPage;
    intptr_t CommonReadedSize = 0, PageDataSize, ReadSize;

    while(StreamBuf->Page0 != nullptr) {
        HdrPage = (PageHeader*)StreamBuf->Page0;
        PageDataSize = HdrPage->EndOffset - HdrPage->StartOffset;
        ReadSize = lq_min(PageDataSize, Size);
        if(DestBuf != nullptr)
            memcpy((char*)DestBuf + CommonReadedSize, (char*)(HdrPage + 1) + HdrPage->StartOffset, ReadSize);
        CommonReadedSize += ReadSize;
        Size -= ReadSize;
        if(Size <= 0) {
            HdrPage->StartOffset += ReadSize;
            break;
        } else {
            LqSbufRemoveFirstPage(StreamBuf);
        }
    }
    StreamBuf->Len -= CommonReadedSize;
    StreamBuf->GlobOffset += CommonReadedSize;
    return CommonReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufPeek(const LqSbuf* StreamBuf, void* DataDest, intptr_t Size) {
    intptr_t ReadedSize = 0, LenRead;
    PageHeader* Hdr;
    for(Hdr = (PageHeader*)StreamBuf->Page0; (Size > ReadedSize) && (Hdr != nullptr); Hdr = (PageHeader*)Hdr->NextPage) {
        LenRead = lq_min(Size - ReadedSize, Hdr->EndOffset - Hdr->StartOffset);
        memcpy((char*)DataDest + ReadedSize, (char*)(Hdr + 1) + Hdr->StartOffset, LenRead);
        ReadedSize += LenRead;
    }
    return ReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufTransfer(LqSbuf* StreamBufSource, LqSbuf* StreamBufDest, intptr_t Size) {
    intptr_t CommonWrittenSize = 0, PageDataSize, ReadSize, Written;
    PageHeader* Hdr;
    while(StreamBufSource->Page0 != nullptr) {
        Hdr = (PageHeader*)StreamBufSource->Page0;
        PageDataSize = Hdr->EndOffset - Hdr->StartOffset;
        ReadSize = lq_min(PageDataSize, Size);
        Written = LqSbufWrite(StreamBufDest, (char*)(Hdr + 1) + Hdr->StartOffset, ReadSize);
        CommonWrittenSize += Written;
        Size -= Written;
        if(Written < PageDataSize) {
            Hdr->StartOffset += Written;
            break;
        } else {
            LqSbufRemoveFirstPage(StreamBufSource);
        }
        if(Written < ReadSize)
            break;
    }
    StreamBufSource->Len -= CommonWrittenSize;
    StreamBufSource->GlobOffset += CommonWrittenSize;
    return CommonWrittenSize;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionFirst(LqSbuf* StreamBuf, LqSbufReadRegion* Reg, intptr_t Size) {
    Reg->CommonWritten = 0;
    Reg->Fin = false;
    if(StreamBuf->Page0 == nullptr)
        return false;
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

    if((Reg->Written < Reg->_PageDataSize) || Reg->Fin) {
        ((PageHeader*)Reg->_Hdr)->StartOffset += Reg->Written;
lblOut:
        Reg->_StreamBuf->Len -= Reg->CommonWritten;
        Reg->_StreamBuf->GlobOffset += Reg->CommonWritten;
        return false;
    } else {
        LqSbufRemoveFirstPage(Reg->_StreamBuf);
    }
    if((Reg->Written < Reg->SourceLen) || ((PageHeader*)Reg->_StreamBuf->Page0 == nullptr))
        goto lblOut;
    Reg->_Hdr = (PageHeader*)Reg->_StreamBuf->Page0;
    Reg->_PageDataSize = ((PageHeader*)Reg->_Hdr)->EndOffset - ((PageHeader*)Reg->_Hdr)->StartOffset;
    Reg->SourceLen = lq_min(Reg->_PageDataSize, Reg->_Size);
    Reg->Source = (char*)(((PageHeader*)Reg->_Hdr) + 1) + ((PageHeader*)Reg->_Hdr)->StartOffset;
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionIsEos(LqSbufReadRegion* Reg) {
    return ((PageHeader*)Reg->_Hdr)->NextPage == nullptr;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufCheck(LqSbuf* StreamBuf) {
    size_t Len = 0;
    PageHeader*p = (PageHeader*)StreamBuf->Page0;
    for(; p; p = (PageHeader*)p->NextPage)
        Len += (p->EndOffset - p->StartOffset);
    return Len;
}

LQ_EXTERN_C bool LQ_CALL LqSbufWriteRegionFirst(LqSbuf* StreamBuf, LqSbufWriteRegion* Reg, intptr_t Size) {
    Reg->CommonReaded = 0;
    Reg->_Size = Size;
    Reg->Fin = false;
    Reg->_StreamBuf = StreamBuf;
    if(StreamBuf->PageN == nullptr) {
lblOperationZero:
        if((Reg->_Hdr = LqSbufCreatePage(StreamBuf, Reg->_Size)) == nullptr)
            return false;
        Reg->DestLen = lq_min(Reg->_Size, ((PageHeader*)Reg->_Hdr)->SizePage);
        Reg->Dest = ((PageHeader*)Reg->_Hdr) + 1;
        Reg->_TypeOperat = 0;
        return true;
    }
    if(((PageHeader*)StreamBuf->PageN)->EndOffset < ((PageHeader*)StreamBuf->PageN)->SizePage) {
        Reg->_Hdr = (PageHeader*)StreamBuf->PageN;
        Reg->DestLen = lq_min(((PageHeader*)Reg->_Hdr)->SizePage - ((PageHeader*)Reg->_Hdr)->EndOffset, Reg->_Size);
        if(Reg->DestLen < 0)
            return false;
        if(Reg->DestLen == 0)
            goto lblOperationZero;
        Reg->Dest = (char*)(((PageHeader*)Reg->_Hdr) + 1) + ((PageHeader*)Reg->_Hdr)->EndOffset;
        Reg->_TypeOperat = 1;
        return true;
    }
    goto lblOperationZero;
}

LQ_EXTERN_C bool LQ_CALL LqSbufWriteRegionNext(LqSbufWriteRegion* Reg) {
    Reg->CommonReaded += Reg->Readed;
    ((PageHeader*)Reg->_Hdr)->EndOffset += Reg->Readed;
    Reg->_Size -= Reg->Readed;
    if(Reg->_TypeOperat == 0) {
        if((Reg->Readed < Reg->DestLen) || (Reg->_Size <= 0) || Reg->Fin) {
            if(Reg->Readed == 0)
                LqSbufRemoveLastPage(Reg->_StreamBuf);
            goto lblOut;
        }
lblOpZero:
        if((Reg->_Hdr = LqSbufCreatePage(Reg->_StreamBuf, Reg->_Size)) == nullptr)
            goto lblOut;
        Reg->DestLen = lq_min(Reg->_Size, ((PageHeader*)Reg->_Hdr)->SizePage);
        Reg->Dest = ((PageHeader*)Reg->_Hdr) + 1;
        return true;
    }
    if(Reg->_TypeOperat == 1) {
        if((Reg->Readed < Reg->DestLen) || (Reg->_Size <= 0) || Reg->Fin)
            goto lblOut;
        Reg->_TypeOperat = 0;
        goto lblOpZero;
    }
lblOut:
    Reg->_StreamBuf->Len += Reg->CommonReaded;
    return false;
}

LQ_EXTERN_C void LQ_CALL LqSbufPtrSet(LqSbuf* StreamBuf, LqSbufPtr* StreamPointerDest) {
    StreamPointerDest->StreamBuf = StreamBuf;
    if(StreamBuf->Len <= 0) {
        static char buf;
        LqSbufWrite(StreamBuf, &buf, 1);
        LqSbufRead(StreamBuf, &buf, 1);
    }
    StreamPointerDest->GlobOffset = StreamBuf->GlobOffset;
    StreamPointerDest->Page = StreamBuf->Page0;
    StreamPointerDest->OffsetInPage = (StreamBuf->Page0 != nullptr) ? ((PageHeader*)StreamBuf->Page0)->StartOffset : 0;
}

LQ_EXTERN_C void LQ_CALL LqSbufPtrCopy(LqSbufPtr* StreamPointerDest, LqSbufPtr* StreamPointerSource) {
    *StreamPointerDest = *StreamPointerSource;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufReadByPtr(LqSbufPtr* StreamPointer, void* DataDest, intptr_t Size) {
    intptr_t ReadedSize, LenReadInCurPage;
    PageHeader* PageHdr;
    size_t StartOffset;

    if((StreamPointer->GlobOffset < StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamPointer->StreamBuf->GlobOffset + StreamPointer->StreamBuf->Len)))
        return -1;
    if(StreamPointer->StreamBuf->Len <= 0)
        return 0;
    ReadedSize = 0;
    if(StreamPointer->Page == nullptr) {
        PageHdr = (PageHeader*)StreamPointer->StreamBuf->Page0;
        StartOffset = PageHdr->StartOffset;
    } else {
        PageHdr = (PageHeader*)StreamPointer->Page;
        StartOffset = StreamPointer->OffsetInPage;
    }
    for(; ; ) {
        LenReadInCurPage = lq_min(Size - ReadedSize, PageHdr->EndOffset - StartOffset);
        if(DataDest != nullptr)
            memcpy((char*)DataDest + ReadedSize, (char*)(PageHdr + 1) + StartOffset, LenReadInCurPage);
        ReadedSize += LenReadInCurPage;
        if((PageHdr->NextPage == nullptr) || (Size <= ReadedSize)) {
            if(PageHdr == StreamPointer->Page) {
                StreamPointer->OffsetInPage += LenReadInCurPage;
            } else {
                StreamPointer->OffsetInPage = StartOffset + LenReadInCurPage;
                StreamPointer->Page = PageHdr;
            }
            StreamPointer->GlobOffset += ReadedSize;
            break;
        }
        PageHdr = (PageHeader*)PageHdr->NextPage;
        StartOffset = PageHdr->StartOffset;
    }
    return ReadedSize;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionPtrFirst(LqSbufPtr* StreamPointer, LqSbufReadRegionPtr* Reg, intptr_t Size) {
    Reg->Fin = false;
    Reg->CommonWritten = 0;
    Reg->_StreamPointer = (LqSbufPtr*)StreamPointer;
    if((StreamPointer->GlobOffset < StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamPointer->StreamBuf->GlobOffset + StreamPointer->StreamBuf->Len)))
        return false;
    if(StreamPointer->StreamBuf->Len <= 0)
        return false;
    if((StreamPointer->GlobOffset == StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->Page == nullptr)) {
        StreamPointer->Page = StreamPointer->StreamBuf->Page0;
        StreamPointer->OffsetInPage = ((PageHeader*)StreamPointer->Page)->StartOffset;
    }
    Reg->_Size = Size;
    Reg->SourceLen = lq_min((size_t)Reg->_Size, ((PageHeader*)StreamPointer->Page)->EndOffset - StreamPointer->OffsetInPage);
    Reg->Source = (char*)(((PageHeader*)StreamPointer->Page) + 1) + StreamPointer->OffsetInPage;
    if(Reg->SourceLen == 0) {
        Reg->Written = 0;
        return LqSbufReadRegionPtrNext(Reg);
    }
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqSbufReadRegionPtrNext(LqSbufReadRegionPtr* Reg) {
    Reg->CommonWritten += Reg->Written;
    Reg->_StreamPointer->GlobOffset += Reg->Written;
    if((((PageHeader*)Reg->_StreamPointer->Page)->NextPage == nullptr) || (Reg->CommonWritten >= Reg->_Size) || (Reg->Written < Reg->SourceLen) || Reg->Fin) {
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
    return ((PageHeader*)Reg->_StreamPointer->Page)->NextPage == nullptr;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufTransferByPtr(LqSbufPtr* StreamPointer, LqSbuf* StreamBufDest, intptr_t Size) {
    intptr_t CommonWritten = 0, LenReadInCurPage, Written;
    PageHeader* PageHdr;
    size_t StartOffset;

    if((StreamPointer->GlobOffset < StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamPointer->StreamBuf->GlobOffset + StreamPointer->StreamBuf->Len)))
        return -1;
    CommonWritten = 0;
    if(StreamPointer->Page == nullptr) {
        PageHdr = (PageHeader*)StreamPointer->StreamBuf->Page0;
        StartOffset = PageHdr->StartOffset;
    } else {
        PageHdr = (PageHeader*)StreamPointer->Page;
        StartOffset = StreamPointer->OffsetInPage;
    }
    for(; ; ) {
        LenReadInCurPage = lq_min(Size - CommonWritten, PageHdr->EndOffset - StartOffset);
        Written = LqSbufWrite(StreamBufDest, (char*)(PageHdr + 1) + StartOffset, LenReadInCurPage);
        CommonWritten += Written;
        if((PageHdr->NextPage == nullptr) || (Size <= CommonWritten) || (Written < LenReadInCurPage)) {
            if(PageHdr == StreamPointer->Page) {
                StreamPointer->OffsetInPage += Written;
            } else {
                StreamPointer->OffsetInPage = StartOffset + Written;
                StreamPointer->Page = PageHdr;
            }
            StreamPointer->GlobOffset += CommonWritten;
            break;
        }
        PageHdr = (PageHeader*)PageHdr->NextPage;
        StartOffset = PageHdr->StartOffset;
    }
    return CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufPeekByPtr(LqSbufPtr* StreamPointer, void* DataDest, intptr_t Size) {
    if((StreamPointer->GlobOffset < StreamPointer->StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamPointer->StreamBuf->GlobOffset + StreamPointer->StreamBuf->Len)))
        return -1;
    intptr_t ReadedSize = 0;
    PageHeader* PageHdr;
    size_t StartOffset;
    if(StreamPointer->Page == nullptr) {
        PageHdr = (PageHeader*)StreamPointer->StreamBuf->Page0;
        StartOffset = PageHdr->StartOffset;
    } else {
        PageHdr = (PageHeader*)StreamPointer->Page;
        StartOffset = StreamPointer->OffsetInPage;
    }
    for(; ; ) {
        intptr_t LenReadInCurPage = lq_min(Size - ReadedSize, PageHdr->EndOffset - StartOffset);
        memcpy((char*)DataDest + ReadedSize, (char*)(PageHdr + 1) + StartOffset, LenReadInCurPage);
        ReadedSize += LenReadInCurPage;
        if((PageHdr->NextPage == nullptr) || (Size <= ReadedSize))
            break;
        PageHdr = (PageHeader*)PageHdr->NextPage;
        StartOffset = PageHdr->StartOffset;
    }
    return ReadedSize;
}

LQ_EXTERN_C void LQ_CALL LqSbufInit(LqSbuf* StreamBuf) {
    StreamBuf->GlobOffset = 0;
    StreamBuf->Len = 0;
    StreamBuf->Page0 = StreamBuf->PageN = nullptr;
}

LQ_EXTERN_C void LQ_CALL LqSbufUninit(LqSbuf* StreamBuf) {
    for(PageHeader* Hdr; (Hdr = (PageHeader*)StreamBuf->Page0) != nullptr;)
        LqSbufRemoveFirstPage(StreamBuf);
}

////////////////////////////




#define FL_MINUS 0x1
#define FL_PLUS 0x2
#define FL_SPACE 0x4
#define FL_SHARP 0x8
#define FL_ZERO 0x10
#define FL_QUE 0x20
#define FL_DOLLAR 0x40

#define SZ_CHAR 0
#define SZ_SHORT 1
#define SZ_INT 2
#define SZ_LONG 3
#define SZ_LONGLONG 4

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
        return nullptr;

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
        return nullptr;
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
        if(_StringToInteger(&c, &Exp, Radix, true)) {
            Result *= pow((long double)Radix, Exp);
        } else {
            c--;
        }
    }
    *Dest = (long double)Result;
    return (char*)c;
}

//////////////////////////////////////////////////////////////
///////LqFwbuf
/////////////////////////////////////////////////////////////


#ifdef LQPLATFORM_WINDOWS
static intptr_t _TermWriteProc(LqFwbuf* Context, char* Buf, size_t Size) {
    DWORD Written;
    if(WriteConsoleA((HANDLE)Context->UserData, Buf, Size, &Written, NULL) == FALSE) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFWBUF_WOULD_BLOCK : LQFWBUF_WRITE_ERROR);
        return 0;
    }
    return Size;
}
#endif

static intptr_t _SockWriteProc(LqFwbuf* Context, char* Buf, size_t Size) {
    int Written;
    if((Written = send((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFWBUF_WOULD_BLOCK : LQFWBUF_WRITE_ERROR);
        return 0;
    }
    return Written;
}

static intptr_t _FileWriteProc(LqFwbuf* Context, char* Buf, size_t Size) {
    intptr_t Written;
    if((Written = LqFileWrite((int)Context->UserData, Buf, Size)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFWBUF_WOULD_BLOCK : LQFWBUF_WRITE_ERROR);
        return 0;
    }
    return Written;
}

static void LqFwbuf_lock(LqFwbuf* Context) {
    if(Context->Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK(Context->Locker);
    }
}

static void LqFwbuf_unlock(LqFwbuf* Context) {
    if(Context->Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_UNLOCK(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_UNLOCK(Context->Locker);
    }
}

static intptr_t _LqFwbuf_vprintf(LqFwbuf* Context, const char* Fmt, va_list va);
static intptr_t _LqFwbuf_write(LqFwbuf* Context, const void* Buf, size_t Size);

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_fdopen(
    LqFwbuf* Context,
    int Fd,
    uint8_t Flags,
    size_t MinBuffSize,
    size_t MaxBuffSize
) {
    intptr_t(*WriteProc)(LqFwbuf*, char*, size_t);
    if(LqFileIsSocket(Fd))
        WriteProc = _SockWriteProc;
#ifdef LQPLATFORM_WINDOWS
    else if(LqFileIsTerminal(Fd))
        WriteProc = _TermWriteProc;
#endif
    else
        WriteProc = _FileWriteProc;
    return LqFwbuf_init(Context, Flags, WriteProc, 0, (void*)Fd, MinBuffSize, MaxBuffSize);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_init(
    LqFwbuf* Context,
    uint8_t Flags,
    intptr_t(*WriteProc)(LqFwbuf*, char*, size_t),
    char FloatSep,
    void* UserData,
    size_t MinBuffSize,
    size_t MaxBuffSize
) {
    if(Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_INIT(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK_INIT(Context->Locker);
    }
    LqSbufInit(&Context->Buf);
    Context->Flags = Flags;
    Context->FloatSep = (FloatSep == 0) ? '.' : FloatSep;
    Context->MinFlush = MinBuffSize;
    Context->MaxFlush = MaxBuffSize;
    Context->UserData = UserData;
    Context->WriteProc = WriteProc;
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFwbuf_uninit(LqFwbuf* Context) {
    LqSbufUninit(&Context->Buf);
    if(Context->Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_UNINIT(Context->FastLocker);
    } else {
        LQFWRBUF_DEFAULT_LOCK_UNINIT(Context->Locker);
    }
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_open(
    LqFwbuf* Context,
    const char* FileName,
    uint32_t Flags,
    int Access,
    size_t MinBuffSize,
    size_t MaxBuffSize
) {
    int Fd = LqFileOpen(FileName, Flags, Access);
    if(Fd == -1)
        return -1;
    if(LqFwbuf_init(Context, LQFWBUF_PRINTF_FLUSH | LQFRWBUF_FAST_LK, _FileWriteProc, 0, (void*)Fd, MinBuffSize, MaxBuffSize) != 0)
        LqFileClose(Fd);
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFwbuf_close(LqFwbuf* Context) {
    LqFwbuf_uninit(Context);
    LqFileClose((int)Context->UserData);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_printf(LqFwbuf* Context, const char* Fmt, ...) {
    va_list arp;
    va_start(arp, Fmt);
    int Res = LqFwbuf_vprintf(Context, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_svnprintf(char* Dest, size_t DestSize, const char* Fmt, va_list va) {
    typedef struct ProcData {
        char* Dest, *MaxDest;

        static intptr_t WriteProc(LqFwbuf* Context, char* Buf, size_t Size) {
            ProcData* Param = (ProcData*)Context->UserData;
            if((Param->MaxDest - Param->Dest) <= 0) {
                Context->Flags |= LQFWBUF_WRITE_ERROR;
                return 0;
            }
            intptr_t TargetSize = lq_min(Size, Param->MaxDest - Param->Dest);
            memcpy(Param->Dest, Buf, TargetSize);
            Param->Dest += TargetSize;
            return TargetSize;
        }
    } ProcData;
    ProcData Param = {Dest, Dest + DestSize};
    LqFwbuf Context;
    Context.FloatSep = '.';
    LqSbufInit(&Context.Buf);
    Context.Flags = 0;
    Context.MinFlush = 0;
    Context.MaxFlush = 0;
    Context.WriteProc = ProcData::WriteProc;
    Context.UserData = &Param;
    intptr_t Res = _LqFwbuf_vprintf(&Context, Fmt, va);
    if((Param.Dest + 1) < Param.MaxDest)
        Param.Dest[0] = '\0';
    LqSbufUninit(&Context.Buf);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_snprintf(char* Dest, size_t DestSize, const char* Fmt, ...) {
    va_list arp;
    va_start(arp, Fmt);
    int Res = LqFwbuf_svnprintf(Dest, DestSize, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_putc(LqFwbuf* Context, int Val) {
    char Buf = Val;
    return LqFwbuf_putsn(Context, &Buf, 1);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_puts(LqFwbuf* Context, const char* Val) {
    return LqFwbuf_putsn(Context, Val, LqStrLen(Val));
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_putsn(LqFwbuf* Context, const char* Val, size_t Len) {
    return LqFwbuf_write(Context, Val, Len);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_write(LqFwbuf* Context, const void* Buf, size_t Size) {
    LqFwbuf_lock(Context);
    intptr_t Res = _LqFwbuf_write(Context, Buf, Size);
    LqFwbuf_unlock(Context);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_flush(LqFwbuf* Context) {
    LqSbufReadRegion Region;
    Context->Flags &= ~LQFWBUF_WRITE_ERROR;
    Region.CommonWritten = 0;
    if(Context->Buf.Len > 0) {
        for(bool __r = LqSbufReadRegionFirst(&Context->Buf, &Region, Context->Buf.Len); __r; __r = LqSbufReadRegionNext(&Region))
            if((Region.Written = Context->WriteProc(Context, (char*)Region.Source, Region.SourceLen)) < 0) {
                Region.Written = 0;
                Region.Fin = true;
            }
    }
    return Region.CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_size(LqFwbuf* Context) {
    return Context->Buf.Len;
}

static intptr_t _LqFwbuf_putc(LqFwbuf* Context, int Val) {
    char Buf = Val;
    return _LqFwbuf_write(Context, &Buf, 1);
}

static intptr_t _LqFwbuf_write(LqFwbuf* Context, const void* Buf, size_t Size) {
    intptr_t Written1, Written2, Written3, WriteLen;
    Context->Flags &= ~LQFWBUF_WRITE_ERROR;
    if(((Context->Buf.Len + Size) > Context->MinFlush) || (Context->Flags & LQFWBUF_PUT_FLUSH)) {
        WriteLen = Context->Buf.Len;
        Written1 = LqFwbuf_flush(Context);
        if(Context->Flags & LQFWBUF_WRITE_ERROR)
            return 0;
        if(Written1 >= WriteLen) {
            Written2 = Context->WriteProc(Context, (char*)Buf, Size);
            if(Written2 < Size) {
                if((Context->Flags & LQFWBUF_WRITE_ERROR) || ((Context->Buf.Len + Size) > Context->MaxFlush))
                    return Written2;
                Written2 = lq_max(Written2, 0);
                Written3 = LqSbufWrite(&Context->Buf, (char*)Size + Written2, Size - Written2);
                return lq_max(Written3, 0) + Written2;
            }
            return Written2;
        } else if((Context->Buf.Len + Size) < Context->MaxFlush)
            return LqSbufWrite(&Context->Buf, (char*)Buf, Size);
    } else {
        return LqSbufWrite(&Context->Buf, (char*)Buf, Size);
    }
    return 0;
}

#define lq_putsn(Context, Buf, Size) {\
    if(_LqFwbuf_write((Context), (Buf), (Size)) < (Size)){lq_errno_set(EOVERFLOW); goto lblOut;}\
    if((Context)->Flags & LQFWBUF_WRITE_ERROR) { goto lblOut;}\
    if((Context)->Flags & LQFWBUF_WOULD_BLOCK) { (Context)->Flags &= ~(LQFWBUF_WOULD_BLOCK); (Context)->MinFlush = (Context)->MaxFlush; }\
}

#define lq_putc(Context, Val) {\
    if(_LqFwbuf_putc((Context), (Val)) <= 0) {lq_errno_set(EOVERFLOW); goto lblOut;}\
    if((Context)->Flags & LQFWBUF_WRITE_ERROR){ goto lblOut;}\
    if((Context)->Flags & LQFWBUF_WOULD_BLOCK) { (Context)->Flags &= ~(LQFWBUF_WOULD_BLOCK); (Context)->MinFlush = (Context)->MaxFlush; }\
}
static const uchar CodeChainBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const uchar CodeChainBase64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"; //-_-
static const uchar CodeChainHexLower[] = "0123456789abcdef";
static const uchar CodeChainHexUpper[] = "0123456789ABCDEF";

LQ_EXTERN_C intptr_t LQ_CALL LqFwbuf_vprintf(LqFwbuf* Context, const char* Fmt, va_list va) {
    LqFwbuf_lock(Context);
    intptr_t Res = _LqFwbuf_vprintf(Context, Fmt, va);
    LqFwbuf_unlock(Context);
    return Res;
}

static intptr_t _LqFwbuf_vprintf(LqFwbuf* Context, const char* Fmt, va_list va) {
    const char* c = Fmt;
    const char* s;
    char ct;
    uint32_t fl;
    bool IsUpper;
    int exp, Written = 0, CountPrinted = 0, width, width2, sz, Radix;
    uint8_t Tflags = Context->Flags;
    intptr_t MinFlush = Context->MinFlush;

    if(Context->Flags & LQFWBUF_PRINTF_FLUSH)
        Context->Flags &= ~(LQFWBUF_PUT_FLUSH);
    char buf[64];
    for(; ;) {
        for(s = c; (*c != '\0') && (*c != '%'); c++);
        if(c != s) {
            lq_putsn(Context, s, c - s);
            Written += (c - s);
        }
        if(*c == '\0') goto lblOut2;
        c++;
        fl = 0;
        for(; ; c++) {
            switch(*c) {
                case '-': fl |= FL_MINUS; break;
                case '+': fl |= FL_PLUS; break;
                case ' ': fl |= FL_SPACE; break;
                case '#': fl |= FL_SHARP; break;
                case '0': fl |= FL_ZERO; break;
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
        sz = SZ_INT;
        switch(*c) {
            case 'l':
                c++;
                if(*c == 'l') {
                    c++;
                    sz = SZ_LONGLONG;
                } else {
                    sz = SZ_LONG;
                }
                break;
            case 'h':
                c++;
                if(*c == 'h') {
                    c++;
                    sz = SZ_CHAR;
                } else {
                    sz = SZ_SHORT;
                }
                break;
            case 'j': c++; exp = sizeof(intmax_t); goto lblGetSzBit;
            case 'z': c++; exp = sizeof(size_t); goto lblGetSzBit;
            case 't': c++; exp = sizeof(ptrdiff_t); goto lblGetSzBit;
            case 'L': c++; exp = sizeof(int64_t); goto lblGetSzBit;
            case 'r': c++; exp = sizeof(intptr_t); goto lblGetSzBit;
            case 'q':
                c++;
                for(exp = 0; *c >= '0' && *c <= '9'; c++)
                    exp = exp * 10 + (*c - '0');
                exp /= 8;
lblGetSzBit:
                if(exp == sizeof(char))
                    sz = SZ_CHAR;
                else if(exp == sizeof(short))
                    sz = SZ_SHORT;
                else if(exp == sizeof(long))
                    sz = SZ_LONG;
                else if(exp == sizeof(int))
                    sz = SZ_INT;
                else if(exp == sizeof(long long))
                    sz = SZ_LONGLONG;
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
                    ct = (fl & FL_ZERO) ? '0' : ' ';
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
                const uchar *CodeChain = (*c == 'B') ? CodeChainBase64URL : CodeChainBase64;
                c++;
                const uchar *s = (const uchar*)va_arg(va, const char*);
                const uchar *sm;
                uchar *d;
                uchar *md = (uchar*)buf + sizeof(buf) - 4;
                if(width2 != -1)
                    sm = s + width2;
                else
                    for(sm = s; *sm; sm++);

                do {
                    d = (uchar*)buf;
                    while(((sm - s) > 2) && (d < md)) {
                        *d++ = CodeChain[(s[0] >> 2) & 0x3f];
                        *d++ = CodeChain[((s[0] & 3) << 4) | (s[1] >> 4)];
                        *d++ = CodeChain[((s[1] & 0x0f) << 2) | (s[2] >> 6)];
                        *d++ = CodeChain[s[2] & 0x3f];
                        s += 3;
                    }

                    lq_putsn(Context, buf, d - (uchar*)buf);
                    Written += (d - (uchar*)buf);
                } while((sm - s) > 2);
                d = (uchar*)buf;
                if((sm - s) > 0) {
                    *d++ = CodeChain[(s[0] >> 2) & 0x3f];
                    if((sm - s) == 1) {
                        *d++ = CodeChain[(s[0] & 3) << 4];
                        if(fl & FL_SHARP) *d++ = '=';
                    } else {
                        *d++ = CodeChain[((s[0] & 3) << 4) | (s[1] >> 4)];
                        *d++ = CodeChain[(s[1] & 0x0f) << 2];
                    }
                    if(fl & FL_SHARP) *d++ = '=';
                    lq_putsn(Context, buf, d - (uchar*)buf);
                    Written += (d - (uchar*)buf);
                }
                CountPrinted++;
            }
            break;
            case 'v':
            case 'V':
            {
                const uchar *CodeChain = (*c == 'v') ? CodeChainHexLower : CodeChainHexUpper;
                c++;
                const uchar *s = va_arg(va, const uchar*);
                uchar *md = (uchar*)buf + sizeof(buf);
                const uchar *sm;
                uchar *d;
                if(width2 != -1)
                    sm = s + width2;
                else
                    for(sm = s; *sm; sm++);
                do {
                    d = (uchar*)buf;
                    for(; (s < sm) && (d < md); s++) {
                        *d++ = CodeChain[*s >> 4];
                        *d++ = CodeChain[*s & 0x0f];
                    }
                    lq_putsn(Context, buf, d - (uchar*)buf);
                    Written += (d - (uchar*)buf);
                } while(s < sm);
                CountPrinted++;
            }
            continue;
            case 'X': c++; IsUpper = true; exp = -1; Radix = 16; goto lblPrintInteger;
            case 'x': c++; IsUpper = false; exp = -1; Radix = 16; goto lblPrintInteger;
            case 'o': c++; exp = -1; Radix = 8; goto lblPrintInteger;
            case 'u': c++; exp = -1; Radix = 10; goto lblPrintInteger;
            case 'd':
            case 'i': c++; exp = fl & FL_PLUS; Radix = 10;
lblPrintInteger:
            {
                char* start;
                const char* end = buf + sizeof(buf);
                long l;
                long long ll;
                unsigned long ul;
                unsigned long long ull;
                if(exp >= 0) {
                    switch(sz) {
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
                    switch(sz) {
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
                    if(!(fl & FL_ZERO)) {
                        ct = ' ';
                        for(; z > 0; z--) {
                            lq_putsn(Context, &ct, 1);
                            Written++;
                        }
                    }
                    if(*j == '+' || *j == '-') {
                        lq_putsn(Context, j++, 1);
                    }
                    if(fl & FL_ZERO) {
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
                    ((fl & FL_PLUS) ? _DTOS_PRINT_SIGN : 0) |
                    ((exp == 0) ? _DTOS_PRINT_EXP_AUTO : ((exp == 1) ? _DTOS_PRINT_EXP : 0)),
                    Context->FloatSep,
                    width2);
                end--;
                if((width == -1) || ((end - buf) >= width)) {
                    lq_putsn(Context, buf, end - buf);
                    Written += (end - buf);
                } else {
                    char* j = buf;
                    size_t z = width - (end - buf);
                    if(!(fl & FL_ZERO)) {
                        ct = ' ';
                        for(; z > 0; z--) {
                            lq_putsn(Context, &ct, 1);
                            Written++;
                        }
                    }
                    if(*j == '+' || *j == '-') {
                        lq_putsn(Context, j++, 1);
                    }
                    if(fl & FL_ZERO) {
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
    if(Context->Flags & LQFWBUF_PRINTF_FLUSH)
        LqFwbuf_flush(Context);
lblOut:
    Context->MinFlush = MinFlush;
    Context->Flags = Tflags;
    return Written;
}

//////////////////////////////////////////////////////////////
///////LqFrbuf
/////////////////////////////////////////////////////////////

#pragma pack(push)
#pragma pack(1)
typedef struct LqFrbuf_state {
    union {
        void* StringPos;
        LqSbufPtr BufPtr;
    };
    bool IsString;
    LqFrbuf* Buf;
} LqFrbuf_state;

#pragma pack(pop)

static inline void LqFrbuf_SaveState(LqFrbuf_state* Dest, LqFrbuf_state* Source) {
    Dest->Buf = Source->Buf;
    if(Dest->IsString = Source->IsString) {
        Dest->StringPos = Source->StringPos;
    } else {
        LqSbufPtrCopy(&Dest->BufPtr, &Source->BufPtr);
    }
}

static inline void LqFrbuf_RestoreState(LqFrbuf_state* Dest, LqFrbuf_state* Source) {
    if(Source->IsString) {
        Dest->StringPos = Source->StringPos;
    } else {
        LqSbufPtrCopy(&Dest->BufPtr, &Source->BufPtr);
    }
}

#define ReadPortionBegin(ProcName, Args, ...) \
static intptr_t ProcName(LqFrbuf_state* State, char* Dst, size_t DstLen, ##__VA_ARGS__ ){\
    bool Fin = false, Eof, WithoutCopy = Dst == nullptr, __r, t = true; \
    char* Dest = Dst, *MaxDest = Dst + DstLen, *Source; Args;\
    LqSbufReadRegionPtr Region;\
    LqSbufWriteRegion RegionW;\
    LqFrbuf_state __MainState;\
    size_t Res = 0;\
    LqFrbuf_SaveState(&__MainState, State);\
    if(State->IsString){\
        Eof = __r = true;\
        State->Buf->Flags |= LQFRBUF_READ_EOF;\
        Region.SourceLen = lq_min((char*)State->Buf->UserData - (char*)State->StringPos, MaxDest - Dest);\
        Region.CommonWritten = 0;\
        Region.Source = (char*)State->StringPos + Region.CommonWritten;\
    } else {\
__lblContinueRead:\
        __r = LqSbufReadRegionPtrFirst(&State->BufPtr, &Region, MaxDest - Dest);\
        Eof = (State->Buf->Flags & LQFRBUF_READ_EOF) && LqSbufReadRegionPtrIsEos(&Region);\
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
            Eof = (State->Buf->Flags & LQFRBUF_READ_EOF) && LqSbufReadRegionPtrIsEos(&Region);\
        }else { break; }\
    }\
    Res += Region.CommonWritten;\
    if(!Fin) {\
        if(State->Buf->Flags & (LQFRBUF_READ_ERROR | LQFRBUF_WOULD_BLOCK | LQFRBUF_READ_EOF)) {\
            goto __lblNotMatch2;\
        }else{\
            for(bool __r = LqSbufWriteRegionFirst(&State->Buf->Buf, &RegionW, State->Buf->PortionSize); __r; __r = LqSbufWriteRegionNext(&RegionW)){ \
                if((RegionW.Readed = State->Buf->ReadProc(State->Buf, (char*)RegionW.Dest, RegionW.DestLen)) < 0) {\
                    RegionW.Readed = 0;\
                    RegionW.Fin = true;\
                }\
            }\
            goto __lblContinueRead;\
        }\
    }\
    WhenOutExec; \
    if((!WithoutCopy) && ((Dest - Dst) < DstLen)) \
        *Dest = '\0'; \
    return Res; \
lblNotMatch:\
    if(!State->IsString){\
        Region.Fin = true;\
        Region.Written = 0;\
        __r = LqSbufReadRegionPtrNext(&Region); \
    }\
__lblNotMatch2:\
    LqFrbuf_RestoreState(State ,&__MainState);\
    return -1;\
}

ReadPortionBegin(LqFrbuf_ReadWhile, ((void)0), const char* ControlSeq, size_t ControlSeqSize)
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

ReadPortionBegin(LqFrbuf_ReadWhileSame, ((void)0), const char* ControlSeq, size_t ControlSeqSize)
char *Control = (char*)ControlSeq,
*MaxControl = Control + ControlSeqSize,
*MaxSource = Source + lq_min(lq_min(Region.SourceLen, MaxDest - Dest), MaxControl - Control);

if(WithoutCopy) {
    for(; (Source < MaxSource) && (*Source == *Control); Source++, Control++, Dest++);
} else {
    for(; (Source < MaxSource) && (*Source == *Control); Source++, Control++, Dest++)
        *Dest = *Source;
}
Fin |= ((Dest >= MaxDest) || (Control >= MaxControl));
if((Source < MaxSource) && (*Source != *Control) || (Eof && !Fin))
    goto lblNotMatch;
ControlSeq = Control;
ReadPortionEnd(((void)0))

///////////////////////////////
ReadPortionBegin(LqFrbuf_ReadWhileNotSame, ((void)0), const char* ControlSeq, size_t ControlSeqSize)
char
*MaxControl = (char*)ControlSeq + ControlSeqSize,
*MaxSource = Source + lq_min(Region.SourceLen, MaxDest - Dest),
*s, *c, *ms;
ms = MaxSource - ControlSeqSize + 1;
if(WithoutCopy) {
    for(; Source < ms; Source++, Dest++)
        for(s = Source, c = (char*)ControlSeq; ; s++, c++) {
            if(c >= MaxControl) {
                Fin |= true; goto lblOut;
            }
            if(*c != *s)
                break;
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
lblOut:
Fin |= (Dest >= MaxDest);
if(Eof && !Fin && (Source >= ms)) {
    Fin |= true;
    for(; Source < MaxSource; Source++, Dest++)
        if(!WithoutCopy)
            *Dest = *Source;
}
ReadPortionEnd(((void)0))


ReadPortionBegin(LqFrbuf_ReadTo, ((void)0), const char* ControlSeq, size_t ControlSeqSize)
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

ReadPortionBegin(LqFrbuf_ReadChar, ((void)0), void*)
*Dest = *Source;
Source++;
Dest++;
Fin = true;
ReadPortionEnd(((void)0))

ReadPortionBegin(LqFrbuf_ReadInt, bool Signed2 = !Signed; int HaveSign = 0, unsigned Radx, bool Signed)
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

static const uchar _DecodeChain[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

static const uchar _DecodeSeqURL[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};


ReadPortionBegin(LqFrbuf_ReadBase64, uchar *Chain = ((IsUrl) ? (uchar*)_DecodeSeqURL : (uchar*)_DecodeChain),/*Args*/bool IsUrl, bool IsReadEq, int* Written)
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
        *Dest1++ = (uchar)(Chain[Source[0]] << 2 | Chain[Source[1]] >> 4);
        *Dest1++ = (uchar)(Chain[Source[1]] << 4 | Chain[Source[2]] >> 2);
        *Dest1++ = (uchar)(Chain[Source[2]] << 6 | Chain[Source[3]]);
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
            *Dest2++ = (uchar)(Chain[Source[0]] << 2 | Chain[Source[1]] >> 4);
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
            *Dest2++ = (uchar)(Chain[Source[1]] << 4 | Chain[Source[2]] >> 2);
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


ReadPortionBegin(LqFrbuf_ReadHex, ((void)0), int* Written)
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

static intptr_t LQ_CALL _LqFrbuf_vscanf(LqFrbuf_state* State, int Flags, const char* Fmt, va_list va);

static intptr_t LqFrbuf_peek_double(
    LqFrbuf_state* State,
    long double* Dest,
    unsigned Radx,
    bool InfInd
) {
    LqFrbuf_state MainState, ExpState;
    LqFrbuf_SaveState(&MainState, State);
    char Buf[128];
    char* c = Buf, *m = c + sizeof(Buf);
    const char* cp;
    int r = LqFrbuf_ReadInt(State, c, m - c, Radx, true);
    if(r <= 0)
        goto lblExit;
    c += r;
    r = LqFrbuf_ReadWhileSame(State, &State->Buf->FloatSep, 1, c, m - c);
    if(r <= 0)
        goto lblProcessDouble;
    c += r;
    r = LqFrbuf_ReadInt(State, c, m - c, Radx, false);
    if(r > 0) c += r;

    LqFrbuf_SaveState(&ExpState, State);
    r = LqFrbuf_ReadWhileSame(State, (Radx <= 10) ? "e" : "p", 1, c, m - c);
    if(r > 0) {
        r = LqFrbuf_ReadInt(State, c + 1, m - c, Radx, true);
        if(r <= 0)
            LqFrbuf_RestoreState(State, &ExpState);
        else
            c += (r + 1);
    }
lblProcessDouble:
    *c = '\0';
    cp = _StringToDouble(Buf, Dest, Radx, InfInd, State->Buf->FloatSep);
    if(cp == nullptr)
        goto lblExit;
    if(cp != c) {
        LqFrbuf_RestoreState(&MainState, State);
        LqFrbuf_ReadTo(State, "\0", 1, Buf, cp - Buf);
        c = (char*)cp;
    }
    return c - Buf;
lblExit:
    LqFrbuf_RestoreState(&MainState, State);
    return -1;
}

static intptr_t LqFrbuf_peek_int(LqFrbuf_state* State, void* Dest, unsigned Radx, bool Signed) {
    LqFrbuf_state MainState;
    char Buf[128];
    const char* EndPointer;
    intptr_t Readed;

    LqFrbuf_SaveState(&MainState, State);
    Readed = LqFrbuf_ReadInt(State, Buf, sizeof(Buf), Radx, Signed);
    if(Readed <= 0)
        return -1;
    Buf[Readed] = '\0';
    EndPointer = Buf;

    if(!_StringToInteger(&EndPointer, Dest, Radx, Signed)) {
        LqFrbuf_RestoreState(State, &MainState);
        return -1;
    }
    if(EndPointer != (Buf + Readed)) {
        LqFrbuf_RestoreState(State, &MainState);
        LqFrbuf_ReadTo(State, "\0", 1, Buf, EndPointer - Buf);
    }
    return EndPointer - Buf;
}

#ifdef LQPLATFORM_WINDOWS
static intptr_t _TermReadProc(LqFrbuf* Context, char* Buf, size_t Size) {
    wchar_t Buf1[3] = {0};
    DWORD Readed;
    if(Size <= 0)
        return 0;
    if(ReadConsoleW((HANDLE)Context->UserData, Buf1, 1, &Readed, nullptr) == FALSE) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFWBUF_WOULD_BLOCK : LQFRBUF_READ_ERROR);
        return 0;
    }
    if(LqStrUtf16Count(Buf1[0]) >= 2) {
        if(ReadConsoleW((HANDLE)Context->UserData, Buf1 + 1, 1, &Readed, nullptr) == FALSE) {
            Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFWBUF_WOULD_BLOCK : LQFRBUF_READ_ERROR);
            return -1;
        }
    }
    int Res = LqCpConvertFromWcs(Buf1, Buf, Size);
    return (Res > 0) ? (Res - 1) : Res;
}
#endif

static intptr_t _SockReadProc(LqFrbuf* Context, char* Buf, size_t Size) {
    int Readed;
    if((Readed = recv((int)Context->UserData, Buf, Size, 0)) == -1) {
        Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFRBUF_WOULD_BLOCK : LQFRBUF_READ_ERROR);
        return 0;
    }
    return Readed;
}

static intptr_t _FileReadProc(LqFrbuf* Context, char* Buf, size_t Size) {
    intptr_t Readed;
    if(Size == 0)
        return 0;
    if((Readed = LqFileRead((int)Context->UserData, Buf, Size)) <= 0) {
        if(Readed == 0)
            Context->Flags |= LQFRBUF_READ_EOF;
        else
            Context->Flags |= ((LQERR_IS_WOULD_BLOCK) ? LQFWBUF_WOULD_BLOCK : LQFRBUF_READ_ERROR);
        return 0;
    }
    return Readed;
}

static intptr_t _StringReadProc(LqFrbuf* Context, char* Buf, size_t Size) {
    Context->Flags |= LQFRBUF_READ_ERROR;
    return 0;
}

static void LqFrbuf_lock(LqFrbuf* Context) {
    if(Context->Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK(Context->Locker);
    }
}

static void LqFrbuf_unlock(LqFrbuf* Context) {
    if(Context->Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_UNLOCK(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_UNLOCK(Context->Locker);
    }
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_init(
    LqFrbuf* Context,
    uint8_t Flags,
    intptr_t(*ReadProc)(LqFrbuf*, char*, size_t),
    char FloatSep,
    void* UserData,
    size_t PortionSize
) {
    if(Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_INIT(Context->FastLocker);
    } else {
        LQFRWBUF_DEFAULT_LOCK_INIT(Context->Locker);
    }
    LqSbufInit(&Context->Buf);
    Context->Flags = Flags;
    Context->FloatSep = (FloatSep == 0) ? '.' : FloatSep;
    Context->PortionSize = PortionSize;
    Context->UserData = UserData;
    Context->ReadProc = ReadProc;
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFrbuf_uninit(LqFrbuf* Context) {
    LqSbufUninit(&Context->Buf);
    if(Context->Flags & LQFRWBUF_FAST_LK) {
        LQFRWBUF_FAST_LOCK_UNINIT(Context->FastLocker);
    } else {
        LQFWRBUF_DEFAULT_LOCK_UNINIT(Context->Locker);
    }
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_fdopen(
    LqFrbuf* Context,
    int Fd,
    uint8_t Flags,
    size_t PortionSize
) {
    intptr_t(*ReadProc)(LqFrbuf*, char*, size_t);
    if(LqFileIsSocket(Fd))
        ReadProc = _SockReadProc;
#ifdef LQPLATFORM_WINDOWS
    else if(LqFileIsTerminal(Fd))
        ReadProc = _TermReadProc;
#endif
    else
        ReadProc = _FileReadProc;
    return LqFrbuf_init(Context, Flags, ReadProc, 0, (void*)Fd, PortionSize);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_open(
    LqFrbuf* Context,
    const char* FileName,
    uint32_t Flags,
    int Access,
    size_t PortionSize
) {
    int Fd = LqFileOpen(FileName, Flags, Access);
    if(Fd == -1)
        return -1;
    if(LqFrbuf_init(Context, LQFWBUF_PRINTF_FLUSH | LQFRWBUF_FAST_LK, _FileReadProc, 0, (void*)Fd, PortionSize) != 0)
        LqFileClose(Fd);
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFrbuf_close(LqFrbuf* Context) {
    LqFrbuf_uninit(Context);
    LqFileClose((int)Context->UserData);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_vssncanf(const char* Source, size_t LenSource, const char* Fmt, va_list va) {
    LqFrbuf_state MainState;
    LqFrbuf Context;
    Context.ReadProc = _StringReadProc;
    Context.Flags = 0;
    Context.FloatSep = '.';
    Context.PortionSize = 1;
    Context.UserData = (char*)Source + LenSource;
    MainState.StringPos = (void*)Source;
    MainState.Buf = &Context;
    MainState.IsString = true;
    return _LqFrbuf_vscanf(&MainState, LQFRBUF_SCANF_PEEK, Fmt, va);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_snscanf(const char* Source, size_t LenSource, const char* Fmt, ...) {
    va_list arp;
    intptr_t Res;
    va_start(arp, Fmt);
    Res = LqFrbuf_vssncanf(Source, LenSource, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_vscanf(LqFrbuf* Context, int Flags, const char* Fmt, va_list va) {
    LqFrbuf_state MainState;
    LqFrbuf_lock(Context);
    LqSbufPtrSet(&Context->Buf, &MainState.BufPtr);
    MainState.Buf = Context;
    MainState.IsString = false;
    intptr_t Res = _LqFrbuf_vscanf(&MainState, Flags, Fmt, va);
    LqFrbuf_unlock(Context);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_scanf(LqFrbuf* Context, int Flags, const char* Fmt, ...) {
    va_list arp;
    intptr_t Res;
    va_start(arp, Fmt);
    Res = LqFrbuf_vscanf(Context, Flags, Fmt, arp);
    va_end(arp);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_getc(LqFrbuf* Context, int* Dest) {
    char Val;
    intptr_t Res = LqFrbuf_read(Context, &Val, 1);
    *Dest = Val;
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_read(LqFrbuf* Context, void* Buf, size_t Len) {
    LqSbufReadRegion ReginR;
    LqSbufWriteRegion RegionW;
    intptr_t Res = 0, Len2;
    bool End = false, WithoutCopy = Buf == nullptr;
    char *Dest = (char*)Buf, *MaxDest = (char*)Buf + Len;

    LqFrbuf_lock(Context);
    Context->Flags &= ~(LQFRBUF_READ_ERROR | LQFRBUF_WOULD_BLOCK);
    while(true) {
        for(bool __r = LqSbufReadRegionFirst(&Context->Buf, &ReginR, MaxDest - Dest); __r; __r = LqSbufReadRegionNext(&ReginR)) {
            Len2 = lq_min(ReginR.SourceLen, MaxDest - Dest);
            if(!WithoutCopy)
                memcpy(Dest, (char*)ReginR.Source, Len2);
            Dest += Len2;
            ReginR.Fin |= (Dest >= MaxDest);
            ReginR.Written = Len2;
        }
        Res += ReginR.CommonWritten;
        if(ReginR.Fin) break;
        if(End) {
            if(Res == 0)
                return -1;
            LqFrbuf_unlock(Context);
            return Res;
        }
        for(bool __r = LqSbufWriteRegionFirst(&Context->Buf, &RegionW, Context->PortionSize); __r; __r = LqSbufWriteRegionNext(&RegionW))
            if((RegionW.Readed = Context->ReadProc(Context, (char*)RegionW.Dest, RegionW.DestLen)) < 0) {
                RegionW.Readed = 0;
                RegionW.Fin = true;
            }
        End = Context->Flags & (LQFRBUF_READ_ERROR | LQFRBUF_WOULD_BLOCK);
    }
    LqFrbuf_unlock(Context);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_peek(LqFrbuf* Context, void* Buf, size_t Len) {
    LqSbufWriteRegion RegionW;
    LqSbufReadRegionPtr  RegionPtrR;
    intptr_t Res, Len2;
    char *Dest, *MaxDest;
    LqFrbuf_state MainState;

    Res = 0;
    Dest = (char*)Buf;
    MaxDest = (char*)Buf + Len;
    LqFrbuf_lock(Context);
    LqSbufPtrSet(&Context->Buf, &MainState.BufPtr);
    MainState.Buf = Context;
    Context->Flags &= ~(LQFRBUF_READ_ERROR | LQFRBUF_WOULD_BLOCK);
    while(true) {
        for(bool __r = LqSbufReadRegionPtrFirst(&MainState.BufPtr, &RegionPtrR, MaxDest - Dest); __r; __r = LqSbufReadRegionPtrNext(&RegionPtrR)) {
            Len2 = lq_min(RegionPtrR.SourceLen, MaxDest - Dest);
            memcpy(Dest, (char*)RegionPtrR.Source, Len2);
            Dest += Len2;
            RegionPtrR.Fin |= (Dest >= MaxDest);
            RegionPtrR.Written = Len2;
        }
        Res += RegionPtrR.CommonWritten;
        if(RegionPtrR.Fin) break;
        if(Context->Flags & (LQFRBUF_READ_ERROR | LQFRBUF_WOULD_BLOCK)) {
            if(Res == 0)
                return -1;
            LqFrbuf_unlock(Context);
            return Res;
        }
        for(bool __r = LqSbufWriteRegionFirst(&Context->Buf, &RegionW, Context->PortionSize); __r; __r = LqSbufWriteRegionNext(&RegionW))
            if((RegionW.Readed = Context->ReadProc(Context, (char*)RegionW.Dest, RegionW.DestLen)) < 0) {
                RegionW.Readed = 0;
                RegionW.Fin = true;
            }
    }
    LqFrbuf_unlock(Context);
    return Res;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_flush(LqFrbuf* Context) {
    return LqFrbuf_read(Context, nullptr, Context->Buf.Len);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFrbuf_size(LqFrbuf* Context) {
    return Context->Buf.Len;
}

static intptr_t LQ_CALL _LqFrbuf_vscanf(LqFrbuf_state* State, int FunFlags, const char* Fmt, va_list va) {
    const char* c = Fmt;
    const char* s;
    uint32_t Flags;
    int Exp, Readed, CountScanned, Width, Width2, ArgSize, Radix, TempReaded;
    uint8_t Tflags;
    char buf[64];
    long long ll;
    unsigned long long ull;
    long double f;
    const char* SeqEnd;
    const char* SeqStart;
    char* Dest;
    int* Written;

    Readed = 0;
    CountScanned = 0;
    Tflags = State->Buf->Flags;

    if(FunFlags & LQFRBUF_SCANF_PEEK_WHEN_ERR)
        FunFlags |= LQFRBUF_SCANF_PEEK;
    State->Buf->Flags &= ~(LQFRBUF_READ_ERROR | LQFRBUF_WOULD_BLOCK | LQFRBUF_READ_EOF);
    for(; ;) {
        for(s = c; (*c != '\0') && (*c != '%'); c++);
        if(c != s) {
            TempReaded = LqFrbuf_ReadWhileSame(State, nullptr, 0xffff, s, (c - s));
            if(TempReaded < (c - s))
                goto lblOut;
            Readed += TempReaded;
        }
        if(*c == '\0') {
            if(FunFlags & LQFRBUF_SCANF_PEEK_WHEN_ERR)
                FunFlags &= ~LQFRBUF_SCANF_PEEK;
            goto lblOut;
        }
        c++;
        Flags = 0;
        for(; ; c++) {
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
            case 'j': c++; Exp = sizeof(intmax_t); goto lblGetSzBit;
            case 'z': c++; Exp = sizeof(size_t); goto lblGetSzBit;
            case 't': c++; Exp = sizeof(ptrdiff_t); goto lblGetSzBit;
            case 'L': c++; Exp = sizeof(int64_t); goto lblGetSzBit;
            case 'r': c++; Exp = sizeof(intptr_t); goto lblGetSzBit;
            case 'q':
                c++;
                for(Exp = 0; *c >= '0' && *c <= '9'; c++)
                    Exp = Exp * 10 + (*c - '0');
                Exp /= 8;
lblGetSzBit:
                if(Exp == sizeof(char))
                    ArgSize = SZ_CHAR;
                else if(Exp == sizeof(short))
                    ArgSize = SZ_SHORT;
                else if(Exp == sizeof(long))
                    ArgSize = SZ_LONG;
                else if(Exp == sizeof(int))
                    ArgSize = SZ_INT;
                else if(Exp == sizeof(long long))
                    ArgSize = SZ_LONGLONG;
                break;
        }
        switch(*c) {
            case '[':
                c++;
                if(*c == '^') {
                    c++;
                    SeqEnd = c;
                    for(; *SeqEnd != ']' && *SeqEnd != '\0'; SeqEnd++);
                    if(*SeqEnd == '\0')
                        goto lblOut;
                    TempReaded = LqFrbuf_ReadTo(State, (Width == -2) ? nullptr : va_arg(va, char*), (Width2 == -1) ? INT_MAX : Width2, c, SeqEnd - c);
                } else {
                    SeqEnd = c;
                    for(; *SeqEnd != ']' && *SeqEnd != '\0'; SeqEnd++);
                    if(*SeqEnd == '\0')
                        goto lblOut;
                    TempReaded = LqFrbuf_ReadWhile(State, (Width == -2) ? nullptr : va_arg(va, char*), (Width2 == -1) ? INT_MAX : Width2, c, SeqEnd - c);
                }
                if(TempReaded <= 0) {
                    if(Flags & FL_QUE)
                        TempReaded = 0;
                    else
                        goto lblOut;
                }
                c = SeqEnd + 1;
                Readed += TempReaded;
                CountScanned++;
                continue;
            case '{':
                Dest = (Width == -2) ? nullptr : va_arg(va, char*);
                if(c[1] == '^') {
                    c += 2;
                    SeqEnd = c;
                    for(; (*SeqEnd != '}') && (*SeqEnd != '\0'); SeqEnd++);
                    TempReaded = LqFrbuf_ReadWhileNotSame(State, Dest, (Width2 == -1) ? INT_MAX : Width2, c, SeqEnd - c);
                    if(TempReaded <= 0) {
                        if(Flags & FL_QUE)
                            TempReaded = 0;
                        else
                            goto lblOut;
                    }
                    if(*SeqEnd == '}')
                        c = SeqEnd + 1;
                    else
                        c = SeqEnd;
                    Readed += TempReaded;
                } else {
                    SeqEnd = c;
                    while(1) {
                        SeqStart = ++SeqEnd;
                        for(; (*SeqEnd != '}') && (*SeqEnd != '\0') && (*SeqEnd != '|'); SeqEnd++);
                        TempReaded = LqFrbuf_ReadWhileSame(State, Dest, (Width2 == -1) ? INT_MAX : Width2, SeqStart, SeqEnd - SeqStart);
                        if(TempReaded >= (SeqEnd - SeqStart)) {
                            Readed += TempReaded;
                            break;
                        } else if(*SeqEnd == '}') {
                            if(!(Flags & FL_QUE))
                                goto lblOut;
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
            case '<':

                continue;
            case 'b':
            case 'B':
                /*Base64 now here*/
                Written = (Flags & FL_DOLLAR) ? va_arg(va, int*) : &Exp;
                TempReaded = LqFrbuf_ReadBase64(State, (Width == -2) ? nullptr : va_arg(va, char*), (Width2 == -1) ? INT_MAX : Width2, *c == 'B', Flags & FL_SHARP, Written);
                c++;
                if(TempReaded <= 0) {
                    if(Flags & FL_QUE)
                        TempReaded = 0;
                    else
                        goto lblOut;
                }
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'v':
            case 'V':
                /*hex too*/
                Written = (Flags & FL_DOLLAR) ? va_arg(va, int*) : &Exp;
                TempReaded = LqFrbuf_ReadHex(State, (Width == -2) ? nullptr : va_arg(va, char*), (Width2 == -1) ? INT_MAX : Width2, Written);
                c++;
                if(TempReaded <= 0) {
                    if(Flags & FL_QUE)
                        TempReaded = 0;
                    else
                        goto lblOut;
                }
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 's':
                c++;
                TempReaded = LqFrbuf_ReadTo(State, (Width == -2) ? nullptr : va_arg(va, char*), (Width2 == -1) ? INT_MAX : Width2, " \n\r\t\0", sizeof(" \n\r\t\0") - 1);
                if(TempReaded <= 0) {
                    if(Flags & FL_QUE)
                        TempReaded = 0;
                    else
                        goto lblOut;
                }
                Readed += TempReaded;
                CountScanned++;
                continue;
            case 'c':
                c++;
                TempReaded = LqFrbuf_ReadChar(State, buf, 1, 0);
                if(TempReaded <= 0) {
                    if(Flags & FL_QUE)
                        TempReaded = 0;
                    else
                        goto lblOut;
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
                    TempReaded = LqFrbuf_peek_int(State, &ll, Radix, true);
                    if(TempReaded <= 0) {
                        if(Flags & FL_QUE)
                            TempReaded = 0;
                        else
                            goto lblOut;
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
                    TempReaded = LqFrbuf_peek_int(State, &ull, Radix, false);
                    if(TempReaded <= 0) {
                        if(Flags & FL_QUE)
                            TempReaded = 0;
                        else
                            goto lblOut;
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
                TempReaded = LqFrbuf_peek_double(State, &f, Radix, Exp >= 0);
                if(TempReaded <= 0) {
                    if(Flags & FL_QUE)
                        TempReaded = 0;
                    else
                        goto lblOut;
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
    if(!(FunFlags & LQFRBUF_SCANF_PEEK))
        LqSbufRead(&State->Buf->Buf, nullptr, Readed);
    return CountScanned;
}


//////////////////////////////////////////////////////////////
///////LqFbuf
/////////////////////////////////////////////////////////////

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_init(
    LqFbuf* Context,
    uint8_t Flags,
    intptr_t(*ReadProc)(LqFrbuf*, char*, size_t),
    intptr_t(*WriteProc)(LqFwbuf*, char*, size_t),
    intptr_t(*SeekProc)(LqFbuf*, int64_t, int),
    char FloatSep,
    void* UserData,
    size_t WriteMinBuffSize,
    size_t WriteMaxBuffSize,
    size_t ReadPortionSize
) {
    if(LqFrbuf_init(&Context->InBuf, Flags, ReadProc, FloatSep, UserData, ReadPortionSize) < 0)
        return -1;
    if(LqFwbuf_init(&Context->OutBuf, Flags, WriteProc, FloatSep, UserData, WriteMinBuffSize, WriteMaxBuffSize) < 0)
        return -1;
    Context->SeekProc = SeekProc;
    return 0;
}

static intptr_t _SeekProc(LqFbuf* Buf, int64_t Offset, int Flags) {
    return LqFileSeek((int)Buf->InBuf.UserData, Offset, Flags);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_fdopen(
    LqFbuf* Context,
    uint8_t Flags,
    int Fd,
    char FloatSep,
    size_t WriteMinBuffSize,
    size_t WriteMaxBuffSize,
    size_t ReadPortionSize
) {
    if(LqFrbuf_fdopen(&Context->InBuf, Fd, Flags, ReadPortionSize) < 0)
        return -1;
    if(LqFwbuf_fdopen(&Context->OutBuf, Fd, Flags, WriteMinBuffSize, WriteMaxBuffSize) < 0)
        return -1;
    Context->SeekProc = _SeekProc;
    return 0;
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_open(
    LqFbuf* Context,
    const char* FileName,
    uint32_t FileFlags,
    int Access,
    char FloatSep,
    size_t WriteMinBuffSize,
    size_t WriteMaxBuffSize,
    size_t ReadPortionSize
) {
    int Fd = LqFileOpen(FileName, FileFlags, Access);
    if(Fd == -1)
        return -1;
    if(LqFrbuf_fdopen(&Context->InBuf, Fd, LQFRWBUF_FAST_LK, ReadPortionSize) < 0)
        return -1;
    if(LqFwbuf_fdopen(&Context->OutBuf, Fd, LQFRWBUF_FAST_LK, WriteMinBuffSize, WriteMaxBuffSize) < 0)
        return -1;
    Context->SeekProc = _SeekProc;
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqFbuf_uninit(LqFbuf* Context) {
    LqSbufUninit(&Context->OutBuf.Buf);
    LqSbufUninit(&Context->InBuf.Buf);
}

LQ_EXTERN_C void LQ_CALL LqFbuf_close(LqFbuf* Context) {
    LqSbufUninit(&Context->InBuf.Buf);
    LqSbufUninit(&Context->OutBuf.Buf);
    LqFileClose((int)Context->InBuf.UserData);
}

LQ_EXTERN_C intptr_t LQ_CALL LqFbuf_seek(LqFbuf* Context, int64_t Offset, int Flags) {
    LqFwbuf_flush(&Context->OutBuf);
    LqFrbuf_flush(&Context->InBuf);
    return Context->SeekProc(Context, Offset, Flags);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToInt(int* Dest, const char* Source, unsigned char Radix) {
    long long v;
    const char* s = Source;
    if(!_StringToInteger(&s, &v, Radix, Radix == 10))
        return -1;
    *Dest = v;
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToLl(long long* Dest, const char* Source, unsigned char Radix) {
    const char* s = Source;
    if(!_StringToInteger(&s, Dest, Radix, Radix == 10))
        return -1;
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToUint(unsigned int* Dest, const char* Source, unsigned char Radix) {
    unsigned long long v;
    const char* s = Source;
    if(!_StringToInteger(&s, &v, Radix, false))
        return -1;
    *Dest = v;
    return (s - Source);
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToUll(unsigned long long* Dest, const char* Source, unsigned char Radix) {
    const char* s = Source;
    if(!_StringToInteger(&s, Dest, Radix, false))
        return -1;
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
    return -1;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrToDouble(double* Dest, const char* Source, unsigned char Radix) {
    long double Res;
    if(char* ResOff = _StringToDouble(Source, &Res, Radix, true, '.')) {
        *Dest = Res;
        return ResOff - Source;
    }
    return -1;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromFloat(char* Dest, float Source, unsigned char Radix) {
    char * ResOff = _DoubleToString(Source, Dest, Radix, 0.00000000000000001, _DTOS_SCALE_EPS | _DTOS_PRINT_EXP_AUTO, '.', 30);
    return ResOff - Dest;
}

LQ_EXTERN_C intptr_t LQ_CALL LqStrFromDouble(char* Dest, double Source, unsigned char Radix) {
    char * ResOff = _DoubleToString(Source, Dest, Radix, 0.00000000000000001, _DTOS_SCALE_EPS | _DTOS_PRINT_EXP_AUTO, '.', 30);
    return ResOff - Dest;
}
