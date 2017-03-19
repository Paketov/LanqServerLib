#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqAtm... - Atomic operations for C defined variables.
*/


#include <atomic>
#include "LqFile.h"

template<typename T>
inline bool LqAtmCmpXchg(volatile T& Val, T& Expected, T NewVal) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    return ((std::atomic<T>&)Val).compare_exchange_strong(Expected, NewVal);
}

template<typename T, typename T2>
inline void LqAtmIntrlkAdd(volatile T& Val, T2 AddVal) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    ((std::atomic<T>&)Val) += AddVal;
}

template<typename T, typename T2>
inline void LqAtmIntrlkOr(volatile T& Val, T2 NewVal) {
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	((std::atomic<T>&)Val) |= NewVal;
}

template<typename T, typename T2>
inline void LqAtmIntrlkAnd(volatile T& Val, T2 NewVal) {
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	((std::atomic<T>&)Val) &= NewVal;
}

template<typename T, typename T2>
inline void LqAtmIntrlkSub(volatile T& Val, T2 SubVal) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    ((std::atomic<T>&)Val) -= SubVal;
}

#ifdef LQPLATFORM_WINDOWS

#include <Windows.h>

template<typename T>
inline typename std::enable_if<sizeof(T) == 1>::type LqAtmIntrlkInc(volatile T& Val) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    ++((std::atomic<T>&)Val);
}
template<typename T>
inline typename std::enable_if<sizeof(T) == 2>::type LqAtmIntrlkInc(volatile T& Val) { _InterlockedIncrement16((volatile SHORT*)&Val); }
template<typename T>
inline typename std::enable_if<sizeof(T) == 4>::type LqAtmIntrlkInc(volatile T& Val) { _InterlockedIncrement((volatile LONG*)&Val); }
template<typename T>
inline typename std::enable_if<sizeof(T) == 8>::type LqAtmIntrlkInc(volatile T& Val) { _InterlockedIncrement64((volatile LONG64*)&Val); }


template<typename T>
inline typename std::enable_if<sizeof(T) == 1>::type LqAtmIntrlkDec(volatile T& Val) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    ++((std::atomic<T>&)Val);
}
template<typename T>
inline typename std::enable_if<sizeof(T) == 2>::type LqAtmIntrlkDec(volatile T& Val) { _InterlockedDecrement16((volatile SHORT*)&Val); }
template<typename T>
inline typename std::enable_if<sizeof(T) == 4>::type LqAtmIntrlkDec(volatile T& Val) { _InterlockedDecrement((volatile LONG*)&Val); }
template<typename T>
inline typename std::enable_if<sizeof(T) == 8>::type LqAtmIntrlkDec(volatile T& Val) { _InterlockedDecrement64((volatile LONG64*)&Val); }

#else

template<typename T>
inline void LqAtmIntrlkInc(volatile T& Val) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    ++((std::atomic<T>&)Val);
}

template<typename T>
inline void LqAtmIntrlkDec(volatile T& Val) {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
    --((std::atomic<T>&)Val);
}

#endif


template<typename T>
inline void LqAtmLkInit(volatile T& Val) { Val = 1; }

template<typename T>
inline void LqAtmLkRd(volatile T& Val) { for(T v; ((v = Val) == 0) || !LqAtmCmpXchg(Val, v, (T)(v + 1)); LqThreadYield()); }

template<typename T>
inline void LqAtmUlkRd(volatile T& Val) { LqAtmIntrlkDec(Val); }

template<typename T>
inline void LqAtmLkWr(volatile T& Val) { for(T v = 1; !LqAtmCmpXchg(Val, v, (T)0); LqThreadYield(), v = 1); }

template<typename T>
inline void LqAtmUlkWr(volatile T& Val) { Val = 1; }

template<typename T>
inline bool LqAtmLkIsRd(volatile T& Val) { return Val > 0; }
