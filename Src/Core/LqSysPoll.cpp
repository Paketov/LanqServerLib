/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSysPoll... - Multiplatform abstracted event folower
* This part of server support:
*       +Windows native events objects.
*       +linux epoll.
*       +kevent for FreeBSD like systems.(*But not yet implemented)
*       +poll for others unix systems.
*
*/

#include "LqSysPoll.h"
#include "LqLog.h"
#include "LqConn.h"

#include "LqWrkBoss.h"
#include "LqAtm.hpp"
#include "LqAlloc.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#ifndef LQEVNT_INCREASE_COEFFICIENT
# define LQEVNT_INCREASE_COEFFICIENT LQ_GOLDEN_RATIO
#endif
#ifndef LQEVNT_DECREASE_COEFFICIENT
# define LQEVNT_DECREASE_COEFFICIENT (LQ_GOLDEN_RATIO + 0.1)
#endif

#define LQ_EVNT

/////////////////

#define LqArrInit(Arr){\
    ((__LqArr*)(Arr))->Data = NULL;\
    ((__LqArr*)(Arr))->Count = 0;\
    ((__LqArr*)(Arr))->AllocCount = 0;\
    ((__LqArr*)(Arr))->IsRemoved = false;\
}

#define LqArrUninit(Arr){\
    if(((__LqArr*)(Arr))->Data != NULL)\
        ___free(((__LqArr*)(Arr))->Data);\
}

#define LqArrPushBack(Arr, TypeVal) {\
 if(((__LqArr*)(Arr))->Count >= ((__LqArr*)(Arr))->AllocCount){\
     intptr_t _NewSize = (intptr_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))((__LqArr*)(Arr))->Count * LQEVNT_INCREASE_COEFFICIENT);\
	 _NewSize++;\
     ((__LqArr*)(Arr))->Data = ___realloc(((__LqArr*)(Arr))->Data, sizeof(TypeVal) * _NewSize);\
     ((__LqArr*)(Arr))->AllocCount = _NewSize;\
 }\
 ((__LqArr*)(Arr))->Count++;\
}

#define LqArrResize(Arr, TypeVal, NewSize) {\
 ((__LqArr*)(Arr))->Data = ___realloc(((__LqArr*)(Arr))->Data, sizeof(TypeVal) * (NewSize));\
 ((__LqArr*)(Arr))->AllocCount = (NewSize); \
 ((__LqArr*)(Arr))->Count = (NewSize); \
}

#define LqArrBack(Arr, TypeVal) (((TypeVal*)(((__LqArr*)(Arr))->Data))[((__LqArr*)(Arr))->Count - 1])
#define LqArrAt(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr*)(Arr))->Data))[Index])

#define LqArrRemoveAt(Arr, TypeVal, Index, EmptyVal) {\
       LqArrAt(Arr, TypeVal, Index) = (EmptyVal);\
       ((__LqArr*)(Arr))->IsRemoved = true;\
}

#define LqArrAlignAfterRemove(Arr, TypeVal, EmptyVal) {\
   if(((__LqArr*)(Arr))->IsRemoved){\
       ((__LqArr*)(Arr))->IsRemoved = false;\
       register TypeVal* _a = &LqArrAt(Arr, TypeVal, 0); \
       register TypeVal* _le = _a + ((__LqArr*)(Arr))->Count; \
       for(; _a < _le; ){\
         if(*_a == (EmptyVal))\
              *_a = *(--_le); \
            else\
               _a++;\
         }\
        ((__LqArr*)(Arr))->Count = (((uintptr_t)_le) - (uintptr_t)&LqArrAt(Arr, TypeVal, 0)) / sizeof(TypeVal);\
        if((intptr_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))((__LqArr*)(Arr))->Count * LQEVNT_DECREASE_COEFFICIENT) < ((__LqArr*)(Arr))->AllocCount){\
            intptr_t _NewCount = lq_max(((__LqArr*)(Arr))->Count, ((intptr_t)1));\
            ((__LqArr*)(Arr))->Data = ___realloc(((__LqArr*)(Arr))->Data, _NewCount * sizeof(TypeVal));\
            ((__LqArr*)(Arr))->AllocCount = _NewCount;\
        }\
   }\
}
/////////////////

#define LqArr2Init(Arr) {\
    ((__LqArr2*)(Arr))->Data = NULL;\
    ((__LqArr2*)(Arr))->MinEmpty = 0;\
    ((__LqArr2*)(Arr))->MaxUsed = 0;\
    ((__LqArr2*)(Arr))->AllocCount = 0;\
    ((__LqArr2*)(Arr))->IsRemoved = false;\
}

