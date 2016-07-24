#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqAtm... - Atomic operations for C defined variables.
*/


#include <atomic>
#include "LqLock.hpp"


template<typename T>
inline bool LqAtmCmpXchg(volatile T& Val, T& Expected, T NewVal)
{
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	return ((std::atomic<T>&)Val).compare_exchange_strong(Expected, NewVal);
}

template<typename T>
inline void LqAtmLkInit(volatile T& Val)
{
	new((void*)&Val) LqLocker<T>();
}

template<typename T>
inline void LqAtmLkRd(volatile T& Val)
{
	static_assert(sizeof(LqLocker<T>) == sizeof(T), "sizeof(LqLocker<T>) != sizeof(T), use another method this");
	((LqLocker<T>&)Val).LockReadYield();
}

template<typename T>
inline void LqAtmUlkRd(volatile T& Val)
{
	static_assert(sizeof(LqLocker<T>) == sizeof(T), "sizeof(LqLocker<T>) != sizeof(T), use another method this");
	((LqLocker<T>&)Val).UnlockRead();
}

template<typename T>
inline void LqAtmLkWr(volatile T& Val)
{
	static_assert(sizeof(LqLocker<T>) == sizeof(T), "sizeof(LqLocker<T>) != sizeof(T), use another method this");
	((LqLocker<T>&)Val).LockWriteYield();
}

template<typename T>
inline void LqAtmUlkWr(volatile T& Val)
{
	static_assert(sizeof(LqLocker<T>) == sizeof(T), "sizeof(LqLocker<T>) != sizeof(T), use another method this");
	((LqLocker<T>&)Val).UnlockWrite();
}

template<typename T>
inline bool LqAtmLkIsRd(volatile T& Val)
{
	return ((LqLocker<T>&)Val).IsLockRead();
}

template<typename T>
inline void LqAtmIntrlkInc(volatile T& Val)
{
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	((std::atomic<T>&)Val)++;
}

template<typename T>
inline void LqAtmIntrlkDec(volatile T& Val)
{
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	((std::atomic<T>&)Val)--;
}

template<typename T, typename T2>
inline void LqAtmIntrlkAdd(volatile T& Val, T2 AddVal)
{
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	((std::atomic<T>&)Val) += AddVal;
}

template<typename T, typename T2>
inline void LqAtmIntrlkSub(volatile T& Val, T2 SubVal)
{
	static_assert(sizeof(std::atomic<T>) == sizeof(T), "sizeof(std::atomic<T>) != sizeof(T), use another method this");
	((std::atomic<T>&)Val) -= SubVal;
}