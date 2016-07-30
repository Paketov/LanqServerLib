#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   Different definitions. (For C++)
*/


#include <string>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include "LqDef.h"


typedef std::basic_string<char>                 LqString;
typedef std::mutex                              LqMutex;
typedef std::condition_variable                 LqCondVar;
typedef std::unique_lock<LqMutex>               LqUniqueLock;
typedef std::thread                             LqSystemThread;
template<typename Type>
using LqAtomic = std::atomic<Type>;
inline void LqThreadYield() { std::this_thread::yield(); }
template<typename NumType>
inline LqString LqToString(NumType Num) { return std::to_string(Num); }
