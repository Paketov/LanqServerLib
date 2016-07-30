
#ifndef LQ_DIREVNT
#error "Only use in LqDirEvnt.cpp !"
#endif


#include "LqDirEvnt.h"
#include "LqDirEnm.h"
#include "LqStr.h"
#include "LqErr.h"

#include <vector>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <poll.h>


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)


struct Subdir
{
    int						Pwd;
    char*					Name;
};


struct FollowData
{
    char*                                       Name;
    std::vector<Subdir>				Subdirs;
    size_t					Max;
    uint32_t					Mask;
    int						Ifd;
    int						Wd;
    bool					IsSubtree;

    FollowData(int NewIfd, int NewWd, const char* NewName, uint32_t NewFlag, bool NewIsSubtree):
	Ifd(NewIfd),
	Max(-1),
	Mask(NewFlag),
	IsSubtree(NewIsSubtree),
	Wd(NewWd)
    {
        InsertDir(-1, NewName, NewName);
        Name = LqStrDuplicate(NewName);
    }

    ~FollowData()
    {
	if(Name != nullptr)
	    free(Name);
	for(auto& i : Subdirs)
	{
	    if(i.Name != nullptr)
		free(i.Name);
	}
	if(Ifd != -1)
	    close(Ifd);
    }

    int InsertDir(int Pwd, const char* Name)
    {
	LqString FullPath;
	DirByWd(Pwd, FullPath);
	if(FullPath[FullPath.length() - 1] != '/')
	    FullPath += "/";
	FullPath += Name;
	int NewWd;
	if((NewWd = InsertDir(Pwd, Name, FullPath.c_str())) == -1)
	    return -1;
	RcrvDirAdd(Pwd, FullPath.c_str());
	return NewWd;
    }

    int InsertDir(int Pwd, const char* Name, const char* FullName)
    {
        auto MaskEx = Mask;
        if(IsSubtree)
            MaskEx |= (((Pwd == -1)?0:IN_DELETE_SELF) | IN_MOVED_FROM | IN_CREATE);
	auto Wd = inotify_add_watch(Ifd, FullName, MaskEx);
	if(Wd == -1)
	    return -1;
	if(Wd >= Subdirs.size())
	{
	    Subdirs.resize(Wd + 1);
	    for(int i = Max + 1; i <= Wd; i++)
	    {
		Subdirs[i].Pwd = -1;
		Subdirs[i].Name = nullptr;
	    }
	    Max = Wd;
	}
	Subdirs[Wd].Pwd = Pwd;
	Subdirs[Wd].Name = LqStrDuplicate(Name);
	return Wd;
    }

    bool RcrvDirAdd()
    {
	LqString Buf;
	const char* Pth = Name;
	auto l = LqStrLen(Name);
	if(Name[l - 1] != '/')
	{
	    Buf = Name;
	    Buf += "/";
	    Pth = Buf.c_str();
	}
	RcrvDirAdd(Wd, Pth);
	return true;
    }

    void RcrvDirAdd(int Pwd, const char* TargetDir)
    {
	LqDirEnm i;
	char Buf[LQ_MAX_PATH];
	uint8_t Type;
	for(int r = LqDirEnmStart(&i, TargetDir, Buf, sizeof(Buf) - 1, &Type); r != -1; r = LqDirEnmNext(&i, Buf, sizeof(Buf) - 1, &Type))
	{
	    if(LqStrSame("..", Buf) || LqStrSame(".", Buf) || (Type != LQ_F_DIR))
		continue;
	    LqString FullDirName = TargetDir;
	    if((FullDirName.length() <= 0) || (FullDirName[FullDirName.length() - 1] != '/'))
		FullDirName += "/";
	    FullDirName += Buf;
	    int NewWd;
	    if((NewWd = InsertDir(Pwd, Buf, FullDirName.c_str())) == -1)
		continue;
	    RcrvDirAdd(NewWd, FullDirName.c_str());
	}
    }

    void DirByWd(int Wd, LqString& DestPath)
    {
	if(Subdirs[Wd].Pwd == -1)
	{
	    DestPath += Subdirs[Wd].Name;
	    return;
	}
	DirByWd(Subdirs[Wd].Pwd, DestPath);
	DestPath += "/";
	DestPath += Subdirs[Wd].Name;
    }

    void RemoveByWd(int Wd)
    {
	free(Subdirs[Wd].Name);
	Subdirs[Wd].Name = nullptr;
	Subdirs[Wd].Pwd = -1;
	inotify_rm_watch(Ifd, Wd);
	if(Wd == Max)
	{
	    int i;
	    for(i = Subdirs.size() - 1; i >= 0; i--)
	    {
		if(Subdirs[i].Name != nullptr)
		    break;
	    }
	    Max = i;
	    i++;
	    Subdirs.resize(i);
	}
    }

    void RemoveByPwdAndName(int Pwd, const char* Name)
    {
	for(int i = 0; i < Subdirs.size(); i++)
	{
	    if((Subdirs[i].Pwd == Pwd) && LqStrSame(Subdirs[i].Name, Name))
	    {
		RemoveByWd(i);
		return;
	    }
	}
    }
};

#pragma pack(pop)

