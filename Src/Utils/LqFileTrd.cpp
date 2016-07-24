/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFileTrd... - (Lanq File Transaction) Implements transactions for correct saving file in os fs.
*/

#include "LqOs.h"
#include "LqErr.h"
#include "LqFile.h"
#include "LqFileTrd.h"
#include "LqTime.h"
#include "LqDfltRef.hpp"
#include "LqStr.h"

#include <string>
#include <string.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#include <io.h>
#include <Windows.h>
#endif


#define LQ_RECIVE_PARTIAL_MASK			".partial_**************_"
#define LQ_RECIVE_PARTIAL_MASK_WCHAR	L".partial_**************_"

#define STR_TYPE(_Type, _Str)		((std::is_same<_Type, wchar_t>::value)?(_Type*)(L ## _Str):(_Type*)(_Str))
#define CHAR_TYPE(_Type, _Char)		((_Type)((std::is_same<_Type, char>::value)?(_Char):(L ## _Char)))

template<typename CharT>
static CharT* MakeTempName(CharT* Res)
{
	static CharT* h = STR_TYPE(CharT, "0123456789abcdef");
	llong TMillisec = LqTimeGetLocMillisec() * rand() + rand();
	CharT* c = Res;
	for(; *c; c++)
	{
		if(*c != CHAR_TYPE(CharT, '*')) continue;
		for(unsigned i = 0; i < sizeof(TMillisec); i++)
		{
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

/*
* Flags must be O_TEXT or O_BINARY
* LQ_TRNSC_REPLACE
* LQ_TRNSC_CREATE_SUBDIRS
*/

LQ_EXTERN_C int LQ_CALL LqFileTrdCreate(const char* FileName, uint32_t Flags, int Access)
{
	LqFileStat Stat;
	if(LqFileGetStat(FileName, &Stat) == 0)
	{
		if(!(LQ_TC_REPLACE & Flags) || (Stat.Type == LQ_F_DIR))
		{
			lq_set_errno(EEXIST);
			return -1;
		}
	}
	std::string LocalFileName = FileName;
	auto SepPos = LocalFileName.find_last_of(LQHTTPPTH_SEPARATOR);
	LocalFileName.insert(SepPos + 1, LQ_RECIVE_PARTIAL_MASK);
	int Fd = -1;
	for(int i = 0; (Fd == -1) && (i < 3); i++)
	{
		std::string Path = LocalFileName;
		Fd = LqFileOpen(MakeTempName((char*)Path.c_str()), LQ_O_CREATE | (Flags & 0xffff), Access);
	}
	if((Fd == -1) && (lq_errno == ENOENT))
	{
		if(Flags & LQ_TC_SUBDIR)
		{
			if(LqFileMakeSubdirs(LocalFileName.c_str(), Access) != 0)
				return -1;

			return LqFileTrdCreate(FileName, Flags, Access);
		} else
		{
			return -1;
		}
	}
	return Fd;
}


/*
* Commit file to another place
*/
LQ_EXTERN_C int LQ_CALL LqFileTrdCommitToPlace(int Fd, const char* DestPath)
{
	char Name[LQ_MAX_PATH];
	if(LqFileGetPath(Fd, Name, sizeof(Name)) == -1)
	{
		LqFileClose(Fd);
		return -1;
	}
	LqFileClose(Fd);
	int Res = 0;
	if(LqFileGetStat(DestPath, LqDfltPtr()) == 0)
		Res = 1;
	if(LqFileMove(Name, DestPath) != 0)
	{
		if(lq_errno == EACCES)
		{
			//If file busy by another thread or programm
			std::basic_string<char> TempDestName;
			unsigned i = 0;
			for(; i < 3; i++)
			{
				TempDestName = DestPath;
				auto SepPos = TempDestName.find_last_of(LQHTTPPTH_SEPARATOR);
				char Hidden[] = LQ_RECIVE_PARTIAL_MASK;
				TempDestName.insert(SepPos + 1, Hidden);
				MakeTempName((char*)TempDestName.c_str());
				if(LqFileMove(DestPath, TempDestName.c_str()) == 0)
					break;
			}
			if(i > 3)
			{
				lq_set_errno(EACCES);
				return -1;
			}
			LqFileRemove(TempDestName.c_str());
			if(LqFileMove(Name, DestPath) != 0)
			{
				lq_set_errno(EACCES);
				return -1;
			}
			return Res;
		}
		return -1;
	}
	return Res;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdGetNameTemp(int Fd, char* DestBuf, size_t DestBufLen)
{
	return LqFileGetPath(Fd, DestBuf, DestBufLen);
}

LQ_EXTERN_C int LQ_CALL LqFileTrdGetNameTarget(int Fd, char* DestBuf, size_t DestBufLen)
{
	char Name[LQ_MAX_PATH];
	if(LqFileGetPath(Fd, Name, sizeof(Name)) == -1)
		return -1;
	std::basic_string<char> DestPath = Name;
	auto SepPos = DestPath.find_last_of(LQHTTPPTH_SEPARATOR);
	if(LqStrSameMax(&DestPath[SepPos + 1], ".partial_", sizeof(".partial_") - 1))
		DestPath.erase(SepPos + 1, sizeof(LQ_RECIVE_PARTIAL_MASK) - 1);
	LqStrCopyMax(DestBuf, DestPath.c_str(), DestBufLen);
	return 0;
}


#if defined(_MSC_VER)

LQ_EXTERN_C int LQ_CALL LqFileTrdCancel(int Fd)
{
	wchar_t Name[LQ_MAX_PATH];
	if(GetFinalPathNameByHandleW((HANDLE)Fd, Name, LQ_MAX_PATH, 0) == 0)
	{
		LqFileClose(Fd);
		return -1;
	}
	LqFileClose(Fd);
	return (DeleteFileW(Name) == TRUE)?0: -1;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdCommit(int Fd)
{
	std::basic_string<wchar_t> Path;
	{
		wchar_t Name[LQ_MAX_PATH];
		if(GetFinalPathNameByHandleW((HANDLE)Fd, Name, LQ_MAX_PATH, 0) == 0)
		{
			LqFileClose(Fd);
			return -1;
		}
		Path = Name;
	}

	std::basic_string<wchar_t> DestPath = Path;
	auto SepPos = DestPath.find_last_of(LQHTTPPTH_SEPARATOR);
	DestPath.erase(SepPos + 1, sizeof(LQ_RECIVE_PARTIAL_MASK) - 1);
	int Res = 0;
	LqFileClose(Fd);
	WIN32_FILE_ATTRIBUTE_DATA   info;
	if(GetFileAttributesExW(DestPath.c_str(), GetFileExInfoStandard, &info) == TRUE)
		Res = 1;
	if(MoveFileExW(Path.c_str(), DestPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE)
	{
		if(GetLastError() == ERROR_ACCESS_DENIED)
		{
			//If file busy by another thread or programm
			std::basic_string<wchar_t> TempDestName;
			unsigned i = 0;
			for(; i < 3; i++)
			{
				TempDestName = DestPath;
				auto SepPos = TempDestName.find_last_of(LQHTTPPTH_SEPARATOR);
				wchar_t Hidden[] = LQ_RECIVE_PARTIAL_MASK_WCHAR;
				TempDestName.insert(SepPos + 1, Hidden);
				MakeTempName((wchar_t*)TempDestName.c_str());
				if(MoveFileExW(DestPath.c_str(), TempDestName.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == TRUE)
					break;
			}
			if(i > 3)
			{
				lq_set_errno(EACCES);
				return -1;
			}
			DeleteFileW(TempDestName.c_str());
			if(MoveFileExW(Path.c_str(), DestPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == FALSE)
			{
				lq_set_errno(EACCES);
				return -1;
			}
			return Res;
		}
		return -1;
	}
	return Res;
}


#else

LQ_EXTERN_C int LQ_CALL LqFileTrdCancel(int Fd)
{
	char Name[LQ_MAX_PATH];
	if(LqFileGetPath(Fd, Name, sizeof(Name)) == -1)
	{
		LqFileClose(Fd);
		return -1;
	}
	LqFileClose(Fd);
	return (LqFileRemove(Name) == 0)? 0: -1;
}

LQ_EXTERN_C int LQ_CALL LqFileTrdCommit(int Fd)
{
	std::basic_string<char> Path;
	{
		char Name[LQ_MAX_PATH];
		if(LqFileGetPath(Fd, Name, sizeof(Name)) == -1)
		{
			LqFileClose(Fd);
			return -1;
		}
		Path = Name;
	}

	std::basic_string<char> DestPath = Path;
	auto SepPos = DestPath.find_last_of(LQHTTPPTH_SEPARATOR);
	DestPath.erase(SepPos + 1, sizeof(LQ_RECIVE_PARTIAL_MASK) - 1);
	int Res = 0;
	LqFileClose(Fd);
	if(LqFileGetStat(DestPath.c_str(), LqDfltPtr()) == 0)
		Res = 1;
	if(LqFileMove(Path.c_str(), DestPath.c_str()) != 0)
	{
		if(lq_errno == EACCES)
		{
			//If file busy by another thread or programm
			std::basic_string<char> TempDestName;
			unsigned i = 0;
			for(; i < 3; i++)
			{
				TempDestName = DestPath;
				auto SepPos = TempDestName.find_last_of(LQHTTPPTH_SEPARATOR);
				char Hidden[] = LQ_RECIVE_PARTIAL_MASK;
				TempDestName.insert(SepPos + 1, Hidden);
				MakeTempName((char*)TempDestName.c_str());
				if(LqFileMove(DestPath.c_str(), TempDestName.c_str()) == 0)
					break;
			}
			if(i > 3)
			{
				lq_set_errno(EACCES);
				return -1;
			}
			LqFileRemove(TempDestName.c_str());
			if(LqFileMove(Path.c_str(), DestPath.c_str()) != 0)
			{
				lq_set_errno(EACCES);
				return -1;
			}
			return Res;
		}
		return -1;
	}
	return Res;
}

#endif


