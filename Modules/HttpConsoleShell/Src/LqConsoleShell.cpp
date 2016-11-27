/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqConsoleShell - Console shell for lanq server lib.
*/

#include "LqWrkBoss.hpp"
#include "LqTime.h"
#include "LqLog.h"
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
#include "LqDef.hpp"


#include <stdlib.h>
#include <type_traits>
#include <vector>
#include <thread>
#include <stack>

#include <fcntl.h>

#ifndef LQPLATFORM_WINDOWS
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
std::stack<FILE*> InFiles;
FILE* OutFile;
FILE* InFile;

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


uint32_t ReadChar()
{
    if(InFiles.size() <= 1)
        return LqStrCharRead(InFiles.top());
    auto Res = LqStrCharReadUtf8File(InFiles.top());
    if((Res == (uint32_t)-1) && feof(InFiles.top()))
    {
        fclose(InFiles.top());
        InFiles.pop();
        return -1;
    }
    return Res;
}


int ReadCommandLine(char* CommandBuf, LqString& Line, LqString& Flags)
{
    Line.clear();
    Flags.clear();
    uint32_t c;

    char* s = CommandBuf;
    while(((c = ReadChar()) == '\r') || (c == '\n') || (c == '\t') || (c == ' '));
    if(c == -1)
        return -1;
    while((c == '@') || (c == '+') || (c == '-'))
    {
        Flags.append(1, (char)c);
        c = ReadChar();
    }
    do
    {
        s = LqStrUtf8CharToStr(s, c);
        if((s - CommandBuf) > 120)
            return -2;
    } while(((c = ReadChar()) != ' ') && (c != '\t') && (c != '\n') && (c != '\r'));
    *s = '\0';
    int CommandLen = s - CommandBuf;
    if((c != '\n') && (c != '\r'))
    {
        while(((c = ReadChar()) != '\n') && (c != '\r'))
        {
            if(Line.empty() && ((c == '\t') || (c == ' ')))
                continue;
            char Buf[5] = {0};
            auto s = LqStrUtf8CharToStr(Buf, c);
            Line.append(Buf, s - Buf);
        }
    }
    return CommandLen;
}

bool IsLoop = true;
bool IsSuccess = true;


