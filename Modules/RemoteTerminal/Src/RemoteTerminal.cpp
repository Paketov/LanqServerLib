/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   MdlAutoLoad... - Auto load/unload modules from spec. dir.
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

#include <vector>
#include <sstream>
#include <thread>


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
	LqHttpConnInterface(c).EvntHandler.Ignore();
}
struct CmdSession;


LqHttpMdl Mod;
LqTimeMillisec SessionTimeLife = 12000;
size_t MaxCountSessions = 50;


class _Sessions
{
    std::vector<CmdSession*> Data;
    int LastEmpty;
    mutable LqLocker<uintptr_t> Lk;

public:
	
	_Sessions()
    {
        LastEmpty = -1;
    }

    ~_Sessions()
    {
        LastEmpty = -1;
    }

	size_t Add(CmdSession* Session);

	void Remove(CmdSession* Session);

	CmdSession* Get(size_t Index, LqString Key) const;

    size_t Count() const
    {
        return Data.size();
    }

} Sessions;

struct CmdSession
{
    LqEvntFd                                                    TimerFd;
    LqEvntFd                                                    ReadFd;

    volatile size_t                                             CountPointers;
    LqLocker<intptr_t>                                          Locker;
    int                                                         InFd;
    int                                                         OutFd;
    int                                                         Pid;
    LqString                                                    Key;
    int                                                         Index;

    LqHttpConn*                                                 Conn;
	LqTimeMillisec												LastAct;

    inline void LockRead()
    {
        Locker.LockReadYield();
    }

    inline void LockWrite()
    {
        Locker.LockWriteYield();
    }

    inline void Unlock()
    {
        Locker.Unlock();
    }

    CmdSession(int NewStdIn, int NewStdOut, int NewPid, LqString& NewKey, void* WrkBoss):
        InFd(NewStdIn),
        OutFd(NewStdOut),
        Pid(NewPid),
        Key(NewKey),
        CountPointers(0),
        Conn(nullptr)
    {
		LastAct = LqTimeGetLocMillisec();
        auto Event = LqFileTimerCreate(LQ_O_NOINHERIT);
        LqFileTimerSet(Event, SessionTimeLife);

        LqEvntFdInit(&TimerFd, Event, WrkBoss, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
        TimerFd.Handler = TimerHandler;
        TimerFd.CloseHandler = TimerHandlerClose;
        TimerFd.UserData = (uintptr_t)&TimerFd - (uintptr_t)this;
        CountPointers++;
        LqEvntFdAdd(&TimerFd);


        ReadFd.Boss = WrkBoss;
        Sessions.Add(this);
    }

    ~CmdSession()
    {
        Sessions.Remove(this);
        if(Pid != -1)
            LqFileProcessKill(Pid);
        if(InFd != -1)
            LqFileClose(InFd);
        if(OutFd != -1)
            LqFileClose(OutFd);
    }


	static void TimerHandler(LqEvntFd* Instance, LqEvntFlag Flags);

    static void TimerHandlerClose(LqEvntFd* Instance, LqEvntFlag Flags)
    {
        auto Ob = (CmdSession*)((char*)Instance - Instance->UserData);
        Ob->LockWrite();
        LqFileClose(Ob->TimerFd.Fd);
        Ob->TimerFd.Fd = -1;
        Ob->CountPointers--;
        bool Del = Ob->CountPointers <= 0;
        Ob->Unlock();
        if(Del)
            LqFastAlloc::Delete(Ob);
    }


    bool StartRead(LqHttpConn* c)
    {
        bool Res = false;
        if(Conn == nullptr)
        {
            Conn = c;
            LqEvntFdInit(&ReadFd, OutFd, ReadFd.Boss, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP);
            ReadFd.Handler = ReadHandler;
            ReadFd.CloseHandler = ReadHandlerClose;
            ReadFd.UserData = (uintptr_t)&ReadFd - (uintptr_t)this;

            LqEvntFdAdd(&ReadFd);
			CountPointers += 2;
			c->ModuleData = (uintptr_t)this;
			LastAct = LqTimeGetLocMillisec();
            Res = true;
        }
        return Res;
    }

	static void EndRead(LqHttpConn* c)
	{
		auto Ob = (CmdSession*)(c->ModuleData);
	    Ob->LockWrite();
		if(Ob->Conn != nullptr)
		{
			Ob->Conn = nullptr;
			LqEvntSetClose(&Ob->ReadFd);
		}
		Ob->LastAct = LqTimeGetLocMillisec();
		Ob->CountPointers--;
		bool Del = Ob->CountPointers <= 0;
		Ob->Unlock();
		if(Del)
			LqFastAlloc::Delete(Ob);
	}

	static void ReadHandler(LqEvntFd* Instance, LqEvntFlag Flags);

    static void ReadHandlerClose(LqEvntFd* Instance, LqEvntFlag Flags)
    {
        auto Ob = (CmdSession*)((char*)Instance - Instance->UserData);
        Ob->LockWrite();
        Ob->CountPointers--;
        bool Del = Ob->CountPointers <= 0;
        Ob->Unlock();
        if(Del)
            LqFastAlloc::Delete(Ob);
    }
};



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

struct ConnHandlers
{
    static void LQ_CALL NewTerminal(LqHttpConn* c)
    {
        if(Sessions.Count() > MaxCountSessions)
        {
            Response(c, 500, "Overflow limit of sessions");
            return;
        }

        LqFileStio StdIo;
        StdIo.Flags = LQ_P_STD_IN_NON_BLOCK | LQ_P_STD_OUT_NON_BLOCK | LQ_P_STD_ERR_NON_BLOCK | LQ_P_STD_ERR_OUT;


        int Pid = LqFileProcessCreate("sh", nullptr, nullptr, &StdIo, nullptr);
        if(Pid == -1)
        {
            Response(c, 500, (LqString("Not create terminal session. ") + strerror(lq_errno)).c_str());
            return;
        }
        
        int Key[5];
        char KeyBuffer[50] = {0};
        for(int i = 0; i < 4; i++)
            Key[i] = rand() + clock() * 10000;
        LqStrToHex(KeyBuffer, Key, sizeof(Key) - sizeof(int));

        auto Ses = LqFastAlloc::New<CmdSession>(StdIo.StdIn, StdIo.StdOut, Pid, LqString(KeyBuffer), LqEvntBossByHdr(c));
        LqString LqResponse;
        LqResponse = "{\"Key\": \"" + LqString(KeyBuffer) + "\", \"SessionIndex\": " + LqToString(Ses->Index) + "}";
        Response(c, 200, LqResponse.c_str());
    }

