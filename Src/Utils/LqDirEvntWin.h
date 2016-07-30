
#ifndef LQ_DIREVNT
#error "Only use in LqDirEvnt.cpp !"
#endif

#include <Windows.h>
#include <vector>

#include "LqDirEvnt.h"
#include "LqCp.h"
#include "LqFile.h"
#include "LqStr.h"

#include "LqAlloc.hpp"


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

struct FollowData
{
    HANDLE					DirHandle;
    OVERLAPPED					Ovlp;
    std::vector<char>				Buffer;
    DWORD					BytesReturned;
    LqString					DirName;
    DWORD					NotifyFilter;
    bool					IsSubtree;
    FollowData::~FollowData()
    {
	if(DirHandle != INVALID_HANDLE_VALUE)
	{
	    CancelIo(DirHandle);
	    CloseHandle(DirHandle);
	}
	if(Ovlp.hEvent != INVALID_HANDLE_VALUE)
	    CloseHandle(Ovlp.hEvent);
    }

    FollowData::FollowData(const char* DirName, HANDLE NewDirHandle, HANDLE EvntHandle, bool IsWatchSubtree, DWORD NewNotifyFilter, size_t BufferSize):
	DirName(DirName),
	DirHandle(NewDirHandle),
	IsSubtree(IsWatchSubtree),
	NotifyFilter(NewNotifyFilter),
	Buffer(BufferSize)
    {
	memset(&Ovlp, 0, sizeof(Ovlp));
	Ovlp.hEvent = EvntHandle;
	BytesReturned = 0;
    }
};

#pragma pack(pop)


