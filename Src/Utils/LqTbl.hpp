#pragma once

#include <stdio.h>
#include <type_traits>
#include <string.h>
#include <functional>
#include "LqAlloc.hpp"



/*
LqTbl
 Solodov A. N. (hotSAN)
 2016

Low-level hash table.
This another version of table.
This table is characterized in that it has a dynamic memory allocation for each element.
This table fully compatible with  HASH_TABLE.
This, this ...
You have the freedom to choose between HASH_TABLE_DYN or HASH_TABLE.


Example:

        typedef struct HASH_ELEMENT
        {
                unsigned vKey;
                double   Val;

                bool SetKey(unsigned k)
                {
                        vKey = k;
                        return false;
                }

                inline static unsigned short IndexByKey(unsigned k, unsigned char MaxCount)
                {
                        return k % MaxCount;
                }

                inline unsigned short IndexInBound(unsigned char MaxCount) const
                {
                        return IndexByKey(KeyVal, MaxCount);
                }

                inline bool CmpKey(unsigned k)
                {
                        return k == vKey;
                }
        }  HASH_ELEMENT;

        HASH_TABLE_DYN<HASH_ELEMENT> HashArray(12);

        HashArray.Insert(0.000012)->Val = 0.000012;
        printf("%lf", HashArray.Search(0.000012)->Val);

*/

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)


template
<
    typename TElementStruct,
    typename TIndex = decltype(std::declval<TElementStruct>().IndexInBound(0)),
    TIndex NothingIndex = TIndex(-1)
>
class LqTbl {
public:
    typedef TIndex      IndexType, *LpIndexType;
    typedef TElementStruct      Element, *LpElement;
    struct Cell;
    typedef Cell *LpCell;
    typedef struct { LpCell Next; } HeadCell, *LpHeadCell;
    struct Cell: public HeadCell, public TElementStruct {};


    static inline void CopyElement(Cell& Dest, const Cell& Source) { *(TElementStruct*)((LpHeadCell)&Dest + 1) = *(TElementStruct*)((LpHeadCell)&Source + 1); }

    static inline LpCell GetCellByElement(TElementStruct* v) {/*Oh noo!!! His used offsets in C++ code! - Trust me, I known better =))*/
        return LpCell((char*)v - ((intptr_t)(TElementStruct*)((Cell*)1000) - 1000));
    }

protected:

    LpCell* Table;
    IndexType count, alloc_count;
public:

    size_t Count() const { return count; }

    size_t AllocCount() const { return alloc_count; }

    bool IsFull() const { return count >= alloc_count; }

    IndexType EmptyCount() const { return alloc_count - count; }

    static const IndexType EmptyElement = NothingIndex;

    inline LpCell* GetTable() const { return Table; }

    template<typename TYPE_KEY>
    inline IndexType IndexByKey(TYPE_KEY Key) const { return TElementStruct::IndexByKey(Key, alloc_count); }

    template<typename TYPE_KEY>
    inline LpCell* ElementByKey(TYPE_KEY Key) const { return GetTable() + TElementStruct::IndexByKey(Key, alloc_count); }

protected:

    bool ReallocAndClear(IndexType NewAllocCount) {
        if(NewAllocCount <= 0)
            NewAllocCount = 1;
        LpCell* Res = (LpCell*)___realloc(Table, sizeof(LpCell) * NewAllocCount);
        if(Res == nullptr)
            return false;
        memset(Table = Res, 0, sizeof(LpCell) * (alloc_count = NewAllocCount));
        return true;
    }

    bool Realloc(IndexType NewAllocCount) {
        IndexType c = NewAllocCount;
        if(NewAllocCount <= 0)
            c = 1;
        LpCell *Res = (LpCell*)___realloc(Table, sizeof(LpCell) * c);
        if(Res == nullptr)
            return false;
        if(NewAllocCount <= 0)
            *Res = nullptr;
        Table = Res;
        alloc_count = c;
        return true;
    }
public:

    /*
            After call this constructor AllocCount = NewAllocCount.
    */
    LqTbl(IndexType NewAllocCount = 1) {
        Table = nullptr;
        alloc_count = count = 0;
        ReallocAndClear(NewAllocCount);
    }

    inline ~LqTbl() {
        if(Table != nullptr) {
            for(LpCell *s = Table, *m = s + alloc_count; s < m; s++)
                for(LpCell i = *s; i != nullptr; ) {
                    LpCell DelElem = i;
                    i = DelElem->HeadCell::Next;
                    LqFastAlloc::Delete(DelElem);
                }
            ___free(Table);
        }
    }

