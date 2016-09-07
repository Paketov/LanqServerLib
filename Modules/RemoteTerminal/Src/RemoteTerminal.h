#pragma once


/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   RemoteTerminal - Create terminal sessions.
*/

#include "LqConn.h"
#include "Lanq.h"
#include "LqWrkBoss.hpp"
#include "LqStrSwitch.h"
#include "LqHttpMdl.h"
#include "LqDfltRef.hpp"
#include "LqHttpPth.hpp"
#include "LqHttpRsp.h"
#include "LqHttpConn.h"
#include "LqHttpAct.h"
#include "LqTime.hpp"
#include "LqStr.hpp"
#include "LqHttpRcv.h"
#include "LqHttp.hpp"
#include "LqHttpAtz.h"
#include "LqLib.h"
#include "LqShdPtr.hpp"

#include <vector>
#include <sstream>

struct CmdSession;


class _Sessions
{
    std::vector<CmdSession*> Data;
    int LastEmpty;
    mutable LqLocker<uintptr_t> Lk;

public:

    inline _Sessions()
    {
        LastEmpty = -1;
    }

    inline ~_Sessions() { LastEmpty = -1; }

    size_t Add(CmdSession* Session);

    void Remove(CmdSession* Session);

    CmdSession* Get(size_t Index, LqString Key) const;

    inline size_t Count() const
    {
        return Data.size();
    }

} Sessions;

struct CmdSession
{
    LqEvntFd                                                    TimerFd;
    LqEvntFd                                                    ReadFd;

    size_t                                                      CountPointers;
    LqLocker<intptr_t>                                          Locker;
    int                                                         MasterFd;
    int                                                         Pid;
    LqString                                                    Key;
    int                                                         Index;

    LqHttpConn*                                                 Conn;
    LqTimeMillisec                                              LastAct;

    inline void LockRead()
    {
        Locker.LockReadYield();
    }

    inline void LockWrite()
    {
        Locker.LockWriteYield();
    }

    inline void Unlock()
    {
        Locker.Unlock();
    }

	CmdSession(int NewStdIn, int NewPid, LqString& NewKey);

	~CmdSession();

    static void LQ_CALL TimerHandler(LqEvntFd* Instance, LqEvntFlag Flags);

	static void LQ_CALL TimerHandlerClose(LqEvntFd* Instance, LqEvntFlag Flags);

	bool StartRead(LqHttpConn* c);

	static void EndRead(LqHttpConn* c);

    static void LQ_CALL ReadHandler(LqEvntFd* Instance, LqEvntFlag Flags);

	static void LQ_CALL ReadHandlerClose(LqEvntFd* Instance, LqEvntFlag Flags);
};

struct ConnHandlers
{
	static void LQ_CALL NewTerminal(LqHttpConn* c);

	static void LQ_CALL CloseTerminal(LqHttpConn* c);

	static void LQ_CALL Write(LqHttpConn* c);

	static void LQ_CALL Write2(LqHttpConn* c);

	static void LQ_CALL Read(LqHttpConn* c);

	static void LQ_CALL ReadClose(LqHttpConn* c);
};



