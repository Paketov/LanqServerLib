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
#include "LqTime.hpp"
#include "LqStr.hpp"
#include "LqHttp.hpp"
#include "LqHttpAtz.h"
#include "LqShdPtr.hpp"
#include "LqPtdArr.hpp"

#define __METHOD_DECLS__
#include "LqAlloc.hpp"


#include "RemoteTerminal.hpp"


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

LqHttpMdl Mod;
LqHttp* Proto;
typedef LqShdPtr<CmdSession, LqFastAlloc::Delete, true, true> SesPtr;
LqPtdArr<SesPtr> Sessions;
std::atomic<int> IndexCounter = {0};

LqTimeMillisec SessionTimeLife = 12000;
size_t MaxCountSessions = 50;
static char ModUnique;


SesPtr SearchSess(int Index, LqString Key) {
	for(auto& i : Sessions) {
		if((i->Index == Index) && (i->Key == Key))
			return i;
	}
	return nullptr;
}


CmdSession::CmdSession(int NewStdIn, int NewPid, LqString& NewKey):
    MasterFd(NewStdIn),
    Pid(NewPid),
    Key(NewKey),
    CountPointers(0),
    Conn(nullptr) 
{
	Index = ::IndexCounter++;
    LastAct = LqTimeGetLocMillisec();
    auto Event = LqTimerCreate(LQ_O_NOINHERIT);
    LqTimerSet(Event, SessionTimeLife);
    LqEvntFdInit(&TimerFd, Event, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP, CmdSession::TimerHandler, CmdSession::TimerHandlerClose);
    LqClientAdd(&TimerFd, NULL);
	LqObPtrReference(this);
	LqEvntFdInit(&ReadFd, MasterFd, LQEVNT_FLAG_HUP, CmdSession::ReadHandler, CmdSession::ReadHandlerClose);
	LqClientAdd(&ReadFd, NULL);
	LqObPtrReference(this);
    Sessions.push_back(SesPtr(this));
}

CmdSession::~CmdSession() {
    if(Pid != -1)
        LqProcessKill(Pid);
    if(MasterFd != -1)
        LqFileClose(MasterFd);
	if(TimerFd.Fd != -1)
		LqFileClose(TimerFd.Fd);
}

void LQ_CALL CmdSession::TimerHandlerClose(LqEvntFd* Instance) {
	SesPtr Ob = (CmdSession*)((char*)Instance - ((uintptr_t)&((CmdSession*)0)->TimerFd));
	Ob->LockWrite();
	if(Ob->Conn != nullptr) {
		LqHttpConnInterface Conn = Ob->Conn;
		Conn << "Time out. Send query again.";
		Conn.Rsp.Status = 408;
		Conn.EndLongPoll();
		Ob->Conn = nullptr;
		LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
	}
	if(Ob->TimerFd.Fd != -1) {
		LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
	}
	LqClientSetClose(&Ob->ReadFd);
	Sessions.remove_mult_by_val(Ob);
	Ob->Unlock();
}

void LQ_CALL CmdSession::ReadHandlerClose(LqEvntFd* Instance) {
	SesPtr Ob = (CmdSession*)((char*)Instance - ((uintptr_t)&((CmdSession*)0)->ReadFd));
	Ob->LockWrite();
	if(Ob->Conn != nullptr) {
		LqHttpConnInterface Conn = Ob->Conn;
		Conn << "Time out. Send query again.";
		Conn.Rsp.Status = 408;
		Conn.EndLongPoll();
		Ob->Conn = nullptr;
		LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
	}
	if(Ob->ReadFd.Fd != -1) {
		LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
	}
	LqClientSetClose(&Ob->TimerFd);
	Sessions.remove_mult_by_val(Ob);
	Ob->Unlock();
}

void LQ_CALL CmdSession::TimerHandler(LqEvntFd* Instance, LqEvntFlag Flags) {
	SesPtr Ob = (CmdSession*)((char*)Instance - ((uintptr_t)&((CmdSession*)0)->TimerFd));
    auto CurTime = LqTimeGetLocMillisec();
    bool End = false;

	Ob->LockWrite();
    LqTimerSet(Ob->TimerFd.Fd, 3000);
    if((CurTime - Ob->LastAct) > SessionTimeLife) {
		if(Ob->Conn != nullptr) {
			LqHttpConnInterface Conn = Ob->Conn;
			Conn << "Time out. Send query again.";
			Conn.Rsp.Status = 408;
			Conn.EndLongPoll();
			Ob->Conn = nullptr;
			LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
			LqClientSetFlags(&Ob->ReadFd, LQEVNT_FLAG_HUP, 0);
			Ob->Unlock();
			return;
		} else {
			End = true;
		}
    }
    Ob->Unlock();
    if(End) {
        Ob->TimerFd.Handler = [](LqEvntFd*, LqEvntFlag) {};
        LqClientSetClose(&Ob->TimerFd);
    }
}

