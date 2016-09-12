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
#include "LqStr.hpp"


inline constexpr uint32_t __StrSwitchLen(const char* const Str) { return (*Str != '\0') ? (__StrSwitchLen(Str + 1) + 1) : 0; }
inline constexpr uint32_t __StrSwitchHash(const char* const Str, uint32_t CurHash) { return (*Str != '\0') ? (__StrSwitchHash(Str + 1, CurHash * 31 + *Str)) : CurHash; }
inline constexpr uint64_t __StrSwitchStrCase(const char* const Str) { return ((uint64_t)__StrSwitchLen(Str) << 32) | (uint64_t)__StrSwitchHash(Str, 0); }

inline uint64_t __StrSwitch(const char* Str)
{
    uint32_t h = 0;
    const char* k = Str;
    for(; *k != '\0'; k++) h = 31 * h + *k;
    return ((uint64_t)(k - Str) << 32) | (uint64_t)h;
}

inline uint64_t __StrSwitchN(const char* Str, size_t Len)
{
    uint32_t h = 0;
    const char* k = Str, *m = Str + Len;
    for(; k < m; k++) h = 31 * h + *k;
    return ((uint64_t)(Len) << 32) | (uint64_t)h;
}

inline uint64_t __StrSwitchI(const char* Str)
{
    uint32_t h = 0;
    const char* k = Str;
    for(; *k != '\0'; k++) h = 31 * h + tolower(*k);
    return ((uint64_t)(k - Str) << 32) | (uint64_t)h;
}

inline uint64_t __StrSwitchNI(const char* Str, size_t Len)
{
    uint32_t h = 0;
    const char* k = Str, *m = Str + Len;
    for(; k < m; k++) h = 31 * h + tolower(*k);
    return ((uint64_t)(Len) << 32) | (uint64_t)h;
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
            if((___switch_hash == h) && (memcmp(Str, ___switch_str, h >> 32) != 0)) break;\
        }

/*@Str - must be string in lower case*/
#define LQSTR_CASE_I(Str) \
        {\
            static const auto h = __StrSwitchStrCase(Str);\
            case h:\
            if((___switch_hash == h) && !LqStrUtf8CmpCaseLen(Str, ___switch_str, h >> 32)) break;\
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
