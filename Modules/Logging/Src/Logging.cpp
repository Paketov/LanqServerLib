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
#include "LqTime.hpp"
#include "LqStr.hpp"
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

static LqString ReadPath(LqString& Source) {
    if(Source[0] == '\"') {
        auto Off = Source.find('\"', 1);
        if(Off == LqString::npos)
            return "";
        auto ret = Source.substr(1, Off - 1);
        Source.erase(0, Off + 1);
        while((Source[0] == ' ') || ((Source[0] == '\t')))
            Source.erase(0, 1);
        return ret;
    } else {
        auto Off = Source.find_first_of("\t\r\n ");
        auto ret = Source.substr(0, Off);
        Source.erase(0, Off);
        while((Source[0] == ' ') || ((Source[0] == '\t')))
            Source.erase(0, 1);
        return ret;
    }
}

static LqString ReadParams(LqString& Source, const char * Params) {
    if((Source[0] == '-') && LqStrChr(Params, Source[1])) {
        LqString Res;
        Source.erase(0, 1);
        while((Source[0] != '\0') && (LqStrChr(Params, Source[0]) != nullptr)) {
            Res += Source[0];
            Source.erase(0, 1);
        }
        while((Source[0] == ' ') || ((Source[0] == '\t')))
            Source.erase(0, 1);
        return Res;
    }
    return "";
}

static void LQ_CALL RspFn(LqHttpConn* NewConn) {
    LqHttpConnInterface Conn(NewConn);

    char Buffer[32768];
    short TimeZone;
    auto CurTime = LqTimeGet(&TimeZone);
    TimeZone = -TimeZone;
    struct tm Tm;
    LqTimeLocSecToLocTm(&Tm, CurTime / 1000);
    Buffer[0] = Buffer[sizeof(Buffer) - 1] = '\0';
    int Written = LqFbuf_snprintf(
        Buffer,
        sizeof(Buffer) - 3,
        "%s - - [%02i/%s/%i:%02i:%02i:%02i +%02i%02i] \"%s %s%s%s%s%s HTTP/%i.%i\" %i %llu \"%s\" \"%s\" \"%s\"\r\n",
        ((LqString)Conn.RemoteIp).c_str(),
        (int)Tm.tm_mday,
        LqTimeMonths[Tm.tm_mon],
        (int)(Tm.tm_year + 1900),
        (int)Tm.tm_hour,
        (int)Tm.tm_min,
        (int)Tm.tm_sec,
        (int)(TimeZone / 60),
        (int)(TimeZone - ((TimeZone / 60) * 60)),
        ((LqString)Conn.Rcv.Method).c_str(),
        ((LqString)Conn.Rcv.Path).c_str(), (Conn.Rcv.Args == "")? "": "?", ((LqString)Conn.Rcv.Args).c_str(), (Conn.Rcv.Fragment == "") ? "" : "#", ((LqString)Conn.Rcv.Fragment).c_str(),
        (int)Conn.Rcv.MajorVer, (int)Conn.Rcv.MinorVer,
        (int)Conn.Rsp.Status,
        (unsigned long long)Conn.Rsp.ContentLen,
        (Conn.Rcv.Hdrs["Referer"] == "")? "-": ((LqString)Conn.Rcv.Hdrs["Referer"]).c_str(),
        (Conn.Rcv.Hdrs["User-Agent"] == "") ? "-" : ((LqString)Conn.Rcv.Hdrs["User-Agent"]).c_str(),
        (Conn.Rcv.Domen == "")? "-": ((LqString)Conn.Rcv.Domen).c_str()
    );
    if((LoopSize < 0xffffffffffff) && (LqFileTell(OutFd) >= LoopSize))
        LqFileSeek(OutFd, 0, LQ_SEEK_SET);
    LqFileWrite(OutFd, Buffer, Written);
}

LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttp* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData) {
    LqHttpMdlInit(Reg, &Mod, "Logging", ModuleHandle);
    Mod.ReciveCommandProc =
        [](LqHttpMdl* Mdl, const char* Command, void* Data) {
            LqFbuf* OutBuffer = nullptr;
            LqString FullCommand;
            if(Command[0] == '?') {
                FullCommand = Command + 1;
                OutBuffer = (LqFbuf*)Data;
            } else {
                FullCommand = Command;
            }
            LqString Cmd;
            while((FullCommand[0] != ' ') && (FullCommand[0] != '\0') && (FullCommand[0] != '\t')) {
                Cmd.append(1, FullCommand[0]);
                FullCommand.erase(0, 1);
            }
            while((FullCommand[0] == ' ') || (FullCommand[0] == '\t'))
                FullCommand.erase(0, 1);


            LQSTR_SWITCH(Cmd.c_str()) {
                LQSTR_CASE("log_in_file") {
                    LqString Path = ReadPath(FullCommand);
                    if(Path[0] == '\0') {
                        if(OutBuffer != nullptr)
                            LqFbuf_printf(OutBuffer, " [Logging] ERROR: invalid syntax of command\n");
                        return;
                    }
                    if(OutFd != -1) {
                        if(OutBuffer != nullptr)
                            LqFbuf_printf(OutBuffer, " [Logging] ERROR: Has been started\n");
                        return;
                    }
                    OutFd = LqFileOpen(Path.c_str(), LQ_O_APND | LQ_O_BIN | LQ_O_WR | LQ_O_CREATE | LQ_O_SEQ, 0666);
                    if(OutFd == -1) {
                        if(OutBuffer != nullptr)
                            LqFbuf_printf(OutBuffer, " [Logging] ERROR: Not open file for write (%s)\n", strerror(lq_errno));
                        break;
                    }
                    LqFileSeek(OutFd, 0, LQ_SEEK_END);

                    LqHttpHndlsRegisterResponse(Mod.HttpAcceptor, RspFn);

                    if(OutBuffer != nullptr)
                        LqFbuf_printf(OutBuffer, " [Logging] OK\n");
                }
                break;
                LQSTR_CASE("set_loop") {
                    LqString LogLenStr = ReadPath(FullCommand);
                    if(LogLenStr[0] == '\0') {
                        if(OutBuffer != nullptr)
                            LqFbuf_printf(OutBuffer, " [Logging] %llu\n", (unsigned long long)LoopSize);
                        return;
                    }
                    LqFileSz LogLen = LqParseInt(LogLenStr);
                    LoopSize = LogLen;
                }
                break;
                LQSTR_CASE("unset_loop")
                    LoopSize = 0xffffffffffff;
                break;
                LQSTR_CASE("?")
                    LQSTR_CASE("help") {
                    if(OutBuffer)
                        LqFbuf_printf(
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
                        LqFbuf_printf(OutBuffer, " [Logging] ERROR: invalid command\n");
            }

    };

    Mod.FreeNotifyProc =
    [](LqHttpMdl* This) -> uintptr_t {
        LqHttpHndlsUnregisterResponse(Mod.HttpAcceptor, RspFn);
        LqWrkBoss::GetGlobal()->EnumClientsAndCallFinAsync11(
            nullptr,
            std::bind(
                [](uintptr_t Handle) -> uintptr_t {
                    if(OutFd != -1)
                        LqFileClose(OutFd);
                    return Handle;
                },
                This->Handle
            )
        );
        return 0;
    };

    return LQHTTPMDL_REG_OK;
}
