/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqTime... - Typical time functions.
*/

#include "LqOs.h"
#include "LqDef.hpp"
#include "LqStr.hpp"
#include "LqTime.h"

#include <chrono>
#include <sys/timeb.h>
#include <string.h>
#if defined(LQPLATFORM_ANDROID)
#include <sys/time.h> 
#endif

static LqTimeSec GmtCorrection = 0;

LQ_EXTERN_C LqTimeSec LQ_CALL LqTimeGetGmtCorrection()
{
    return GmtCorrection;
}

LQ_EXTERN_C LqTimeSec LQ_CALL LqTimeSetGmtCorrection(LqTimeSec NewCorrection)
{
    return GmtCorrection = NewCorrection;
}

LQ_EXTERN_CPP LqString LQ_CALL LqTimeDiffSecToStlStr(LqTimeSec t1, LqTimeSec t2)
{
    time_t r = t2 - t1;
    time_t Years = r / (365 * 24 * 60 * 60);
    r %= (365 * 24 * 60 * 60);
    time_t Days = r / (24 * 60 * 60);
    r %= (24 * 60 * 60);
    time_t Hours = r / (60 * 60);
    r %= (60 * 60);
    time_t Minutes = r / 60;
    r %= 60;
    time_t Seconds = r;

    bool k = false;
    LqString str_r;
    if(Years != 0) 
    str_r = LqToString(Years) + " years ", k = true;
    if((Days != 0) || k) 
    str_r += LqToString(Days) + " days ", k = true;
    char Buf[30];
    sprintf(Buf, "%02i:%02i:%02i", (int)Hours, (int)Minutes, (int)Seconds);
    return str_r + Buf;
}

LQ_EXTERN_CPP LqString LQ_CALL LqTimeDiffMillisecToStlStr(LqTimeMillisec t1, LqTimeMillisec t2)
{
    char Buf[10];
    sprintf(Buf, ":%03i", (int)((t2 - t1) % 1000));
    return LqTimeDiffSecToStlStr(t1 / 1000, t2 / 1000) + Buf;
}

LQ_EXTERN_CPP LqString LQ_CALL LqTimeLocSecToStlStr(LqTimeSec t)
{
    char b[30]; b[0] = '\0';
    tm Tm;
    LqTimeLocSecToLocTm(&Tm, t);
    sprintf(b, PRINTF_TIME_TM_FORMAT, PRINTF_TIME_TM_ARG(Tm));
    auto l = LqStrLen(b);
    if(l > 0) b[l - 1] = '\0';
    return b;
}

LQ_EXTERN_CPP LqString LQ_CALL LqTimeGmtSecToStlStr(LqTimeSec t)
{
    char Buf[100];
    t += GmtCorrection;
    time_t Tt = t;
    auto Tm = gmtime(&Tt);
    sprintf(Buf, PRINTF_TIME_TM_FORMAT_GMT, PRINTF_TIME_TM_ARG_GMT(*Tm));
    return Buf;
}

LQ_EXTERN_C int LQ_CALL LqTimeStrToLocTm(const char* Str, struct tm* Result)
{
    tm OutTm;
    int n = 0;
    int t;
    char MonthBuf[4] = {'\0'}, WeekBuf[4] = {'\0'};
    for(; ((Str[n] >= 'a') && (Str[n] <= 'z') || (Str[n] >= 'A') && (Str[n] <= 'Z')) && (n < 3); n++)
    WeekBuf[n] = Str[n];
    if(Str[n] != ' ') return -1;
    n++;
    for(int i = 0; ((Str[n] >= 'a') && (Str[n] <= 'z') || (Str[n] >= 'A') && (Str[n] <= 'Z')) && (i < 3); n++, i++)
    MonthBuf[i] = Str[n];
    if(Str[n] != ' ') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_mday, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ' ') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_hour, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ':') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_min, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ':') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_sec, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ' ') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_year, Str + n)) < 1) return -1;
    n += t;
    OutTm.tm_year -= 1900;
    OutTm.tm_mon = OutTm.tm_wday = -1;
    for(uint i = 0; i < (sizeof(LqTimeWeeks) / sizeof(LqTimeWeeks[0])); i++)
    if(*(uint32_t*)WeekBuf == *(uint32_t*)(LqTimeWeeks[i]))
    {
        OutTm.tm_wday = i;
        break;
    }
    if(OutTm.tm_wday == -1) return -1;
    for(uint i = 0; i < (sizeof(LqTimeMonths) / sizeof(LqTimeMonths[0])); i++)
    if(*(uint32_t*)MonthBuf == *(uint32_t*)(LqTimeMonths[i]))
    {
        OutTm.tm_mon = i;
        break;
    }
    if(OutTm.tm_mon == -1) return -1;
    OutTm.tm_isdst = -1;
    OutTm.tm_yday = -1;
    *Result = OutTm;
    return n;
}


