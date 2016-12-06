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

	char Next = '\0';
	for(int i = 1; i < argc; i++)
	{
		switch(Next)
		{
			case 'a':
			{
				addrinfo hi = {0}, *ah = nullptr;
				hi.ai_socktype = SOCK_STREAM;
				hi.ai_family = IPPROTO_IP;
				hi.ai_protocol = AF_UNSPEC;
				hi.ai_flags = 0;                   //AI_PASSIVE
				StrAddr = argv[i];
				int res = getaddrinfo(argv[i], nullptr, &hi, &ah);
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
			case 'p':
			{
				int StartRange = 0, EndRange = 0;
				auto Readed = sscanf(argv[i], "%i-%i", &StartRange, &EndRange);
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
			case 'b':
			{
				int TmpFreg, TmpLen;
				auto Readed = sscanf(argv[i], "%i,%i", &TmpFreg, &TmpLen);
				if(Readed == 1)
				{
					BeepFreg = TmpFreg;
					BeepLen = 200;
				} else if(Readed == 2)
				{
					BeepFreg = TmpFreg;
					BeepLen = TmpLen;
				} else if(Readed < 1)
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
			case 'l':
			{
				LogFileName = argv[i];
				LqFileStat Stat;
				auto StatRes = LqFileGetStat(LogFileName.c_str(), &Stat);
				if((StatRes >= 0) && !(Stat.Type & LQ_F_REG))
				{
					fprintf(stderr, "ERROR: Invalid file name (%s)\n", LogFileName.c_str());
					return -1;
				}
			}
			break;
			case 'w':
			{
				WaitMillisec = LqParseInt(argv[i]);
				if(WaitMillisec < 30)
				{
					fprintf(stderr, "ERROR: Invalid wait time\n");
					return -1;
				}
			}
			break;
			case 'c':
			{
				CountWorkers = LqParseInt(argv[i]);
				if(CountWorkers < 0)
					CountWorkers = std::thread::hardware_concurrency();
			}
			break;
			case 'h':
			{
				printf(
					"Lanq Port Scanner\n"
					"hotSAN 2016\n"
					" Only for TCP\n"
					" Arguments: \n"
					"  -a <ip address or host> - Scan host or ip address\n"
					"  -e - Print in log file only host name when not found open ports\n"
					"  -p <StartPort[-EndPort]> - Add port range to scan\n"
					"  -b <Freg,Len> - Beep when foun open ports\n"
					"  -l <Log file name or ?stdout> - Out file name\n"
					"  -w <Millisec> - Connect wait time\n"
					"  -c <Worker_Count> - Worker count\n"
				);
				return 0;
			}
			default:
				if(argv[i][0] == '-')
				{
					switch(argv[i][1])
					{
						case 'e': IsOutEmpty = true; break;
						case 'a': case 'p': case 'b':
						case 'l': case 'w': case 'c':
						case 'h': Next = argv[i][1]; break;
					}
				}
				continue;
		}
		Next = '\0';
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
			OutStr = StrAddr + ":";
			for(auto i: OpenPorts)
				OutStr += (" " + std::to_string(i));
			OutStr += "\n";
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