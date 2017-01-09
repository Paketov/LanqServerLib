#include "LqLog.h"
#include "LqSbuf.h"
#include "LqLock.hpp"

#include <stdarg.h>



static LqFwbuf LogInfoBuf;
static LqFwbuf LogErr;


void LQ_LOG_INFO(const char* Fmt, ...) {
#ifdef LQ_LOG_USER_HAVE
	va_list va;
	va_start(va, Fmt);
	LqFwbuf_vprintf(&LogUserBuf, Fmt, va);
	va_end(va);
#endif
}

void LQ_LOG_ERR(const char* Fmt, ...) {
#ifdef LQ_ERR_HAVE
	va_list va;
	va_start(va, Fmt);
	LqFwbuf_vprintf(&LogErr, Fmt, va);
	va_end(va);
#endif
}