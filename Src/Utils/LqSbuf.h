/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqSbuf... - (Lanq Stream Buffer) Implement faster stream buffer for async send/recive data from/in socket.
*/


#ifndef __STREAM_BUF_H_HAS_INCLUDED__
#define __STREAM_BUF_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqDef.h"
#include "LqFile.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

LQ_EXTERN_C_BEGIN

/*
* Common part of stream buffer.
*/
#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

typedef struct LqSbuf {
    uint64_t GlobOffset;
    size_t Len;
    void* Page0;
    void* PageN;
} LqSbuf;

typedef struct LqSbufPtr {
    LqSbuf*  StreamBuf;
    uint64_t GlobOffset;
    size_t   OffsetInPage;
    void*    Page;
} LqSbufPtr;

#pragma pack(pop)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

typedef struct LqSbufWriteRegion {
    LqSbuf*    _StreamBuf;
    intptr_t   _TypeOperat;
    intptr_t   _Size;
    void*      _Hdr;

    void*    Dest;
    intptr_t DestLen;
    intptr_t Readed;
    intptr_t CommonReaded;
    bool     Fin;
} LqSbufWriteRegion;

typedef struct LqSbufReadRegion {
    LqSbuf*    _StreamBuf;
    intptr_t   _Size;
    void*      _Hdr;
    size_t     _PageDataSize;

    void*    Source;
    intptr_t SourceLen;
    intptr_t Written;
    intptr_t CommonWritten;
    bool     Fin;
} LqSbufReadRegion;

typedef struct LqSbufReadRegionPtr {
    LqSbufPtr* _StreamPointer;
    intptr_t   _Size;

    void*    Source;
    intptr_t SourceLen;
    intptr_t Written;
    intptr_t CommonWritten;
    bool     Fin;
} LqSbufReadRegionPtr;

#pragma pack(pop)

LQ_IMPORTEXPORT void LQ_CALL LqSbufInit(LqSbuf* lqaout StreamBuf);
LQ_IMPORTEXPORT void LQ_CALL LqSbufUninit(LqSbuf* lqain StreamBuf);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufWrite(LqSbuf* lqaio lqatns StreamBuf, const void* lqain DataSource, intptr_t DataSourceSize);
/*
* @StreamBuf - Pointer on stream buf structure.
* @DataDest - If eq. nullptr, then only skipped DataDestSize, otherwise put data in buffer.
* @DataDestSize - Size read from stream.
* @return - count readed data.
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufRead(LqSbuf* lqaio lqatns StreamBuf, void* lqaout DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufPeek(const LqSbuf* lqain lqatns StreamBuf, void* lqaout DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufTransfer(LqSbuf* lqaio lqatns StreamBufDest, LqSbuf* StreamBufSource, intptr_t Size);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufCopy(LqSbuf* lqaio lqatns StreamDest, const LqSbuf* lqain lqatns StreamSource);

/*
* Read data directly from regions(Without double buffering).
*  Used for fast data transfer.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionFirst(LqSbuf* lqaio lqatns StreamBuf, LqSbufReadRegion* lqaout lqatns Reg, intptr_t Size);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionNext(LqSbufReadRegion* lqaio lqatns Reg);  /* Get next page*/
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionIsEos(LqSbufReadRegion* lqain lqatns Reg); /* Is end of stream (last page)*/

/*
* Write data directly in memory(Without double buffering).
*  Used for fast data transfer.
*/
LQ_IMPORTEXPORT bool LQ_CALL LqSbufWriteRegionFirst(LqSbuf* lqaio lqatns StreamBuf, LqSbufWriteRegion* lqaout lqatns Reg, intptr_t Size);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufWriteRegionNext(LqSbufWriteRegion* lqaio lqatns Reg);



/*
* Pointer on various position in stream buffer.
*           Pointer 1          Pointer 2
*               v                 v
*  Start <|----------------------------------------------|> End Stream
*/

LQ_IMPORTEXPORT void LQ_CALL LqSbufPtrSet(LqSbufPtr* lqaout lqatns StreamPointerDest, LqSbuf* lqain lqatns StreamBuf);
LQ_IMPORTEXPORT void LQ_CALL LqSbufPtrCopy(LqSbufPtr* lqaout lqatns StreamPointerDest, const LqSbufPtr* lqain lqatns StreamPointerSource);


LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufReadByPtr(LqSbufPtr* lqaio lqatns StreamPointer, void* lqaout DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufPeekByPtr(LqSbufPtr* lqaio lqatns StreamPointer, void* lqaout DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufTransferByPtr(LqSbufPtr* lqaio lqatns StreamPointer, LqSbuf* lqaio lqatns StreamBufDest, intptr_t Size);

LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionPtrFirst(LqSbufPtr* lqaio lqatns StreamPointer, LqSbufReadRegionPtr* lqaout lqatns Reg, intptr_t Size);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionPtrNext(LqSbufReadRegionPtr* lqaio lqatns Reg);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionPtrIsEos(LqSbufReadRegionPtr* lqain lqatns Reg);

/*
* File io
*/

typedef uint16_t LqFbufFlag;

#define LQFBUF_PRINTF_FLUSH      ((LqFbufFlag)0x0001)
#define LQFBUF_PUT_FLUSH         ((LqFbufFlag)0x0002)
#define LQFBUF_WRITE_ERROR       ((LqFbufFlag)0x0004)
#define LQFBUF_WRITE_WOULD_BLOCK ((LqFbufFlag)0x0008)

#define LQFBUF_READ_ERROR        ((LqFbufFlag)0x0010)
#define LQFBUF_READ_WOULD_BLOCK  ((LqFbufFlag)0x0020)
#define LQFBUF_READ_EOF          ((LqFbufFlag)0x0040)
#define LQFBUF_FAST_LK		     ((LqFbufFlag)0x0080)

#define LQFBUF_POINTER           ((LqFbufFlag)0x0100)
#define LQFBUF_STREAM            ((LqFbufFlag)0x0200)
#define LQFBUF_SEP_COMMA         ((LqFbufFlag)0x0400)


#define LQFRBUF_SCANF_PEEK           ((int)0x01)
#define LQFRBUF_SCANF_PEEK_WHEN_ERR  ((int)0x02)


#define LQFRWBUF_FAST_LOCKER     int
#define LQFRWBUF_DEFAULT_LOCKER  LqMutex

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqFbufCookie;
typedef struct LqFbufCookie LqFbufCookie;

typedef struct LqFbuf {
	union {
		struct {
			LqSbuf OutBuf;
			LqSbuf InBuf;
		};
		LqSbufPtr BufPtr;
	};

	void* UserData;
	LqFbufFlag Flags;
	

	intptr_t MinFlush;
	intptr_t MaxFlush;
	intptr_t PortionSize;

	LqFbufCookie* Cookie;
	union {
		LQFRWBUF_FAST_LOCKER FastLocker;
		LQFRWBUF_DEFAULT_LOCKER Locker;
	};
} LqFbuf;

struct LqFbufCookie {
	intptr_t(*ReadProc)(LqFbuf*, char*, size_t);
	intptr_t(*WriteProc)(LqFbuf*, char*, size_t);
	intptr_t(*SeekProc)(LqFbuf*, int64_t Offset, int Flags);
	bool(*CopyProc)(LqFbuf*Dest, LqFbuf*Source);
	intptr_t(*CloseProc)(LqFbuf*);
};

#pragma pack(pop)


LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_open_cookie(
    LqFbuf* lqaout Context,
	void* UserData,
	LqFbufCookie* lqain Cookie,
	LqFbufFlag Flags,
	intptr_t WriteMinBuffSize,
	intptr_t WriteMaxBuffSize,
	intptr_t ReadPortionSize
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_fdopen(
    LqFbuf* lqaout Context,
	LqFbufFlag Flags,
    int Fd,
	intptr_t WriteMinBuffSize,
	intptr_t WriteMaxBuffSize,
	intptr_t ReadPortionSize
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_open(
    LqFbuf* lqaout Context,
    const char* lqain FileName,
    uint32_t FileFlags,
    int Access,
	intptr_t WriteMinBuffSize,
	intptr_t WriteMaxBuffSize,
	intptr_t ReadPortionSize
);
/*
* Create stream pipe
*  you can write some data and read them.
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_stream(LqFbuf* lqaout lqats Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_close(LqFbuf* lqain lqats Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_seek(LqFbuf* lqaio lqats Context, int64_t Offset, int Flags);

//////////////////

/*
* Buffering scan.
*  @Context - Source buffer
*  @Flags: LQFRBUF_SCANF_PEEK - only peek buffer. Buffer can`t flush.
           LQFRBUF_SCANF_PEEK_WHEN_ERR - Flush only when read all arguments.
*  @Fmt - Format string
*
*  %[flags][width][.accuracy][number_len][type]
*   flags:
                # - Extendet flags
                $ - Use for base64 or hex
                ? - Skip when not math

    width:       when *(star), not set result value

    accuracy:    Set max read len, can be *

    number_len:  hh - char
                 h  - short
                 l - long
                 ll - long long
                 j - intmax_t
                 z - size_t
                 t - ptrdiff_t
                 r - intptr_t
                 q<num> - where num - count bit. Ex: %q8u - uint8_t

    type:        s - read string
                 c - read char
                 [<char>...<char2>-<char3>...] - read this chars
                 [^<char>...<char2>-<char3>...] - read all without this chars
                 {<val>|<val2>|...} - reading one of these values
                 b - read tradition base64. Flag # used for read = char. Flag $ used for get written size(for get readed size use %n).
                 B - read URL base 64. Flag # used for read = char. Flag $ used for get written size(for get readed size use %n).
                 V, v - read HEX data.  Flag $ used for get written size(for get readed size use %n).
                 X, x - read HEX number.
                 o - read octal number.
                 u - read dec unsigned number
                 i, d - read dec signed number
                 A, a - read hex floating point number
                 E, e - read expanent floating point number
                 G, g - read auto expanent floating point number
                 F, f - read floating point number without expanent
                 n - get count readed chars
*/

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_vscanf(LqFbuf* lqaio lqats Context, int Flags, const char* lqain lqacp Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_scanf(LqFbuf* lqaio lqats Context, int Flags, const char* lqain lqacp Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_getc(LqFbuf* lqaio lqats Context, int* lqaout Dest);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_read(LqFbuf* lqaio lqats Context, void* lqaout lqaopt Buf, size_t Len);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_peek(LqFbuf* lqaio lqats Context, void* lqaout lqaopt Buf, size_t Len);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_flush(LqFbuf* lqaio lqats Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_vprintf(LqFbuf* lqaio lqats Context, const char* lqain Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_printf(LqFbuf* lqaio lqats Context, const char* lqain Fmt, ...);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_putc(LqFbuf* lqaio lqats Context, int Val);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_puts(LqFbuf* lqaio lqats Context, const char* lqain lqacp Val);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_write(LqFbuf* lqaio lqats Context, const void* lqain Buf, size_t Size);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_putsn(LqFbuf* lqaio lqats Context, const char* lqain lqacp Val, size_t Len);
LQ_IMPORTEXPORT size_t LQ_CALL LqFbuf_sizes(const LqFbuf* lqain Context, size_t* lqaopt lqaout OutBuf, size_t* lqaopt lqaout InBuf);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_vssncanf(const char* lqain Source, size_t LenSource, const char* lqain Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_snscanf(const char* lqain Source, size_t LenSource, const char* lqain Fmt, ...);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_svnprintf(char* lqaout lqacp Dest, size_t DestSize, const char* lqain lqacp Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_snprintf(char* lqaout lqacp Dest, size_t DestSize, const char* lqain lqacp Fmt, ...);


LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_make_ptr(LqFbuf* lqaio lqats DestSource);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_copy(LqFbuf* lqats lqaout Dest, LqFbuf* lqain lqats Source);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_set_ptr_cookie(LqFbuf* lqats lqaout Dest, void* lqain UserData, LqFbufCookie* lqain Cookie);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_set_ptr_fd(LqFbuf* lqats lqaio Dest, int Fd);/* For write in file (use LqFbuf_flush())*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_transfer(LqFbuf* lqats lqaio Dest, LqFbuf* lqats lqaio Source, size_t Size); /* Transfer transfer data from pointer/file in to another file (Without double buffering)*/

//////////////////

LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrToInt(int* Dest, const char* Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrToLl(long long* Dest, const char* Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrToUint(unsigned int* Dest, const char* Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrToUll(unsigned long long* Dest, const char* Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrFromInt(char* Dest, int Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrFromLl(char* Dest, long long Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrFromUint(char* Dest, unsigned int Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrFromUll(char* Dest, unsigned long long Source, unsigned char Radix);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrToFloat(float* Dest, const char* Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrToDouble(double* Dest, const char* Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrFromFloat(char* Dest, float Source, unsigned char Radix);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqStrFromDouble(char* Dest, double Source, unsigned char Radix);

LQ_EXTERN_C_END

#endif