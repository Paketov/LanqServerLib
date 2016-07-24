#pragma once

/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqTime... - Typical time functions. (C++ version)
*/


#include "LqDef.hpp"
#include "LqTime.h"

LQ_EXTERN_CPP_BEGIN

LQ_IMPORTEXPORT LqString LQ_CALL LqTimeDiffSecToStlStr(LqTimeSec t1, LqTimeSec t2);
LQ_IMPORTEXPORT LqString LQ_CALL LqTimeDiffMillisecToStlStr(LqTimeMillisec t1, LqTimeMillisec t2);

LQ_IMPORTEXPORT LqString LQ_CALL LqTimeLocSecToStlStr(LqTimeSec t);
LQ_IMPORTEXPORT LqString LQ_CALL LqTimeGmtSecToStlStr(LqTimeSec t);

LQ_EXTERN_CPP_END