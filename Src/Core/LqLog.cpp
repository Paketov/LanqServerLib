#include "LqLog.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>


void LQ_LOG_DEBUG(const char* val, ...)
{
#ifdef LQ_LOG_DEBUG_HAVE
    va_list va;
    va_start(va, val);
    vfprintf(stdout, val, va);
#endif
}


void LQ_LOG_USER(const char* val, ...)
{
#ifdef LQ_LOG_USER_HAVE
    va_list va;
    va_start(va, val);
    vfprintf(stdout, val, va);
#endif
}

void LQ_ERR(const char* val, ...)
{
#ifdef LQ_ERR_HAVE
    va_list va;
    va_start(va, val);
    vfprintf(stderr, val, va);
#endif
}