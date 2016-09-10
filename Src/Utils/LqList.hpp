#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqList... - The usual list.
*/

#include "LqAlloc.hpp"
#include "LqOs.h"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

/*
* This class not thread safe!
*/
template <typename T>
class LqList
{
    struct ElH;
    struct ElH
    {
        ElH* n;
        ElH* p;
    };

    ElH s;
    size_t c;
public:

    class El
    {
        friend LqList<T>;
        ElH h;
    public:
        T d;
    };

    LqList()
    {
        c = 0;
        s.p = s.n = &s;
    }

    ~LqList()
    {
        for(auto i = s.n; i != &s;)
        {
            auto n = i->n;
            LqFastAlloc::Delete(i);
            i = n;
        }
    }

    El* Append()
    {
        auto e = LqFastAlloc::New<El>();
        if(e != nullptr)
        {
            e->h.n = s.n;
            e->h.p = &s;
            s.n = s.n->p = (ElH*)e;
            c++;
        }
        return e;
    }

    void Delete(El** Element)
    {
        auto e = *Element;
        *Element = e->h.n;
        e->h.n->p = e->h.p;
        e->h.p->n = e->h.n;
        LqFastAlloc::Delete(e);
        c--;
    }

    void DeleteAll()
    {
        for(auto i = s.n; i != &s;)
        {
            auto n = i->n;
            LqFastAlloc::Delete(i);
            i = n;
        }
    }

    El* Search(T& Data)
    {
        for(auto i = s.n; i != &s; i = i->n)
        {
            if(((El*)i)->d == Data)
                return (El*)i;
        }
        return nullptr;
    }

    El* StartEnum()
    {
        return (s.n == &s) ? nullptr : (El*)s.n;
    }

    El* NextEnum(El* Prev)
    {
        return (Prev->h.n == &s) ? nullptr : (El*)Prev->h.n;
    }

    size_t Count() const
    {
        return c;
    }
};


#pragma pack(pop)
