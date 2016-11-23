#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqPtdArr (LanQ ProTecteD ARRay) - Multithread array.
*/

#include "LqOs.h"
#include "LqAlloc.hpp"
#include "LqShdPtr.hpp"

#include <vector>
#include <type_traits>


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

template<typename TypeVal>
class LqPtdArr {
    friend class interator;
    static const bool IsDebug = false;
    struct Arr {
        size_t        CountPointers;
        intptr_t      Count;
        TypeVal       Val[1];
    };

    static void Del(Arr* Val) {
        for(auto* i = Val->Val, *m = i + Val->Count; i < m; i++)
            i->~TypeVal();
        ___free(Val);
    }

    typedef LqShdPtr<Arr, Del, true, false> GlobPtr;
    typedef LqShdPtr<Arr, Del, false, false> LocalPtr;

    GlobPtr Ptr;
    static Arr* AllocNew() {
        struct _st { size_t CountPointers; intptr_t Count; };
        static _st __Empty = {5, 0};
        return (Arr*)&__Empty;
    }

    static Arr* AllocNew(size_t Count) {
        Arr* Res;
        if(Res = (Arr*)___malloc(Count * sizeof(TypeVal) + (sizeof(Arr) - sizeof(TypeVal)))) {
            Res->Count = Count;
            Res->CountPointers = 0;
        }
        return Res;
    }
    template<typename _SetVal>
    bool _AddByArr(const _SetVal* Val, size_t Count) {
        auto Cur = Ptr.NewStart();
        size_t NewCount = Cur->Count + Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr) {
            Ptr.NewFin(Cur);
            return false;
        }
        size_t i = 0;
        for(auto m = Cur->Count; i < m; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        for(size_t j = 0; i < NewCount; i++, j++)
            new(NewArr->Val + i) TypeVal(Val[j]);
        Ptr.NewFin(NewArr);
        return true;
    }
    template<typename _SetVal>
    bool _SetByArr(const _SetVal* Val, size_t Count) {
        auto Cur = Ptr.NewStart();
        auto NewCount = Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr) {
            Ptr.NewFin(Cur);
            return false;
        }
        for(size_t i = 0; i < NewCount; i++)
            new(NewArr->Val + i) TypeVal(Val[i]);
        Ptr.NewFin(NewArr);
        return true;
    }

    size_t _RmCount(size_t Count){
        size_t Res = 0;
        auto Cur = Ptr.NewStart();
        intptr_t NewCount = Cur->Count - Count;
        if(NewCount <= 0){
            Res = Cur->Count;
            Ptr.NewFin(AllocNew());
            return Res;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr){
            Ptr.NewFin(Cur);
            return 0;
        }
        for(intptr_t i = 0; i < NewCount; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        Res = Cur->Count - NewCount;
        Ptr.NewFin(NewArr);
        return Res;
    }

public:

    class interator
    {
        friend LqPtdArr;
        LocalPtr Ptr;
        size_t Index;
        template<typename PtrType>
        inline interator(const PtrType& NewPtr, size_t NewIndex = 0): Ptr(NewPtr), Index(NewIndex) {}
    public:
        inline interator(): Ptr(AllocNew()), Index(size_t(-1)) {}
        inline interator(const interator& NewPtr) : Ptr(NewPtr.Ptr), Index(NewPtr.Index) {}
        inline interator& operator=(const interator& NewPtr) { Ptr.Set(NewPtr.Ptr); Index = NewPtr.Index; return *this; }
        //Preincrement
        inline interator& operator++() { ++Index; return (*this); }
        inline interator operator++(int) { ++Index; return *this; }
        inline interator& operator--() { ++Index; return (*this); }
        inline interator operator--(int) { --Index; return *this; }
        inline TypeVal& operator*() const { return Ptr->Val[Index]; }
        inline TypeVal& operator[](size_t Index) const { return Ptr->Val[this->Index + Index]; }
        inline TypeVal* operator->() const { return &Ptr->Val[Index]; }
        inline interator operator+(int Add) const { return interator(Ptr, Index + Add); }
        inline interator operator-(int Sub) const { return interator(Ptr, Index - Sub); }

        inline interator& operator+=(size_t Add) { Index += Add; return (*this); }
        inline interator& operator-=(size_t Sub) { Index -= Sub; return (*this); }
        inline bool operator!=(const interator& Another) const {
            if(Another.Index == size_t(-1))
                return Index != Ptr->Count;
            return (Ptr != Another.Ptr) || (Index != Another.Index);
        }
        inline bool operator==(const interator& Another) const {
            if(Another.Index == size_t(-1))
                return Index == Ptr->Count;
            return (Ptr == Another.Ptr) && (Index == Another.Index);
        }
        inline size_t size() const { return Ptr->Count; }
        inline bool is_end() const { return Index >= Ptr->Count; }
        inline void set_end() { Index = TypeIndex(-1); }
    };

    inline LqPtdArr(): Ptr(AllocNew()) {}
    inline LqPtdArr(const LqPtdArr& Val) : Ptr(Val.Ptr) {}
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdArr(const std::initializer_list<InType> _Ax) : Ptr(AllocNew()) { _SetByArr(_Ax.begin(), _Ax.size()); }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdArr(const std::vector<InType>& _Ax) : Ptr(AllocNew()) { _SetByArr(_Ax.data(), _Ax.size()); }
    inline LqPtdArr(TypeVal&& _Ax) : Ptr(AllocNew()) { push_back(_Ax); }

    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdArr& operator=(const std::initializer_list<InType> Val) { _SetByArr(Val.begin(), Val.size()); return *this; }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdArr& operator=(const std::vector<InType>& Val) { _SetByArr(Val.data(), Val.size()); return *this; }
    inline LqPtdArr& operator=(const LqPtdArr& Val) { Ptr.Set(Val.Ptr); return *this; }

    inline interator begin() const { return Ptr; }
    inline interator end() const { return interator(); }
    inline interator back() const { interator i = Ptr; return i + (i.size() - 1); }

    template<typename InType, typename = decltype(std::declval<TypeVal>() == std::declval<InType>())>
    interator search(InType&& SrchVal) const {
        interator LocPtr(Ptr, size_t(-1));
        auto* Val = LocPtr.Ptr->Val;
        for(size_t i = 0, m = LocPtr.Ptr->Count; i < m; i++){
            if(Val[i] == SrchVal){
                LocPtr.Index = i;
                return LocPtr;
            }
        }
        return interator();
    }

    interator search_next(interator Prev) const{
        interator LocPtr(Prev.Ptr);
        auto* Val = LocPtr.Ptr->Val;
        auto& v = Val[Prev.Index];
        for(size_t i = Prev.Index + 1, m = LocPtr.Ptr->Count; i < m; i++){
            if(Val[i] == v){
                LocPtr.Index = i;
                return LocPtr;
            }
        }
        return interator();
    }

    int append(interator& Start, interator& End) {
        if(IsDebug){
            if(Start.Ptr != End.Ptr)
                throw "LqPtdArr::append(interator, interator): Interatrs points to different arrays. (Check threads race cond)\n";
        }
        if(_AddByArr(&*Start, End.Index - Start.Index))
            return End.Index - Start.Index;
        return 0;
    }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    int append(const std::initializer_list<InType> Start) { return _AddByArr(Start.begin(), Start.size())? Start.size(): 0; }

    int append(interator& Start) {
        if(_AddByArr(&*Start, Start.Ptr->Count - Start.Index))
            return Start.Ptr->Count - Start.Index;
        return 0;
    }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline int append(const std::vector<InType>& Val) { return _AddByArr(Val.data(), Val.size())? Val.size(): 0; }
    inline int append(const LqPtdArr& Val) { return append(Val.begin()); }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline int append(const InType* Val, size_t Count) { return _AddByArr(Val, Count)? Count: 0; }

    inline int unappend(size_t Count) { return _RmCount(Count); }
    inline void swap(LqPtdArr& AnotherVal) { Ptr.Swap(AnotherVal.Ptr); }

    template<
        typename InType,
        typename = decltype(TypeVal(std::declval<InType>()))
    >
    inline bool push_back(InType&& NewVal) { return _AddByArr(&NewVal, 1); }

    template<
        typename InType,
        typename = decltype(std::declval<TypeVal>() == std::declval<InType>()),
        typename = decltype(TypeVal(std::declval<InType>()))
    >
    int push_back_uniq(InType&& NewVal)
    {
        auto Cur = Ptr.NewStart();
        for(size_t m = Cur->Count, i = 0; i < m; i++)
            if(Cur->Val[i] == NewVal){
                Ptr.NewFin(Cur);
                return 0;
            }
        size_t NewCount = Cur->Count + 1;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr){
            Ptr.NewFin(Cur);
            return -1;
        }
        size_t i = 0;
        for(auto m = Cur->Count; i < m; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        new(NewArr->Val + i) TypeVal(NewVal);
        Ptr.NewFin(NewArr);
        return 1;
    }

    template<
        typename InType,
        typename InType2,
        typename = decltype(std::declval<TypeVal>() == std::declval<InType>()),
        typename = decltype(TypeVal(std::declval<InType2>()))
    >
    int replace(InType&& PrevVal, InType2&& NewVal)
    {
        auto Cur = Ptr.NewStart();
        intptr_t RmIndex = -1;
        for(intptr_t i = 0, m = Cur->Count; i < m; i++){
            if(Cur->Val[i] == PrevVal) {
                RmIndex = i;
                break;
            }
        }
        if(RmIndex == -1){
            Ptr.NewFin(Cur);
            return 0;
        }
        size_t NewCount = Cur->Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr) {
            Ptr.NewFin(Cur);
            return -1;
        }
        intptr_t i = 0;
        for(; i < RmIndex; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        new(NewArr->Val + i++) TypeVal(NewVal);
        for(; i < NewCount; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        Ptr.NewFin(NewArr);
        return 1;
    }

    inline bool pop_back() { return _RmCount(1) == 1; }

    template<
        typename InType,
        typename = decltype(std::declval<TypeVal>() == std::declval<InType>())
    >
    inline bool remove_by_val(InType&& RmVal) { return remove_by_val<TypeVal>(RmVal, nullptr); }

    template<
        typename TypeGetVal, 
        typename InType, 
        typename = decltype(std::declval<TypeVal>() == std::declval<InType>()),
        typename = decltype(TypeGetVal(std::declval<TypeVal>()))
    >
    bool remove_by_val(InType&& RmVal, TypeGetVal* RemovedVal)
    {
        auto Cur = Ptr.NewStart();
        intptr_t RmIndex = -1;
        for(intptr_t i = 0, m = Cur->Count; i < m; i++) {
            if(Cur->Val[i] == RmVal) {
                RmIndex = i;
                break;
            }
        }
        if(RmIndex == -1) {
            Ptr.NewFin(Cur);
            return false;
        }
        size_t NewCount = Cur->Count - 1;
        if(NewCount <= 0){
            Ptr.NewFin(AllocNew());
            return true;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr){
            Ptr.NewFin(Cur);
            return false;
        }
        size_t i = 0;
        for(; i < RmIndex; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        if(RemovedVal != nullptr)
            *RemovedVal = Cur->Val[i];
        for(; i < NewCount; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i + 1]);
        Ptr.NewFin(NewArr);
        return true;
    }
    template<
        typename InType,
        typename = decltype(std::declval<TypeVal>() == std::declval<InType>())
    >
    intptr_t remove_mult_by_val(InType&& RmVal)
    {
        auto Cur = Ptr.NewStart();
        intptr_t RmCount = 0;
        for(intptr_t i = 0, m = Cur->Count; i < m; i++){
            if(Cur->Val[i] == RmVal)
                RmCount++;
        }
        if(RmCount == 0){
            Ptr.NewFin(Cur);
            return 0;
        }
        size_t NewCount = Cur->Count - RmCount;
        if(NewCount <= 0){
            Ptr.NewFin(AllocNew());
            return RmCount;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr){
            Ptr.NewFin(Cur);
            return 0;
        }
        intptr_t i = 0;
        for(intptr_t j = 0; j < Cur->Count; j++)
            if(!(Cur->Val[j] == RmVal))
                new(NewArr->Val + i++) TypeVal(Cur->Val[j]);
        NewArr->Count = i;
        Ptr.NewFin(NewArr);
        return RmCount;
    }

    inline bool remove_by_compare_fn(std::function<bool(TypeVal&)> CompareFn) { return remove_by_compare_fn<TypeVal>(CompareFn, nullptr); }

    template<
        typename TypeGetVal,
        typename = decltype(TypeGetVal(std::declval<TypeVal>()))
    >
    bool remove_by_compare_fn(std::function<bool(TypeVal&)> CompareFn, TypeGetVal* RemovedVal) {
        auto Cur = Ptr.NewStart();
        intptr_t RmIndex = -1;
        for(intptr_t i = 0, m = Cur->Count; i < m; i++){
            if(CompareFn(Cur->Val[i])){
                RmIndex = i;
                break;
            }
        }
        if(RmIndex == -1){
            Ptr.NewFin(Cur);
            return false;
        }
        size_t NewCount = Cur->Count - 1;
        if(NewCount <= 0){
            Ptr.NewFin(AllocNew());
            return true;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr){
            Ptr.NewFin(Cur);
            return false;
        }
        size_t i = 0;
        for(; i < RmIndex; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i]);
        if(RemovedVal != nullptr)
            *RemovedVal = Cur->Val[i];
        for(; i < NewCount; i++)
            new(NewArr->Val + i) TypeVal(Cur->Val[i + 1]);
        Ptr.NewFin(NewArr);
        return true;
    }

    size_t remove_mult_by_compare_fn(std::function<bool(TypeVal&)> CompareFn) {
        auto Cur = Ptr.NewStart();
        auto NewArr = AllocNew(Cur->Count);
        if(NewArr == nullptr) {
            Ptr.NewFin(Cur);
            return 0;
        }
        intptr_t i = 0, j = 0;
        for(; j < Cur->Count; j++) {
            if(!CompareFn(Cur->Val[j]))
                new(NewArr->Val + i++) TypeVal(Cur->Val[j]);
        }
        NewArr->Count = i;
        Ptr.NewFin(NewArr);
        return j - i;
    }

    int clear() {
        auto Res = Ptr.NewStart()->Count;
        Ptr.NewFin(AllocNew());
        return Res;
    }

    size_t size() const{
        const LocalPtr p = Ptr;
        return p->Count;
    }
};

#pragma pack(pop)
