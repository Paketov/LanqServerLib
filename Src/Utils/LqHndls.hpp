#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqHndls - Multiple events caller. Work in multiple thread area.
*/

#include "LqOs.h"
#include "LqPtdArr.hpp"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

template<typename TypeFunc>
class LqHndls
{
    LqPtdArr<TypeFunc> Arr;
public:

    typedef TypeFunc Func;

    inline LqHndls() {}
    inline LqHndls(const LqHndls& _Ax): Arr(_Ax.Arr) {}
    inline LqHndls(const std::initializer_list<TypeFunc> _Ax) : Arr(_Ax) {}
    inline LqHndls(TypeFunc _Ax) : Arr(_Ax) {}

    inline LqHndls& operator=(const LqHndls& _Ax) { Arr = _Ax.Arr; return *this; }
    inline LqHndls& operator=(const std::initializer_list<TypeFunc> _Ax) { Arr = _Ax; return *this; }

    inline bool Add(TypeFunc NewVal) { return Arr.push_back(NewVal); }

    inline bool Rm(TypeFunc RemVal) { return Arr.remove_mult_by_val(RemVal); }

    inline int RmAll() { return Arr.clear(); }

    inline size_t Count() { return Arr.count(); }

    /* Call all hanlers*/
    template<class... _Args, typename = decltype((std::declval<TypeFunc>())(std::declval<_Args>()...))>
    void Call(_Args&&... _Ax)
    {
        for(auto i = Arr.begin(); !i.is_end(); ++i)
            (*i)(_Ax...);
    }

};
#pragma pack(pop)