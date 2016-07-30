/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqDirEnm... - Enumerate files in directory.
*/


#include "LqDirEnm.h"
#include "LqCp.h"
#include "LqFile.h"

#if defined(LQPLATFORM_WINDOWS)
# include <Windows.h>

int _LqFileConvertNameToWcs(const char* Name, wchar_t* DestBuf, size_t DestBufSize);

LQ_EXTERN_C int LQ_CALL LqDirEnmStart(LqDirEnm* Enm, const char* Dir, char* DestName, size_t NameLen, uint8_t* Type)
{
    wchar_t DirName[LQ_MAX_PATH];
    WIN32_FIND_DATAW Fdata = {0};
    auto l = _LqFileConvertNameToWcs(Dir, DirName, LQ_MAX_PATH - 4);
    if(l < 0)
	return -1;

    if(DirName[l - 2] != L'*')
    {
	if(DirName[l - 2] != L'\\')
	{
	    DirName[l - 1] = L'\\';
	    l++;
	}
	DirName[l - 1] = L'*';
	DirName[l] = L'\0';
    }
    auto Hndl = FindFirstFileW(DirName, &Fdata);
    if(Hndl == INVALID_HANDLE_VALUE)
	return -1;
    Enm->Hndl = (uintptr_t)Hndl;
    LqCpConvertFromWcs(Fdata.cFileName, DestName, NameLen);
    if(Type != nullptr)
    {
	if(Fdata.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ARCHIVE))
	    *Type = LQ_F_REG;
	else if(Fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    *Type = LQ_F_DIR;
	else
	    *Type = LQ_F_OTHER;
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqDirEnmNext(LqDirEnm* Enm, char* DestName, size_t NameLen, uint8_t* Type)
{
    WIN32_FIND_DATAW Fdata = {0};
    if(FindNextFileW((HANDLE)Enm->Hndl, &Fdata) == FALSE)
    {
	FindClose((HANDLE)Enm->Hndl);
	Enm->Hndl = (uintptr_t)INVALID_HANDLE_VALUE;
	return -1;
    }
    LqCpConvertFromWcs(Fdata.cFileName, DestName, NameLen);
    if(Type != nullptr)
    {
	if(Fdata.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ARCHIVE))
	    *Type = LQ_F_REG;
	else if(Fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    *Type = LQ_F_DIR;
	else
	    *Type = LQ_F_OTHER;
    }
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqDirEnmBreak(LqDirEnm* Enm)
{
    if(Enm->Hndl != (uintptr_t)INVALID_HANDLE_VALUE)
	FindClose((HANDLE)Enm->Hndl);
}

#else

# include <dirent.h>
# include "LqStr.h"

LQ_EXTERN_C int LQ_CALL LqDirEnmStart(LqDirEnm* Enm, const char* Dir, char* DestName, size_t NameLen, uint8_t* Type)
{
    auto Hndl = opendir(Dir);
    if(Hndl == nullptr)
	return -1;

    auto Entry = readdir(Hndl);
    if(Entry == nullptr)
    {
	closedir(Hndl);
	Enm->Hndl = 0;
	return -1;
    }
    Enm->Hndl = (uintptr_t)Hndl;
    LqStrCopyMax(DestName, Entry->d_name, NameLen);
#if !defined(_DIRENT_HAVE_D_TYPE)
    Enm->Internal = LqStrDuplicate(Dir);
#endif
    if(Type != nullptr)
    {
#if defined(_DIRENT_HAVE_D_TYPE)
	if(Entry->d_type == DT_DIR)
	    *Type = LQ_F_DIR;
	else if(Entry->d_type == DT_REG)
	    *Type = LQ_F_REG;
	else
	    *Type = LQ_F_OTHER;
#else
	LqFileStat Stat;
        LqString FullPath = Enm->Internal;
        if(FullPath[FullPath.length() - 1] != '/')
             FullPath += "/";
        FullPath += Entry->d_name;
	if(LqFileGetStat(FullPath.c_str(), &Stat) == -1)
	{
	    closedir(Hndl);
	    free(Enm->Internal);
	    Enm->Hndl = 0;
	    return -1;
	}
	*Type = Stat.Type;
#endif
    }
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqDirEnmNext(LqDirEnm* Enm, char* DestName, size_t NameLen, uint8_t* Type)
{
    auto Entry = readdir((DIR*)Enm->Hndl);
    if(Entry == nullptr)
    {
	closedir((DIR*)Enm->Hndl);
	Enm->Hndl = 0;
#if !defined(_DIRENT_HAVE_D_TYPE)
	free(Enm->Internal);
#endif
	return -1;
    }
    LqStrCopyMax(DestName, Entry->d_name, NameLen);
    if(Type != nullptr)
    {
#if defined(_DIRENT_HAVE_D_TYPE)
	if(Entry->d_type == DT_DIR)
	    *Type = LQ_F_DIR;
	else if(Entry->d_type == DT_REG)
	    *Type = LQ_F_REG;
	else
	    *Type = LQ_F_OTHER;
#else
	LqFileStat Stat;
        LqString FullPath = Enm->Internal;
        if(FullPath[FullPath.length() - 1] != '/')
             FullPath += "/";
        FullPath += Entry->d_name;
	if(LqFileGetStat(FullPath.c_str(), &Stat) == -1)
	{
	    closedir((DIR*)Enm->Hndl);
	    free(Enm->Internal);
	    Enm->Hndl = 0;
	    return -1;
	}
	*Type = Stat.Type;
#endif
    }
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqDirEnmBreak(LqDirEnm* Enm)
{
    if(Enm->Hndl != 0)
    {
	closedir((DIR*)Enm->Hndl);
#if !defined(_DIRENT_HAVE_D_TYPE)
	free(Enm->Internal);
#endif
    }
}
#endif




