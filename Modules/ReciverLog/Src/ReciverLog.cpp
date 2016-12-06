/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   ReciverLog - Reciver of pakets. Can run another process when recive them.
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
#include "LqTime.h"
#include "LqPtdArr.hpp"
#include "LqLib.h"
#include "LqZmbClr.h"
#include "LqIpRow.hpp"

#include "LqTime.hpp"
#define __METHOD_DECLS__
#include "LqAlloc.hpp"

#include <regex>
#include <stdlib.h>


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

static void ParseArgs(LqString& Source, std::vector<std::pair<std::string, std::string>>& Res)
{
	static std::regex Regex("(?:[\t ]*)(?:(?:\"([^\"]*)\")|([^ \t=]*))(?:=(?:(?:\"([^\"]*)\")|([^ \t]*)))?(.*)");
	const char* Str = Source.c_str();
	std::cmatch Match;
	for(;;)
	{
		std::regex_match(Str, Match, Regex);
		if(Match.empty())
			return;
		std::string Name;
		std::string Val;
		Name = (Match[1].matched) ? Match[1].str() : Match[2].str();
		if(Match[3].matched)
			Val = Match[3].str();
		else if(Match[4].matched)
			Val = Match[4].str();
		Res.push_back(std::pair<std::string, std::string>(Name, Val));
		Str = Match[5].first;
		if(Str[0] == '\0')
			return;
	}
}

static void ParseArgsProc(LqString& Source, std::vector<std::string>& Res)
{
	static std::regex Regex("(?:[\t ]*)((?:[^\t \"]*)(?:\"([^\"]*)\")?(?:[^\t ]*)?)(.*)");
	const char* Str = Source.c_str();
	std::cmatch Match;
	for(;;)
	{
		std::regex_match(Str, Match, Regex);
		if(Match.empty())
			return;
		if(Match[1].first[0] == '\"')
			Res.push_back(Match[2].str());
		else if(Match[1].matched)
			Res.push_back(Match[1].str());
		Str = Match[3].first;
		if(Str[0] == '\0')
			return;
	}
}


bool GetLocalAddress(LqConnInetAddress* Dest)
{
	auto Fd = LqConnConnect("8.8.8.8", "53", AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, nullptr, nullptr, false);
	if(Fd == -1)
		return false;

	socklen_t addr_len = sizeof(LqConnInetAddress);
	getsockname(Fd, &Dest->Addr, &addr_len);
	closesocket(Fd);
	return true;
}



enum PktFormatType
{
	IpVer,
	IpLen,
	IpDscp,
	IpEcn,
	IpPktLen,
	IpId,
	IpNoFrag,
	IpHasFrag,
	IpOffset,
	IpTtl,
	IpProto,
	IpChecksum,
	IpSrcAddr,
	IpDstAddr,

	TcpSrcPort,
	TcpDstPort,
	TcpSeq,
	TcpAckSeq,
	TcpPktLen,
	TcpFlags,
	TcpWin,
	TcpChecksum,
	TcpUrgPtr,

	UdpSrcPort,
	UdpDstPort,
	UdpLen,
	UdpCheck,

	PktData
};

struct LqBindedResponse
{
	bool   IsProc;
	size_t CountPointers;
	size_t SizeData;
	char Data[1];
};

struct LqPortRange
{
	int StartRange;
	int EndRange;
	bool IsUdp;
	bool IsTcp;
	int LogFd;
	LqFileSz Loop;
	std::vector<std::string> __Args;
};

void DelLqBindedResponse(LqBindedResponse* Val) { free(Val); }

typedef LqShdPtr<LqBindedResponse, DelLqBindedResponse, false, true> LqBindedResponsePtr;

struct LqBindedConn
{
	LqConn              Conn;
	uint16_t            Port;
	LqBindedResponsePtr RspData;
	uint16_t            SizeRecive;
	LqTimeMillisec      TimeLive;
};

struct LqClientConn
{
	LqConn              Conn;
	LqBindedResponsePtr RspData;
	uint16_t            SizeRecive;
	uint16_t			SizeWrite;
	LqTimeMillisec      EndTime;
};


#define MAX_PACKET_SIZE    0x10000
LqConn PktConn;
LqConn PktConnUdp;
LqHttpMdl Mod;
LqProto PktProto;
LqProto ClientProto;
LqProto BindedProto;
LqEvntFd ZmbClr;

LqPtdArr<PktFormatType> PktPrintFormat;
LqPtdArr<LqPortRange> PortRange;
LqConnInetAddress LocalAddress;



