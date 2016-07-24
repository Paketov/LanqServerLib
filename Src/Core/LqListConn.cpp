/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqWrkList - List of connections. Use for transfer connections between workers.
*/

#include "LqOs.h"
#include "LqListConn.hpp"
#include <string.h>


LqListConn::LqListConn():	Count(0), Conn(nullptr) {}

LqListConn::~LqListConn()
{
	if(Conn != nullptr)
	{
		for(size_t i = 0; i < Count; i++)
		{
			if(Conn[i] != nullptr)
				Conn[i]->Proto->EndConnProc(Conn[i]);
		}
		free(Conn);
	}
}

LqListConn::LqListConn(LqListConn&& AnotherList)
{
	Count = AnotherList.Count;
	AnotherList.Count = 0;
	Conn = AnotherList.Conn;
	AnotherList.Conn = nullptr;
}
LqListConn::LqListConn(const LqListConn& AnotherList): Count(0), Conn(nullptr)
{
	auto r = realloc(Conn, sizeof(Conn[0]) * AnotherList.Count);
	if(r == nullptr)
		return;
	Conn = (LqConn**)r;
	Count = AnotherList.Count;
	memcpy(Conn, AnotherList.Conn, sizeof(Conn[0]) * Count);
}

size_t LqListConn::GetCount() const
{
	return Count;
}

LqConn* &LqListConn::operator[](size_t Index) const
{
	return Conn[Index];
}

LqListConn & LqListConn::operator=(const LqListConn & AnotherList)
{
	auto r = realloc(Conn, sizeof(Conn[0]) * AnotherList.Count);
	if(r == nullptr)
		return *this;
	Conn = (LqConn**)r;
	Count = AnotherList.Count;
	memcpy(Conn, AnotherList.Conn, sizeof(Conn[0]) * Count);
	return *this;
}

bool LqListConn::Add(LqConn* NewConnection)
{
	auto r = realloc(Conn, (Count + 1) * sizeof(LqConn*));
	if(r == nullptr) return false;
	Conn = (LqConn**)r;
	Conn[Count] = NewConnection;
	Count++;
	return true;
}
