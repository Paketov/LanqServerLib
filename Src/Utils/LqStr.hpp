#pragma	once
/*
* Lanq(Lan Quick)
* Solodov A. N.	(hotSAN)
* 2016
*   LqStr... - Typical string implementation (basic for	UTF-8 CP). (C++	version)
*/


#include "LqStr.h"

template<typename TypeNumber>
int LqStrToInt(TypeNumber * Dest, const	char * Str)
{
    TypeNumber Negative = (TypeNumber)1;
    int	CountReaded = 0;
    if(std::is_signed<TypeNumber>::value)
	switch(Str[CountReaded])
	{
	    case '-':
		Negative = -1;
	    case '+':
		CountReaded++;
	}
    TypeNumber Ret = (TypeNumber)0;
    size_t t = CountReaded;
    if((Str[CountReaded] > '9') || (Str[CountReaded] < '0')) return 0;
    for(; ; CountReaded++)
    {
	unsigned char Digit = Str[CountReaded] - '0';
	if(Digit > 9)
	    break;
	Ret = Ret * 10 + Digit;
    }
    *Dest = Ret	* Negative;
    return CountReaded;
}