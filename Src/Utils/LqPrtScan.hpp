#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqPrtScan (LanQ Port Scanner) - Scan ports of local or remote machine. Creates sockets and just trying connect
*/

#include "LqTime.hpp"
#include "LqConn.h"
#include "LqDef.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <type_traits>
#include <vector>

LQ_EXTERN_CPP_BEGIN

LQ_IMPORTEXPORT bool LQ_CALL LqPrtScanDo(LqConnInetAddress* Addr, std::vector<std::pair<uint16_t, uint16_t>>& PortRanges, int MaxScanConn, LqTimeMillisec ConnWait, std::vector<uint16_t>& OpenPorts);

LQ_EXTERN_CPP_END