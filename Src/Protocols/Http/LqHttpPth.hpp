#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpPth... - Functions for working with path and domens.
*/


struct LqHttpDomainPaths;

#include <time.h>
#include "LqLock.hpp"
#include "LqHashTable.hpp"
#include "LqHttpPth.h"

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqHttpDomainPaths
{
	struct Element
	{
		LqHttpPth* p;

		bool SetKey(const LqHttpPth* NewKey);
		static size_t IndexByKey(const LqHttpPth* Key, size_t MaxCount);
		static size_t IndexByKey(const char* Key, size_t MaxCount);
		size_t IndexInBound(size_t MaxCount) const;
		bool CmpKey(const LqHttpPth* Key) const;
		bool CmpKey(const char* Key) const;
	};

	char*						NameDomain;
	size_t						NameHash;
	LqHashTable<Element>		t;

	bool SetKey(const char* Name);
	static size_t IndexByKey(const char* Key, size_t MaxCount);
	size_t IndexInBound(size_t MaxCount) const;
	bool CmpKey(const char* Key) const;
	LqHttpDomainPaths();
	~LqHttpDomainPaths();
};

struct LqHttpFileSystem
{
	LqHashTable<LqHttpDomainPaths>	t;
	LqLocker<uintptr_t>				l;
};

#pragma pack(pop)
