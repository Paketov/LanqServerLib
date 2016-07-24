/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqConsoleShell - Console shell for lanq server lib.
* !!! In module projects you must set alignment to 1 byte
*/

#include "LqWrkBoss.hpp"
#include "LqTime.h"
#include "LqLog.h"
#include "LqZombieKiller.hpp"
#include "LqHttpMdl.h"
#include "LqHttp.hpp"
#include "LqHttpPth.hpp"
#include "LqStrSwitch.h"
#include "LqFile.h"
#include "LqFileTrd.h"
#include "LqSbuf.h"
#include "LqCp.h"
#include "LqLib.h"
#include "LqStr.hpp"
#include "LqHttpPth.h"
#include "LqHttpAtz.h"
#include "LqBse64.hpp"
#include "LqStr.hpp"

#include <stdlib.h>
#include <stdint.h>
#include <type_traits>
#include <vector>
#include <thread>




#ifdef LQPLATFORM_WINDOWS
# pragma comment(lib, "Ws2_32.lib")
# pragma comment(lib, "Mswsock.lib")
#else
# include <signal.h>
#endif

LqString Port;

const char* PrintHlp();
LqString PermToString(uint8_t Perm);
void PrintAuth(FILE* Dest, LqHttpAtz* Auth);
LqString ReadPath(LqString& Source);
LqString ReadParams(LqString& Source, const char * Params);
void PrintPthRegisterResult(FILE* Dest, LqHttpPthResultEnm Res);
uint8_t ReadPermissions(LqString& Source);

template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, float>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%f%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}

template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, double>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%e%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}

template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, int>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%i%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}

template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, uint>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%u%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}

template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, ulong>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%u%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}


template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, llong>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%lli%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}

template<typename TypeNumber>
typename std::enable_if<std::is_same<TypeNumber, ullong>::value, bool>::type ReadNumber(LqString& Source, TypeNumber* Dest)
{
    int n = -1; sscanf(Source.c_str(), "%llu%n", Dest, &n); if(n != -1) { Source.erase(0, n); while((Source[0] == ' ') || ((Source[0] == '\t')))Source.erase(0, 1); return true; }return false;
}


bool IsLoop = true;

