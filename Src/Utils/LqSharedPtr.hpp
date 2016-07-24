#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqSharedPtr - Pointer on shared object.
*     Object must have public integer field "CountPointers".
*/




template<typename T>
void SHARED_POINTERDeleteProc(T* p) { delete p; }

/*
* _t - must have CountPointers field
*/


#pragma pack(push)
#pragma pack(LQCACHE_ALIGN_MEM)

template<typename _t, void(*DeleteProc)(_t*) = SHARED_POINTERDeleteProc>
class LqSharedPtr
{
	_t* p;
	inline void Deinit()
	{
		if(p == nullptr) return;
		p->CountPointers--;
		if(p->CountPointers <= 0) DeleteProc(p);
	}
public:
	inline LqSharedPtr(): p(nullptr) {};
	inline LqSharedPtr(_t* Pointer): p(Pointer) { if(p != nullptr) p->CountPointers++; }
	inline LqSharedPtr(const LqSharedPtr& a) : p(a.p) { if(p != nullptr) p->CountPointers++; }
	inline ~LqSharedPtr() { Deinit(); }
	inline LqSharedPtr& operator=(const LqSharedPtr& a)
	{
		Deinit();
		p = a.p;
		if(p != nullptr) p->CountPointers++;
		return *this;
	}

	inline _t* operator->() const { return p; }
	inline _t* Get() const { return p; }
	inline bool operator ==(const LqSharedPtr& Worker2) const { return p == Worker2.p; }
	inline bool operator !=(const LqSharedPtr& Worker2) const { return p != Worker2.p; }
	inline bool operator ==(decltype(nullptr) n) const { return p == nullptr; }
	inline bool operator !=(decltype(nullptr) n) const { return p != nullptr; }
};

#pragma pack(pop)
