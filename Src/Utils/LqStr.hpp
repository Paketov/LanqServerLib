#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqStr... - Typical string implementation (basic for UTF-8 CP). (C++ version)
*/


#include "LqStr.h"
#include "LqDef.hpp"
#include "LqSbuf.h"

class LqParseInt {
    const char* Str;
public:
    inline LqParseInt(const char* Val): Str(Val) {}
    inline LqParseInt(const LqString& Val) : Str(Val.c_str()) {}

    template<typename TypeNumber>
    inline operator TypeNumber() const {
        if(((TypeNumber)-1) < 0) {
            long long Res = 0;
            LqStrToLl(&Res, Str, 10);
            return Res;
        }
        unsigned long long Res = 0;
        LqStrToUll(&Res, Str, 10);
        return Res;
    }
};

class LqParseFloat {
    const char* Str;
public:
    inline LqParseFloat(const char* Val): Str(Val) {}
    inline LqParseFloat(const LqString& Val) : Str(Val.c_str()) {}

    template<typename TypeNumber>
    inline operator TypeNumber() const {
        double Res;
        LqStrToDouble(&Res, Str, 10);
        return Res;
    }
};

LQ_EXTERN_CPP_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqStrUtf8ToUtf16Stl(const char* lqautf8 SourceStr, LqString16& DestStr);
LQ_IMPORTEXPORT int LQ_CALL LqStrUtf16ToUtf8Stl(const wchar_t* lqautf8 SourceStr, LqString& DestStr);

LQ_EXTERN_CPP_END

inline int LqStrUtf8ToUtf16Stl(LqString& SourceStr, LqString16& DestStr) {
    return LqStrUtf8ToUtf16Stl(SourceStr.c_str(), DestStr);
}

inline int LqStrUtf16ToUtf8Stl(LqString16& SourceStr, LqString& DestStr) {
    return LqStrUtf16ToUtf8Stl(SourceStr.c_str(), DestStr);
}