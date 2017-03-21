/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqFdIpc... - Transfer files descriptors between working processes.
*/

#include "LqSockBuf.h"
#include "LqAlloc.hpp"
#include "LqTime.h"
#include "LqAtm.hpp"
#include "LqWrkBoss.hpp"
#include "LqFdIpc.h"
#include "LqConn.h"
#include "LqWrk.hpp"

#ifndef LQPLATFORM_WINDOWS
//#include <stropts.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#define HAVE_MSGHDR_MSG_CONTROL

#endif

#define LQFDIPC_FLAG_USED ((unsigned char)1)
#define LQFDIPC_FLAG_WORK ((unsigned char)2)
#define LQFDIPC_FLAG_PID_SENDED ((unsigned char)4)
#define LQFDIPC_FLAG_MUST_ACCEPT ((unsigned char)8)
#define LQFDIPC_FLAG_IN_HANDLER     ((unsigned char)16)

typedef struct _FdIpcHdr {
    char Signature[2];
#ifndef LQPLATFORM_WINDOWS
    int Fd;
#endif
    int SizeData;
}_FdIpcHdr;

/* Recursive lock*/
static void _FdIpcLock(LqFdIpc* FdIpc) {
    int CurThread = LqThreadId();

    while(true) {
        LqAtmLkWr(FdIpc->Lk);
        if((FdIpc->ThreadOwnerId == 0) || (FdIpc->ThreadOwnerId == CurThread)) {
            FdIpc->ThreadOwnerId = CurThread;
            FdIpc->Deep++;
            CurThread = 0;
        }
        LqAtmUlkWr(FdIpc->Lk);
        if(CurThread == 0)
            return;
        LqThreadYield();
    }
}

/* Recursive unlock*/
static void _FdIpcUnlock(LqFdIpc* FdIpc) {
    LqAtmLkWr(FdIpc->Lk);
    FdIpc->Deep--;
    if(FdIpc->Deep == 0) {
        FdIpc->ThreadOwnerId = 0;
        if(!(FdIpc->Flags & (LQFDIPC_FLAG_WORK | LQFDIPC_FLAG_USED))) {
            LqFileClose(FdIpc->Evnt.Fd);
            LqFastAlloc::Delete(FdIpc);
            return;
        }
    }
    LqAtmUlkWr(FdIpc->Lk);
}

static int _BlocketWriteInPipe(int PipeFd, int EventFd, const void* SourceBuf, int Size) {
    intptr_t Written = -1;
    LqAsync AsyncData;
    LqEventReset(EventFd);
    if(LqFileWriteAsync(PipeFd, SourceBuf, Size, 0, EventFd, &AsyncData) == -1)
        return -1;
    LqPollCheckSingle(EventFd, LQ_POLLIN | LQ_POLLOUT, LqTimeGetMaxMillisec());
    if(LqFileAsyncStat(&AsyncData, &Written) != 0)
        return -1;
    return Written;
}

static int _BlocketReadFromPipe(int PipeFd, int EventFd, void* DestBuf, int Size) {
    intptr_t Readed = -1;
    LqAsync AsyncData;
    LqEventReset(EventFd);
    if(LqFileReadAsync(PipeFd, DestBuf, Size, 0, EventFd, &AsyncData) == -1)
        return -1;
    LqPollCheckSingle(EventFd, LQ_POLLIN | LQ_POLLOUT, LqTimeGetMaxMillisec());
    if(LqFileAsyncStat(&AsyncData, &Readed) != 0)
        return -1;
    return Readed;
}