static void LQ_CALL PktReciveProc(LqConn* Conn, LqEvntFlag)
{
	char Buf[MAX_PACKET_SIZE] = {0};
	
	int PktSize = recv(Conn->Fd, Buf, sizeof(Buf), 0);
	if(PktSize == -1)
		return;
	ipheader* Ip = (ipheader*)Buf;
	if(Ip->version != 4)
		return;
	if(LocalAddress.AddrInet.sin_addr.s_addr != *((uint32_t*)&Ip->daddr))
		return;
	struct tm Tm;
	LqTimeGetLocTm(&Tm);
	if(Ip->protocol == IPPROTO_TCP)
	{
		tcphdr* Tcp = (tcphdr*)(((char*)Ip) + (Ip->ihl * 4));
		char* Data = (char*)(((char*)Tcp) + (Tcp->thl * 4));
		size_t DataSize = Ip->len - ((uintptr_t)Data - (uintptr_t)Ip);

		uint16_t Port = Tcp->dest;
		auto i = PortRange.begin();
		for(; !i.is_end(); i++)
		{
			if((i->StartRange <= Port) && (Port <= i->EndRange))
				goto lblBreak;
		}
		return;
lblBreak:
		if(!i->IsTcp)
			return;
		if(i->__Args.size() > 0)
		{
			char GlobEnv[1024 * 16];
			auto Count = LqFileGetEnvs(GlobEnv, sizeof(GlobEnv));
			std::vector<char*> Envs;
			for(char* c = GlobEnv; *c; c++)
			{
				Envs.push_back(c);
				for(; *c != '\0'; c++);
			}

			std::vector<std::string> __Envs;
			bool IsResponseData = false;

			for(auto j : PktPrintFormat)
			{
				char Buf[1024];
				switch(j)
				{
					case IpVer: sprintf(Buf, "IpVer=%u", (unsigned)Ip->version); break;
					case IpLen: sprintf(Buf, "IpLen=%u", (unsigned)Ip->ihl); break;
					case IpDscp: sprintf(Buf, "IpDscp=%u", (unsigned)Ip->dscp); break;
					case IpEcn: sprintf(Buf, "IpEcn=%u", (unsigned)Ip->ecn); break;
					case IpPktLen: sprintf(Buf, "IpPktLen=%u", (unsigned)Ip->ihl); break;
					case IpId: sprintf(Buf, "IpId=%u", (unsigned)Ip->id); break;
					case IpNoFrag: sprintf(Buf, "IpNoFrag=%u", (unsigned)Ip->nofr); break;
					case IpHasFrag: sprintf(Buf, "IpHasFrag=%u", (unsigned)Ip->hasfr); break;
					case IpOffset: sprintf(Buf, "IpOffset=%u", (unsigned)Ip->offset); break;
					case IpTtl: sprintf(Buf, "IpTtl=%u", (unsigned)Ip->ttl); break;
					case IpProto: sprintf(Buf, "IpProto=%u", (unsigned)Ip->protocol); break;
					case IpChecksum: sprintf(Buf, "IpChecksum=%u", (unsigned)Ip->check); break;
					case IpSrcAddr: sprintf(Buf, "IpSrcAddr=%u.%u.%u.%u",
						(unsigned)(((uint8_t*)&Ip->saddr)[0]),
											(unsigned)(((uint8_t*)&Ip->saddr)[1]),
											(unsigned)(((uint8_t*)&Ip->saddr)[2]),
											(unsigned)(((uint8_t*)&Ip->saddr)[3])); break;
					case IpDstAddr: sprintf(Buf, "IpDstAddr=%u.%u.%u.%u",
						(unsigned)(((uint8_t*)&Ip->daddr)[0]),
											(unsigned)(((uint8_t*)&Ip->daddr)[1]),
											(unsigned)(((uint8_t*)&Ip->daddr)[2]),
											(unsigned)(((uint8_t*)&Ip->daddr)[3])); break;

					case TcpSrcPort: sprintf(Buf, "TcpSrcPort=%u", (unsigned)Tcp->source); break;
					case TcpDstPort: sprintf(Buf, "TcpDstPort=%u", (unsigned)Tcp->dest); break;
					case TcpSeq: sprintf(Buf, "TcpSeq=%u", (unsigned)Tcp->seq); break;
					case TcpAckSeq: sprintf(Buf, "TcpAckSeq=%u", (unsigned)Tcp->ack_seq); break;
					case TcpPktLen: sprintf(Buf, "TcpPktLen=%u", (unsigned)Tcp->thl); break;
					case TcpFlags: sprintf(
						Buf,
						"TcpFlags=cwr=%u, ece=%u, urg=%u, ack=%u, psh=%u, rst=%u, syn=%u, fin=%u",
						(unsigned)Tcp->cwr,
						(unsigned)Tcp->ece,
						(unsigned)Tcp->urg,
						(unsigned)Tcp->ack,
						(unsigned)Tcp->psh,
						(unsigned)Tcp->rst,
						(unsigned)Tcp->syn,
						(unsigned)Tcp->fin); break;
					case TcpWin: sprintf(Buf, "TcpWin=%u", (unsigned)Tcp->window); break;
					case TcpChecksum: sprintf(Buf, "TcpChecksum=%u", (unsigned)Tcp->check); break;
					case TcpUrgPtr: sprintf(Buf, "TcpUrgPtr=%u", (unsigned)Tcp->urg_ptr); break;
					case PktData: IsResponseData = true; continue;
					default: continue;
				}
				__Envs.push_back(Buf);
			}
			for(auto& j : __Envs)
				Envs.push_back((char*)j.data());
			int Rd, Wr;
			LqFilePipeCreate(&Rd, &Wr, 0, 0);
			int Dev = LqFileOpen(LQ_NULLDEV, LQ_O_WR, 0);
			Envs.push_back((char*)nullptr);
			
			std::vector<char*> Args;
			for(auto f = 1; f < i->__Args.size(); f++ )
				Args.push_back((char*)i->__Args[f].c_str());
			Args.push_back((char*)nullptr);
			if(LqFileProcessCreate(i->__Args[0].c_str(), Args.data(), Envs.data(), nullptr, Rd, Dev, Dev, nullptr, false) != -1)
			{
				LqFileWrite(Wr, Data, DataSize);
			}
			LqFileClose(Wr);
			LqFileClose(Rd);
			LqFileClose(Dev);

		} else
		{
			char HdrsBuf[70000];
			char* CurPos = HdrsBuf;
			CurPos += sprintf(HdrsBuf, "== " PRINTF_TIME_TM_FORMAT "\r\n", PRINTF_TIME_TM_ARG(Tm));
			for(auto i : PktPrintFormat)
			{
				switch(i)
				{
					case IpVer: CurPos += sprintf(CurPos, "IpVer: %u\r\n", (unsigned)Ip->version); break;
					case IpLen: CurPos += sprintf(CurPos, "IpLen: %u\r\n", (unsigned)Ip->ihl); break;
					case IpDscp: CurPos += sprintf(CurPos, "IpLen: %u\r\n", (unsigned)Ip->ihl); break;
					case IpEcn: CurPos += sprintf(CurPos, "IpEcn: %u\r\n", (unsigned)Ip->ecn); break;
					case IpPktLen: CurPos += sprintf(CurPos, "IpPktLen: %u\r\n", (unsigned)Ip->ihl); break;
					case IpId: CurPos += sprintf(CurPos, "IpId: %u\r\n", (unsigned)Ip->id); break;
					case IpNoFrag: CurPos += sprintf(CurPos, "IpNoFrag: %u\r\n", (unsigned)Ip->nofr); break;
					case IpHasFrag: CurPos += sprintf(CurPos, "IpHasFrag: %u\r\n", (unsigned)Ip->hasfr); break;
					case IpOffset: CurPos += sprintf(CurPos, "IpOffset: %u\r\n", (unsigned)Ip->offset); break;
					case IpTtl: CurPos += sprintf(CurPos, "IpTtl: %u\r\n", (unsigned)Ip->ttl); break;
					case IpProto: CurPos += sprintf(CurPos, "IpProto: %u\r\n", (unsigned)Ip->protocol); break;
					case IpChecksum: CurPos += sprintf(CurPos, "IpChecksum: %u\r\n", (unsigned)Ip->check); break;
					case IpSrcAddr: CurPos += sprintf(CurPos, "IpSrcAddr: %u.%u.%u.%u\r\n",
						(unsigned)(((uint8_t*)&Ip->saddr)[0]),
													  (unsigned)(((uint8_t*)&Ip->saddr)[1]),
													  (unsigned)(((uint8_t*)&Ip->saddr)[2]),
													  (unsigned)(((uint8_t*)&Ip->saddr)[3])); break;
					case IpDstAddr: CurPos += sprintf(CurPos, "IpDstAddr: %u.%u.%u.%u\r\n",
						(unsigned)(((uint8_t*)&Ip->daddr)[0]),
													  (unsigned)(((uint8_t*)&Ip->daddr)[1]),
													  (unsigned)(((uint8_t*)&Ip->daddr)[2]),
													  (unsigned)(((uint8_t*)&Ip->daddr)[3])); break;

					case TcpSrcPort: CurPos += sprintf(CurPos, "TcpSrcPort: %u\r\n", (unsigned)Tcp->source); break;
					case TcpDstPort: CurPos += sprintf(CurPos, "TcpDstPort: %u\r\n", (unsigned)Tcp->dest); break;
					case TcpSeq: CurPos += sprintf(CurPos, "TcpSeq: %u\n", (unsigned)Tcp->seq); break;
					case TcpAckSeq: CurPos += sprintf(CurPos, "TcpAckSeq: %u\r\n", (unsigned)Tcp->ack_seq); break;
					case TcpPktLen: CurPos += sprintf(CurPos, "TcpPktLen: %u\r\n", (unsigned)Tcp->thl); break;
					case TcpFlags: CurPos += sprintf(
						CurPos,
						"TcpFlags: cwr=%u, ece=%u, urg=%u, ack=%u, psh=%u, rst=%u, syn=%u, fin=%u\r\n",
						(unsigned)Tcp->cwr,
						(unsigned)Tcp->ece,
						(unsigned)Tcp->urg,
						(unsigned)Tcp->ack,
						(unsigned)Tcp->psh,
						(unsigned)Tcp->rst,
						(unsigned)Tcp->syn,
						(unsigned)Tcp->fin); break;
					case TcpWin: CurPos += sprintf(CurPos, "TcpWin: %u\r\n", (unsigned)Tcp->window); break;
					case TcpChecksum: CurPos += sprintf(CurPos, "TcpChecksum: %u\r\n", (unsigned)Tcp->check); break;
					case TcpUrgPtr: CurPos += sprintf(CurPos, "TcpUrgPtr: %u\r\n", (unsigned)Tcp->urg_ptr); break;
					case PktData: Data[DataSize] = '\0'; CurPos += sprintf(CurPos, "Data: %s\r\n", (unsigned)Data); break;
				}
			}
			if((i->Loop < 0xffffffffffff) && (LqFileTell(i->LogFd) >= i->Loop))
				LqFileSeek(i->LogFd, 0, LQ_SEEK_SET);
			LqFileWrite(i->LogFd, HdrsBuf, (uintptr_t)CurPos - (uintptr_t)HdrsBuf);
		}
	} else if(Ip->protocol == IPPROTO_UDP)
	{
		udphdr* Udp = (udphdr*)(((char*)Ip) + (Ip->ihl * 4));
		char* Data = (char*)(Udp + 1);
		size_t DataSize = Udp->len;

		uint16_t Port = Udp->dest;
		auto i = PortRange.begin();
		for(; !i.is_end(); i++)
		{
			if((i->StartRange <= Port) && (Port <= i->EndRange))
				goto lblContinue2;
		}
		return;
lblContinue2:
		if(!i->IsUdp)
			return;
		if(i->__Args.size() > 0)
		{
			char GlobEnv[1024 * 16];
			LqFileGetEnvs(GlobEnv, sizeof(GlobEnv));
			std::vector<char*> Envs;
			for(char* c = GlobEnv; *c; c++)
			{
				Envs.push_back(c);
				for(; *c != '\0'; c++);
			}

			std::vector<std::string> __Envs;
			bool IsResponseData = false;

			for(auto j : PktPrintFormat)
			{
				char Buf[1024];
				switch(j)
				{
					case IpVer: sprintf(Buf, "IpVer=%u", (unsigned)Ip->version); break;
					case IpLen: sprintf(Buf, "IpLen=%u", (unsigned)Ip->ihl); break;
					case IpDscp: sprintf(Buf, "IpDscp=%u", (unsigned)Ip->dscp); break;
					case IpEcn: sprintf(Buf, "IpEcn=%u", (unsigned)Ip->ecn); break;
					case IpPktLen: sprintf(Buf, "IpPktLen=%u", (unsigned)Ip->ihl); break;
					case IpId: sprintf(Buf, "IpId=%u", (unsigned)Ip->id); break;
					case IpNoFrag: sprintf(Buf, "IpNoFrag=%u", (unsigned)Ip->nofr); break;
					case IpHasFrag: sprintf(Buf, "IpHasFrag=%u", (unsigned)Ip->hasfr); break;
					case IpOffset: sprintf(Buf, "IpOffset=%u", (unsigned)Ip->offset); break;
					case IpTtl: sprintf(Buf, "IpTtl=%u", (unsigned)Ip->ttl); break;
					case IpProto: sprintf(Buf, "IpProto=%u", (unsigned)Ip->protocol); break;
					case IpChecksum: sprintf(Buf, "IpChecksum=%u", (unsigned)Ip->check); break;
					case IpSrcAddr: sprintf(Buf, "IpSrcAddr=%u.%u.%u.%u",
						(unsigned)(((uint8_t*)&Ip->saddr)[0]),
											(unsigned)(((uint8_t*)&Ip->saddr)[1]),
											(unsigned)(((uint8_t*)&Ip->saddr)[2]),
											(unsigned)(((uint8_t*)&Ip->saddr)[3])); break;
					case IpDstAddr: sprintf(Buf, "IpDstAddr=%u.%u.%u.%u",
						(unsigned)(((uint8_t*)&Ip->daddr)[0]),
											(unsigned)(((uint8_t*)&Ip->daddr)[1]),
											(unsigned)(((uint8_t*)&Ip->daddr)[2]),
											(unsigned)(((uint8_t*)&Ip->daddr)[3])); break;

					case UdpSrcPort: sprintf(Buf, "UdpSrcPort=%u", (unsigned)Udp->source); break;
					case UdpDstPort: sprintf(Buf, "UdpDstPort=%u", (unsigned)Udp->dest); break;
					case UdpLen: sprintf(Buf, "UdpLen=%u", (unsigned)Udp->len); break;
					case UdpCheck: sprintf(Buf, "UdpCheck=%u", (unsigned)Udp->check); break;
					case PktData: IsResponseData = true; continue;
					default: continue;
				}
				__Envs.push_back(Buf);
			}
			for(auto& j : __Envs)
				Envs.push_back((char*)j.data());
			int Rd, Wr;
			LqFilePipeCreate(&Rd, &Wr, 0, 0);
			int Dev = LqFileOpen(LQ_NULLDEV, LQ_O_WR, 0);
			Envs.push_back(nullptr);
			std::vector<char*> Args;
			for(auto f = 1; f < i->__Args.size(); f++)
				Args.push_back((char*)i->__Args[f].c_str());
			Args.push_back((char*)nullptr);
			if(LqFileProcessCreate(i->__Args[0].c_str(), Args.data(), Envs.data(), nullptr, Rd, Dev, Dev, nullptr, false) != -1)
			{
				LqFileWrite(Wr, Data, DataSize);
			}
			LqFileClose(Wr);
			LqFileClose(Rd);
			LqFileClose(Dev);
		} else
		{
			char HdrsBuf[70000];
			char* CurPos = HdrsBuf;
			CurPos += sprintf(HdrsBuf, "== " PRINTF_TIME_TM_FORMAT "\r\n", PRINTF_TIME_TM_ARG(Tm));
			for(auto i : PktPrintFormat)
			{
				switch(i)
				{
					case IpVer: CurPos += sprintf(CurPos, "IpVer: %u\r\n", (unsigned)Ip->version); break;
					case IpLen: CurPos += sprintf(CurPos, "IpLen: %u\r\n", (unsigned)Ip->ihl); break;
					case IpDscp: CurPos += sprintf(CurPos, "IpLen: %u\r\n", (unsigned)Ip->ihl); break;
					case IpEcn: CurPos += sprintf(CurPos, "IpEcn: %u\r\n", (unsigned)Ip->ecn); break;
					case IpPktLen: CurPos += sprintf(CurPos, "IpPktLen: %u\r\n", (unsigned)Ip->ihl); break;
					case IpId: CurPos += sprintf(CurPos, "IpId: %u\r\n", (unsigned)Ip->id); break;
					case IpNoFrag: CurPos += sprintf(CurPos, "IpNoFrag: %u\r\n", (unsigned)Ip->nofr); break;
					case IpHasFrag: CurPos += sprintf(CurPos, "IpHasFrag: %u\r\n", (unsigned)Ip->hasfr); break;
					case IpOffset: CurPos += sprintf(CurPos, "IpOffset: %u\r\n", (unsigned)Ip->offset); break;
					case IpTtl: CurPos += sprintf(CurPos, "IpTtl: %u\r\n", (unsigned)Ip->ttl); break;
					case IpProto: CurPos += sprintf(CurPos, "IpProto: %u\r\n", (unsigned)Ip->protocol); break;
					case IpChecksum: CurPos += sprintf(CurPos, "IpChecksum: %u\r\n", (unsigned)Ip->check); break;
					case IpSrcAddr: CurPos += sprintf(CurPos, "IpSrcAddr: %u.%u.%u.%u\r\n",
						(unsigned)(((uint8_t*)&Ip->saddr)[0]),
													  (unsigned)(((uint8_t*)&Ip->saddr)[1]),
													  (unsigned)(((uint8_t*)&Ip->saddr)[2]),
													  (unsigned)(((uint8_t*)&Ip->saddr)[3])); break;
					case IpDstAddr: CurPos += sprintf(CurPos, "IpDstAddr: %u.%u.%u.%u\r\n",
						(unsigned)(((uint8_t*)&Ip->daddr)[0]),
													  (unsigned)(((uint8_t*)&Ip->daddr)[1]),
													  (unsigned)(((uint8_t*)&Ip->daddr)[2]),
													  (unsigned)(((uint8_t*)&Ip->daddr)[3])); break;

					case UdpSrcPort: CurPos += sprintf(CurPos, "UdpSrcPort: %u\r\n", (unsigned)Udp->source); break;
					case UdpDstPort: CurPos += sprintf(CurPos, "UdpDstPort: %u\r\n", (unsigned)Udp->dest); break;
					case UdpLen: CurPos += sprintf(CurPos, "UdpLen: %u\r\n", (unsigned)Udp->len); break;
					case UdpCheck: CurPos += sprintf(CurPos, "UdpCheck: %u\r\n", (unsigned)Udp->check); break;
					case PktData: Data[DataSize] = '\0'; CurPos += sprintf(CurPos, "Data: %s\r\n", (unsigned)Data); break;
				}
			}

			if((i->Loop < 0xffffffffffff) && (LqFileTell(i->LogFd) >= i->Loop))
				LqFileSeek(i->LogFd, 0, LQ_SEEK_SET);
			LqFileWrite(i->LogFd, HdrsBuf, (uintptr_t)CurPos - (uintptr_t)HdrsBuf);
		}
	}
}

