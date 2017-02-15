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
#include <stdint.h>

LQ_EXTERN_C_BEGIN

struct LqEvntHdr;
struct LqConn;
struct LqEvntFd;
struct LqProto;

typedef struct LqEvntHdr LqEvntHdr;
typedef struct LqConn LqConn;
typedef struct LqEvntFd LqEvntFd;
typedef struct LqProto LqProto;

#define LQEVNT_FLAG_RD                          ((LqEvntFlag)1)          /*Ready for read*/
#define LQEVNT_FLAG_WR                          ((LqEvntFlag)2)          /*Ready for write*/
#if defined(LQPLATFORM_WINDOWS)
#define LQEVNT_FLAG_CONNECT                     ((LqEvntFlag)1024)       /*Ready for connect*/
#define LQEVNT_FLAG_ACCEPT                      ((LqEvntFlag)2048)       /*Ready for accept client*/
#else
#define LQEVNT_FLAG_CONNECT                     LQEVNT_FLAG_WR           /*Ready for read*/
#define LQEVNT_FLAG_ACCEPT                      LQEVNT_FLAG_RD           /*Ready for write*/
#endif

#define LQEVNT_FLAG_HUP                         ((LqEvntFlag)4)          /*Connection lost or can been closed by client*/
#define LQEVNT_FLAG_RDHUP                       ((LqEvntFlag)8)          /*Connection lost or can been closed by client*/
#define LQEVNT_FLAG_END                         ((LqEvntFlag)16)         /*Want end session*/
#define LQEVNT_FLAG_ERR                         ((LqEvntFlag)32)         /*Have error in event descriptor*/

#define _LQEVNT_FLAG_ONLY_ONE_BOSS              ((LqEvntFlag)64)         /*When move to another boss, calling close handler*/
#define _LQEVNT_FLAG_SYNC                       ((LqEvntFlag)128)        /*Use for check sync*/
#define _LQEVNT_FLAG_NOW_EXEC                   ((LqEvntFlag)256)        /*Exec by handles*/
#define _LQEVNT_FLAG_CONN                       ((LqEvntFlag)512)        /*Use for check is connection*/



#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)


#define LQ_CONN_COMMON_EVNT_HDR                         \
    LqEvntFlag          Flag;                           \
	uint16_t			Lk;								\
    int                 Fd;      /*Sock descriptor*/	\
	void*		        WrkOwner;



struct LqEvntHdr {
    LQ_CONN_COMMON_EVNT_HDR;
};

/*
* Use this structure basically with sockets
*/
struct LqConn {
    LQ_CONN_COMMON_EVNT_HDR;
    LqProto*            Proto;      /*Protocol registartion(or maybe just handlers)*/
};


/*
* Use this structure for different type of handles
*/
struct LqEvntFd {
    LQ_CONN_COMMON_EVNT_HDR;
    void (LQ_CALL      *Handler)(LqEvntFd* Fd, LqEvntFlag RetFlags);
    void (LQ_CALL      *CloseHandler)(LqEvntFd* Fd);
#if defined(LQPLATFORM_WINDOWS)
    struct {
        union {
            long        Status;
            void*       Pointer;
        };
        uintptr_t       Information;
    } __Reserved1, __Reserved2;    /*IO_STATUS_BLOCK for read/write check in windows*/
#endif
};

#define LqEvntIsConn(Hdr) (((LqEvntHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN)
#define LqEvntToConn(Hdr) ((((LqEvntHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN)? ((LqConn*)(Hdr)): ((LqConn*)NULL))
#define LqEvntToFd(Hdr) ((((LqEvntHdr*)(Hdr))->Flag & _LQEVNT_FLAG_CONN)? ((LqEvntFd*)NULL): ((LqEvntFd*)(Hdr)))

#pragma pack(pop)

/*
*       Use for protocol level registration
*/

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqProto {
    void* UserData;

    LqFileSz MaxSendInTact;
    LqFileSz MaxSendInSingleTime;
    LqFileSz MaxReciveInSingleTime;

    /*
    * Read, Write and Error notification.
    */
    void(LQ_CALL  *Handler)(LqConn* Connection, LqEvntFlag RetFlags);

    /*
    * Close and delete connection
    */
    void(LQ_CALL  *CloseHandler)(LqConn* Conn);

    bool (LQ_CALL *CmpAddressProc)(LqConn* Conn, const void* Address);
    /*
    * Close connection on time out.
    * If returned true, remove connection from worker list
    */
    bool (LQ_CALL *KickByTimeOutProc)(
        LqConn*        Connection,
        LqTimeMillisec CurrentTimeMillisec/*Use fast get current time in millisec*/,
        LqTimeMillisec EstimatedLiveTime /*Otional input parametr*/
    );
    /*
    * This procedure must return pointer on dynamic string or NULL.
    * !!! Function-receiver delete this string herself. !!!
    */
    char* (LQ_CALL *DebugInfoProc)(LqConn* Conn);

};


#pragma pack(pop)

LQ_EXTERN_C_END

#endif
