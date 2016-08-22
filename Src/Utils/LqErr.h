
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
#define lq_errno_set(NewErrNo)  (___lq_windows_set_errno(NewErrNo))

# ifdef EWOULDBLOCK
#  define LQERR_IS_WOULD_BLOCK (lq_errno == EWOULDBLOCK)
# else
#  define LQERR_IS_WOULD_BLOCK (lq_errno == EAGAIN)
# endif

#else
#define lq_errno                ((int)errno)
#define lq_errno_set(NewErrNo)  ((int)(errno = (int)NewErrNo))

# if defined(__sun__)
#  define LQERR_IS_WOULD_BLOCK (uwsgi_is_again())
# elif defined(EWOULDBLOCK) && defined(EAGAIN)
#  define LQERR_IS_WOULD_BLOCK ((lq_errno == EWOULDBLOCK) || (lq_errno == EAGAIN))
# elif defined(EWOULDBLOCK)
#  define LQERR_IS_WOULD_BLOCK (lq_errno == EWOULDBLOCK)
# else
#  define LQERR_IS_WOULD_BLOCK (lq_errno == EAGAIN)
# endif

#endif

#endif