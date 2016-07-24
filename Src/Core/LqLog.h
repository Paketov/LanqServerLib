/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LQ_LOG or LQ_ERR - Minimum logging implementation.
*/


#ifndef __LQ_LOG_H_HAS_INCLUDED__
#define __LQ_LOG_H_HAS_INCLUDED__


void LQ_LOG_DEBUG(const char* val, ...);
void LQ_LOG_USER(const char* val, ...);
void LQ_ERR(const char* val, ...);

#endif
