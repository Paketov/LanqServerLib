/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower
* This part of server support:
*       +Windows native events objects.
*       +linux epoll.
*       +kevent for FreeBSD like systems.(*But not yet implemented)
*       +poll for others unix systems.
*
*/

#include "LqEvnt.h"
#include "LqLog.h"
#include "LqConn.h"

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
}

#define LqArrUninit(Arr){\
    if(((__LqArr*)(Arr))->Data != NULL)\
        ___free(((__LqArr*)(Arr))->Data);\
}

#define LqArrPushBack(Arr, TypeVal) {\
 if(((__LqArr*)(Arr))->Count >= ((__LqArr*)(Arr))->AllocCount){\
     size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))((__LqArr*)(Arr))->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;\
     ((__LqArr*)(Arr))->Data = ___realloc(((__LqArr*)(Arr))->Data, sizeof(TypeVal) * NewSize);\
     ((__LqArr*)(Arr))->AllocCount = NewSize;\
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

#define LqArrAlignAfterRemove(Arr, TypeVal) {\
    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))((__LqArr*)(Arr))->Count * LQEVNT_DECREASE_COEFFICIENT) < ((__LqArr*)(Arr))->AllocCount){\
        size_t NewCount = lq_max(((__LqArr*)(Arr))->Count, 1);\
        ((__LqArr*)(Arr))->Data = ___realloc(((__LqArr*)(Arr))->Data, NewCount * sizeof(TypeVal));\
        ((__LqArr*)(Arr))->AllocCount = NewCount;\
    }\
}
/////////////////

#define LqArr2Init(Arr) {\
    ((__LqArr2*)(Arr))->Data = NULL;\
    ((__LqArr2*)(Arr))->MinEmpty = 0;\
    ((__LqArr2*)(Arr))->AllocCount = 0;\
}

#define LqArr2Uninit(Arr) {\
    if(((__LqArr2*)(Arr))->Data != NULL)\
        ___free(((__LqArr2*)(Arr))->Data);\
}

#define LqArr2PushBack(Arr, TypeVal, NewIndex, EmptyVal) \
{\
 (NewIndex) = ((__LqArr2*)(Arr))->MinEmpty;\
 auto i = ((__LqArr2*)(Arr))->MinEmpty + 1;\
 auto a = ((TypeVal*)((__LqArr2*)(Arr))->Data);\
 for(auto m = ((__LqArr2*)(Arr))->AllocCount; i < m; i++)\
    if(a[i] == (EmptyVal))\
        break;\
 ((__LqArr2*)(Arr))->MinEmpty = i;\
 if(((__LqArr2*)(Arr))->MinEmpty >= ((__LqArr2*)(Arr))->AllocCount){\
        size_t NewSize = (size_t)(((decltype(LQEVNT_INCREASE_COEFFICIENT))((__LqArr2*)(Arr))->AllocCount) * LQEVNT_INCREASE_COEFFICIENT) + 1; \
        auto NewArr = (TypeVal*)___realloc(((__LqArr2*)(Arr))->Data, sizeof(TypeVal) * NewSize); \
        i = ((__LqArr2*)(Arr))->AllocCount;\
        ((__LqArr2*)(Arr))->Data = NewArr; \
        ((__LqArr2*)(Arr))->AllocCount = NewSize; \
        for(; i < NewSize; i++) \
			NewArr[i] = (EmptyVal);\
 }\
}

#define LqArr2At(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr2*)(Arr))->Data))[Index])

#define LqArr2RemoveAt(Arr, TypeVal, Index, EmptyVal) \
{\
	LqArr2At(Arr, TypeVal, (Index)) = (EmptyVal);\
	((__LqArr2*)(Arr))->MinEmpty = lq_min(((__LqArr2*)(Arr))->MinEmpty, (Index));\
}

