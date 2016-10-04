#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqPtdTbl (LanQ ProTecteD TaBLe) - Multithread hash table.
*/

#include "LqOs.h"
#include "LqAlloc.hpp"
#include "LqShdPtr.hpp"

#include <vector>
#include <type_traits>

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)



template<typename TypeVal, typename TypeCmp, typename TypeIndex = uint16_t>
class LqPtdTbl
{
    friend class interator;

    static const auto NullIndex = TypeIndex(-1);
    static const bool IsDebug = false;

    struct TypeLine
    {
        TypeIndex     Start;
        TypeIndex     Next;
        TypeVal       Val;
    };

    struct Arr
    {
        size_t        CountPointers;
        intptr_t      Count;
        TypeLine      Line[1];
    };

    static void Del(Arr* Val)
    {
        for(auto* i = Val->Line, *m = i + Val->Count; i < m; i++)
            i->Val.~TypeVal();
        ___free(Val);
    }

    typedef LqShdPtr<Arr, Del, true, false> GlobPtr;
    typedef LqShdPtr<Arr, Del, false, false> LocalPtr;

    GlobPtr Ptr;
    static Arr* AllocNew()
    {
        struct _st { size_t CountPointers;  intptr_t Count; };
        static _st __Empty = {5, 0};
        return (Arr*)&__Empty;
    }

    static Arr* AllocNew(size_t Count)
    {
        Arr* Res;
        if(Res = (Arr*)___malloc(Count * sizeof(TypeLine) + (sizeof(Arr) - sizeof(TypeLine))))
        {
            for(auto* i = Res->Line, *m = i + Count; i < m; i++)
                i->Start = NullIndex;
            Res->Count = Count;
            Res->CountPointers = 0;
        }
        return Res;
    }

    template<typename InType>
    TypeLine* _GetCell(Arr* Cur, InType&& Val)
    {
        if(Cur->Count > 0)
        {
            for(TypeIndex Index = Cur->Line[TypeCmp::IndexByKey(Val, Cur->Count)].Start;
                Index != NullIndex;
                Index = Cur->Line[Index].Next)
            {
                if(TypeCmp::Cmp(Cur->Line[Index].Val, Val))
                    return &Cur->Line[Index];
            }
        }
        return nullptr;
    }

    template<typename InType>
    TypeIndex _GetCellIndex(Arr* Cur, InType&& Val)
    {
        if(Cur->Count > 0)
        {
            for(TypeIndex Index = Cur->Line[TypeCmp::IndexByKey(Val, Cur->Count)].Start;
                Index != NullIndex;
                Index = Cur->Line[Index].Next)
            {
                if(TypeCmp::Cmp(Cur->Line[Index].Val, Val))
                    return Index;
            }
        }
        return NullIndex;
    }

    size_t _RmCount(size_t Count)
    {
        size_t Res = 0;
        auto Cur = Ptr.NewStart();
        intptr_t NewCount = Cur->Count - Count;
        if(NewCount <= 0)
        {
            Res = Cur->Count;
            Ptr.NewFin(AllocNew());
            return Res;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return 0;
        }
        for(TypeIndex i = 0; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Res = Cur->Count - NewCount;
        Ptr.NewFin(NewArr);
        return Res;
    }
    template<typename _SetVal>
    bool _SetByArr(const _SetVal* Val, size_t Count)
    {
        auto Cur = Ptr.NewStart();
        intptr_t NewCount = Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return false;
        }
        for(size_t i = 0; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Val[i]);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Ptr.NewFin(NewArr);
        return true;
    }

    template<typename _SetVal>
    bool _AddByArr(const _SetVal* Val, size_t Count)
    {
        auto Cur = Ptr.NewStart();
        intptr_t NewCount = Cur->Count + Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return false;
        }
        TypeIndex i = 0;
        for(TypeIndex m = Cur->Count; i < m; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        for(TypeIndex j = 0; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Val[j]);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Ptr.NewFin(NewArr);
        return true;
    }

