#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqTime... - Typical time functions. (C version)
*/

#ifndef __LQ_TIME_H_HAS_INCLUDED__
#define __LQ_TIME_H_HAS_INCLUDED__

#include "LqDef.h"
#include <time.h>
#include <sys/types.h>
#include <wchar.h>

LQ_EXTERN_C_BEGIN

static const char *LqTimeWeeks[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *LqTimeMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const auto LqTimeGmtStrLen = sizeof("Mon, 28 Sep 1970 06:00:00 GMT");
static const auto LqTimeStrLen = sizeof("Wed Feb 13 16:06:10 2013");

#define PRINTF_TIME_TM_FORMAT_GMT "%3s, %02i %3s %i %02i:%02i:%02i GMT"

#define PRINTF_TIME_TM_ARG_GMT(Tm) \
		(char*)LqTimeWeeks[(Tm).tm_wday],\
		(int)(Tm).tm_mday,\
		(char*)LqTimeMonths[(Tm).tm_mon],\
		(int)((Tm).tm_year + 1900),\
		(int)(Tm).tm_hour,\
		(int)(Tm).tm_min,\
		(int)(Tm).tm_sec


#define PRINTF_TIME_TM_FORMAT "%3s %3s %02i %02i:%02i:%02i %i"

#define PRINTF_TIME_TM_ARG(Tm) \
		(char*)LqTimeWeeks[(Tm).tm_wday],\
		(char*)LqTimeMonths[(Tm).tm_mon],\
		(int)(Tm).tm_mday,\
		(int)(Tm).tm_hour,\
		(int)(Tm).tm_min,\
		(int)(Tm).tm_sec,\
		(int)((Tm).tm_year + 1900)


LQ_IMPORTEXPORT int LQ_CALL LqTimeStrToLocTm(const char* Str, tm* Result);
LQ_IMPORTEXPORT int LQ_CALL LqTimeStrToGmtTm(const char * Str, tm* Result);

LQ_IMPORTEXPORT int LQ_CALL LqTimeStrToLocSec(const char* Str, LqTimeSec* Result);
LQ_IMPORTEXPORT int LQ_CALL LqTimeStrToGmtSec(const char* Str, LqTimeSec* Result);

LQ_IMPORTEXPORT bool LQ_CALL LqTimeGmtTmToStr(char* DestStr, LqTimeSec DestStrLen, const tm* InTmGmt);
LQ_IMPORTEXPORT bool LQ_CALL LqTimeLocTmToStr(char* DestStr, LqTimeSec DestStrLen, const tm* InTm);

LQ_IMPORTEXPORT bool LQ_CALL LqTimeLocToStr(char* DestStr, LqTimeSec DestStrLen);
LQ_IMPORTEXPORT bool LQ_CALL LqTimeGmtToStr(char* DestStr, LqTimeSec DestStrLen);

LQ_IMPORTEXPORT bool LQ_CALL LqTimeGmtSecToStr(char* DestStr, size_t DestStrLen, LqTimeSec FileTimeGmt);
LQ_IMPORTEXPORT bool LQ_CALL LqTimeLocSecToStr(char* DestStr, size_t DestStrLen, LqTimeSec FileTime);

LQ_IMPORTEXPORT void LQ_CALL LqTimeLocSecToLocTm(tm* Tm, LqTimeSec LocalTime);
LQ_IMPORTEXPORT void LQ_CALL LqTimeLocSecToGmtTm(tm* Tm, LqTimeSec LocalTime);

LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqTimeLocSecToGmtSec(LqTimeSec LocalTime);
LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqTimeGmtSecToLocSec(LqTimeSec GmtTime);

/*
* Get current times.
*/

LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqTimeGetGmtCorrection();
LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqTimeSetGmtCorrection(LqTimeSec NewCorrection);

LQ_IMPORTEXPORT void LQ_CALL LqTimeGetLocTm(tm* Tm);
LQ_IMPORTEXPORT void LQ_CALL LqTimeGetGmtTm(tm* Tm);

LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqTimeGetLocSec();
LQ_IMPORTEXPORT LqTimeSec LQ_CALL LqTimeGetGmtSec();

LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqTimeGetLocMillisec();
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqTimeGetGmtMillisec();
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqTimeGetMaxMillisec();

LQ_EXTERN_C_END

#endif