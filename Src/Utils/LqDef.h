/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   Different definitions.
*/

#ifndef __LQ_DEF_H_HAS_BEEN_DEFINED__
#define __LQ_DEF_H_HAS_BEEN_DEFINED__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef long long               LqFileSz;
typedef long long               LqTimeSec;
typedef long long               LqTimeMillisec;
typedef uint16_t                LqEvntFlag;

#define LQ_NUMERIC_IS_SIGNED(type) (((type)(0)) > ((type)-((type)1)))
#define LQ_NUMERIC_MAX(type) ((type)(LQ_NUMERIC_IS_SIGNED(type)? (~((type)0) ^ (((type)1) << ((sizeof(type) * 8) - 1))): (~((type)0))))
#define LQ_NUMERIC_MIN(type) ((type)(LQ_NUMERIC_IS_SIGNED(type)? (((type)1) << ((sizeof(type) * 8) - 1)): ((type)0)))

#ifndef INT8_MIN
# define INT8_MIN LQ_NUMERIC_MIN(int8_t)
#endif
#ifndef INT8_MAX
# define INT8_MAX LQ_NUMERIC_MAX(int8_t)
#endif
#ifndef INT16_MIN
# define INT16_MIN LQ_NUMERIC_MIN(int16_t)
#endif
#ifndef INT16_MAX
# define INT16_MAX LQ_NUMERIC_MAX(int16_t)
#endif
#ifndef INT32_MIN
# define INT32_MIN LQ_NUMERIC_MIN(int32_t)
#endif
#ifndef INT32_MAX
# define INT32_MAX LQ_NUMERIC_MAX(int32_t)
#endif
#ifndef INT64_MIN
# define INT64_MIN LQ_NUMERIC_MIN(int64_t)
#endif
#ifndef INT64_MAX
# define INT64_MAX LQ_NUMERIC_MAX(int64_t)
#endif

#ifndef UINT16_MAX
# define UINT16_MAX LQ_NUMERIC_MAX(uint16_t)
#endif
#ifndef UINT32_MAX
# define UINT32_MAX LQ_NUMERIC_MAX(uint32_t)
#endif
#ifndef UINT64_MAX
# define UINT64_MAX LQ_NUMERIC_MAX(uint64_t)
#endif

#ifndef CHAR_MIN
# define CHAR_MIN LQ_NUMERIC_MIN(char)
#endif
#ifndef CHAR_MAX
# define CHAR_MAX LQ_NUMERIC_MAX(char)
#endif
#ifndef UCHAR_MAX
# define UCHAR_MAX LQ_NUMERIC_MAX(unsigned char)
#endif
#ifndef SHRT_MIN
# define SHRT_MIN LQ_NUMERIC_MIN(short)
#endif
#ifndef SHRT_MAX
# define SHRT_MAX LQ_NUMERIC_MAX(short)
#endif
#ifndef USHRT_MAX
# define USHRT_MAX LQ_NUMERIC_MAX(unsigned short)
#endif
#ifndef INT_MIN
# define INT_MIN LQ_NUMERIC_MIN(int)
#endif
#ifndef INT_MAX
# define INT_MAX LQ_NUMERIC_MAX(int)
#endif
#ifndef UINT_MAX
# define UINT_MAX LQ_NUMERIC_MAX(unsigned int)
#endif
#ifndef LONG_MIN
# define LONG_MIN LQ_NUMERIC_MIN(long)
#endif
#ifndef LONG_MAX
# define LONG_MAX LQ_NUMERIC_MAX(long)
#endif
#ifndef ULONG_MAX
# define ULONG_MAX LQ_NUMERIC_MAX(unsigned long)
#endif
#ifndef LLONG_MIN
# define LLONG_MIN LQ_NUMERIC_MIN(long long)
#endif
#ifndef LLONG_MAX
# define LLONG_MAX LQ_NUMERIC_MAX(long long)
#endif
#ifndef ULLONG_MAX
# define ULLONG_MAX LQ_NUMERIC_MAX(unsigned long long)
#endif

#define lq_max(a,b) (((a) > (b)) ? (a) : (b))
#define lq_min(a,b) (((a) < (b)) ? (a) : (b))

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqListHdr {
    void* First;
    void* Last;
} LqListHdr;

#pragma pack(pop)

#define LqListInit(ListHdr) {\
    ((LqListHdr*)(ListHdr))->Last = ((LqListHdr*)(ListHdr))->First = NULL;\
  }

#define LqListAdd(ListHdr, NewElem, TypeElem) {\
    ((TypeElem*)(NewElem))->Next = NULL;\
    if(((LqListHdr*)(ListHdr))->Last == NULL) { \
        ((LqListHdr*)(ListHdr))->Last = ((LqListHdr*)(ListHdr))->First = ((TypeElem*)(NewElem));\
    }else{\
        ((TypeElem*)(((LqListHdr*)(ListHdr))->Last))->Next = ((TypeElem*)(NewElem));\
        ((LqListHdr*)(ListHdr))->Last = ((TypeElem*)(NewElem));\
    }\
  }

#define LqListAddForward(ListHdr, NewElem, TypeElem) {\
    ((TypeElem*)(NewElem))->Next = (TypeElem*)((LqListHdr*)(ListHdr))->First;\
    ((LqListHdr*)(ListHdr))->First = ((TypeElem*)(NewElem));\
    if(((LqListHdr*)(ListHdr))->Last == NULL)\
        ((LqListHdr*)(ListHdr))->Last = ((TypeElem*)(NewElem));\
  }

