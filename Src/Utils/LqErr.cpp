/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*  Error numbers convert from unix to windows and conversely.
*/
#include "LqOs.h"
#include "LqErr.h"
#ifdef LQPLATFORM_WINDOWS

#include <errno.h>
#include <Windows.h>


LQ_EXTERN_C int LQ_CALL ___lq_windows_errno()
{
	// Translate win error to unix error
	switch(GetLastError())
	{
		case NO_ERROR:						return 0;
		case ERROR_INVALID_OPERATION:		return EPERM;
		case ERROR_ALREADY_EXISTS:
		case ERROR_FILE_EXISTS:				return EEXIST;
		case ERROR_BAD_EXE_FORMAT:			return ENOEXEC;
		case ERROR_INVALID_HANDLE:			return EBADF;
		case ERROR_WAIT_NO_CHILDREN:		return ECHILD;
		case ERROR_NOT_READY:
		case ERROR_MORE_DATA:
		case ERROR_ACTIVE_CONNECTIONS:
		case ERROR_MAX_THRDS_REACHED:
		case ERROR_NO_PROC_SLOTS:
		case ERROR_NO_SYSTEM_RESOURCES:		return EAGAIN;
		case ERROR_OUTOFMEMORY:
		case ERROR_NOT_ENOUGH_MEMORY:		return ENOMEM;
		case ERROR_CANNOT_MAKE:
		case ERROR_SHARING_VIOLATION:
		case ERROR_CURRENT_DIRECTORY:
		case ERROR_INVALID_ACCESS:
		case ERROR_LOCK_VIOLATION:
		case ERROR_LOCKED:
		case ERROR_NOACCESS:
		case ERROR_ACCESS_DENIED:			return EACCES;
		case ERROR_PROCESS_ABORTED:
		case ERROR_INVALID_ADDRESS:			return EFAULT;
		case ERROR_DIRECTORY:				return ENOTDIR;
		case ERROR_DIR_NOT_EMPTY:			return ENOTEMPTY;
		case ERROR_INVALID_NAME:
		case ERROR_INVALID_PARAMETER:
		case ERROR_BAD_PIPE:
		case ERROR_BAD_USERNAME:
		case ERROR_INVALID_DATA:
		case ERROR_INVALID_SIGNAL_NUMBER:
		case ERROR_META_EXPANSION_TOO_LONG:
		case ERROR_NEGATIVE_SEEK:
		case ERROR_NO_TOKEN:
		case ERROR_THREAD_1_INACTIVE:
		case ERROR_BAD_ARGUMENTS:			return EINVAL;
		case ERROR_TOO_MANY_OPEN_FILES:		return ENFILE;
		case ERROR_DEVICE_IN_USE:
		case ERROR_BUSY:
		case ERROR_BUSY_DRIVE:
		case ERROR_CHILD_NOT_COMPLETE:
		case ERROR_PIPE_BUSY:
		case ERROR_PIPE_CONNECTED:
		case ERROR_SIGNAL_PENDING:
		case ERROR_OPEN_FILES:				return EBUSY;
		case ERROR_HANDLE_DISK_FULL:
		case ERROR_END_OF_MEDIA:
		case ERROR_EOM_OVERFLOW:
		case ERROR_NO_DATA_DETECTED:
		case ERROR_DISK_FULL:				return ENOSPC;
		case ERROR_WRITE_PROTECT:			return EROFS;
		case ERROR_BROKEN_PIPE:				return EPIPE;
		case ERROR_BUFFER_OVERFLOW:
		case ERROR_FILENAME_EXCED_RANGE:	return ENAMETOOLONG;
		case ERROR_INVALID_FUNCTION:
		case ERROR_CALL_NOT_IMPLEMENTED:
		case ERROR_NOT_SUPPORTED:			return ENOSYS;
		case ERROR_BAD_UNIT:
		case ERROR_DEVICE_REMOVED:
		case ERROR_DEVICE_NOT_AVAILABLE:
		case ERROR_DEV_NOT_EXIST:
		case ERROR_INVALID_DRIVE:
		case ERROR_BAD_DEVICE:
		case ERROR_DEVICE_NOT_CONNECTED:	return ENODEV;
		case ERROR_FILE_NOT_FOUND:
		case ERROR_BAD_PATHNAME:
		case ERROR_MOD_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:			return ENOENT;
		case ERROR_IO_DEVICE:
		case ERROR_CRC:
		case ERROR_NO_SIGNAL_SENT:
		case ERROR_CANTOPEN:
		case ERROR_CANTREAD:
		case ERROR_CANTWRITE:
		case ERROR_OPEN_FAILED:
		case ERROR_READ_FAULT:
		case ERROR_WRITE_FAULT:
		case ERROR_SIGNAL_REFUSED:
		case ERROR_SEEK:					return EIO;
		case ERROR_NOT_SAME_DEVICE:			return EXDEV;
		case ERROR_POSSIBLE_DEADLOCK:		return EDEADLOCK;
		case ERROR_INVALID_AT_INTERRUPT_TIME:return EINTR;
		case ERROR_NO_MORE_SEARCH_HANDLES:	return EMFILE;
		case ERROR_HANDLE_EOF:				return ENODATA;
		case ERROR_SHARING_BUFFER_EXCEEDED:	return ENOLCK;
		case ERROR_NOT_CONNECTED:			return ENOLINK;
		case ERROR_FILE_INVALID:			return ENXIO;
		case ERROR_ARITHMETIC_OVERFLOW:		return EOVERFLOW;
		case ERROR_SETMARK_DETECTED:
		case ERROR_BEGINNING_OF_MEDIA:		return ESPIPE;
#ifdef ENOTUNIQ
		case ERROR_DUP_NAME:				return ENOTUNIQ;
#endif
#ifdef ENOSHARE
		case ERROR_BAD_NETPATH:				return ENOSHARE;
		case ERROR_BAD_NET_NAME:			return ENOSHARE;
#endif
#ifdef ENONET
		case ERROR_REM_NOT_LIST:			return ENONET;
#endif
#ifdef ECOMM
		case ERROR_PIPE_NOT_CONNECTED:
		case ERROR_PIPE_LISTENING:			return ECOMM;
#endif
#ifdef ENMFILE
		case ERROR_NO_MORE_FILES:			return ENMFILE;
#endif


		//WsaErrors
		case WSAENOTEMPTY:					return ENOTEMPTY;
#ifdef EADDRINUSE
		case WSAEADDRINUSE:					return EADDRINUSE;
#endif
#ifdef EADDRNOTAVAIL
		case WSAEADDRNOTAVAIL:				return EADDRNOTAVAIL;
#endif
#ifdef EAFNOSUPPORT
		case WSAEAFNOSUPPORT:				return EAFNOSUPPORT;
#endif
#ifdef EALREADY
		case WSAEALREADY:					return EALREADY;
#endif
		case WSAEBADF:						return EBADF;
#ifdef ECONNABORTED
		case WSAECONNABORTED:				return ECONNABORTED;
#endif
#ifdef ECONNREFUSED
		case WSAECONNREFUSED:				return ECONNREFUSED;
#endif
#ifdef ECONNRESET
		case WSAECONNRESET:					return ECONNRESET;
#endif
#ifdef EDESTADDRREQ
		case WSAEDESTADDRREQ:				return EDESTADDRREQ;
#endif
		case WSAEFAULT:						return EFAULT;
#ifdef EHOSTDOWN
		case WSAEHOSTDOWN:					return EHOSTDOWN;
#endif
#ifdef EINPROGRESS
		case WSAEINPROGRESS:				return EINPROGRESS;
#endif
		case WSAEINTR:						return EINTR;
		case WSAEINVAL:						return EINVAL;
#ifdef EISCONN
		case WSAEISCONN:					return EISCONN;
#endif
#ifdef ELOOP
		case WSAELOOP:						return ELOOP;
#endif
		case WSAEMFILE:						return EMFILE;
#ifdef EMSGSIZE
		case WSAEMSGSIZE:					return EMSGSIZE;
#endif
		case WSAENAMETOOLONG:				return ENAMETOOLONG;
#ifdef ENETDOWN
		case WSAENETDOWN:					return ENETDOWN;
#endif
#ifdef ENETRESET
		case WSAENETRESET:					return ENETRESET;
#endif
#ifdef ENETUNREACH
		case WSAENETUNREACH:				return ENETUNREACH;
#endif
#ifdef ENOBUFS
		case WSAENOBUFS:					return ENOBUFS;
#endif
#ifdef ENOPROTOOPT
		case WSAENOPROTOOPT:				return ENOPROTOOPT;
#endif
#ifdef ENOTCONN
		case WSAENOTCONN:					return ENOTCONN;
#endif
		case WSANOTINITIALISED:				return EAGAIN;
#ifdef ENOTSOCK
		case WSAENOTSOCK:					return ENOTSOCK;
#endif
		case WSAEOPNOTSUPP:					return EOPNOTSUPP;
#ifdef EPFNOSUPPORT
		case WSAEPFNOSUPPORT:				return EPFNOSUPPORT;
#endif
#ifdef EPROTONOSUPPORT
		case WSAEPROTONOSUPPORT:			return EPROTONOSUPPORT;
#endif
#ifdef EPROTOTYPE
		case WSAEPROTOTYPE:					return EPROTOTYPE;
#endif
#ifdef ESHUTDOWN
		case WSAESHUTDOWN:					return ESHUTDOWN;
#endif
#ifdef ESOCKTNOSUPPORT
		case WSAESOCKTNOSUPPORT:			return ESOCKTNOSUPPORT;
#endif
#ifdef ETIMEDOUT
		case WSAETIMEDOUT:					return ETIMEDOUT;
#endif

#ifdef ETOOMANYREFS
		case WSAETOOMANYREFS:				return ETOOMANYREFS;
#endif
		case ERROR_IO_PENDING:
		case WSAEWOULDBLOCK:
#ifdef EWOULDBLOCK
											return EWOULDBLOCK;
#else
											return EAGAIN;
#endif
#ifdef EHOSTUNREACH
		case WSAEHOSTUNREACH:
		case WSAHOST_NOT_FOUND:				return EHOSTUNREACH;
#endif
		case WSASYSNOTREADY:
		case WSATRY_AGAIN:					return EAGAIN;
#ifdef DB_OPNOTSUP
		case WSAVERNOTSUPPORTED:			return DB_OPNOTSUP;
#endif
		case WSAEACCES:						return EACCES;
#ifdef EREMOTE
		case WSAEREMOTE:				    return EREMOTE;
#endif
#ifdef ESTALE
		case WSAESTALE:						return ESTALE;
#endif
#ifdef EDQUOT
		case WSAEDQUOT:						return EDQUOT;
#endif
#ifdef EUSERS
		case WSAEUSERS:						return EUSERS;
#endif
#ifdef EPROCLIM
		case WSAEPROCLIM:					return EPROCLIM;
#endif
	}
	return EOTHER;
}

