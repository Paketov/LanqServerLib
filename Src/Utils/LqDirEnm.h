/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqDirEnm... - Enumerate files in directory.
*   Example:
*    LqDirEnm DirEnm;
*    char FileName[255];
*    char * Dir = "/usr/admin";
*    for(auto r = LqDirEnmStart(&DirEnm, Dir, FileName, sizeof(FileName), &Flag); r != -1; r = LqDirEnmNext(&DirEnm, FileName, sizeof(FileName), &Flag))
*    {
*        if(strcmp(FileName, "target") == 0) //Out from cycle when found "target" file
*        {
*            LqDirEnmBreak(DirEnm)
*            break;
*        }
*       printf("Found file: %s\n", FileName);
*    }
*/

#ifndef __LQ_DIRENM_H__HAS_INCLUDED__
#define __LQ_DIRENM_H__HAS_INCLUDED__

#include <stdint.h>
#include "LqFile.h"
#include "LqOs.h"

LQ_EXTERN_C_BEGIN

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqDirEnm
{
    uintptr_t Hndl;
    char* Internal;
};

#pragma pack(pop)

LQ_IMPORTEXPORT int LQ_CALL LqDirEnmStart(LqDirEnm* lqaio Enm, const char* lqain Dir, char* lqaout DestName, size_t NameLen, uint8_t* lqaout lqaopt Type);
LQ_IMPORTEXPORT int LQ_CALL LqDirEnmNext(LqDirEnm* lqaio Enm, char* lqaout DestName, size_t NameLen, uint8_t* lqaout lqaopt Type);
LQ_IMPORTEXPORT void LQ_CALL LqDirEnmBreak(LqDirEnm* lqaio Enm);

LQ_EXTERN_C_END

#endif