    /*
            Insert with checkin have element
    */
    template<typename T>
    inline TElementStruct* Insert(T SearchKey) {
        LpCell *lpStart = ElementByKey(SearchKey);
        for(; *lpStart != nullptr; lpStart = &(*lpStart)->HeadCell::Next) {
            if((*lpStart)->CmpKey(SearchKey))
                return *lpStart;
        }
        if((*lpStart = LqFastAlloc::New<Cell>()) == nullptr)
            return nullptr;
        (*lpStart)->HeadCell::Next = nullptr;
        (*lpStart)->SetKey(SearchKey);
        count++;
        return *lpStart;
    }

    /*
            Insert element without check availability element.
            In case collision for read multiple values use NextCollision function.
    */
    template<typename T>
    inline TElementStruct* OnlyInsert(T SearchKey) {
        LpCell *lpStart = ElementByKey(SearchKey), NewElem;
        if((NewElem = LqFastAlloc::New<Cell>()) == nullptr)
            return nullptr;
        NewElem->HeadCell::Next = *lpStart;
        (*lpStart = NewElem)->SetKey(SearchKey);
        count++;
        return NewElem;
    }

    /*
            Simple search element by various type key
    */
    template<typename T>
    inline TElementStruct* Search(T SearchKey) const {
        for(LpCell lpStart = GetTable()[TElementStruct::IndexByKey(SearchKey, alloc_count)]; lpStart != nullptr; lpStart = lpStart->HeadCell::Next) {
            if(lpStart->CmpKey(SearchKey))
                return lpStart;
        }
        return nullptr;
    }

    /*
            Search next collision in table.
            Caution! You can not change the table between Search and NextCollision.
    */
    template<typename T>
    inline TElementStruct* NextCollision(TElementStruct* CurElem, T SearchKey) const {
        for(LpCell lpNext = GetCellByElement(CurElem)->HeadCell::Next; lpNext != nullptr; lpNext = lpNext->HeadCell::Next) {
            if(lpNext->CmpKey(SearchKey))
                return lpNext;
        }
        return nullptr;
    }

    void DeleteRetPointer(TElementStruct* Pointer) {
        LqFastAlloc::Delete(GetCellByElement(Pointer));
    }

    /*
            Remove element by key.
            Return address element in table.
    */
    template<typename T>
    TElementStruct* RemoveAndRetPointer(T SearchKey) {
        for(LpCell *lpStart = ElementByKey(SearchKey); *lpStart != nullptr; lpStart = &(*lpStart)->HeadCell::Next) {
            if((*lpStart)->CmpKey(SearchKey)) {
                auto DelElem = *lpStart;
                *lpStart = DelElem->HeadCell::Next;
                count--;
                return DelElem;
            }
        }
        return nullptr;
    }

    /*
            Remove element by key.
            Return address element in table.
    */
    template<typename T>
    bool Remove(T SearchKey) {
        for(LpCell *lpStart = ElementByKey(SearchKey); *lpStart != nullptr; lpStart = &(*lpStart)->HeadCell::Next) {
            if((*lpStart)->CmpKey(SearchKey)) {
                auto DelElem = *lpStart;
                *lpStart = DelElem->HeadCell::Next;
                count--;
                LqFastAlloc::Delete(DelElem);
                return true;
            }
        }
        return false;
    }

    /*
            Remove element by key.
            Return all cell.
            In case call this method you must call LqFastAlloc::Delete for correct delete element;
    */
    template<typename T>
    LpCell RemoveRow(T SearchKey) {
        for(LpCell *lpStart = ElementByKey(SearchKey); *lpStart != nullptr; lpStart = &(*lpStart)->HeadCell::Next) {
            if((*lpStart)->CmpKey(SearchKey)) {
                LpCell DelElem = *lpStart;
                *lpStart = DelElem->HeadCell::Next;
                count--;
                return DelElem;
            }
        }
        return nullptr;
    }

    /*
            Remove all collision element by key.
            To find out the number of deleted items, check property GetCount.
    */
    template<typename T>
    void RemoveAllCollision(T SearchKey) {
        for(LpCell *i = ElementByKey(SearchKey); *i != nullptr; ) {
            if((*i)->CmpKey(SearchKey)) {
                LpCell DelElem = *i;
                *i = DelElem->HeadCell::Next;
                LqFastAlloc::Delete(DelElem);
                count--;
            } else
                i = &((*i)->Next);
        }
    }

