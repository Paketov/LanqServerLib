/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower.
* This part of server support:
*	+Windows native events objects.
*	+linux epoll.
*	+kevent for FreeBSD like systems.(*But not yet implemented)
*	+poll for others unix systems.
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

struct LqEvntConnInterator
{
#if defined(LQEVNT_KEVENT)
#else
	int Index;
#endif
};


struct LqEvnt
{
	bool IsSignalSended;
#if defined(LQEVNT_WIN_EVENT)
	int			Count;
	int			AllocCount;
	HANDLE*		EventArr;
	LqConn**	ClientArr;
	int			EventEnumIndex;
#elif defined(LQEVNT_KEVENT)
#elif defined(LQEVNT_EPOLL)
    int			EpollFd;
    int			SignalFd;
    LqConn**	ClientArr;
    int			Count;
    int			AllocCount;
    void*		EventArr;
    int			EventArrCount;
	int			CountReady;
    int			EventEnumIndex;
#elif defined(LQEVNT_POLL)
	void*		EventArr;
	LqConn**	ClientArr;
    int			Count;
    int			AllocCount;
	int			EventEnumIndex;
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
bool LqEvntAddConnection(LqEvnt* Dest, LqConn* Client);

/*
* Start enumerate events by internal interator.
*  @return - 0 - is not have event, otherwise flag LQCONN_FLAG_RD, LQCONN_FLAG_WR, LQCONN_FLAG_HUP
*/
LqConnFlag LqEvntEnumEventBegin(LqEvnt* Events);

/*
* Enumerate next event by internal interator.
*  @return - 0 - is not have event, otherwise flag LQCONN_FLAG_RD, LQCONN_FLAG_WR, LQCONN_FLAG_HUP
*/
LqConnFlag LqEvntEnumEventNext(LqEvnt* Events);

/*
* Remove connection by internal event interator.
*/
void LqEvntRemoveByEventInterator(LqEvnt* Events);

/*
* Get client struct by internal interator.
*  @return - ptr on event connection.
*/
LqConn* LqEvntGetClientByEventInterator(LqEvnt* Events);


#if defined(LQEVNT_WIN_EVENT)
/*
* Use only in Windows
*  @return - true is success
*/
void LqEvntUnuseClientByEventInterator(LqEvnt* Events);
#else
#define LqEvntUnuseClientByEventInterator(Events) ((void)0)

#endif

/*
* Set event mask for connection by internal interator.
*  @return - true is success
*/
bool LqEvntSetMaskByEventInterator(LqEvnt* Events);

/*
* Unlock connection. After call events for @Conn recived again
*  @return - true - is locket, otherwise - false.
*/
bool LqEvntUnlock(LqEvnt* Events, LqConn* Conn);

/*
* Start enumerate connections.
*  @Interator - Index
*  @return - true - is have element
*/
bool LqEvntEnumConnBegin(LqEvnt* Events, LqEvntConnInterator* Interator);

/*
* Enumerate next connection.
*  @Interator - Index
*  @return - true - is have next element
*/
bool LqEvntEnumConnNext(LqEvnt* Events, LqEvntConnInterator* Interator);

/*
* Start enumerate by interator.
*  @Interator - Index
*  @return - true - is have next element
*/
void LqEvntRemoveByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator);
LqConn* LqEvntGetClientByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator);


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
