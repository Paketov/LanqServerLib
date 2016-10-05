/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFile... - File layer between os and server.
*/


#ifndef __LQ_FILE_H__HAS_INCLUDED__
#define __LQ_FILE_H__HAS_INCLUDED__

#include "LqDef.h"
#include "LqOs.h"


#ifdef LQPLATFORM_POSIX
# include <poll.h>

# ifdef LQ_ASYNC_IO_NOT_HAVE
typedef struct LqAsync
{
    char __empty;
} LqAsync;
# else

#  include <aio.h>

typedef struct LqAsync
{
    bool IsNonBlock;
    int EvFd;
    aiocb cb;
} LqAsync;
# endif

typedef struct pollfd LqFilePoll;

#define LQ_POLLIN   POLLIN
#define LQ_POLLOUT  POLLOUT
#define LQ_POLLHUP  POLLHUP
#define LQ_POLLNVAL POLLNVAL
#define LQ_POLLERR  POLLERR
/*Use in LqFileOpen*/
#define LQ_NULLDEV "/dev/null"



#else

LQ_EXTERN_C_BEGIN
LQ_IMPORTEXPORT int LQ_CALL __LqStdErrFileNo();
LQ_IMPORTEXPORT int LQ_CALL __LqStdOutFileNo();
LQ_IMPORTEXPORT int LQ_CALL __LqStdInFileNo();
LQ_EXTERN_C_END

#define STDERR_FILENO __LqStdErrFileNo()
#define STDOUT_FILENO __LqStdOutFileNo()
#define STDIN_FILENO  __LqStdInFileNo()

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqFilePoll
{
    int   fd;         /* file descriptor */
    short events;     /* requested events */
    short revents;    /* returned events */
} LqFilePoll;
#pragma pack(pop)

typedef struct LqAsync
{
    union
    {
        long        Status;
        void*       Pointer;
    };
    uintptr_t       Information;
} LqAsync;


#define LQ_POLLIN    1
#define LQ_POLLOUT   2
#define LQ_POLLHUP   4
#define LQ_POLLNVAL  8
#define LQ_POLLERR   16

/*Use in LqFileOpen*/
#define LQ_NULLDEV "nul"

#endif

LQ_EXTERN_C_BEGIN

#define LQ_F_DIR                1
#define LQ_F_REG                2
#define LQ_F_OTHER              3
#define LQ_F_DEV                4


#define LQ_STDERR STDERR_FILENO
#define LQ_STDOUT STDOUT_FILENO
#define LQ_STDIN  STDIN_FILENO

