/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFileTrd... - (Lanq File Transaction) Implements transactions for correct saving file in OS FS.
*/

#include "LqOs.h"
#include "LqDef.hpp"
#include "LqErr.h"
#include "LqFile.h"
#include "LqFileTrd.h"
#include "LqTime.h"
#include "LqDfltRef.hpp"
#include "LqStr.h"

#include <string>
#include <string.h>
#include <stdlib.h>

#if defined(LQPLATFORM_WINDOWS)
#include <Windows.h>
#endif


#define LQ_RECIVE_PARTIAL_MASK           ".partial_**************_"
#define LQ_RECIVE_PARTIAL_MASK_WCHAR    L".partial_**************_"

#define STR_TYPE(_Type, _Str)       ((std::is_same<_Type, wchar_t>::value)?(_Type*)(L ## _Str):(_Type*)(_Str))
#define CHAR_TYPE(_Type, _Char)     ((_Type)((std::is_same<_Type, char>::value)?(_Char):(L ## _Char)))

template<typename CharT>
static CharT* MakeTempName(CharT* Res) {
    static CharT* h = STR_TYPE(CharT, "0123456789abcdef");
    LqTimeMillisec TMillisec = LqTimeGetLocMillisec() * rand() + rand();
    CharT* c = Res;
    for(; *c; c++) {
        if(*c != CHAR_TYPE(CharT, '*')) continue;
        for(unsigned i = 0; i < sizeof(TMillisec); i++) {
            if(*c != CHAR_TYPE(CharT, '*')) break;
            *c++ = h[TMillisec & 0xf];
            TMillisec >>= 4;

            if(*c != CHAR_TYPE(CharT, '*')) break;
            *c++ = h[TMillisec & 0xf];
            TMillisec >>= 4;
        }
        break;
    }
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdIsTransacted(int Fd) {
	LqString Path("", 32768);
	intptr_t Res;
	static const char FindChar[] = {LQ_PATH_SEPARATOR, '\0'};
	size_t Pos;

    if(Fd == -1)
        return -1;
    Res = LqFileGetPath(Fd, (char*)Path.data(), Path.size() - 2);
	if(Res == -1)
		return -1;
    if((Pos = Path.find_last_of(FindChar)) == LqString::npos) {
        if(LqStrSameMax(Path.c_str(), ".partial_", sizeof(".partial_") - 1))
            return 1;
    } else {
        if(LqStrSameMax(Path.data() + Pos + 1, ".partial_", sizeof(".partial_") - 1))
            return 1;
    }
    return 0;
}

/*
* Flags must be O_TEXT or O_BINARY
* LQ_TRNSC_REPLACE
* LQ_TRNSC_CREATE_SUBDIRS
*/

LQ_EXTERN_C int LQ_CALL LqFileTrdCreate(const char* FileName, uint32_t Flags, int Access) {
    LqFileStat Stat;
	LqString LocalFileName, Path;
	size_t SepPos;
	int Fd, i;

    if(LqFileGetStat(FileName, &Stat) == 0) {
        if(!(LQ_TC_REPLACE & Flags) || (Stat.Type == LQ_F_DIR)) {
            lq_errno_set(EEXIST);
            return -1;
        }
    }
    LocalFileName = FileName;
    SepPos = LocalFileName.find_last_of(LQ_PATH_SEPARATOR);
    LocalFileName.insert(SepPos + 1, LQ_RECIVE_PARTIAL_MASK);
    Fd = -1;
    for(i = 0; (Fd == -1) && (i < 3); i++) {
        Path = LocalFileName;
        Fd = LqFileOpen(MakeTempName((char*)Path.c_str()), LQ_O_CREATE | (Flags & 0xffff), Access);
    }
    if((Fd == -1) && (lq_errno == ENOENT)) {
        if(Flags & LQ_TC_SUBDIR) {
            if(LqFileMakeSubdirs(LocalFileName.c_str(), Access) != 0)
                return -1;

            return LqFileTrdCreate(FileName, Flags, Access);
        } else {
            return -1;
        }
    }
    return Fd;
}


/*
* Commit file to another place
*/
LQ_EXTERN_C int LQ_CALL LqFileTrdCommitToPlace(int Fd, const char* DestPath) {
    char Name[LQ_MAX_PATH];
	unsigned i;
	LqString TempDestName;
	static const char* Hidden = LQ_RECIVE_PARTIAL_MASK;
	size_t SepPos;

    if(LqFileGetPath(Fd, Name, LQ_MAX_PATH - 1) == -1) {
        LqFileClose(Fd);
        return -1;
    }
    LqFileClose(Fd);
    int Res = 0;
    if(LqFileGetStat(DestPath, LqDfltPtr()) == 0)
        Res = 1;
    if(LqFileMove(Name, DestPath) != 0) {
        if(lq_errno == EACCES) {
            /*If file busy by another thread or programm*/
            i = 0;
            for(; i < 3; i++) {
                TempDestName = DestPath;
                SepPos = TempDestName.find_last_of(LQ_PATH_SEPARATOR);
                TempDestName.insert(SepPos + 1, Hidden);
                MakeTempName((char*)TempDestName.c_str());
                if(LqFileMove(DestPath, TempDestName.c_str()) == 0)
                    break;
            }
            if(i > 3) {
                lq_errno_set(EACCES);
                return -1;
            }
            LqFileRemove(TempDestName.c_str());
            if(LqFileMove(Name, DestPath) != 0) {
                lq_errno_set(EACCES);
                return -1;
            }
            return Res;
        }
        return -1;
    }
    return Res;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdGetNameTemp(int Fd, char* DestBuf, size_t DestBufLen) {
    return LqFileGetPath(Fd, DestBuf, DestBufLen);
}

LQ_EXTERN_C int LQ_CALL LqFileTrdGetNameTarget(int Fd, char* DestBuf, size_t DestBufLen) {
    char Name[LQ_MAX_PATH];
	LqString DestPath = Name;
	size_t SepPos;

    if(LqFileGetPath(Fd, Name, LQ_MAX_PATH - 1) == -1)
        return -1;
    DestPath = Name;
    SepPos = DestPath.find_last_of(LQ_PATH_SEPARATOR);
    if(LqStrSameMax(&DestPath[SepPos + 1], ".partial_", sizeof(".partial_") - 1))
        DestPath.erase(SepPos + 1, sizeof(LQ_RECIVE_PARTIAL_MASK) - 1);
    LqStrCopyMax(DestBuf, DestPath.c_str(), DestBufLen);
    return 0;
}


#if defined(LQPLATFORM_WINDOWS)

LQ_EXTERN_C int LQ_CALL LqFileTrdCancel(int Fd) {
    wchar_t Name[LQ_MAX_PATH];
    if(GetFinalPathNameByHandleW((HANDLE)Fd, Name, LQ_MAX_PATH, 0) == 0) {
        LqFileClose(Fd);
        return -1;
    }
    LqFileClose(Fd);
    return (DeleteFileW(Name) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdCommit(int Fd) {
	int Res = 0, i;
	WIN32_FILE_ATTRIBUTE_DATA info;
	size_t SepPos;
    std::basic_string<wchar_t> Path, DestPath, TempDestName;
	static const wchar_t* Hidden = LQ_RECIVE_PARTIAL_MASK_WCHAR;

    {
        wchar_t Name[LQ_MAX_PATH];
        if(GetFinalPathNameByHandleW((HANDLE)Fd, Name, LQ_MAX_PATH, 0) == 0) {
            LqFileClose(Fd);
            return -1;
        }
        Path = Name;
    }

    DestPath = Path;
    SepPos = DestPath.find_last_of(LQ_PATH_SEPARATOR);
    DestPath.erase(SepPos + 1, sizeof(LQ_RECIVE_PARTIAL_MASK) - 1);
    Res = 0;
    LqFileClose(Fd);
    if(GetFileAttributesExW(DestPath.c_str(), GetFileExInfoStandard, &info) == TRUE)
        Res = 1;
    if(MoveFileExW(Path.c_str(), DestPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE) {
        if(GetLastError() == ERROR_ACCESS_DENIED) {
            /* If file busy by another thread or programm */
            i = 0;
            for(; i < 3; i++) {
                TempDestName = DestPath;
                SepPos = TempDestName.find_last_of(LQ_PATH_SEPARATOR);
                TempDestName.insert(SepPos + 1, Hidden);
                MakeTempName((wchar_t*)TempDestName.c_str());
                if(MoveFileExW(DestPath.c_str(), TempDestName.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == TRUE)
                    break;
            }
            if(i > 3) {
                lq_errno_set(EACCES);
                return -1;
            }
            DeleteFileW(TempDestName.c_str());
            if(MoveFileExW(Path.c_str(), DestPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE) {
                lq_errno_set(EACCES);
                return -1;
            }
            return Res;
        }
        return -1;
    }
    return Res;
}


#else

LQ_EXTERN_C int LQ_CALL LqFileTrdCancel(int Fd) {
    char Name[LQ_MAX_PATH];
    if(LqFileGetPath(Fd, Name, sizeof(Name)) == -1) {
        LqFileClose(Fd);
        return -1;
    }
    LqFileClose(Fd);
    return (LqFileRemove(Name) == 0) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdCommit(int Fd) {
	LqString TempDestName,Path, DestPath;
	int Res, i;
	size_t SepPos;
	static const char* Hidden = LQ_RECIVE_PARTIAL_MASK;

    {
        char Name[LQ_MAX_PATH];
        if(LqFileGetPath(Fd, Name, sizeof(Name)) == -1) {
            LqFileClose(Fd);
            return -1;
        }
        Path = Name;
    }

    DestPath = Path;
    SepPos = DestPath.find_last_of(LQ_PATH_SEPARATOR);
    DestPath.erase(SepPos + 1, sizeof(LQ_RECIVE_PARTIAL_MASK) - 1);
    Res = 0;
    LqFileClose(Fd);
    if(LqFileGetStat(DestPath.c_str(), LqDfltPtr()) == 0)
        Res = 1;
    if(LqFileMove(Path.c_str(), DestPath.c_str()) != 0) {
        if(lq_errno == EACCES) {
            //If file busy by another thread or programm
            i = 0;
            for(; i < 3; i++) {
                TempDestName = DestPath;
                SepPos = TempDestName.find_last_of(LQ_PATH_SEPARATOR);
                TempDestName.insert(SepPos + 1, Hidden);
                MakeTempName((char*)TempDestName.c_str());
                if(LqFileMove(DestPath.c_str(), TempDestName.c_str()) == 0)
                    break;
            }
            if(i > 3) {
                lq_errno_set(EACCES);
                return -1;
            }
            LqFileRemove(TempDestName.c_str());
            if(LqFileMove(Path.c_str(), DestPath.c_str()) != 0) {
                lq_errno_set(EACCES);
                return -1;
            }
            return Res;
        }
        return -1;
    }
    return Res;
}

#endif