#define LqArr2Uninit(Arr) {\
    if(((__LqArr2*)(Arr))->Data != NULL)\
        ___free(((__LqArr2*)(Arr))->Data);\
}

#define LqArr2PushBack(Arr, TypeVal, NewIndex, EmptyVal) {\
 intptr_t _n;\
 if(!((__LqArr2*)(Arr))->IsRemoved){\
     (NewIndex) = ((__LqArr2*)(Arr))->MinEmpty;\
     if(((__LqArr2*)(Arr))->MinEmpty > ((__LqArr2*)(Arr))->MaxUsed)\
        ((__LqArr2*)(Arr))->MaxUsed = ((__LqArr2*)(Arr))->MinEmpty;\
     register intptr_t _i = ((__LqArr2*)(Arr))->MinEmpty; _i++;\
     TypeVal* _a = ((TypeVal*)((__LqArr2*)(Arr))->Data);\
     for(register intptr_t _m = ((__LqArr2*)(Arr))->AllocCount; _i < _m; _i++)\
        if(_a[_i] == (EmptyVal))\
            break;\
     ((__LqArr2*)(Arr))->MinEmpty = _n = _i;\
 }else{ (NewIndex) = _n = ++(((__LqArr2*)(Arr))->MaxUsed); }\
 if(_n >= ((__LqArr2*)(Arr))->AllocCount){\
        intptr_t _NewSize = (intptr_t)(((decltype(LQEVNT_INCREASE_COEFFICIENT))((__LqArr2*)(Arr))->AllocCount) * LQEVNT_INCREASE_COEFFICIENT);\
		_NewSize++;\
        TypeVal* _NewArr = (TypeVal*)___realloc(((__LqArr2*)(Arr))->Data, sizeof(TypeVal) * _NewSize); \
        register intptr_t _i = ((__LqArr2*)(Arr))->AllocCount;\
        ((__LqArr2*)(Arr))->Data = _NewArr; \
        ((__LqArr2*)(Arr))->AllocCount = _NewSize; \
        for(; _i < _NewSize; _i++) \
            _NewArr[_i] = (EmptyVal);\
 }\
}

#define LqArr2At(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr2*)(Arr))->Data))[Index])

#define LqArr2RemoveAt(Arr, TypeVal, Index, EmptyVal) {\
    LqArr2At(Arr, TypeVal, (Index)) = (EmptyVal);\
    ((__LqArr2*)(Arr))->MinEmpty = lq_min(((__LqArr2*)(Arr))->MinEmpty, ((intptr_t)(Index)));\
    ((__LqArr2*)(Arr))->IsRemoved = true;\
}

#define LqArr2AlignAfterRemove(Arr, TypeVal, EmptyVal) {\
    if(((__LqArr2*)(Arr))->IsRemoved){\
        ((__LqArr2*)(Arr))->IsRemoved = false;\
        register TypeVal* _Conns = (TypeVal*)((__LqArr2*)(Arr))->Data;\
        register intptr_t _i = ((__LqArr2*)(Arr))->AllocCount - ((intptr_t)1);\
        for(; (_i >= ((intptr_t)0)) && (_Conns[_i] == (EmptyVal)); _i--);\
        ((__LqArr2*)(Arr))->MaxUsed = _i;\
        intptr_t _n = _i + ((intptr_t)2);\
        if(((intptr_t)(((decltype(LQEVNT_DECREASE_COEFFICIENT))_n) * LQEVNT_DECREASE_COEFFICIENT)) < ((__LqArr2*)(Arr))->AllocCount){\
            ((__LqArr2*)(Arr))->Data = ___realloc(((__LqArr2*)(Arr))->Data, _n * sizeof(TypeVal));\
            ((__LqArr2*)(Arr))->AllocCount = _n;\
        }\
    }\
}

//////////////////////

#define LqArr3Init(Arr){\
    ((__LqArr3*)(Arr))->Data = NULL;\
    ((__LqArr3*)(Arr))->Data2 = NULL;\
    ((__LqArr3*)(Arr))->Count = 0;\
    ((__LqArr3*)(Arr))->AllocCount = 0;\
    ((__LqArr3*)(Arr))->IsRemoved = false;\
}

#define LqArr3Uninit(Arr){\
    if(((__LqArr3*)(Arr))->Data != NULL)\
        ___free(((__LqArr3*)(Arr))->Data);\
    if(((__LqArr3*)(Arr))->Data2 != NULL)\
        ___free(((__LqArr3*)(Arr))->Data2);\
}

