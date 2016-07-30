/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFile... - File layer between os and server.
*/


#ifndef __LQ_FILE_H__HAS_INCLUDED__
#define __LQ_FILE_H__HAS_INCLUDED__

#include "LqDef.h"
#include "LqOs.h"
#include <stdint.h>

LQ_EXTERN_C_BEGIN

#define LQ_F_DIR                1
#define LQ_F_REG                2
#define LQ_F_OTHER              3
#define LQ_F_DEV                4

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct LqFileStat
{
    uint8_t             Type;
    uint16_t            Access;

    unsigned int        DevId;
    unsigned int        Id;
    unsigned int        RefCount;

    LqFileSz            Size;
    LqTimeSec           CreateTime;
    LqTimeSec           ModifTime;
    LqTimeSec           AccessTime;

    short               Gid;
    short               Uid;
};

#pragma pack(pop)

#define LQ_MAX_PATH 32767


/*
* LqFileOpen flags
*/
#define LQ_O_RD             0x0000
#define LQ_O_WR             0x0001
#define LQ_O_RDWR           0x0002
#define LQ_O_APND           0x0008
#define LQ_O_RND            0x0010
#define LQ_O_SEQ            0x0020
#define LQ_O_TMP            0x0040
#define LQ_O_NOINHERIT      0x0080
#define LQ_O_CREATE         0x0100
#define LQ_O_TRUNC          0x0200
#define LQ_O_EXCL           0x0400
#define LQ_O_SHORT_LIVED    0x1000
#define LQ_O_DSYNC          0x2000
#define LQ_O_TXT            0x4000
#define LQ_O_BIN            0x8000

#define LQ_SEEK_CUR         1
#define LQ_SEEK_END         2
#define LQ_SEEK_SET         0



LQ_IMPORTEXPORT int LQ_CALL LqFileOpen(const char* lqautf8 lqain FileName, uint32_t Flags, int Access);
LQ_IMPORTEXPORT int LQ_CALL LqFileRead(int Fd, void* lqaout DestBuf, unsigned int SizeBuf);
LQ_IMPORTEXPORT int LQ_CALL LqFileWrite(int Fd, const void* lqain SourceBuf, unsigned int SizeBuf);
LQ_IMPORTEXPORT int LQ_CALL LqFileClose(int Fd);
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqFileTell(int Fd);
/*
* @Flag: LQ_SEEK_CUR or LQ_SEEK_SET or LQ_SEEK_END
*/
LQ_IMPORTEXPORT LqFileSz LQ_CALL LqFileSeek(int Fd, LqFileSz Offset, int Flag);
LQ_IMPORTEXPORT int LQ_CALL LqFileEof(int Fd); //1 - end of file, 0 - not end, -1 - error

LQ_IMPORTEXPORT int LQ_CALL LqFileGetStat(const char* lqautf8 lqain FileName, LqFileStat* lqaout StatDest);
LQ_IMPORTEXPORT int LQ_CALL LqFileGetStatByFd(int Fd, LqFileStat* lqaout StatDest);
//Get name file by descriptor
LQ_IMPORTEXPORT int LQ_CALL LqFileGetPath(int Fd, char* lqautf8 lqaout DestBuf, unsigned int SizeBuf);

LQ_IMPORTEXPORT int LQ_CALL LqFileRemove(const char* lqautf8 lqain FileName);
LQ_IMPORTEXPORT int LQ_CALL LqFileMove(const char* lqautf8 lqain OldName, const char* lqautf8 lqain NewName);
LQ_IMPORTEXPORT int LQ_CALL LqFileMakeDir(const char* lqautf8 lqain NewDirName, int Access);
LQ_IMPORTEXPORT int LQ_CALL LqFileMakeSubdirs(const char* lqautf8 lqain NewSubdirsDirName, int Access);
LQ_IMPORTEXPORT int LQ_CALL LqFileRemoveDir(const char* lqautf8 lqain NewDirName);

LQ_IMPORTEXPORT int LQ_CALL LqFileRealPath(const char* lqain Source, char* lqaout Dest, size_t DestLen);

LQ_EXTERN_C_END


#endif
