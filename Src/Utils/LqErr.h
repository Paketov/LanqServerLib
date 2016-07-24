
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   Errors defenitions.
*/

#ifndef __LQ_ERR_H_HAS_BEEN_DEFINED__
#define __LQ_ERR_H_HAS_BEEN_DEFINED__

#include <errno.h>
#include "LqOs.h"

#ifdef LQPLATFORM_WINDOWS

LQ_EXTERN_C_BEGIN
LQ_IMPORTEXPORT     int LQ_CALL ___lq_windows_errno();
LQ_IMPORTEXPORT     int LQ_CALL ___lq_windows_set_errno(int NewErr);
LQ_EXTERN_C_END

#define lq_errno                (___lq_windows_errno())
#define lq_set_errno(NewErrNo)  (___lq_windows_set_errno(NewErrNo))

#else
#define lq_errno                ((int)errno)
#define lq_set_errno(NewErrNo)  ((int)(errno = (int)NewErrNo))
#endif

#endif