int main(int argc, char* argv[])
{
    OutFile = stdout;
    InFile = stdin;

    FILE* NullOut = fopen(LQ_NULLDEV, "wt");
#if !defined(LQPLATFORM_WINDOWS)
    signal(SIGTERM, [](int) -> void { IsLoop = false; });
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

    char CommandBuf[128];
    int CommandLen;

    std::vector<LqHttpAtz*> AtzList;
    InFiles.push(InFile);

    LqString CommandData;
    LqString CommandFlags;

    if(argc > 1)
    {
        FILE* InCmd = fopen(argv[1], "rb");
        if(InCmd == nullptr)
        {
            fprintf(OutFile, " ERROR: Not open lqcmd file\n");
            return -1;
        }
        InFiles.push(InCmd);
    }

    while(IsLoop)
    {
lblAgain:
        OutFile = stdout;
        CommandData.clear();
        if(InFiles.size() <= 1)
            fprintf(OutFile, "LQ>");
        CommandLen = ReadCommandLine(CommandBuf, CommandData, CommandFlags);
        if(CommandLen == -1)
            goto lblAgain;
        if(InFiles.size() > 1)
        {
            for(int i = 0, m = InFiles.size(); i < m; i++)
                fprintf(OutFile, ">");
            fprintf(OutFile, "%s%s %s\n", CommandFlags.c_str(), CommandBuf, CommandData.c_str());
        }

        if(CommandLen == -2)
        {
            fprintf(OutFile, " ERROR: Invalid command\n");
            goto lblAgain;
        }
        if((CommandFlags.find_first_of('+') != LqString::npos) && !IsSuccess)
        {
            continue;
        } else if((CommandFlags.find_first_of('-') != LqString::npos) && IsSuccess)
        {
            continue;
        }

        if(CommandFlags.find_first_of('@') != LqString::npos)
            OutFile = NullOut;
        IsSuccess = true;

        size_t LastIndex = 0;
        while(LastIndex != LqString::npos)
        {
            auto VarIndex = CommandData.find("$<", LastIndex);
            LastIndex = LqString::npos;
            if(VarIndex != LqString::npos)
            {
                auto EndIndex = CommandData.find('>', VarIndex);
                if(EndIndex != LqString::npos)
                {
                    char Buf[32768];
                    Buf[0] = '\0';
                    if(LqFileGetEnv(CommandData.substr(VarIndex + 2, EndIndex - (VarIndex + 2)).c_str(), Buf, 32767) != -1)
                        CommandData = CommandData.substr(0, VarIndex) + Buf + CommandData.substr(EndIndex + 1);
                    LastIndex = EndIndex;
                }
            }
        }

        LQSTR_SWITCH_N(CommandBuf, CommandLen)
        {
            LQSTR_CASE("rem") /* Ignore line */
                break;
            LQSTR_CASE("lqcmd")
            {
                if(InFiles.size() >= 200)
                {
                    fprintf(OutFile, " ERROR: Over 200 recursive lqcmd called\n");
                    IsSuccess = false;
                    break;
                }
                LqString Path = ReadPath(CommandData);
                if(Path[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid lqcmd file path\n");
                    IsSuccess = false;
                    break;
                }

                FILE* InCmd = fopen(Path.c_str(), "rb");
                if(InCmd == NULL)
                {
                    fprintf(OutFile, " ERROR: Not open lqcmd file\n");
                    IsSuccess = false;
                    break;
                }
                fprintf(OutFile, " OK\n");
                InFiles.push(InCmd);
            }
            break;
            LQSTR_CASE("exitlqcmd")
            {
                if(InFiles.size() > 1)
                {
                    fclose(InFiles.top());
                    InFiles.pop();
                    fprintf(OutFile, " OK\n");
                } else
                {
                    fprintf(OutFile, " ERROR: Not exit from stdin\n");
                    IsSuccess = false;
                }
            }
            break;
            LQSTR_CASE("cd")
            {
                LqString Path = ReadPath(CommandData);
                if(Path[0] == '\0')
                {
                    char Buf[LQ_MAX_PATH];
                    LqFileGetCurDir(Buf, LQ_MAX_PATH - 1);
                    fprintf(OutFile, " %s\n", Buf);
                    break;
                }
                if(LqFileSetCurDir(Path.c_str()) == -1)
                {
                    fprintf(OutFile, " ERROR: Not change current dir (%s)\n", strerror(lq_errno));
                    IsSuccess = false;
                } else
                {
                    fprintf(OutFile, " OK\n");
                }
            }
            break;
            LQSTR_CASE("set")
            {
                if(CommandData[0] == '\0')
                {
                    char Buf[32768];
                    char *s = Buf;
                    Buf[0] = Buf[1] = '\0';
                    auto Count = LqFileGetEnvs(Buf, 32767);
                    for(; *s != '\0'; )
                    {
                        printf("%s\n", s);
                        s += (LqStrLen(s) + 1);
                    }
                    break;
                }
                auto i = CommandData.find('=');
                if(i == LqString::npos)
                {
                    char Buf[32768];
                    Buf[0] = '\0';
                    LqFileGetEnv(CommandData.c_str(), Buf, 32767);
                    fprintf(OutFile, " %s\n", Buf);
                    break;
                }
                LqString Name = CommandData.substr(0, i);
                LqString Value = CommandData.substr(i + 1);
                if(LqFileSetEnv(Name.c_str(), Value.c_str()) == -1)
                {
                    fprintf(OutFile, " ERROR: Not setted env. arg. (%s)\n", strerror(lq_errno));
                    IsSuccess = false;
                } else
                {
                    fprintf(OutFile, " OK\n");
                }
            }
            break;
            LQSTR_CASE("unset")
            {
                if(LqFileSetEnv(CommandData.c_str(), nullptr) == -1)
                {
                    fprintf(OutFile, " ERROR: Not unsetted env. arg. (%s)\n", strerror(lq_errno));
                    IsSuccess = false;
                } else
                {
                    fprintf(OutFile, " OK\n");
                }
            }
            break;
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
                    LqHttpProtoGetInfo(Reg, nullptr, 0, PortName, sizeof(PortName) - 1, nullptr, nullptr, nullptr);
                    fprintf(OutFile, " %s\n", PortName);
                    break;
                }
                LqHttpProtoSetInfo(Reg, nullptr, PortName, nullptr, nullptr, nullptr);
                fprintf(OutFile, " OK\n");
            }
            break;
            LQSTR_CASE("hst")
            {
                if(CommandData[0] == '\0')
                {
                    char Buf[16000];
                    LqHttpProtoGetInfo(Reg, Buf, sizeof(Buf) - 1, nullptr, 0, nullptr, nullptr, nullptr);
                    fprintf(OutFile, " %s\n", Buf);
                    break;
                }

                LqString Path = ReadPath(CommandData);
                LqHttpProtoSetInfo(Reg, Path.c_str(), nullptr, nullptr, nullptr, nullptr);
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
                    int v = AF_INET6;
                    LqHttpProtoSetInfo(Reg, nullptr, nullptr, &v, nullptr, nullptr);
                } else if(Param.find_first_of("4") != LqString::npos)
                {
                    int v = AF_INET;
                    LqHttpProtoSetInfo(Reg, nullptr, nullptr, &v, nullptr, nullptr);
                } else if(Param.find_first_of("u") != LqString::npos)
                {
                    int v = AF_UNSPEC;
                    LqHttpProtoSetInfo(Reg, nullptr, nullptr, &v, nullptr, nullptr);
                } else
                {
                    int Proto;
                    LqHttpProtoGetInfo(Reg, nullptr, 0, nullptr, 0, &Proto, nullptr, nullptr);
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
                if(LqHttpProtoIsBind(Reg))
                {
                    fprintf(OutFile, " ERROR: Has been started\n");
                    IsSuccess = false;
                    break;
                }
                int Count;
                if((Count = LqWrkBossStartAllWrkSync()) >= 0)
                {
                    if(LqHttpProtoBind(Reg) == -1)
                    {
                        fprintf(OutFile, " Not bind (%s)\n", strerror(lq_errno));
                        IsSuccess = false;
                    } else
                    {
                        fprintf(OutFile, " OK\n");
                    }
                } else
                {
                    fprintf(OutFile, " ERROR: Not start workers\n");
                    IsSuccess = false;
                    break;
                }
            }
            break;
            LQSTR_CASE("stop")
            {
                if(LqHttpProtoUnbind(Reg) == 0)
                {
                    fprintf(OutFile, " OK\n");
                } else
                {
                    fprintf(OutFile, " ERROR: Not stopping\n");
                    IsSuccess = false;
                }
            }
            break;
            LQSTR_CASE("maxconn")
            {
                int MaxConn = 0;
                if(!ReadNumber(CommandData, &MaxConn))
                {
                    int Count;
                    LqHttpProtoGetInfo(Reg, nullptr, 0, nullptr, 0, nullptr, &Count, nullptr);
                    fprintf(OutFile, " %llu\n", (ullong)Count);
                    continue;
                }
                LqHttpProtoSetInfo(Reg, nullptr, nullptr, nullptr, &MaxConn, nullptr);
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
                    fprintf(OutFile, " %llu\n", (ullong)LqWrkBossCountWrk());
                    break;
                }
                if(Count < 0)
                    Count = std::thread::hardware_concurrency();
                if(LqWrkBossAddWrks(Count, true) == Count)
                {
                    fprintf(OutFile, " OK\n");
                } else
                {
                    fprintf(OutFile, " ERROR: Not adding workers.\n");
                    IsSuccess = false;
                }
            }
            break;
            LQSTR_CASE("rmwrk")
            {
                int Count = 0;
                if(sscanf(CommandData.c_str(), "%i", &Count) < 1)
                {
                    fprintf(OutFile, " ERROR: Invalid count of workers\n");
                    IsSuccess = false;
                    continue;
                }

                if(Count == -1)
                    Count = std::thread::hardware_concurrency();
                int CurCount = LqWrkBossCountWrk();
                if(CurCount > 0)
                    Count = lq_min(CurCount, Count);
                LqWrkBossKickWrks(Count);
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
                LqString Path = ReadPath(CommandData);
                if(Path[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid module path\n");
                    IsSuccess = false;
                    break;
                }
                uintptr_t ModuleHandle;
                auto Res = LqHttpMdlLoad(Reg, Path.c_str(), nullptr, &ModuleHandle);
                switch(Res)
                {
                    case LQHTTPMDL_LOAD_ALREADY_HAVE:
                        fprintf(OutFile, " ERROR: Already have this module\n");
                        IsSuccess = false;
                        break;
                    case LQHTTPMDL_LOAD_FAIL:
                        fprintf(OutFile, " ERROR: Fail load module \"%s\"\n", strerror(lq_errno));
                        IsSuccess = false;
                        break;
                    case LQHTTPMDL_LOAD_PROC_NOT_FOUND:
                        fprintf(OutFile, " ERROR: Fail load module. Not found entry point\n");
                        IsSuccess = false;
                        break;
                    case LQHTTPMDL_LOAD_INDEPENDENTLY_UNLOADED:
                        fprintf(OutFile, " NOTE: Independently unloaded\n");
                        IsSuccess = false;
                        break;
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
                    IsSuccess = false;
                    continue;
                }
                if(ModuleName[0] == '#')
                {
                    ModuleName.erase(0, 1);
                    ullong v = 0;
                    sscanf(ModuleName.data(), "%llx", &v);
                    if(LqHttpMdlFreeByHandle(Reg, (uintptr_t)v) >= 1)
                    {
                        fprintf(OutFile, " OK\n");
                    } else
                    {
                        fprintf(OutFile, " ERROR: Module not found\n");
                        IsSuccess = false;
                    }
                } else
                {
                    auto CountRemoved = LqHttpMdlFreeByName(Reg, ModuleName.data(), true);
                    if(CountRemoved >= 1)
                    {
                        fprintf(OutFile, " Removed %i count\n", (int)CountRemoved);
                    } else
                    {
                        fprintf(OutFile, " ERROR: Module not found\n");
                        IsSuccess = false;
                    }
                }
            }
            break;
            LQSTR_CASE("mdllist")
            {
                char Buf[10000];
                bool IsFree = false;
                for(uintptr_t h = 0; LqHttpMdlEnm(Reg, &h, Buf, sizeof(Buf), &IsFree) >= 0;)
                    fprintf(OutFile, " #%llx (%s) %s\n", (ullong)h, Buf, (IsFree) ? "released" : "");
            }
            break;
            LQSTR_CASE("mdlcmd")
            {
                LqString ModuleName = ReadPath(CommandData);
                if(ModuleName.empty())
                {
                    fprintf(OutFile, " ERROR: Invalid module name\n");
                    IsSuccess = false;
                    break;
                }
                CommandData = "?" + CommandData;
                if(ModuleName[0] == '#')
                {
                    ModuleName.erase(0, 1);
                    ullong v = 0;
                    sscanf(ModuleName.data(), "%llx", &v);

                    if(LqHttpMdlSendCommandByHandle(Reg, (uintptr_t)v, CommandData.c_str(), OutFile) >= 0)
                        fprintf(OutFile, "\n OK\n");
                    else
                    {
                        fprintf(OutFile, " ERROR: Module not found\n");
                        IsSuccess = false;
                    }
                    break;
                }
                if(LqHttpMdlSendCommandByName(Reg, ModuleName.c_str(), CommandData.c_str(), OutFile) > 0)
                    fprintf(OutFile, "\n OK\n");
                else
                {
                    fprintf(OutFile, " ERROR: Module not found\n");
                    IsSuccess = false;
                }
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
                    IsSuccess = false;
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
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }

                LqString NetDir = ReadPath(CommandData);
                if(NetDir[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid net dir name\n");
                    IsSuccess = false;
                    break;
                }
                LqString RealPath = ReadPath(CommandData);
                if(RealPath[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid real path name\n");
                    IsSuccess = false;
                    break;
                }
                int Status = 0;
                if(!ReadNumber(CommandData, &Status))
                {
                    fprintf(OutFile, " ERROR: Invalid status code\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }

                LqString NetDir = ReadPath(CommandData);
                if(NetDir[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid net dir name\n");
                    IsSuccess = false;
                    break;
                }
                LqString RealPath = ReadPath(CommandData);
                if(RealPath[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid real path name\n");
                    IsSuccess = false;
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
                                fprintf
                                (
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
                                fprintf
                                (
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
                                fprintf
                                (
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
                                fprintf
                                (
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
                                fprintf
                                (
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
                                fprintf
                                (
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
                    IsSuccess = false;
                    break;
                }
                LqString NetDir = ReadPath(CommandData);
                if(NetDir[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid net file name\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }
                LqString NetDir = ReadPath(CommandData);
                if(NetDir[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid net file name\n");
                    IsSuccess = false;
                    break;
                }
                LqString RealmName = ReadPath(CommandData);
                if(RealmName[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid realm\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }
                LqString NetDir = ReadPath(CommandData);
                if(NetDir[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid net file name\n");
                    IsSuccess = false;
                    break;
                }
                auto Res = (Param.find("f") == LqString::npos) ?
                    LqHttpPthDirSetAtz(Reg, Domen.c_str(), NetDir.c_str(), nullptr, true) :
                    LqHttpPthFileSetAtz(Reg, Domen.c_str(), NetDir.c_str(), nullptr, true);
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
                    IsSuccess = false;
                    break;
                }
                LqString RealmName = ReadPath(CommandData);
                if(RealmName[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid realm\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }

                auto NewAtz = LqHttpAtzCreate((Param.find("b") != LqString::npos) ? LQHTTPATZ_TYPE_BASIC : LQHTTPATZ_TYPE_DIGEST, RealmName.c_str());
                if(NewAtz == nullptr)
                {
                    fprintf(OutFile, " ERROR: Not alloc new authorization\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }
                LqString UserName = ReadPath(CommandData);
                if(UserName[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid user name\n");
                    IsSuccess = false;
                    break;
                }

                LqString Password = ReadPath(CommandData);
                if(Password[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid password\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }
                if(!LqHttpAtzAdd(Atz, Perm, UserName.c_str(), Password.c_str()))
                {
                    fprintf(OutFile, " ERROR: Not adding user in authorization\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }
                uint8_t Perm = ReadPermissions(CommandData);
                LqString UserName = ReadPath(CommandData);
                if(UserName[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid user name\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }
                if(!LqHttpAtzRemove(Atz, UserName.c_str()))
                {
                    fprintf(OutFile, " ERROR: Not remove user from authorization list\n");
                    IsSuccess = false;
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
                    IsSuccess = false;
                    break;
                }

                LqString KeyFileName = ReadPath(CommandData);
                if(CertName[0] == '\0')
                {
                    fprintf(OutFile, " ERROR: Invalid key file name\n");
                    IsSuccess = false;
                    break;
                }

#ifdef HAVE_OPENSSL
                bool Res = LqHttpProtoCreateSSL
                (
                    Reg,
                    SSLv23_method(),
                    CertName.c_str(),
                    KeyFileName.c_str(),
                    nullptr,
                    (Param.find_first_of("p") != LqString::npos) ? SSL_FILETYPE_PEM : SSL_FILETYPE_ASN1,
                    nullptr,
                    nullptr
                );
                if(Res)
                {
                    fprintf(OutFile, " OK\n");
                } else
                {
                    fprintf(OutFile, " ERROR: Not create ssl\n");
                    IsSuccess = false;
                }
#else
                fprintf(OutFile, " ERROR: Not implemented(Recompile all server with OpenSSL library)\n");
                IsSuccess = false;
#endif
            }
            break;
            LQSTR_CASE("unsetssl")
            {
                LqHttpProtoRemoveSSL(Reg);
                fprintf(OutFile, " OK\n");
                IsSuccess = false;
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
                fprintf(OutFile, " %llu\n", (ullong)Reg->CountConnections);
            }
            break;
            LQSTR_CASE("conncloseall")
            {
                LqWrkBossCloseConnByProtoAsync(&Reg->Proto);
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
                    IsSuccess = false;
                    break;
                }
                LqConnInetAddress adr;
                if(LqConnStrToRowIp((Param.find_first_of("6") != LqString::npos)?6: 4, IpAddress.c_str(), &adr) == -1)
                {
                    fprintf(OutFile, " ERROR: Invalid ip address\n");
                    IsSuccess = false;
                    break;
                }

                LqWrkBossCloseConnByIpSync(&adr.Addr);
                fprintf(OutFile, " OK\n");
            }
            break;
            LQSTR_CASE("connlist")
            {

                LqWrkBossEnumCloseRmEvntByProto(
					[](void* OutFile, LqEvntHdr* Conn) -> unsigned
					{
						if(LqEvntIsConn(Conn))
						{
							char IpBuf[256];
							LqHttpConnGetRemoteIpStr((LqHttpConn*)Conn, IpBuf, 255);
							fprintf((FILE*)OutFile, " Host: %s, Port: %i\n", IpBuf, LqHttpConnGetRemotePort((LqHttpConn*)Conn));
						}
						return 0;
					},
					&Reg->Proto, 
					OutFile
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
                    LqHttpProtoGetInfo(Reg, nullptr, 0, nullptr, 0, nullptr, nullptr, &Millisec);
                    fprintf(OutFile, " %llu\n", (ullong)Millisec);
                    break;
                }
                LqHttpProtoSetInfo(Reg, nullptr,nullptr, nullptr, nullptr, &Millisec);
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
            LQSTR_CASE("dbginfo")
            {
                LqString DbgInfo = LqWrkBoss::GetGlobal()->DebugInfo();
                fprintf(OutFile, "%s", DbgInfo.c_str());
            }
            break;
            LQSTR_CASE("fulldbginfo")
            {
                LqString DbgInfo = LqWrkBoss::GetGlobal()->AllDebugInfo();
                fprintf(OutFile, "%s", DbgInfo.c_str());
            }
            break;
            LQSTR_SWITCH_DEFAULT
            {
                fprintf(OutFile, " ERROR: Invalid command\n");
                IsSuccess = false;
            }
            break;
        }
    }
    LqHttpProtoDelete(Reg);
    LqWrkBossSetMinWrkCount(0);
    LqWrkBossKickAllWrk();
    return 0;
}

const char* PrintHlp()
{
    static const char* HlpMsg =
        " Help:\n"
        "   Syntax of command: [Command prefix]<command> [args ...]\n"
        "     [Command prefix] - @ - Not out messages, + - Execute when previous command success, - - Execute when previous command fail\n"
        "     <command> - Command name (Ex. start, ldmdl, lqcmd)\n"
        "     [args ...] - Command arguments. Can contain ref on environment variable (Ex: $<VARIABLE>)\n"
        "   Example commands:\n"
        "    lqcmd D:\\server\\lqcmd\\init_serv.lqcmd\n"
        "    @+start\n"
        "    -quit\n"
        "   Commands:\n"
        "    ? or help - Show this help.\n"
        "    q or quit - Quit from server shell.\n"
        "    rem <Some comments> - Ignore line (Primary used in lqcmd file)\n"
        "    set [Name][=Value]- Set or get environment variable (When spec =, then setted, otherwise get)\n"
        "    unset [Name]- Unset environment variable\n"
        "    lqcmd <Path to lanq command file> - Execute command file\n"
        "    exitlqcmd - Quit from execute command file\n"
        "    cd [<Dir path>]- Set or get Current directory\n"
        "    hst [\"Name or IP address host\"] - Set or get host address. Ex. hst 0.0.0.0; hst \"0.0.0.0\"; hst; hst ::;\n"
        "    prt [<integer or name service>] - Set or get port number or name service. Ex: prt http; prt 8080; prt;\n"
        "    gmtcorr [<Count secods>] - Set or get GMT (relatively) correction for current system time. Ex: gmtcorr 3600; gmtcorr -7200; gmtcorr;\n"
        "    maxconn [<Count connections>] - Set or get max connection.\n"
        "    protofamily [-(4 - IPv4; 6 - IPv6; u - Unspec)] - Set or get proto family.\n"

        "    start - Start server.\n"
        "    stop - Stop server.\n"

        "    addwrk <Count> - Add workers. If set -1, then set for the current CPU`s in the system. Ex: addwrk 2, addwrk -1;\n"
        "    rmwrk <Count> - Remove workers. Ex: rmwrk 2, rmwrk -1;\n"

        "    ldmdl <path to module> - Load module. Ex: ldmdl C:\\lanq\\Module.dll, ldmdl /usr/lanq/Module.so\n"
        "    rmmdl  <name or #handle> - Remove module by internal name or handle. Ex: rmmdl #0777AD78, rmmdl Site1\n"
        "    mdllist - Show all modules.\n"
        "    mdlcmd <Module name or #handle> <Command> [<Command arguments>] - Send command to module.\n"

        "    mkredirect [-(f - for file(otherwise for dir);)] <domen name or *> <URI path> <Real path> <Respose status> <permissions> - Add redirection."
        " Ex: mkredirect * /hello http://lanqsite.com/hello 301 rt\n"
        "    mkpth [-(f - for file(otherwise for dir); s - with subdir(default non subdirs))] <domen name or *> <URI path> <Real path> <permissions> - Add path."
        " Ex: mkpth * /hello/ \"C:\\LanQ\\html\\\" rt; mkpth -f * / \"C:\\LanQ\\html\\index.html\" rt; mkpth * / \"C:\\LanQ\\html\" rt\n"
        "    rmpth [-(f - for file(otherwise for dir);)] <domen name or *> <URI path> - Remove path or redirection."
        " Ex: rmpth -f * /\n"
        "    pthlist [-(p - full info)] - Show all path list. If set flag -p, then show authorization info also.\n"
        "    pthsetperm [-(f - for file;)] <domen name or *> <URI path> <New permissions> - Set new permissions to path. Ex: pthsetperm -f * / rtc\n"
        "    pthsetatz [-(f - for file;)] <domen name or *> <URI path> <Realm of atz> - Set new authorization to path. Ex: pthsetatz -f * / MainAuthorization\n"
        "    pthunsetatz [-(f - for file;)] <domen name or *> <URI path> <Realm of atz> - Unset authorization from path. Ex: pthunsetatz -f * / MainAuthorization\n"

        "    mkatz -(d - digest; b - basic) <Realm of atz> - Create new authorization. Ex: mkatz -d MainAuthorization\n"
        "    rmatz <Realm of atz> - Remove authorization. Ex: rmatz MainAuthorization\n"
        "    atzmkusr <Realm of atz> <User name> <Password> <Perrmissions> - Add user to authorization. Ex: atzmkusr MainAuthorization Admin Ht2443422kdff cdtrws\n"
        "    atzrmusr <Realm of atz> - Remove user from authorization. Ex: atzrmusr MainAuthorization.\n"
        "    atzlist - Print all authorization.\n"

        "    setssl - (a - The file is in abstract syntax notation 1 (ASN.1) format.; p - The file is in base64 privacy enhanced mail (PEM) format.) <Cert file name> <Key file name> - "
        "Registrate new SSL contex. Ex: setssl -p server.pem server.key\n"
        "    unsetssl - Unregister SSL. \n"

        "    chemaxsize [<Size>] - Set or get max size of cache (In bytes).\n"
        "    chemaxsizef [<Size>] - Set or get max size file in cache(In bytes).\n"
        "    chesize - Get current size of cache.\n"
        "    cheperu [<Millisecond>] - Set or get period update file in cache(Milliseconds).\n"
        "    cheprepcount [<Count>] - Set or get count files in prepare queue cache.\n"

        "    conncount - Get current connection count.\n"
        "    conncloseall - Close all connection.\n"
        "    connclosebyip [-(6 - is IPv6;)] <IP address> - Close connections by IP address.\n"
        "    connlist - Show ip and port of all connections."

        "    name [<Name of server>] - Set or get name of server.\n"
        "    timelife [<Millisec>] - Set or get time life connection in milliseconds.\n"
        "    maxhdrsize [<Size>] - Set or get maximum size (in bytes) of reciving http headers .\n"
        "    maxmltprthdrsize [<Size>] - Set or get maximum multipart header size (in bytes).\n"
        "    perchgdigestnonce [<Soconds>] - Set or get period change digest nonce(public key)\n"

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
        case LQHTTPPTH_RES_NOT_DIR: fprintf(Dest, " ERROR: Not dir (dir must be ex: /serv/)\n"); break;
        case LQHTTPPTH_RES_NOT_HAVE_PATH: fprintf(Dest, " ERROR: Not have path\n"); break;
        case LQHTTPPTH_RES_OK: fprintf(Dest, " OK\n"); return;
        default: fprintf(Dest, " ERROR\n"); break;
    }
    IsSuccess = false;
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
