/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpAct... - Functions for work with action.
*/


#ifndef __LANQ_HTTP_ACT_H_HAS_INCLUDED__
#define __LANQ_HTTP_ACT_H_HAS_INCLUDED__

#include "LqHttp.h"
#include "LqOs.h"

#if defined(LANQBUILD)
void LqHttpActSwitchToRcv(LqHttpConn* c);
#endif

LQ_EXTERN_C_BEGIN

#define LqHttpActGetClassByConn(Conn) ((Conn)->ActionState & (LqHttpActState)0xC0)

/*
*       Action control.
*/
LQ_IMPORTEXPORT void LQ_CALL LqHttpActSwitchToRsp(LqHttpConn* c);
LQ_IMPORTEXPORT void LQ_CALL LqHttpActSwitchToClose(LqHttpConn* c);
LQ_IMPORTEXPORT char* LQ_CALL LqHttpActSwitchToRspAndSetStartLine(LqHttpConn* c, int StatusCode);
/*Manual recive body*/
LQ_IMPORTEXPORT void LQ_CALL LqHttpActSwitchToManualRcv(LqHttpConn* c);
LQ_IMPORTEXPORT void LQ_CALL LqHttpActSwitchToManualRsp(LqHttpConn* c);
LQ_IMPORTEXPORT void LQ_CALL LqHttpActKeepOnlyHeaders(LqHttpConn* c);


LQ_EXTERN_C_END

#endif