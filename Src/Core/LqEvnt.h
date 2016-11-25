/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event follower.
* This part of server support:
*       +Windows native events objects.
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

typedef struct LqEvntInterator
{
#if defined(LQEVNT_KEVENT)
#else
    intptr_t Index;
#endif
#ifdef LQEVNT_WIN_EVENT
    bool  IsEnumConn;
#endif
} LqEvntInterator;


typedef struct __LqArr
{
    void* Data;
    intptr_t Count;
    intptr_t AllocCount;
    bool     IsRemoved;
} __LqArr;

typedef struct __LqArr2
{
    void* Data;
    intptr_t AllocCount;
    intptr_t MinEmpty;
    intptr_t MaxUsed;
    bool     IsRemoved;
} __LqArr2;

typedef struct __LqArr3
{
    void* Data;
    void* Data2;
    intptr_t Count;
    intptr_t AllocCount;
    bool     IsRemoved;
} __LqArr3;

typedef struct LqEvnt
{   
    intptr_t            DeepLoop;
    intptr_t            CommonCount;
#if defined(LQEVNT_WIN_EVENT)
    __LqArr2            ConnArr;
    __LqArr3            EvntFdArr;

    uintptr_t           WinHandle;

    intptr_t            EventObjectIndex;
    intptr_t            ConnIndex;
     
#elif defined(LQEVNT_KEVENT)
#elif defined(LQEVNT_EPOLL)
    __LqArr             ClientArr;
    __LqArr             EventArr;
    intptr_t            CountReady;
    intptr_t            EventEnumIndex;
    int                 EpollFd;
#elif defined(LQEVNT_POLL)
    __LqArr3            EvntFdArr;
    int                 EventEnumIndex;
#endif
} LqEvnt;

#pragma pack(pop)

#define lqevnt_enum_changes_do(EventFollower, EventFlags) for(LqEvntFlag EventFlags = __LqEvntEnumEventBegin(&(EventFollower)); EventFlags != 0; EventFlags = __LqEvntEnumEventNext(&(EventFollower)))
#define lqevnt_enum_changes_while(EventFollower)  __LqEvntRestructAfterRemoves(&(EventFollower))

#define lqevnt_enum_do(EventFollower, IndexName) {LqEvntInterator IndexName; for(auto __r = __LqEvntEnumBegin(&EventFollower, &IndexName); __r; __r = __LqEvntEnumNext(&EventFollower, &IndexName))
#define lqevnt_enum_while(EventFollower)  __LqEvntRestructAfterRemoves(&(EventFollower));}

/*
* Init LqEvnt struct.
*/
bool LqEvntInit(LqEvnt* Dest);

#if defined(LQEVNT_WIN_EVENT)
bool LqEvntThreadInit(LqEvnt* Dest);

void LqEvntThreadUninit(LqEvnt* Dest);
#else
#define LqEvntThreadInit(Dest) (true)

#define LqEvntThreadUninit(Dest) ((void)0)
#endif

/*
* Uninit LqEvnt struct.
*/
void LqEvntUninit(LqEvnt* Dest);

/*
* Add connection in event follower list.
*/
bool LqEvntAddHdr(LqEvnt* Dest, LqEvntHdr* Client);

/*
* Start enumerate events by internal interator.
*  @return - 0 - is not have event, otherwise flag LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP
*/
LqEvntFlag __LqEvntEnumEventBegin(LqEvnt* Events);

/*
* Enumerate next event by internal interator.
*  @return - 0 - is not have event, otherwise flag LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP
*/
LqEvntFlag __LqEvntEnumEventNext(LqEvnt* Events);

/*
* Remove connection by internal event interator.
*/
void LqEvntRemoveCurrent(LqEvnt* Events);

void __LqEvntRestructAfterRemoves(LqEvnt* Events);

/*
* Get client struct by internal interator.
*  @return - ptr on event connection.
*/
LqEvntHdr* LqEvntGetHdrByCurrent(LqEvnt* Events);


#if defined(LQEVNT_WIN_EVENT)
/*
* Use only in Windows
*  Call before enum next coonnection
*/
void LqEvntUnuseCurrent(LqEvnt* Events);
#else
#define LqEvntUnuseCurrent(Events) ((void)0)
#endif

/*
* Set event mask for connection by internal interator.
*  @return - true is success
*/
bool LqEvntSetMaskByCurrent(LqEvnt* Events);

/*
* Set new mask for onnection or object, by header
*  @return - true - is mask setted, otherwise - false.
*/
int LqEvntUpdateAllMask(LqEvnt* Events, void* UserData, void(*DelProc)(void*, LqEvntInterator*), bool IsRestruct);

/*
* Start enumerate connections.
*  @Interator - Index
*  @return - true - is have element
*/
bool __LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator);

/*
* Enumerate next connection.
*  @Interator - Index
*  @return - true - is have next element
*/
bool __LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator);

/*
* Start enumerate by interator.
*  @Interator - Index
*  @return - true - is have next element
*/
LqEvntHdr* LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator);
LqEvntHdr* LqEvntGetHdrByInterator(LqEvnt* Events, LqEvntInterator* Interator);


/*
* Routine wait until cauth one or more events.
*  @WaitTime - Wait time in millisec.
*  @return - -1 - catch error, 0 - timeout, > 0 caught events.
*/
int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime);

/*
* Routine return count connections.
*  @return - count connections in event poll.
*/
size_t LqEvntCount(const LqEvnt* Events);


#endif
