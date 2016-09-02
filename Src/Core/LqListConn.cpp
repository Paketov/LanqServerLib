/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkList - List of connections. Use for transfer connections between workers.
*/

#include "LqOs.h"
#include "LqConn.h"
#include "LqListConn.hpp"
#include <string.h>



LqListEvnt::LqListEvnt(): Count(0), Conn(nullptr) {}

LqListEvnt::~LqListEvnt()
{
    if(Conn != nullptr)
    {
        for(size_t i = 0; i < Count; i++)
        {
            if(Conn[i] != nullptr)
            {
                LqEvntHdrClose(Conn[i]);
            }
        }
        free(Conn);
    }
}

LqListEvnt::LqListEvnt(LqListEvnt&& AnotherList)
{
    Count = AnotherList.Count;
    AnotherList.Count = 0;
    Conn = AnotherList.Conn;
    AnotherList.Conn = nullptr;
}
LqListEvnt::LqListEvnt(const LqListEvnt& AnotherList): Count(0), Conn(nullptr)
{
    auto r = realloc(Conn, sizeof(Conn[0]) * AnotherList.Count);
    if(r == nullptr)
        return;
    Conn = (LqEvntHdr**)r;
    Count = AnotherList.Count;
    memcpy(Conn, AnotherList.Conn, sizeof(Conn[0]) * Count);
}

size_t LqListEvnt::GetCount() const
{
    return Count;
}

LqEvntHdr* &LqListEvnt::operator[](size_t Index) const
{
    return Conn[Index];
}

LqListEvnt & LqListEvnt::operator=(const LqListEvnt & AnotherList)
{
    auto r = realloc(Conn, sizeof(Conn[0]) * AnotherList.Count);
    if(r == nullptr)
        return *this;
    Conn = (LqEvntHdr**)r;
    Count = AnotherList.Count;
    memcpy(Conn, AnotherList.Conn, sizeof(Conn[0]) * Count);
    return *this;
}

bool LqListEvnt::Add(LqEvntHdr* NewConnection)
{
    auto r = realloc(Conn, (Count + 1) * sizeof(LqEvntHdr*));
    if(r == nullptr) return false;
    Conn = (LqEvntHdr**)r;
    Conn[Count] = NewConnection;
    Count++;
    return true;
}
