/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqOs - Fitures for some OS`s and compilers.
*/

#ifndef __LQ_OS_H_HAS_INCLUDED__
#define __LQ_OS_H_HAS_INCLUDED__

#if defined(_WIN64) || defined(_WIN32)

# define _CRT_SECURE_NO_WARNINGS
/* Poll method */

# if !defined(LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL) && !defined(LQEVNT_WIN_EVENT)
#  define LQEVNT_WIN_EVENT_INTERNAL_IOCP_POLL
# endif
//# define LQEVNT_WIN_EVENT
# define LQPLATFORM_WINDOWS

# pragma warning(disable : 4996)
# pragma warning(disable : 4307)

# define LQ_CALL        __cdecl
# define LQ_EXPORT      __declspec(dllexport)
# define LQ_IMPORT      __declspec(dllimport)
# define LQ_PATH_SEPARATOR  '\\'


/* For windows SysPoll*/
# ifndef LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS
#  define LQ_WINEVNT_WAIT_WHEN_GR_64_OBJECTS 20 /* millisec */
# endif
# ifndef LQ_WINEVNT_WAIT_WHEN_HAVE_ONLY_HUP_OBJ
#  define LQ_WINEVNT_WAIT_WHEN_HAVE_ONLY_HUP_OBJ 100 /* millisec */
# endif
# ifndef LQ_WINEVNT_WAIT_WHEN_HAVE_IOCP_AND_EVNTOBJ
#  define LQ_WINEVNT_WAIT_WHEN_HAVE_IOCP_AND_EVNTOBJ 10
# endif

/* For windows PollCheck*/
# ifndef LQ_POLLCHECK_WAIT_WHEN_GR_MAXIMUM_WAIT_OBJECTS
#  define LQ_POLLCHECK_WAIT_WHEN_GR_MAXIMUM_WAIT_OBJECTS ((LqTimeMillisec)10)
# endif
# ifndef LQ_POLLCHECK_WAIT_WHEN_HAVE_PIPE_OR_TERMINAL
#  define LQ_POLLCHECK_WAIT_WHEN_HAVE_PIPE_OR_TERMINAL ((LqTimeMillisec)30)
# endif

#else

# define LQPLATFORM_POSIX
# ifdef __ANDROID__
#  define LQPLATFORM_ANDROID
#  define LQ_ASYNC_IO_NOT_HAVE
# endif

# if defined(__linux__)
#  ifndef LQEVNT_EPOOL_MAX_WAIT_EVENTS
#   define LQEVNT_EPOOL_MAX_WAIT_EVENTS 128
#  endif
#  ifndef LQPLATFORM_ANDROID
#   define LQPLATFORM_LINUX
#  endif
#  define LQEVNT_EPOLL

# elif defined(__FreeBSD__) || defined(__APPLE__)

#  if defined(__FreeBSD__)
#   define LQPLATFORM_FREEBSD
/*#  define LQEVNT_KEVENT */
#   define LQEVNT_POLL
#  elif defined(__APPLE__)
/*#  define LQEVNT_KEVENT */
#   define LQEVNT_POLL
#   define LQPLATFORM_APPLE
#  endif

/*#  define LQEVNT_KEVENT
#  error "Kevent not implemented"
*/
# else
//For others unix systems
#  define LQEVNT_POLL
#  define LQPLATFORM_OTHER
# endif



# define LQ_CALL
# define LQ_EXPORT      __attribute__ ((visibility("default")))
# define LQ_IMPORT

# define LQ_PATH_SEPARATOR '/'
#endif

/* Extern defines */

#ifdef LANQBUILD
# define LQ_IMPORTEXPORT LQ_EXPORT
#else
# define LQ_IMPORTEXPORT LQ_IMPORT
#endif

#ifdef __cplusplus
# define LQ_LANG_CPLUSPLUS

# define LQ_EXTERN_C extern "C"
# define LQ_EXTERN_C_BEGIN LQ_EXTERN_C {
# define LQ_EXTERN_C_END }

# define LQ_EXTERN_CPP extern "C++"
# define LQ_EXTERN_CPP_BEGIN LQ_EXTERN_CPP {
# define LQ_EXTERN_CPP_END }

#else
# define LQ_LANG_C

# define LQ_EXTERN_C
# define LQ_EXTERN_C_BEGIN
# define LQ_EXTERN_C_END
#endif


/* Architectuire word bits */
#if defined(_WIN64)
# define LQARCH_64
#elif defined(_WIN32)
# define LQARCH_32
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


#if defined(__clang__)
# define LQCOMPILER_CLANG
#elif defined(__ICC) || defined(__INTEL_COMPILER)
# define LQCOMPILER_INTEL
#elif defined(__GNUC__) || defined(__GNUG__)
# define LQCOMPILER_GCC_GPP
#elif defined(__HP_cc) || defined(__HP_aCC)
# define LQCOMPILER_HP
#elif defined(__IBMC__) || defined(__IBMCPP__)
# define LQCOMPILER_IBM
#elif defined(_MSC_VER)
# define LQCOMPILER_VC
# ifdef _DEBUG
#  define LQ_DEBUG
# endif
#elif defined(__PGI)
# define LQCOMPILER_PGCC_PGCPP
#elif defined(__SUNPRO_C) || defined(__SUNPRO_CC)
# define LQCOMPILER_ORACLE_SOLARIS_STUDIO
#endif

#if defined(LQCOMPILER_VC)
#define LQ_NO_INLINE __declspec(noinline)
#define LQ_FORCE_INLINE __forceinline
#elif defined(LQCOMPILER_GCC_GPP)
#define LQ_NO_INLINE __attribute__ ((noinline))
#define LQ_FORCE_INLINE inline
#else
#define LQ_NO_INLINE
#define LQ_FORCE_INLINE inline
#endif

#ifdef _MSC_VER
#    if (_MSC_VER >= 1800)
#        define __alignas_is_defined 1
#    endif
#    if (_MSC_VER >= 1900)
#        define __alignof_is_defined 1
#    endif
#else
#    include <stdalign.h>   // __alignas/of_is_defined directly from the implementation
#endif

#ifdef __alignas_is_defined
#    define LQ_ALIGN(X) alignas(X)
#else
#    ifdef __GNUG__
#        define LQ_ALIGN(X) __attribute__ ((aligned(X)))
#    elif defined(_MSC_VER)
#        define LQ_ALIGN(X) __declspec(align(X))
#    else
#        define LQ_ALIGN(X) 
#    endif
#endif

#ifdef __alignof_is_defined
#    define ALIGNOF(X) alignof(x)
#else
#    ifdef __GNUG__
#        define ALIGNOF(X) __alignof__ (X)
#    elif defined(_MSC_VER)
#        define ALIGNOF(X) __alignof(X)
#    else
#        define ALIGNOF(X)
#    endif
#endif

#ifndef LQSTRUCT_ALIGN_FAST
/* Set struct or class align */
# ifdef LQARCH_32
#  define LQSTRUCT_ALIGN_FAST 4 /* Optimize for CPU cash */
# else
#  define LQSTRUCT_ALIGN_FAST 8 /* Optimize for CPU cash */
# endif
#endif

#ifndef LQSTRUCT_ALIGN_MEM
# define LQSTRUCT_ALIGN_MEM 1 /* Optimize for memory */
#endif


#define __STDINT_LIMITS

#define lqaopt                  /* LanQ Argument Optional */
#define lqain                   /* LanQ Argument Input */
#define lqaio                   /* LanQ Argument Input, output */
#define lqaout                  /* LanQ Argument Output */
#define lqats                   /* LanQ Argument Thread Save */
#define lqatns                  /* LanQ Argument Not Thread Save */
#define lqamrelease             /* LanQ Argument Must release before use */
#define lqautf8                 /* LanQ Argument UTF-8 string */
#define lqautf16                /* LanQ Argument UTF-16 string */
#define lqacp                   /* LanQ Argument defined in LqCp ... code page */
#define lqacpeng                /* LanQ Argument only english letters */

#define LqStructByField(TypeStruct, FieldName, AddressField) ((TypeStruct*)(((char*)(AddressField)) - ((uintptr_t)&((TypeStruct*)0)->FieldName)))

#define LQ_GOLDEN_RATIO         1.61803398874989484820 

#endif