#define LqArr3PushBack(Arr, TypeVal1, TypeVal2) {\
 if(((__LqArr3*)(Arr))->Count >= ((__LqArr3*)(Arr))->AllocCount){\
     intptr_t _NewSize = (intptr_t)(((decltype(LQEVNT_INCREASE_COEFFICIENT))((__LqArr3*)(Arr))->Count) * LQEVNT_INCREASE_COEFFICIENT);\
	 _NewSize++;\
     ((__LqArr3*)(Arr))->Data = ___realloc(((__LqArr3*)(Arr))->Data, sizeof(TypeVal1) * _NewSize);\
     ((__LqArr3*)(Arr))->Data2 = ___realloc(((__LqArr3*)(Arr))->Data2, sizeof(TypeVal2) * _NewSize);\
     ((__LqArr3*)(Arr))->AllocCount = _NewSize;\
 }\
 ((__LqArr3*)(Arr))->Count++;\
}

#define LqArr3Back_1(Arr, TypeVal) (((TypeVal*)(((__LqArr3*)(Arr))->Data))[((__LqArr3*)(Arr))->Count - ((intptr_t)1)])
#define LqArr3Back_2(Arr, TypeVal) (((TypeVal*)(((__LqArr3*)(Arr))->Data2))[((__LqArr3*)(Arr))->Count - ((intptr_t)1)])

#define LqArr3At_1(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr3*)(Arr))->Data))[Index])
#define LqArr3At_2(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr3*)(Arr))->Data2))[Index])

#define LqArr3RemoveAt(Arr, TypeVal1, TypeVal2, Index, EmptyValForTypeVal2) {\
   LqArr3At_2(Arr, TypeVal2, Index) = (EmptyValForTypeVal2);\
   ((__LqArr3*)(Arr))->IsRemoved = true;\
}

#define LqArr3AlignAfterRemove(Arr, TypeVal1, TypeVal2, EmptyValForTypeVal2) {\
  if(((__LqArr3*)(Arr))->IsRemoved){\
   ((__LqArr3*)(Arr))->IsRemoved = false;\
   register TypeVal2* _a2 = &LqArr3At_2(Arr, TypeVal2, 0);\
   register TypeVal1* _a1 = &LqArr3At_1(Arr, TypeVal1, 0);\
   register intptr_t _Count = ((__LqArr3*)(Arr))->Count;\
   for(register intptr_t _i = ((intptr_t)0); _i < _Count;){\
        if(_a2[_i] == (EmptyValForTypeVal2)){\
            _a2[_i] = _a2[--_Count];\
            _a1[_i] = _a1[_Count];\
        } else{\
            _i++;\
        }\
    }\
    ((__LqArr3*)(Arr))->Count = _Count;\
    if(((intptr_t)(((decltype(LQEVNT_DECREASE_COEFFICIENT))_Count) * LQEVNT_DECREASE_COEFFICIENT)) < ((__LqArr3*)(Arr))->AllocCount){\
        intptr_t _NewCount = lq_max(((__LqArr3*)(Arr))->Count, ((intptr_t)1));\
        ((__LqArr3*)(Arr))->Data = ___realloc(((__LqArr3*)(Arr))->Data, _NewCount * sizeof(TypeVal1));\
        ((__LqArr3*)(Arr))->Data2 = ___realloc(((__LqArr3*)(Arr))->Data2, _NewCount * sizeof(TypeVal2));\
        ((__LqArr3*)(Arr))->AllocCount = _NewCount;\
    }\
  }\
}


static LqEvntFlag _LqEvntGetFlagForUpdate(void* EvntOrConn) {
    LqClientHdr* EvntHdr = (LqClientHdr*)EvntOrConn;
    LqEvntFlag ExpectedEvntFlag = LqClientGetFlags(EvntHdr), NewEvntFlag;
    do {
        NewEvntFlag = ExpectedEvntFlag & ~_LQEVNT_FLAG_SYNC;
    } while(!LqAtmCmpXchg(EvntHdr->Flag, ExpectedEvntFlag, NewEvntFlag));
    return ExpectedEvntFlag;
}


//////////////////////

#if defined(LQEVNT_WIN_EVENT)
# include "LqSysPollWin.hpp"
#elif defined(LQEVNT_KEVENT)
#err "Not implemented kevent"
#elif defined(LQEVNT_EPOLL)
# include "LqSysPollEpoll.hpp"
#elif defined(LQEVNT_POLL)
# include "LqSysPollPoll.hpp"
#endif
