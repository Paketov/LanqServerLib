/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFile... - File layer between os and server.
*/

#include "LqOs.h"
#include "LqFile.h"
#include "LqStr.h"

#include <string.h>

#if defined(_MSC_VER)

#include <Windows.h>
#include "LqCp.h"


static int LqFileOpenFlagsToCreateFileFlags(int openFlags)
{
	switch (openFlags & (LQ_O_CREATE | LQ_O_TRUNC | LQ_O_EXCL))
	{
		case 0:
		case LQ_O_EXCL:
			return OPEN_EXISTING;
		case LQ_O_CREATE:
			return OPEN_ALWAYS;
		case LQ_O_TRUNC:
		case LQ_O_TRUNC | LQ_O_EXCL:
			return TRUNCATE_EXISTING;
		case LQ_O_CREATE | LQ_O_TRUNC:
			return CREATE_ALWAYS;
		case LQ_O_CREATE | LQ_O_EXCL:
		case LQ_O_CREATE | LQ_O_TRUNC | LQ_O_EXCL:
			return CREATE_NEW;
	}
	return 0;
}

static void LqFileConvertNameToWcs(const char* Name, wchar_t* DestBuf, size_t DestBufSize)
{
	if((Name[0] != '\\') || (Name[1] != '\\') || (Name[2] != '?') || (Name[3] != '\\'))
	{
		memcpy(DestBuf, L"\\\\?\\", sizeof(L"\\\\?\\"));
		DestBufSize -= 4;
		LqCpConvertToWcs(Name, DestBuf + 4, DestBufSize);
	} else
	{
		LqCpConvertToWcs(Name, DestBuf, DestBufSize);
	}
}

LQ_EXTERN_C int LQ_CALL LqFileOpen(const char *FileName, uint32_t Flags, int Access)
{
	//int			fd;
	HANDLE		h;
	wchar_t Name[LQ_MAX_PATH];
	LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);

	if ((h = CreateFileW
			(
				Name,
				(Flags & LQ_O_RDWR) ? (GENERIC_WRITE | GENERIC_READ) :
				((Flags & LQ_O_WR) ? GENERIC_WRITE : GENERIC_READ),
				(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
				NULL,
				LqFileOpenFlagsToCreateFileFlags(Flags),
				FILE_ATTRIBUTE_NORMAL |
				((Flags & LQ_O_RND) ? FILE_FLAG_RANDOM_ACCESS : 0) |
				((Flags & LQ_O_SEQ) ? FILE_FLAG_SEQUENTIAL_SCAN : 0) |
				((Flags & LQ_O_SHORT_LIVED /*_O_SHORT_LIVED*/) ? FILE_ATTRIBUTE_TEMPORARY : 0) |
				((Flags & LQ_O_TMP) ? FILE_FLAG_DELETE_ON_CLOSE : 0) |
				((Flags & LQ_O_DSYNC) ? FILE_FLAG_WRITE_THROUGH : 0),
				NULL
			)
		) == INVALID_HANDLE_VALUE
		)
	{
		switch (GetLastError())
		{

			case ERROR_PATH_NOT_FOUND:
			case ERROR_FILE_NOT_FOUND:
				errno = ENOENT;
				break;
			case ERROR_FILE_EXISTS:
				errno = EEXIST;
				break;
			case ERROR_ACCESS_DENIED:
				errno = EACCES;
				break;
			default:
				errno = EINVAL;
		}
		return -1;
	}
	//if(
	//	((fd = _open_osfhandle((long)h, (Flags & LQ_O_APND) ? O_APPEND : 0)) < 0) ||
	//	(Flags & (LQ_O_TXT | LQ_O_BIN) && (_setmode(fd, ((Flags & LQ_O_TXT) ? O_TEXT : 0) | ((Flags & LQ_O_BIN) ? O_BINARY : 0)) < 0))
	//  ) CloseHandle(h);
	return (int)h;
}


LQ_EXTERN_C int LQ_CALL LqFileGetPath(int Fd, char* DestBuf, unsigned int SizeBuf)
{
	wchar_t Name[LQ_MAX_PATH];
	if(GetFinalPathNameByHandleW((HANDLE)Fd, Name, LQ_MAX_PATH, 0) == 0)
		return -1;
	LqCpConvertFromWcs(Name, DestBuf, SizeBuf);
	return 0;
}

