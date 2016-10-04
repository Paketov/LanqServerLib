#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpPth... - Functions for working with path and domens.
*/


#include <time.h>
#include "LqLock.hpp"
#include "LqPtdArr.hpp"
#include "LqPtdTbl.hpp"
#include "LqHttpPth.h"
#include "LqDef.hpp"
#include "LqHttp.h"


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqHttpDmn;
struct LqHttpPthCmp;

void _LqHttpPthDelete(LqHttpPth* Pth);
void _LqHttpDmnDelete(LqHttpDmn* Pth);

typedef LqShdPtr<LqHttpPth, _LqHttpPthDelete, false, false> LqHttpPthPtr;
typedef LqShdPtr<LqHttpDmn, _LqHttpDmnDelete, false, false> LqHttpDmnPtr;

struct LqHttpPthCmp 
{
    static uint16_t IndexByKey(const LqHttpPthPtr& CurPth, uint16_t MaxIndex);
    static uint16_t IndexByKey(const char* Key, uint16_t MaxIndex);

    static bool Cmp(const LqHttpPthPtr& CurPth, const LqHttpPthPtr& Key);
    static bool Cmp(const LqHttpPthPtr& CurPth, const char* Key);
};
typedef LqPtdTbl<LqHttpPthPtr, LqHttpPthCmp>            LqHttpPthTbl;

struct LqHttpDmn
{
    static uint16_t IndexByKey(const LqHttpDmnPtr& CurPth, uint16_t MaxIndex);
    static uint16_t IndexByKey(const char* Key, uint16_t MaxIndex);

    static bool Cmp(const LqHttpDmnPtr& CurPth, const LqHttpDmnPtr& Key);
    static bool Cmp(const LqHttpDmnPtr& CurPth, const char* Key);

    char*                               Name;
    size_t                              CountPointers;
    uint32_t                            NameHash;
    LqHttpPthTbl                        Pths;
};


typedef LqPtdTbl<LqHttpDmnPtr, LqHttpDmn>               LqHttpDmnTbl;
typedef LqPtdArr<LqHttpPth*>                            LqHttpPthArr;

#pragma pack(pop)