static void LQ_CALL PktEndConnProc(LqConn* Conn)
{
	closesocket(Conn->Fd);
}

static void LQ_CALL ClientEndConnProc(LqConn* Conn)
{
	auto ClientConn = (LqClientConn*)Conn;
	closesocket(ClientConn->Conn.Fd);
	LqFastAlloc::Delete(ClientConn);
}

static void LQ_CALL BindedReciveProc(LqConn* Conn, LqEvntFlag)
{
	LqConnInetAddress Addr;
	socklen_t Len = sizeof(Addr);
	auto fd = accept(Conn->Fd, &Addr.Addr, &Len);
	if(fd == -1)
		return;
	auto BConn = (LqBindedConn*)Conn;
	if((BConn->RspData != nullptr) && (BConn->RspData->IsProc))
	{
		LqString Str = BConn->RspData->Data;
		std::vector<LqString> __Args;
		std::vector<char*> Args, Envs;
		ParseArgsProc(Str, __Args);
		for(auto& i : __Args)
			Args.push_back((char*)i.c_str());
		if(Args.empty())
			return;
		Args.push_back(nullptr);
		char GlobEnv[1024 * 16];
		auto Count = LqFileGetEnvs(GlobEnv, sizeof(GlobEnv));
		for(char* c = GlobEnv; *c; c++)
		{
			Envs.push_back(c);
			for(; *c != '\0'; c++);
		}
		char Buf1[100], Buf2[100], Buf3[100];
		sprintf(Buf1, "SourceIp=%u.%u.%u.%u",
			(unsigned)(((uint8_t*)&Addr.AddrInet.sin_addr)[0]),
				(unsigned)(((uint8_t*)&Addr.AddrInet.sin_addr)[1]),
				(unsigned)(((uint8_t*)&Addr.AddrInet.sin_addr)[2]),
				(unsigned)(((uint8_t*)&Addr.AddrInet.sin_addr)[3])
		);
		Envs.push_back(Buf1);
		sprintf(Buf2, "SourcePort=%u", ntohs(Addr.AddrInet.sin_port));
		Envs.push_back(Buf2);
		sprintf(Buf3, "DestPort=%u", BConn->Port);
		Envs.push_back(Buf3);
		Envs.push_back(nullptr);
		int Dev = LqFileOpen(LQ_NULLDEV, LQ_O_WR, 0);

		LqConnSwitchNonBlock(fd, 1);
		LqFileProcessCreate(Args[0], Args.data() + 1, Envs.data(), nullptr, fd, fd, Dev, nullptr, false);
		closesocket(fd);
		LqFileClose(Dev);
		return;
	}
	auto ClientConn = LqFastAlloc::New<LqClientConn>();
	if(BConn->RspData != nullptr)
		ClientConn->SizeWrite = BConn->RspData->SizeData;
	else
		ClientConn->SizeWrite = 0;
	ClientConn->SizeRecive = BConn->SizeRecive;
	ClientConn->EndTime = LqTimeGetLocMillisec() + BConn->TimeLive;
	ClientConn->RspData = BConn->RspData;

	LqConnInit(ClientConn, fd, &ClientProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | ((ClientConn->SizeRecive > 0) ? LQEVNT_FLAG_RD : 0) | ((ClientConn->SizeWrite > 0) ? LQEVNT_FLAG_WR : 0));
	if((ClientConn->SizeRecive > 0) || (ClientConn->SizeWrite > 0))
		LqWrkBossAddEvntAsync((LqEvntHdr*)ClientConn);
	else
		ClientEndConnProc((LqConn*)ClientConn);
}

