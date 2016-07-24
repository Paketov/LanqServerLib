#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*  Default reference and pointer.
*/
#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

class LqDfltPtr
{
	template<typename t, typename at>
	struct assoc_type { static at value; };
public:
	template<typename RetVal>
	inline operator RetVal*() const { struct s {}; return &assoc_type<s, RetVal>::value; }
};

template<typename TYPE, typename ASSOC_TYPE>
ASSOC_TYPE LqDfltPtr::assoc_type<TYPE, ASSOC_TYPE>::value;

class LqDfltRef
{
	template<typename t, typename at>
	struct assoc_type { static at value; };
public:
	template<typename RetVal>
	inline operator RetVal&() const { struct s {}; return assoc_type<s, RetVal>::value; }
};

#pragma pack(pop)


template<typename TYPE, typename ASSOC_TYPE>
ASSOC_TYPE LqDfltRef::assoc_type<TYPE, ASSOC_TYPE>::value;
