/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqEvnt... - Multiplatform abstracted event folower
* This part of server support:
*   +Windows native events objects.
*   +linux epoll.
*   +kevent for BSD like systems.(*But not yet implemented)
*   +poll for others unix systems.
*
*/

#ifndef LQ_EVNT
# error "Only use in LqEvnt.cpp !"
#endif


#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include "LqFile.h"

#define LqEvntSystemEventByConnEvents(Client)               \
    (((Client->Flag & LQEVNT_FLAG_RD) ? POLLIN : 0) |       \
    ((Client->Flag & LQEVNT_FLAG_WR) ?  POLLOUT : 0) |      \
    ((Client->Flag & LQEVNT_FLAG_HUP) ? POLLHUP: 0) |       \
    ((Client->Flag & LQEVNT_FLAG_ERR) ? POLLERR : 0))


bool LqEvntInit(LqEvnt* Dest)
{
    Dest->ClientArr = nullptr;
    Dest->EventArr = malloc(sizeof(pollfd));
    if(Dest->EventArr == nullptr)
        return false;
    Dest->ClientArr = (LqEvntHdr**)malloc(sizeof(LqEvntHdr*));
    if(Dest->ClientArr == nullptr)
        return false;
    Dest->ClientArr[0] = nullptr;
    if((((pollfd*)Dest->EventArr)[0].fd = LqFileEventCreate(LQ_O_NOINHERIT)) == -1)
    {
        free(Dest->ClientArr);
        return false;
    }
    Dest->SignalFd = ((pollfd*)Dest->EventArr)[0].fd;
    ((pollfd*)Dest->EventArr)[0].events = POLLIN;
    ((pollfd*)Dest->EventArr)[0].revents = 0;
    Dest->AllocCount = Dest->Count = 1;
    Dest->EventEnumIndex = 0;
	Dest->DeepLoop = 0;
	Dest->IsRemoved = 0;
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

bool LqEvntAddHdr(LqEvnt* Dest, LqEvntHdr* Client)
{
    if(Dest->Count >= Dest->AllocCount)
    {
        size_t NewSize = (size_t)((decltype(LQEVNT_INCREASE_COEFFICIENT))Dest->Count * LQEVNT_INCREASE_COEFFICIENT) + 1;
        auto NewEventArr = (pollfd*)realloc(Dest->EventArr, sizeof(pollfd) * NewSize);
        auto ClientArr = (LqEvntHdr**)realloc(Dest->ClientArr, sizeof(LqEvntHdr*) * NewSize);
        if(NewEventArr == nullptr)
            return false;
        Dest->EventArr = NewEventArr;
        if(ClientArr == nullptr)
            return false;
        Dest->ClientArr = ClientArr;
        Dest->AllocCount = NewSize;
    }
    ((pollfd*)Dest->EventArr)[Dest->Count].fd = Client->Fd;
    ((pollfd*)Dest->EventArr)[Dest->Count].events = LqEvntSystemEventByConnEvents(Client);
    ((pollfd*)Dest->EventArr)[Dest->Count].revents = 0;
    Client->Flag &= ~_LQEVNT_FLAG_SYNC;
    Dest->ClientArr[Dest->Count] = Client;
    Dest->Count++;
    return true;
}

LqEvntFlag LqEvntEnumEventBegin(LqEvnt* Events)
{
    Events->EventEnumIndex = 0; //Set start index
	Events->DeepLoop++;
    return LqEvntEnumEventNext(Events);
}

LqEvntFlag LqEvntEnumEventNext(LqEvnt* Events)
{
    for(register int i = Events->EventEnumIndex + 1, m = Events->Count; i < m; i++)
    {
		if(Events->ClientArr[i] == nullptr)
			continue;
        auto e = ((pollfd*)Events->EventArr)[i].revents;
        if((e & (POLLIN | POLLOUT | POLLHUP | POLLERR)) || (Events->ClientArr[i]->Flag & LQEVNT_FLAG_END))
        {
            Events->EventEnumIndex = i;
            LqEvntFlag r = 0;
            if(e & POLLIN)
            {
                int res = -1;
                if(
                    (Events->ClientArr[i]->Flag & LQEVNT_FLAG_RDHUP) &&
                    (ioctl(Events->ClientArr[i]->Fd, FIONREAD, &res) >= 0) &&
                    (res <= 0)
                    )
                    r |= LQEVNT_FLAG_RDHUP;
                r |= LQEVNT_FLAG_RD;
            }
            if(e & POLLOUT)
                r |= LQEVNT_FLAG_WR;
            if(e & POLLHUP)
                r |= LQEVNT_FLAG_HUP;
            if(e & POLLERR)
                r |= LQEVNT_FLAG_ERR;
            if(Events->ClientArr[i]->Flag & LQEVNT_FLAG_END)
                r |= LQEVNT_FLAG_END;
            return r;
        }
    }
    return 0;
}

void LqEvntRemoveCurrent(LqEvnt* Events)
{
    Events->ClientArr[Events->EventEnumIndex]->Flag &= ~_LQEVNT_FLAG_SYNC;
	Events->ClientArr[Events->EventEnumIndex] = nullptr;
	Events->IsRemoved = 1;
}

void LqEvntRestructAfterRemoves(LqEvnt* Events)
{
	Events->DeepLoop--;
	if((Events->DeepLoop > 0) || (Events->IsRemoved == 0))
		return;
	Events->IsRemoved = 0;

	register auto AllClients = Events->ClientArr;
	register auto AllEvents = (pollfd*)Events->EventArr;
	for(register size_t i = 1; i < Events->Count;)
	{
		if(AllClients[i] == nullptr)
		{
			Events->Count--;
			AllClients[i] = AllClients[Events->Count];
			AllEvents[i] = AllEvents[Events->Count];
		} else
		{
			i++
		}
	}
	if((size_t)((decltype(LQEVNT_DECREASE_COEFFICIENT))Events->Count * LQEVNT_DECREASE_COEFFICIENT) < Events->AllocCount)
	{
		size_t NewCount = lq_max(Events->Count, 1);
		Events->EventArr = (pollfd*)realloc(Events->EventArr, NewCount * sizeof(pollfd));
		Events->ClientArr = (LqEvntHdr**)realloc(Events->ClientArr, NewCount * sizeof(LqEvntHdr*));
		Events->AllocCount = NewCount;
	}
}

LqEvntHdr* LqEvntGetHdrByCurrent(LqEvnt* Events)
{
    return Events->ClientArr[Events->EventEnumIndex];
}

bool LqEvntSetMaskByCurrent(LqEvnt* Events)
{
    auto c = Events->ClientArr[Events->EventEnumIndex];
    if(c->Flag & LQEVNT_FLAG_END)
        LqEvntSignalSet(Events);
    ((pollfd*)Events->EventArr)[Events->EventEnumIndex].events = LqEvntSystemEventByConnEvents(c);
    c->Flag &= ~_LQEVNT_FLAG_SYNC;
    return true;
}

bool LqEvntSetMaskByHdr(LqEvnt* Events, LqEvntHdr* Conn)
{
    for(size_t i = 1; i < Events->Count; i++)
        if(Events->ClientArr[i] == Conn)
        {
            if(Conn->Flag & LQEVNT_FLAG_END)
                LqEvntSignalSet(Events);

            ((pollfd*)Events->EventArr)[i].events = LqEvntSystemEventByConnEvents(Conn);
            Conn->Flag &= ~_LQEVNT_FLAG_SYNC;
            return true;
        }
    return false;
}

bool LqEvntEnumBegin(LqEvnt* Events, LqEvntInterator* Interator)
{
	Events->DeepLoop++;
	Interator->Index = 0;
	return LqEvntEnumNext(Events, Interator);
}

bool LqEvntEnumNext(LqEvnt* Events, LqEvntInterator* Interator)
{
	for(register int Index = Interator->Index + 1; Index < Events->Count; Index++)
	{
		if(Events->ClientArr[Index] != nullptr)
		{
			Interator->Index = Index;
			return true;
		}
	}
	return false;
}


void LqEvntRemoveByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
	Events->ClientArr[Interator->Index]->Flag &= ~_LQEVNT_FLAG_SYNC;
	Events->ClientArr[Interator->Index] = nullptr;
	Events->IsRemoved = 1;
}

LqEvntHdr* LqEvntGetHdrByInterator(LqEvnt* Events, LqEvntInterator* Interator)
{
    return Events->ClientArr[Interator->Index];
}

bool LqEvntSignalCheckAndReset(LqEvnt* Events)
{
    return LqFileEventReset(Events->SignalFd) > 0;
}

void LqEvntSignalSet(LqEvnt* Events)
{
    LqFileEventSet(Events->SignalFd);
}

int LqEvntCheck(LqEvnt* Events, LqTimeMillisec WaitTime)
{
    return poll((pollfd*)Events->EventArr, Events->Count, WaitTime);
}

size_t LqEvntCount(const LqEvnt* Events)
{
    return Events->Count - 1;
}

