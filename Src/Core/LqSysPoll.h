/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqSysPoll... - Multiplatform abstracted event follower.
* This part of server support:
*       +Windows WSAAsyncSelect (Creates window in each worker thread).
*       +linux epoll.
*       +kevent for BSD like systems.(*But not yet implemented)
*       +poll for others unix systems.
*
*/

#ifndef __LQ_EVNT_H_HAS_INCLUDED__
#define __LQ_EVNT_H_HAS_INCLUDED__

#include "LqConn.h"
#include "Lanq.h"
#include "LqOs.h"
#include "LqDef.h"


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

typedef struct LqEvntInterator {
#if defined(LQEVNT_KEVENT)
#else
    intptr_t Index;
#endif
#if defined(LQEVNT_WIN_EVENT) || defined(LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL)
    bool  IsEnumConn;
#endif
} LqEvntInterator;


typedef struct __LqArr {
    void*    Data;
    intptr_t Count;
    intptr_t AllocCount;
    bool     IsRemoved;
} __LqArr;

typedef struct __LqArr2 {
    void*    Data;
    intptr_t AllocCount;
    intptr_t MinEmpty;
    intptr_t MaxUsed;
    bool     IsRemoved;
} __LqArr2;

typedef struct __LqArr3 {
    void*    Data;
    void*    Data2;
    intptr_t Count;
    intptr_t AllocCount;
    bool     IsRemoved;
} __LqArr3;

typedef struct LqSysPoll {
    intptr_t            CommonCount;
#if defined(LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL)
    __LqArr2            IocpArr;
    __LqArr3            EvntArr;

	void*               IocpHandle;
	void*               AfdAsyncHandle;

	intptr_t            EventObjectIndex;
	intptr_t            ConnIndex;

	uintptr_t           IsHaveOnlyHup;
	intptr_t			EnumCalledCount;

	void*               PollBlock;

	            
#elif defined(LQEVNT_WIN_EVENT)
    __LqArr2            ConnArr;
    __LqArr3            EvntArr;

    uintptr_t           WinHandle;

    intptr_t            EventObjectIndex;
    intptr_t            ConnIndex;

    uintptr_t           IsHaveOnlyHup;
#elif defined(LQEVNT_KEVENT)
#elif defined(LQEVNT_EPOLL)
    __LqArr2            ClientArr;
    __LqArr             EventArr;
    intptr_t            CountReady;
    intptr_t            EventEnumIndex;
    int                 EpollFd;
#elif defined(LQEVNT_POLL)
    __LqArr3            EvntFdArr;
    int                 EventEnumIndex;
#endif
} LqSysPoll;

#pragma pack(pop)
#if defined(LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL)
#define __LqSysPollIsRestruct(EventFollower)  ((EventFollower)->IocpArr.IsRemoved || (EventFollower)->EvntArr.IsRemoved)
#elif defined(LQEVNT_WIN_EVENT)
#define __LqSysPollIsRestruct(EventFollower)  ((EventFollower)->ConnArr.IsRemoved || (EventFollower)->EvntArr.IsRemoved)
#elif defined(LQEVNT_KEVENT)

#elif defined(LQEVNT_EPOLL)
#define __LqSysPollIsRestruct(EventFollower)  ((EventFollower)->ClientArr.IsRemoved)
#elif defined(LQEVNT_POLL)
#define __LqSysPollIsRestruct(EventFollower)  ((EventFollower)->EvntFdArr.IsRemoved)
#endif




#define lqsyspoll_enum_changes_do(EventFollower, EventFlags) {for(LqEvntFlag EventFlags = __LqSysPollEnumEventBegin(&(EventFollower)); EventFlags != ((LqEvntFlag)0); EventFlags = __LqEvntEnumEventNext(&(EventFollower)))
#define lqsyspoll_enum_changes_while(EventFollower)  if(__LqSysPollIsRestruct(&(EventFollower))) __LqSysPollRestructAfterRemoves(&(EventFollower));}

#define lqsyspoll_enum_do(EventFollower, IndexName) {LqEvntInterator IndexName; for(auto __r = __LqSysPollEnumBegin(&EventFollower, &IndexName); __r; __r = __LqSysPollEnumNext(&EventFollower, &IndexName))
#define lqsyspoll_enum_while(EventFollower)  if(__LqSysPollIsRestruct(&(EventFollower))) __LqSysPollRestructAfterRemoves(&(EventFollower));}

/*
* Init LqSysPoll struct.
*/
bool LqSysPollInit(LqSysPoll* Dest);

#if defined(LQEVNT_WIN_EVENT) && !defined(LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL)
bool LqSysPollThreadInit(LqSysPoll* Dest);

void LqSysPollThreadUninit(LqSysPoll* Dest);
#else
#define LqSysPollThreadInit(Dest) (true)

#define LqSysPollThreadUninit(Dest) ((void)0)
#endif

/*
* Uninit LqSysPoll struct.
*/
void LqSysPollUninit(LqSysPoll* Dest);

/*
* Add connection in event follower list.
*/
bool LqSysPollAddHdr(LqSysPoll* Dest, LqClientHdr* Client);

/*
* Start enumerate events by internal interator.
*  @return - 0 - is not have event, otherwise flag LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP
*/
LqEvntFlag __LqSysPollEnumEventBegin(LqSysPoll* Events);

/*
* Enumerate next event by internal interator.
*  @return - 0 - is not have event, otherwise flag LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP
*/
LqEvntFlag __LqEvntEnumEventNext(LqSysPoll* Events);

/*
* Remove connection by internal event interator.
*/
void LqSysPollRemoveCurrent(LqSysPoll* Events);

void __LqSysPollRestructAfterRemoves(LqSysPoll* Events);

/*
* Get client struct by internal interator.
*  @return - ptr on event connection.
*/
LqClientHdr* LqSysPollGetHdrByCurrent(LqSysPoll* Events);


#if defined(LQEVNT_WIN_EVENT) || defined(LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL)
/*
* Use only in Windows
*  Call before enum next coonnection
*/
void LqSysPollUnuseCurrent(LqSysPoll* Events);
#else
#define LqSysPollUnuseCurrent(Events) ((void)0)
#endif

/*
* Set event mask for connection by internal interator.
*  @return - true is success
*/
bool LqSysPollSetMaskByCurrent(LqSysPoll* Events);

/*
* Set new mask for onnection or object, by header
*  @return - true - is mask setted, otherwise - false.
*/
int LqSysPollUpdateAllMask(LqSysPoll* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*));

/*
* Start enumerate connections.
*  @Interator - Index
*  @return - true - is have element
*/
bool __LqSysPollEnumBegin(LqSysPoll* Events, LqEvntInterator* Interator);

/*
* Enumerate next connection.
*  @Interator - Index
*  @return - true - is have next element
*/
bool __LqSysPollEnumNext(LqSysPoll* Events, LqEvntInterator* Interator);

/*
* Start enumerate by interator.
*  @Interator - Index
*  @return - true - is have next element
*/
LqClientHdr* LqSysPollRemoveByInterator(LqSysPoll* Events, LqEvntInterator* Interator);
LqClientHdr* LqSysPollGetHdrByInterator(LqSysPoll* Events, LqEvntInterator* Interator);


/*
* Routine wait until cauth one or more events.
*  @WaitTime - Wait time in millisec.
*  @return - -1 - catch error, 0 - timeout, > 0 caught events.
*/
int LqSysPollCheck(LqSysPoll* Events, LqTimeMillisec WaitTime);

/*
* Routine return count connections.
*  @return - count connections in event poll.
*/
size_t LqSysPollCount(const LqSysPoll* Events);


#endif
