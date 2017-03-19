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
#include "LqTime.hpp"
#include "LqStr.hpp"
#include "LqHttp.hpp"
#include "LqHttpAtz.h"
#include "LqLib.h"
#include "LqShdPtr.hpp"

#include <vector>
#include <sstream>

struct CmdSession;

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

    inline void LockRead() {
        Locker.LockReadYield();
    }

    inline void LockWrite() {
        Locker.LockWriteYield();
    }

    inline void Unlock() {
        Locker.Unlock();
    }

    CmdSession(int NewStdIn, int NewPid, LqString& NewKey);

    ~CmdSession();

    static void LQ_CALL TimerHandler(LqEvntFd* Instance, LqEvntFlag Flags);

    static void LQ_CALL TimerHandlerClose(LqEvntFd* Instance);

    static void LQ_CALL ReadHandler(LqEvntFd* Instance, LqEvntFlag Flags);

    static void LQ_CALL ReadHandlerClose(LqEvntFd* Instance);
};

