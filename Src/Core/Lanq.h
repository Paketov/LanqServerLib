/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* Main module of core.
* LqProto - descriptor of top level protocol.
* LqConn - descriptor of base connection. Must creates in LqProto::NewConnProc. Deletes only in LqProto::EndConnProc.
*/


#ifndef __LANQ_H_HAS_INCLUDED__
#define __LANQ_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqDef.h"

LQ_EXTERN_C_BEGIN

struct LqEvntHdr;
struct LqConn;
struct LqEvntFd;
struct LqProto;

typedef struct LqEvntHdr LqEvntHdr;
typedef struct LqConn LqConn;
typedef struct LqEvntFd LqEvntFd;
typedef struct LqProto LqProto;

#define LQEVNT_FLAG_RD                          ((LqEvntFlag)1)         /*Ready for read*/
#define LQEVNT_FLAG_WR                          ((LqEvntFlag)2)         /*Ready for write*/
#define LQEVNT_FLAG_HUP                         ((LqEvntFlag)4)         /*Connection lost or can been closed by client*/
#define LQEVNT_FLAG_RDHUP                       ((LqEvntFlag)8)         /*Connection lost or can been closed by client*/
#define LQEVNT_FLAG_END                         ((LqEvntFlag)16)        /*Want end session*/
#define LQEVNT_FLAG_ERR                         ((LqEvntFlag)32)        /*Have error in event descriptor*/

#define _LQEVNT_FLAG_SYNC                       ((LqEvntFlag)64)        /*Use for check sync*/
#define _LQEVNT_FLAG_NOW_EXEC                   ((LqEvntFlag)128)       /*Exec by protocol handles*/
#define _LQEVNT_FLAG_USER_SET                   ((LqEvntFlag)256)       /*Is set by user*/
#define _LQEVNT_FLAG_CONN                       ((LqEvntFlag)1024)      /*Use for check sync*/

#define _LQEVNT_FLAG_RESERVED_1                 ((LqEvntFlag)2048)      /*Use only for windows internal*/
#define _LQEVNT_FLAG_RESERVED_2                 ((LqEvntFlag)4096)      /*Use only for windows internal*/

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)


#define LQ_CONN_COMMON_EVNT_HDR                         \
    LqEvntFlag          Flag;                           \
    int                 Fd      /*Sock descriptor*/


struct LqEvntHdr
{
    LQ_CONN_COMMON_EVNT_HDR;
};

/*
* Use this structure basically with sockets
*/
struct LqConn
{
    LQ_CONN_COMMON_EVNT_HDR;
    LqProto*            Proto;      /*Server registration*/
};

typedef void (LQ_CALL *LqEvntFdHandlerFn)(LqEvntFd* Fd, LqEvntFlag RetFlags);


/*
* Use this structure for different type of handles
*/
struct LqEvntFd
{
    LQ_CONN_COMMON_EVNT_HDR;
    LqEvntFdHandlerFn   Handler;
    LqEvntFdHandlerFn   CloseHandler;
    uintptr_t           UserData;
#if defined(LQPLATFORM_WINDOWS)
    struct
    {
        union
        {
            long        Status;
            void*       Pointer;
        };
        uintptr_t       Information;
    } __Reserved1, __Reserved2;    /*IO_STATUS_BLOCK for read/write check in windows*/
#endif
};

#define LqEvntIsConn(Hdr) (((LqEvntHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN)
#define LqEvntToConn(Hdr) ((((LqEvntHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN)? ((LqConn*)(Hdr)): ((LqConn*)nullptr))
#define LqEvntToFd(Hdr) ((((LqEvntHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN)? ((LqEvntFd*)nullptr): ((LqEvntFd*)(Hdr)))

#pragma pack(pop)

/*
*       Use for protocol level registration
*/

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqProto
{
    void* UserData;

    LqFileSz MaxSendInTact;
    LqFileSz MaxSendInSingleTime;
    LqFileSz MaxReciveInSingleTime;

    /*
    * Read notification.
    */
    void (LQ_CALL *ReciveProc)(LqConn* Connection);
    /*
    * Write notification.
    */
    void (LQ_CALL *WriteProc)(LqConn* Connection);

    /*
    * Error notification.
    */
    void (LQ_CALL *ErrorProc)(LqConn* Connection);
    /*
    * Close and delete connection
    */
    void (LQ_CALL *EndConnProc)(LqConn* Connection);

    bool (LQ_CALL *CmpAddressProc)(LqConn* Connection, const void* Address);
    /*
            Close connection on time out.
            If returned true, remove connection from worker list
    */
    bool (LQ_CALL *KickByTimeOutProc)
    (
        LqConn* Connection,
        LqTimeMillisec CurrentTimeMillisec/*Use fast get current time in millisec*/,
        LqTimeMillisec EstimatedLiveTime /*Otional input parametr*/
    );
    /*
    This procedure must return pointer on dynamic string or NULL.
    !!! Function-receiver delete this string herself. !!!
    */
    char* (LQ_CALL *DebugInfoProc)(LqConn* Connection);

};


#pragma pack(pop)

LQ_EXTERN_C_END

#endif
