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
    intptr_t Len;
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
    intptr_t   _PageDataSize;

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

/* Used for LqFbuf_open_cookie*/
#define LQFBUF_PRINTF_FLUSH      ((LqFbufFlag)0x0001) /* Flush on each call LqFbuf_printf, LqFbuf_vprintf */
#define LQFBUF_PUT_FLUSH         ((LqFbufFlag)0x0002) /* Flush on each call LqFbuf_put */
#define LQFBUF_SEP_COMMA         ((LqFbufFlag)0x0400) /* In the number with floating precision, a comma is used */

/* These flags are located in the Flag field. You can check them after call write/read/seek/printf/scanf/transfer functions*/
#define LQFBUF_WRITE_ERROR       ((LqFbufFlag)0x0004) /* An error occurred while writing */
#define LQFBUF_WRITE_WOULD_BLOCK ((LqFbufFlag)0x0008) /* When written, internal buffer overflow. You can use LqPollCheck, LqPollCheckSingle, LqWrk for check ready. */

#define LQFBUF_READ_ERROR        ((LqFbufFlag)0x0010) /* An error occurred while reading */
#define LQFBUF_READ_WOULD_BLOCK  ((LqFbufFlag)0x0020) /* There are currently no input data. You can use LqPollCheck, LqPollCheckSingle, LqWrk for check ready. */
#define LQFBUF_READ_EOF          ((LqFbufFlag)0x0040) /* The end of the input data is found. To check if all data is read (including in the internal buffer), use LqFbuf_eof func. */
#define LQFBUF_FAST_LK           ((LqFbufFlag)0x0080) /* Used to improve performance, but with a long request, the processor can be loaded */

/* Type of stream */
#define LQFBUF_POINTER           ((LqFbufFlag)0x0100) /* Virtual read-only stream (The stream exists only in RAM). When using LqFbuf_copy 
														only a pointer to this stream is copied, the pointer count increases */
#define LQFBUF_STREAM            ((LqFbufFlag)0x0200) /* Virtual pipe (The stream exists only in RAM). You can write in one side and read in other side */

/* Used when call LqFbuf_vscanf, LqFbuf_scanf  */
#define LQFBUF_SCANF_PEEK           ((int)0x01)       /* Read from the stream without resetting this buffer */
#define LQFBUF_SCANF_PEEK_WHEN_ERR  ((int)0x02)       /* Read from the stream without resetting this buffer, only when at least one argument is not read */


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

    void* UserData;		   /* Can be changed by user */
    LqFbufFlag Flags;	   /* Read/write flags, ex: LQFBUF_WRITE_ERROR, LQFBUF_READ_WOULD_BLOCK ... . Can be readed by user */
    

    intptr_t MinFlush;	   /* When this volume is reached, a flushing. Can be changed by user */
    intptr_t MaxFlush;	   /* When this volume is reached, it must necessarily be flushing. Can be changed by user */
    intptr_t PortionSize;  /* The amount of data requested for reading. Can be changed by user */

    LqFbufCookie* Cookie;  /* Can be readed by user */
    union {
        LQFRWBUF_FAST_LOCKER FastLocker;
        LQFRWBUF_DEFAULT_LOCKER Locker;
    };
} LqFbuf;

