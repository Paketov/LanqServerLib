#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqListConn - List of connections. Use for transfer connections between workers.
*/

#include "LqAlloc.hpp"
#include "Lanq.h"

#pragma pack(push) 
#pragma pack(LQCACHE_ALIGN_FAST)

class LQ_IMPORTEXPORT LqListConn
{ 
	size_t			Count;
	LqConn**		Conn;
public:

	LqListConn();
	~LqListConn();

	LqListConn(LqListConn&& Another);
	LqListConn(const LqListConn& Another);

	size_t GetCount() const;
	LqConn* &operator[](size_t Index) const;
	LqListConn& operator=(const LqListConn& AnotherList);

	bool Add(LqConn* NewConnection);
};

#pragma pack(pop)