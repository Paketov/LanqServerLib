/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* PortScanner - Console shell for scan ports remote or local host.
*/

#include "LqWrkBoss.hpp"
#include "LqTime.h"
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

#include "LqPrtScan.hpp"


#include <stdlib.h>
#include <type_traits>
#include <vector>
#include <thread>
#include <stack>
#include <regex>

static bool ParseArgs(LqString& Source, std::pair<LqString, LqString>& Res)
{
	static std::regex Regex("(?:[\t ]*)(?:(?:\"([^\"]*)\")|([^ \t=]*))(?:=(?:(?:\"([^\"]*)\")|([^ \t]*)))?(.*)");
	std::cmatch Match;
	std::regex_match(Source.c_str(), Match, Regex);
	if(Match.empty())
		return false;

	LqString Name;
	LqString Val;
	Name = (Match[1].matched) ? Match[1].str() : Match[2].str();
	if(Match[3].matched)
		Val = Match[3].str();
	else if(Match[4].matched)
		Val = Match[4].str();
	Res.first = Name;
	Res.second = Val;
	return true;
}


int main(int argc, char* argv[])
{
#if !defined(LQPLATFORM_WINDOWS)
	signal(SIGTERM, [](int) -> void {  });
#endif
#if !defined(LQPLATFORM_WINDOWS)
	signal(SIGPIPE, SIG_IGN);
#endif
	LqCpSet(LQCP_UTF_8);
	intptr_t CountWorkers = 1;
	int BeepFreg = -1, BeepLen = -1;
	std::vector<std::pair<uint16_t, uint16_t>> Ports;
	std::string LogFileName = "?stdout";
	int WaitMillisec = 1000;
	LqConnInetAddress Addr;
	Addr.Addr.sa_family = 0;
	LqString StrAddr;
	bool IsOutEmpty = false;


	for(int i = 1; i < argc; i++)
	{
		LqString Name, Value;
		for(char* c = argv[i]; *c != '\0'; c++)
		{
			if(*c == '=')
			{
				Value = c + 1;
				*c = '\0';
				Name = argv[i];
				*c = '=';
				break;
			}
		}
		if(Name.empty())
			Name = argv[i];

		LQSTR_SWITCH_I(Name.c_str())
		{
			LQSTR_CASE_I("--addr")
			{
				addrinfo hi = {0}, *ah = nullptr, *i;
				hi.ai_socktype = SOCK_STREAM;
				hi.ai_family = IPPROTO_IP;
				hi.ai_protocol = AF_UNSPEC;
				hi.ai_flags = 0;                   //AI_PASSIVE
				StrAddr = Value;
				int res = getaddrinfo(Value.c_str(), nullptr, &hi, &ah);
				if(res == 0)
				{
					memcpy(&Addr, ah->ai_addr, ah->ai_addrlen);
					freeaddrinfo(ah);
				} else
				{
					fprintf(stderr, "ERROR: Invalid address (%s)\n", gai_strerror(res));
					return -1;
				}
			}
			break;
			LQSTR_CASE_I("--outempty")
			{
				IsOutEmpty = true;
			}
			break;
			LQSTR_CASE_I("--prt")
			{
				int StartRange = 0, EndRange = 0;
				auto Readed = sscanf(Value.c_str(), "%i-%i", &StartRange, &EndRange);
				if(Readed == 1)
				{
					EndRange = StartRange;
				} else if((Readed < 1) || (StartRange > EndRange))
				{
					fprintf(stderr, "ERROR: Invalid port range (%i, %i)\n", StartRange, EndRange);
					return -1;
				}
				if(StartRange <= 0)
				{
					fprintf(stderr, "ERROR: Invalid port range (%i, %i)\n", StartRange, EndRange);
					return -1;
				}
				Ports.push_back(std::pair<uint16_t, uint16_t>(StartRange, EndRange));
			}
			break;
			LQSTR_CASE_I("--beep")
			{
				int TmpFreg, TmpLen;
				auto Readed = sscanf(Value.c_str(), "%i,%i", &TmpFreg, &TmpLen);
				if(Readed == 1)
				{
					BeepFreg = TmpFreg;
					BeepLen = 200;
				} else if(Readed == 2)
				{
					BeepFreg = TmpFreg;
					BeepLen = TmpLen;
				}else if(Readed < 1)
				{
					fprintf(stderr, "ERROR: Invalid beep params\n");
					return -1;
				}
				if((BeepFreg < 0) || (BeepFreg > 20000) || (BeepLen < 0))
				{
					fprintf(stderr, "ERROR: Invalid beep params\n");
					return -1;
				}
			}
			break;
			LQSTR_CASE_I("--log")
			{
				LqString Str = argv[i];
				std::pair<LqString, LqString> Res;
				if(!ParseArgs(Str, Res))
				{
					fprintf(stderr, "ERROR: Invalid log filename\n");
					return -1;
				}
				LogFileName = Res.second;
				LqFileStat Stat;
				auto StatRes = LqFileGetStat(LogFileName.c_str(), &Stat);
				if((StatRes >= 0) && !(Stat.Type & LQ_F_REG))
				{
					fprintf(stderr, "ERROR: Invalid file name (%s)\n", LogFileName.c_str());
					return -1;
				}
			}
			break;
			LQSTR_CASE_I("--wait")
			{
				WaitMillisec = LqParseInt(Value);
				if(WaitMillisec < 30)
				{
					fprintf(stderr, "ERROR: Invalid wait time\n");
					return -1;
				}
			}
			break;
			LQSTR_CASE_I("--wrkcount")
			{
				CountWorkers = LqParseInt(Value);
				if(CountWorkers < 0)
					CountWorkers = std::thread::hardware_concurrency();
			}
			break;
			LQSTR_CASE_I("--help")
			{
				printf(
					"Lanq Port Scanner\n"
					"hotSAN 2016\n"
					" Arguments: \n"
					"  --addr=<ip address or host> - Scan host or ip address\n"
					"  --outempty - Print in log file only host name when not found open ports\n"
					"  --prt=<StartPort[-EndPort]> - Add port range to scan\n"
					"  --beep=<Freg,Len> - Beep when foun open ports\n"
					"  --log=<Log file name or ?stdout> - Out file name\n"
					"  --wait=<Millisec> - Connect wait time\n"
					"  --wrkcount=<Count> - Worker count\n"
				);
				return 0;
			}
			break;
		}
	}
	if(Addr.Addr.sa_family == 0)
	{
		fprintf(stderr, "ERROR: Not specified ip or host address\n");
		return -1;
	}

	if(Ports.empty())
	{
		fprintf(stderr, "ERROR: Port rage not specified\n");
		return -1;
	}

	LqWrkBossSetMinWrkCount(1);
	LqWrkBossAddWrks(CountWorkers, true);
	std::vector<uint16_t> OpenPorts;
	if(!LqPrtScanDo(&Addr, Ports, 2000, WaitMillisec, OpenPorts))
	{
		fprintf(stderr, "ERROR: Not scan remote host\n");
		return -1;
	}

	if(!OpenPorts.empty() || IsOutEmpty)
	{
		if(!LogFileName.empty())
		{
			int LogFd;
			if(LogFileName == "?stdout")
			{
				fflush(stdout);
				LogFd = LQ_STDOUT;
			} else
			{
				LogFd = LqFileOpen(LogFileName.c_str(), LQ_O_APND | LQ_O_BIN | LQ_O_WR | LQ_O_CREATE | LQ_O_SEQ, 0666);
				LqFileSeek(LogFd, 0, LQ_SEEK_END);
			}

			LqString OutStr;
			OutStr = StrAddr + ": ";
			for(auto i: OpenPorts)
				OutStr += (" " + std::to_string(i) + ",");
			OutStr.pop_back();
			OutStr += "\r\n";
			LqFileWrite(LogFd, OutStr.c_str(), OutStr.length());
			if(LogFileName != "?stdout")
				LqFileClose(LogFd);
		}
	}
	if((BeepFreg > 0) && !OpenPorts.empty())
	{
#ifdef LQPLATFORM_WINDOWS
		Beep(BeepFreg, BeepLen);
#endif
	}

	LqWrkBossSetMinWrkCount(0);
	LqWrkBossKickAllWrk();
	return OpenPorts.size();
}


#define __METHOD_DECLS__
#include "LqAlloc.hpp"