LQ_EXTERN_C int LQ_CALL LqDirEvntAdd(LqDirEvnt* Evnt, const char* Name, uint8_t FollowFlag)
{
    for(unsigned i = 0; i < Evnt->Count; i++)
	if(LqStrUtf8CmpCase(((FollowData*)Evnt->Data)[i].DirName.c_str(), Name))
	    return -1;
    wchar_t Wdir[LQ_MAX_PATH];
    LqCpConvertToWcs(Name, Wdir, LQ_MAX_PATH - 1);
    DWORD NotifyFilter = 0;
    if(FollowFlag & LQDIREVNT_ADDED)
	NotifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
    if(FollowFlag & LQDIREVNT_MOD)
	NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
    if(FollowFlag & (LQDIREVNT_MOVE_TO | LQDIREVNT_MOVE_FROM))
	NotifyFilter |= (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
    if(FollowFlag & LQDIREVNT_RM)
	NotifyFilter |= (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);


    HANDLE DirHandle = CreateFileW(Wdir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if(DirHandle == INVALID_HANDLE_VALUE)
	return false;
    HANDLE EventHandle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(EventHandle == INVALID_HANDLE_VALUE)
    {
	CloseHandle(DirHandle);
	return -1;
    }
    auto Val = new FollowData(Name, DirHandle, EventHandle, FollowFlag & LQDIREVNT_SUBTREE, NotifyFilter, 2048);
    Evnt->Data = realloc(Evnt->Data, sizeof(FollowData*) * (Evnt->Count + 1));
    ((FollowData**)Evnt->Data)[Evnt->Count] = Val;
    Evnt->Count++;
    auto Res = ReadDirectoryChangesW
    (
	DirHandle,
	Val->Buffer.data(),
	Val->Buffer.size(),
	Val->IsSubtree,
	Val->NotifyFilter,
	&Val->BytesReturned,
	&Val->Ovlp,
	nullptr
    );

    return 0;
}

LQ_EXTERN_C int LQ_CALL LqDirEvntInit(LqDirEvnt * Evnt)
{
    Evnt->Count = 0;
    Evnt->Data = nullptr;
    return 0;
}

LQ_EXTERN_C void LqDirEvntUninit(LqDirEvnt * Evnt)
{
    for(int k = 0; k < Evnt->Count; k++)
	delete ((FollowData**)Evnt->Data)[k];
    if(Evnt->Data != nullptr)
	free(Evnt->Data);
}

LQ_EXTERN_C void LQ_CALL LqDirEvntPathFree(LqDirEvntPath ** Dest)
{
    for(auto i = *Dest; i != nullptr; )
    {
	auto j = i;
	i = i->Next;
	free(j);
    }
    *Dest = nullptr;
}

LQ_EXTERN_C int LQ_CALL LqDirEvntRm(LqDirEvnt* Evnt, const char* Name)
{
    for(int k = 0; k < Evnt->Count; k++)
    {
	auto d = ((FollowData**)Evnt->Data)[k];
	if(LqStrUtf8CmpCase(d->DirName.c_str(), Name))
	{
	    delete d;
	    Evnt->Count--;
	    ((FollowData**)Evnt->Data)[k] = ((FollowData**)Evnt->Data)[Evnt->Count];
	    Evnt->Data = realloc(Evnt->Data, Evnt->Count * sizeof(FollowData*));
	    return 0;
	}
    }
    return -1;
}

LQ_EXTERN_C int LQ_CALL LqDirEvntCheck(LqDirEvnt* Evnt, LqDirEvntPath** Dest, LqTimeMillisec WaitTime)
{
    LqDirEvntPathFree(Dest);
    int Count = 0;
    LqDirEvntPath* NewList = *Dest = nullptr;
    bool IsHave = false;
    while(true)
    {
	for(int k = 0; k < Evnt->Count; k++)
	{
	    auto d = ((FollowData**)Evnt->Data)[k];
	    if(HasOverlappedIoCompleted(&d->Ovlp))
	    {
		DWORD Offset = 0;
		char FileName[LQ_MAX_PATH];
		while(true)
		{
		    auto Info = (FILE_NOTIFY_INFORMATION*)(d->Buffer.data() + Offset);
		    uint8_t RetFlag;
		    switch(Info->Action)
		    {
			case FILE_ACTION_ADDED: RetFlag = LQDIREVNT_ADDED; break;
			case FILE_ACTION_REMOVED: RetFlag = LQDIREVNT_RM; break;
			case FILE_ACTION_MODIFIED: RetFlag = LQDIREVNT_MOD; break;
			case FILE_ACTION_RENAMED_OLD_NAME: RetFlag = LQDIREVNT_MOVE_FROM; break;
			case FILE_ACTION_RENAMED_NEW_NAME: RetFlag = LQDIREVNT_MOVE_TO; break;
		    }

		    auto t = Info->FileName[Info->FileNameLength / 2];
		    Info->FileName[Info->FileNameLength / 2] = L'\0';
		    LqCpConvertFromWcs(Info->FileName, FileName, LQ_MAX_PATH - 1);
		    Info->FileName[Info->FileNameLength / 2] = t;

		    size_t NewSize = LqStrLen(FileName) + sizeof(LqDirEvntPath) + d->DirName.length() + 2;
		    auto Val = (LqDirEvntPath*)malloc(NewSize);
		    auto Count = LqStrCopy(Val->Name, d->DirName.c_str());

		    if(Val->Name[0] != 0)
		    {
			char Sep[2] = {LQ_PATH_SEPARATOR, 0};
			if(Val->Name[Count] != LQ_PATH_SEPARATOR)
			    LqStrCat(Val->Name, Sep);
		    }
		    LqStrCat(Val->Name, FileName);
		    Val->Flag = RetFlag;

		    Val->Next = NewList;
		    NewList = Val;
		    Offset += Info->NextEntryOffset;
		    if(Info->NextEntryOffset == 0)
			break;
		}

		ResetEvent(d->Ovlp.hEvent);
		d->Ovlp.Offset = d->Ovlp.OffsetHigh = d->Ovlp.InternalHigh = d->Ovlp.Internal = 0;
		ReadDirectoryChangesW
		(
		    d->DirHandle,
		    d->Buffer.data(),
		    d->Buffer.size(),
		    d->IsSubtree,
		    d->NotifyFilter,
		    &d->BytesReturned,
		    &d->Ovlp,
		    nullptr
		);
		Count++;
		IsHave = true;
	    }
	}
	if((!IsHave) && (WaitTime > 0))
	{
	    std::vector<HANDLE> EventArr;
	    for(int k = 0; k < Evnt->Count; k++)
	    {
		auto d = (FollowData*)Evnt->Data + k;
		EventArr.push_back(d->Ovlp.hEvent);
	    }
	    switch(WaitForMultipleObjects(EventArr.size(), EventArr.data(), FALSE, WaitTime))
	    {
		case WAIT_TIMEOUT:
		    return 0;
		case WAIT_FAILED:
		    return -1;
	    }
	    IsHave = true;
	} else
	{
	    break;
	}
    }
    *Dest = NewList;
    return Count;
}

