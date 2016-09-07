/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   RemoteTerminal - Create terminal sessions.
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

#define __METHOD_DECLS__
#include "LqAlloc.hpp"


#include "RemoteTerminal.h"


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

static void Response(LqHttpConn* c, int Code, const char* Content)
{
    LqHttpConnInterface Conn = c;
    Conn.Rsp.MakeStatus(Code);
    auto l = LqStrLen(Content);
    Conn.Rsp.Hdrs["Content-Length"] = LqToString(l);
    Conn.Rsp.Hdrs["Cache-Control"] = "no-cache";
    Conn.Rsp.Hdrs["Content-Type"] = "text/plain";
    Conn.Rsp.Hdrs.AppendSmallContent(Content, l);
    Conn.EvntHandler.Ignore();
}


LqHttpMdl Mod;
LqHttpProtoBase* Proto;
LqTimeMillisec SessionTimeLife = 12000;
size_t MaxCountSessions = 50;



CmdSession::CmdSession(int NewStdIn, int NewPid, LqString& NewKey):
	MasterFd(NewStdIn),
	Pid(NewPid),
	Key(NewKey),
	CountPointers(0),
	Conn(nullptr)
{
	LastAct = LqTimeGetLocMillisec();
	auto Event = LqFileTimerCreate(LQ_O_NOINHERIT);
	LqFileTimerSet(Event, SessionTimeLife);

	LqEvntFdInit(&TimerFd, Event, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
	TimerFd.Handler = TimerHandler;
	TimerFd.CloseHandler = TimerHandlerClose;
	TimerFd.UserData = (uintptr_t)&TimerFd - (uintptr_t)this;
	LqObReference(this);
	LqEvntFdAdd(&TimerFd);
	Sessions.Add(this);
}

CmdSession::~CmdSession()
{
	Sessions.Remove(this);
	if(Pid != -1)
		LqFileProcessKill(Pid);
	if(MasterFd != -1)
		LqFileClose(MasterFd);
}

void LQ_CALL CmdSession::TimerHandlerClose(LqEvntFd* Instance, LqEvntFlag Flags)
{
	auto Ob = (CmdSession*)((char*)Instance - Instance->UserData);
	Ob->LockWrite();
	LqFileClose(Ob->TimerFd.Fd);
	Ob->TimerFd.Fd = -1;
	Ob->Unlock();
	LqObDereference<CmdSession, LqFastAlloc::Delete>(Ob);
}


bool CmdSession::StartRead(LqHttpConn* c)
{
	bool Res = false;
	if(Conn == nullptr)
	{
		Conn = c;
		LqEvntFdInit(&ReadFd, MasterFd, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
		ReadFd.Handler = ReadHandler;
		ReadFd.CloseHandler = ReadHandlerClose;
		ReadFd.UserData = (uintptr_t)&ReadFd - (uintptr_t)this;

		LqEvntFdAdd(&ReadFd);
		LqObReference(this);
		LqObReference(this);
		c->ModuleData = (uintptr_t)this;
		LastAct = LqTimeGetLocMillisec();
		Res = true;
	}
	return Res;
}

void CmdSession::EndRead(LqHttpConn* c)
{
	auto Ob = (CmdSession*)(c->ModuleData);
	Ob->LockWrite();

	Ob->LastAct = LqTimeGetLocMillisec();
	if(Ob->Conn != nullptr)
	{
		Ob->Conn = nullptr;
		LqEvntSetClose(&Ob->ReadFd);
	}
	Ob->Unlock();
	LqObDereference<CmdSession, LqFastAlloc::Delete>(Ob);
}

void LQ_CALL CmdSession::ReadHandlerClose(LqEvntFd* Instance, LqEvntFlag Flags)
{
	auto Ob = (CmdSession*)((char*)Instance - Instance->UserData);
	LqObDereference<CmdSession, LqFastAlloc::Delete>(Ob);
}

size_t _Sessions::Add(CmdSession* Session)
{
    size_t Ret = 0;
    Lk.LockWriteYield();
    if(LastEmpty == -1)
    {
        Data.push_back(Session);
        Ret = Data.size() - 1;
    } else
    {
        Data[Ret = LastEmpty] = Session;
        LastEmpty = -1;
        for(auto i = LastEmpty; i < Data.size(); i++)
        {
            if(Data[i] == nullptr)
            {
                LastEmpty = i;
                break;
            }
        }
    }

    Session->Index = Ret;
    Lk.UnlockWrite();
    return Ret;
}

void _Sessions::Remove(CmdSession* Session)
{
    Lk.LockWriteYield();
    if((Session->Index < Data.size()) && (Data[Session->Index] == Session))
    {
        Data[Session->Index] = nullptr;
        if((LastEmpty > Session->Index) || (LastEmpty == -1))
            LastEmpty = Session->Index;
        int i;
        for(i = Data.size() - 1; i >= 0; i--)
        {
            if(Data[i] != nullptr)
                break;
        }
        Data.resize(i + 1);
        if(LastEmpty >= Data.size())
            LastEmpty = -1;
    }
    Lk.UnlockWrite();
}

CmdSession* _Sessions::Get(size_t Index, LqString Key) const
{
    CmdSession* Ret = nullptr;
    Lk.LockReadYield();
    if((Index < Data.size()) && (Data[Index]->Key == Key))
        Ret = Data[Index];

    if(Ret != nullptr)
        Ret->LockWrite();
    Lk.UnlockRead();
    return Ret;
}


void LQ_CALL ConnHandlers::NewTerminal(LqHttpConn* c)
{
    if(Sessions.Count() > MaxCountSessions)
    {
        Response(c, 500, "Overflow limit of sessions");
        return;
    }

    int SlaveFd = -1, MasterFd = -1;
    //Create terminal fd
    if(LqFileTermPairCreate(&MasterFd, &SlaveFd, LQ_O_NONBLOCK | LQ_O_NOINHERIT, 0) == -1)
    {
        Response(c, 500, (LqString("Not create terminal session. ") + strerror(lq_errno)).c_str());
        return;
    }
#if defined(LQPLATFORM_WINDOWS)
    const char* TerminalShell = "cmd";
    char ** Arg = nullptr;
#else
    const char* TerminalShell = "sh";
    char * Arg[] = {"-", nullptr};
#endif
    int Pid = LqFileProcessCreate(TerminalShell, Arg, nullptr, nullptr, SlaveFd, SlaveFd, SlaveFd, nullptr);
    LqFileClose(SlaveFd);
    if(Pid == -1)
    {
        LqFileClose(MasterFd);
        Response(c, 500, (LqString("Not create terminal session.") + strerror(lq_errno)).c_str());
        return;
    }

    int Key[5];
    char KeyBuffer[50] = {0};
    for(int i = 0; i < 4; i++)
        Key[i] = rand() + clock() * 10000;
    LqStrToHex(KeyBuffer, Key, sizeof(Key) - sizeof(int));

    auto Ses = LqFastAlloc::New<CmdSession>(MasterFd, Pid, LqString(KeyBuffer));
    LqString LqResponse;
    LqResponse = "{\"Key\": \"" + LqString(KeyBuffer) + "\", \"SessionIndex\": " + LqToString(Ses->Index) + "}";
    Response(c, 200, LqResponse.c_str());
}

void LQ_CALL ConnHandlers::CloseTerminal(LqHttpConn* c)
{
    LqHttpConnInterface Conn(c);
    int Index = LqParseInt(Conn.Quer.Args["SessionIndex"]);


    auto Ses = Sessions.Get(Index, Conn.Quer.Args["Key"]);
    if(Ses == nullptr)
    {
        Response(c, 500, "Invalid key or index of session.");
        return;
    }

    Sessions.Remove(Ses);
    Response(c, 200, "OK");
    LqEvntSetClose(&Ses->TimerFd);
    Ses->Unlock();
}

void LQ_CALL ConnHandlers::Write(LqHttpConn* c)
{
    LqHttpConnInterface Conn(c);
    if(Conn.Quer.ContentLen > 4096)
    {
        Response(c, 500, "Limit overflow.");
        return;
    }
    Conn.Quer.Stream.Set();
    Conn.EvntHandler = Write2;
}

void LQ_CALL ConnHandlers::Write2(LqHttpConn* c)
{
    LqHttpConnInterface Conn(c);
    int Index = LqParseInt(Conn.Quer.Args["SessionIndex"]);

    auto Ses = Sessions.Get(Index, Conn.Quer.Args["Key"]);
    if(Ses == nullptr)
    {
        Response(c, 500, "Invalid key or index of session.");
        return;
    }
    char Buf[4096];
    auto Readed = Conn.Quer.Stream.Read(Buf, sizeof(Buf));

    auto Written = LqFileWrite(Ses->MasterFd, Buf, Readed);
    Ses->Unlock();

    if((Written > -1) || LQERR_IS_WOULD_BLOCK)
    {
        Response(c, 200, (LqString("Written: ") + LqToString(Written)).c_str());
    } else
    {
        Response(c, 500, (LqString("Terminal error. ") + strerror(lq_errno)).c_str());
    }

}


void LQ_CALL ConnHandlers::Read(LqHttpConn* c)
{
    LqHttpConnInterface Conn(c);
    int Index = LqParseInt(Conn.Quer.Args["SessionIndex"]);

    auto Ses = Sessions.Get(Index, Conn.Quer.Args["Key"]);
    if(Ses == nullptr)
    {
        Response(c, 500, "Invalid key or index of session.");
        return;
    }


    if(!Ses->StartRead(c))
    {
        Response(c, 500, "Not read from termminal.");
    } else
    {
        Conn.EvntFlag = LQEVNT_FLAG_HUP;
        Conn.CloseHandler = ReadClose;
    }
    Ses->Unlock();
}

void LQ_CALL ConnHandlers::ReadClose(LqHttpConn* c)
{
    LqHttpConnInterface Conn(c);
    CmdSession::EndRead(c);
    Conn.EvntHandler.Ignore();
    Conn.CloseHandler.Ignore();
}



void LQ_CALL CmdSession::TimerHandler(LqEvntFd* Instance, LqEvntFlag Flags)
{
    auto Ob = (CmdSession*)((char*)Instance - Instance->UserData);
    auto CurTime = LqTimeGetLocMillisec();
    bool End = false;

    LqFileTimerSet(Ob->TimerFd.Fd, 3000);
    Ob->LockWrite();
    if((CurTime - Ob->LastAct) > SessionTimeLife)
    {
        if(Ob->Conn != nullptr)
        {
            LqHttpConnInterface Conn = Ob->Conn;
            Response(Conn.Row, 408, "Time out. Send query again.");
            Conn.EvntHandler = ConnHandlers::ReadClose;
            Conn.EvntFlag.ReturnToDefault();
            Ob->Unlock();
            return;
        } else
        {
            End = true;
        }
    }
    Ob->Unlock();
    if(End)
    {
        Ob->TimerFd.Handler = [](LqEvntFd*, LqEvntFlag) {};
        LqEvntSetClose(&Ob->TimerFd);
    }
}

void LQ_CALL CmdSession::ReadHandler(LqEvntFd* Instance, LqEvntFlag Flags)
{
    auto Ob = (CmdSession*)((char*)Instance - Instance->UserData);

    if(Flags & LQEVNT_FLAG_RD)
    {
        Ob->LockWrite();
        if(Ob->Conn != nullptr)
        {
            LqHttpConnInterface Conn = Ob->Conn;

            char Buf[4096];
            auto Readed = LqFileRead(Ob->ReadFd.Fd, Buf, sizeof(Buf) - 1);
            if(Readed == -1)
            {
                Response(Conn.Row, 500, "Not get data from session.");
                Conn.EvntHandler = ConnHandlers::ReadClose;
                Conn.EvntFlag.ReturnToDefault();
                Ob->Unlock();
                return;
            }
            Conn.Rsp.MakeStatus(200);
            Conn.Rsp.Stream.Write(Buf, Readed);
            Conn.Rsp.Hdrs["Content-Length"] = LqToString(Readed);
            Conn.Rsp.Hdrs["Cache-Control"] = "no-cache";
            Conn.Rsp.Hdrs["Content-Type"] = "text/plain";

            Conn.EvntHandler = ConnHandlers::ReadClose;
            Conn.EvntFlag.ReturnToDefault();
        }
        Ob->Unlock();
    }
    LqEvntSetClose(&Ob->ReadFd);
}


LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{
	Proto = Reg;
    LqHttpMdlInit(Reg, &Mod, "RemoteTerminal", ModuleHandle);

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
			LQSTR_CASE("start")
			{
				/*start <Path to html> <Type of auth -d - Digest, -b - basic> <User> <Password>*/
				LqString Path = ReadPath(FullCommand);
				LqString AuthType = ReadPath(FullCommand);
				LqString User = ReadPath(FullCommand);
				LqString Password = ReadPath(FullCommand);
				if(
					(Path[0] == '\0') || 
					(AuthType[0] != '-') || ((AuthType[1] != 'b') && (AuthType[1] != 'd')) ||
					(User[0] == '\0') ||
					(Password[0] == '\0')
				)
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [RemoteTerminal] Error: invalid syntax of command\n");
					return;
				}

				auto c = LqHttpAtzCreate(LQHTTPATZ_TYPE_BASIC, "Lanq Remote Terminal");
				LqHttpAtzAdd(c,
							 LQHTTPATZ_PERM_CHECK | 
							 LQHTTPATZ_PERM_CREATE | 
							 LQHTTPATZ_PERM_CREATE_SUBDIR |
							 LQHTTPATZ_PERM_READ |
							 LQHTTPATZ_PERM_WRITE |
							 LQHTTPATZ_PERM_MODIFY,
							 User.c_str(),
							 Password.c_str());

				if(LqHttpPthRegisterFile
				(
				   Proto,
				   &Mod,
				   "*",
				   "/RemoteTerminal",
				   Path.c_str()
				   , LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
				   nullptr,
				   0
				   ) != LQHTTPPTH_RES_OK)
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [RemoteTerminal] Error: not create path entry for html page\n");
					return;
				}


				if(LqHttpPthRegisterExecFile
				(
				   Proto,
				   &Mod,
				   "*",
				   "/RemoteTerminal/New",
				   ConnHandlers::NewTerminal,
				   0,
				   c,
				   0
				   ) != LQHTTPPTH_RES_OK)
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [RemoteTerminal] Error: not create path entry for creating shell\n");
					return;
				}
				if(OutBuffer != nullptr)
					fprintf(OutBuffer, " [RemoteTerminal] OK\n");
			}
			break;
			LQSTR_CASE("?")
			LQSTR_CASE("help")
			{
				if(OutBuffer)
					fprintf
					(
					OutBuffer,
					" [RemoteTerminal]\n"
					" Module: RemoteTerminal\n"
					" hotSAN 2016\n"
					"  ? | help  - Show this help.\n"
					"  start <Path to html page> <Type of auth -d - Digest, -b - basic> <User> <Password> - Start hosting sessions\n"
					);
			}
			break;
			LQSTR_SWITCH_DEFAULT
				if(OutBuffer != nullptr)
					fprintf(OutBuffer, " [RemoteTerminal] Error: invalid command\n");
		}

    };

    LqHttpPthRegisterExecFile
    (
        Reg,
        &Mod,
        "*",
        "/RemoteTerminal/Close",
        ConnHandlers::CloseTerminal,
        LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );

    LqHttpPthRegisterExecFile
    (
        Reg,
        &Mod,
        "*",
        "/RemoteTerminal/In",
        ConnHandlers::Write,
        LQHTTPATZ_PERM_WRITE | LQHTTPATZ_PERM_MODIFY | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );

    LqHttpPthRegisterExecFile
    (
        Reg,
        &Mod,
        "*",
        "/RemoteTerminal/Out",
        ConnHandlers::Read,
        LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );


    Mod.FreeNotifyProc =
        [](LqHttpMdl* This) -> uintptr_t
    {
        return This->Handle;
    };
    //Add task to worker boss
    return LQHTTPMDL_REG_OK;
}