    static void LQ_CALL CloseTerminal(LqHttpConn* c)
    {
        LqHttpConnInterface Conn(c);
        int Index = -1;
        LqStrToInt(&Index, Conn.Quer.Args["SessionIndex"].c_str());


        auto Ses = Sessions.Get(Index, Conn.Quer.Args["Key"]);
        if(Ses == nullptr)
        {
            Response(c, 500, "Invalid key or index of session.");
            return;
        }

        Sessions.Remove(Ses);
        LqEvntSetClose(&Ses->TimerFd);
        Response(c, 200, "OK");
        Ses->Unlock();
    }

    static void LQ_CALL Write(LqHttpConn* c)
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

    static void LQ_CALL Write2(LqHttpConn* c)
    {
        LqHttpConnInterface Conn(c);
        int Index = -1;
        LqStrToInt(&Index, Conn.Quer.Args["SessionIndex"].c_str());


        auto Ses = Sessions.Get(Index, Conn.Quer.Args["Key"]);
        if(Ses == nullptr)
        {
            Response(c, 500, "Invalid key or index of session.");
            return;
        }
        char Buf[4096];
        auto Readed = Conn.Quer.Stream.Read(Buf, sizeof(Buf));

        auto Written = LqFileWrite(Ses->InFd, Buf, Readed);
        Ses->Unlock();

		if((Written > -1) || LQERR_IS_WOULD_BLOCK)
		{
			Response(c, 200, (LqString("Written: ") + LqToString(Written)).c_str());
		} else
		{
			Response(c, 500, (LqString("Terminal error. ") + strerror(lq_errno)).c_str());
		}
        
    }


    static void LQ_CALL Read(LqHttpConn* c)
    {
        LqHttpConnInterface Conn(c);
        int Index = -1;
        LqStrToInt(&Index, Conn.Quer.Args["SessionIndex"].c_str());

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

	static void LQ_CALL ReadClose(LqHttpConn* c)
	{
		CmdSession::EndRead(c);
	}

};


void CmdSession::TimerHandler(LqEvntFd* Instance, LqEvntFlag Flags)
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
			Response(Ob->Conn, 408, "Time out. Send query again.");
			Ob->Conn->EventAct = ConnHandlers::ReadClose;
			LqHttpEvntSetFlagByAct(Ob->Conn);
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

void CmdSession::ReadHandler(LqEvntFd* Instance, LqEvntFlag Flags)
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
				Response(Ob->Conn, 500, "Not get data from session.");
				Ob->Conn->EventAct = ConnHandlers::ReadClose;
				LqHttpEvntSetFlagByAct(Ob->Conn);
				Ob->Unlock();
				return;
			}
			Conn.Rsp.MakeStatus(200);
			Conn.Rsp.Stream.Write(Buf, Readed);
			Conn.Rsp.Hdrs["Content-Length"] = LqToString(Readed);
			Conn.Rsp.Hdrs["Cache-Control"] = "no-cache";
			Conn.Rsp.Hdrs["Content-Type"] = "text/plain";
			Ob->Conn->EventAct = ConnHandlers::ReadClose;
			LqHttpEvntSetFlagByAct(Ob->Conn);
		}
		Ob->Unlock();
	}
	LqEvntSetClose(&Ob->ReadFd);
}


LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{
    LqHttpMdlInit(Reg, &Mod, "RemoteTerminal", ModuleHandle);

    Mod.ReciveCommandProc =
        [](LqHttpMdl* Mdl, const char* Command, void* Data)
    {
    };

	auto Atz = LqHttpAtzCreate(LQHTTPATZ_TYPE_DIGEST, "main");
	LqHttpAtzAdd(Atz,
				 LQHTTPATZ_PERM_CHECK | LQHTTPATZ_PERM_CREATE | LQHTTPATZ_PERM_CREATE_SUBDIR | LQHTTPATZ_PERM_DELETE | LQHTTPATZ_PERM_WRITE | LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_MODIFY,
				 "Admin", "Password");

    LqHttpPthRegisterExecFile
    (
        Reg,
        &Mod,
        "*",
        "/RemoteTerminal/New",
		ConnHandlers::NewTerminal,
        0,
		Atz,
        0
    );

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


	LqHttpPthRegisterFile
	(
		Reg,
		&Mod,
		"*",
		"/RemoteTerminal",
		"/sdcard/serv/HtmlConsole.html",
		LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
		nullptr,
		0
	);


    Mod.FreeNotifyProc =
        [](LqHttpMdl* This) -> uintptr_t
    {
        printf("Unload RemoteTerminal module\n");
        return This->Handle;
    };
    //Add task to worker boss
    return LQHTTPMDL_REG_OK;
}





#define __METHOD_DECLS__
#include "LqAlloc.hpp"

