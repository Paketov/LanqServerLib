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
#include "LqDirEvnt.h"
#include "LqDirEnm.h"
#include "LqDfltRef.hpp"

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

class LqMdlPathFollowTask:
    public LqWrkTask::Task
{
public:
    LqDirEvnt			Dirs;
    LqLocker<uint>		PathsLocker;
    LqLocker<uint>		ModuleLocker;
    LqHttpProtoBase*		RegDest;
    struct Module
    {
	LqString		ModuleName;
	uintptr_t		Handle;
    };

    std::vector<Module>	Modules;

    LqMdlPathFollowTask(): LqWrkTask::Task("MdlAutoLoad") 
    {
    }

    virtual uintptr_t SendCommand(const char * Command, ...) 
    {
	return 0;
    }

    virtual void WorkingMethod()
    {
	LqDirEvntPath* Paths = nullptr;
	PathsLocker.LockWriteYield();
	LqDirEvntCheck(&Dirs, &Paths, 10);
	PathsLocker.UnlockWrite();
	for(auto j = Paths; j != nullptr; j = j->Next)
	{
	    if(j->Flag & (LQDIREVNT_ADDED | LQDIREVNT_MOVE_TO))
	    {
		LoadModule(j->Name);
	    } else if(j->Flag & (LQDIREVNT_RM | LQDIREVNT_MOVE_FROM))
	    {
		ModuleLocker.LockWrite();
		for(unsigned i = 0; i < Modules.size(); i++)
		{
		    if(LqStrSame(Modules[i].ModuleName.c_str(), j->Name))
		    {
			if(LqHttpMdlFreeByHandle(RegDest, Modules[i].Handle) > -1)
			{
			    Modules[i] = Modules[Modules.size() - 1];
			    Modules.pop_back();
			}
		    }
		}
		ModuleLocker.UnlockWrite();
	    }
	}
	LqDirEvntPathFree(&Paths);
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
    LqDirEnm DirEnm;
    char FileName[32768];
    uint8_t Flag;

    for(auto r = LqDirEnmStart(&DirEnm, Dir, FileName, sizeof(FileName) - 1, &Flag); r != -1; r = LqDirEnmNext(&DirEnm, FileName, sizeof(FileName) - 1, &Flag))
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
	    (LQ_PATH_SEPARATOR == '\\')? 
	    LqStrUtf8CmpCaseLen(Task.Modules[i].ModuleName.c_str(), DirName, l):
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
    if(LqDirEvntInit(&Task.Dirs) == -1)
    {
	printf("MdlAutoLoad error: Not init dir following \"%s\"\n", strerror(lq_errno));
	return LQHTTPMDL_REG_FREE_LIB;
    }

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

		Task.PathsLocker.LockWrite();
		auto r = LqDirEvntAdd
		(
		    &Task.Dirs, Path.c_str(),
		    LQDIREVNT_MOVE_TO | LQDIREVNT_MOVE_FROM | LQDIREVNT_ADDED | LQDIREVNT_RM | ((Params.find_first_of('r') != LqString::npos)? LQDIREVNT_SUBTREE: 0)
		);
		Task.PathsLocker.UnlockWrite();
		if(r >= 0)
		{
		    if(Params.find_first_of('l') != LqString::npos)
			LoadAllModulesFromDir(Path.c_str(), Params.find_first_of('r') != LqString::npos);
		    if(OutBuffer)
			fprintf(OutBuffer, " MdlAutoLoad: Path added");
		} else
		{
		    if(OutBuffer)
			fprintf(OutBuffer, " MdlAutoLoad: Path not added");
		}
	    }
	    break;
	    LQSTR_CASE("rmdir")
	    {
		LqString Params = ReadParams(FullCommand, "u");
		LqString Path = ReadPath(FullCommand);
		Task.PathsLocker.LockWrite();
		auto r = LqDirEvntRm(&Task.Dirs, Path.c_str());
		Task.PathsLocker.UnlockWrite();
		if(r >= 0)
		{
		    if(Params.find_first_of('u') != LqString::npos)
			UnloadAllModules(Path.c_str());
		    if(OutBuffer)
			fprintf(OutBuffer, " MdlAutoLoad: Path removed");
		} else
		{
		    if(OutBuffer)
			fprintf(OutBuffer, " MdlAutoLoad: Not found path");
		}
	    }
	    break;
	    LQSTR_CASE("rmmdls")
	    {
		UnloadAllModules();
		if(OutBuffer)
		    fprintf(OutBuffer, " MdlAutoLoad: All modules removed");
	    }
	    break;
	    LQSTR_CASE("?")
	    LQSTR_CASE("help")
	    {
		if(OutBuffer)
		    fprintf
		    (
			OutBuffer,
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
		    fprintf(OutBuffer, "MdlAutoLoad: Unknown command");
	}
    };

    Mod.FreeModuleNotifyProc =
    [](LqHttpMdl* This) -> uintptr_t
    {
	LqWrkBossByProto(This->Proto)->Tasks.Remove(&Task);
	LqDirEvntUninit(&Task.Dirs);
	printf("Unload MdlAutoLoad module\n");
	return This->Handle;
    };
    Task.RegDest = Reg;
    //Add task to worker boss
    LqWrkBossByProto(Reg)->Tasks.Add(&Task);
    LqWrkBossByProto(Reg)->Tasks["MdlAutoLoad"]->SetPeriodMillisec(3000);
    return LQHTTPMDL_REG_OK;
}