LQ_EXTERN_C int LQ_CALL LqTimeStrToGmtTm(const char * Str, struct tm* Result)
{
    tm OutTm;
    int n = 0;
    int t;
    char MonthBuf[4] = {'\0'}, WeekBuf[4] = {'\0'};

    for(; ((Str[n] >= 'a') && (Str[n] <= 'z') || (Str[n] >= 'A') && (Str[n] <= 'Z')) && (n < 3); n++)
    WeekBuf[n] = Str[n];
    if(Str[n] != ',') return -1;
    n++;
    if(Str[n] != ' ') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_mday, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ' ') return -1;
    n++;
    for(int i = 0; ((Str[n] >= 'a') && (Str[n] <= 'z') || (Str[n] >= 'A') && (Str[n] <= 'Z')) && (i < 3); n++, i++)
    MonthBuf[i] = Str[n];
    if(Str[n] != ' ') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_year, Str + n)) < 1) return -1;
    n += t;
    if(Str[n] != ' ') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_hour, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ':') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_min, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ':') return -1;
    n++;
    if((t = LqStrToInt(&OutTm.tm_sec, Str + n)) < 2) return -1;
    n += t;
    if(Str[n] != ' ') return -1;
    n++;
    for(int i = 0; i < 3; n++, i++)
    if(Str[n] != "GMT"[i])
        return -1;
    OutTm.tm_year -= 1900;
    OutTm.tm_mon = OutTm.tm_wday = -1;
    for(uint i = 0; i < (sizeof(LqTimeWeeks) / sizeof(LqTimeWeeks[0])); i++)
    if(*(uint32_t*)WeekBuf == *(uint32_t*)(LqTimeWeeks[i]))
    {
        OutTm.tm_wday = i;
        break;
    }
    if(OutTm.tm_wday == -1) return -1;
    for(uint i = 0; i < (sizeof(LqTimeMonths) / sizeof(LqTimeMonths[0])); i++)
    if(*(uint32_t*)MonthBuf == *(uint32_t*)(LqTimeMonths[i]))
    {
        OutTm.tm_mon = i;
        break;
    }
    if(OutTm.tm_mon == -1) return -1;
    OutTm.tm_isdst = -1;
    OutTm.tm_yday = -1;
    *Result = OutTm;
    return n;
}


LQ_EXTERN_C int LQ_CALL LqTimeStrToLocSec(const char* Str, LqTimeSec* Result)
{
    tm Tm;
    int r;
    if((r = LqTimeStrToLocTm(Str, &Tm)) < 0)
    return -1;
    *Result = mktime(&Tm);
    return r;
}

LQ_EXTERN_C int LQ_CALL LqTimeStrToGmtSec(const char* Str, LqTimeSec* Result)
{
    tm Tm;
    int r;
    if((r = LqTimeStrToGmtTm(Str, &Tm)) < 0)
    return -1;
#if !defined(LQPLATFORM_ANDROID)
    timeb t;
    ftime(&t);
    *Result = mktime(&Tm) - (t.timezone * 60) + GmtCorrection;
#else
    struct timezone tz;
    timeval tv;
    gettimeofday(&tv, &tz);
    *Result = mktime(&Tm) - (tz.tz_minuteswest * 60) + GmtCorrection;
#endif
    return r;
}


LQ_EXTERN_C int LQ_CALL LqTimeGmtTmToStr(char* DestStr, LqTimeSec DestStrLen, const struct tm* InTmGmt)
{
    return snprintf(DestStr, DestStrLen, PRINTF_TIME_TM_FORMAT_GMT, PRINTF_TIME_TM_ARG_GMT(*InTmGmt));
}

LQ_EXTERN_C int LQ_CALL LqTimeLocTmToStr(char* DestStr, LqTimeSec DestStrLen, const struct tm* InTm)
{
    return snprintf(DestStr, DestStrLen, PRINTF_TIME_TM_FORMAT_GMT, PRINTF_TIME_TM_ARG_GMT(*InTm));
}

LQ_EXTERN_C int LQ_CALL LqTimeLocToStr(char* DestStr, LqTimeSec DestStrLen)
{
    auto t = time(nullptr);
    auto Tm = localtime(&t);
    return LqTimeLocTmToStr(DestStr, DestStrLen, Tm);
}

LQ_EXTERN_C int LQ_CALL LqTimeGmtToStr(char* DestStr, LqTimeSec DestStrLen)
{
    auto t = time(nullptr) + GmtCorrection;
    time_t Tt = t;
    auto Tm = gmtime(&Tt);
    return LqTimeLocTmToStr(DestStr, DestStrLen, Tm);
}

