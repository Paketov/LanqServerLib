#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqShdPtr - Pointer on shared object.
*     Object must have public integer field "CountPointers".
*/

#include "LqOs.h"
#include "LqAtm.hpp"
#include "LqLock.hpp"
#include <type_traits>

template<typename T>
void SHARED_POINTERDeleteProc(T* p) { delete p; }

/*
* _t - must have CountPointers field
*/


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

template<typename _t, void(*DeleteProc)(_t*) = SHARED_POINTERDeleteProc, bool IsLock = false, typename LockerType = unsigned char>
class LqShdPtr
{
	struct _s 
	{
	   _t* p;
	   inline void LockRead() const { }
	   inline void LockWrite() const { }
	   inline void UnlockWrite() const { }
	   inline void UnlockRead() const { }
	};

	struct _slk
	{
		_t* p;
		mutable LqLocker<LockerType> lk;
		inline void LockRead() const { lk.LockRead(); }
		inline void LockWrite() const { lk.LockWrite(); }
		inline void UnlockWrite() const { lk.UnlockWrite(); }
		inline void UnlockRead() const { lk.UnlockRead(); }
	};

	typename std::conditional<IsLock, _slk, _s>::type fld;
   
	inline _t* Deinit()
	{
		if(fld.p == nullptr)
			return nullptr;
		decltype(fld.p->CountPointers) Expected;
		bool IsDelete;
		do
		{
			Expected = fld.p->CountPointers;
			IsDelete = Expected == 1;
		} while(!LqAtmCmpXchg(fld.p->CountPointers, Expected, Expected - 1));
		return IsDelete? fld.p: nullptr;
	}
public:
	inline LqShdPtr() { fld.p = nullptr; };
    inline LqShdPtr(_t* Pointer) { if((fld.p = Pointer) != nullptr) LqAtmIntrlkInc(fld.p->CountPointers);  }
	LQ_NO_INLINE LqShdPtr(const LqShdPtr& a)
	{ 
		a.fld.LockRead();
		fld.p = a.fld.p;
		if(fld.p != nullptr)
			LqAtmIntrlkInc(fld.p->CountPointers);
		a.fld.UnlockRead();
	}
	LQ_NO_INLINE ~LqShdPtr()
	{
		if(auto Del = Deinit())
			DeleteProc(Del);
	}
    LQ_NO_INLINE LqShdPtr& operator=(const LqShdPtr& a)
    {
		fld.LockWrite();
		a.fld.LockRead();
        auto Del = Deinit();
		fld.p = a.fld.p;
        if(fld.p != nullptr)
			LqAtmIntrlkInc(fld.p->CountPointers);
		a.fld.UnlockRead();
		fld.UnlockWrite();
		if(Del != nullptr)
			DeleteProc(Del);
        return *this;
    }

    inline _t* operator->() const { return fld.p; }
    inline _t* Get() const { return fld.p; }
	inline bool operator ==(const LqShdPtr& Worker2) const { return fld.p == Worker2.fld.p; }
    inline bool operator !=(const LqShdPtr& Worker2) const { return fld.p != Worker2.fld.p; }
	inline bool operator ==(const void* Worker2) const { return fld.p == Worker2; }
	inline bool operator !=(const void* Worker2) const { return fld.p != Worker2; }
	inline bool operator ==(const _t* Worker2) const { return fld.p == Worker2; }
	inline bool operator !=(const _t* Worker2) const { return fld.p != Worker2; }
    inline bool operator ==(decltype(nullptr) n) const { return fld.p == nullptr; }
    inline bool operator !=(decltype(nullptr) n) const { return fld.p != nullptr; }
};

template<typename _t>
void LqObReference(_t* Ob)
{
	LqAtmIntrlkInc(Ob->CountPointers);
}

template<typename _t, void(*DeleteProc)(_t*) = SHARED_POINTERDeleteProc>
bool LqObDereference(_t* Ob)
{
	decltype(Ob->CountPointers) Expected;
	bool IsDelete;
	do
	{
		Expected = Ob->CountPointers;
		IsDelete = Expected == 1;
	} while(!LqAtmCmpXchg(Ob->CountPointers, Expected, Expected - 1));
	if(IsDelete)
	{
		DeleteProc(Ob);
		return true;
	} else
	{
		return false;
	}
}


#pragma pack(pop)