static inline LqTimeSec FileTimeToTimeSec(const FILETIME* ft)
{
	ULARGE_INTEGER ull;
	ull.LowPart = ft->dwLowDateTime;
	ull.HighPart = ft->dwHighDateTime;
	return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

LQ_EXTERN_C int LQ_CALL LqFileGetStat(const char* FileName, LqFileStat* StatDest)
{

	WIN32_FILE_ATTRIBUTE_DATA   info;
	wchar_t Name[LQ_MAX_PATH];
	LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);
	if(GetFileAttributesExW(Name, GetFileExInfoStandard, &info) == FALSE)
		return -1;
	StatDest->CreateTime = FileTimeToTimeSec(&info.ftCreationTime);
	StatDest->AccessTime = FileTimeToTimeSec(&info.ftLastAccessTime);
    StatDest->ModifTime = FileTimeToTimeSec(&info.ftLastWriteTime);
	StatDest->RefCount = 0;
	StatDest->Size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
	StatDest->DevId = 0;
	StatDest->Gid = 0;
	StatDest->Uid = 0;
	StatDest->Id = 0;
	if(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		StatDest->Type = LQ_F_REG;
	else if(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		StatDest->Type = LQ_F_DIR;
	else
		StatDest->Type = LQ_F_REG;

	if(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
		StatDest->Access = 0444;
	else
		StatDest->Access = 0666;
	return 0;
}

LQ_EXTERN_C int LQ_CALL LqFileGetStatByFd(int Fd, LqFileStat* StatDest)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle((HANDLE)Fd, &info) != TRUE)
		return -1;

	//StatDest->RefCount = info.nNumberOfLinks;
	//StatDest->DevId = info.dwVolumeSerialNumber;
	//{
	//	FILE_ID_INFO fid = {0};
	//	GetFileInformationByHandleEx((HANDLE)Fd, FileIdInfo, &fid, sizeof(fid));
	//	unsigned int h = 0;
	//	for(int i = 0; i < 16; i++) h = 31 * h + fid.FileId.Identifier[i];
	//	StatDest->Id = h;
	//}

	StatDest->CreateTime = FileTimeToTimeSec(&info.ftCreationTime);
	StatDest->AccessTime = FileTimeToTimeSec(&info.ftLastAccessTime);
    StatDest->ModifTime = FileTimeToTimeSec(&info.ftLastWriteTime);
	StatDest->RefCount = 0;
	StatDest->Size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
	StatDest->DevId = 0;
	StatDest->Gid = 0;
	StatDest->Uid = 0;
	StatDest->Id = 0;
	if(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		StatDest->Type = LQ_F_REG;
	else if(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		StatDest->Type = LQ_F_DIR;
	else
		StatDest->Type = LQ_F_REG;

	if(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
		StatDest->Access = 0444;
	else
		StatDest->Access = 0666;
	return 0;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFileTell(int Fd)
{
	LARGE_INTEGER CurPos = {0}, NewPos = {0};
	if(SetFilePointerEx((HANDLE)Fd, NewPos, &CurPos, FILE_CURRENT) == FALSE)
		return -1;
	return CurPos.QuadPart;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFileSeek(int Fd, LqFileSz Offset, int Flag)
{
	LARGE_INTEGER NewPos;
	DWORD MoveMethod;
	switch(Flag)
	{
		case LQ_SEEK_END: MoveMethod = FILE_END; break;
		case LQ_SEEK_SET: MoveMethod = FILE_BEGIN; break;
		default:
		case LQ_SEEK_CUR: MoveMethod = FILE_CURRENT; break;
	}
	NewPos.QuadPart = Offset;
	if(SetFilePointerEx((HANDLE)Fd, NewPos, &NewPos, MoveMethod) != TRUE)
		return -1;
	return NewPos.QuadPart;
}

LQ_EXTERN_C int LQ_CALL LqFileClose(int Fd)
{
	return (CloseHandle((HANDLE)Fd) == TRUE) ? 0 : -1;
}

LQ_EXTERN_C int LQ_CALL LqFileEof(int Fd)
{
	LARGE_INTEGER li = {0};
	if(GetFileSizeEx((HANDLE)Fd, &li) == FALSE)
		return -1;
	return LqFileTell(Fd) >= li.QuadPart;
}

LQ_EXTERN_C int LQ_CALL LqFileRead(int Fd, void* DestBuf, unsigned int SizeBuf)
{
	DWORD Readed;
	if(ReadFile((HANDLE)Fd, DestBuf, SizeBuf, &Readed, nullptr) != TRUE)
		return -1;
	return Readed;
}

LQ_EXTERN_C int LQ_CALL LqFileWrite(int Fd, const void* SourceBuf, unsigned int SizeBuf)
{
	DWORD Written;
	if(WriteFile((HANDLE)Fd, SourceBuf, SizeBuf, &Written, nullptr) != TRUE)
		return -1;
	return Written;
}

LQ_EXTERN_C int LQ_CALL LqFileMakeDir(const char* NewDirName, int Access)
{
	wchar_t Name[LQ_MAX_PATH];
	LqFileConvertNameToWcs(NewDirName, Name, LQ_MAX_PATH);
	return CreateDirectoryW(Name, nullptr) == TRUE;
}

LQ_EXTERN_C int LQ_CALL LqFileRemoveDir(const char* NewDirName)
{
	wchar_t Name[LQ_MAX_PATH];
	LqFileConvertNameToWcs(NewDirName, Name, LQ_MAX_PATH);
	return RemoveDirectoryW(Name) == TRUE;
}

LQ_EXTERN_C int LQ_CALL LqFileMove(const char* OldName, const char* NewName)
{
	wchar_t Old[LQ_MAX_PATH];
	wchar_t New[LQ_MAX_PATH];
	LqFileConvertNameToWcs(OldName, Old, LQ_MAX_PATH);
	LqFileConvertNameToWcs(NewName, New, LQ_MAX_PATH);
	return (MoveFileW(Old, New) == TRUE)? 0: -1;
}

LQ_EXTERN_C int LQ_CALL LqFileRemove(const char* FileName)
{
	wchar_t Name[LQ_MAX_PATH];
	LqFileConvertNameToWcs(FileName, Name, LQ_MAX_PATH);
	return (DeleteFileW(Name) == TRUE)? 0: -1;
}

#else

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>


#ifndef O_SHORT_LIVED
#define O_SHORT_LIVED 0
#endif

#ifndef O_DSYNC
#define O_DSYNC 0
#endif

#ifndef O_TEMPORARY
#define O_TEMPORARY 0
#endif // O_TEMPORARY

#ifndef O_BINARY
#define O_BINARY 0
#endif // O_TEMPORARY

#ifndef O_TEXT
#define O_TEXT 0
#endif // O_TEXT

#ifndef O_NOINHERIT
#define O_NOINHERIT 0
#endif

#ifndef O_SEQUENTIAL
#define O_SEQUENTIAL 0
#endif

#ifndef O_RANDOM
#define O_RANDOM 0
#endif

#ifndef S_IFIFO
#define S_IFIFO 0xffff
#endif

LQ_EXTERN_C int LQ_CALL LqFileGetPath(int Fd, char* DestBuf, unsigned int SizeBuf)
{
	char PathToFd[64];
	sprintf(PathToFd, "/proc/%i/fd/%i", (int)getpid(), Fd);
	if(readlink(PathToFd, DestBuf, SizeBuf) == -1)
		return -1;
	return 0;
}


LQ_EXTERN_C int LQ_CALL LqFileOpen(const char *FileName, uint32_t Flags, int Access)
{
	int DecodedFlags =
		((Flags & LQ_O_RD) ? O_RDONLY : 0) |
		((Flags & LQ_O_WR) ? O_WRONLY : 0) |
		((Flags & LQ_O_RDWR) ? O_RDWR : 0) |
		((Flags & LQ_O_CREATE) ? O_CREAT : 0) |
		((Flags & LQ_O_TMP) ? O_TEMPORARY : 0) |
		((Flags & LQ_O_BIN) ? O_BINARY : 0) |
		((Flags & LQ_O_TXT) ? O_TEXT : 0) |
		((Flags & LQ_O_APND) ? O_APPEND : 0) |
		((Flags & LQ_O_TRUNC) ? O_TRUNC : 0) |
		((Flags & LQ_O_NOINHERIT) ? O_NOINHERIT : 0) |
		((Flags & LQ_O_SEQ) ? O_SEQUENTIAL : 0) |
		((Flags & LQ_O_RND) ? O_RANDOM : 0) |
		((Flags & LQ_O_EXCL) ? O_EXCL : 0) |
		((Flags & LQ_O_DSYNC) ? O_DSYNC : 0) |
		((Flags & LQ_O_SHORT_LIVED) ? O_SHORT_LIVED : 0);
	return open(FileName, DecodedFlags, Access);
}

LQ_EXTERN_C int LQ_CALL LqFileGetStat(const char* FileName, LqFileStat* StatDest)
{
	struct stat64 s;
	if(stat64(FileName, &s) == -1)
		return -1;

	StatDest->Size = s.st_size;
	StatDest->CreateTime = s.st_ctime;
	StatDest->AccessTime = s.st_atime;
	StatDest->ModifTime = s.st_mtime;
	StatDest->Gid = s.st_gid;
	StatDest->Uid = s.st_uid;
	StatDest->Id = s.st_ino;
	StatDest->DevId = s.st_dev;
	StatDest->RefCount = s.st_nlink;
	StatDest->Access = s.st_mode & 0777;
	if(S_ISDIR(s.st_mode))
        StatDest->Type = LQ_F_DIR;
    else if(S_ISREG(s.st_mode))
        StatDest->Type = LQ_F_REG;
    else
        StatDest->Type = LQ_F_OTHER;
	return 0;
}


LQ_EXTERN_C int LQ_CALL LqFileGetStatByFd(int Fd, LqFileStat* StatDest)
{
	struct stat64 s;
	if(fstat64(Fd, &s) == -1)
		return -1;
	StatDest->Size = s.st_size;
	StatDest->CreateTime = s.st_ctime;
	StatDest->AccessTime = s.st_atime;
	StatDest->ModifTime = s.st_mtime;
	StatDest->Gid = s.st_gid;
	StatDest->Uid = s.st_uid;
	StatDest->Id = s.st_ino;
	StatDest->DevId = s.st_dev;
	StatDest->RefCount = s.st_nlink;
	StatDest->Access = s.st_mode & 0777;
	StatDest->Type = S_IFREG;
	if(S_ISDIR(s.st_mode))
        StatDest->Type = LQ_F_DIR;
    else if(S_ISREG(s.st_mode))
        StatDest->Type = LQ_F_REG;
    else
        StatDest->Type = LQ_F_OTHER;
	return 0;
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFileTell(int Fd)
{
    return lseek64(Fd, 0, SEEK_CUR);
}

LQ_EXTERN_C LqFileSz LQ_CALL LqFileSeek(int Fd, LqFileSz Offset, int Flag)
{
	return lseek64(Fd, Offset, Flag);
}

LQ_EXTERN_C int LQ_CALL LqFileMakeDir(const char* NewDirName, int Access)
{
	return mkdir(NewDirName, Access);
}

LQ_EXTERN_C int LQ_CALL LqFileRemoveDir(const char* NewDirName)
{
	return rmdir(NewDirName);
}

LQ_EXTERN_C int LQ_CALL LqFileMove(const char* OldName, const char* NewName)
{
	return rename(OldName, NewName);
}

LQ_EXTERN_C int LQ_CALL LqFileRemove(const char* FileName)
{
	return unlink(FileName);
}

LQ_EXTERN_C int LQ_CALL LqFileClose(int Fd)
{
	return close(Fd);
}

LQ_EXTERN_C int LQ_CALL LqFileEof(int Fd)
{
	LqFileStat Fs = {0};
	if(LqFileGetStatByFd(Fd, &Fs) == -1)
		return -1;
	return LqFileTell(Fd) >= Fs.Size;
}

LQ_EXTERN_C int LQ_CALL LqFileRead(int Fd, void* DestBuf, unsigned int SizeBuf)
{
	return read(Fd, DestBuf, SizeBuf);
}

LQ_EXTERN_C int LQ_CALL LqFileWrite(int Fd, const void* SourceBuf, unsigned int SizeBuf)
{
	return write(Fd, SourceBuf, SizeBuf);
}

#endif




LQ_EXTERN_C int LQ_CALL LqFileMakeSubdirs(const char* NewSubdirsDirName, int Access)
{
	size_t DirPos = 0;
	char c;
	char Name[LQ_MAX_PATH];
	char* Sep = Name;
	LqFileStat s;
	LqStrCopyMax(Name, NewSubdirsDirName, sizeof(Name));
	int RetStat = 1;
	while(true)
	{
		if((Sep = strchr(Sep, LQHTTPPTH_SEPARATOR)) == nullptr)
			break;
		Sep++;
		c = *Sep;
		*Sep = '\0';
		s.Type = 0;
		if(LqFileGetStat(Name, &s) == 0)
		{
			*Sep = c;
			if(s.Type == LQ_F_DIR)
				continue;
			return -1;
		} else
		{
			if(!LqFileMakeDir(Name, Access))
			{
				*Sep = c;
				return -1;
			}
			RetStat = 0;
			*Sep = c;
		}
	}
	return RetStat;
}
