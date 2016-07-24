#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkList - List of workers.
*/

class LqWrkList;
class LqWrk;

#include "LqLock.hpp"
#include "LqSharedPtr.hpp"
#include "LqAlloc.hpp"

typedef LqSharedPtr<LqWrk, LqFastAlloc::Delete> LqWorkerPtr;

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqWrkList
{
protected:

	mutable LqLocker<uint>				WorkerListLocker;
	LqWorkerPtr*						Workers;
	size_t								WorkersCount;

	LqWrkList();
	~LqWrkList();
};

#pragma pack(pop)