#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   Different definitions. (For C++)
*/


#include <string>
#include <atomic>
#include "LqDef.h"


typedef std::basic_string<char>                 LqString;
typedef std::basic_string<wchar_t>              LqString16;
template<typename Type>
using LqAtomic = std::atomic<Type>;
template<typename NumType>
inline LqString LqToString(NumType Num) { return std::to_string(Num); }
