/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqFastAlloc - Fast memory allocator.
*/


#ifndef __LQ_ALLOC_H_1_
#define __LQ_ALLOC_H_1_

#include <malloc.h>
#include "LqDef.hpp"
#include "LqLock.hpp"


#if /*!defined(_DEBUG) &&*/ defined(_MSC_VER)
#include <Windows.h>
#define ___malloc(size) HeapAlloc(GetProcessHeap(), 0, (size))
#define ___realloc(pointer, size) (((pointer) == nullptr)? ___malloc(size): HeapReAlloc(GetProcessHeap(), 0, (pointer), (size)))
#define ___free(pointer) HeapFree(GetProcessHeap(), 0, (pointer))
#else
#define ___malloc(size) malloc(size)
#define ___realloc(pointer, size) realloc(pointer, size)
#define ___free(pointer) free(pointer)
#endif

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)


class LqFastAlloc
{
	template<size_t SizeElem>
	struct Fields
	{
		void* StartElement;
		size_t Count;
		size_t SizeList;
		mutable LqLocker<uchar> Locker;

		Fields(): StartElement(nullptr), SizeList(80), Count(0) {}

		void* Alloc()
		{
			Locker.LockWrite();
			if(StartElement != nullptr)
			{
				void* Ret = StartElement;
				StartElement = *(void**)Ret;
				Count--;
				Locker.UnlockWrite();
				return Ret;
			} else
			{
				Locker.UnlockWrite();
				return ___malloc(SizeElem);
			}
		}
		void Free(void* Data)
		{
			Locker.LockWrite();
			if(Count >= SizeList)
			{
				Locker.UnlockWrite();
				___free(Data);
			} else
			{
				*(void**)Data = StartElement;
				StartElement = Data;
				Count++;
				Locker.UnlockWrite();
			}
		}
		void ClearList()
		{
			Locker.LockWrite();
			void * Cur = StartElement;
			StartElement = nullptr;
			while(Cur != nullptr)
			{
				void * v = *(void**)Cur;
				___free(Cur);
				Cur = v;
			}
			Count = 0;
			Locker.UnlockWrite();
		}
		inline void SetMaxCount(size_t NewVal) { SizeList = NewVal; }
	};

	template<size_t Len>
	struct VAL_TYPE_: Fields<((Len < sizeof(void*)) ? sizeof(void*) : Len)> {};

	template<class T>
	struct VAL_TYPE: VAL_TYPE_<sizeof(T)> {};

	template<typename TYPE, TYPE Val, typename ASSOC_TYPE>
	struct assoc_val { static ASSOC_TYPE value; };
public:

	template<typename Type>
	inline static Type* New();

	template<typename Type, typename A1>
	inline static Type* New(A1&& arg1);

	template<typename Type, typename A1, typename A2>
	inline static Type* New(A1&& arg1, A2&& arg2);

	template<typename Type, typename A1, typename A2, typename A3>
	inline static Type* New(A1&& arg1, A2&& arg2, A3&& arg3);

	template<typename Type, typename A1, typename A2, typename A3, typename A4>
	inline static Type* New(A1&& arg1, A2&& arg2, A3&& arg3, A4&& arg4);

	template<typename Type, typename A1, typename A2, typename A3, typename A4, typename A5>
	inline static Type* New(A1 arg1, A2&& arg2, A3&& arg3, A4&& arg4, A5&& arg5);

	template<typename Type, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
	inline static Type* New(A1&& arg1, A2&& arg2, A3&& arg3, A4&& arg4, A5&& arg5, A6&& arg6);

	/*
	Delete memory region with adding in stack regions. Late, this region takes from stack.
	*/
	template<typename Type>
	inline static typename std::enable_if<!std::is_same<Type, void>::value>::type Delete(Type* Val);

	/*
	Delete memory region without adding in stack.
	!!! Caution! In this case function not call destructor for type. !!!
	*/
	inline static void JustDelete(void* Val) { ___free(Val); }

	template<typename Type>
	inline static void ClearList();

	/*
	* Set max count in memory region stack.
	*/
	template<typename Type>
	inline static void SetMaxCountList(size_t NewSize);

	template<typename Type>
	inline static size_t GetMaxCountList();

};

#pragma pack(pop)


#endif

#if defined(__METHOD_DECLS__) && !defined(__LQ_ALLOC_H_2_)
#define __LQ_ALLOC_H_2_


template<typename Type>
inline Type* LqFastAlloc::New()
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type();
}

template<typename Type, typename A1>
inline Type* LqFastAlloc::New(A1&& arg1)
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type(arg1);
}

template<typename Type, typename A1, typename A2>
inline Type* LqFastAlloc::New(A1&& arg1, A2&& arg2)
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type(arg1, arg2);
}

template<typename Type, typename A1, typename A2, typename A3>
inline Type* LqFastAlloc::New(A1&& arg1, A2&& arg2, A3&& arg3)
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type(arg1, arg2, arg3);
}

template<typename Type, typename A1, typename A2, typename A3, typename A4>
inline Type* LqFastAlloc::New(A1&& arg1, A2&& arg2, A3&& arg3, A4&& arg4)
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type(arg1, arg2, arg3, arg4);
}

template<typename Type, typename A1, typename A2, typename A3, typename A4, typename A5>
inline Type* LqFastAlloc::New(A1 arg1, A2&& arg2, A3&& arg3, A4&& arg4, A5&& arg5)
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type(arg1, arg2, arg3, arg4, arg5);
}

template<typename Type, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
inline Type* LqFastAlloc::New(A1&& arg1, A2&& arg2, A3&& arg3, A4&& arg4, A5&& arg5, A6&& arg6)
{
    return new(assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Alloc()) Type(arg1, arg2, arg3, arg4, arg5, arg6);
}

/*
Delete memory region with adding in stack regions. Late, this region takes from stack.
*/
template<typename Type>
inline typename std::enable_if<!std::is_same<Type, void>::value>::type LqFastAlloc::Delete(Type* Val)
{
	Val->~Type();
    assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.Free(Val);
}


template<typename Type>
inline void LqFastAlloc::ClearList()
{
    assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.ClearList();
}

template<typename Type>
inline void LqFastAlloc::SetMaxCountList(size_t NewSize)
{
    assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.SetMaxCount(NewSize);
}

template<typename Type>
inline size_t LqFastAlloc::GetMaxCountList()
{
    return assoc_val<size_t, sizeof(Type), VAL_TYPE<Type>>::value.SizeList;
}

template<typename TYPE, TYPE Val, typename ASSOC_TYPE>
ASSOC_TYPE LqFastAlloc::assoc_val<TYPE, Val, ASSOC_TYPE>::value;

#endif