static void LQ_CALL _Handler(LqEvntFd* Fd, LqEvntFlag RetFlags) {
    LqFdIpc* FdIpc = (LqFdIpc*)Fd;
    int NewPid;
    int NewFd;
    int WaitEvent;
    intptr_t Res;
#ifndef LQPLATFORM_WINDOWS
    LqWrkPtr WrkPtr = LqWrk::GetNull();
#endif
    _FdIpcLock(FdIpc);
    if(RetFlags & LQEVNT_FLAG_ERR)
        LqClientSetClose(Fd);
    if(RetFlags & LQEVNT_FLAG_RD) {
#ifdef LQPLATFORM_WINDOWS
        if(!(FdIpc->Flags & LQFDIPC_FLAG_PID_SENDED)) {
            if((WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
                goto lblOut;
            NewPid = LqProcessId();
            Res = _BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, &NewPid, sizeof(NewPid));
            LqFileClose(WaitEvent);
            if(Res < (intptr_t)sizeof(NewPid))
                goto lblOut;
            FdIpc->Flags |= LQFDIPC_FLAG_PID_SENDED;
        }
        if(FdIpc->Pid == -1) {
            if((WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
                goto lblOut;
            Res = _BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, &NewPid, sizeof(NewPid));
            LqFileClose(WaitEvent);
            if(Res < (intptr_t)sizeof(NewPid))
                goto lblOut;
            FdIpc->Pid = NewPid;
        }

        if(!(FdIpc->Flags & LQFDIPC_FLAG_PID_SENDED)) {
            if((WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
                goto lblOut;
            NewPid = LqProcessId();
            LqFileWrite(FdIpc->Evnt.Fd, &NewPid, sizeof(NewPid));
            FdIpc->Flags |= LQFDIPC_FLAG_PID_SENDED;
        }
        if(FdIpc->Pid == -1) {
            if(LqFileRead(FdIpc->Evnt.Fd, &NewPid, sizeof(NewPid)) < sizeof(NewPid))
                goto lblOut;
            FdIpc->Pid = NewPid;
        }
#else
        if(FdIpc->Flags & LQFDIPC_FLAG_MUST_ACCEPT) {
            NewFd = accept(FdIpc->Evnt.Fd, NULL, NULL);
            if(NewFd == -1)
                goto lblOut;
            if(FdIpc->Flags & LQFDIPC_FLAG_WORK) {
                WrkPtr = LqWrk::ByEvntHdr((LqClientHdr*)FdIpc);
                LqClientSetRemove3(FdIpc);
            }
            dup2(NewFd, FdIpc->Evnt.Fd);
            LqFileClose(NewFd);
            if(WrkPtr->GetId() != -1ll)
                WrkPtr->AddClientSync((LqClientHdr*)FdIpc);
            FdIpc->Flags &= ~LQFDIPC_FLAG_MUST_ACCEPT;
        }
#endif
        if(FdIpc->RecvHandler) {
            FdIpc->Flags |= LQFDIPC_FLAG_IN_HANDLER;
            _FdIpcUnlock(FdIpc);
            FdIpc->RecvHandler(FdIpc);
            _FdIpcLock(FdIpc);
            FdIpc->Flags &= ~LQFDIPC_FLAG_IN_HANDLER;
        }
    }
lblOut:
    _FdIpcUnlock(FdIpc);
}

static void LQ_CALL _LqHttpConnCloseHandler(LqEvntFd* Fd) {
    LqFdIpc* FdIpc = (LqFdIpc*)Fd;
    _FdIpcLock(FdIpc);
    FdIpc->Flags &= ~LQFDIPC_FLAG_WORK;
    if(FdIpc->CloseHandler) {
        FdIpc->Flags |= LQFDIPC_FLAG_IN_HANDLER;
        _FdIpcUnlock(FdIpc);
        FdIpc->CloseHandler(FdIpc);
        _FdIpcLock(FdIpc);
        FdIpc->Flags &= ~LQFDIPC_FLAG_IN_HANDLER;
    }
    _FdIpcUnlock(FdIpc);
}

LQ_EXTERN_C LqFdIpc* LQ_CALL LqFdIpcOpen(const char* Name, bool IsNoInherit, void* UserData) {
    LqFdIpc* NewIpc;
    char NameBuf[4096];
    int Fd;
    int CurProcessId;
    int WaitEvent;
    intptr_t Written;
#ifndef LQPLATFORM_WINDOWS
    struct sockaddr_un SockName = {0};
#endif
    if((NewIpc = LqFastAlloc::New<LqFdIpc>()) == NULL) {
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
#ifdef LQPLATFORM_WINDOWS
    LqFbuf_snprintf(NameBuf, sizeof(NameBuf), "\\\\.\\Pipe\\FdIpc_%s", Name);
    Fd = LqFileOpen(NameBuf, LQ_O_NONBLOCK | LQ_O_BIN | LQ_O_RDWR | ((IsNoInherit) ? LQ_O_NOINHERIT : 0), 0666);
    if(Fd == -1)
        goto lblErr;
    CurProcessId = LqProcessId();
    if((WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
        goto lblErr;
    Written = _BlocketWriteInPipe(Fd, WaitEvent, &CurProcessId, sizeof(CurProcessId));
    LqFileClose(WaitEvent);
    if(Written == -1)
        goto lblErr;
#else
    SockName.sun_family = AF_UNIX;
    LqFbuf_snprintf(SockName.sun_path, sizeof(SockName.sun_path) - 1, "/tmp/FdIpc_%s", Name);
    Fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(Fd == -1)
        goto lblErr;
    LqConnSwitchNonBlock(Fd, 1);
    if(connect(Fd, (struct sockaddr*)&SockName, sizeof(SockName)) == -1)
        goto lblErr;
#endif
    LqEvntFdInit(&NewIpc->Evnt, Fd, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, _Handler, _LqHttpConnCloseHandler);

    NewIpc->UserData = UserData;
    NewIpc->Flags = LQFDIPC_FLAG_USED
#ifdef LQPLATFORM_WINDOWS
        | LQFDIPC_FLAG_PID_SENDED
#endif
        ;
    NewIpc->CloseHandler = NULL;
    NewIpc->RecvHandler = NULL;
    NewIpc->Pid = -1;

    LqAtmLkInit(NewIpc->Lk);
    NewIpc->ThreadOwnerId = 0;
    NewIpc->Deep = 0;
    return NewIpc;
lblErr:
    if(Fd != -1)
        LqFileClose(Fd);
    if(NewIpc != NULL)
        LqFastAlloc::Delete(NewIpc);
    return NULL;
}

LQ_EXTERN_C LqFdIpc* LQ_CALL LqFdIpcCreate(const char* Name, bool IsNoInherit, void* UserData) {
    LqFdIpc* NewIpc;
    char NameBuf[4096];
    int Fd;
#ifndef LQPLATFORM_WINDOWS
    struct sockaddr_un SockName = {0};
#endif

    if((NewIpc = LqFastAlloc::New<LqFdIpc>()) == NULL) {
        lq_errno_set(ENOMEM);
        goto lblErr;
    }
#ifdef LQPLATFORM_WINDOWS
    LqFbuf_snprintf(NameBuf, sizeof(NameBuf), "\\\\.\\Pipe\\FdIpc_%s", Name);
    Fd = LqPipeCreateNamed(NameBuf, LQ_O_NONBLOCK | LQ_O_BIN | LQ_O_RDWR | ((IsNoInherit) ? LQ_O_NOINHERIT : 0));
    if(Fd == -1)
        goto lblErr;
#else
    LqFileMakeDir("/tmp", 0777);
    SockName.sun_family = AF_UNIX;
    LqFbuf_snprintf(SockName.sun_path, sizeof(SockName.sun_path) - 1, "/tmp/FdIpc_%s", Name);
    Fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(Fd == -1)
        goto lblErr;
    LqConnSwitchNonBlock(Fd, 1);
    remove(SockName.sun_path);
    if(bind(Fd, (struct sockaddr*)&SockName, sizeof(SockName)) == -1)
        goto lblErr;
    listen(Fd, 1);
#endif
    LqEvntFdInit(&NewIpc->Evnt, Fd, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, _Handler, _LqHttpConnCloseHandler);

    NewIpc->UserData = UserData;
    NewIpc->Flags = LQFDIPC_FLAG_USED;
#ifndef LQPLATFORM_WINDOWS
    NewIpc->Flags |= LQFDIPC_FLAG_MUST_ACCEPT;
#endif
    NewIpc->CloseHandler = NULL;
    NewIpc->RecvHandler = NULL;
    NewIpc->Pid = -1;

    LqAtmLkInit(NewIpc->Lk);
    NewIpc->ThreadOwnerId = 0;
    NewIpc->Deep = 0;
    return NewIpc;
lblErr:
    if(Fd != -1)
        LqFileClose(Fd);
    if(NewIpc != NULL)
        LqFastAlloc::Delete(NewIpc);
    return NULL;
}

LQ_EXTERN_C int LQ_CALL LqFdIpcSend(LqFdIpc* FdIpc, int Fd, const void* AttachedBuffer, int SizeBuffer) {
    int Res = -1;
#ifdef LQPLATFORM_WINDOWS
    int NewPid;
    int WaitEvent = -1;
    HANDLE TargetHandle;
    int TargetHandle2;
    HANDLE DestProcessHandle = NULL;

    _FdIpcLock(FdIpc);
    if((WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
        goto lblOut;

    if(!(FdIpc->Flags & LQFDIPC_FLAG_PID_SENDED)) {
        NewPid = LqProcessId();
        if(_BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, &NewPid, sizeof(NewPid)) < (intptr_t)sizeof(NewPid))
            goto lblOut;
        FdIpc->Flags |= LQFDIPC_FLAG_PID_SENDED;
    }
    if(FdIpc->Pid == -1) {
        if(_BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, &NewPid, sizeof(NewPid)) < (intptr_t)sizeof(NewPid))
            goto lblOut;
        FdIpc->Pid = NewPid;
    }
    if((DestProcessHandle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, FdIpc->Pid)) == NULL)
        goto lblOut;

    if(_BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, "fd", 2) < 2)
        goto lblOut;
    if(Fd != -1) {
        if(DuplicateHandle(GetCurrentProcess(), (HANDLE)Fd, DestProcessHandle, &TargetHandle, DUPLICATE_SAME_ACCESS, FALSE, DUPLICATE_SAME_ACCESS) == FALSE)
            goto lblOut;
        TargetHandle2 = (int)TargetHandle;
    } else {
        TargetHandle2 = -1;
    }
    if(_BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, &TargetHandle2, sizeof(TargetHandle2)) < sizeof(TargetHandle2))
        goto lblOut;

    TargetHandle2 = (AttachedBuffer == NULL) ? 0 : SizeBuffer;

    if(_BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, &TargetHandle2, sizeof(TargetHandle2)) < sizeof(TargetHandle2))
        goto lblOut;

    if(SizeBuffer > 0) {
        if(_BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, AttachedBuffer, SizeBuffer) < SizeBuffer)
            goto lblOut;
    }

    Res = SizeBuffer;
lblOut:
    _FdIpcUnlock(FdIpc);
    if(WaitEvent != -1)
        LqFileClose(WaitEvent);
    if(DestProcessHandle != NULL)
        CloseHandle(DestProcessHandle);
    return Res;
#else
    LqWrkPtr WrkPtr = LqWrk::GetNull();
    int NewFd;
    LqWrk* WorkerOwner = NULL;
    struct msghdr   msg = {0};
    struct iovec    iov[1];
#ifdef HAVE_MSGHDR_MSG_CONTROL
    union {
        struct cmsghdr    cm;
        char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;
#endif

    _FdIpcLock(FdIpc);
    if(FdIpc->Flags & LQFDIPC_FLAG_MUST_ACCEPT) {
        NewFd = accept(FdIpc->Evnt.Fd, NULL, NULL);
        if(NewFd == -1)
            goto lblOut;
        if(FdIpc->Flags & LQFDIPC_FLAG_WORK) {
            WrkPtr = LqWrk::ByEvntHdr((LqClientHdr*)FdIpc);
            LqClientSetRemove3(FdIpc);
        }
        dup2(NewFd, FdIpc->Evnt.Fd);
        LqFileClose(NewFd);
        if(WrkPtr->GetId() != -1ll)
            WrkPtr->AddClientSync((LqClientHdr*)FdIpc);
        FdIpc->Flags &= ~LQFDIPC_FLAG_MUST_ACCEPT;
    }

    iov[0].iov_base = &SizeBuffer;
    iov[0].iov_len = sizeof(SizeBuffer);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
#ifdef  HAVE_MSGHDR_MSG_CONTROL
    if(Fd != -1) {
        msg.msg_control = &control_un;
        msg.msg_controllen = sizeof(control_un);

        cmptr = CMSG_FIRSTHDR(&msg);
        cmptr->cmsg_len = CMSG_LEN(sizeof(int));
        cmptr->cmsg_level = SOL_SOCKET;
        cmptr->cmsg_type = SCM_RIGHTS;
        *((int *)CMSG_DATA(cmptr)) = Fd;
    } else {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
    }
#else
    msg.msg_accrights = (caddr_t)&Fd;
    msg.msg_accrightslen = sizeof(int);
#endif

    LqConnSwitchNonBlock(FdIpc->Evnt.Fd, 0);
    if(sendmsg(FdIpc->Evnt.Fd, &msg, 0) <= 0)
        goto lblOut2;
    if(SizeBuffer > 0) {
        iov[0].iov_base = AttachedBuffer;
        iov[0].iov_len = SizeBuffer;
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
#ifdef  HAVE_MSGHDR_MSG_CONTROL
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
#else
        msg.msg_accrights = NULL;
        msg.msg_accrightslen = 0;
#endif
        if(sendmsg(FdIpc->Evnt.Fd, &msg, 0) > 0)
            Res = SizeBuffer;
    } else {
        Res = 0;
    }
lblOut2:
    LqConnSwitchNonBlock(FdIpc->Evnt.Fd, 1);
lblOut:
    _FdIpcUnlock(FdIpc);
    return Res;
#endif
}


LQ_EXTERN_C int LQ_CALL LqFdIpcRecive(LqFdIpc* FdIpc, int* Fd, void** AttachedBuffer) {
    int Res = -1;
    void* GlobBuffer = NULL;
#ifdef LQPLATFORM_WINDOWS
    int WaitEvent = -1;
    int SourceHandle2;
    char Buff[16];
    int NewPid;

    _FdIpcLock(FdIpc);
    if((WaitEvent = LqEventCreate(LQ_O_NOINHERIT)) == -1)
        goto lblOut;

    if(!(FdIpc->Flags & LQFDIPC_FLAG_PID_SENDED)) {
        NewPid = LqProcessId();
        if(_BlocketWriteInPipe(FdIpc->Evnt.Fd, WaitEvent, &NewPid, sizeof(NewPid)) < (intptr_t)sizeof(NewPid))
            goto lblOut;
        FdIpc->Flags |= LQFDIPC_FLAG_PID_SENDED;
    }
    if(FdIpc->Pid == -1) {
        if(_BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, &NewPid, sizeof(NewPid)) < (intptr_t)sizeof(NewPid))
            goto lblOut;
        FdIpc->Pid = NewPid;
    }

    if((_BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, Buff, 2) < 2) || (Buff[0] != 'f') || (Buff[1] != 'd'))
        goto lblOut;
    if(_BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, &SourceHandle2, sizeof(SourceHandle2)) < sizeof(SourceHandle2))
        goto lblOut;
    *Fd = SourceHandle2;
    if(_BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, &SourceHandle2, sizeof(SourceHandle2)) < sizeof(SourceHandle2))
        goto lblOut;
    if(SourceHandle2 > 0) {
        GlobBuffer = malloc(SourceHandle2);
        if(_BlocketReadFromPipe(FdIpc->Evnt.Fd, WaitEvent, GlobBuffer, SourceHandle2) < SourceHandle2)
            goto lblOut;
		if(AttachedBuffer != NULL)
			*AttachedBuffer = GlobBuffer;
		else
			free(GlobBuffer);
        GlobBuffer = NULL;
    }
    Res = SourceHandle2;
