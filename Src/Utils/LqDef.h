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
	(NewElem)->Next = NULL;\
	if(((LqListHdr*)(ListHdr))->Last != nullptr)\
		((TypeElem*)((LqListHdr*)(ListHdr))->Last)->Next = ((TypeElem*)(NewElem));\
	((LqListHdr*)(ListHdr))->Last = ((TypeElem*)(NewElem));\
	if(((LqListHdr*)(ListHdr))->First == NULL)\
		((LqListHdr*)(ListHdr))->First = ((TypeElem*)(NewElem));\
  }

#define LqListFirst(ListHdr, TypeElem) ((TypeElem*)((LqListHdr*)(ListHdr))->First)

#define LqListRemove(ListHdr, TypeElem) {\
	if(((LqListHdr*)(ListHdr))->First != NULL) {\
		if(((LqListHdr*)(ListHdr))->First == ((LqListHdr*)(ListHdr))->Last)\
			((LqListHdr*)(ListHdr))->Last = NULL;\
		((LqListHdr*)(ListHdr))->First = ((TypeElem*)(((LqListHdr*)(ListHdr))->First))->Next;\
	}\
  }

#endif
