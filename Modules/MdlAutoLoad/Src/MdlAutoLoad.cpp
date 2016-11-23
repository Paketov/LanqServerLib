/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   MdlAutoLoad... - Auto load/unload modules from spec. dir.
*/

#include "Lanq.h"
#include "LqWrkBoss.hpp"
#include "LqStrSwitch.h"
#include "LqHttpMdl.h"
#include "LqFile.h"
#include "LqDfltRef.hpp"
#include "LqLib.h"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#include <vector>


static void LoadModule(const char* ModuleFileName);

static LqString ReadPath(LqString& Source)
{
    if(Source[0] == '\"')
    {
        auto Off = Source.find('\"', 1);
        if(Off == LqString::npos)
            return "";
        auto ret = Source.substr(1, Off - 1);
        Source.erase(0, Off + 1);
        while((Source[0] == ' ') || ((Source[0] == '\t')))
            Source.erase(0, 1);
        return ret;
    } else
    {
        auto Off = Source.find_first_of("\t\r\n ");
        auto ret = Source.substr(0, Off);
        Source.erase(0, Off);
        while((Source[0] == ' ') || ((Source[0] == '\t')))
            Source.erase(0, 1);
        return ret;
    }
}

static LqString ReadParams(LqString& Source, const char * Params)
{
    if((Source[0] == '-') && strchr(Params, Source[1]))
    {
        LqString Res;
        Source.erase(0, 1);
        while((Source[0] != '\0') && (strchr(Params, Source[0]) != nullptr))
        {
            Res += Source[0];
            Source.erase(0, 1);
        }
        while((Source[0] == ' ') || ((Source[0] == '\t')))
            Source.erase(0, 1);
        return Res;
    }
    return "";
}


class LqMdlPathFollowTask
{
public:
    LqEvntFd                     TimerFd;
    std::vector<LqFilePathEvnt*> Dirs;
    std::vector<LqFilePoll>      DirsPoll;
    LqLocker<uintptr_t>          PathsLocker;
    LqLocker<uintptr_t>          ModuleLocker;
    LqHttpProtoBase*             RegDest;
    struct Module
    {
        LqString        ModuleName;
        uintptr_t       Handle;
    };

    std::vector<Module> Modules;



    static void WorkingMethod(LqEvntFd* Fd, LqEvntFlag RetFlags)
    {
        LqFileTimerSet(Fd->Fd, 3000);

        LqFilePathEvntEnm* Paths = nullptr;
        auto Ft = (LqMdlPathFollowTask*)Fd->UserData;
        Ft->PathsLocker.LockWriteYield();
        int CountEvents = LqFilePollCheck(Ft->DirsPoll.data(), Ft->DirsPoll.size(), 2);
        if(CountEvents <= 0)
        {
            Ft->PathsLocker.UnlockWrite();
            return;
        }

        for(size_t i = 0; i < Ft->DirsPoll.size(); i++)
        {
            if(!(Ft->DirsPoll[i].events & LQEVNT_FLAG_RD))
                continue;
            LqFilePathEvntDoEnum(Ft->Dirs[i], &Paths);
            for(auto j = Paths; j != nullptr; j = j->Next)
            {
                if(j->Flag & (LQDIREVNT_ADDED | LQDIREVNT_MOVE_TO))
                {
                    LoadModule(j->Name);
                } else if(j->Flag & (LQDIREVNT_RM | LQDIREVNT_MOVE_FROM))
                {
                    Ft->ModuleLocker.LockWrite();
                    for(unsigned i = 0; i < Ft->Modules.size(); i++)
                    {
                        if(LqStrSame(Ft->Modules[i].ModuleName.c_str(), j->Name))
                        {
                            if(LqHttpMdlFreeByHandle(Ft->RegDest, Ft->Modules[i].Handle) > -1)
                            {
                                Ft->Modules[i] = Ft->Modules[Ft->Modules.size() - 1];
                                Ft->Modules.pop_back();
                            }
                        }
                    }
                    Ft->ModuleLocker.Unlock();
                }
            }
            LqFilePathEvntFreeEnum(&Paths);
        }
        Ft->PathsLocker.Unlock();
    }
};

static LqHttpMdl Mod;
static LqMdlPathFollowTask Task;