#define LqListFirst(ListHdr, TypeElem) ((TypeElem*)((LqListHdr*)(ListHdr))->First)

#define LqListRemove(ListHdr, TypeElem) {\
    if(((LqListHdr*)(ListHdr))->First != NULL) {\
        ((LqListHdr*)(ListHdr))->First = ((TypeElem*)(((LqListHdr*)(ListHdr))->First))->Next;\
        if(((LqListHdr*)(ListHdr))->First == NULL)\
            ((LqListHdr*)(ListHdr))->Last = NULL;\
    }\
  }

////////////////////
#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqTblRow {
    void**  Tbl;
    size_t Count;
    size_t MaxCount;
    float  IncCoef;
    float  DecCoef;
} LqTblRow;
#pragma pack(pop)

#define LqTblRowInit(_Tbl) {\
    (_Tbl)->Count = 0; (_Tbl)->MaxCount = 1; (_Tbl)->Tbl = (void**)LqMemAlloc(sizeof(void*)); (_Tbl)->Tbl[0] = NULL;\
    (_Tbl)->IncCoef = 1.61803398874989484820; (_Tbl)->DecCoef = (_Tbl)->IncCoef + 0.1;\
}

#define LqTblRowRealloc(_Tbl, ___NewCount, _IndexByElem, _TypeElem, _Next) {\
    size_t __NewCount = (size_t)(___NewCount);\
    void** __NewArr = (void**)LqMemAlloc(__NewCount * sizeof(void*));\
    memset(__NewArr, 0, __NewCount * sizeof(void*));\
    _TypeElem *_a, *_n;\
    void **__Arr = (_Tbl)->Tbl, **__MaxArr = __Arr + (_Tbl)->MaxCount;\
    (_Tbl)->MaxCount = __NewCount;\
    for(; __Arr < __MaxArr; __Arr++) {\
        for(_a = ((_TypeElem*)*__Arr); _a; _a = _n) {\
            __i = _IndexByElem(_a, __NewCount); _n = (_TypeElem*)(_a->_Next);\
            _a->_Next = ((_TypeElem*)__NewArr[__i]); __NewArr[__i] = _a;\
    }}\
    LqMemFree((_Tbl)->Tbl);\
    (_Tbl)->Tbl = __NewArr;\
}

#define LqTblRowInsert(_Tbl, _Elem, _Next, _IndexByElem, _TypeElem) {\
    size_t __i;\
    if(((_Tbl)->Count + 1) > (_Tbl)->MaxCount)\
        LqTblRowRealloc((_Tbl), (_Tbl)->MaxCount * (_Tbl)->IncCoef + 1.0, _IndexByElem, _TypeElem, _Next);\
    __i = _IndexByElem((_Elem), (_Tbl)->MaxCount);\
    ((_TypeElem*)(_Elem))->_Next = ((_TypeElem*)(_Tbl)->Tbl[__i]);\
    (_Tbl)->Tbl[__i] = ((_TypeElem*)(_Elem));\
    (_Tbl)->Count++;\
}

#define LqTblRowRemove(_Tbl, _Elem, _Next, _IndexByElem, IsInLoop, _TypeElem) {\
    _TypeElem **__a;\
    size_t __i = _IndexByElem((_Elem), (_Tbl)->MaxCount);\
    for(__a = ((_TypeElem**)&(_Tbl)->Tbl[__i]); *__a != NULL; __a = &((_TypeElem*)(*__a))->_Next)\
        if(*__a == ((void*)(_Elem))){\
            *__a = ((_TypeElem*)(_Elem))->_Next; (_Tbl)->Count--;\
            if(!(IsInLoop) && ((size_t)((_Tbl)->Count * (_Tbl)->DecCoef) < (_Tbl)->MaxCount) && ((_Tbl)->Count > 0))\
                LqTblRowRealloc((_Tbl), (_Tbl)->Count, _IndexByElem, _TypeElem, _Next);\
            break;\
        }\
}

#define LqTblRowSearch(_Tbl, _Key, _Res, _Next, _IndexByKey, _CmpKeyVal, _TypeElem) {\
    void* __a;\
    size_t __i = _IndexByKey((_Key), (_Tbl)->MaxCount); (_Res) = NULL;\
    for(__a = (_Tbl)->Tbl[__i]; __a != NULL; __a = ((_TypeElem*)__a)->_Next) {\
        if(_CmpKeyVal((_Key), (_TypeElem*)__a)) { (_Res) = ((_TypeElem*)__a); break; }\
    }\
}

#define LqTblRowForEach(_Tbl, _Elem, _Next, _TypeElem) \
    for(_TypeElem **__b = (_TypeElem**)(_Tbl)->Tbl, **__m = __b + (_Tbl)->MaxCount, *__a = NULL, *__n; (__b < __m) && (__a == NULL); __b++)\
        for(__a = *__b; (((_Elem) = __a) != NULL) && (__n = (_TypeElem*)__a->_Next, 1); __a = __n)

#define LqTblRowUninit(_Tbl) { if((_Tbl)->Tbl != NULL) LqMemFree((_Tbl)->Tbl); }

#endif
