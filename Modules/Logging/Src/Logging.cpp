/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   Logging - Log queries in file.
*/

#include "LqConn.h"
#include "Lanq.h"
#include "LqWrkBoss.hpp"
#include "LqStrSwitch.h"
#include "LqHttpMdl.h"
#include "LqDfltRef.hpp"
#include "LqHttpPth.hpp"
#include "LqHttpRsp.h"
#include "LqHttpConn.h"
#include "LqHttpAct.h"
#include "LqTime.hpp"
#include "LqStr.hpp"
#include "LqHttpRcv.h"
#include "LqHttp.hpp"
#include "LqHttpAtz.h"
#include "LqShdPtr.hpp"

#include "LqTime.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"

LqHttpMdl Mod;
char UniqPtrStartLine;
char UniqPtrUserAgent;
char UniqPtrHst;
char UniqPtrRefer;
int OutFd = -1;
LqFileSz LoopSize = 0xffffffffffff;

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

void Log(const char* RemoteAddress, const char* StartLine, int RspStat, LqFileSz RspLen, const char* UserAgent, const char* Referer, const char* Host)
{

    char Buffer[32768];
    short TimeZone;
    auto CurTime = LqTimeGet(&TimeZone);
    TimeZone = -TimeZone;
    struct tm Tm;
    LqTimeLocSecToLocTm(&Tm, CurTime / 1000);
    int Written = snprintf(
        Buffer,
        sizeof(Buffer) - 3,
        "%s - - [%02i/%s/%i:%02i:%02i:%02i +%02i%02i] \"%s\" %i %llu \"%s\" \"%s\" \"%s\"\r\n",
        RemoteAddress,
        (int)Tm.tm_mday,
        LqTimeMonths[Tm.tm_mon],
        (int)(Tm.tm_year + 1900),
        (int)Tm.tm_hour,
        (int)Tm.tm_min,
        (int)Tm.tm_sec,
        (int)(TimeZone / 60),
        (int)(TimeZone - ((TimeZone / 60) * 60)),
        StartLine,
        RspStat,
        (unsigned long long)RspLen,
        Referer,
        UserAgent,
        Host
    );
    if((LoopSize < 0xffffffffffff) && (LqFileTell(OutFd) >= LoopSize))
        LqFileSeek(OutFd, 0, LQ_SEEK_SET);
    LqFileWrite(OutFd, Buffer, Written);
}


static void LQ_CALL QueryFn(LqHttpConn* NewConn)
{
    LqHttpConnInterface Conn(NewConn);
    {
        char* Start = nullptr, *End = nullptr;
        Conn.Quer.StartLine.Get(&Start, &End);
        if(Start != nullptr)
            Conn.UserData[&UniqPtrStartLine] = LqStrDuplicateMax(Start, End - Start);
    }
    {
        char* Start = nullptr, *End = nullptr;
        LqHttpRcvHdrSearchEx(NewConn, 0, "User-Agent", sizeof("User-Agent") - 1, nullptr, &Start, &End);
        if(Start != nullptr)
            Conn.UserData[&UniqPtrUserAgent] = LqStrDuplicateMax(Start, End - Start);
    }
    {
        char* Start = nullptr, *End = nullptr;
        LqHttpRcvHdrSearchEx(NewConn, 0, "Referer", sizeof("Referer") - 1, nullptr, &Start, &End);
        if(Start != nullptr)
            Conn.UserData[&UniqPtrRefer] = LqStrDuplicateMax(Start, End - Start);
    }

    {
        char* Start = nullptr, *End = nullptr;
        LqHttpRcvHdrSearchEx(NewConn, 0, "Host", sizeof("Host") - 1, nullptr, &Start, &End);
        if(Start != nullptr)
            Conn.UserData[&UniqPtrHst] = LqStrDuplicateMax(Start, End - Start);
    }
}


static void LQ_CALL ResponseFn(LqHttpConn* NewConn)
{
    LqHttpConnInterface Conn(NewConn);

    void*StartLine = Conn.UserData[&UniqPtrStartLine],
        *UserAgent = Conn.UserData[&UniqPtrUserAgent],
        *Referer = Conn.UserData[&UniqPtrRefer],
        *Host = Conn.UserData[&UniqPtrHst];
    char IpStr[256];
    IpStr[0] = '-';
    IpStr[1] = '\0';
    LqHttpConnGetRemoteIpStr(NewConn, IpStr, sizeof(IpStr) - 1);
    LqFileSz Length = 0;
    int Stat = 0;
    if(Conn.Rsp)
    {
        Length = LqParseInt(Conn.Rsp.Hdrs["Content-Length"]);
        Stat = Conn.Rsp.Stat;
    }

    Log(IpStr, (char*)(StartLine ? StartLine : "-"), Stat, Length, (char*)(UserAgent ? UserAgent : "-"), (char*)(Referer ? Referer : "-"), (char*)(Host ? Host : "-"));
    if(UserAgent)
    {
        Conn.UserData[&UniqPtrUserAgent].Delete();
        free(UserAgent);
    }
    if(StartLine)
    {
        Conn.UserData[&UniqPtrStartLine].Delete();
        free(StartLine);
    }
    if(Referer)
    {
        Conn.UserData[&UniqPtrRefer].Delete();
        free(Referer);
    }
    if(Host)
    {
        Conn.UserData[&UniqPtrHst].Delete();
        free(Host);
    }
}

