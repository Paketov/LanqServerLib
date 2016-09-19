#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LQSTR_SWITCH... - Switch, case for C++.
*/

/*
*   Fast string switch-case;
*   Used hash method
*   Excample using:
    char* s = "2";
    LQSTR_SWITCH(s)
    {
        LQSTR_CASE("1");
        LQSTR_CASE("2");
        LQSTR_SWITCH(s)
        {
            LQSTR_CASE("2");
            {
                printf("case 2");
            }
            break;
        }
        printf("case 1");
        break;
        LQSTR_CASE("0");
        printf("case 0");
        break;
        LQSTR_CASE("3ab");
        printf("case 3");
        break;
        LQSTR_CASE("4");
        printf("case 4");
        break;
    }
*/

#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "LqStr.h"

#ifndef LQSTR_SWITCH_TYPE_CASE
#define LQSTR_SWITCH_TYPE_CASE uint64_t
#endif
#ifndef LQSTR_SWITCH_TYPE_HASH
#define LQSTR_SWITCH_TYPE_HASH uint32_t
#endif
#ifndef LQSTR_SWITCH_TYPE_LEN
#define LQSTR_SWITCH_TYPE_LEN  uint32_t
#endif

inline constexpr LQSTR_SWITCH_TYPE_LEN __StrSwitchLen(const char* const Str) { return (*Str != '\0') ? (__StrSwitchLen(Str + 1) + 1) : 0; }
inline constexpr LQSTR_SWITCH_TYPE_HASH __StrSwitchHash(const char* const Str, LQSTR_SWITCH_TYPE_HASH CurHash) \
	{ return (*Str != '\0') ? (__StrSwitchHash(Str + 1, CurHash * (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8 - 1) + *Str)) : CurHash; }
inline constexpr LQSTR_SWITCH_TYPE_CASE __StrSwitchStrCase(const char* const Str) \
	{ return ((LQSTR_SWITCH_TYPE_CASE)__StrSwitchLen(Str) << (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8)) | (LQSTR_SWITCH_TYPE_CASE)__StrSwitchHash(Str, 0); }

inline LQSTR_SWITCH_TYPE_CASE __StrSwitch(const char* Str)
{
	LQSTR_SWITCH_TYPE_HASH h = 0;
    const char* k = Str;
    for(; *k != '\0'; k++) h = (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8 - 1) * h + *k;
    return ((LQSTR_SWITCH_TYPE_CASE)(k - Str) << (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8)) | (LQSTR_SWITCH_TYPE_CASE)h;
}

inline LQSTR_SWITCH_TYPE_CASE __StrSwitchN(const char* Str, size_t Len)
{
	LQSTR_SWITCH_TYPE_HASH h = 0;
    const char* k = Str, *m = Str + Len;
    for(; k < m; k++) h = (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8 - 1) * h + *k;
    return ((LQSTR_SWITCH_TYPE_CASE)(Len) << (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8)) | (LQSTR_SWITCH_TYPE_CASE)h;
}

inline LQSTR_SWITCH_TYPE_CASE __StrSwitchI(const char* Str)
{
	LQSTR_SWITCH_TYPE_HASH h = 0;
    const char* k = Str;
    for(; *k != '\0'; k++) h = (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8 - 1) * h + tolower(*k);
    return ((LQSTR_SWITCH_TYPE_CASE)(k - Str) << (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8)) | (LQSTR_SWITCH_TYPE_CASE)h;
}

inline LQSTR_SWITCH_TYPE_CASE __StrSwitchNI(const char* Str, size_t Len)
{
	LQSTR_SWITCH_TYPE_HASH h = 0;
    const char* k = Str, *m = Str + Len;
    for(; k < m; k++) h = (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8 - 1) * h + tolower(*k);
    return ((LQSTR_SWITCH_TYPE_CASE)(Len) << (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8)) | (LQSTR_SWITCH_TYPE_CASE)h;
}

#define LQSTR_SWITCH(Str) \
    for(const char* ___switch_str = (Str); ___switch_str; ___switch_str = nullptr)\
        switch(auto ___switch_hash = __StrSwitch(Str))

#define LQSTR_SWITCH_I(Str) \
    for(const char* ___switch_str = (Str); ___switch_str; ___switch_str = nullptr)\
        switch(auto ___switch_hash = __StrSwitchI(Str))

#define LQSTR_SWITCH_N(Str, Len) \
    for(const char* ___switch_str = (Str); ___switch_str; ___switch_str = nullptr)\
        switch(auto ___switch_hash = __StrSwitchN(Str, Len))

#define LQSTR_SWITCH_NI(Str, Len) \
    for(const char* ___switch_str = (Str); ___switch_str; ___switch_str = nullptr)\
        switch(auto ___switch_hash = __StrSwitchNI(Str, Len))

#define LQSTR_CASE(Str) \
        {\
            static const auto h = __StrSwitchStrCase(Str);\
            case h:\
            if((___switch_hash == h) && (memcmp(Str, ___switch_str, h >> (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8)) != 0)) break;\
        }

/*@Str - must be string in lower case*/
#define LQSTR_CASE_I(Str) \
        {\
            static const auto h = __StrSwitchStrCase(Str);\
            case h:\
            if((___switch_hash == h) && !LqStrUtf8CmpCaseLen(Str, ___switch_str, h >> (sizeof(LQSTR_SWITCH_TYPE_HASH) * 8))) break;\
        }

#define LQSTR_SWITCH_DEFAULT default:


/*
* Switching based on mapping string in 64-bit variable
*/

#define A8B(s, i, j) (uint64_t)(((uint64_t)((s "\0\0\0\0\0\0\0\0")[i])) << (j * 8))

#if BIGENDIAN == 1
#define STR_INT64(Str) \
                       (A8B(Str, 0, 0)|\
                        A8B(Str, 1, 1)|\
                        A8B(Str, 2, 2)|\
                        A8B(Str, 3, 3)|\
                        A8B(Str, 4, 4)|\
                        A8B(Str, 5, 5)|\
                        A8B(Str, 6, 6)|\
                        A8B(Str, 7, 7))
#else
#define STR_INT64(Str) \
                        (A8B(Str, 0, 7)|\
                        A8B(Str, 1, 6)|\
                        A8B(Str, 2, 5)|\
                        A8B(Str, 3, 4)|\
                        A8B(Str, 4, 3)|\
                        A8B(Str, 5, 2)|\
                        A8B(Str, 6, 1)|\
                        A8B(Str, 7, 0))
#endif

#define LQ_BREAK_BLOCK_BEGIN    { do {
#define LQ_BREAK_BLOCK_END  } while(false); }
