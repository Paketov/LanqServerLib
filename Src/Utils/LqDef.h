/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   Different definitions.
*/

#ifndef __LQ_DEF_H_HAS_BEEN_DEFINED__
#define __LQ_DEF_H_HAS_BEEN_DEFINED__

#include <stddef.h>
#include <stdint.h>

typedef unsigned char           uchar;
typedef unsigned short          ushort;
typedef unsigned int            uint;
typedef unsigned long           ulong;
typedef long long               llong;
typedef unsigned long long      ullong;
typedef llong                   LqFileSz;
typedef llong                   LqTimeSec;
typedef llong                   LqTimeMillisec;
typedef uint16_t                LqEvntFlag;

#define lq_max(a,b) (((a) > (b)) ? (a) : (b))
#define lq_min(a,b) (((a) < (b)) ? (a) : (b))

#endif
