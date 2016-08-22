/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkList - List of workers.
*/


#include "LqOs.h"
#include "LqWrk.hpp"
#include "LqWrkList.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

LqWrkList::LqWrkList(): WorkersCount(0), Workers(nullptr)
{}

LqWrkList::~LqWrkList()
{
    WorkerListLocker.LockWrite();
    for(size_t i = 0, m = WorkersCount; i < m; i++)
        Workers[i].~LqWorkerPtr();
    if(Workers != nullptr)
        free(Workers);
    WorkerListLocker.UnlockWrite();
}

