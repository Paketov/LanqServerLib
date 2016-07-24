/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower
* This part of server support:
*	+Windows native events objects.
*	+linux epoll.
*	+kevent for FreeBSD like systems.(*But not yet implemented)
*	+poll for others unix systems.
*
*/

#ifndef LQ_EVNT
# error "Only use in LqEvnt.cpp !"
#endif


#include <poll.h>
#include <sys/eventfd.h>
#include <stdlib.h>
#include <string.h>

#define LqEvntSystemEventByConnEvents(Client)				\
	(((Client->Flag & LQCONN_FLAG_RD) ?	POLLIN : 0) |		\
	((Client->Flag & LQCONN_FLAG_WR) ?	POLLOUT : 0) |		\
	((Client->Flag & LQCONN_FLAG_HUP) ? (POLLHUP | POLLERR): 0))


bool LqEvntInit(LqEvnt* Dest)
{
	Dest->ClientArr = nullptr;
	Dest->EventArr = malloc(sizeof(pollfd));
	if(Dest->EventArr == nullptr)
		return false;
	Dest->ClientArr = (LqConn**)malloc(sizeof(LqConn*));
	if(Dest->ClientArr == nullptr)
		return false;
	Dest->ClientArr[0] = nullptr;
	if((((pollfd*)Dest->EventArr)[0].fd = eventfd(0, EFD_NONBLOCK)) == -1)
		return false;
	((pollfd*)Dest->EventArr)[0].events = POLLIN;
	((pollfd*)Dest->EventArr)[0].revents = 0;
	Dest->AllocCount = Dest->Count = 1;
	Dest->EventEnumIndex = 0;
	return true;
}

void LqEvntUninit(LqEvnt* Dest)
{
	if(Dest->EventArr != nullptr)
	{
		if(((pollfd*)Dest->EventArr)[0].fd != -1)
			close(((pollfd*)Dest->EventArr)[0].fd);
		free(Dest->EventArr);
	}
	if(Dest->ClientArr != nullptr)
		free(Dest->ClientArr);
}

bool LqEvntAddConnection(LqEvnt* Dest, LqConn* Client)
{
	if(Dest->Count >= Dest->AllocCount)
	{
		size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))Dest->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;
		auto NewEventArr = (pollfd*)realloc(Dest->EventArr, sizeof(pollfd) * NewSize);
		auto ClientArr = (LqConn**)realloc(Dest->ClientArr, sizeof(LqConn*) * NewSize);
		if(NewEventArr == nullptr) return false;
		Dest->EventArr = NewEventArr;
		if(ClientArr == nullptr) return false;
		Dest->ClientArr = ClientArr;
		Dest->AllocCount = NewSize;
	}
	((pollfd*)Dest->EventArr)[Dest->Count].fd = Client->SockDscr;
	((pollfd*)Dest->EventArr)[Dest->Count].events = LqConnIsLock(Client)? 0: LqEvntSystemEventByConnEvents(Client);
	((pollfd*)Dest->EventArr)[Dest->Count].revents = 0;
	Dest->ClientArr[Dest->Count] = Client;
	Dest->Count++;
	return true;
}

LqConnFlag LqEvntEnumEventBegin(LqEvnt* Events)
{
	Events->EventEnumIndex = 0; //Set start index
	return LqEvntEnumEventNext(Events);
}

LqConnFlag LqEvntEnumEventNext(LqEvnt* Events)
{
	for(int i = Events->EventEnumIndex + 1, m = Events->Count; i < m; i++)
	{
		auto e = ((pollfd*)Events->EventArr)[i].revents;
		if(e & (POLLIN | POLLOUT | POLLHUP | POLLERR))
		{
			Events->EventEnumIndex = i;
			LqConnFlag r = 0;
			
			if(e & POLLIN)
			{
                if((Events->ClientArr[i]->Flag & LQCONN_FLAG_RDHUP) && (LqConnCountPendingData(Events->ClientArr[i]) <= 0))
                    r |= LQCONN_FLAG_RDHUP;
				r |= LQCONN_FLAG_RD;
            }
			if(e & POLLOUT)
				r |= LQCONN_FLAG_WR;
			if(e & (POLLHUP | POLLERR))
				r |= LQCONN_FLAG_HUP;
			return r;
		}
	}
	return 0;
}

