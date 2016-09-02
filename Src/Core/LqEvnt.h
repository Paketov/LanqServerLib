/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower.
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

#include <stdint.h>

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqEvntInterator
{
#if defined(LQEVNT_KEVENT)
#else
    int Index;
#endif
};


struct LqEvnt
{
#if defined(LQEVNT_WIN_EVENT)
    int                 Count;
    int                 AllocCount;
    HANDLE*             EventArr;
    LqEvntHdr**         ClientArr;
    int                 EventEnumIndex;
    int                 SignalFd;
#elif defined(LQEVNT_KEVENT)
#elif defined(LQEVNT_EPOLL)
    int                 EpollFd;
    int                 SignalFd;
    LqEvntHdr**         ClientArr;
    int                 Count;
    int                 AllocCount;
    void*               EventArr;
    int                 EventArrCount;
    int                 CountReady;
    int                 EventEnumIndex;
    int                 FirstEnd;
#elif defined(LQEVNT_POLL)
    void*               EventArr;
    LqEvntHdr**         ClientArr;
    int                 Count;
    int                 AllocCount;
    int                 EventEnumIndex;
    int                 SignalFd;
#endif
};

#pragma pack(pop)

/*
* Init LqEvnt struct.
*/
bool LqEvntInit(LqEvnt* Dest);

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
LqEvntFlag LqEvntEnumEventBegin(LqEvnt* Events);

/*
* Enumerate next event by internal interator.
*  @return - 0 - is not have event, otherwise flag LQEVNT_FLAG_RD, LQEVNT_FLAG_WR, LQEVNT_FLAG_HUP
*/
LqEvntFlag LqEvntEnumEventNext(LqEvnt* Events);

/*
* Remove connection by internal event interator.
*/
void LqEvntRemoveCurrent(LqEvnt* Events);

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
* Unlock connection. After call events for @Conn recived again
*  @return - true - is locket, otherwise - false.
*/
bool LqEvntSetMaskByHdr(LqEvnt* Events, LqEvntHdr* Hdr);

/*
* Start enumerate connections.
*  @Interator - Index
*  @return - true - is have element
*/
bool LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator);

/*
* Enumerate next connection.
*  @Interator - Index
*  @return - true - is have next element
*/
bool LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator);

/*
* Start enumerate by interator.
*  @Interator - Index
*  @return - true - is have next element
*/
void LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator);
LqEvntHdr* LqEvntGetHdrByInterator(LqEvnt* Events, LqEvntInterator* Interator);


/*
* Check is waiting has been interrupted.
*  @return - true - is has been called LqEvntSignalSet
*/
bool LqEvntSignalCheckAndReset(LqEvnt* Events);

/*
* Interrupt waiting events.
*/
void LqEvntSignalSet(LqEvnt* Events);

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
