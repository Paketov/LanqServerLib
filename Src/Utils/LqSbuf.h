/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqSbuf... - (Lanq Stream Buffer) Implement faster stream buffer for async send or recive data from/in socket.
*/


#ifndef __STREAM_BUF_H_HAS_INCLUDED__
#define __STREAM_BUF_H_HAS_INCLUDED__

#include <stdint.h>
#include "LqOs.h"
#include "LqDef.h"

LQ_EXTERN_C_BEGIN

/*
* Common part of stream buffer.
*/
#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqSbuf
{
    uint64_t GlobOffset;
    size_t Len;
    void* Page0;
    void* PageN;
};

struct LqSbufPtr
{
    uint64_t GlobOffset;
    size_t OffsetInPage;
    void* Page;
};

#pragma pack(pop)

LQ_IMPORTEXPORT void LQ_CALL LqSbufInit(LqSbuf* StreamBuf);
LQ_IMPORTEXPORT void LQ_CALL LqSbufUninit(LqSbuf* StreamBuf);

LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufWrite(LqSbuf* StreamBuf, const void* DataSource, intptr_t DataSourceSize);
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
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufReadInFile(LqSbuf* StreamBuf, int FileDescriptor, intptr_t SizeWrite);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufWriteFromFile(LqSbuf* StreamBuf, int FileDescriptor, intptr_t SizeRead);

/*
* Pointer on various position in stream buffer.
*			Pointer 1          Pointer 2
*				v                 v
*  Start <|----------------------------------------------|> End Stream
*/

LQ_IMPORTEXPORT void LQ_CALL LqSbufPtrSet(const LqSbuf* StreamBuf, LqSbufPtr* StreamPointerDest);
/*
* @StreamBuf - Pointer on stream buf structure.
* @StreamPointer - Pointer on data in stream.
* @DataDest - If eq. nullptr, then only skipped DataDestSize, otherwise put data in buffer.
* @DataDestSize - Size read from stream.
* @return - count readed data.
*/
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufReadByPtr(const LqSbuf* StreamBuf, LqSbufPtr* StreamPointer, void* DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufPeekByPtr(const LqSbuf* StreamBuf, LqSbufPtr* StreamPointer, void* DataDest, intptr_t DataDestSize);
LQ_IMPORTEXPORT intptr_t LQ_CALL LqSbufTransferByPtr(const LqSbuf* StreamBufSource, LqSbufPtr* StreamPointer, LqSbuf* StreamBufDest, intptr_t Size);

LQ_EXTERN_C_END

#endif