LQ_EXTERN_C int LQ_CALL LqDirEvntAdd(LqDirEvnt* Evnt, const char* Name, uint8_t FollowFlag)
{
    uint32_t NotifyFilter = 0;
    bool IsWatchSubtree = false;
    if(FollowFlag & LQDIREVNT_ADDED)
	NotifyFilter |= IN_CREATE;
    if(FollowFlag & LQDIREVNT_MOD)
	NotifyFilter |= IN_CLOSE_WRITE;
    if(FollowFlag & LQDIREVNT_MOVE_TO)
	NotifyFilter |= IN_MOVED_TO;
    if(FollowFlag & LQDIREVNT_MOVE_FROM)
	NotifyFilter |= IN_MOVED_FROM;
    if(FollowFlag & LQDIREVNT_RM)
	NotifyFilter |= IN_DELETE;
    int Ifd = inotify_init();
    if(Ifd == -1)
	return -1;
    int nonBlocking = 1;
    fcntl(Ifd, F_SETFL, O_NONBLOCK, nonBlocking);
    int Wd = inotify_add_watch(Ifd, Name, NotifyFilter | ((FollowFlag & LQDIREVNT_SUBTREE) ? IN_CREATE | IN_MOVED_FROM : 0));
    if(Wd == -1)
    {
	close(Ifd);
	return -1;
    }
    auto NewData = new FollowData(Ifd, Wd, Name, NotifyFilter, FollowFlag & LQDIREVNT_SUBTREE);
    Evnt->Data = realloc(Evnt->Data, sizeof(FollowData*) * (Evnt->Count + 1));
    ((FollowData**)Evnt->Data)[Evnt->Count] = NewData;
    Evnt->Count++;
    if(FollowFlag & LQDIREVNT_SUBTREE)
	NewData->RcrvDirAdd();
    return 0;
}

LQ_EXTERN_C int LQ_CALL LqDirEvntInit(LqDirEvnt * Evnt)
{
    Evnt->Count = 0;
    Evnt->Data = nullptr;
    return 0;
}

LQ_EXTERN_C void LQ_CALL LqDirEvntUninit(LqDirEvnt * Evnt)
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
	if(LqStrUtf8CmpCase(d->Name, Name))
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
    char Buf[32768];

    LqDirEvntPath* NewList = *Dest = nullptr;
    int Count = 0;
    bool IsHave = false;
    while(true)
    {
	for(int k = 0; k < Evnt->Count; k++)
	{
	    auto d = ((FollowData**)Evnt->Data)[k];
	    int Readed = read(d->Ifd, Buf, sizeof(Buf));
	    if(Readed <= 0)
		continue;
	    for(int Off = 0; Off < Readed; )
	    {
		auto Info = (struct inotify_event *)(Buf + Off);
		uint8_t Flag = 0;
		if(Info->mask & IN_CREATE)
		{
		    Flag |= (d->Mask & IN_CREATE) ? LQDIREVNT_ADDED : 0;
		    if(d->IsSubtree && (Info->mask & IN_ISDIR))
			d->InsertDir(Info->wd, Info->name);
		}
		if(Info->mask & IN_CLOSE_WRITE)
		    Flag |= LQDIREVNT_MOD;
		if(Info->mask & IN_MOVED_TO)
		    Flag |= LQDIREVNT_MOVE_TO;

		if(Info->mask & IN_MOVED_FROM)
		{
		    Flag |= (d->Mask & IN_MOVED_FROM) ? LQDIREVNT_MOVE_FROM : 0;
		    if(d->IsSubtree && (Info->mask & IN_ISDIR))
			d->RemoveByPwdAndName(Info->wd, Info->name);
		}
		if(Info->mask & IN_DELETE)
		    Flag |= LQDIREVNT_RM;
		if((Info->mask & IN_DELETE_SELF) && d->IsSubtree)
		    d->RemoveByWd(Info->wd);

		if(Flag != 0)
		{
		    LqString FullPath;
		    d->DirByWd(Info->wd, FullPath);
		    if(FullPath[FullPath.length() - 1] != '/')
			FullPath += "/";
		    FullPath += Info->name;

		    size_t NewSize = sizeof(LqDirEvntPath) + FullPath.length() + 2;
		    auto Val = (LqDirEvntPath*)malloc(NewSize);
		    LqStrCopy(Val->Name, FullPath.c_str());
		    Val->Flag = Flag;
		    Val->Next = NewList;
		    NewList = Val;

		    Count++;
		    IsHave = true;
		}
		Off += (sizeof(struct inotify_event) + Info->len);
	    }
	}
	if(!IsHave && (WaitTime > 0) && (lq_errno == EAGAIN))
	{
	    std::vector<pollfd> Plfd;
	    for(int k = 0; k < Evnt->Count; k++)
	    {
		auto d = ((FollowData**)Evnt->Data)[k];
		pollfd fd;
		fd.fd = d->Ifd;
		fd.events = POLLIN;
		fd.revents = 0;
		Plfd.push_back(fd);
	    }
	    auto Ret = poll(Plfd.data(), Plfd.size(), WaitTime);
	    if(Ret == -1)
		return -1;
	    if(Ret == 0)
		return 0;
	    IsHave = true;
	} else
	    break;
    }
    *Dest = NewList;
    return Count;
}