int main(int argc, char* argv[])
{
    FILE* OutFile = stdout;
    FILE* InFile = stdin;
#if !defined(LQPLATFORM_WINDOWS)
    signal(SIGTERM, [](int) ->void { IsLoop = false; });
#endif

    LqCpSet(LQCP_UTF_8);

    fprintf(OutFile,
	    " Name: LanQ (Lan Quick) Server Shell\n"
	    " hotSAN 2016\n\n"
    );


    auto Reg = LqHttpProtoCreate();
    if(Reg == nullptr)
    {
	fprintf(OutFile, "ERROR: Not alloc memory for protocol struct\n");
	return -1;
    }
    LqWrkBoss Boss(&Reg->Proto);

    Boss.SetProtocolFamily(AF_INET);
    Boss.Tasks["LqZombieKillerTask"]->SetPeriodMillisec(10 * 1000);
    Boss.SetMaxConn(32768);
    char CommandBuf[128];
    int CommandLen;

    std::vector<LqHttpAtz*> AtzList;

    LqString CommandData;
    while(IsLoop)
    {
lblAgain:
	CommandData.clear();
	fprintf(OutFile, "LQ>");
	uint32_t c;

	char* s = CommandBuf;
	while(((c = LqStrCharRead(InFile)) == '\r') || (c == '\n') || (c == '\t') || (c == ' '));
	do
	{
	    s = LqStrUtf8CharToStr(s, c);
	    if((s - CommandBuf) > 120)
	    {
		fprintf(OutFile, " ERROR: Invalid command\n");
		goto lblAgain;
	    }
	} while(((c = LqStrCharRead(InFile)) != ' ') && (c != '\t') && (c != '\n') && (c != '\r'));
	*s = '\0';
	CommandLen = s - CommandBuf;
	if((c != '\n') && (c != '\r'))
	{
	    while(((c = LqStrCharRead(InFile)) != '\n') && (c != '\r'))
	    {
		if(CommandData.empty() && ((c == '\t') || (c == ' ')))
		    continue;
		char Buf[5] = {0};
		auto s = LqStrUtf8CharToStr(Buf, c);
		CommandData.append(Buf, s - Buf);
	    }
	}
	LQSTR_SWITCH_N(CommandBuf, CommandLen)
	{
	    LQSTR_CASE("?")
		LQSTR_CASE("help")
	    {
		fprintf(OutFile, PrintHlp());
	    }
	    break;
	    LQSTR_CASE("q")
		LQSTR_CASE("quit")
	    {
		IsLoop = false;
	    }
	    break;
	    LQSTR_CASE("prt")
	    {
		char PortName[255];
		PortName[0] = '\0';
		if(sscanf(CommandData.c_str(), "%254[a-zA-Z0-9]", PortName) < 1)
		{
		    Boss.GetPrt(PortName, sizeof(PortName) - 2);
		    fprintf(OutFile, " %s\n", PortName);
		    break;
		}
		Boss.SetPrt(PortName);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("gmtcorr")
	    {
		int Corr = 0;
		if(sscanf(CommandData.c_str(), "%i", &Corr) < 1)
		{
		    fprintf(OutFile, " %i\n", (int)LqTimeGetGmtCorrection());
		    continue;
		}
		LqTimeSetGmtCorrection(Corr);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("protofamily")
	    {
		LqString Param = ReadParams(CommandData, "64u");
		if(Param.find_first_of("6") != LqString::npos)
		{
		    Boss.SetProtocolFamily(AF_INET6);
		} else if(Param.find_first_of("4") != LqString::npos)
		{
		    Boss.SetProtocolFamily(AF_INET);
		} else if(Param.find_first_of("u") != LqString::npos)
		{
		    Boss.SetProtocolFamily(AF_UNSPEC);
		} else
		{
		    auto Proto = Boss.GetProtocolFamily();
		    const char * NameProto = "Unspec";
		    switch(Proto)
		    {
			case AF_INET: NameProto = "IPv4"; break;
			case AF_INET6: NameProto = "IPv6"; break;
		    }
		    fprintf(OutFile, " %s\n", NameProto);
		    break;
		}
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("start")
	    {
		Boss.ErrBind = -1;
		if(Boss.StartSync() && Boss.Tasks.StartSync())
		{
		    while(Boss.ErrBind == -1);
		    if(Boss.ErrBind != 0)
		    {
			Boss.Tasks.EndWorkSync();
			fprintf(OutFile, " ERROR: bind error \"%s\"\n", strerror(Boss.ErrBind));
			break;
		    }
		    fprintf(OutFile, " OK\n");
		} else
		{
		    Boss.ErrBind = 0;
		    fprintf(OutFile, " Has been started\n");
		}
	    }
	    break;
	    LQSTR_CASE("stop")
	    {
		if(Boss.EndWorkSync() && Boss.Tasks.EndWorkSync())
		    fprintf(OutFile, " OK\n");
		else
		    fprintf(OutFile, " ERROR: Not stopping\n");
	    }
	    break;
	    LQSTR_CASE("maxconn")
	    {
		int MaxConn = 0;
		if(!ReadNumber(CommandData, &MaxConn))
		{
		    fprintf(OutFile, " %llu\n", (ullong)Boss.GetMaxConn());
		    continue;
		}
		Boss.SetMaxConn(MaxConn);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    /*---------------------------
	    *Worker commands
	    */
	    LQSTR_CASE("addwrk")
	    {
		int Count = 0;
		if(sscanf(CommandData.c_str(), "%i", &Count) < 1)
		{
		    fprintf(OutFile, " %llu\n", (ullong)Boss.CountWorkers());
		    break;
		}
		if(Count < 0)
		    Count = std::thread::hardware_concurrency();
		if(Boss.AddWorkers(Count))
		    fprintf(OutFile, " OK\n");
		else
		    fprintf(OutFile, " ERROR: Not adding workers.\n");
	    }
	    break;
	    LQSTR_CASE("rmwrk")
	    {
		int Count = 0;
		if(sscanf(CommandData.c_str(), "%i", &Count) < 1)
		{
		    fprintf(OutFile, " ERROR: Invalid count of workers\n");
		    continue;
		}

		if(Count == -1)
		    Count = std::thread::hardware_concurrency();
		int CurCount = Boss.CountWorkers();
		if(CurCount > 0)
		    Count = lq_min(CurCount, Count);
		Boss.KickWorkers(Count);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    /*
	    *Worker commands
	    *---------------------------
	    */
	    /*---------------------------
	    *Module commands
	    */
	    LQSTR_CASE("ldmdl")
	    {
		if(CommandData.empty())
		{
		    fprintf(OutFile, " ERROR: Invalid module name\n");
		    continue;
		}
		if(CommandData[0] == '\"')
		{
		    CommandData.erase(0, 1);
		    auto Pos = CommandData.find('\"');
		    if(Pos == LqString::npos)
		    {
			fprintf(OutFile, " ERROR: Invalid module name\n");
			continue;
		    }
		    CommandData.erase(Pos, -1);
		}
		uintptr_t ModuleHandle;
		auto Res = LqHttpMdlLoad(Reg, CommandData.c_str(), nullptr, &ModuleHandle);
		switch(Res)
		{
		    case LQHTTPMDL_LOAD_ALREADY_HAVE:
			fprintf(OutFile, " ERROR: Already have this module\n");
			break;
		    case LQHTTPMDL_LOAD_FAIL:
			fprintf(OutFile, " ERROR: Fail load module \"%s\"\n", strerror(lq_errno));

			break;
		    case LQHTTPMDL_LOAD_PROC_NOT_FOUND:
			fprintf(OutFile, " ERROR: Fail load module. Not found entry point\n");
			break;
		    case LQHTTPMDL_LOAD_INDEPENDENTLY_UNLOADED:
			fprintf(OutFile, " NOTE: Independently unloaded\n");
		    case LQHTTPMDL_LOAD_OK:
		    {
			char NameBuf[1024];
			NameBuf[0] = '\0';
			LqHttpMdlGetNameByHandle(Reg, ModuleHandle, NameBuf, sizeof(NameBuf) - 1);
			fprintf(OutFile, " Module #%llx (%s) loaded\n", (unsigned long long)ModuleHandle, NameBuf);
			break;
		    }
		}
	    }
	    break;
	    LQSTR_CASE("rmmdl")
	    {
		LqString ModuleName = ReadPath(CommandData);
		if(ModuleName.empty())
		{
		    fprintf(OutFile, " ERROR: Invalid module name\n");
		    continue;
		}
		if(ModuleName[0] == '#')
		{
		    ModuleName.erase(0, 1);
		    ullong v = 0;
		    sscanf(ModuleName.data(), "%llx", &v);
		    if(LqHttpMdlFreeByHandle(Reg, (uintptr_t)v) >= 1)
			fprintf(OutFile, " OK\n");
		    else
			fprintf(OutFile, " ERROR: Module not found\n");
		} else
		{
		    auto CountRemoved = LqHttpMdlFreeByName(Reg, ModuleName.data(), true);
		    if(CountRemoved >= 1)
			fprintf(OutFile, " Removed %i count\n", (int)CountRemoved);
		    else
			fprintf(OutFile, " ERROR: Module not found\n");
		}
	    }
	    break;
	    LQSTR_CASE("mdllist")
	    {
		char Buf[10000];
		bool IsFree = false;
		for(uintptr_t h = 0; LqHttpMdlEnm(Reg, &h, Buf, sizeof(Buf), &IsFree);)
		    fprintf(OutFile, " #%llx (%s) %s\n", (ullong)h, Buf, (IsFree) ? "released" : "");
	    }
	    break;
	    LQSTR_CASE("mdlcommand")
	    {
		LqString ModuleName = ReadPath(CommandData);
		if(ModuleName.empty())
		{
		    fprintf(OutFile, " ERROR: Invalid module name\n");
		    break;
		}
		CommandData = "?" + CommandData;
		if(ModuleName[0] == '#')
		{
		    ModuleName.erase(0, 1);
		    ullong v = 0;
		    sscanf(ModuleName.data(), "%llx", &v);

		    if(LqHttpMdlSendCommandByHandle(Reg, (uintptr_t)v, CommandData.c_str(), OutFile) >= 1)
			fprintf(OutFile, "\n OK\n");
		    else
			fprintf(OutFile, " ERROR: Module not found\n");
		    break;
		}
		if(LqHttpMdlSendCommandByName(Reg, ModuleName.c_str(), CommandData.c_str(), OutFile) >= 1)
		    fprintf(OutFile, "\n OK\n");
		else
		    fprintf(OutFile, " ERROR: Module not found\n");
	    }
	    break;
	    /*
	    *Module commands
	    *---------------------------
	    */
	    /*---------------------------
	    *Domen commands
	    */
	    LQSTR_CASE("mkdmn")
	    {
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}
		LqHttpPthResultEnm Res = LqHttpPthDmnCreate(Reg, Domen.c_str());
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    LQSTR_CASE("rmdmn")
	    {
		LqString Domen = ReadPath(CommandData);
		if((Domen[0] == '\0') || (Domen[0] == '*'))
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}
		LqHttpPthResultEnm Res = LqHttpPthDmnDelete(Reg, Domen.c_str());
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    /*
	    *Domen commands
	    *---------------------------
	    */
	    /*---------------------------
	    *Path commands
	    */
	    LQSTR_CASE("mkredirect")
	    {
		LqString Param = ReadParams(CommandData, "f");
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}

		LqString NetDir = ReadPath(CommandData);
		if(NetDir[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid net dir name\n");
		    break;
		}
		LqString RealPath = ReadPath(CommandData);
		if(RealPath[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid real path name\n");
		    break;
		}
		int Status = 0;
		if(!ReadNumber(CommandData, &Status))
		{
		    fprintf(OutFile, " ERROR: Invalid status code\n");
		    break;
		}
		uint8_t Perm = ReadPermissions(CommandData);
		LqHttpPthResultEnm Res = (Param.find("f") == LqString::npos) ?
		    LqHttpPthRegisterDirRedirection(Reg, nullptr, Domen.c_str(), NetDir.c_str(), RealPath.c_str(), Status, Perm, nullptr, 0) :
		    LqHttpPthRegisterFileRedirection(Reg, nullptr, Domen.c_str(), NetDir.c_str(), RealPath.c_str(), Status, Perm, nullptr, 0);
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    LQSTR_CASE("mkpth")
	    {
		LqString Param = ReadParams(CommandData, "sf");
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}

		LqString NetDir = ReadPath(CommandData);
		if(NetDir[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid net dir name\n");
		    break;
		}
		LqString RealPath = ReadPath(CommandData);
		if(RealPath[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid real path name\n");
		    break;
		}
		uint8_t Perm = ReadPermissions(CommandData);
		LqHttpPthResultEnm Res = (Param.find("f") == LqString::npos) ?
		    LqHttpPthRegisterDir(Reg, nullptr, Domen.c_str(), NetDir.c_str(), RealPath.c_str(), Param.find("s") != LqString::npos, Perm, nullptr, 0) :
		    LqHttpPthRegisterFile(Reg, nullptr, Domen.c_str(), NetDir.c_str(), RealPath.c_str(), Perm, nullptr, 0);
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    LQSTR_CASE("rmpth")
	    {
		LqString Param = ReadParams(CommandData, "f");
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}

		LqString NetDir = ReadPath(CommandData);
		if(NetDir[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid net dir name\n");
		    break;
		}
		LqHttpPthResultEnm Res = (Param.find("f") == LqString::npos) ?
		    LqHttpPthUnregisterDir(Reg, Domen.c_str(), NetDir.c_str()) :
		    LqHttpPthUnregisterFile(Reg, Domen.c_str(), NetDir.c_str());
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    LQSTR_CASE("pthlist")
	    {
		char DomenBuf[512];
		DomenBuf[0] = '\0';
		LqString Param = ReadParams(CommandData, "p");
		while(LqHttpPthDmnEnm(Reg, DomenBuf, sizeof(DomenBuf) - 1))
		{
		    fprintf(OutFile, " Domen: %s\n", DomenBuf);
		    char NetAddress[10000];
		    char RealPath[LQ_MAX_PATH];
		    char ModuleName[10000];
		    RealPath[0] = ModuleName[0] = NetAddress[0] = '\0';
		    int Type;
		    LqHttpEvntHandlerFn FuncHandler;
		    uintptr_t ModuleHandler = 0;
		    uint8_t DefaultAccess = 0;
		    LqHttpAtz* UserList = nullptr;
		    while(
			LqHttpPthEnm
			(
			    Reg,
			    DomenBuf,
			    NetAddress,
			    sizeof(NetAddress) - 1,
			    &Type,
			    RealPath,
			    sizeof(RealPath) - 1,
			    &FuncHandler,
			    &ModuleHandler,
			    ModuleName,
			    sizeof(ModuleName),
			    nullptr,
			    &DefaultAccess,
			    &UserList
			)
		    )
		    {
			const char * TypeStr;
			switch(Type & LQHTTPPTH_TYPE_SEP)
			{
			    case LQHTTPPTH_TYPE_DIR:
				fprintf(
				    OutFile,
				    "  %s\n   dir%s\n   \"%s\"\n   #%llx (%s)\n   %s",
				    NetAddress,
				    (Type& LQHTTPPTH_FLAG_SUBDIR) ? " and subdirs" : "",
				    RealPath,
				    (unsigned long long)ModuleHandler,
				    ModuleName,
				    PermToString(DefaultAccess).c_str()
				);
				break;
			    case LQHTTPPTH_TYPE_FILE:
				fprintf(
				    OutFile,
				    "  %s\n   file\n   \"%s\"\n   #%llx (%s)\n   %s",
				    NetAddress,
				    RealPath,
				    (unsigned long long)ModuleHandler,
				    ModuleName,
				    PermToString(DefaultAccess).c_str()
				);
				break;
			    case LQHTTPPTH_TYPE_EXEC_DIR:
				fprintf(
				    OutFile,
				    "  %s\n   exec dir%s\n   #%llx\n   #%llx (%s)\n   %s",
				    NetAddress,
				    (Type& LQHTTPPTH_FLAG_SUBDIR) ? " and subdirs" : "",
				    (unsigned long long)FuncHandler,
				    (unsigned long long)ModuleHandler,
				    ModuleName,
				    PermToString(DefaultAccess).c_str()
				);
				break;
			    case LQHTTPPTH_TYPE_EXEC_FILE:
				fprintf(
				    OutFile,
				    "  %s\n   exec file\n   #%llx\n   #%llx (%s)\n   %s",
				    NetAddress,
				    (unsigned long long)FuncHandler,
				    (unsigned long long)ModuleHandler,
				    ModuleName,
				    PermToString(DefaultAccess).c_str()
				);
				break;
			    case LQHTTPPTH_TYPE_FILE_REDIRECTION:
				fprintf(
				    OutFile,
				    "  %s\n   file redirection\n   %s\n   #%llx (%s)\n   %s",
				    NetAddress,
				    RealPath,
				    (unsigned long long)ModuleHandler,
				    ModuleName,
				    PermToString(DefaultAccess).c_str()
				);
				break;
			    case LQHTTPPTH_TYPE_DIR_REDIRECTION:
				fprintf(
				    OutFile,
				    "  %s\n   dir redirection%s\n   %s\n   #%llx (%s)\n   %s",
				    NetAddress,
				    (Type& LQHTTPPTH_FLAG_SUBDIR) ? " and subdirs" : "",
				    RealPath,
				    (unsigned long long)ModuleHandler,
				    ModuleName,
				    PermToString(DefaultAccess).c_str()
				);
				break;
			}
			if((UserList != nullptr) && (Param == "p"))
			{
			    fprintf(OutFile, "\n");
			    PrintAuth(OutFile, UserList);
			}
			fprintf(OutFile, "\n");
			RealPath[0] = '\0';
			LqHttpAtzRelease(UserList);
			UserList = nullptr;
		    }
		}
	    }
	    break;
	    LQSTR_CASE("pthsetperm")
	    {
		LqString Param = ReadParams(CommandData, "f");
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}
		LqString NetDir = ReadPath(CommandData);
		if(NetDir[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid net file name\n");
		    break;
		}
		uint8_t Perm = ReadPermissions(CommandData);
		auto Res = (Param.find("f") == LqString::npos) ?
		    LqHttpPthDirSetPerm(Reg, Domen.c_str(), NetDir.c_str(), Perm) :
		    LqHttpPthFileSetPerm(Reg, Domen.c_str(), NetDir.c_str(), Perm);
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    LQSTR_CASE("pthsetatz")
	    {
		LqString Param = ReadParams(CommandData, "fr");
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}
		LqString NetDir = ReadPath(CommandData);
		if(NetDir[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid net file name\n");
		    break;
		}
		LqString RealmName = ReadPath(CommandData);
		if(RealmName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid realm\n");
		    break;
		}
		LqHttpAtz* Atz = nullptr;
		for(auto i : AtzList)
		    if(RealmName == i->Realm)
		    {
			Atz = i;
			break;
		    }
		if(Atz == nullptr)
		{
		    fprintf(OutFile, " ERROR: Not found authorization(Enter another realm)\n");
		    break;
		}
		auto Res = (Param.find("f") == LqString::npos) ?
		    LqHttpPthDirSetAtz(Reg, Domen.c_str(), NetDir.c_str(), Atz, Param.find("r") != LqString::npos) :
		    LqHttpPthFileSetAtz(Reg, Domen.c_str(), NetDir.c_str(), Atz, Param.find("r") != LqString::npos);
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    LQSTR_CASE("pthunsetatz")
	    {
		LqString Param = ReadParams(CommandData, "f");
		LqString Domen = ReadPath(CommandData);
		if(Domen[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid domen name\n");
		    break;
		}
		LqString NetDir = ReadPath(CommandData);
		if(NetDir[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid net file name\n");
		    break;
		}
		auto Res = (Param.find("f") == LqString::npos) ?
		    LqHttpPthDirSetAtz(Reg, Domen.c_str(), NetDir.c_str(), nullptr, false) :
		    LqHttpPthFileSetAtz(Reg, Domen.c_str(), NetDir.c_str(), nullptr, false);
		PrintPthRegisterResult(OutFile, Res);
	    }
	    break;
	    /*
	    *Path commands
	    *---------------------------
	    */
	    /*---------------------------
	    *Authorization commands
	    */
	    LQSTR_CASE("mkatz")
	    {
		LqString Param = ReadParams(CommandData, "bd");
		if(Param.find_first_of("bd") == LqString::npos)
		{
		    fprintf(OutFile, " ERROR: Invalid type of authorization (b - basic, d - digest)\n");
		    break;
		}
		LqString RealmName = ReadPath(CommandData);
		if(RealmName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid realm\n");
		    break;
		}

		bool IsHave = false;
		for(auto i : AtzList)
		    if(RealmName == i->Realm)
		    {
			IsHave = true;
			break;
		    }
		if(IsHave)
		{
		    fprintf(OutFile, " ERROR: Already have this authorization(enter another realm)\n");
		    break;
		}

		auto NewAtz = LqHttpAtzCreate((Param.find("b") != LqString::npos) ? LQHTTPATZ_TYPE_BASIC : LQHTTPATZ_TYPE_DIGEST, RealmName.c_str());
		if(NewAtz == nullptr)
		{
		    fprintf(OutFile, " ERROR: Not alloc new authorization\n");
		    break;
		}
		AtzList.push_back(NewAtz);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("rmatz")
	    {
		LqString RealmName = ReadPath(CommandData);
		if(RealmName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid realm\n");
		    break;
		}
		bool IsHave = false;
		for(int i = 0; i < AtzList.size(); i++)
		{
		    if(RealmName == AtzList[i]->Realm)
		    {
			IsHave = true;
			LqHttpAtzRelease(AtzList[i]); /* decrement count of pointers */
			if(AtzList.size() > 1)
			    AtzList[i] = AtzList[AtzList.size() - 1];
			AtzList.pop_back();
		    }
		}
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("atzmkusr")
	    {
		LqString RealmName = ReadPath(CommandData);
		if(RealmName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid realm\n");
		    break;
		}
		LqString UserName = ReadPath(CommandData);
		if(UserName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid user name\n");
		    break;
		}

		LqString Password = ReadPath(CommandData);
		if(Password[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid password\n");
		    break;
		}
		uint8_t Perm = ReadPermissions(CommandData);
		LqHttpAtz* Atz = nullptr;
		for(auto i : AtzList)
		    if(RealmName == i->Realm)
		    {
			Atz = i;
			break;
		    }
		if(Atz == nullptr)
		{
		    fprintf(OutFile, " ERROR: Not found authorization(Enter another realm)\n");
		    break;
		}
		if(!LqHttpAtzAdd(Atz, Perm, UserName.c_str(), Password.c_str()))
		{
		    fprintf(OutFile, " ERROR: Not adding user in authorization\n");
		    break;
		}
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("atzrmusr")
	    {
		LqString RealmName = ReadPath(CommandData);
		if(RealmName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid realm\n");
		    break;
		}
		uint8_t Perm = ReadPermissions(CommandData);
		LqString UserName = ReadPath(CommandData);
		if(UserName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid user name\n");
		    break;
		}
		LqHttpAtz* Atz = nullptr;
		for(auto i : AtzList)
		    if(RealmName == i->Realm)
		    {
			Atz = i;
			break;
		    }
		if(Atz == nullptr)
		{
		    fprintf(OutFile, " ERROR: Not found authorization(Enter another realm)\n");
		    break;
		}
		if(!LqHttpAtzRemove(Atz, UserName.c_str()))
		{
		    fprintf(OutFile, " ERROR: Not remove user from authorization list\n");
		    break;
		}
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("atzlist")
	    {
		for(auto i : AtzList)
		    PrintAuth(OutFile, i);
		fprintf(OutFile, "\n");
	    }
	    break;

	    /*
	    *Authorization commands
	    *---------------------------
	    */
	    /*---------------------------
	    *SSL Parametrs
	    */
	    LQSTR_CASE("setssl")
	    {
		LqString Param = ReadParams(CommandData, "ap");
		LqString CertName = ReadPath(CommandData);
		if(CertName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid cert file name\n");
		    break;
		}

		LqString KeyFileName = ReadPath(CommandData);
		if(CertName[0] == '\0')
		{
		    fprintf(OutFile, " ERROR: Invalid key file name\n");
		    break;
		}

#ifdef HAVE_OPENSSL
		bool Res = LqHttpProtoCreateSSL
		(
		    Reg,
		    SSLv23_method(),
		    CertName.c_str(),
		    KeyFileName.c_str(),
		    (Param.find_first_of("p") != LqString::npos) ? SSL_FILETYPE_PEM : SSL_FILETYPE_ASN1,
		    nullptr,
		    nullptr,
		    0,
		    0
		);
		if(Res)
		{
		    fprintf(OutFile, " OK\n");
		} else
		{
		    fprintf(OutFile, " ERROR: Not create ssl\n");
		}
#else
		fprintf(OutFile, " ERROR: Not implemented(Recompile all server with OpenSSL library)\n");
#endif
	    }
	    break;
	    LQSTR_CASE("unsetssl")
	    {
		LqHttpProtoRemoveSSL(Reg);
	    }
	    break;
	    /*
	    *SSL Parametrs
	    *---------------------------
	    */
	    /*---------------------------
	    *Cache parametrs
	    */
	    LQSTR_CASE("chemaxsize")
	    { /* Cache parametrs*/
		size_t Size = 0;
		if(!ReadNumber(CommandData, &Size))
		{
		    fprintf(OutFile, " %llu\n", (ullong)LqHttpCheGetMaxSize(Reg));
		    break;
		}
		LqHttpCheSetMaxSize(Reg, Size);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("chemaxsizef")
	    {
		size_t Size = 0;
		if(!ReadNumber(CommandData, &Size))
		{
		    fprintf(OutFile, " %llu\n", (ullong)LqHttpCheGetMaxSizeFile(Reg));
		    break;
		}
		LqHttpCheSetMaxSizeFile(Reg, Size);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("chesize")
	    {
		fprintf(OutFile, " %llu\n", (ullong)LqHttpCheGetEmployedSize(Reg));
	    }
	    break;
	    LQSTR_CASE("cheperu") //CacHE PERiod Update
	    {
		LqTimeMillisec Millisec;
		if(!ReadNumber(CommandData, &Millisec))
		{
		    fprintf(OutFile, " %llu\n", (ullong)LqHttpCheGetPeriodUpdateStat(Reg));
		    break;
		}
		LqHttpCheSetPeriodUpdateStat(Reg, Millisec);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("cheprepcount") //CacHE PREPared COUNT
	    {
		size_t Count;
		if(!ReadNumber(CommandData, &Count))
		{
		    fprintf(OutFile, " %llu\n", (ullong)LqHttpCheGetMaxCountOfPrepared(Reg));
		    break;
		}
		LqHttpCheSetMaxCountOfPrepared(Reg, Count);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    /*
	    *Cache parametrs
	    *--------------------------
	    */
	    /*---------------------------
	    *Connections parametr
	    */
	    LQSTR_CASE("conncount")
	    {
		fprintf(OutFile, " %llu\n", (ullong)Boss.CountConnections());
	    }
	    break;
	    LQSTR_CASE("conncloseall")
	    {
		Boss.CloseAllConnAsync();
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("connclosebyip")
	    {
		LqString Param = ReadParams(CommandData, "6");
		LqString IpAddress = ReadPath(CommandData);
		if(IpAddress.empty())
		{
		    fprintf(OutFile, " ERROR: Invalid ip address\n");
		    break;
		}
		union Addr
		{
		    sockaddr adr;
		    sockaddr_in in;
		    sockaddr_in6 in6;
		};
		Addr adr;
		if(inet_pton(adr.adr.sa_family = (Param.find_first_of("6") != LqString::npos) ? AF_INET6 : AF_INET, IpAddress.c_str(),
		    (Param.find_first_of("6") != LqString::npos)
		   ? (void*)&adr.in6.sin6_addr : (void*)&adr.in.sin_addr) != 1)
		    {
			fprintf(OutFile, " ERROR: Invalid ip address\n");
			break;
		    }
		Boss.CloseConnByIpSync(&adr.adr);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("connlist")
	    {
		Boss.EnumConn(OutFile,
				[](void* OutFile, LqConn* Conn)
				{
				    union Addr
				    {
					sockaddr adr;
					sockaddr_in in;
					sockaddr_in6 in6;
				    };
				    socklen_t PeerName = sizeof(Addr);
				    Addr adr;
				    getpeername(Conn->SockDscr, &adr.adr, &PeerName);
				    char Host[1024];
				    char Service[1024];
				    getnameinfo(&adr.adr, PeerName, Host, sizeof(Host) - 1, Service, sizeof(Service) - 1, NI_NUMERICSERV | NI_NUMERICHOST);
				    fprintf((FILE*)OutFile, " Host: %s, Port: %s\n", Host, Service);
				}
		);
	    }
	    break;
	    /*
	    *Connections parametr
	    *--------------------------
	    */
	    LQSTR_CASE("name")
	    {
		LqString ServName = ReadPath(CommandData);
		if(ServName.empty())
		{
		    char ServName[4096] = {0};
		    LqHttpProtoGetNameServer(Reg, ServName, sizeof(ServName) - 1);
		    fprintf(OutFile, " %s\n", ServName);
		    break;
		}
		LqHttpProtoSetNameServer(Reg, ServName.c_str());
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("timelife")
	    {
		LqTimeMillisec Millisec;
		if(!ReadNumber(CommandData, &Millisec))
		{
		    Boss.Tasks["LqZombieKillerTask"]->SendCommand("gettimelife", &Millisec);
		    fprintf(OutFile, " %llu\n", (ullong)Millisec);
		    break;
		}
		Boss.Tasks["LqZombieKillerTask"]->SendCommand("settimelife", Millisec);
		Boss.Tasks["LqZombieKillerTask"]->SetPeriodMillisec(Millisec / 2);
		Boss.Tasks.CheckNow();
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("maxhdrsize")
	    {
		size_t Size;
		if(!ReadNumber(CommandData, &Size))
		{
		    fprintf(OutFile, " %llu\n", (ullong)Reg->MaxHeadersSize);
		    break;
		}
		Reg->MaxHeadersSize = lq_max(Size, 4);
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("maxmltprthdrsize")
	    {
		size_t Size;
		if(!ReadNumber(CommandData, &Size))
		{
		    fprintf(OutFile, " %llu\n", (ullong)Reg->MaxMultipartHeadersSize);
		    break;
		}
		Reg->MaxMultipartHeadersSize = Size;
		fprintf(OutFile, " OK\n");
	    }
	    break;
	    LQSTR_CASE("perchgdigestnonce") //Period chenge digest nonce
	    {
		LqTimeSec Time;
		if(!ReadNumber(CommandData, &Time))
		{
		    fprintf(OutFile, " %llu\n", (ullong)Reg->PeriodChangeDigestNonce);
		    break;
		}
		Reg->PeriodChangeDigestNonce = Time;
		fprintf(OutFile, " OK\n");
	    }
	    break;

	    LQSTR_SWITCH_DEFAULT
	    {
		    fprintf(OutFile, " ERROR: Invalid command\n");
	    }
	    break;
	}
    }
    return 0;
}

const char* PrintHlp()
{
    static const char* HlpMsg =
	" Help:\n"
	"   ? or help - Show this help.\n"
	"   q or quit - Quit from server shell.\n"
	"   prt [<integer or name service>] - Set or get port number or name service. Ex: prt http; prt 8080; prt;\n"
	"   gmtcorr [<Count secods>] - Set or get GMT (relatively) correction for current system time. Ex: gmtcorr 3600; gmtcorr -7200; gmtcorr;\n"
	"   maxconn [<Count connections>] - Set or get max connection.\n"
	"   protofamily [-(4 - IPv4; 6 - IPv6; u - Unspec)] - Set or get proto family.\n"

	"   start - Start server.\n"
	"   stop - Stop server.\n"

	"   addwrk <Count> - Add workers. If set -1, then set for the current CPU`s in the system. Ex: addwrk 2, addwrk -1;\n"
	"   rmwrk <Count> - Remove workers. Ex: rmwrk 2, rmwrk -1;\n"

	"   ldmdl <path to module> - Load module. Ex: ldmdl C:\\lanq\\Module.dll, ldmdl /usr/lanq/Module.so\n"
	"   rmmdl  <name or #handle> - Remove module by internal name or handle. Ex: rmmdl #0777AD78, rmmdl Site1\n"
	"   mdllist - Show all modules.\n"
	"   mdlcommand <Module name or #handle> <Command> [<Command arguments>] - Send command to module.\n"

	"   mkredirect [-(f - for file(otherwise for dir);)] <domen name or *> <URI path> <Real path> <Respose status> <permissions> - Add redirection."
	" Ex: mkredirect * /hello http://lanqsite.com/hello 301 rt\n"
	"   mkpth [-(f - for file(otherwise for dir);)] <domen name or *> <URI path> <Real path> <permissions> - Add path."
	" Ex: mkpth * /hello/ \"C:\\LanQ\\html\\\" rt; mkpth -f * / \"C:\\LanQ\\html\\index.html\" rt; mkpth * / \"C:\\LanQ\\html\" rt\n"
	"   rmpth [-(f - for file(otherwise for dir);)] <domen name or *> <URI path> - Remove path or redirection."
	" Ex: rmpth -f * /\n"
	"   pthlist [-(p - full info)] - Show all path list. If set flag -p, then show authorization info also.\n"
	"   pthsetperm [-(f - for file;)] <domen name or *> <URI path> <New permissions> - Set new permissions to path. Ex: pthsetperm -f * / rtc\n"
	"   pthsetatz [-(f - for file;)] <domen name or *> <URI path> <Realm of atz> - Set new authorization to path. Ex: pthsetatz -f * / MainAuthorization\n"
	"   pthunsetatz [-(f - for file;)] <domen name or *> <URI path> <Realm of atz> - Unset authorization from path. Ex: pthunsetatz -f * / MainAuthorization\n"

	"   mkatz -(d - digest; b - basic) <Realm of atz> - Create new authorization. Ex: mkatz -d MainAuthorization\n"
	"   rmatz <Realm of atz> - Remove authorization. Ex: rmatz MainAuthorization\n"
	"   atzmkusr <Realm of atz> <User name> <Password> <Perrmissions> - Add user to authorization. Ex: atzmkusr MainAuthorization Admin Ht2443422kdff cdtrws\n"
	"   atzrmusr <Realm of atz> - Remove user from authorization. Ex: atzrmusr MainAuthorization.\n"
	"   atzlist - Print all authorization.\n"

	"   setssl - (a - The file is in abstract syntax notation 1 (ASN.1) format.; p - The file is in base64 privacy enhanced mail (PEM) format.) <Cert file name> <Key file name> - "
	"Registrate new SSL contex. Ex: setssl -p server.pem server.key\n"
	"   unsetssl - Unregister SSL. \n"

	"   chemaxsize [<Size>] - Set or get max size of cache (In bytes).\n"
	"   chemaxsizef [<Size>] - Set or get max size file in cache(In bytes).\n"
	"   chesize - Get current size of cache.\n"
	"   cheperu [<Millisecond>] - Set or get period update file in cache(Milliseconds).\n"
	"   cheprepcount [<Count>] - Set or get count files in prepare queue cache.\n"

	"   conncount - Get current connection count.\n"
	"   conncloseall - Close all connection.\n"
	"   connclosebyip [-(6 - is IPv6;)] <IP address> - Close connections by IP address.\n"
	"   connlist - Show ip and port of all connections."

	"   name [<Name of server>] - Set or get name of server.\n"
	"   timelife [<Millisec>] - Set or get time life connection in milliseconds.\n"
	"   maxhdrsize [<Size>] - Set or get maximum size (in bytes) of reciving http headers .\n"
	"   maxmltprthdrsize [<Size>] - Set or get maximum multipart header size (in bytes).\n"
	"   perchgdigestnonce [<Soconds>] - Set or get period change digest nonce(public key)\n"

	"\n";
    return HlpMsg;
}

LqString PermToString(uint8_t Perm)
{
    LqString p;
    if(LQHTTPATZ_PERM_READ & Perm) p += "r";
    if(LQHTTPATZ_PERM_WRITE & Perm) p += "w";
    if(LQHTTPATZ_PERM_CHECK & Perm) p += "t";
    if(LQHTTPATZ_PERM_CREATE & Perm) p += "c";
    if(LQHTTPATZ_PERM_DELETE & Perm) p += "d";
    if(LQHTTPATZ_PERM_MODIFY & Perm) p += "m";
    if(LQHTTPATZ_PERM_CREATE_SUBDIR & Perm) p += "s";
    return p;
}

void PrintPthRegisterResult(FILE* Dest, LqHttpPthResultEnm Res)
{
    switch(Res)
    {
	case LQHTTPPTH_RES_ALREADY_HAVE: fprintf(Dest, " ERROR: Already have\n"); break;
	case LQHTTPPTH_RES_DOMEN_NAME_OVERFLOW: fprintf(Dest, " ERROR: Domen name overflow\n"); break;
	case LQHTTPPTH_RES_INVALID_NAME: fprintf(Dest, " ERROR: Invlaid name\n"); break;
	case LQHTTPPTH_RES_MODULE_REJECT: fprintf(Dest, " ERROR: Module reject\n"); break;
	case LQHTTPPTH_RES_NOT_ALLOC_MEM: fprintf(Dest, " ERROR: Not alloc mem\n"); break;
	case LQHTTPPTH_RES_NOT_HAVE_DOMEN: fprintf(Dest, " ERROR: Not have domen\n"); break;
	case LQHTTPPTH_RES_NOT_HAVE_ATZ: fprintf(Dest, " ERROR: Not have authorization\n"); break;
	case LQHTTPPTH_RES_ALREADY_HAVE_ATZ: fprintf(Dest, " ERROR: Already have authorization\n"); break;
	case LQHTTPPTH_RES_NOT_DIR: fprintf(Dest, " ERROR: Not dir\n"); break;
	case LQHTTPPTH_RES_NOT_HAVE_PATH: fprintf(Dest, " ERROR: Not have path\n"); break;
	case LQHTTPPTH_RES_OK: fprintf(Dest, " OK\n"); break;
	default: fprintf(Dest, " ERROR\n"); break;
    }
}

void PrintAuth(FILE* Dest, LqHttpAtz* Auth)
{
    LqHttpAtzLockRead(Auth);
    fprintf(Dest, "   Auth type: %s; Realm: %s", (Auth->AuthType == LQHTTPATZ_TYPE_BASIC) ? "basic" : "digest", Auth->Realm);
    if(Auth->AuthType == LQHTTPATZ_TYPE_BASIC)
    {
	for(int i = 0; i < Auth->CountAuthoriz; i++)
	{
	    auto Res = LqBase64DecodeToStlStr(Auth->Basic[i].LoginPassword, LqStrLen(Auth->Basic[i].LoginPassword));
	    auto Pos = Res.find(':', 0);
	    if(Pos == LqString::npos)
		continue;
	    Res.erase(Pos, -1);
	    fprintf(Dest, "\n    User: %s; Mask: %s", Res.c_str(), PermToString(Auth->Basic[i].AccessMask).c_str());
	}
    } else
    {
	for(int i = 0; i < Auth->CountAuthoriz; i++)
	{
	    fprintf(Dest, "\n    User: %s; Mask: %s", Auth->Digest[i].UserName, PermToString(Auth->Digest[i].AccessMask).c_str());
	}
    }
    LqHttpAtzUnlock(Auth);
}

uint8_t ReadPermissions(LqString& Source)
{
    uint8_t r = 0;
    for(int i = 0; (Source[0] != '\0') && (strchr("rwtcdms", Source[0]) != nullptr); i++)
    {
	switch(Source[0])
	{
	    case 'r': r |= LQHTTPATZ_PERM_READ; break;
	    case 'w': r |= LQHTTPATZ_PERM_WRITE; break;
	    case 't': r |= LQHTTPATZ_PERM_CHECK; break;
	    case 'c': r |= LQHTTPATZ_PERM_CREATE; break;
	    case 'd': r |= LQHTTPATZ_PERM_DELETE; break;
	    case 'm': r |= LQHTTPATZ_PERM_MODIFY; break;
	    case 's': r |= LQHTTPATZ_PERM_CREATE_SUBDIR; break;
	}
	Source.erase(0, 1);
    }
    while((Source[0] == ' ') || ((Source[0] == '\t')))
	Source.erase(0, 1);
    return r;
}

LqString ReadPath(LqString& Source)
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

LqString ReadParams(LqString& Source, const char * Params)
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