void LqEvntRemoveByEventInterator(LqEvnt* Events)
{
	Events->Count--;
	((pollfd*)Events->EventArr)[Events->EventEnumIndex] = ((pollfd*)Events->EventArr)[Events->Count];
	Events->ClientArr[Events->EventEnumIndex] = Events->ClientArr[Events->Count];
	if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
	{
        size_t NewCount = lq_max(Events->Count, 1);
		Events->EventArr = (pollfd*)realloc(Events->EventArr, NewCount * sizeof(pollfd));
		Events->ClientArr = (LqConn**)realloc(Events->ClientArr, NewCount * sizeof(LqConn*));
		Events->AllocCount = NewCount;
	}
	Events->EventEnumIndex--;
}

LqConn* LqEvntGetClientByEventInterator(LqEvnt* Events)
{
	return Events->ClientArr[Events->EventEnumIndex];
}

bool LqEvntSetMaskByEventInterator(LqEvnt* Events)
{
    auto c = Events->ClientArr[Events->EventEnumIndex];
	((pollfd*)Events->EventArr)[Events->EventEnumIndex].events = LqConnIsLock(c)? 0: LqEvntSystemEventByConnEvents(c);
	return true;
}

bool LqEvntUnlock(LqEvnt* Events, LqConn* Conn)
{
	for(size_t i = 1; i < Events->Count; i++)
		if(Events->ClientArr[i] == Conn)
		{
			((pollfd*)Events->EventArr)[i].events = LqEvntSystemEventByConnEvents(Conn);
			Events->ClientArr[i]->Flag &= ~((LqConnFlag)LQCONN_FLAG_LOCK);
			return true;
		}
	return false;
}

bool LqEvntEnumConnBegin(LqEvnt* Events, LqEvntConnInterator* Interator)
{
	return (Interator->Index = 1) < Events->Count;
}

bool LqEvntEnumConnNext(LqEvnt* Events, LqEvntConnInterator* Interator)
{
	return ++Interator->Index < Events->Count;
}

void LqEvntRemoveByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator)
{
	Events->Count--;
	((pollfd*)Events->EventArr)[Interator->Index] = ((pollfd*)Events->EventArr)[Events->Count];
	Events->ClientArr[Interator->Index] = Events->ClientArr[Events->Count];
	if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
	{
		Events->EventArr = (pollfd*)realloc(Events->EventArr, Events->Count * sizeof(pollfd));
		Events->ClientArr = (LqConn**)realloc(Events->ClientArr, Events->Count * sizeof(LqConn*));
		Events->AllocCount = Events->Count;
	}
	Interator->Index--;
}

LqConn* LqEvntGetClientByConnInterator(LqEvnt* Events, LqEvntConnInterator* Interator)
{
	return Events->ClientArr[Interator->Index];
}

bool LqEvntSignalCheckAndReset(LqEvnt* Events)
{
	if(Events->IsSignalSended)
	{
		Events->IsSignalSended = false;
		eventfd_t r[2];
		read(((pollfd*)Events->EventArr)[0].fd, &r, sizeof(r));
		return true;
	}
	return false;
}

void LqEvntSignalSet(LqEvnt* Events)
{
	Events->IsSignalSended = true;
	eventfd_t r = 1;
	write(((pollfd*)Events->EventArr)[0].fd, &r, sizeof(r));
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime)
{
	return poll((pollfd*)Events->EventArr, Events->Count, WaitTime);
}

size_t LqEvntCount(const LqEvnt* Events)
{
	return Events->Count - 1;
}

