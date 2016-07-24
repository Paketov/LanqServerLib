/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqOs - Fitures for some OS`s and compilers.
*/

#ifndef __LQ_OS_H_HAS_INCLUDED__
#define __LQ_OS_H_HAS_INCLUDED__


#if defined(_MSC_VER)

# define _CRT_SECURE_NO_WARNINGS
# define LQEVNT_WIN_EVENT
# define LQPLATFORM_WINDOWS
//# define LQCONN_HAS_EVNT_AGAIN  //Use primary in windows, because win events not supported level triggered
# pragma warning(disable : 4996)
# pragma warning(disable : 4307)

# define LQ_CALL	__cdecl
# define LQ_EXPORT	__declspec(dllexport)
# define LQ_IMPORT	__declspec(dllimport)
# define LQHTTPPTH_SEPARATOR '\\'

#else

# ifdef __ANDROID__
#  define LQPLATFORM_ANDROID
# endif

# if defined(__linux__)
#  ifndef LQEVNT_EPOOL_MAX_WAIT_EVENTS
#   define LQEVNT_EPOOL_MAX_WAIT_EVENTS 70
#  endif
#  ifndef LQPLATFORM_ANDROID
#   define LQPLATFORM_LINUX
#  endif
#  define LQEVNT_EPOLL

# elif defined(__FreeBSD__) || defined(__APPLE__)

# if defined(__FreeBSD__)
#  define LQPLATFORM_FREEBSD
# elif defined(__APPLE__)
#  define LQPLATFORM_APPLE
# endif

#  define LQEVNT_KEVENT
#  error "Kevent not implemented"
# else
//For others unix systems
#  define LQEVNT_POLL
#  define LQPLATFORM_OTHER
# endif



# define LQ_CALL
# define LQ_EXPORT	__attribute__ ((visibility("default")))
# define LQ_IMPORT

# define LQHTTPPTH_SEPARATOR '/'
#endif

/* Extern defines */

#ifdef LANQBUILD
# define LQ_IMPORTEXPORT LQ_EXPORT
#else
# define LQ_IMPORTEXPORT LQ_IMPORT
#endif

#ifdef __cplusplus
# define LQ_EXTERN_C extern "C"
# define LQ_EXTERN_C_BEGIN LQ_EXTERN_C {
# define LQ_EXTERN_C_END }

# define LQ_EXTERN_CPP extern "C++"
# define LQ_EXTERN_CPP_BEGIN LQ_EXTERN_CPP {
# define LQ_EXTERN_CPP_END }

#else
# define LQ_EXTERN_C
# define LQ_EXTERN_C_BEGIN
# define LQ_EXTERN_C_END
#endif


/* Architectuire word bits */
#if defined(_WIN32)
# define LQARCH_32
#elif defined(_WIN64)
# define LQARCH_64
#elif __WORDSIZE == 64
# define LQARCH_64
#elif __WORDSIZE == 32
# define LQARCH_64
#elif defined(__GNUC__)
# ifdef __x86_64__
#  define LQARCH_64
# else
#  define LQARCH_32
# endif
#else
# error "Not detect platform architecture"
#endif


/* Set struct or class align */
#ifdef LQARCH_32
# define LQCACHE_ALIGN_FAST 4
#else
# define LQCACHE_ALIGN_FAST 8
#endif

#define LQCACHE_ALIGN_MEM 1

#define lqaopt			//LanQ Argument Optional
#define lqain			//LanQ Argument Input
#define lqaio			//LanQ Argument Input, output
#define lqaout			//LanQ Argument Output
#define lqats			//LanQ Argument Thread Save
#define lqatns			//LanQ Argument Not Thread Save
#define lqamrelease		//LanQ Argument Must release before use
#define lqautf8		    	//LanQ Argument UTF-8 string

#endif
