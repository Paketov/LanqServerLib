#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpCore... - Main handlers of HTTP protocol.
*   C++ Part.
*/

struct LqHttpConn;
struct LqHttpProto;
struct LqHttpQuery;

#include "LqHttp.h"
#include "LqFile.h"
#include "LqFileChe.hpp"
#include "LqHttpPth.hpp"
#include "LqMd5.h"

#define LqHttpGetReg(ConnectionPointer) ((LqHttpProto*)((LqConn*)ConnectionPointer)->Proto)

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqCachedFileHdr
{
private:
	LqMd5	Hash;
public:
	char*		Etag;
	char*		MimeType;
	void   GetMD5(const void * CacheInterator, LqMd5* Dest);

	void Cache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, const LqFileStat* Stat);
	void Recache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, const LqFileStat * Stat);
	inline void Uncache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime) const { }
};


struct LqHttpProto 
{
	LqHttpProtoBase					Base;
	LqHttpFileSystem				FileSystem;
	LqFileChe<LqCachedFileHdr>		Cache;
	LqLocker<uintptr_t>				ModuleListLocker;
};

#pragma pack(pop)