static void LQ_CALL BindedEndConnProc(LqConn* Conn)
{
	auto BConn = (LqBindedConn*)Conn;
	closesocket(BConn->Conn.Fd);
	LqFastAlloc::Delete(BConn);
}

static void LQ_CALL ClientReciveWriteProc(LqConn* Conn, LqEvntFlag Flags)
{
	if(Flags & LQEVNT_FLAG_RD)
	{
		char Buf[30000];
		auto ClientConn = (LqClientConn*)Conn;
		auto Recived = recv(Conn->Fd, Buf, sizeof(Buf), 0);
		if(Recived == -1)
			return;
		ClientConn->SizeRecive -= Recived;
		if(ClientConn->SizeRecive == 0)
		{
			if(ClientConn->SizeWrite == 0)
				LqEvntSetClose(Conn);
			else
				LqEvntSetFlags(Conn, LQEVNT_FLAG_WR | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
		}
	} else if(Flags & LQEVNT_FLAG_WR)
	{
		auto ClientConn = (LqClientConn*)Conn;
		auto Sended = send(ClientConn->Conn.Fd, ClientConn->RspData->Data + ClientConn->RspData->SizeData - ClientConn->SizeWrite, ClientConn->SizeWrite, 0);
		if(Sended == -1)
			return;
		ClientConn->SizeWrite -= Sended;
		if(ClientConn->SizeWrite == 0)
		{
			if(ClientConn->SizeRecive == 0)
				LqEvntSetClose(Conn);
			else
				LqEvntSetFlags(Conn, LQEVNT_FLAG_RD | LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP, 0);
		}
	}
}

static bool LQ_CALL ClientKickByTimeOutProc(LqConn* Conn, LqTimeMillisec CurrentTimeMillisec, LqTimeMillisec) { return CurrentTimeMillisec >= ((LqClientConn*)Conn)->EndTime; }

LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{
	LqHttpMdlInit(Reg, &Mod, "ReciverLog", ModuleHandle);

	int sock = socket(AF_INET, SOCK_RAW, 
#ifdef LQPLATFORM_WINDOWS
					  IPPROTO_IP
#else
					  IPPROTO_TCP
#endif
	);

	memset(&LocalAddress, 0, sizeof(LocalAddress));
	GetLocalAddress(&LocalAddress);

#ifdef LQPLATFORM_WINDOWS
	bind(sock, &LocalAddress.Addr, sizeof(LocalAddress.AddrInet));
	unsigned long        flag = 1;
	ioctlsocket(sock, 0x98000001, &flag);
#else	
	int sockUdp = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
	LqConnSwitchNonBlock(sockUdp, 1);
	LqConnSwitchNonBlock(sock, 1);
	int Fl = 1;
	setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &Fl, sizeof(Fl));
	setsockopt(sockUdp, IPPROTO_IP, IP_HDRINCL, &Fl, sizeof(Fl));
#endif

	LqProtoInit(&PktProto);
	PktProto.Handler = PktReciveProc;
	PktProto.CloseHandler = PktEndConnProc;

	LqProtoInit(&BindedProto);
	BindedProto.Handler = BindedReciveProc;
	BindedProto.CloseHandler = BindedEndConnProc;

	LqProtoInit(&ClientProto);
	ClientProto.Handler = ClientReciveWriteProc;
	ClientProto.CloseHandler = ClientEndConnProc;
	ClientProto.KickByTimeOutProc = ClientKickByTimeOutProc;

	LqZmbClrInit(&ZmbClr, &ClientProto, 10000, nullptr, nullptr);
	LqWrkBossAddEvntSync((LqEvntHdr*)&ZmbClr);

	if(sock != -1)
	{
		LqConnInit(&PktConn, sock, &PktProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD);
		LqWrkBossAddEvntSync((LqEvntHdr*)&PktConn);
	}
#ifndef LQPLATFORM_WINDOWS
	if(sockUdp != -1)
	{
		LqConnInit(&PktConnUdp, sockUdp, &PktProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD);
		LqWrkBossAddEvntSync((LqEvntHdr*)&PktConnUdp);
	}
#endif


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
			LQSTR_CASE("format")
			{
				int i = 0;
				PktPrintFormat.clear();
				for(; i < FullCommand.length(); i++)
				{
					for(; (FullCommand[i] == ' ') || (FullCommand[i] == '\n') || (FullCommand[i] == '\t'); i++);
					LqString Alias;
					for(; ((FullCommand[i] >= 'A') && (FullCommand[i] <= 'Z')) || ((FullCommand[i] >= 'a') && (FullCommand[i] <= 'z')); i++)
						Alias.append(1, FullCommand[i]);
					LQSTR_SWITCH_I(Alias.c_str())
					{
						LQSTR_CASE_I("ipver") PktPrintFormat.push_back(IpVer); break;
						LQSTR_CASE_I("iplen") PktPrintFormat.push_back(IpLen); break;
						LQSTR_CASE_I("ipdscp") PktPrintFormat.push_back(IpDscp); break;
						LQSTR_CASE_I("ipecn") PktPrintFormat.push_back(IpEcn); break;
						LQSTR_CASE_I("ippktlen") PktPrintFormat.push_back(IpPktLen); break;
						LQSTR_CASE_I("ipid") PktPrintFormat.push_back(IpId); break;
						LQSTR_CASE_I("ipnofrag") PktPrintFormat.push_back(IpNoFrag); break;
						LQSTR_CASE_I("iphasfrag") PktPrintFormat.push_back(IpHasFrag); break;
						LQSTR_CASE_I("ipoffset") PktPrintFormat.push_back(IpOffset); break;
						LQSTR_CASE_I("ipttl") PktPrintFormat.push_back(IpTtl); break;
						LQSTR_CASE_I("ipproto") PktPrintFormat.push_back(IpProto); break;
						LQSTR_CASE_I("ipchecksum") PktPrintFormat.push_back(IpChecksum); break;
						LQSTR_CASE_I("ipsrcaddr") PktPrintFormat.push_back(IpSrcAddr); break;
						LQSTR_CASE_I("ipdstaddr") PktPrintFormat.push_back(IpDstAddr); break;

						LQSTR_CASE_I("tcpsrcport") PktPrintFormat.push_back(TcpSrcPort); break;
						LQSTR_CASE_I("tcpdstport") PktPrintFormat.push_back(TcpDstPort); break;
						LQSTR_CASE_I("tcpseq") PktPrintFormat.push_back(TcpSeq); break;
						LQSTR_CASE_I("tcpackseq") PktPrintFormat.push_back(TcpAckSeq); break;
						LQSTR_CASE_I("tcppktlen") PktPrintFormat.push_back(TcpPktLen); break;
						LQSTR_CASE_I("tcpflags") PktPrintFormat.push_back(TcpFlags); break;
						LQSTR_CASE_I("tcpwin") PktPrintFormat.push_back(TcpWin); break;
						LQSTR_CASE_I("tcpchecksum") PktPrintFormat.push_back(TcpChecksum); break;
						LQSTR_CASE_I("tcpurgptr") PktPrintFormat.push_back(TcpUrgPtr); break;

						LQSTR_CASE_I("udpsrcport") PktPrintFormat.push_back(UdpSrcPort); break;
						LQSTR_CASE_I("udpdstport") PktPrintFormat.push_back(UdpDstPort); break;
						LQSTR_CASE_I("udplen") PktPrintFormat.push_back(UdpLen); break;
						LQSTR_CASE_I("udpcheck") PktPrintFormat.push_back(UdpCheck); break;

						LQSTR_CASE_I("pktdata") PktPrintFormat.push_back(PktData); break;
					}

				}
			}
			break;
			LQSTR_CASE("add_prt_range")
			{
				/*
				--logfile="<name log file>"
				*/
				std::vector<std::pair<LqString, LqString>> Args;
				ParseArgs(FullCommand, Args);
				LqString LogFileName;
				int StartPrt, EndPrt;
				LqFileSz Loop = 0xffffffffffff;
				for(auto& i : Args)
				{
					LQSTR_SWITCH_I(i.first.c_str())
					{
						LQSTR_CASE_I("--logfile")
						{
							LogFileName = i.second;
						}
						break;
						LQSTR_CASE_I("--logloop")
						{
							Loop = LqParseInt(i.second);
						}
						break;
						LQSTR_SWITCH_DEFAULT
						{
							auto Rt = sscanf(i.first.c_str(), "%i-%i", &StartPrt, &EndPrt);
							if(Rt == 0)
							{
								if(OutBuffer != nullptr)
									fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port range (%s)\n", i.first.c_str());
							} else if(Rt == 1)
							{
								EndPrt = StartPrt;
							}
						}
						break;
					}
				}
				LqPortRange PrtRng;
				PrtRng.StartRange = StartPrt;
				PrtRng.EndRange = EndPrt;
				PrtRng.IsTcp = true;
				PrtRng.IsUdp = true;
				PrtRng.LogFd = -1;
				if(!LogFileName.empty())
				{
					int Fd = LqFileOpen(LogFileName.c_str(), LQ_O_APND | LQ_O_BIN | LQ_O_WR | LQ_O_CREATE | LQ_O_SEQ, 0666);
					if(Fd == -1)
					{
						if(OutBuffer != nullptr)
							fprintf(OutBuffer, " [ReciverLog] ERROR: open log file (%s)\n", strerror(lq_errno));
						return;
					}
					LqFileSeek(Fd, 0, LQ_SEEK_END);
					PrtRng.LogFd = Fd;
					PrtRng.Loop = Loop;
				} else
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [ReciverLog] ERROR: set log file\n");
				}
				PortRange.push_back(PrtRng);
			}
			break;
			LQSTR_CASE("add_prt_range_proc")
			{
				std::vector<LqString> Args;
				ParseArgsProc(FullCommand, Args);
				int StartPrt, EndPrt;
				if(Args.empty())
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port range\n");
					return;
				}
				auto Rt = sscanf(Args[0].c_str(), "%i-%i", &StartPrt, &EndPrt);
				if(Rt == 0)
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port range (%s)\n", Args[0].c_str());
					return;
				} else if(Rt == 1)
				{
					EndPrt = StartPrt;
				}
				PortRange.push_back(LqPortRange());
				auto PrtRng = PortRange.back();
				PrtRng->StartRange = StartPrt;
				PrtRng->EndRange = EndPrt;
				PrtRng->LogFd = -1;
				for(auto i = 1; i < Args.size(); i++)
					PrtRng->__Args.push_back(Args[i]);
				PrtRng->IsTcp = true;
				PrtRng->IsUdp = true;
			}
			break;
			LQSTR_CASE("add_bind_port")
			{
				/*
				  startport-endport [args]
				  args:
				   --file=<response_file>
				   --recvln=<len>
				   --live=<timelive>
				*/
				std::vector<std::pair<LqString, LqString>> Args;
				ParseArgs(FullCommand, Args);
				LqString ResponseFileName;
				int RecvLen = 0;
				int TimeLive = 0;
				int StartPrt, EndPrt;
				for(auto& i : Args)
				{
					LQSTR_SWITCH_I(i.first.c_str())
					{
						LQSTR_CASE_I("--rspfile")
							ResponseFileName = i.second;
						break;
						LQSTR_CASE_I("--recvln")
							RecvLen = atoi(i.second.c_str());
						break;
						LQSTR_CASE_I("--live")
							TimeLive = atoi(i.second.c_str());
						break;
						LQSTR_SWITCH_DEFAULT
						{
							auto Rt = sscanf(i.first.c_str(), "%i-%i", &StartPrt, &EndPrt);
							if(Rt == 0)
							{
								if(OutBuffer != nullptr)
									fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port range (%s)\n", i.first.c_str());
							} else if(Rt == 1)
							{
								EndPrt = StartPrt;
							}
						}
						break;
					}
				}
				if(EndPrt == 0)
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port range\n");
					return;
				}

				LqBindedResponsePtr ResponseDataPtr = nullptr;
				if(!ResponseFileName.empty())
				{
					auto Fd = LqFileOpen(ResponseFileName.c_str(), LQ_O_RD, 0);
					if(Fd == -1)
					{
						fprintf(OutBuffer, " [ReciverLog] ERROR: Not open response file (%s)\n", strerror(lq_errno));
					} else
					{
						LqFileStat stat;
						LqFileGetStatByFd(Fd, &stat);
						auto ResponseData = (LqBindedResponse*)malloc(sizeof(LqBindedResponse) + stat.Size);
						ResponseData->IsProc = false;
						ResponseData->CountPointers = 0;
						ResponseData->SizeData = stat.Size;
						ResponseDataPtr = ResponseData;

						LqFileRead(Fd, ResponseData->Data, stat.Size);
						LqFileClose(Fd);
					}
				}
				char PortBuf[50];
				for(int i = StartPrt; i <= EndPrt; i++)
				{
					sprintf(PortBuf, "%i", (int)i);
					int NewFd = LqConnBind(nullptr, PortBuf, AF_INET, SOCK_STREAM, IPPROTO_TCP, 100, true);
					if(NewFd == -1)
					{
						if(OutBuffer != nullptr)
							fprintf(OutBuffer, " [ReciverLog] ERROR: Not bind to port %i (%s). Continue binding...\n", i, strerror(lq_errno));
						continue;
					}
					auto NewListenSock = LqFastAlloc::New<LqBindedConn>();
					NewListenSock->RspData = ResponseDataPtr;
					NewListenSock->Port = i;
					NewListenSock->TimeLive = TimeLive;
					NewListenSock->SizeRecive = RecvLen;
					LqConnInit(NewListenSock, NewFd, &BindedProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD);
					LqWrkBossAddEvntSync((LqEvntHdr*)NewListenSock);
				}
			}
			break;
			LQSTR_CASE("add_bind_port_proc")
			{

				int StartPrt, EndPrt;
				auto Readed = LqStrToInt(&StartPrt, FullCommand.c_str());
				if(Readed < 1)
				{
					if(OutBuffer != nullptr)
						fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port\n");
					break;
				}
				FullCommand = FullCommand.c_str() + Readed + 1;
				EndPrt = StartPrt;
				if(FullCommand[0] == '-')
				{
					auto Readed2 = LqStrToInt(&EndPrt, FullCommand.c_str() + 1);
					if(Readed2 < 1)
					{
						if(OutBuffer != nullptr)
							fprintf(OutBuffer, " [ReciverLog] ERROR: Invalid port range\n");
						break;
					}
					FullCommand = FullCommand.c_str() + Readed2 + 2;
				}

				for(; FullCommand.length() > 0; )
				{
					if((FullCommand[0] == ' ') || (FullCommand[0] == '\t'))
						FullCommand = FullCommand.c_str() + 1;
					else
						break;
				}
				LqBindedResponsePtr ResponseDataPtr = nullptr;
				auto ResponseData = (LqBindedResponse*)malloc(sizeof(LqBindedResponse) + FullCommand.length() + 4);
				ResponseData->IsProc = true;
				ResponseData->CountPointers = 0;
				ResponseData->SizeData = FullCommand.length();
				memcpy(ResponseData->Data, FullCommand.c_str(), FullCommand.length() + 1);
				ResponseDataPtr = ResponseData;
				char PortBuf[50];
				for(int i = StartPrt; i <= EndPrt; i++)
				{
					sprintf(PortBuf, "%i", (int)i);
					int NewFd = LqConnBind(nullptr, PortBuf, AF_INET, SOCK_STREAM, IPPROTO_TCP, 100, true);
					if(NewFd == -1)
					{
						if(OutBuffer != nullptr)
							fprintf(OutBuffer, " [ReciverLog] ERROR: Not bind to port %i (%s). Continue binding...\n", i, strerror(lq_errno));
						continue;
					}
					auto NewListenSock = LqFastAlloc::New<LqBindedConn>();
					NewListenSock->RspData = ResponseDataPtr;
					NewListenSock->Port = i;
					NewListenSock->TimeLive = 0;
					NewListenSock->SizeRecive = 0;
					LqConnInit(NewListenSock, NewFd, &BindedProto, LQEVNT_FLAG_HUP | LQEVNT_FLAG_RD);
					LqWrkBossAddEvntSync((LqEvntHdr*)NewListenSock);
				}
			}
			break;
			LQSTR_CASE("clear_prt_range")
			{
				for(auto& i : PortRange)
					if(i.LogFd != -1)
						LqFileClose(i.LogFd);
				PortRange.clear();
			}
			break;
			LQSTR_CASE("clear_bind_range")
			{
				LqWrkBossEnumCloseRmEvnt(					
					[](void*, LqEvntHdr* Conn) -> unsigned
					{
						if(LqEvntIsConn(Conn) && (((LqConn*)Conn)->Proto == &BindedProto))
							return 2;
						return 0;
					},
					nullptr
				);
			}
			break;
			LQSTR_CASE("?")
			LQSTR_CASE("help")
			{
				if(OutBuffer)
					fprintf
					(
					OutBuffer,
					" [ReciverLog]\n"
					" Module: ReciverLog\n"
					" hotSAN 2016\n"
					"  ? | help  - Show this help.\n"
					"  format <log or proc vars> - Set log out format (	IpVer, IpLen, IpDscp, IpEcn, IpPktLen,"
					"IpId, IpNoFrag, IpHasFrag, IpOffset, IpTtl, IpProto, IpChecksum, IpSrcAddr, IpDstAddr, TcpSrcPort,"
					"TcpDstPort, TcpSeq, TcpAckSeq, TcpPktLen, TcpFlags, TcpWin, TcpChecksum, TcpUrgPtr, UdpSrcPort, "
					"UdpDstPort, UdpLen, UdpCheck, PktData)\n"
					"  add_prt_range <StartPort>[-<EndPort>] --logfile=<Out Log file> [--logloop=<Max size of log file>] - Out recived packet in log file\n"
					"  add_prt_range_proc <StartPort>[-<EndPort>] <Proc> [args] - Call programm if recived paket (Transfer packet data in stdin)\n"
					"  add_bind_port <StartPort>[-<EndPort>] [--rspfile=<response file>] [--live=<Time live>] [--recvln=<recive len>] - Bind port range\n"
					"  add_bind_port_proc <StartPort>[-<EndPort>] <Proc> [args] - Call programm if have connection (Transfer socket in stdin and stdout)\n"
					"  clear_prt_range - Delete all packet port ranges\n"
					"  clear_bind_range - Delete all bind range\n"
					);
			}
			break;
			LQSTR_SWITCH_DEFAULT
				if(OutBuffer != nullptr)
					fprintf(OutBuffer, " [ReciverLog] ERROR: invalid command\n");
		}

	};

	Mod.FreeNotifyProc =
		[](LqHttpMdl* This) -> uintptr_t
		{
			LqWrkBossEnumCloseRmEvnt(
				[](void*, LqEvntHdr* Conn) -> unsigned
				{
					if(LqEvntIsConn(Conn) && ((((LqConn*)Conn)->Proto == &PktProto) || (((LqConn*)Conn)->Proto == &ClientProto) || (((LqConn*)Conn)->Proto == &BindedProto)))
					{
						return 2;
					} else if(!LqEvntIsConn(Conn) && ((LqEvntFd*)Conn) == &ZmbClr)
					{
						return 2;
					}
					return 0;
				},
				nullptr
			);
			for(auto& i : PortRange)
			{
				if(i.LogFd != -1)
					LqFileClose(i.LogFd);
			}
			return This->Handle;
		};

	return LQHTTPMDL_REG_OK;
}