LQ_EXTERN_C LqTimeSec LQ_CALL LqTimeGetLocSec()
{
    return time(nullptr);
}

LQ_EXTERN_C LqTimeSec LQ_CALL LqTimeGetGmtSec()
{
#if !defined(LQPLATFORM_ANDROID)
    timeb t;
    ftime(&t);
    return t.time + t.timezone * 60 + GmtCorrection;
#else
    struct timezone tz;
    timeval tv;
    gettimeofday(&tv, &tz);
    return tv.tv_sec + tz.tz_minuteswest * 60 + GmtCorrection;
#endif
}

LQ_EXTERN_C void LQ_CALL LqTimeGetLocTm(struct tm* Tm)
{
    auto t = time(nullptr);
    *Tm = *localtime(&t);
}

LQ_EXTERN_C void LQ_CALL LqTimeGetGmtTm(struct tm* Tm)
{
    auto t = time(nullptr) + GmtCorrection;
    time_t Tt = t;
    *Tm = *gmtime(&Tt);
}

void LQ_CALL LqTimeLocSecToLocTm(struct tm* Tm, LqTimeSec LocalTime)
{
    time_t Tt = LocalTime;
    *Tm = *localtime(&Tt);
}

LQ_EXTERN_C void LQ_CALL LqTimeLocSecToGmtTm(struct tm* Tm, LqTimeSec LocalTime)
{
    LocalTime += GmtCorrection;
    time_t Tt = LocalTime;
    *Tm = *gmtime(&Tt);
}

LQ_EXTERN_C LqTimeSec LQ_CALL LqTimeLocSecToGmtSec(LqTimeSec LocalTime)
{
#if !defined(LQPLATFORM_ANDROID)
    timeb t;
    ftime(&t);
    return LocalTime + t.timezone * 60 + GmtCorrection;
#else
    struct timezone tz;
    timeval tv;
    gettimeofday(&tv, &tz);
    return LocalTime + tz.tz_minuteswest * 60 + GmtCorrection;
#endif
}

LQ_EXTERN_C LqTimeSec LQ_CALL LqTimeGmtSecToLocSec(LqTimeSec GmtTime)
{
#if !defined(LQPLATFORM_ANDROID)
    timeb t;
    ftime(&t);
    return GmtTime - (t.timezone * 60 + GmtCorrection);
#else
    struct timezone tz;
    timeval tv;
    gettimeofday(&tv, &tz);
    return GmtTime - (tz.tz_minuteswest * 60 + GmtCorrection);
#endif
}

LQ_EXTERN_C int LQ_CALL LqTimeLocSecToStr(char* DestStr, size_t DestStrLen, LqTimeSec FileTime)
{
    time_t Tt = FileTime;
    auto Tm = localtime(&Tt);
    return LqTimeLocTmToStr(DestStr, DestStrLen, Tm);
}

LQ_EXTERN_C int LQ_CALL LqTimeGmtSecToStr(char* DestStr, size_t DestStrLen, LqTimeSec FileTimeGmt)
{
    FileTimeGmt += GmtCorrection;
    time_t Tt = FileTimeGmt;
    auto Tm = gmtime(&Tt);
    return LqTimeLocTmToStr(DestStr, DestStrLen, Tm);
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqTimeGetLocMillisec()
{
    return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock().now()).time_since_epoch().count();
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqTimeGetGmtMillisec()
{
#if !defined(LQPLATFORM_ANDROID)
    timeb t;
    ftime(&t);
    return (LqTimeMillisec)t.time * 1000ULL + t.millitm + t.timezone * (60LL * 1000LL);
#else
    struct timezone tz;
    timeval tv;
    gettimeofday(&tv, &tz);
    return (LqTimeMillisec)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL + tz.tz_minuteswest * (60LL * 1000LL);
#endif
}

LQ_EXTERN_C LqTimeMillisec LQ_CALL LqTimeGet(short* TimeZone)
{
#if !defined(LQPLATFORM_ANDROID)
	timeb t;
	ftime(&t);
	*TimeZone = t.timezone;
	return (LqTimeMillisec)t.time * 1000ULL + t.millitm;
#else
	struct timezone tz;
	timeval tv;
	gettimeofday(&tv, &tz);
	*TimeZone = tz.tz_minuteswest;
	return (LqTimeMillisec)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
#endif
}


LQ_EXTERN_C LqTimeMillisec LQ_CALL LqTimeGetMaxMillisec()
{
    return (LqTimeMillisec)std::numeric_limits<LqTimeMillisec>::max();
}
