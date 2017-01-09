/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LQ_LOG or LQ_LOG_ERR - Minimum logging implementation.
*/


#ifndef __LQ_LOG_H_HAS_INCLUDED__
#define __LQ_LOG_H_HAS_INCLUDED__


//#define LQ_LOG_DEBUG_HAVE
//#define LQ_LOG_USER_HAVE
//#define LQ_ERR_HAVE

void LQ_LOG_INFO(const char* val, ...);
void LQ_LOG_ERR(const char* val, ...);

#endif