    bool _AddByTbl(const TypeLine* Val, size_t Count)
    {
        auto Cur = Ptr.NewStart();
        size_t NewCount = Cur->Count + Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return false;
        }
        TypeIndex i = 0;
        for(TypeIndex m = Cur->Count; i < m; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        for(TypeIndex j = 0; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Val[j].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Ptr.NewFin(NewArr);
        return true;
    }

public:

    class interator
    {
        friend LqPtdTbl;

        LocalPtr Ptr;
        TypeIndex Index;

        template<typename PtrType>
        inline interator(const PtrType& NewPtr, TypeIndex NewIndex = 0): Ptr(NewPtr), Index(NewIndex) {}
    public:
        inline interator(): Ptr(AllocNew()), Index(TypeIndex(-1)) {}
        inline interator(const interator& NewPtr) : Ptr(NewPtr.Ptr), Index(NewPtr.Index) {}
        inline interator& operator=(const interator& NewPtr) { Ptr.Set(NewPtr.Ptr); Index = NewPtr.Index; return *this; }
        //Preincrement
        inline interator& operator++() { ++Index; return (*this); }
        inline interator operator++(int) { ++Index; return *this; }
        inline interator& operator--() { ++Index; return (*this); }
        inline interator operator--(int) { --Index; return *this; }
        inline TypeVal& operator*() const { return Ptr->Line[Index].Val; }
        inline TypeVal& operator[](TypeIndex Index) const { return Ptr->Line[this->Index + Index].Val; }
        inline TypeVal* operator->() const { return &Ptr->Line[Index].Val; }
        inline interator operator+(int Add) const { return interator(Ptr, Index + Add); }
        inline interator operator-(int Sub) const { return interator(Ptr, Index - Sub); }

        inline interator& operator+=(TypeIndex Add) { Index += Add; return (*this); }
        inline interator& operator-=(TypeIndex Sub) { Index -= Sub; return (*this); }
        inline bool operator!=(const interator& Another) const
        {
            if(Another.Index == TypeIndex(-1))
                return Index != Ptr->Count;
            return (Ptr != Another.Ptr) || (Index != Another.Index);
        }
        inline bool operator==(const interator& Another) const
        {
            if(Another.Index == TypeIndex(-1))
                return Index == Ptr->Count;
            return (Ptr == Another.Ptr) && (Index == Another.Index);
        }
        inline size_t size() const { return Ptr->Count; }
        inline bool is_end() const { return Index >= Ptr->Count; }
        inline void set_end() { Index = TypeIndex(-1); }
    };

    inline LqPtdTbl(): Ptr(AllocNew()) {}
    inline LqPtdTbl(const LqPtdTbl& Val) : Ptr(Val.Ptr) {}
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdTbl(const std::initializer_list<InType> _Ax) : Ptr(AllocNew()) { _SetByArr(_Ax.begin(), _Ax.size()); }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdTbl(const std::vector<InType>& _Ax) : Ptr(AllocNew()) { _SetByArr(_Ax.data(), _Ax.size()); }
    inline LqPtdTbl(TypeVal&& _Ax) : Ptr(AllocNew()) { push_back(_Ax); }

    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdTbl& operator=(const std::initializer_list<InType> Val) { _SetByArr(Val.begin(), Val.size()); return *this; }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline LqPtdTbl& operator=(const std::vector<InType>& Val) { _SetByArr(Val.data(), Val.size()); return *this; }
    inline LqPtdTbl& operator=(const LqPtdTbl& Val) { Ptr.Set(Val.Ptr); return (LqPtdTbl&)Val; }

    inline interator begin() const { return Ptr; }
    inline interator end() const { return interator(); }

    int append(interator& Start, interator& End)
    {
        if(IsDebug)
        {
            if(Start.Ptr != End.Ptr)
                throw "LqPtdArr::append(interator, interator): Interatrs points to different arrays. (Check threads race cond)\n";
        }
        if(_AddByTbl(Start.Ptr.Get() + Start.Index, End.Index - Start.Index))
            return End.Index - Start.Index;
        return 0;
    }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    int append(const std::initializer_list<InType> Start) { return _AddByArr(Start.begin(), Start.size()) ? Start.size() : 0; }
    int append(interator& Start)
    {
        if(_AddByTbl(Start.Ptr.Get() + Start.Index, Start.Ptr->Count - Start.Index))
            return Start.Ptr->Count - Start.Index;
        return 0;
    }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline int append(const std::vector<InType>& Val) { return _AddByArr(Val.data(), Val.size())? Val.size(): 0; }
    inline int append(const LqPtdTbl& Val) { return append(Val.begin()); }
    template<typename InType, typename = decltype(TypeVal(std::declval<InType>()))>
    inline int append(const InType* Val, size_t Count) { return _AddByArr(Val, Count) ? Count : 0; }
    inline int unappend(size_t Count) { return _RmCount(Count); }
    inline void swap(LqPtdTbl& AnotherVal) { Ptr.Swap(AnotherVal.Ptr); }

    template<
        typename InType, 
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(), std::declval<InType>()))
    >
    bool push_back(InType&& NewVal) { return _AddByArr(&NewVal, 1); }

    template<
        typename InType, 
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(), std::declval<InType>()))
    >
    int push_back_uniq(InType&& NewVal)
    {
        auto Cur = Ptr.NewStart();
        if(_GetCell(Cur, NewVal) != nullptr)
        {
            Ptr.NewFin(Cur);
            return 0;
        }
        intptr_t NewCount = Cur->Count + 1;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return -1;
        }
        TypeIndex i = 0;
        for(TypeIndex m = Cur->Count; i < m; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        new(&NewArr->Line[i].Val) TypeVal(NewVal);
        TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
        NewArr->Line[i].Next = NewArr->Line[di].Start;
        NewArr->Line[di].Start = i;
        Ptr.NewFin(NewArr);
        return 1;
    }

    template<
        typename InType, 
        typename InType2,
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(), std::declval<InType>())),
        typename = decltype(TypeVal(std::declval<InType2>()))
    >
    int replace(InType&& PrevVal, InType2&& NewVal)
    {
        auto Cur = Ptr.NewStart();
        auto RmIndex = _GetCellIndex(Cur, PrevVal);
        if(RmIndex == NullIndex)
        {
            Ptr.NewFin(Cur);
            return 0;
        }
        intptr_t NewCount = Cur->Count;
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return -1;
        }
        TypeIndex i = 0;
        for(; i < RmIndex; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        new(&NewArr->Line[i].Val) TypeVal(NewVal);
        TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
        NewArr->Line[i].Next = NewArr->Line[di].Start;
        NewArr->Line[di].Start = i;
        i++;
        for(; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Ptr.NewFin(NewArr);
        return 1;
    }

    inline bool pop_back() { return _RmCount(1) > 0; }

    template<
        typename InType,
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(), std::declval<InType>()))
    >
    interator search(InType&& SrchVal) const
    {
        LocalPtr LocPtr(Ptr);
        auto* Line = LocPtr->Line;
        if(LocPtr->Count > 0)
        {
            for(TypeIndex Index = Line[TypeCmp::IndexByKey(SrchVal, LocPtr->Count)].Start;
                Index != NullIndex;
                Index = Line[Index].Next)
            {
                if(TypeCmp::Cmp(Line[Index].Val, SrchVal))
                    return interator(LocPtr, Index);
            }
        }
        return interator();
    }

    interator search_next(interator Prev) const
    {
        LocalPtr LocPtr(Ptr);
        auto& v = *Prev;
        auto* Line = LocPtr->Line;
        if(LocPtr->Count > 0)
        {
            for(TypeIndex Index = Line[Prev.Index].Next;
                Index != NullIndex;
                Index = Line[Index].Next)
            {
                if(TypeCmp::Cmp(Line[Index].Val, v))
                    return interator(LocPtr, Index);
            }
        }
        return interator();
    }
    template<
        typename InType, 
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(), std::declval<InType>()))
    >
    inline bool remove_by_val(InType&& RmVal) { return remove_by_val<TypeVal>(RmVal, nullptr); }

    template<
        typename TypeGetVal,
        typename InType, 
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(),std::declval<InType>())), 
        typename = decltype(TypeGetVal(std::declval<TypeVal>()))
    >
    bool remove_by_val(InType&& RmVal, TypeGetVal* RemovedVal)
    {
        TypeIndex RmIndex;
        auto Cur = Ptr.NewStart();
        if((Cur->Count <= 0) || ((RmIndex = _GetCellIndex(Cur, RmVal)) == NullIndex))
        {
            Ptr.NewFin(Cur);
            return false;
        }
        if(RemovedVal)
            *RemovedVal = Cur->Line[RmIndex].Val;
        intptr_t NewCount = Cur->Count - 1;
        if(NewCount <= 0)
        {
            Ptr.NewFin(AllocNew());
            return true;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return false;
        }
        TypeIndex i = 0;
        for(; i < RmIndex; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        if(RemovedVal != nullptr)
            *RemovedVal = Cur->Line[i].Val;
        for(; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i + 1].Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Ptr.NewFin(NewArr);
        return true;
    }

    template<
        typename InType,
        typename = decltype(TypeCmp::Cmp(std::declval<TypeVal>(), std::declval<InType>()))
    >
    intptr_t remove_mult_by_val(InType&& RmVal)
    {
        auto Cur = Ptr.NewStart();
        TypeLine* RmCell;
        if((Cur->Count <= 0) || ((RmCell = _GetCell(Cur, RmVal)) == nullptr))
        {
            Ptr.NewFin(Cur);
            return 0;
        }
        size_t CountDelete = 1;
        for(auto i = RmCell.Next; i != NullIndex; i = Cur->Line[i].Next)
        {
            if(TypeCmp::Cmp(Cur->Line[i].Val, RmVal))
                CountDelete++;
        }
        intptr_t NewCount = Cur->Count - CountDelete;
        if(NewCount <= 0)
        {
            Ptr.NewFin(AllocNew());
            return CountDelete;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return 0;
        }
        TypeIndex le = 0; //Last empty
        for(auto* ci = Cur->Line, *cm = ci + Cur->Count; ci < cm; ci++)
        {
            if(TypeCmp::Cmp(ci->Val, RmVal))
                continue;
            new(&NewArr->Line[le].Val) TypeVal(ci->Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[le].Val, NewCount);
            NewArr->Line[le].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = le;
            le++;
        }
        Ptr.NewFin(NewArr);
        return CountDelete;
    }

    inline bool remove_by_compare_fn(std::function<bool(TypeVal&)> CompareFn) { return remove_by_compare_fn<TypeVal>(CompareFn, nullptr); }

    template<
        typename TypeGetVal,
        typename = decltype(TypeGetVal(std::declval<TypeVal>()))
    >
    bool remove_by_compare_fn(std::function<bool(TypeVal&)> CompareFn, TypeGetVal* RemovedVal)
    {
        auto Cur = Ptr.NewStart();
        TypeIndex RmIndex = NullIndex;
        for(TypeIndex i = 0, m = Cur->Count; i < m; i++)
        {
            if(CompareFn(Cur->Line[i]->Val))
            {
                RmIndex = i;
                break;
            }
        }
        if(RmIndex == NullIndex)
        {
            Ptr.NewFin(Cur);
            return false;
        }
        size_t NewCount = Cur->Count - 1;
        if(NewCount <= 0)
        {
            Ptr.NewFin(AllocNew());
            return true;
        }
        auto NewArr = AllocNew(NewCount);
        if(NewArr == nullptr)
        {
            Ptr.NewFin(Cur);
            return false;
        }
        TypeIndex i = 0;
        for(; i < RmIndex; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i]->Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        if(RemovedVal != nullptr)
            *RemovedVal = Cur->Line[i]->Val;
        for(; i < NewCount; i++)
        {
            new(&NewArr->Line[i].Val) TypeVal(Cur->Line[i + 1]->Val);
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        Ptr.NewFin(NewArr);
        return true;
    }

    size_t remove_mult_by_compare_fn(std::function<bool(TypeVal&)> CompareFn)
    {
        auto Cur = Ptr.NewStart();
        Arr* NewArr;
        if((Cur->Count <= 0) || ((NewArr = AllocNew(Cur->Count)) == nullptr))
        {
            Ptr.NewFin(Cur);
            return 0;
        }
        TypeIndex NewCount = 0;
        for(auto* ci = Cur->Line, *cm = ci + Cur->Count, *cj = NewArr->Line; ci < cm; ci++)
        {
            if(CompareFn(ci->Val))
                continue;
            new(&cj->Val) TypeVal(ci->Val);
            cj++;
            NewCount++;
        }
        for(TypeIndex i = 0; i < NewCount; i++)
        {
            TypeIndex di = TypeCmp::IndexByKey(NewArr->Line[i].Val, NewCount);
            NewArr->Line[i].Next = NewArr->Line[di].Start;
            NewArr->Line[di].Start = i;
        }
        NewArr->Count = NewCount;
        size_t Res = Cur->Count - NewCount;
        Ptr.NewFin(NewArr);
        return Res;
    }

    int clear()
    {
        int Res = Ptr.NewStart()->Count;
        Ptr.NewFin(AllocNew());
        return Res;
    }

    size_t size() const
    {
        const LocalPtr p = Ptr;
        return p->Count;
    }
};

#pragma pack(pop)