struct LqFbufCookie {
    intptr_t(LQ_CALL *ReadProc)(LqFbuf*, char*, size_t);
    intptr_t(LQ_CALL *WriteProc)(LqFbuf*, char*, size_t);
    intptr_t(LQ_CALL *SeekProc)(LqFbuf*, int64_t Offset, int Flags);
    bool(LQ_CALL *CopyProc)(LqFbuf*Dest, LqFbuf*Source);
    intptr_t(LQ_CALL *CloseProc)(LqFbuf*);
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
	Create null stream. This is analog /dev/null, but without call kernell
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_null(LqFbuf* lqaout lqats Context);
/* 
	Create virtual pipe (The stream exists only in RAM). You can write in one side and read in other side 
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_stream(LqFbuf* lqaout lqats Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_close(LqFbuf* lqain lqats Context);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_seek(LqFbuf* lqaio lqats Context, int64_t Offset, int Flags);

//////////////////

/*
* Buffering scan.
*  @Context - Source buffer
*  @Flags: LQFBUF_SCANF_PEEK - only peek buffer. Buffer can`t flush.
           LQFBUF_SCANF_PEEK_WHEN_ERR - Flush only when read all arguments.
*  @Fmt - Format string
*
*  %[flags][width][.accuracy][number_len][type]
*   flags:
                # - Extendet flags
                $ - Use for base64 or hex
                ? - Skip when not math (and increment return value)

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
                 q<num> - where num - count bit. Ex: %q8u - uint8_t, %q16i - int16_t

    type:        s - read string
                 c - read char
                 [<char>...<char2>-<char3>...] - read this chars
                 [^<char>...<char2>-<char3>...] - read all without this chars
                 {<val>|<val2>|...} - reading one of these values. #- used for case independet
                 {^<val>} - read while not same <val>,  #- used for case independet
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
/*
	LqFbuf_sizes
	Get current sizes internall buffers
		@Context - Target file stream
		@OutBuf - size output buffer
		@InBuf - size input buffer
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqFbuf_sizes(const LqFbuf* lqain Context, size_t* lqaopt lqaout OutBuf, size_t* lqaopt lqaout InBuf);

LQ_IMPORTEXPORT bool LQ_CALL LqFbuf_eof(LqFbuf* lqaio lqats Context);

/*
	Make a pointer to the virtual stream from the file stream.
	Only the intermediate buffer is saved.
	From the input or output buffer - selected by the largest size
	if @DestSource already a pointer, then only reset position pointer on start.
	Example1:
		char Buf[1024];
		LqFbuf Stream, Stream2;
		LqFbuf_stream(&Stream);
		LqFbuf_printf(&Stream, "Hello world");
		LqFbuf_make_ptr(&Stream);
		LqFbuf_copy(&Stream2, &Stream);  //When copy cookie proc CopyProc is called.
		LqFbuf_read(&Stream, Buf, 1024); //Readed "Hello world" from shared reagion
		LqFbuf_read(&Stream2, Buf, 1024); //Readed "Hello world" from shared reagion
		LqFbuf_make_ptr(&Stream);         //Reset pointer on begin stream.
		LqFbuf_close(&Stream);
		LqFbuf_close(&Stream2); //Here shared stream is deleted
	Example2:
		char Buf[1024];
		LqFbuf Stream;
		LqFbuf_open(&Stream, "/usr/me/data.bin", LQ_O_RD | LQ_O_BIN | LQ_O_SEQ, 0, 10, 4096, 128);
		LqFbuf_peek(&Stream, NULL, 1024); //Load 1024 bytes from file in RAM buffer
		LqFbuf_make_ptr(&Stream);         //We leave the internal buffer, close the file and make ptr on this internal buffer.
		LqFbuf_read(&Stream, Buf, 123); //Read in Buf 123 bytes
		LqFbuf_close(&Stream); //Here shared stream is deleted
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_make_ptr(LqFbuf* lqaio lqats DestSource);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_copy(LqFbuf* lqats lqaout Dest, LqFbuf* lqain lqats Source);
/*
  You can set cookie for pointer. By pointer used only CopyProc and CloseProc.
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_set_ptr_cookie(LqFbuf* lqats lqaout Dest, void* lqain UserData, LqFbufCookie* lqain Cookie);
/*LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_set_ptr_fd(LqFbuf* lqats lqaio Dest, int Fd); */

/*
	Transferring data directly to another stream.
	It's faster than stupid double copying.
		@Dest - Dest stream
		@Source - Source stream
		@Size - Maximum transfer length
		@return - Length transferred
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqFbuf_transfer(LqFbuf* lqats lqaio Dest, LqFbuf* lqats lqaio Source, LqFileSz Size); /* Transfer transfer data from pointer/file in to another file (Without double buffering)*/


/*
	Transferring data directly to another stream, before the user-defined sequence.
	It's faster than stupid double copying.
		@Dest - Dest stream
		@Source - Source stream
		@Size - Maximum transfer length
		@Seq - A user-declared sequence, in which the transmission ends
		@SeqSize - Size of serched seq.
		@IsCaseIndependet - For different letter sizes(only for english letters)
		@IsFound - Return true, if @seq founded
		@return - Length transferred
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqFbuf_transfer_while_not_same(
    LqFbuf* lqats lqaio Dest, 
    LqFbuf* lqats lqaio Source, 
    LqFileSz Size,
    const char* lqain Seq, 
    size_t SeqSize,
    bool IsCaseIndependet, 
    bool* lqaout IsFound
);

/*  String functions */
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_vssncanf(const char* lqain Source, size_t LenSource, const char* lqain Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_snscanf(const char* lqain Source, size_t LenSource, const char* lqain Fmt, ...);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_svnprintf(char* lqaout lqacp Dest, size_t DestSize, const char* lqain lqacp Fmt, va_list va);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqFbuf_snprintf(char* lqaout lqacp Dest, size_t DestSize, const char* lqain lqacp Fmt, ...);

/* String to/from number */

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