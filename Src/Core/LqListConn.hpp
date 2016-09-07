#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqListEvnt - List of connections. Use for manual transfer connections between workers.
*/

#include "LqAlloc.hpp"
#include "Lanq.h"

#pragma pack(push) 
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqListEvnt
{
    size_t              Count;
    LqEvntHdr**         Conn;
public:

    LqListEvnt();
    ~LqListEvnt();

    LqListEvnt(LqListEvnt&& Another);
    LqListEvnt(const LqListEvnt& Another);

    size_t GetCount() const;
    LqEvntHdr* &operator[](size_t Index) const;
    LqListEvnt& operator=(const LqListEvnt& AnotherList);

    bool Add(LqEvntHdr* NewConnection);
};

#pragma pack(pop)