    /*
            Clear all table.
            Deletes all element from table.
    */
    void Clear() {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell i = *s; i != nullptr; ) {
                LpCell DelElem = i;
                i = DelElem->HeadCell::Next;
                LqFastAlloc::Delete(DelElem);
            }
        ReallocAndClear(1);
        count = 0;
    }

    /*
            Enumerate all elements in the table with the EnumFunc.
            Caution! When you call this function, you can not change the contents of the table!
    */
    inline bool EnumValues(bool(*EnumFunc)(void* UserData, TElementStruct* Element), void* UserData = nullptr) const {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell i = *s; i != nullptr; i = i->HeadCell::Next) {
                if(!EnumFunc(UserData, i))
                    return false;
            }
        return true;
    }

    inline bool EnumValues2(std::function<bool(TElementStruct*)> EnumFunc) const {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell i = *s; i != nullptr; i = i->HeadCell::Next) {
                if(!EnumFunc(i))
                    return false;
            }
        return true;
    }

    /*
            Enumerate all elements in the table with the EnumFunc.
            Caution! When you call this function, you can not change the contents of the table!
    */
    inline bool EnumValues(bool(*EnumFunc)(TElementStruct* Element)) {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell i = *s; i != nullptr; i = i->HeadCell::Next) {
                if(!EnumFunc(i))
                    return false;
            }
        return true;
    }

    /*
            Enumerate all elements in the table with the EnumFunc.
            Is function return true element will be removed.
            Caution! When you call this function, you can not change the contents of the table!
    */
    inline void EnumDelete(bool(*IsDeleteProc)(void* UserData, TElementStruct* Element), void* UserData = nullptr) {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell *i = s; *i != nullptr; ) {
                if(IsDeleteProc(UserData, *i)) {
                    LpCell DelElem = *i;
                    *i = (*i)->HeadCell::Next;
                    LqFastAlloc::Delete(DelElem);
                    count--;
                } else
                    i = &((*i)->HeadCell::Next);
            }
    }

    /*
            Enumerate all elements in the table with the EnumFunc.
            Is function return true element will be removed.
            Caution! When you call this function, you can not change the contents of the table!
    */
    inline void EnumDelete(bool(*IsDeleteProc)(TElementStruct* Element)) {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell* i = s; *i != nullptr; ) {
                if(IsDeleteProc(*i)) {
                    LpCell DelElem = *i;
                    *i = DelElem->HeadCell::Next;
                    LqFastAlloc::Delete(DelElem);
                    count--;
                } else
                    i = &((*i)->HeadCell::Next);
            }
    }

    inline void EnumDelete2(std::function<bool(TElementStruct*)> IsDeleteProc) {
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell* i = s; *i != nullptr; ) {
                if(IsDeleteProc(*i)) {
                    LpCell DelElem = *i;
                    *i = DelElem->HeadCell::Next;
                    LqFastAlloc::Delete(DelElem);
                    count--;
                } else
                    i = &((*i)->HeadCell::Next);
            }
    }

    /*========================================================*/
    /*Resize table functional*/
    /*
            Resize table to GetCount size. After call AllocCount == GetCount.
            Since it is a low-level(!) hash table, a change in the size you have to do manually.
            This is made to maximize the flexibility of the algorithm table.
            After removal of the items, you are free to choose whether to reduce the table to the number of existing elements.
    */
    inline bool ResizeAfterRemove() { return ResizeAfterRemove(count); }

    bool ResizeAfterRemove(IndexType NewCount) {
        if(NewCount <= 0) {
            Clear();
            return true;
        } else
            return ResizeBeforeInsert(NewCount);
    }

    /*
            Resize table AllocCount to NewCount. After call AllocCount == NewCount.
            If before call NewCount < AllocCount, then behavior is unpredictable!
            Since it is a low-level(!) hash table, a change in the size you have to do manually.
            To verify the necessity of calling this function, use the property IsFull, next
            calculate NewCount and call this function.
    */
    bool ResizeBeforeInsert(IndexType NewCount) {
        LpCell UsedList = nullptr;
        for(LpCell *s = GetTable(), *m = s + alloc_count; s < m; s++)
            for(LpCell i = *s; i != nullptr; ) {
                LpCell j = UsedList;
                UsedList = i;
                i = i->HeadCell::Next;
                UsedList->HeadCell::Next = j;
            }
        bool r = ReallocAndClear(NewCount);
        LpCell *t = GetTable();
        while(UsedList != nullptr) {
            LpCell* j = t + UsedList->IndexInBound(alloc_count), c = *j;
            UsedList = (*j = UsedList)->HeadCell::Next;
            (*j)->HeadCell::Next = c;
        }
        return r;
    }


    /*========================================================*/
    /*
            Clone this table to another
    */
    bool Clone(LqTbl<TElementStruct, TIndex, NothingIndex>& Dest) const {
        LpCell UsedList = nullptr;
        if(Dest.count >= 0) {
            for(LpCell *s = Dest.GetTable(), *m = s + Dest.alloc_count; s < m; s++)
                for(LpCell i = *s; i != nullptr; ) {
                    LpCell j = UsedList;
                    UsedList = i;
                    i = i->HeadCell::Next;
                    UsedList->HeadCell::Next = j;
                }
        }
        if(!Dest.ReallocAndClear(alloc_count)) {
            /*If not alloc memory, then return all back*/
            LpCell* dt = Dest.GetTable();
            for(; UsedList != nullptr;) {
                LpCell* j = dt + UsedList->IndexInBound(Dest.alloc_count);
                LpCell c = *j;
                *j = UsedList;
                UsedList = UsedList->HeadCell::Next;
                (*j)->HeadCell::Next = c;
            }
            return false;
        }
        LpCell *st = GetTable(), *dt = Dest.GetTable();
        for(IndexType k = 0, m = alloc_count; k < m; k++)
            for(LpCell i = st[k]; i != nullptr; i = i->HeadCell::Next) {
                if(UsedList == nullptr) {
                    /*If used list == null, then for faster jump to cilcle without checkin*/
                    while(true) {
                        for(; i != nullptr; i = i->HeadCell::Next) {
                            LpCell n = LqFastAlloc::New<Cell>();
                            CopyElement(*n, *i);
                            n->HeadCell::Next = dt[k];
                            dt[k] = n;
                        }
                        k++;
                        if(k >= m) break;
                        i = st[k];
                    };
                    goto lblOut;
                }
                LpCell n = UsedList;
                UsedList = UsedList->HeadCell::Next;
                CopyElement(*n, *i);
                n->HeadCell::Next = dt[k];
                dt[k] = n;
            }
        while(UsedList != nullptr) {
            LpCell DelElem = UsedList;
            UsedList = UsedList->HeadCell::Next;
            LqFastAlloc::Delete(DelElem);
        }
lblOut:
        Dest.count = count;
        return true;
    }
    /*
            Move this table to another.
            After call GetCount == 0.
    */
    bool Move(LqTbl<TElementStruct, TIndex, NothingIndex>& Dest) {
        if(!Dest.ReallocAndClear(1))
            return false;
        LpCell* t = Dest.Table;
        Dest.Table = Table;
        Table = t;
        Dest.alloc_count = alloc_count;
        alloc_count = 1;
        Dest.count = count;
        count = 0;
        return true;
    }


    /*========================================================*/
    /*
            While you use interator, you can't use ResizeAfterRemove or ResizeBeforeInsert functions.
            You can use Remove or RemoveAllCollision functions, but if you use Insert or OnlyInsert
            inserted elements may be listed or not.
            You can't remove iterate element via Remove or RemoveAllCollision functions.

    */
    /*
            Interator type
    */
    typedef struct Iterator {
        friend LqTbl;
    public:
        union {
            class {
                friend Iterator;
                friend LqTbl;
                IndexType CurStartList;
                LpCell CurElementInList;
            public:
                inline operator bool() const { return CurStartList == EmptyElement; }
            } IsShouldEnd;
        };
        void StartAgain() { IsShouldEnd.CurStartList = EmptyElement; }
        inline Iterator() { StartAgain(); }
    } Iterator, *LpInterator;

    /*
            Start or continue interate table.
    */
    bool Interate(Iterator& SetInterator) const {
        IndexType p;
        LpCell *t = GetTable();
        if(SetInterator.IsShouldEnd) {
            p = 0;
lblSearchStart:
            for(IndexType m = alloc_count; p < m; p++)
                if(t[p] != nullptr) {
                    SetInterator.IsShouldEnd.CurStartList = p;
                    SetInterator.IsShouldEnd.CurElementInList = t[p];
                    return true;
                }
            SetInterator.IsShouldEnd.CurStartList = EmptyElement;
            return false;
        }
        if(SetInterator.IsShouldEnd.CurElementInList->Next != nullptr) {
            SetInterator.IsShouldEnd.CurElementInList = SetInterator.IsShouldEnd.CurElementInList->Next;
            return true;
        }
        p = SetInterator.IsShouldEnd.CurStartList + 1;
        goto lblSearchStart;
    }

    /*
            Check interator is correct.
    */
    inline bool InteratorCheck(const Iterator& Interator) const {
        if(Interator.IsShouldEnd)
            return false;
        return true;
    }
    /*
            Get element by interator.
    */
    static inline TElementStruct* ElementByInterator(const Iterator& SetInterator) { return SetInterator.IsShouldEnd.CurElementInList; }

    /*
            Search key and set interator to key position.
    */
    template<typename TKey>
    bool InteratorByKey(TKey SearchKey, Iterator& Interator) {
        LpCell *t = GetTable();
        IndexType s = TElementStruct::IndexByKey(SearchKey, alloc_count);
        for(LpCell i = t[s]; i != nullptr; i = i->HeadCell::Next)
            if(i->CmpKey(SearchKey)) {
                Interator.IsShouldEnd.CurStartList = s;
                Interator.IsShouldEnd.CurElementInList = i;
                return true;
            }
        return false;
    }
    /*
            Remove by interator.
    */
    TElementStruct* RemoveByInteratorRetPointer(Iterator& Interator) {
        for(LpCell *DelElem = GetTable() + Interator.IsShouldEnd.CurStartList; *DelElem != nullptr; DelElem = &(*DelElem)->HeadCell::Next) {
            if(*DelElem == Interator.IsShouldEnd.CurElementInList) {
                Interate(Interator);
                auto El2 = *DelElem;
                *DelElem = El2->HeadCell::Next;
                count--;
                return El2;
            }
        }
        return nullptr;
    }

    bool RemoveByInterator(Iterator& Interator) {
        for(LpCell *DelElem = GetTable() + Interator.IsShouldEnd.CurStartList; *DelElem != nullptr; DelElem = &(*DelElem)->HeadCell::Next) {
            if(*DelElem == Interator.IsShouldEnd.CurElementInList) {
                Interate(Interator);
                auto El2 = *DelElem;
                *DelElem = El2->HeadCell::Next;
                count--;
                LqFastAlloc::Delete(El2);
                return true;
            }
        }
        return false;
    }
    /*========================================================*/
    /*Interate by key val*/
    inline TElementStruct* GetStartCell() const {
        for(LpCell *p = GetTable(), *m = p + alloc_count; p < m; p++)
            if(*p != nullptr) {
                return *p;
            }
        return nullptr;
    }

    /*
            Next interate by key val
            Use only for not collision values! To do this, use only Insert function.
    */
    template<typename TKey>
    TElementStruct* GetNextCellByKey(TKey SearchKey) const {
        const LpCell *t = GetTable(), *i = ElementByKey(SearchKey);
        for(LpCell e = *i; e != nullptr; e = e->HeadCell::Next) {
            if(e->CmpKey(SearchKey)) {
                if(e->HeadCell::Next != nullptr)
                    return e->HeadCell::Next;
                i++;
                for(const LpCell *m = t + alloc_count; i < m; i++)
                    if(*i != nullptr)
                        return *i;
                return nullptr;
            }
        }
        return nullptr;
    }

    /*========================================================*/
    /*For debug*/
    unsigned QualityInfo(char * Buf, unsigned LenBuf) {
        unsigned CurLen = LenBuf, Len2, CurIndex = 0;
        Len2 = LqFwbuf_snprintf(
            Buf,
            CurLen,
            "Count elements: %u\nAlloc count elements: %u\nSize(in bytes): %u\nHash quality:\n",
            unsigned(count),
            unsigned(alloc_count),
            alloc_count * sizeof(Cell) + unsigned(alloc_count) * sizeof(LpCell));
        if(Len2 < 0)
            Len2 = 0;
        CurLen -= Len2;
        Buf += Len2;
        for(LpCell* c = GetTable(), *m = c + alloc_count; c < m; c++) {
            unsigned CountInCurIndex = 0;
            for(LpCell p = *c; p != nullptr; p = p->HeadCell::Next)
                CountInCurIndex++;

            Len2 = LqFwbuf_snprintf(Buf, CurLen, "%u:%u,", CurIndex, CountInCurIndex);
            if(Len2 < 0)
                Len2 = 0;
            CurLen -= Len2;
            Buf += Len2;
            CurIndex++;
        }
        return LenBuf - CurLen;
    }
};

#pragma pack(pop)
