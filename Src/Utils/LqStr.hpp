#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqStr... - Typical string implementation (basic for UTF-8 CP). (C++ version)
*/


#include "LqStr.h"



template<typename TypeNumber>
int LqStrToInt(TypeNumber * Dest, const char * Str)
{
    TypeNumber Negative = (TypeNumber)1;
    int CountReaded = 0;
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
    *Dest = Ret * Negative;
    return CountReaded;
}

class LqParseInt
{
    const char* Str;
public:
    inline LqParseInt(const char* Val): Str(Val){}
    inline LqParseInt(const LqString& Val): Str(Val.c_str()) {}

    template<typename TypeNumber>
    inline operator TypeNumber() const
    {
        TypeNumber Res = 0;
        LqStrToInt(&Res, Str);
        return Res;
    }
};

class LqParseFloat
{
    const char* Str;
public:
    inline LqParseFloat(const char* Val): Str(Val) {}
    inline LqParseFloat(const LqString& Val): Str(Val.c_str()) {}

    template<typename TypeNumber>
    inline operator TypeNumber() const
    {
        return atof(Str);
    }
};

LQ_EXTERN_CPP_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqStrUtf8ToUtf16Stl(const char* lqautf8 SourceStr, LqString16& DestStr);
LQ_IMPORTEXPORT int LQ_CALL LqStrUtf16ToUtf8Stl(const wchar_t* lqautf8 SourceStr, LqString& DestStr);

LQ_EXTERN_CPP_END

inline int LqStrUtf8ToUtf16Stl(LqString& SourceStr, LqString16& DestStr)
{
    return LqStrUtf8ToUtf16Stl(SourceStr.c_str(), DestStr);
}

inline int LqStrUtf16ToUtf8Stl(LqString16& SourceStr, LqString& DestStr)
{
    return LqStrUtf16ToUtf8Stl(SourceStr.c_str(), DestStr);
}