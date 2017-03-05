/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LQ_LOG or LqLogErr - Minimum logging implementation.
*/


#ifndef __LQ_LOG_H_HAS_INCLUDED__
#define __LQ_LOG_H_HAS_INCLUDED__


//#define LQ_LOG_DEBUG_HAVE
//#define LQ_LOG_USER_HAVE
//#define LQ_ERR_HAVE

void LqLogInfo(const char* val, ...);
void LqLogErr(const char* val, ...);

#endif