LQ_EXTERN_C int LQ_CALL ___lq_windows_set_errno(int NewErr)
{
	DWORD Err;
	errno = NewErr;
	// Translate unix error to win error

	switch(NewErr)
	{
		case 0:	Err = NO_ERROR; break;
		case EPERM: Err = ERROR_INVALID_OPERATION; break;
		case EEXIST: Err = ERROR_FILE_EXISTS; break;
		case ENOEXEC: Err = ERROR_BAD_EXE_FORMAT; break;
		case EBADF: Err = ERROR_INVALID_HANDLE; break;
		case ECHILD: Err = ERROR_WAIT_NO_CHILDREN; break;
		case EAGAIN: Err = ERROR_NOT_READY; break;
		case ENOMEM: Err = ERROR_NOT_ENOUGH_MEMORY; break;
		case EACCES: Err = ERROR_NOACCESS; break;
		case EFAULT: Err = ERROR_INVALID_ADDRESS; break;
		case ENOTDIR: Err = ERROR_DIRECTORY; break;
		case ENOTEMPTY: Err = ERROR_DIR_NOT_EMPTY; break;
		case EINVAL: Err = ERROR_INVALID_PARAMETER; break;
		case ENFILE: Err = ERROR_TOO_MANY_OPEN_FILES; break;
		case EBUSY: Err = ERROR_BUSY; break;
		case ENOSPC: Err = ERROR_DISK_FULL; break;
		case EROFS: Err = ERROR_WRITE_PROTECT; break;
		case EPIPE: Err = ERROR_BROKEN_PIPE; break;
		case ENAMETOOLONG: Err = ERROR_FILENAME_EXCED_RANGE; break;
		case ENOSYS: Err = ERROR_NOT_SUPPORTED; break;
		case ENODEV: Err = ERROR_BAD_DEVICE; break;
		case ENOENT: Err = ERROR_PATH_NOT_FOUND; break;
		case EIO: Err = ERROR_IO_DEVICE; break;
		case EXDEV: Err = ERROR_NOT_SAME_DEVICE; break;
		case EDEADLOCK: Err = ERROR_POSSIBLE_DEADLOCK; break;
		case EINTR: Err = ERROR_INVALID_AT_INTERRUPT_TIME; break;
		case EMFILE: Err = ERROR_NO_MORE_SEARCH_HANDLES; break;
		case ENODATA: Err = ERROR_HANDLE_EOF; break;
		case ENOLCK: Err = ERROR_SHARING_BUFFER_EXCEEDED; break;
		case ENOLINK: Err = ERROR_NOT_CONNECTED; break;
		case ENXIO: Err = ERROR_FILE_INVALID; break;
		case EOVERFLOW: Err = ERROR_ARITHMETIC_OVERFLOW; break;
		case ESPIPE: Err = ERROR_SETMARK_DETECTED; break;

#ifdef ENOTUNIQ
		case ENOTUNIQ: Err = ERROR_DUP_NAME; break;
#endif
#ifdef ENOSHARE
		case ENOSHARE: Err = ERROR_BAD_NETPATH; break;
#endif
#ifdef ENONET
		case ENONET: Err = ERROR_REM_NOT_LIST; break;
#endif
#ifdef ECOMM
		case ECOMM: Err = ERROR_PIPE_NOT_CONNECTED; break;
#endif
#ifdef ENMFILE
		case ENMFILE: Err = ERROR_NO_MORE_FILES; break;
#endif


		//WsaErrors
#ifdef EADDRINUSE
		case EADDRINUSE: Err = WSAEADDRINUSE; break;
#endif
#ifdef EADDRNOTAVAIL
		case EADDRNOTAVAIL: Err = WSAEADDRNOTAVAIL; break;
#endif
#ifdef EAFNOSUPPORT
		case EAFNOSUPPORT: Err = WSAEAFNOSUPPORT; break;
#endif
#ifdef EALREADY
		case EALREADY: Err = WSAEALREADY; break;
#endif
#ifdef ECONNABORTED
		case ECONNABORTED: Err = WSAECONNABORTED; break;
#endif
#ifdef ECONNREFUSED
		case ECONNREFUSED: Err = WSAECONNREFUSED; break;
#endif
#ifdef ECONNRESET
		case ECONNRESET: Err = WSAECONNRESET; break;
#endif
#ifdef EDESTADDRREQ
		case EDESTADDRREQ: Err = WSAEDESTADDRREQ; break;
#endif
#ifdef EHOSTDOWN
		case EHOSTDOWN: Err = WSAEHOSTDOWN; break;
#endif
#ifdef EINPROGRESS
		case EINPROGRESS: Err = WSAEINPROGRESS; break;
#endif
#ifdef EISCONN
		case EISCONN: Err = WSAEISCONN; break;
#endif
#ifdef ELOOP
		case ELOOP: Err = WSAELOOP; break;
#endif
#ifdef EMSGSIZE
		case EMSGSIZE: Err = WSAEMSGSIZE; break;
#endif
#ifdef ENETDOWN
		case ENETDOWN: Err = WSAENETDOWN; break;
#endif
#ifdef ENETRESET
		case ENETRESET: Err = WSAENETRESET; break;
#endif
#ifdef ENETUNREACH
		case ENETUNREACH: Err = WSAENETUNREACH; break;
#endif
#ifdef ENOBUFS
		case ENOBUFS: Err = WSAENOBUFS; break;
#endif
#ifdef ENOPROTOOPT
		case ENOPROTOOPT: Err = WSAENOPROTOOPT; break;
#endif
#ifdef ENOTCONN
		case ENOTCONN: Err = WSAENOTCONN; break;
#endif
#ifdef ENOTSOCK
		case ENOTSOCK: Err = WSAENOTSOCK; break;
#endif
		case EOPNOTSUPP: Err = WSAEOPNOTSUPP; break;
#ifdef EPFNOSUPPORT
		case EPFNOSUPPORT: Err = WSAEPFNOSUPPORT; break;
#endif
#ifdef EPROTONOSUPPORT
		case EPROTONOSUPPORT: Err = WSAEPROTONOSUPPORT; break;
#endif
#ifdef EPROTOTYPE
		case EPROTOTYPE: Err = WSAEPROTOTYPE; break;
#endif
#ifdef ESHUTDOWN
		case ESHUTDOWN: Err = WSAESHUTDOWN; break;
#endif
#ifdef ESOCKTNOSUPPORT
		case ESOCKTNOSUPPORT: Err = WSAESOCKTNOSUPPORT; break;
#endif
#ifdef ETIMEDOUT
		case ETIMEDOUT: Err = WSAETIMEDOUT; break;
#endif
#ifdef ETOOMANYREFS
		case ETOOMANYREFS: Err = WSAETOOMANYREFS; break;
#endif
#ifdef EWOULDBLOCK
		case EWOULDBLOCK: Err = WSAEWOULDBLOCK; break;
#endif
#ifdef EHOSTUNREACH
		case EHOSTUNREACH: Err = WSAEHOSTUNREACH; break;
#endif
#ifdef DB_OPNOTSUP
		case DB_OPNOTSUP: Err = WSAVERNOTSUPPORTED; break;
#endif
#ifdef EREMOTE
		case EREMOTE: Err = WSAEREMOTE; break;
#endif
#ifdef ESTALE
		case ESTALE: Err = WSAESTALE; break;
#endif
#ifdef EDQUOT
		case EDQUOT: Err = WSAEDQUOT; break;
#endif
#ifdef EUSERS
		case EUSERS: Err = WSAEUSERS; break;
#endif
#ifdef EPROCLIM
		case EPROCLIM: Err = WSAEPROCLIM; break;
#endif
		default:
			Err = ERROR_UNIDENTIFIED_ERROR;
	}

	SetLastError(Err);
	return NewErr;
}

#endif
