/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqCp... - Code page conversations.
*/


#ifndef __CODE_PAGE_H_HAS_INCLUDED__
#define __CODE_PAGE_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqDef.h"



#define LQCP_UTF_8                  1
#define LQCP_UTF_7                  2
#define LQCP_UTF_16                 3
#define LQCP_ACP                    4
#define LQCP_OEMCP                  5
#define LQCP_MACCP                  6


LQ_EXTERN_C_BEGIN

/* Set global code page */
LQ_IMPORTEXPORT int LQ_CALL LqCpSet(int NewCodePage);

/* Get current global code page */
LQ_IMPORTEXPORT int LQ_CALL LqCpGet();


LQ_IMPORTEXPORT int LQ_CALL LqCpConvertToWcs(const char* Source, wchar_t* Dest, size_t DestCount);
LQ_IMPORTEXPORT int LQ_CALL LqCpConvertFromWcs(const wchar_t* Source, char* Dest, size_t DestCount);

LQ_EXTERN_C_END


#endif