enum
{
    LQDIREVNT_ADDED = 1,
    LQDIREVNT_RM = 2,
    LQDIREVNT_MOD = 4,
    LQDIREVNT_MOVE_FROM = 8,
    LQDIREVNT_MOVE_TO = 16,
    LQDIREVNT_SUBTREE = 32
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqFileStat
{
    uint8_t             Type;
    uint16_t            Access;

    unsigned int        DevId;
    unsigned int        Id;
    unsigned int        RefCount;

    LqFileSz            Size;
    LqTimeSec           CreateTime;
    LqTimeSec           ModifTime;
    LqTimeSec           AccessTime;

    short               Gid;
    short               Uid;
} LqFileStat;

typedef struct LqFileEnm
{
    uintptr_t Hndl;
    char* Internal;
} LqFileEnm;

/*----------------------------------------
* LqFilePathEvnt
*/

typedef struct LqFilePathEvnt
{
    int Fd; /*This event can use in LqEvnt or LqFilePoll*/

    /*Internal data*/
#if defined(LQPLATFORM_WINDOWS)
    struct
    {
        int           DirFd;
        bool          IsSubtree;
        unsigned long NotifyFilter;
        char*         DirName;
        size_t        DirNameLen;
        char*         Buffer;
        size_t        BufferSize;
        struct
        {
            union
            {
                long  Status;
                void* Pointer;
            };
            uintptr_t Information;
        } IoStatusBlock;    /*O_STATUS_BLOCK for read/write check in windows*/

    } _Data;
#else
    struct Subdir
    {
        int                     Pwd;
        char*                   Name;
    };
    struct
    {
        char*                   Name;
        struct Subdir*          Subdirs;
        size_t                  SubdirsCount;
        size_t                  Max;
        uint32_t                Mask;
        int                     Ifd;
        int                     Wd;
        bool                    IsSubtree;
    } _Data;
#endif
} LqFilePathEvnt;

struct LqFilePathEvntEnm;
typedef struct LqFilePathEvntEnm LqFilePathEvntEnm;

struct LqFilePathEvntEnm
{
    LqFilePathEvntEnm*  Next;
    uint8_t             Flag;
    char                Name[1];
};

#pragma pack(pop)

#define LQ_MAX_PATH 32767


/*
* LqFileOpen flags
*/
#define LQ_O_RD             0x0000
#define LQ_O_WR             0x0001
#define LQ_O_RDWR           0x0002
#define LQ_O_APND           0x0008
#define LQ_O_RND            0x0010
#define LQ_O_SEQ            0x0020
#define LQ_O_TMP            0x0040
#define LQ_O_NOINHERIT      0x0080  /* Child process not inherit created descriptor*/
#define LQ_O_CREATE         0x0100
#define LQ_O_TRUNC          0x0200
#define LQ_O_EXCL           0x0400
#define LQ_O_NONBLOCK       0x0800  /*Work in windows and unix*/
#define LQ_O_SHORT_LIVED    0x1000
#define LQ_O_DSYNC          0x2000
#define LQ_O_TXT            0x4000
#define LQ_O_BIN            0x8000


#define LQ_SEEK_SET         0
#define LQ_SEEK_CUR         1
#define LQ_SEEK_END         2

/*
* For lock file
*/
#define LQ_FLOCK_WR         1
#define LQ_FLOCK_RD         2
#define LQ_FLOCK_WAIT       4
#define LQ_FLOCK_UNLOCK     8




/*------------------------------------------
* Files
* On windows or linux you can open device/file/pipe and other kernel objects.
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileOpen(const char* lqacp lqain FileName, uint32_t Flags, int Access);

/*
* On windows or linux you can read/write in non block mode (LQ_O_NONBLOCK)
*  @Fd: Open file descriptor or LQ_STDIN
*  @return: -1 - on error (check lq_errno), 0 - success
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileRead(int Fd, void* lqaout DestBuf, intptr_t SizeBuf);
/*
* LQ_STDERR, LQ_STDOUT
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileWrite(int Fd, const void* lqain SourceBuf, intptr_t SizeBuf);

/*-------------------------------------------
* Async read/write
*/
/*
* Async read from file
*  @Fd: File descriptor for read. Must be created with LQ_O_NONBLOCK flag
*  @DestBuf: Output buffer
*  @SizeBuf: Size buffer
*  @Offset: Offset in file
*  @EventFd: Event created by LqFileEventCreate(). Before call must be set to zero.
*  @Target: Target LqAsync structure
*  @return: -1 - on error(chaeck lq_errno), 0 - on success
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileReadAsync(int Fd, void* lqaout DestBuf, intptr_t SizeBuf, LqFileSz Offset, int EventFd, LqAsync* lqaout Target);
/*
* Async write in file
*  @Fd: File descriptor for write. Must be created with LQ_O_NONBLOCK flag
*  @DestBuf: Output buffer
*  @SizeBuf: Size buffer
*  @Offset: Offset in file
*  @EventFd: Event created by LqFileEventCreate(). Before call must be set to zero.
*  @Target: Target LqAsync structure
*  @return: -1 - on error(chaeck lq_errno), 0 - on success
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileWriteAsync(int Fd, const void* lqain DestBuf, intptr_t SizeBuf, LqFileSz Offset, int EventFd, LqAsync* lqaout Target);

/*
* Cancel async read/write operation
*  @Fd: File descriptor
*  @Target: Target LqAsync structure
*  @return: -1 - on error(chaeck lq_errno), 0 - on success
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileAsyncCancel(int Fd, LqAsync* lqain Target);
/*
* Get info about async operation
*  @Target: Target LqAsync structure
*  @LenWritten: When return 0, then value have number count written/readed bytes.
*  @return: 0 - on success (check @LenWritten), another val is error or EINPROGRESS
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileAsyncStat(LqAsync* lqain Target, intptr_t* lqaout LenWritten);

/*
* @Fd: Opened file
* @StartOffset: Start of region
* @Len: Len of region
* @LockFlags: Lock flags: LQ_FLOCK_WR - Lock write, LQ_FLOCK_RD - Lock read, LQ_FLOCK_WAIT - Wait until set new locking, LQ_FLOCK_UNLOCK - Remove locking.
* @return: -1 - On error (check lq_errno), 0 - success
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileSetLock(int Fd, LqFileSz StartOffset, LqFileSz Len, int LockFlags);

LQ_IMPORTEXPORT int LQ_CALL LqFileClose(int Fd);
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqFileTell(int Fd);
/*
* @Flag: LQ_SEEK_CUR or LQ_SEEK_SET or LQ_SEEK_END
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqFileSeek(int Fd, LqFileSz Offset, int Flag);
LQ_IMPORTEXPORT int LQ_CALL LqFileEof(int Fd); //1 - end of file, 0 - not end, -1 - error

LQ_IMPORTEXPORT int LQ_CALL LqFileGetStat(const char* lqacp lqain FileName, LqFileStat* lqaout StatDest);
LQ_IMPORTEXPORT int LQ_CALL LqFileGetStatByFd(int Fd, LqFileStat* lqaout StatDest);
/*Get name file by descriptor*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileGetPath(int Fd, char* lqacp lqaout DestBuf, intptr_t SizeBuf);

LQ_IMPORTEXPORT int LQ_CALL LqFileRemove(const char* lqacp lqain FileName);
LQ_IMPORTEXPORT int LQ_CALL LqFileMove(const char* lqacp lqain OldName, const char* lqacp lqain NewName);
LQ_IMPORTEXPORT int LQ_CALL LqFileMakeDir(const char* lqacp lqain NewDirName, int Access);
LQ_IMPORTEXPORT int LQ_CALL LqFileMakeSubdirs(const char* lqacp lqain NewSubdirsDirName, int Access);
LQ_IMPORTEXPORT int LQ_CALL LqFileRemoveDir(const char* lqacp lqain NewDirName);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileRealPath(const char* lqain lqacp Source, char* lqaout lqacp Dest, intptr_t DestLen);

LQ_IMPORTEXPORT int LQ_CALL LqFileFlush(int Fd);

/*
* Version of unix poll for Windows.
* You must send to function only native descriptors (Not CRT).
* Supports:
*   +Read/Write events
*   +Any type of Read/Write file types (sockets, files, pipes, ...)
*   +Event objects (when have signal simulate POLLIN and POLLOUT)
*   +Supports disconnect event POLLHUP (but use only with POLLIN or POLLOUT)
*   -In widows only can be only use with LQ_O_NONBLOCK creation parametr

* In unix, function it behaves just like a standard poll
*/
LQ_IMPORTEXPORT int LQ_CALL LqFilePollCheck(LqFilePoll* Fds, size_t CountFds, LqTimeMillisec TimeoutMillisec);

/*
*Event
* This event can be pass in to LqFilePollCheck for intterupt waiting.
* destroy by LqFileClose function
*  @InheritFlags: Can be LQ_O_NOINHERIT or LQ_O_INHERIT
*  @return: new event or -1;
*   Note: Use LQ_POLLIN for LqFilePoll or LQEVNT_FLAG_RD for LqEvnt.
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileEventCreate(int InheritFlag);
LQ_IMPORTEXPORT int LQ_CALL LqFileEventSet(int FileEvent);
/*
* return: -1 - error, 0 - resetted and previous state == 0, 1 - resetted and previous state == 1
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileEventReset(int FileEvent);

/*------------------------------------------
* Timer
*  @InheritFlags: Can be LQ_O_NOINHERIT or LQ_O_INHERIT
*  @return: new  timer event or -1;
*   Note: Use LQ_POLLIN for LqFilePoll or LQEVNT_FLAG_RD for LqEvnt.
*   When timer event has been set in signal state you must set LqFileTimerSet for new period
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileTimerCreate(int InheritFlags);
LQ_IMPORTEXPORT int LQ_CALL LqFileTimerSet(int TimerFd, LqTimeMillisec Time);

/*------------------------------------------
* Pipes
*
*/
LQ_IMPORTEXPORT int LQ_CALL LqFilePipeCreate(int* lqaout lpReadPipe, int* lqaout lpWritePipe, uint32_t FlagsRead, uint32_t FlagsWrite);
LQ_IMPORTEXPORT int LQ_CALL LqFilePipeCreateRw(int* lqaout Pipe1, int* lqaout Pipe2, uint32_t Flags1, uint32_t Flags2);
LQ_IMPORTEXPORT int LQ_CALL LqFilePipeCreateNamed(const char* lqacp lqain NameOfPipe, uint32_t Flags);

/*------------------------------------------
* Descriptor parameters
* @Descriptor: Input descriptor
* @IsInherit: LQ_O_NOINHERIT - if handle not transfer to child process
* @return: New descriptor or -1 when have error
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileDescrDup(int Descriptor, int InheritFlag);
LQ_IMPORTEXPORT int LQ_CALL LqFileDescrSetInherit(int Descriptor, int IsInherit);

/*
* Set standart descriptors
* @Descriptor: new descriptor
* @StdNo: LQ_STDERR, LQ_STDIN or LQ_STDOUT
* @return: -1 - on error, 0 - on success
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileDescrDupToStd(int Descriptor, int StdNo);

/*------------------------------------------
* Process
* LqFileProcessCreate
*  @FileName - Path to executible image
*  @Argv: Arguments to programm. If set nullptr, then args not sended, otherwise before last arg must have nullptr
*  @Envp: Enviroment arg. If set nullptr, then used enviroment parent process.
*  @WorkingDir: Set current dir for new process
*  @StdIn: -1 - use std in of current process, otherwise use spec. dev or pipe or file. You can use null devices Ex. LqFileOpen(LQ_NULLDEV, LQ_O_RD, 0)
*  @StdOut: -1 - use std out of current process, otherwise use spec. dev or pipe or file. You can use null devices Ex. LqFileOpen(LQ_NULLDEV, LQ_O_WR, 0)
*  @StdErr: -1 - use std err of current process, otherwise use spec. dev or pipe or file. You can use null devices Ex. LqFileOpen(LQ_NULLDEV, LQ_O_WR, 0)
*  @StdDscr: If set, then returned standart in/out pipes to child process. If not set, then uses parent std in/out.
*  @EventKill: Is set non null, get event completion for process. For correct get event on all platforms use flags
        (LQ_POLLHUP | LQ_POLLIN) for LqFilePoll or (LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP) for LqEvnt.
*  @return: PID to new process, or -1 is have error.
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileProcessCreate(
    const char* lqain lqacp FileName,
    char* const lqaopt lqain Argv[],
    char* const lqaopt lqain Envp[],
    const char* lqaopt lqain lqacp WorkingDir,
    int StdIn,
    int StdOut,
    int StdErr,
    int* lqaopt lqaout EventKill
);
/*
* LqFileProcessKill
*  @Pid: pid to process
*  @return: 0 - is success, -1 otherwise
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileProcessKill(int Pid);

LQ_IMPORTEXPORT int LQ_CALL LqFileProcessId();

LQ_IMPORTEXPORT int LQ_CALL LqFileProcessParentId();

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFileProcessName(int Pid, char* lqacp lqaout DestBuf, intptr_t SizeBuf);

/*------------------------------------------
* Change or get current working directory
* Set current working directory
*   @return: -1 - on error, 0 - on success
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileSetCurDir(const char* lqacp lqain NewDir);
/* Get current working directory
*   @return: -1 - on error, 0 - on success
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileGetCurDir(char* lqacp lqaout DirBuf, size_t LenBuf);
/*------------------------------------------
* Start enumerate path
*  @retur: -1 is end enum, 0 - is have path
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileEnmStart(LqFileEnm* lqaio Enm, const char* lqacp lqain Dir, char* lqaout lqacp DestName, size_t NameLen, uint8_t* lqaout lqaopt Type);
LQ_IMPORTEXPORT int LQ_CALL LqFileEnmNext(LqFileEnm* lqaio Enm, char* lqaout lqacp DestName, size_t NameLen, uint8_t* lqaout lqaopt Type);
LQ_IMPORTEXPORT void LQ_CALL LqFileEnmBreak(LqFileEnm* lqaio Enm);

/*------------------------------------------
* LqFilePathEvnt
*  Event directory changes
*/

LQ_IMPORTEXPORT int LQ_CALL LqFilePathEvntCreate(LqFilePathEvnt* lqaio Evnt, const char* lqain lqacp DirOrFile, uint8_t FollowFlag);
LQ_IMPORTEXPORT void LQ_CALL LqFilePathEvntFreeEnum(LqFilePathEvntEnm** lqaio Dest);
LQ_IMPORTEXPORT int LQ_CALL LqFilePathEvntDoEnum(LqFilePathEvnt* lqaio Evnt, LqFilePathEvntEnm** lqaio Dest);
LQ_IMPORTEXPORT int LQ_CALL LqFilePathEvntGetName(LqFilePathEvnt* lqaio Evnt, char* lqaout lqacp DestName, size_t DestNameSize);
LQ_IMPORTEXPORT void LQ_CALL LqFilePathEvntFree(LqFilePathEvnt* lqaio Evnt);


/*
* Craete terminal pair(usually use for create child process)
*  @MasterFd: Use in parrent process
*  @SlaveFd: Use in child proccess
*  @MasterFlags: Flags for master descriptor(LQ_O_NOINHERIT, LQ_O_NONBLOCK)
*  @SlaveFlags: Flags for slafe descriptor(LQ_O_NOINHERIT, LQ_O_NONBLOCK)
*  @return: 0 - on success, -1 - on error
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileTermPairCreate(int* lqaout MasterFd, int* lqaout SlaveFd, int MasterFlags, int SlaveFlags);

LQ_IMPORTEXPORT bool LQ_CALL LqFileDirIsRoot(const char* DirOrFile);


/*------------------------------------------
* Multiplatform environment (Used code page from LqCp...)
*/
/*LqFileSetEnv
*  @Name: Name of env. var
*  @Value: New value, or when == nullptr unsetted var
*  @return: -1 - on error, 0 - on success
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileSetEnv(const char* lqain Name, const char* lqain Value);

/*LqFileGetEnv
*  @Name: Name of env. var
*  @Value: Buffer for value
*  @ValueBufLen: Size of @Value buffer
*  @return: -1 - on error, otherwise count written bytes
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileGetEnv(const char* lqain Name, char* lqaout Value, size_t ValueBufLen);

/*LqFileGetEnvs
*  @Buf: Dest buffer (Name1=Value1\0Name2=Value2\0\0)
*  @BufLen: Length of @Buf
*  @return: -1 - on error, otherwise count variables written
*/
LQ_IMPORTEXPORT int LQ_CALL LqFileGetEnvs(char* lqaout Buf, size_t BufLen);

/*------------------------------------------
* Shared memory
*
*/

LQ_IMPORTEXPORT int LQ_CALL LqFileSharedCreate(int key, size_t Size, int DscrFlags, int UserAccess);
LQ_IMPORTEXPORT int LQ_CALL LqFileSharedOpen(int key, size_t Size, int DscrFlags, int UserAccess);
LQ_IMPORTEXPORT void* LQ_CALL LqFileSharedAt(int shmid, void* lqain lqaopt BaseAddress);
LQ_IMPORTEXPORT int LQ_CALL LqFileSharedUnmap(void* lqain addr);
LQ_IMPORTEXPORT int LQ_CALL LqFileSharedClose(int shmid);

LQ_EXTERN_C_END


#endif
