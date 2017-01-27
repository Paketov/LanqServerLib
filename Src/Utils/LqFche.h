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
	LqFileSz            SizeFile;

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

LQ_IMPORTEXPORT LqFche* LQ_CALL LqFcheCreate();
LQ_IMPORTEXPORT LqFche* LQ_CALL LqFcheCopy(LqFche* lqaio Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheDelete(LqFche* lqain RmSrc);



LQ_IMPORTEXPORT size_t LQ_CALL LqFcheClear(LqFche* Che);
LQ_IMPORTEXPORT bool LQ_CALL LqFcheRead(LqFche* Che, const char* Path, LqFbuf* Fbuf);
LQ_IMPORTEXPORT bool LQ_CALL LqFcheUpdateAndRead(LqFche* Che, const char* Path, LqFbuf* Fbuf);
LQ_IMPORTEXPORT void LQ_CALL LqFcheUpdateAllCache(LqFche* Che);
LQ_IMPORTEXPORT int LQ_CALL LqFcheUpdate(LqFche* Che, const char* Path);
LQ_IMPORTEXPORT bool LQ_CALL LqFcheUncache(LqFche* Che, const char* Path);

LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetMaxSizeFile(LqFche* Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetMaxSizeFile(LqFche* Che, size_t NewSize);
LQ_IMPORTEXPORT size_t LQ_CALL GLqFcheGetEmployedSize(LqFche* Che);
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetMaxSize(LqFche* Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetMaxSize(LqFche* Che, size_t NewSize);
LQ_IMPORTEXPORT LqTimeMillisec LQ_CALL LqFcheGetPeriodUpdateStat(LqFche* Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetPeriodUpdateStat(LqFche* Che, LqTimeMillisec NewPeriodMillisec);
LQ_IMPORTEXPORT float LQ_CALL LqFcheGetPerSecReadAdding(LqFche* Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetPerSecReadAdding(LqFche* Che, float NewPeriod);
LQ_IMPORTEXPORT size_t LQ_CALL LqFcheGetMaxCountOfPrepared(LqFche* Che);
LQ_IMPORTEXPORT void LQ_CALL LqFcheSetMaxCountOfPrepared(LqFche* Che, size_t NewPeriod);


LQ_EXTERN_C_END


#endif