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

struct LqConn;
struct LqProto;


#define LQCONN_FLAG_RD                          ((LqConnFlag)1)         /*Ready for read*/
#define LQCONN_FLAG_WR                          ((LqConnFlag)2)         /*Ready for write*/
#define LQCONN_FLAG_HUP                         ((LqConnFlag)4)         /*Connection lost or can been closed by client*/
#define LQCONN_FLAG_RDHUP                       ((LqConnFlag)8)         /*Connection lost or can been closed by client*/
#define LQCONN_FLAG_END                         ((LqConnFlag)16)    /*Connection partially has been closed*/
#define LQCONN_FLAG_LOCK                        ((LqConnFlag)32)        /*Connection has been locket by app. protocol*/

#define _LQCONN_FLAG_RESERVED_1         ((LqConnFlag)64)        /*Use only for windows internal*/
#define _LQCONN_FLAG_RESERVED_2         ((LqConnFlag)128)       /*Use only for windows internal*/

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqConn
{
    LqProto*            Proto;  /*Server registration*/
    int                         SockDscr;       /*Sock descriptor*/
    LqConnFlag          Flag;
};
#pragma pack(pop)

/*
*       Use for protocol level registration
*/

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqProto
{
    void* LqWorkerBoss;
    void* UserData;

    LqFileSz MaxSendInTact;
    LqFileSz MaxSendInSingleTime;
    LqFileSz MaxReciveInSingleTime;

    /*
    * Call for create new connection.
    */
    LqConn* (LQ_CALL *NewConnProc)(LqProto* This, int ConnectionDescriptor, void* Address/*sockaddr*/);
    /*
    * Read notifycation.
    */
    void (LQ_CALL *ReciveProc)(LqConn* Connection);
    /*
    * Write notifycation.
    */
    void (LQ_CALL *WriteProc)(LqConn* Connection);
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

    void (LQ_CALL *FreeProtoNotifyProc)(LqProto* This);

};


#pragma pack(pop)

LQ_EXTERN_C_END

#endif
