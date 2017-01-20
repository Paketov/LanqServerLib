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
    LqEvntSetClose(Conn);
}

static void LQ_CALL TimerHandler(LqEvntFd* Fd, LqEvntFlag Flag) {
    LqEvntSetClose(Fd);
}

static void LQ_CALL TimerEnd(LqEvntFd* TimerFd) {
    auto Conn = LqStructByField(ConnScan, LiveTime, TimerFd);
    Conn->Locker.LockWrite();
    if(Conn->CountPtr == 2)
        LqEvntSetClose(Conn);
    Conn->CountPtr--;
    if(Conn->CountPtr <= 0) {
        auto Proto = ((ProtoScan*)Conn->Conn.Proto);
        closesocket(Conn->Conn.Fd);
        LqFastAlloc::Delete(Conn);
        Proto->CountConn--;
        LqFileEventSet(Proto->WaitEvent);
        return;
    }
    Conn->Locker.UnlockWrite();
}

static void LQ_CALL EndProc(LqConn* Connection) {
    auto Conn = (ConnScan*)Connection;
    Conn->Locker.LockWrite();
    if(Conn->CountPtr == 2)
        LqEvntSetClose(&Conn->LiveTime);
    Conn->CountPtr--;
    if(Conn->CountPtr <= 0) {
        auto Proto = ((ProtoScan*)Conn->Conn.Proto);
        closesocket(Conn->Conn.Fd);
        LqFastAlloc::Delete(Conn);
        Proto->CountConn--;
        LqFileEventSet(Proto->WaitEvent);
        return;
    }
    Conn->Locker.UnlockWrite();
}

LQ_EXTERN_CPP bool LQ_CALL LqPrtScanDo(LqConnInetAddress* Addr, std::vector<std::pair<uint16_t, uint16_t>>& PortRanges, int MaxScanConn, LqTimeMillisec ConnWait, std::vector<uint16_t>& OpenPorts) {
    ProtoScan Proto;
    LqProtoInit(&Proto.Proto);
    Proto.Proto.Handler = ConnScanHandler;
    Proto.Proto.CloseHandler = EndProc;
    Proto.WaitEvent = LqFileEventCreate(LQ_O_NOINHERIT);
    if(Proto.WaitEvent == -1)
        return -1;
    Proto.CountConn = 0;
    for(auto& i : PortRanges) {
        for(auto j = i.first; j <= i.second; j++) {
            if(Proto.CountConn >= MaxScanConn) {
lblWaitAgain:
                LqFilePollCheckSingle(Proto.WaitEvent, LQ_POLLIN | LQ_POLLOUT, 60 * 2 * 1000);
                LqFileEventReset(Proto.WaitEvent);
                if(Proto.CountConn >= MaxScanConn)
                    goto lblWaitAgain;
            }
            auto Fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if(Fd == -1)
                continue;
            LqSockSwitchNonBlock(Fd, 1);
            LqConnInetAddress AddrLoc = *Addr;
            if(AddrLoc.Addr.sa_family == AF_INET) {
                AddrLoc.AddrInet.sin_port = htons(j);
            } else if(AddrLoc.Addr.sa_family == AF_INET6) {
                AddrLoc.AddrInet6.sin6_port = htons(j);
            }
            auto Res = connect(Fd, &AddrLoc.Addr, sizeof(AddrLoc));
            if(Res == 0) {
                Proto.OpenedPorts.push_back(j);
                closesocket(Fd);
            } else if(LQERR_IS_WOULD_BLOCK) {
                auto Conn = LqFastAlloc::New<ConnScan>();
                LqConnInit(Conn, Fd, &Proto.Proto, LQEVNT_FLAG_CONNECT | LQEVNT_FLAG_HUP);
                auto TimerFd = LqFileTimerCreate(LQ_O_NOINHERIT);
                LqFileTimerSet(TimerFd, ConnWait);
                LqEvntFdInit(&Conn->LiveTime, TimerFd, LQEVNT_FLAG_RD);
                Conn->LiveTime.Handler = TimerHandler;
                Conn->LiveTime.CloseHandler = TimerEnd;
                Conn->CountPtr = 2;
                Conn->Port = j;
                Proto.CountConn++;
                LqWrkBossAddEvntAsync((LqEvntHdr*)&Conn->LiveTime);
                LqWrkBossAddEvntAsync((LqEvntHdr*)Conn);
            } else {
                closesocket(Fd);
            }
        }
    }
    while(Proto.CountConn > 0) {
        LqFilePollCheckSingle(Proto.WaitEvent, LQ_POLLIN | LQ_POLLOUT, 60 * 2 * 1000);
        LqFileEventReset(Proto.WaitEvent);
    }
    for(auto i : Proto.OpenedPorts)
        OpenPorts.push_back(i);
    LqFileClose(Proto.WaitEvent);

    return true;
}

#define __METHOD_DECLS__
#include "LqAlloc.hpp"