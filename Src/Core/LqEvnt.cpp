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


#ifndef LQEVNT_INCREASE_COEFFICIENT
# define LQEVNT_INCREASE_COEFFICIENT 1.61f
#endif
#ifndef LQEVNT_DECREASE_COEFFICIENT
# define LQEVNT_DECREASE_COEFFICIENT 1.7f
#endif

#define LQ_EVNT

#if defined(LQEVNT_WIN_EVENT)
# include "LqEvntWin.h"
#elif defined(LQEVNT_KEVENT)
#err "Not implemented kevent"
#elif defined(LQEVNT_EPOLL)
# include "LqEvntEpoll.h"
#elif defined(LQEVNT_POLL)
# include "LqEvntPoll.h"
#endif

