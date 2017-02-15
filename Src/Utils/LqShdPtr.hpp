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

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

template<
    typename _t,                                        /* _t - must contain CountPointers field */
    void(*DeleteProc)(_t*) = SHARED_POINTERDeleteProc,  /* Proc called when object have zero references */
    bool IsLock = false                                 /* true - is pointer contact with multiple threads */,
    bool IsNullCheck = true,                            /* Set false, if pointer never be setted on nullptr (For optimized) */
    typename LockerType = unsigned char                 /* Locker type (use any integer type) */
>
class LqShdPtr {
    template<typename _t2, void(*)(_t2*), bool, bool, typename>
    friend class LqShdPtr;

    struct _s {
        _t* p;
        inline void LockRead() const {}
        inline void LockWrite() const {}
        inline void UnlockWrite() const {}
        inline void UnlockRead() const {}
    };

    struct _slk {
        _t* p;
        mutable LqLocker<LockerType> lk;
        inline void LockRead() const { lk.LockRead(); }
        inline void LockWrite() const { lk.LockWrite(); }
        inline void UnlockWrite() const { lk.UnlockWrite(); }
        inline void UnlockRead() const { lk.UnlockRead(); }
    };

    typename std::conditional<IsLock, _slk, _s>::type fld;

    inline _t* Deinit() {
        if(IsNullCheck && (fld.p == nullptr))
            return nullptr;
        /* thread race cond. solution */
        auto Expected = fld.p->CountPointers;
        for(; !LqAtmCmpXchg(fld.p->CountPointers, Expected, Expected - 1); Expected = fld.p->CountPointers);
        return (Expected == 1) ? fld.p : nullptr;
    }

public:
    static const auto IsHaveLock = IsLock;
    typedef LockerType        LockType;
    typedef _t                ElemType;

    inline LqShdPtr() { fld.p = nullptr; };
    inline LqShdPtr(_t* Pointer) { if(((fld.p = Pointer) != nullptr) || !IsNullCheck) LqAtmIntrlkInc(fld.p->CountPointers); }
    LqShdPtr(const LqShdPtr<_t, DeleteProc, false, IsNullCheck, LockerType>& a) {
        if(((fld.p = a.fld.p) != nullptr) || !IsNullCheck)
            LqAtmIntrlkInc(fld.p->CountPointers);
    }
    LQ_NO_INLINE LqShdPtr(const LqShdPtr<_t, DeleteProc, true, IsNullCheck, LockerType>& a) {
        a.fld.LockRead();
        if(((fld.p = a.fld.p) != nullptr) || !IsNullCheck)
            LqAtmIntrlkInc(fld.p->CountPointers);
        a.fld.UnlockRead();
    }

    LQ_NO_INLINE ~LqShdPtr() {
        if(auto Del = Deinit())
            DeleteProc(Del);
    }
    template<bool ArgIsHaveLock, bool ArgNullCheck, typename ArgLockerType>
    void Set(const LqShdPtr<_t, DeleteProc, ArgIsHaveLock, ArgNullCheck, ArgLockerType>& a) {
        fld.LockWrite();
        a.fld.LockRead();
        if((a.fld.p != nullptr) || !IsNullCheck || !ArgNullCheck)
            LqAtmIntrlkInc(a.fld.p->CountPointers);
        auto Del = Deinit();
        fld.p = a.fld.p;
        a.fld.UnlockRead();
        fld.UnlockWrite();
        if(Del != nullptr)
            DeleteProc(Del);
    }

    void Set(_t* a) {
        fld.LockWrite();
        if((a != nullptr) || !IsNullCheck)
            LqAtmIntrlkInc(a->CountPointers);
        auto Del = Deinit();
        fld.p = a;
        fld.UnlockWrite();
        if(Del != nullptr)
            DeleteProc(Del);
    }

    inline LqShdPtr& operator=(LqShdPtr&& a) { Set(a); return *this; }

    template<typename _TInput>
    inline LqShdPtr& operator=(_TInput&& a) { Set(a); return *this; }
    /* Set new data between NewStart() and NewFin()*/
    inline _t* NewStart() {
        fld.LockWrite();
        return fld.p;
    }
    void NewFin(_t* NewVal) {
        if((NewVal != nullptr) || !IsNullCheck)
            LqAtmIntrlkInc(NewVal->CountPointers);
        auto Del = Deinit();
        fld.p = NewVal;
        fld.UnlockWrite();
        if(Del != nullptr)
            DeleteProc(Del);
    }

    template<bool ArgIsHaveLock, bool ArgNullCheck, typename ArgLockerType>
    void Swap(LqShdPtr<_t, DeleteProc, ArgIsHaveLock, ArgNullCheck, ArgLockerType>& a) {
        fld.LockWrite();
        a.fld.LockRead();
        auto t = fld.p;
        fld.p = a.fld.p;
        a.fld.p = t;
        a.fld.UnlockRead();
        fld.UnlockWrite();
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


/* C - defines for trivial types */

template<typename _t>
inline void LqObPtrReference(_t* Ob) { LqAtmIntrlkInc(Ob->CountPointers); }

template<typename _t, void(*DeleteProc)(_t*) = SHARED_POINTERDeleteProc>
bool LqObPtrDereference(_t* Ob) {
    auto Expected = Ob->CountPointers;
    for(; !LqAtmCmpXchg(Ob->CountPointers, Expected, Expected - 1); Expected = Ob->CountPointers);
    if(Expected == 1) {
        DeleteProc(Ob);
        return true;
    } else {
        return false;
    }
}

template<typename _t, typename _LkType>
void LqObPtrInit(_t*& Ob, _t* NewOb, volatile _LkType& Lk) {
    LqAtmLkInit(Lk);
    Ob = NewOb;
    if(Ob != nullptr)
        LqAtmIntrlkInc(Ob->CountPointers);
}

template<typename _t, typename _LkType>
_t* LqObPtrGet(_t* Ob, volatile _LkType& Lk) {
    LqAtmLkRd(Lk);
    if(Ob != nullptr)
        LqAtmIntrlkInc(Ob->CountPointers);
    _t* Res = Ob;
    LqAtmUlkRd(Lk);
    return Res;
}

template<typename _t, void(*DeleteProc)(_t*), bool IsNullCheck, typename _LkType>
LqShdPtr<_t, DeleteProc, false, IsNullCheck> LqObPtrGetEx(_t* Ob, volatile _LkType& Lk) {
    LqAtmLkRd(Lk);
    auto Res = LqShdPtr<_t, DeleteProc, false, IsNullCheck>(Ob);
    LqAtmUlkRd(Lk);
    return Res;
}

template<typename _t, typename _LkType>
inline _t* LqObPtrNewStart(_t* Ob, volatile _LkType& Lk) {
    LqAtmLkWr(Lk);
    return Ob;
}

template<typename _t, void(*DeleteProc)(_t*), typename _LkType>
void LqObPtrNewFin(_t*& Ob, _t* NewVal, volatile _LkType& Lk) {
    if(NewVal != nullptr)
        LqAtmIntrlkInc(NewVal->CountPointers);
    _t* DelVal;
    if(Ob != nullptr) {
        auto Expected = Ob->CountPointers;
        for(; !LqAtmCmpXchg(Ob->CountPointers, Expected, Expected - 1); Expected = Ob->CountPointers);
        DelVal = (Expected == 1) ? Ob : nullptr;
    } else {
        DelVal = nullptr;
    }
    Ob = NewVal;
    LqAtmUlkWr(Lk);
    if(DelVal != nullptr)
        DeleteProc(DelVal);
}


#pragma pack(pop)