void LQ_CALL CmdSession::ReadHandler(LqEvntFd* Instance, LqEvntFlag Flags) {
	SesPtr Ob = (CmdSession*)((char*)Instance - ((uintptr_t)&((CmdSession*)0)->ReadFd));

	Ob->LockWrite();
	if(Flags & LQEVNT_FLAG_RD) {
		if(Ob->Conn != nullptr) {
			LqHttpConnInterface Conn = Ob->Conn;
			char Buf[4096];
			auto Readed = LqFileRead(Ob->ReadFd.Fd, Buf, sizeof(Buf) - 1);
			if(Readed == -((intptr_t)1)) {
				Conn << "Not get data from session.";
				Conn.Rsp.Status = 500;
			} else {
				Conn.Rsp.Hdrs["Content-Type"] = "text/plain";
				Conn.Write(Buf, Readed);
			}
			Conn.EndLongPoll();
			Ob->LastAct = LqTimeGetLocMillisec();
			Ob->Conn = nullptr;
			LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
		}
	} else {
		if(Ob->Conn != nullptr) {
			LqHttpConnInterface Conn = Ob->Conn;
			Conn << "Not get data from session.";
			Conn.Rsp.Status = 500;
			Conn.EndLongPoll();
			Ob->Conn = nullptr;
			LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ob.Get());
		}
	}
	LqClientSetFlags(&Ob->ReadFd, LQEVNT_FLAG_HUP, 0);
	Ob->Unlock();
}

LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttp* Http, uintptr_t ModuleHandle, const char* LibPath, void* UserData) {
    Proto = Http;
    LqHttpMdlInit(Http, &Mod, "RemoteTerminal", ModuleHandle);

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
            LQSTR_CASE("start") {
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
                    ) {
                    if(OutBuffer != nullptr)
                        LqFbuf_printf(OutBuffer, " [RemoteTerminal] ERROR: invalid syntax of command\n");
                    return;
                }

                auto c = LqHttpAtzCreate((AuthType[1] == 'b') ? LQHTTPATZ_TYPE_BASIC : LQHTTPATZ_TYPE_DIGEST, "Lanq Remote Terminal");
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
                 ) != LQHTTPPTH_RES_OK) {
                    if(OutBuffer != nullptr)
                        LqFbuf_printf(OutBuffer, " [RemoteTerminal] ERROR: not create path entry for html page\n");
                    return;
                }


                if(LqHttpPthRegisterExecFile(
                    Proto,
                    &Mod,
                    "*",
                    "/RemoteTerminal/New",
					[](LqHttpConn* c) {
						LqHttpConnInterface Conn = c;
						if(Sessions.size() > MaxCountSessions) {
							Conn.Rsp.Status = 500;
							Conn << "Overflow limit of sessions";
							return;
						}

						int SlaveFd = -1, MasterFd = -1;
						//Create terminal fd
						if(LqTermPairCreate(&MasterFd, &SlaveFd, LQ_O_NONBLOCK | LQ_O_NOINHERIT, 0) == -1) {
							Conn.Rsp.Status = 500;
							Conn << "Not create terminal session. " << strerror(lq_errno);
							return;
						}
#if defined(LQPLATFORM_WINDOWS)
						const char* TerminalShell = "cmd";
						char ** Arg = nullptr;
#else
						const char* TerminalShell = "sh";
						char * Arg[] = {"-", nullptr};
#endif
						int Pid = LqProcessCreate(TerminalShell, Arg, nullptr, nullptr, SlaveFd, SlaveFd, SlaveFd, nullptr, false);
						LqFileClose(SlaveFd);
						if(Pid == -1) {
							LqFileClose(MasterFd);
							Conn.Rsp.Status = 500;
							Conn << "Not create terminal session. " << strerror(lq_errno);
							return;
						}

						int Key[5];
						char KeyBuffer[50] = {0};
						for(int i = 0; i < 4; i++)
							Key[i] = rand() + clock() * 10000;
						LqFbuf_snprintf(KeyBuffer, sizeof(KeyBuffer), "%.*v", (int)(sizeof(Key) - sizeof(int)), Key);
						auto Ses = LqFastAlloc::New<CmdSession>(MasterFd, Pid, LqString(KeyBuffer));

						Conn.Rsp.Hdrs["Content-Type"] = "application/json";
						Conn << "{\"Key\": \"" << KeyBuffer << "\", \"SessionIndex\": " << Ses->Index << "}";
					},
                    0,
                    c,
                    0
                    ) != LQHTTPPTH_RES_OK) {
                    if(OutBuffer != nullptr)
                        LqFbuf_printf(OutBuffer, " [RemoteTerminal] ERROR: not create path entry for creating shell\n");
                    return;
                }
                if(OutBuffer != nullptr)
                    LqFbuf_printf(OutBuffer, " [RemoteTerminal] OK\n");
            }
            break;
            LQSTR_CASE("?")
                LQSTR_CASE("help") {
                if(OutBuffer)
                    LqFbuf_printf
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
                    LqFbuf_printf(OutBuffer, " [RemoteTerminal] ERROR: invalid command\n");
        }

    };

    LqHttpPthRegisterExecFile(
        Http,
        &Mod,
        "*",
        "/RemoteTerminal/Close",
		[](LqHttpConn* c) -> void {
			LqHttpConnInterface Conn(c);
			auto Ses = SearchSess(LqParseInt(Conn.Rcv.Args["SessionIndex"]), Conn.Rcv.Args["Key"]);
			if(Ses == nullptr) {
				Conn << "Invalid key or index of session.";
				Conn.Rsp.Status = 500;

				return;
			}
			Sessions.remove_mult_by_val(Ses);
			LqClientSetClose(&Ses->TimerFd);

			Conn << "OK";
		},
        LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );

    LqHttpPthRegisterExecFile(
        Http,
        &Mod,
        "*",
        "/RemoteTerminal/In",
		[](LqHttpConn* c) -> void { /* Transfer data to terminal */
			LqHttpConnInterface Conn(c);
			if(Conn.Rcv.ContentLen > 4096) {
				Conn.Rsp.Status = 500;
				Conn << "Limit overflow.";
				return;
			}
			Conn.Rcv.WaitLen(
				[](LqHttpConnRcvResult* Res) -> void {
					if(Res->HttpConn == nullptr) { /* Have error or close conn*/
						return;
					}
					LqHttpConnInterface Conn(Res->HttpConn);
					auto Ses = SearchSess(LqParseInt(Conn.Rcv.Args["SessionIndex"]), Conn.Rcv.Args["Key"]);
					if(Ses == nullptr) {
						Conn.Rsp.Status = 500;
						Conn << "Invalid key or index of session.";
						return;
					}
					char Buf[4096];
					Ses->LockWrite();
					auto Readed = Conn.TryRead(Buf, Conn.Rcv.ContentLen);
					auto Written = LqFileWrite(Ses->MasterFd, Buf, Readed);
					Ses->Unlock();
					if((Written >= 0) || LQERR_IS_WOULD_BLOCK) {
						Conn << "Written: " << Written;
					} else {
						Conn.Rsp.Status = 500;
						Conn << "Terminal error. " << strerror(lq_errno);
					}
				}
			);
		},
        LQHTTPATZ_PERM_WRITE | LQHTTPATZ_PERM_MODIFY | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );

    LqHttpPthRegisterExecFile(
        Http,
        &Mod,
        "*",
        "/RemoteTerminal/Out",
		[](LqHttpConn* c) -> void {
			LqHttpConnInterface Conn(c);
			bool Res = false;
			auto Ses = SearchSess(LqParseInt(Conn.Rcv.Args["SessionIndex"]), Conn.Rcv.Args["Key"]);
			if(Ses == nullptr) {
				Conn << "Invalid key or index of session.";
				Conn.Rsp.Status = 500;
				return;
			}
			Ses->LockWrite();
			if(Ses->Conn == nullptr) {

				Conn.UserData[&ModUnique] = Ses.Get();
				Ses->LastAct = LqTimeGetLocMillisec();
				Res = true;
				Conn.BeginLongPoll(
					[](LqHttpConn* c) -> void {
						LqHttpConnInterface Conn(c);
						SesPtr Ses = (CmdSession*)Conn.UserData[&ModUnique];
						Ses->LockWrite();
						if(Ses->Conn != nullptr) {
							Ses->Conn = nullptr;
							LqObPtrDereference<CmdSession, LqFastAlloc::Delete>(Ses.Get());
						}
						Ses->Unlock();
						Conn.EndLongPoll();
				    }
				);
				Ses->Conn = c;
				LqClientSetFlags(&Ses->ReadFd, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP, 0);
				/////////////
				LqObPtrReference(Ses.Get());
			} else {
				Conn << "Not read from termminal. This session used by another user.";
				Conn.Rsp.Status = 500;
			}
			Ses->Unlock();
		},
        LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );


    Mod.FreeNotifyProc =
    [](LqHttpMdl* This) -> uintptr_t {
		LqWrkBoss::GetGlobal()->EnumClientsAndCallFinAsync11(
			[](LqWrkPtr&, LqClientHdr* Evnt) -> int {
				if(auto EvntFd = LqClientToFd(Evnt))
					return ((EvntFd->Handler == CmdSession::TimerHandler) || (EvntFd->Handler == CmdSession::ReadHandler)) ? 2 : 0;
				return 0;
			},
			std::bind(
				[](uintptr_t Handle) -> uintptr_t {
					return Handle;
				},
				This->Handle
			)
		);
        return 0;
    };

    return LQHTTPMDL_REG_OK;
}
