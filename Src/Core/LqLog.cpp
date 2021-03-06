#include "LqLog.h"
#include "LqSbuf.h"
#include "LqLock.hpp"

#include <stdarg.h>



static LqFbuf LogInfoBuf;
static LqFbuf LogErr;


void LqLogInfo(const char* Fmt, ...) {
#ifdef LQ_LOG_USER_HAVE
	va_list va;
	va_start(va, Fmt);
	LqFbuf_vprintf(&LogUserBuf, Fmt, va);
	va_end(va);
#endif
}

void LqLogErr(const char* Fmt, ...) {
#ifdef LQ_ERR_HAVE
	va_list va;
	va_start(va, Fmt);
	LqFbuf_vprintf(&LogErr, Fmt, va);
	va_end(va);
#endif
}