lblOut:
    _FdIpcUnlock(FdIpc);
    if(WaitEvent != -1)
        LqFileClose(WaitEvent);
    if(GlobBuffer != NULL)
        free(GlobBuffer);
    return Res;
#else
    LqWrkPtr WrkPtr = LqWrk::GetNull();
    int NewFd;
    LqWrk* WorkerOwner = NULL;
    struct msghdr   msg = {0};
    struct iovec    iov[2];
    ssize_t         Recived;
    int             newfd;
    int             LenBuf = 0;
#ifdef  HAVE_MSGHDR_MSG_CONTROL
    union {
        struct cmsghdr    cm;
        char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;
#endif
    _FdIpcLock(FdIpc);
    if(FdIpc->Flags & LQFDIPC_FLAG_MUST_ACCEPT) {
        NewFd = accept(FdIpc->Evnt.Fd, NULL, NULL);
        if(NewFd == -1)
            goto lblOut;
        if(FdIpc->Flags & LQFDIPC_FLAG_WORK) {
            WrkPtr = LqWrk::ByEvntHdr((LqClientHdr*)FdIpc);
            LqClientSetRemove3(FdIpc);
        }
        dup2(NewFd, FdIpc->Evnt.Fd);
        LqFileClose(NewFd);
        if(WrkPtr->GetId() != -1ll)
            WrkPtr->AddClientSync((LqClientHdr*)FdIpc);
        FdIpc->Flags &= ~LQFDIPC_FLAG_MUST_ACCEPT;
    }
#ifdef  HAVE_MSGHDR_MSG_CONTROL
    msg.msg_control = &control_un;
    msg.msg_controllen = sizeof(control_un);
#else
    msg.msg_accrights = (caddr_t)&newfd;
    msg.msg_accrightslen = sizeof(int);
#endif

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = &LenBuf;
    iov[0].iov_len = sizeof(LenBuf);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    LqConnSwitchNonBlock(FdIpc->Evnt.Fd, 0);
    Recived = recvmsg(FdIpc->Evnt.Fd, &msg, 0);
    LqConnSwitchNonBlock(FdIpc->Evnt.Fd, 1);
    if(Recived <= 0)
        goto lblOut;
#ifdef  HAVE_MSGHDR_MSG_CONTROL
    if(((cmptr = CMSG_FIRSTHDR(&msg)) != NULL) && (cmptr->cmsg_len == CMSG_LEN(sizeof(int)))) {
        if((cmptr->cmsg_level != SOL_SOCKET) || (cmptr->cmsg_type != SCM_RIGHTS))
            goto lblOut;
        *Fd = *((int *)CMSG_DATA(cmptr));
    } else
        *Fd = -1;
#else
    if(msg.msg_accrightslen == sizeof(int))
        *Fd = newfd;
    else
        *Fd = -1;
#endif

    if(LenBuf > 0) {
        GlobBuffer = malloc(LenBuf);
        iov[0].iov_base = GlobBuffer;
        iov[0].iov_len = LenBuf;
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
#ifdef  HAVE_MSGHDR_MSG_CONTROL
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
#else
        msg.msg_accrights = NULL;
        msg.msg_accrightslen = 0;
#endif
        LqConnSwitchNonBlock(FdIpc->Evnt.Fd, 0);
        Recived = recvmsg(FdIpc->Evnt.Fd, &msg, 0);
        LqConnSwitchNonBlock(FdIpc->Evnt.Fd, 1);
        if(Recived < LenBuf)
            goto lblOut;
		if(AttachedBuffer != NULL)
			*AttachedBuffer = GlobBuffer;
		else
			free(GlobBuffer);
        GlobBuffer = NULL;
    }
    Res = LenBuf;
lblOut:
    _FdIpcUnlock(FdIpc);
    if(GlobBuffer != NULL)
        free(GlobBuffer);
    return Res;
#endif
}

