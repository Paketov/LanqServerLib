/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqSbuf... - (Lanq Stream Buffer) Implement faster stream buffer for async send or recive data from/in socket.
*/


#include "LqFile.h"
#include "LqSbuf.h"
#include "LqAlloc.hpp"

#include <string.h>

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct PageHeader
{
	size_t SizePage;
	size_t StartOffset;
	size_t EndOffset;
	void* NextPage;
};

#pragma pack(pop)


template<size_t LenPage>
static PageHeader* LqSbufCreatePage(LqSbuf* StreamBuf)
{
	struct PageSize { uint8_t v[LenPage]; };
	auto NewPage = LqFastAlloc::New<PageSize>();
	if(NewPage == nullptr)
		return nullptr;
	PageHeader* Hdr = (PageHeader*)NewPage;
	if(StreamBuf->PageN == nullptr)
	{
		StreamBuf->Page0 = StreamBuf->PageN = Hdr;
	} else
	{
		((PageHeader*)StreamBuf->PageN)->NextPage = Hdr;
		StreamBuf->PageN = Hdr;
	}
	Hdr->NextPage = nullptr;
	Hdr->EndOffset = Hdr->StartOffset = 0;
	Hdr->SizePage = sizeof(PageSize) - sizeof(PageHeader);
	return Hdr;
}

static PageHeader* LqSbufCreatePage(LqSbuf* StreamBuf, intptr_t Size)
{
	if(Size <= 0) return nullptr;
	if(Size < (4096 - sizeof(PageHeader)))
		return LqSbufCreatePage<4096>(StreamBuf);
	else if(Size < (16384 - sizeof(PageHeader)))
		return LqSbufCreatePage<16384>(StreamBuf);
	else
		return LqSbufCreatePage<32768>(StreamBuf);
}

template<size_t LenPage>
static void LqSbufRemovePage(LqSbuf* StreamBuf)
{
	struct PageSize { uint8_t v[LenPage]; };
	PageHeader* Hdr = (PageHeader*)StreamBuf->Page0;
	StreamBuf->Page0 = Hdr->NextPage;
	if(StreamBuf->Page0 == nullptr)
		StreamBuf->PageN = nullptr;
	LqFastAlloc::Delete<PageSize>((PageSize*)Hdr);
}

static void LqSbufRemoveLastPage(LqSbuf* StreamBuf)
{
	PageHeader* Hdr = (PageHeader*)StreamBuf->PageN;
	if(Hdr->SizePage <= (4096 - sizeof(PageHeader)))
		LqSbufRemovePage<4096>(StreamBuf);
	else if(Hdr->SizePage <= (16384 - sizeof(PageHeader)))
		LqSbufRemovePage<16384>(StreamBuf);
	else if(Hdr->SizePage <= (32768 - sizeof(PageHeader)))
		LqSbufRemovePage<32768>(StreamBuf);
}

static void LqSbufRemoveFirstPage(LqSbuf* StreamBuf)
{
	PageHeader* Hdr = (PageHeader*)StreamBuf->Page0;
	if(Hdr->SizePage <= (4096 - sizeof(PageHeader)))
		LqSbufRemovePage<4096>(StreamBuf);
	else if(Hdr->SizePage <= (16384 - sizeof(PageHeader)))
		LqSbufRemovePage<16384>(StreamBuf);
	else if(Hdr->SizePage <= (32768 - sizeof(PageHeader)))
		LqSbufRemovePage<32768>(StreamBuf);
}

static intptr_t LqSbufAddPages(LqSbuf* StreamBuf, const void* Data, intptr_t Size)
{
	intptr_t Written = 0;
	do
	{
		auto DestPage = LqSbufCreatePage(StreamBuf, Size);
		if(DestPage == nullptr) break;
		intptr_t SizeWrite = lq_min(Size, DestPage->SizePage);
		memcpy(DestPage + 1, (char*)Data + Written, SizeWrite);
		DestPage->EndOffset = DestPage->StartOffset + SizeWrite;
		Written += SizeWrite;
		Size -= SizeWrite;
	} while(Size > 0);
	StreamBuf->Len += Written;
	return Written;
}

