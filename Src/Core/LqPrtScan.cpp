/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqPrtScan (LanQ Port Scanner) - Scan ports of local or remote machine. Creates sockets and just trying connect
*/


#include "LqPrtScan.hpp"
#include "LqWrkBoss.hpp"
#include "LqTime.h"
#include "LqStrSwitch.h"
#include "LqFile.h"
#include "LqFileTrd.h"
#include "LqSbuf.h"
#include "LqCp.h"
#include "LqLib.h"
#include "LqStr.hpp"
#include "LqStr.hpp"
#include "LqDef.hpp"
#include "LqTime.hpp"


#include <stdint.h>
#include <stdlib.h>
#include <type_traits>
#include <vector>

struct ConnScan {
    LqConn Conn;
    LqEvntFd LiveTime;
    uint16_t Port;
    std::atomic<uint8_t> CountPtr;
    LqLocker<uint8_t>    Locker;
};

struct ProtoScan {
    LqProto  Proto;

    std::atomic<uintptr_t> CountConn;
    int WaitEvent;
    LqPtdArr<uint16_t> OpenedPorts;
};

static void LQ_CALL ConnScanHandler(LqConn* Conn, LqEvntFlag Flag) {
    if(!(Flag & LQEVNT_FLAG_ERR))
        ((ProtoScan*)Conn->Proto)->OpenedPorts.push_back(((ConnScan*)Conn)->Port);
    LqClientSetClose(Conn);
}

static void LQ_CALL TimerHandler(LqEvntFd* Fd, LqEvntFlag Flag) {
    LqClientSetClose(Fd);
}

static void LQ_CALL TimerEnd(LqEvntFd* TimerFd) {
	ProtoScan* Proto;
	ConnScan* Conn;

	Conn = LqStructByField(ConnScan, LiveTime, TimerFd);
    Conn->Locker.LockWrite();
    if(Conn->CountPtr == 2)
        LqClientSetClose(Conn);
    Conn->CountPtr--;
    if(Conn->CountPtr <= 0) {
        auto Proto = ((ProtoScan*)Conn->Conn.Proto);
        closesocket(Conn->Conn.Fd);
        LqFastAlloc::Delete(Conn);
        Proto->CountConn--;
        LqEventSet(Proto->WaitEvent);
        return;
    }
    Conn->Locker.UnlockWrite();
}

static void LQ_CALL EndProc(LqConn* Connection) {
	ProtoScan* Proto;
    ConnScan* Conn = (ConnScan*)Connection;
    Conn->Locker.LockWrite();
    if(Conn->CountPtr == 2)
        LqClientSetClose(&Conn->LiveTime);
    Conn->CountPtr--;
    if(Conn->CountPtr <= 0) {
        Proto = ((ProtoScan*)Conn->Conn.Proto);
        closesocket(Conn->Conn.Fd);
        LqFastAlloc::Delete(Conn);
        Proto->CountConn--;
        LqEventSet(Proto->WaitEvent);
        return;
    }
    Conn->Locker.UnlockWrite();
}

LQ_EXTERN_CPP bool LQ_CALL LqPrtScanDo(LqConnAddr* Addr, std::vector<std::pair<uint16_t, uint16_t>>& PortRanges, int MaxScanConn, LqTimeMillisec ConnWait, std::vector<uint16_t>& OpenPorts) {
    ProtoScan Proto;
	LqConnAddr AddrLoc;
	int Fd, Res, TimerFd;
	ConnScan* Conn;

    LqProtoInit(&Proto.Proto);
    Proto.Proto.Handler = ConnScanHandler;
    Proto.Proto.CloseHandler = EndProc;
    if((Proto.WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
        return -1;
    Proto.CountConn = 0;
    for(auto& i : PortRanges) {
        for(auto j = i.first; j <= i.second; j++) {
            if(Proto.CountConn >= MaxScanConn) {
lblWaitAgain:
                LqPollCheckSingle(Proto.WaitEvent, LQ_POLLIN | LQ_POLLOUT, 60 * 2 * 1000);
                LqEventReset(Proto.WaitEvent);
                if(Proto.CountConn >= MaxScanConn)
                    goto lblWaitAgain;
            }
            if((Fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == -1)
                continue;
            LqConnSwitchNonBlock(Fd, 1);
            AddrLoc = *Addr;
            if(AddrLoc.Addr.sa_family == AF_INET) {
                AddrLoc.AddrInet.sin_port = htons(j);
            } else if(AddrLoc.Addr.sa_family == AF_INET6) {
                AddrLoc.AddrInet6.sin6_port = htons(j);
            }
            if((Res = connect(Fd, &AddrLoc.Addr, sizeof(AddrLoc))) == 0) {
                Proto.OpenedPorts.push_back(j);
                closesocket(Fd);
            } else if(LQERR_IS_WOULD_BLOCK) {
                Conn = LqFastAlloc::New<ConnScan>();
                LqConnInit(Conn, Fd, &Proto.Proto, LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_HUP);
                TimerFd = LqTimerCreate(LQ_O_NOINHERIT);
                LqTimerSet(TimerFd, ConnWait);
                LqEvntFdInit(&Conn->LiveTime, TimerFd, LQEVNT_FLAG_RD, TimerHandler, TimerEnd);
                Conn->CountPtr = 2;
                Conn->Port = j;
                Proto.CountConn++;
                LqClientAdd((LqClientHdr*)&Conn->LiveTime, NULL);
				LqClientAdd((LqClientHdr*)Conn, NULL);
            } else {
                closesocket(Fd);
            }
        }
    }
    while(Proto.CountConn > 0) {
        LqPollCheckSingle(Proto.WaitEvent, LQ_POLLIN | LQ_POLLOUT, 60 * 2 * 1000);
        LqEventReset(Proto.WaitEvent);
    }
    for(auto i : Proto.OpenedPorts)
        OpenPorts.push_back(i);
    LqFileClose(Proto.WaitEvent);

    return true;
}

#define __METHOD_DECLS__
#include "LqAlloc.hpp"