LQ_EXTERN_C bool LQ_CALL LqFdIpcDelete(LqFdIpc* FdIpc) {
    _FdIpcLock(FdIpc);
    FdIpc->UserData = NULL;
    FdIpc->CloseHandler = NULL;
    FdIpc->RecvHandler = NULL;
    FdIpc->Flags &= ~LQFDIPC_FLAG_USED;
    if(FdIpc->Flags & LQFDIPC_FLAG_WORK)
        LqClientSetClose(FdIpc);
    _FdIpcUnlock(FdIpc);
    return true;
}

LQ_EXTERN_C bool LQ_CALL LqFdIpcGoWork(LqFdIpc* FdIpc, void* WrkBoss) {
    bool Res = false;
    _FdIpcLock(FdIpc);
    if(FdIpc->Flags & LQFDIPC_FLAG_WORK)
        goto lblOut;
    if(LqClientAdd(&FdIpc->Evnt, WrkBoss)) {
        FdIpc->Flags |= LQFDIPC_FLAG_WORK;
        Res = true;
        goto lblOut;
    }
lblOut:
    _FdIpcUnlock(FdIpc);
    return Res;
}

LQ_EXTERN_C bool LQ_CALL LqFdIpcInterruptWork(LqFdIpc* FdIpc) {
    bool Res;
    _FdIpcLock(FdIpc);
    if(Res = LqClientSetRemove3(&FdIpc->Evnt))
        FdIpc->Flags &= ~LQFDIPC_FLAG_WORK;
    _FdIpcUnlock(FdIpc);
    return Res;
}

LQ_IMPORTEXPORT void LQ_CALL LqFdIpcLock(LqFdIpc* FdIpc) {
    _FdIpcLock(FdIpc);
}

LQ_IMPORTEXPORT void LQ_CALL LqFdIpcUnlock(LqFdIpc* FdIpc) {
    _FdIpcUnlock(FdIpc);
}

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