static void LoadModule(const char* ModuleFileName)
{
    uintptr_t Handle;
    Task.ModuleLocker.LockWrite();
    if(LqHttpMdlLoad(Task.RegDest, ModuleFileName, nullptr, &Handle) == LQHTTPMDL_LOAD_OK)
    {
        Task.Modules.push_back(LqMdlPathFollowTask::Module());
        auto& Val = Task.Modules[Task.Modules.size() - 1];
        Val.Handle = Handle;
        Val.ModuleName = ModuleFileName;
    }
    Task.ModuleLocker.UnlockWrite();
}

static void LoadAllModulesFromDir(const char * Dir, bool IsSubdir)
{
    LqFileEnm DirEnm;
    char FileName[32768];
    uint8_t Flag;

    for(auto r = LqFileEnmStart(&DirEnm, Dir, FileName, sizeof(FileName) - 1, &Flag); r != -1; r = LqFileEnmNext(&DirEnm, FileName, sizeof(FileName) - 1, &Flag))
    {
        if(LqStrSame(FileName, ".") || LqStrSame(FileName, ".."))
            continue;
        LqString ModuleFileName = Dir;
        if(ModuleFileName[ModuleFileName.length() - 1] != LQ_PATH_SEPARATOR)
            ModuleFileName.append(1, LQ_PATH_SEPARATOR);
        ModuleFileName += FileName;
        if(Flag == LQ_F_DIR)
        {
            if(IsSubdir)
                LoadAllModulesFromDir(ModuleFileName.c_str(), IsSubdir);
            continue;
        }
        LoadModule(ModuleFileName.c_str());
    }
}

static void UnloadAllModules(const char * DirName)
{
    auto l = LqStrLen(DirName);
    Task.ModuleLocker.LockWrite();
    for(unsigned i = 0; i < Task.Modules.size(); i++)
    {
        if(
            (LQ_PATH_SEPARATOR == '\\') ?
            LqStrUtf8CmpCaseLen(Task.Modules[i].ModuleName.c_str(), DirName, l) :
            LqStrSameMax(Task.Modules[i].ModuleName.c_str(), DirName, l)
            )
        {
            if(LqHttpMdlFreeByHandle(Task.RegDest, Task.Modules[i].Handle) > -1)
            {
                Task.Modules[i] = Task.Modules[Task.Modules.size() - 1];
                Task.Modules.pop_back();
            }
        }
    }
    Task.ModuleLocker.UnlockWrite();
}

static void UnloadAllModules()
{
    Task.ModuleLocker.LockWrite();
    for(unsigned i = 0; i < Task.Modules.size(); i++)
    {
        if(LqHttpMdlFreeByHandle(Task.RegDest, Task.Modules[i].Handle) > -1)
        {
            Task.Modules[i] = Task.Modules[Task.Modules.size() - 1];
            Task.Modules.pop_back();
        }
    }
    Task.ModuleLocker.UnlockWrite();
}

LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{
    LqHttpMdlInit(Reg, &Mod, "MdlAutoLoad", ModuleHandle);

    Mod.ReciveCommandProc =
        [](LqHttpMdl* Mdl, const char* Command, void* Data)
    {
        FILE* OutBuffer = nullptr;
        LqString FullCommand;
        if(Command[0] == '?')
        {
            FullCommand = Command + 1;
            OutBuffer = (FILE*)Data;
        } else
        {
            FullCommand = Command;
        }
        LqString Cmd;
        while((FullCommand[0] != ' ') && (FullCommand[0] != '\0') && (FullCommand[0] != '\t'))
        {
            Cmd.append(1, FullCommand[0]);
            FullCommand.erase(0, 1);
        }
        while((FullCommand[0] == ' ') || (FullCommand[0] == '\t'))
            FullCommand.erase(0, 1);


        LQSTR_SWITCH(Cmd.c_str())
        {
            LQSTR_CASE("mkdir")
            {
                LqString Params = ReadParams(FullCommand, "lr");
                LqString Path = ReadPath(FullCommand);


                auto DirEvent = LqFastAlloc::New<LqFilePathEvnt>();
                if(DirEvent == nullptr)
                {
                    if(OutBuffer)
                        fprintf(OutBuffer, " [MdlAutoLoad] ERROR: path not added (not mem alloc)\n");
                    break;
                }
                auto r = LqFilePathEvntCreate
                (
                    DirEvent,
                    Path.c_str(),
                    LQDIREVNT_MOVE_TO | LQDIREVNT_MOVE_FROM | LQDIREVNT_ADDED | LQDIREVNT_RM | ((Params.find_first_of('r') != LqString::npos) ? LQDIREVNT_SUBTREE : 0)
                );
                if(r == -1)
                {
                    if(OutBuffer)
                        fprintf(OutBuffer, " [MdlAutoLoad] ERROR: path not added\n");
                    break;
                }

                /*Adding new dir event */
                Task.PathsLocker.LockWrite();

                LqFilePoll Pl;
                Pl.fd = DirEvent->Fd;
                Pl.events = LQ_POLLIN;

                Task.DirsPoll.push_back(Pl);
                Task.Dirs.push_back(DirEvent);
                /*  */
                Task.PathsLocker.Unlock();

                if(Params.find_first_of('l') != LqString::npos)
                    LoadAllModulesFromDir(Path.c_str(), Params.find_first_of('r') != LqString::npos);
                if(OutBuffer)
                    fprintf(OutBuffer, " [MdlAutoLoad] OK\n");
            }
            break;
            LQSTR_CASE("rmdir")
            {
                LqString Params = ReadParams(FullCommand, "u");
                LqString Path = ReadPath(FullCommand);
                int CountDeleted = 0;
                Task.PathsLocker.LockWrite();
                for(size_t i = 0; i < Task.Dirs.size(); i++)
                {
                    char DestName[LQ_MAX_PATH];
                    LqFilePathEvntGetName(Task.Dirs[i], DestName, sizeof(DestName) - 1);
                    if(LqStrSame(DestName, Path.c_str()))
                    {
                        LqFilePathEvntFree(Task.Dirs[i]);
                        LqFastAlloc::Delete(Task.Dirs[i]);
                        Task.Dirs[i] = Task.Dirs[Task.Dirs.size() - 1];
                        Task.Dirs.pop_back();
                        Task.DirsPoll[i] = Task.DirsPoll[Task.DirsPoll.size() - 1];
                        Task.DirsPoll.pop_back();
                        CountDeleted++;
                    }
                }
                Task.PathsLocker.Unlock();

                if(CountDeleted > 0)
                {
                    if(Params.find_first_of('u') != LqString::npos)
                        UnloadAllModules(Path.c_str());
                    if(OutBuffer)
                        fprintf(OutBuffer, " [MdlAutoLoad] OK (removed %i count)\n", CountDeleted);
                } else
                {
                    if(OutBuffer)
                        fprintf(OutBuffer, " [MdlAutoLoad] ERROR: not found path\n");
                }
            }
            break;
            LQSTR_CASE("rmmdls")
            {
                UnloadAllModules();
                if(OutBuffer)
                    fprintf(OutBuffer, " [MdlAutoLoad] OK\n");
            }
            break;
            LQSTR_CASE("?")
            LQSTR_CASE("help")
            {
                if(OutBuffer)
                    fprintf
                    (
                    OutBuffer,
                    " [MdlAutoLoad]\n"
                    " Module: MdlAutoLoad\n"
                    " hotSAN 2016\n"
                    "  ? | help  - Show this help.\n"
                    "  mkdir [-(l - Load all modules from dir; -r - Follow directory recursively)] <Dirname> - Add directory to watch list.\n"
                    "  rmdir [-(u - Unload all libs in dir)] <Dirname> - Remove directory from watch list.\n"
                    "  rmmdls - Remove all loaded libraries.\n"
                    );
            }
            break;
            LQSTR_SWITCH_DEFAULT
                if(OutBuffer)
                    fprintf(OutBuffer, " [MdlAutoLoad] ERROR: unknown command");
        }
    };

    Mod.FreeNotifyProc =
    [](LqHttpMdl* This) -> uintptr_t
    {
        LqEvntSetClose2(&Task.TimerFd, 0);
        volatile int* Tst = &Task.TimerFd.Fd;
        while(*Tst != -1);
        Task.PathsLocker.LockWrite();
        for(auto& i : Task.Dirs)
        {
            LqFilePathEvntFree(i);
            LqFastAlloc::Delete(i);
        }
        Task.PathsLocker.Unlock();

        return This->Handle;
    };
    Task.RegDest = Reg;
    //Add task to worker boss
    auto NewTimer = LqFileTimerCreate(LQ_O_NOINHERIT);
    LqFileTimerSet(NewTimer, 3000);

    LqEvntFdInit(&Task.TimerFd, NewTimer, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
    Task.TimerFd.UserData = (uintptr_t)&Task;
    Task.TimerFd.Handler = Task.WorkingMethod;
    Task.TimerFd.CloseHandler = [](LqEvntFd* Fd)
    {
        LqFileClose(Fd->Fd);
        Fd->Fd = -1;
    };

    LqEvntFdAdd(&Task.TimerFd);

    return LQHTTPMDL_REG_OK;
}