static intptr_t LqSbufAddPagesFromFile(LqSbuf* StreamBuf, int FileDescriptor, intptr_t Size)
{
	intptr_t Written = 0;
	do
	{
		auto DestPage = LqSbufCreatePage(StreamBuf, Size);
		if(DestPage == nullptr) break;
		intptr_t SizeWrite = lq_min(Size, DestPage->SizePage);
		int ReadedSize = LqFileRead(FileDescriptor, DestPage + 1, SizeWrite);
		if(ReadedSize < 0)
		{
			LqSbufRemoveLastPage(StreamBuf);
			break;
		}
		Written += ReadedSize;
		if(ReadedSize < SizeWrite) break;
		Size -= ReadedSize;
	} while(Size > 0);
	StreamBuf->Len += Written;
	return Written;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufWrite(LqSbuf* StreamBuf, const void* DataSource, intptr_t Size)
{
	if(StreamBuf->PageN == nullptr)
		return LqSbufAddPages(StreamBuf, DataSource, Size);
	intptr_t CommonWritten = 0;
	if(((PageHeader*)StreamBuf->PageN)->EndOffset < ((PageHeader*)StreamBuf->PageN)->SizePage)
	{
		PageHeader* Hdr = (PageHeader*)StreamBuf->PageN;
		auto SizeWrite = lq_min(Hdr->SizePage - Hdr->EndOffset, Size);
		memcpy((char*)(Hdr + 1) + Hdr->EndOffset, DataSource, SizeWrite);
		DataSource = (char*)DataSource + SizeWrite;
		Size -= SizeWrite;
		Hdr->EndOffset += SizeWrite;
		CommonWritten += SizeWrite;
		StreamBuf->Len += SizeWrite;
	}
	if(Size > 0)
		CommonWritten += LqSbufAddPages(StreamBuf, DataSource, Size);
	return CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufRead(LqSbuf* StreamBuf, void* DestBuf, intptr_t Size)
{
	intptr_t CommonReadedSize = 0;
	while(StreamBuf->Page0 != nullptr)
	{
		PageHeader* HdrPage = (PageHeader*)StreamBuf->Page0;
		intptr_t PageDataSize = HdrPage->EndOffset - HdrPage->StartOffset;
		intptr_t ReadSize = lq_min(PageDataSize, Size);
		if(DestBuf != nullptr)
			memcpy((char*)DestBuf + CommonReadedSize, (char*)(HdrPage + 1) + HdrPage->StartOffset, ReadSize);
		CommonReadedSize += ReadSize;
		Size -= ReadSize;
		if(Size <= 0)
		{
			HdrPage->StartOffset += ReadSize;
			break;
		} else
		{
			LqSbufRemoveFirstPage(StreamBuf);
		}
	}
	StreamBuf->Len -= CommonReadedSize;
	StreamBuf->GlobOffset += CommonReadedSize;
	return CommonReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufPeek(const LqSbuf* StreamBuf, void* DataDest, intptr_t Size)
{
	intptr_t ReadedSize = 0;
	for(PageHeader* Hdr = (PageHeader*)StreamBuf->Page0; (Size > ReadedSize) && (Hdr != nullptr); Hdr = (PageHeader*)Hdr->NextPage)
	{
		intptr_t LenRead = lq_min(Size - ReadedSize, Hdr->EndOffset - Hdr->StartOffset);
		memcpy((char*)DataDest + ReadedSize, (char*)(Hdr + 1) + Hdr->StartOffset, LenRead);
		ReadedSize += LenRead;
	}
	return ReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufTransfer(LqSbuf* StreamBufSource, LqSbuf* StreamBufDest, intptr_t Size)
{
	intptr_t CommonWrittenSize = 0;
	while(true)
	{
		if(StreamBufSource->Page0 == nullptr) break;
		PageHeader* Hdr = (PageHeader*)StreamBufSource->Page0;
		intptr_t PageDataSize = Hdr->EndOffset - Hdr->StartOffset;
		intptr_t ReadSize = lq_min(PageDataSize, Size);
		intptr_t Written = LqSbufWrite(StreamBufDest, (char*)(Hdr + 1) + Hdr->StartOffset, ReadSize);
		CommonWrittenSize += Written;
		Size -= Written;
		if(Written < PageDataSize)
		{
			Hdr->StartOffset += Written;
			break;
		} else
		{
			LqSbufRemoveFirstPage(StreamBufSource);
		}
		if(Written < ReadSize) break;
	}
	StreamBufSource->Len -= CommonWrittenSize;
	StreamBufSource->GlobOffset += CommonWrittenSize;
	return CommonWrittenSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufReadInFile(LqSbuf * StreamBuf, int fd, intptr_t Size)
{
	intptr_t CommonWrittenSize = 0;
	while(StreamBuf->Page0 != nullptr)
	{
		PageHeader* Hdr = (PageHeader*)StreamBuf->Page0;
		intptr_t PageDataSize = Hdr->EndOffset - Hdr->StartOffset;
		intptr_t ReadSize = lq_min(PageDataSize, Size);
		int Written = LqFileWrite(fd, (char*)(Hdr + 1) + Hdr->StartOffset, ReadSize);
		if(Written < 0) break;
		CommonWrittenSize += Written;
		Size -= Written;
		if(Written < PageDataSize)
		{
			Hdr->StartOffset += Written;
			break;
		} else
		{
			LqSbufRemoveFirstPage(StreamBuf);
		}
		if(Written < ReadSize) break;
	}
	StreamBuf->Len -= CommonWrittenSize;
	StreamBuf->GlobOffset += CommonWrittenSize;
	return CommonWrittenSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufWriteFromFile(LqSbuf* StreamBuf, int FileDescriptor, intptr_t Size)
{
	if(StreamBuf->PageN == nullptr)
		return LqSbufAddPagesFromFile(StreamBuf, FileDescriptor, Size);

	intptr_t CommonReaded = 0;
	if(((PageHeader*)StreamBuf->PageN)->EndOffset < ((PageHeader*)StreamBuf->PageN)->SizePage)
	{
		PageHeader* Hdr = (PageHeader*)StreamBuf->PageN;
		auto LenReadInCurPage = lq_min(Hdr->SizePage - Hdr->EndOffset, Size);
		int ReadedSize = LqFileRead(FileDescriptor, (char*)(Hdr + 1) + Hdr->EndOffset, LenReadInCurPage);
		if(ReadedSize < 0)
			return CommonReaded;
		Size -= ReadedSize;
		Hdr->EndOffset += ReadedSize;
		CommonReaded += ReadedSize;
		StreamBuf->Len += ReadedSize;
		if(ReadedSize < LenReadInCurPage)
			return CommonReaded;
	}
	if(Size > 0)
		CommonReaded += LqSbufAddPagesFromFile(StreamBuf, FileDescriptor, Size);

	return CommonReaded;
}

LQ_EXTERN_C void LQ_CALL LqSbufPtrSet(const LqSbuf* StreamBuf, LqSbufPtr* StreamPointerDest)
{
	StreamPointerDest->GlobOffset = StreamBuf->GlobOffset;
	StreamPointerDest->Page = StreamBuf->Page0;
	StreamPointerDest->OffsetInPage = (StreamBuf->Page0 != nullptr) ? ((PageHeader*)StreamBuf->Page0)->StartOffset : 0;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufReadByPtr(const LqSbuf* StreamBuf, LqSbufPtr* StreamPointer, void* DataDest, intptr_t Size)
{
	if((StreamPointer->GlobOffset < StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamBuf->GlobOffset + StreamBuf->Len)))
		return -1;
	intptr_t ReadedSize = 0;
	PageHeader* PageHdr;
	size_t StartOffset;
	if(StreamPointer->Page == nullptr)
	{
		PageHdr = (PageHeader*)StreamBuf->Page0;
		StartOffset = PageHdr->StartOffset;
	} else
	{
		PageHdr = (PageHeader*)StreamPointer->Page;
		StartOffset = StreamPointer->OffsetInPage;
	}
	for(; ; )
	{
		intptr_t LenReadInCurPage = lq_min(Size - ReadedSize, PageHdr->EndOffset - StartOffset);
		if(DataDest != nullptr)
			memcpy((char*)DataDest + ReadedSize, (char*)(PageHdr + 1) + StartOffset, LenReadInCurPage);
		ReadedSize += LenReadInCurPage;
		if((PageHdr->NextPage == nullptr) || (Size <= ReadedSize))
		{
			if(PageHdr == StreamPointer->Page)
			{
				StreamPointer->OffsetInPage += LenReadInCurPage;
			} else
			{
				StreamPointer->OffsetInPage = StartOffset + LenReadInCurPage;
				StreamPointer->Page = PageHdr;
			}
			StreamPointer->GlobOffset += ReadedSize;
			break;
		}
		PageHdr = (PageHeader*)PageHdr->NextPage;
		StartOffset = PageHdr->StartOffset;
	}
	return ReadedSize;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufTransferByPtr(const LqSbuf* StreamBufSource, LqSbufPtr* StreamPointer, LqSbuf* StreamBufDest, intptr_t Size)
{
	if((StreamPointer->GlobOffset < StreamBufSource->GlobOffset) || (StreamPointer->GlobOffset > (StreamBufSource->GlobOffset + StreamBufSource->Len)))
		return -1;
	intptr_t CommonWritten = 0;
	PageHeader* PageHdr;
	size_t StartOffset;
	if(StreamPointer->Page == nullptr)
	{
		PageHdr = (PageHeader*)StreamBufSource->Page0;
		StartOffset = PageHdr->StartOffset;
	} else
	{
		PageHdr = (PageHeader*)StreamPointer->Page;
		StartOffset = StreamPointer->OffsetInPage;
	}
	for(; ; )
	{
		intptr_t LenReadInCurPage = lq_min(Size - CommonWritten, PageHdr->EndOffset - StartOffset);
		intptr_t Written = LqSbufWrite(StreamBufDest, (char*)(PageHdr + 1) + StartOffset, LenReadInCurPage);
		CommonWritten += Written;
		if((PageHdr->NextPage == nullptr) || (Size <= CommonWritten) || (Written < LenReadInCurPage))
		{
			if(PageHdr == StreamPointer->Page)
			{
				StreamPointer->OffsetInPage += Written;
			} else
			{
				StreamPointer->OffsetInPage = StartOffset + Written;
				StreamPointer->Page = PageHdr;
			}
			StreamPointer->GlobOffset += CommonWritten;
			break;
		}
		PageHdr = (PageHeader*)PageHdr->NextPage;
		StartOffset = PageHdr->StartOffset;
	}
	return CommonWritten;
}

LQ_EXTERN_C intptr_t LQ_CALL LqSbufPeekByPtr(const LqSbuf* StreamBuf, LqSbufPtr* StreamPointer, void* DataDest, intptr_t Size)
{
	if((StreamPointer->GlobOffset < StreamBuf->GlobOffset) || (StreamPointer->GlobOffset > (StreamBuf->GlobOffset + StreamBuf->Len)))
		return -1;
	intptr_t ReadedSize = 0;
	PageHeader* PageHdr;
	size_t StartOffset;
	if(StreamPointer->Page == nullptr)
	{
		PageHdr = (PageHeader*)StreamBuf->Page0;
		StartOffset = PageHdr->StartOffset;
	} else
	{
		PageHdr = (PageHeader*)StreamPointer->Page;
		StartOffset = StreamPointer->OffsetInPage;
	}
	for(; ; )
	{
		intptr_t LenReadInCurPage = lq_min(Size - ReadedSize, PageHdr->EndOffset - StartOffset);
		memcpy((char*)DataDest + ReadedSize, (char*)(PageHdr + 1) + StartOffset, LenReadInCurPage);
		ReadedSize += LenReadInCurPage;
		if((PageHdr->NextPage == nullptr) || (Size <= ReadedSize))
			break;
		PageHdr = (PageHeader*)PageHdr->NextPage;
		StartOffset = PageHdr->StartOffset;
	}
	return ReadedSize;
}

LQ_EXTERN_C void LQ_CALL LqSbufInit(LqSbuf* StreamBuf)
{
	StreamBuf->GlobOffset = 0;
	StreamBuf->Len = 0;
	StreamBuf->Page0 = StreamBuf->PageN = nullptr;
}

LQ_EXTERN_C void LQ_CALL LqSbufUninit(LqSbuf* StreamBuf)
{
	for(PageHeader* Hdr; (Hdr = (PageHeader*)StreamBuf->Page0) != nullptr;)
		LqSbufRemoveFirstPage(StreamBuf);
}