static void LQ_CALL CloseFn(LqHttpConn* NewConn)
{
    LqHttpConnInterface Conn(NewConn);
    void*StartLine = Conn.UserData[&UniqPtrStartLine],
        *UserAgent = Conn.UserData[&UniqPtrUserAgent],
        *Referer = Conn.UserData[&UniqPtrRefer],
        *Host = Conn.UserData[&UniqPtrHst];

    if(StartLine != nullptr)
        free(StartLine);
    if(UserAgent != nullptr)
        free(UserAgent);
    if(Referer != nullptr)
        free(Referer);
    if(Host != nullptr)
        free(Host);
}

LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{
    LqHttpMdlInit(Reg, &Mod, "Logging", ModuleHandle);
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
            LQSTR_CASE("log_in_file")
            {
                LqString Path = ReadPath(FullCommand);
                if(Path[0] == '\0')
                {
                    if(OutBuffer != nullptr)
                        fprintf(OutBuffer, " [Logging] ERROR: invalid syntax of command\n");
                    return;
                }
                if(OutFd != -1)
                {
                    if(OutBuffer != nullptr)
                        fprintf(OutBuffer, " [Logging] ERROR: Has been started\n");
                    return;
                }
                OutFd = LqFileOpen(Path.c_str(), LQ_O_APND | LQ_O_BIN | LQ_O_WR | LQ_O_CREATE | LQ_O_SEQ, 0666);
                if(OutFd == -1)
                {
                    if(OutBuffer != nullptr)
                        fprintf(OutBuffer, " [Logging] ERROR: Not open file for write (%s)\n", strerror(lq_errno));
                    break;
                }
                LqFileSeek(OutFd, 0, LQ_SEEK_END);

                LqHttpHndlsRegisterQuery(Mod.Proto, QueryFn);
                LqHttpHndlsRegisterResponse(Mod.Proto, ResponseFn);
                LqHttpHndlsRegisterDisconnect(Mod.Proto, CloseFn);

                if(OutBuffer != nullptr)
                    fprintf(OutBuffer, " [Logging] OK\n");
            }
            break;
            LQSTR_CASE("set_loop")
            {
                LqString LogLenStr = ReadPath(FullCommand);
                if(LogLenStr[0] == '\0')
                {
                    if(OutBuffer != nullptr)
                        fprintf(OutBuffer, " [Logging] %llu\n", (unsigned long long)LoopSize);
                    return;
                }
                LqFileSz LogLen = LqParseInt(LogLenStr);
                LoopSize = LogLen;
            }
            break;
            LQSTR_CASE("unset_loop")
            {
                LoopSize = 0xffffffffffff;
            }
            break;
            LQSTR_CASE("?")
            LQSTR_CASE("help")
            {
                if(OutBuffer)
                    fprintf
                    (
                    OutBuffer,
                    " [Logging]\n"
                    " Module: Logging\n"
                    " hotSAN 2016\n"
                    "  ? | help  - Show this help.\n"
                    "  log_in_file <Path to log file> - Start logging in file\n"
                    "  set_loop [<Max file size>] - Set max size log file\n"
                    "  unset_loop - Set \"infinity\" size of file\n"
                    );
            }
            break;
            LQSTR_SWITCH_DEFAULT
                if(OutBuffer != nullptr)
                    fprintf(OutBuffer, " [Logging] ERROR: invalid command\n");
        }

    };



    Mod.FreeNotifyProc =
    [](LqHttpMdl* This) -> uintptr_t
    {
        LqHttpHndlsUnregisterQuery(Mod.Proto, QueryFn);
        LqWrkBossEnumDelEvntByProto(
            (LqProto*)This->Proto,
            nullptr,
            [](void*, LqEvntHdr* Conn)
            {
                CloseFn((LqHttpConn*)Conn);
                return false;
            }
        );
        LqHttpHndlsUnregisterResponse(Mod.Proto, ResponseFn);
        LqHttpHndlsUnregisterDisconnect(Mod.Proto, CloseFn);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(OutFd != -1)
            LqFileClose(OutFd);
        return This->Handle;
    };

    return LQHTTPMDL_REG_OK;
}
