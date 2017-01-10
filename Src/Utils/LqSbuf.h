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

LQ_IMPORTEXPORT void LQ_CALL LqSbufInit(LqSbuf* StreamBuf);
LQ_IMPORTEXPORT void LQ_CALL LqSbufUninit(LqSbuf* StreamBuf);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufWrite(LqSbuf* StreamBuf, const void* lqain DataSource, intptr_t DataSourceSize);
/*
* @StreamBuf - Pointer on stream buf structure.
* @DataDest - If eq. nullptr, then only skipped DataDestSize, otherwise put data in buffer.
* @DataDestSize - Size read from stream.
* @return - count readed data.
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufRead(LqSbuf* StreamBuf, void* DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufPeek(const LqSbuf* StreamBuf, void* DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufTransfer(LqSbuf* StreamBufSource, LqSbuf* StreamBufDest, intptr_t Size);

/*
* Redirect part of stream in file, or read from file.
*/

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufCheck(LqSbuf* StreamBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionFirst(LqSbuf* StreamBuf, LqSbufReadRegion* Reg, intptr_t Size);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionNext(LqSbufReadRegion* Reg);  /* Get next page*/
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionIsEos(LqSbufReadRegion* Reg); /* Is end of stream (last page)*/

LQ_IMPORTEXPORT bool LQ_CALL LqSbufWriteRegionFirst(LqSbuf* StreamBuf, LqSbufWriteRegion* Reg, intptr_t Size);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufWriteRegionNext(LqSbufWriteRegion* Reg);



/*
* Pointer on various position in stream buffer.
*           Pointer 1          Pointer 2
*               v                 v
*  Start <|----------------------------------------------|> End Stream
*/

LQ_IMPORTEXPORT void LQ_CALL LqSbufPtrSet(LqSbuf* StreamBuf, LqSbufPtr* StreamPointerDest);

LQ_IMPORTEXPORT void LQ_CALL LqSbufPtrCopy(LqSbufPtr* StreamPointerDest, LqSbufPtr* StreamPointerSource);
/*
* @StreamBuf - Pointer on stream buf structure.
* @StreamPointer - Pointer on data in stream.
* @DataDest - If eq. nullptr, then only skipped DataDestSize, otherwise put data in buffer.
* @DataDestSize - Size read from stream.
* @return - count readed data.
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufReadByPtr(LqSbufPtr* StreamPointer, void* lqaout DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufPeekByPtr(LqSbufPtr* StreamPointer, void* lqaout DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufTransferByPtr(LqSbufPtr* StreamPointer, LqSbuf* StreamBufDest, intptr_t Size);

LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionPtrFirst(LqSbufPtr* StreamPointer, LqSbufReadRegionPtr* Reg, intptr_t Size);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionPtrNext(LqSbufReadRegionPtr* Reg);
LQ_IMPORTEXPORT bool LQ_CALL LqSbufReadRegionPtrIsEos(LqSbufReadRegionPtr* Reg);
/*
* File io
*/

#define LQFWBUF_PRINTF_FLUSH   ((uint8_t)0x01)
#define LQFWBUF_PUT_FLUSH      ((uint8_t)0x02)
#define LQFWBUF_WRITE_ERROR    ((uint8_t)0x04)
#define LQFWBUF_WOULD_BLOCK    ((uint8_t)0x08)


#define LQFRBUF_READ_ERROR     ((uint8_t)0x10)
#define LQFRBUF_WOULD_BLOCK    ((uint8_t)0x20)
#define LQFRBUF_READ_EOF       ((uint8_t)0x40)
#define LQFRWBUF_FAST_LK       ((uint8_t)0x80)

#define LQFRBUF_SCANF_PEEK    ((int)0x01)
#define LQFRBUF_SCANF_PEEK_WHEN_ERR    ((int)0x02)


#define LQFRWBUF_FAST_LOCKER     int
#define LQFRWBUF_DEFAULT_LOCKER  LqMutex


typedef struct LqFwbuf {
    void* UserData;
    LqSbuf Buf;
    intptr_t MinFlush;
    intptr_t MaxFlush;
    uint8_t Flags;
    char FloatSep;
    intptr_t(*WriteProc)(LqFwbuf*, char*, size_t);
    union {
        LQFRWBUF_FAST_LOCKER FastLocker;
        LQFRWBUF_DEFAULT_LOCKER Locker;
    };
} LqFwbuf;

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_fdopen(
    LqFwbuf* Context,
    int Fd,
    uint8_t Flags,
    size_t MinBuffSize,
    size_t MaxBuffSize
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_init(
    LqFwbuf* Context,
    uint8_t Flags,
    intptr_t(*WriteProc)(LqFwbuf*, char*, size_t),
    char FloatSep,
    void* UserData,
    size_t MinBuffSize,
    size_t MaxBuffSize
);

LQ_IMPORTEXPORT void LQ_CALL LqFwbuf_uninit(LqFwbuf* Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_open(
    LqFwbuf* Context,
    const char* FileName,
    uint32_t Flags,
    int Access,
    size_t MinBuffSize,
    size_t MaxBuffSize
);

LQ_IMPORTEXPORT void LQ_CALL LqFwbuf_close(LqFwbuf* Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_vprintf(LqFwbuf* Context, const char* Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_printf(LqFwbuf* Context, const char* Fmt, ...);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_putc(LqFwbuf* Context, int Val);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_puts(LqFwbuf* Context, const char* Val);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_write(LqFwbuf* Context, const void* Buf, size_t Size);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_putsn(LqFwbuf* Context, const char* Val, size_t Len);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_flush(LqFwbuf* Context);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_size(LqFwbuf* Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_svnprintf(char* Dest, size_t DestSize, const char* Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFwbuf_snprintf(char* Dest, size_t DestSize, const char* Fmt, ...);
////************************************************/

typedef struct LqFrbuf {
    void* UserData;
    LqSbuf Buf;
    intptr_t PortionSize;
    uint8_t Flags;
    char FloatSep;
    intptr_t(*ReadProc)(LqFrbuf*, char*, size_t);
    union {
        LQFRWBUF_FAST_LOCKER FastLocker;
        LQFRWBUF_DEFAULT_LOCKER Locker;
    };
} LqFrbuf;

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_init(
    LqFrbuf* Context,
    uint8_t Flags,
    intptr_t(*ReadProc)(LqFrbuf*, char*, size_t),
    char FloatSep,
    void* UserData,
    size_t PortionSize
);

LQ_IMPORTEXPORT void LQ_CALL LqFrbuf_uninit(LqFrbuf* Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_fdopen(
    LqFrbuf* Context,
    int Fd,
    uint8_t Flags,
    size_t PortionSize
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_open(
    LqFrbuf* Context,
    const char* FileName,
    uint32_t Flags,
    int Access,
    size_t PortionSize
);

LQ_IMPORTEXPORT void LQ_CALL LqFrbuf_close(LqFrbuf* Context);

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
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_vscanf(LqFrbuf* Context, int Flags, const char* Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_scanf(LqFrbuf* Context, int Flags, const char* Fmt, ...);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_getc(LqFrbuf* Context, int* Dest);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_read(LqFrbuf* Context, void* Buf, size_t Len);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_peek(LqFrbuf* Context, void* Buf, size_t Len);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_flush(LqFrbuf* Context);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_size(LqFrbuf* Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_vssncanf(const char* Source, size_t LenSource, const char* Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFrbuf_snscanf(const char* Source, size_t LenSource, const char* Fmt, ...);

//////

typedef struct LqFbuf {
    LqFwbuf OutBuf;
    LqFrbuf InBuf;
    intptr_t(*SeekProc)(LqFbuf* Buf, int64_t Offset, int Flags);
} LqFbuf;


LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_init(
    LqFbuf* Context,
    uint8_t Flags,
    intptr_t(*ReadProc)(LqFrbuf*, char*, size_t),
    intptr_t(*WriteProc)(LqFwbuf*, char*, size_t),
    intptr_t(*SeekProc)(LqFbuf*, int64_t Offset, int Flags),
    char FloatSep,
    void* UserData,
    size_t WriteMinBuffSize,
    size_t WriteMaxBuffSize,
    size_t ReadPortionSize
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_fdopen(
    LqFbuf* Context,
    uint8_t Flags,
    int Fd,
    char FloatSep,
    size_t WriteMinBuffSize,
    size_t WriteMaxBuffSize,
    size_t ReadPortionSize
);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_open(
    LqFbuf* Context,
    const char* FileName,
    uint32_t FileFlags,
    int Access,
    char FloatSep,
    size_t WriteMinBuffSize,
    size_t WriteMaxBuffSize,
    size_t ReadPortionSize
);

LQ_IMPORTEXPORT void LQ_CALL LqFbuf_uninit(LqFbuf* Context);
LQ_IMPORTEXPORT void LQ_CALL LqFbuf_close(LqFbuf* Context);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_seek(LqFbuf* Context, int64_t Offset, int Flags);


#define LqFbuf_vscanf(Context, Flags, Fmt, va)      (LqFrbuf_vscanf(&(Context)->InBuf,(Flags), (Fmt), (va)))
#define LqFbuf_scanf(Context, Flags, Fmt, ...)      (LqFrbuf_scanf(&(Context)->InBuf,(Flags), (Fmt), ##__VA_ARGS__))
#define LqFbuf_getc(Context, Dest)                  (LqFrbuf_getc(&(Context)->InBuf, (Dest)))
#define LqFbuf_read(Context, Buf, Len)              (LqFrbuf_read(&(Context)->InBuf, (Buf), (Len)))
#define LqFbuf_peek(Context, Buf, Len)              (LqFrbuf_peek(&(Context)->InBuf, (Buf), (Len)))

#define LqFbuf_flush(Context)                       (LqFwbuf_flush(&(Context)->OutBuf))
#define LqFbuf_vprintf(Context, Fmt, va)            (LqFwbuf_vprintf(&(Context)->OutBuf, (Fmt), (va)))
#define LqFbuf_printf(Context, Fmt, ...)            (LqFwbuf_printf(&(Context)->OutBuf, (Fmt), ##__VA_ARGS__))
#define LqFbuf_putc(Context, Val)                   (LqFwbuf_putc(&(Context)->OutBuf, (Val)))
#define LqFbuf_puts(Context, Val)                   (LqFwbuf_puts(&(Context)->OutBuf, (Val)))
#define LqFbuf_write(Context, Buf, Size)            (LqFwbuf_write(&(Context)->OutBuf, (Buf), (Size)))
#define LqFbuf_putsn(Context, Val, Len)             (LqFwbuf_putsn(&(Context)->OutBuf, (Val), (Len)))


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