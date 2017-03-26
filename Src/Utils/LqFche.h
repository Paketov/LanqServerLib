/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFileTrd... - (Lanq File Transaction) Implements transactions for correct saving file in os fs.
*/


#ifndef __LQ_FILE_CHE_H_HAS_INCLUDED__
#define __LQ_FILE_CHE_H_HAS_INCLUDED__

#include "LqOs.h"
#include "LqTime.h"
#include "LqFile.h"
#include "LqStr.h"
#include "LqSbuf.h"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqFche;

struct __LqCachedFile {
	__LqCachedFile*     Next;
	__LqCachedFile*     Prev;
	__LqCachedFile*     TblNext;
	LqFche*			    Owner;


	char*               Path;
	size_t              PathHash;

	LqFbuf				File;
	intptr_t            SizeFile;

	size_t				CountRef;
	intptr_t            CountReaded;
	LqTimeMillisec      LastTestTime;
	LqTimeSec           LastModifTime;
	uint8_t				CachingLocker;
};

struct LqFche {
	__LqCachedFile*     CachedListEnd;

	__LqCachedFile      RatingList;
	LqTblRow            Table;

	float               PerSecReadAdding;
	size_t              MaxInPreparedList;
	size_t              MaxSizeBuff;
	size_t              CurSize;
	size_t              MaxSizeFile;
	size_t              CountInUncached;
	LqTimeMillisec      PeriodUpdateStatMillisec;
	uintptr_t           Locker;
	size_t              CountPtrs;
	size_t              FilePtrs;
};

typedef struct __LqCachedFile __LqCachedFile;
typedef struct LqFche LqFche;

#pragma pack(pop)

LQ_EXTERN_C_BEGIN

/*
  Create Cache Instance
		@return - new cache instance or NULL when have error.
*/
LQ_IMPORTEXPORT LqFche* LQ_CALL LqFcheCreate();

/* Increment count of pointers only */
LQ_IMPORTEXPORT LqFche* LQ_CALL LqFcheCopy(LqFche* lqaio Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheDelete(LqFche* lqain RmSrc);

/*
	Make empty cache
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheClear(LqFche* lqaio Che);

/*
	Only try read file from cache
		@Che - Source cache instance
		@Path - File source path
		@Fbuf - Dest file buffer(maked ptr on virtual file)
		@return - true - is readed from cache, false - otherwise
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFcheRead(LqFche* lqaio Che, const char* lqain Path, LqFbuf* lqaout Fbuf);

/*
	Try read file from cache and update the statistics for this file
		@Che - Source cache instance
		@Path - File source path
		@Fbuf - Dest file buffer(maked ptr on virtual file)
		@return - true - is readed from cache, false - otherwise
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFcheUpdateAndRead(LqFche* lqaio Che, const char* lqain Path, LqFbuf* lqaout Fbuf);

/*
	Update all statistics in cache
		@Che - Target cache instance
*/
LQ_IMPORTEXPORT void LQ_CALL LqFcheUpdateAllCache(LqFche* lqaio Che);

/*
	Update statistics only for one file
		@Che - Target cache instance
		@Path - Path to the required file
*/
LQ_IMPORTEXPORT int LQ_CALL LqFcheUpdate(LqFche* lqaio Che, const char* lqain Path);

/*
	Unload file from cache
		@Che - Target cache instance
		@Path - Path to the required file
*/
LQ_IMPORTEXPORT bool LQ_CALL LqFcheUncache(LqFche* lqaio Che, const char* lqain Path);

/* Parametrs */
/*
	Get maximum size of one caching file
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetMaxSizeFile(LqFche* lqain Che);

/*
	Set maximum size of one caching file
*/
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetMaxSizeFile(LqFche* lqaio Che, intptr_t NewSize);

/*
	Get current size of all cache
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetEmployedSize(LqFche* lqain Che);

/*
	Get max size of all cache
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetMaxSize(LqFche* lqain Che);

/*
	Set max size of all cache
*/
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetMaxSize(LqFche* lqaio Che, intptr_t NewSize);

/*
	Get the update period of statistics for one file
*/
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqFcheGetPeriodUpdateStat(LqFche* lqain Che);

/*
	Set the update period of statistics for one file
*/
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetPeriodUpdateStat(LqFche* lqaio Che, LqTimeMillisec NewPeriodMillisec);

/*
	Get the number of reads per second at which the file is added to the cache
*/
LQ_IMPORTEXPORT float LQ_CALL LqFcheGetPerSecReadAdding(LqFche* lqain Che);
/*
	Set the number of reads per second at which the file is added to the cache
*/
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetPerSecReadAdding(LqFche* lqaio Che, float NewPeriod);

/*
	Get the maximum queue length for adding to the cache (Prepared queue - the intermediate step between reading 
	and adding to the cache used to collect statistics)
*/
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetMaxCountOfPrepared(LqFche* lqain Che);

/*
	Set the maximum queue length for adding to the cache
*/
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetMaxCountOfPrepared(LqFche* lqaio Che, size_t NewPeriod);

LQ_EXTERN_C_END

#endif