#define LqArr2AlignAfterRemove(Arr, TypeVal, EmptyVal) \
{\
    auto Conns = (TypeVal*)((__LqArr2*)(Arr))->Data;\
    register intptr_t i = ((__LqArr2*)(Arr))->AllocCount - 1;\
    for(; (i >= 0) && (Conns[i] == (EmptyVal)); i--);\
    auto n = i + 2;\
    if((size_t)(((decltype(LQEVNT_INCREASE_COEFFICIENT))n) * LQEVNT_DECREASE_COEFFICIENT) < ((__LqArr2*)(Arr))->AllocCount){\
        ((__LqArr2*)(Arr))->Data = ___realloc(((__LqArr2*)(Arr))->Data, n * sizeof(TypeVal));\
        ((__LqArr2*)(Arr))->AllocCount = n;\
    }\
}

//////////////////////

#define LqArr3Init(Arr){\
    ((__LqArr3*)(Arr))->Data = NULL;\
    ((__LqArr3*)(Arr))->Data2 = NULL;\
    ((__LqArr3*)(Arr))->Count = 0;\
    ((__LqArr3*)(Arr))->AllocCount = 0;\
}

#define LqArr3Uninit(Arr){\
    if(((__LqArr3*)(Arr))->Data != NULL)\
        ___free(((__LqArr3*)(Arr))->Data);\
    if(((__LqArr3*)(Arr))->Data2 != NULL)\
        ___free(((__LqArr3*)(Arr))->Data2);\
}

#define LqArr3PushBack(Arr, TypeVal1, TypeVal2) {\
 if(((__LqArr3*)(Arr))->Count >= ((__LqArr3*)(Arr))->AllocCount){\
     size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))((__LqArr3*)(Arr))->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;\
     ((__LqArr3*)(Arr))->Data = ___realloc(((__LqArr3*)(Arr))->Data, sizeof(TypeVal1) * NewSize);\
     ((__LqArr3*)(Arr))->Data2 = ___realloc(((__LqArr3*)(Arr))->Data2, sizeof(TypeVal2) * NewSize);\
     ((__LqArr3*)(Arr))->AllocCount = NewSize;\
 }\
 ((__LqArr3*)(Arr))->Count++;\
}
#define LqArr3Back_1(Arr, TypeVal) (((TypeVal*)(((__LqArr3*)(Arr))->Data))[((__LqArr3*)(Arr))->Count - 1])
#define LqArr3Back_2(Arr, TypeVal) (((TypeVal*)(((__LqArr3*)(Arr))->Data2))[((__LqArr3*)(Arr))->Count - 1])

#define LqArr3At_1(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr3*)(Arr))->Data))[Index])
#define LqArr3At_2(Arr, TypeVal, Index) (((TypeVal*)(((__LqArr3*)(Arr))->Data2))[Index])

#define LqArr3AlignAfterRemove(Arr, TypeVal1, TypeVal2) {\
    if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))((__LqArr3*)(Arr))->Count * LQEVNT_DECREASE_COEFFICIENT) < ((__LqArr3*)(Arr))->AllocCount){\
        size_t NewCount = lq_max(((__LqArr3*)(Arr))->Count, 1);\
        ((__LqArr3*)(Arr))->Data = ___realloc(((__LqArr3*)(Arr))->Data, NewCount * sizeof(TypeVal1));\
        ((__LqArr3*)(Arr))->Data2 = ___realloc(((__LqArr3*)(Arr))->Data2, NewCount * sizeof(TypeVal2));\
        ((__LqArr3*)(Arr))->AllocCount = NewCount;\
    }\
}

//////////////////////

#if defined(LQEVNT_WIN_EVENT)
# include "LqEvntWin.hpp"
#elif defined(LQEVNT_KEVENT)
#err "Not implemented kevent"
#elif defined(LQEVNT_EPOLL)
# include "LqEvntEpoll.hpp"
#elif defined(LQEVNT_POLL)
# include "LqEvntPoll.hpp"
#endif
