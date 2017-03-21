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


typedef unsigned char           uchar;
typedef unsigned short          ushort;
typedef unsigned int            uint;
typedef unsigned long           ulong;
typedef long long               llong;
typedef unsigned long long      ullong;
typedef long long               LqFileSz;
typedef long long               LqTimeSec;
typedef long long               LqTimeMillisec;
typedef uint16_t                LqEvntFlag;


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
	(_Tbl)->Count = 0; (_Tbl)->MaxCount = 1; (_Tbl)->Tbl = (void**)LqMemMalloc(sizeof(void*)); (_Tbl)->Tbl[0] = NULL;\
	(_Tbl)->IncCoef = 1.61803398874989484820; (_Tbl)->DecCoef = (_Tbl)->IncCoef + 0.1;\
}

#define LqTblRowRealloc(_Tbl, ___NewCount, _IndexByElem, _TypeElem, _Next) {\
	size_t __NewCount = (size_t)(___NewCount);\
	void** __NewArr = (void**)LqMemMalloc(__NewCount * sizeof(void*));\
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
