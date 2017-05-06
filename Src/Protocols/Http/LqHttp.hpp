#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpCore... - Main handlers of HTTP protocol.
*   C++ Part.
*/

#include "LqConn.h"
#include "LqHttp.h"
#include "LqFile.h"
#include "LqFileTrd.h"
#include "LqHttpPth.hpp"
#include "LqDfltRef.hpp"
#include "LqHndls.hpp"
#include "LqShdPtr.hpp"
#include "LqPtdArr.hpp"

#include <stdarg.h>
#include <vector>

void __LqHttpMdlDelete(LqHttpMdl*);

typedef LqShdPtr<LqHttpMdl, __LqHttpMdlDelete, false, false> LqHttpMdlPtr;


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqHttpData {
	LqZmbClr*                ZmbClr;
	
	size_t					 MaxCountConn;
	size_t	                 CountPtrs;
	LqTimeMillisec			 KeepAlive;

	char                     ServName[1024];

	LqHttpMdl                StartModule;

	LqFileSz                 MaxSkipContentLength;

	char                     HTTPProtoVer[16];
	
	LqTimeSec                PeriodChangeDigestNonce;
	bool                     IsResponse429;
	bool					 UseDefaultDmn;
	void*					 SslCtx;

	size_t                   MaxHeadersSize;
	size_t					 MaxLenRspConveyor;

	LqHttpDmnTbl             Dmns;

	void*                    WrkBoss;
	volatile bool*           IsDeleteFlag;

	LqPtdArr<LqHttpMdlPtr>   Modules;

	LqHndls<LqHttpNotifyFn>  StartQueryHndls;
	LqHndls<LqHttpNotifyFn>  EndResponseHndls;

	LqHndls<LqHttpNotifyFn>  ConnectHndls;
	LqHndls<LqHttpNotifyFn>  DisconnectHndls;
};

#pragma pack(pop)

#define LqHttpConnGetHttpData(HttpConn) ((LqHttpData*)((LqHttp*)(HttpConn)->UserData2)->UserData)
#define LqHttpGetHttpData(Http) ((LqHttpData*)(Http)->UserData)
#define LqHttpPthGetMdl(Path) (((Path)->Type & LQHTTPPTH_FLAG_CHILD) ? ((Path)->Parent->ParentModule) : (Path)->ParentModule)
#define LqHttpConnGetMdl(Conn) ((LqHttpConnGetData(Conn)->Pth == nullptr)? (&LqHttpConnGetHttpData(Conn)->StartModule): (LqHttpPthGetMdl(LqHttpConnGetData(Conn)->Pth)))

#define DISABLE_COPY_CONSTRUCT(NameClass) NameClass(const NameClass&) = delete; NameClass& operator =(const NameClass&) = delete;
#define DEF_CMP_STRING \
inline bool operator==(const LqString& Str) const { return Str == operator LqString(); }\
inline bool operator!=(const LqString& Str) const { return Str != operator LqString(); }\
inline bool operator==(const char* Str) const { return Str == operator LqString(); }\
inline bool operator!=(const char* Str) const { return Str != operator LqString(); }



#pragma pack(push)
#pragma pack(1)

/*
* C++ interface of Lanq Http Connection
* Use for simply create web apps.
*/


class LqHttpConnInterface {
public:

    typedef std::vector<std::pair<LqString, LqString>> ArgsType;
    typedef std::vector<std::pair<LqString, LqString>> HdrsType;
    union {
		class _Rcv {
		public:
			union {
				class _Method {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_Method) DEF_CMP_STRING
					inline operator LqString() const { if(LqHttpConnGetRcvHdrs(Conn)->Method == NULL) return ""; return LqHttpConnGetRcvHdrs(Conn)->Method; }
				} Method;
				class _Domen {
					friend LqHttpConnInterface;
					friend _Rcv;
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_Domen) DEF_CMP_STRING
					inline operator LqString() const { if(LqHttpConnGetRcvHdrs(Conn)->Host == NULL) return ""; return LqHttpConnGetRcvHdrs(Conn)->Host; }
				} Domen;
				class _User {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_User) DEF_CMP_STRING
					inline operator LqString() const { if(LqHttpConnGetRcvHdrs(Conn)->UserInfo == NULL) return ""; return LqHttpConnGetRcvHdrs(Conn)->UserInfo; }
				} User;
				class _Path {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_Path) DEF_CMP_STRING
					inline operator LqString() const { if(LqHttpConnGetRcvHdrs(Conn)->Path == NULL) return ""; return LqHttpConnGetRcvHdrs(Conn)->Path; }
				} Path;
				class _Fragment {
					LqHttpConn* Conn;
				public:  DISABLE_COPY_CONSTRUCT(_Fragment) DEF_CMP_STRING
					inline operator LqString() const { if(LqHttpConnGetRcvHdrs(Conn)->Fragment == NULL) return ""; return LqHttpConnGetRcvHdrs(Conn)->Fragment; }
				} Fragment;
				class _ProtoVer {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_ProtoVer)
					inline operator int() const { return LqHttpConnGetRcvHdrs(Conn)->MinorVer; }
				} MinorVer;
				class _MajorVer {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_MajorVer)
					inline operator int() const { return LqHttpConnGetRcvHdrs(Conn)->MajorVer; }
				} MajorVer;

				class _Arg {
					LqHttpConn* Conn;
					inline LqString Inter(const char * Index, bool& IsHave = LqDfltRef()) const {
						auto l = LqStrLen(Index);
						char* c = LqHttpConnGetRcvHdrs(Conn)->Args, *m = c + ((c) ? LqStrLen(c) : 0);
						char* a, *ea, *sv, *ev;
						while(c < m) {
							a = c;
							for(; (*c != '&') && (*c != '=') && (c < m); c++);
							ea = c;
							if((*c == '=') && (c < m)) {
								c++;
								sv = c;
								for(; (*c != '&') && (c < m); c++);
								ev = c;
							} else {
								sv = ev = c;
							}
							c++;
							if((l == (ea - a)) && LqStrUtf8CmpCaseLen(Index, a, l)) {
								IsHave = true;
								return LqString(sv, ev - sv);
							}
						}
						IsHave = false;
						return LqString();
					}
				public:  DISABLE_COPY_CONSTRUCT(_Arg) DEF_CMP_STRING
					inline operator ArgsType() {
						ArgsType Res;
						char* c = LqHttpConnGetRcvHdrs(Conn)->Args, *m = c + ((c) ? LqStrLen(c) : 0);
						char* a, *ea, *sv, *ev;
						while(c < m) {
							a = c;
							for(; (*c != '&') && (*c != '=') && (c < m); c++);
							ea = c;
							if((*c == '=') && (c < m)) {
								c++;
								sv = c;
								for(; (*c != '&') && (c < m); c++);
								ev = c;
							} else {
								sv = ev = c;
							}
							c++;
							Res.push_back(std::pair<LqString, LqString>(LqString(a, ea - a), LqString(sv, ev - sv)));
						}
						return Res;
					}
					inline LqString operator[](const char* Index) const { return Inter(Index); }
					inline LqString operator[](const LqString& Index) const { return operator[](Index.c_str()); }
					inline bool IsHave(const LqString & Index) const { bool r;  Inter(Index.c_str(), r); return r; }
					inline bool IsHave(const char* Index) const { bool r;  Inter(Index, r); return r; }
					inline operator LqString() const { if(LqHttpConnGetRcvHdrs(Conn)->Args == NULL) return ""; return LqHttpConnGetRcvHdrs(Conn)->Args; }
				} Args;

				class _ContentLen {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_ContentLen)
					inline operator LqFileSz() const { return LqHttpConnRcvGetContentLength(Conn); }
				} ContentLen;
				class _ContentLenLeft {
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_ContentLenLeft)
					inline operator LqFileSz() const { return LqHttpConnRcvGetContentLengthLeft(Conn); }
				} ContentLenLeft;
				class _Hdr {
					LqHttpConn* Conn;
					class _HdrsInterator {
						friend _Hdr;
						LqHttpConn* Conn;
						const char* Name;
						_HdrsInterator(LqHttpConn* NewConn, const char* NewName): Conn(NewConn), Name(NewName) {}
					public: _HdrsInterator& operator =(const _HdrsInterator&) = delete; DEF_CMP_STRING
						inline operator LqString() const {
							LqHttpConnLock(Conn);
							const char* Hdr = LqHttpConnRcvHdrGet(Conn, Name);
							LqString Res = (Hdr) ? Hdr : "";
							LqHttpConnUnlock(Conn);
							return Res;
						}
						inline int Scanf(const char* FormatStr, ...) {
							va_list Va;
							int Res = -1;
							va_start(Va, FormatStr);
							LqHttpConnLock(Conn);
							const char* Hdr = LqHttpConnRcvHdrGet(Conn, Name);
							if(Hdr != NULL)
								Res = LqFbuf_vssncanf(Hdr, LqStrLen(Hdr), FormatStr, Va);
							LqHttpConnUnlock(Conn);
							va_end(Va);
							return Res;
						}
					};
				public: DISABLE_COPY_CONSTRUCT(_Hdr)
					class interator {
						friend _Hdr;
						LqHttpConn* Conn;
						int Index;
						interator(LqHttpConn* NewConn, int NewIndex): Conn(NewConn), Index(NewIndex) {}
					public:
						inline interator& operator++() { ++Index; return (*this); }
						inline interator operator++(int) { ++Index; return *this; }
						inline interator& operator--() { --Index; return (*this); }
						inline interator operator--(int) { --Index; return *this; }
						inline LqHttpRcvHdr& operator*() const { return LqHttpConnGetRcvHdrs(Conn)->Hdrs[Index]; }
						inline bool operator!=(const interator& Another) const { return Index != Another.Index; }
						inline bool operator==(const interator& Another) const { return Index == Another.Index; }
					};

					inline operator HdrsType() const {
						HdrsType Res;
						auto Hdrs = LqHttpConnGetRcvHdrs(Conn)->Hdrs;
						int Count = LqHttpConnGetRcvHdrs(Conn)->CountHdrs;
						for(int i = 0; i < Count; i++)
							Res.push_back(std::pair<LqString, LqString>(Hdrs[i].Name, Hdrs[i].Val));
						return Res;
					}
					inline _HdrsInterator operator[](const char* Index) const { return _HdrsInterator(Conn, Index); }
					inline _HdrsInterator operator[](const LqString& Index) const { return operator[](Index.c_str()); }

					inline interator begin() const { return interator(Conn, 0); }
					inline interator end() const { return interator(Conn, LqHttpConnGetRcvHdrs(Conn)->CountHdrs); }
				} Hdrs;
				class _Boundary {
					LqHttpConn* Conn;
				public:  DISABLE_COPY_CONSTRUCT(_Boundary) DEF_CMP_STRING
					inline operator LqString() const {
						int Len; 
						if((Len = LqHttpConnRcvGetBoundary(Conn, NULL, NULL)) <= 0) 
							return "";
						LqString Res(Len, '\0');
						LqHttpConnRcvGetBoundary(Conn, (char*)Res.data(), Len);
						return Res;
					}
				} Boundary;
			};

			bool WaitLen(std::function<void(LqHttpConnRcvResult*)> Func, intptr_t RecvDataLen = -((intptr_t)1)) {
				std::function<void(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<void(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return false;
				if(!LqHttpConnRcvWaitLen(Domen.Conn, [](LqHttpConnRcvResult* Res) {
					std::function<void(LqHttpConnRcvResult*)>* Data = (std::function<void(LqHttpConnRcvResult*)>*)Res->UserData;
					Res->UserData = nullptr;
					Data->operator()(Res);
					LqFastAlloc::Delete(Data);
				}, Data, RecvDataLen)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
			bool ReadFile(
				const char* Path = nullptr,
				std::function<bool(LqHttpConnRcvResult*)> Func = nullptr,
				LqFileSz ReadLen = -((LqFileSz)1),
				int Access = 0666,
				bool IsReplace = true,
				bool IsCreateSubdir = true
			) {
				if(Func == nullptr)
					return LqHttpConnRcvFile(Domen.Conn, Path, nullptr, nullptr, ReadLen, Access, IsReplace, IsCreateSubdir);
				std::function<bool(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<bool(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return false;
				if(!LqHttpConnRcvFile(Domen.Conn, Path, [](LqHttpConnRcvResult* RcvRes) {
					std::function<bool(LqHttpConnRcvResult*)>* Data = (std::function<bool(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					bool Res = Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
					return Res;
				}, Data, ReadLen, Access, IsReplace, IsCreateSubdir)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
			bool ReadFbuf(
				LqFbuf* Dest,
				std::function<void(LqHttpConnRcvResult*)> Func,
				LqFileSz ReadLen = -((LqFileSz)1)
			) {
				std::function<void(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<void(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return false;
				if(!LqHttpConnRcvFbuf(Domen.Conn, Dest, [](LqHttpConnRcvResult* RcvRes) {
					std::function<void(LqHttpConnRcvResult*)>* Data = (std::function<void(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
				}, Data, ReadLen)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
			bool ReadFileAboveBoundary(
				const char* Path = nullptr,
				std::function<bool(LqHttpConnRcvResult*)> Func = nullptr,
				const char* Boundary = nullptr,
				LqFileSz MaxLen = -((LqFileSz)1),
				int Access = 0666,
				bool IsReplace = true,
				bool IsCreateSubdir = true
			) {
				if(Func == nullptr)
					return LqHttpConnRcvFileAboveBoundary(Domen.Conn, Path, nullptr, nullptr, Boundary, MaxLen, Access, IsReplace, IsCreateSubdir);
				std::function<bool(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<bool(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return false;
				if(!LqHttpConnRcvFileAboveBoundary(Domen.Conn, Path, [](LqHttpConnRcvResult* RcvRes) {
					std::function<bool(LqHttpConnRcvResult*)>* Data = (std::function<bool(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					bool Res = Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
					return Res;
				}, Data, Boundary, MaxLen, Access, IsReplace, IsCreateSubdir)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
			bool ReadFbufAboveBoundary(
				LqFbuf* Dest,
				std::function<void(LqHttpConnRcvResult*)> Func,
				const char* Boundary = nullptr,
				LqFileSz MaxLen = -((LqFileSz)1)
			) {
				std::function<void(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<void(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return false;
				if(!LqHttpConnRcvFbufAboveBoundary(Domen.Conn, Dest, [](LqHttpConnRcvResult* RcvRes) {
					std::function<void(LqHttpConnRcvResult*)>* Data = (std::function<void(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
				}, Data, Boundary, MaxLen)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
			int ReadMultipartHdrs(
				std::function<void(LqHttpConnRcvResult*)> Func,
				const char* Boundary = nullptr,
				LqHttpMultipartHdrs** Dest = nullptr /* If we try to get the headers right now */
			) {
				int Res = -1;
				std::function<void(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<void(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return Res;
				if(((Res = LqHttpConnRcvMultipartHdrs(Domen.Conn, [](LqHttpConnRcvResult* RcvRes) {
					std::function<void(LqHttpConnRcvResult*)>* Data = (std::function<void(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
				}, Data, Boundary, Dest)) == 0) || (Res == 1)) {
					LqFastAlloc::Delete(Data);
					return Res;
				}
				return Res;
			}
			bool ReadMultipartFbufNext(
				LqFbuf* Dest,
				std::function<void(LqHttpConnRcvResult*)> Func,
				const char* Boundary = nullptr,
				LqFileSz MaxLen = -((LqFileSz)1)
			) {
				std::function<void(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<void(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) return false;
				if(!LqHttpConnRcvMultipartFbufNext(Domen.Conn, Dest, [](LqHttpConnRcvResult* RcvRes) {
					std::function<void(LqHttpConnRcvResult*)>* Data = (std::function<void(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
				}, Data, Boundary, MaxLen)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
			bool ReadMultipartFileNext(
				const char* Path = nullptr,
				std::function<bool(LqHttpConnRcvResult*)> Func = nullptr,
				const char* Boundary = nullptr,
				LqFileSz MaxLen = -((LqFileSz)1),
				int Access = 0666,
				bool IsReplace = true,
				bool IsCreateSubdir = true
			) {
				if(Func == nullptr)
					return LqHttpConnRcvMultipartFileNext(Domen.Conn, Path, nullptr, nullptr, Boundary, MaxLen, Access, IsReplace, IsCreateSubdir);
				std::function<bool(LqHttpConnRcvResult*)>* Data = LqFastAlloc::New<std::function<bool(LqHttpConnRcvResult*)>>(Func);
				if(Data == nullptr) 
					return false;
				if(!LqHttpConnRcvMultipartFileNext(Domen.Conn, Path, [](LqHttpConnRcvResult* RcvRes) {
					std::function<bool(LqHttpConnRcvResult*)>* Data = (std::function<bool(LqHttpConnRcvResult*)>*)RcvRes->UserData;
					RcvRes->UserData = nullptr;
					bool Res = Data->operator()(RcvRes);
					LqFastAlloc::Delete(Data);
					return Res;
				}, Data, Boundary, MaxLen, Access, IsReplace, IsCreateSubdir)) {
					LqFastAlloc::Delete(Data);
					return false;
				}
				return true;
			}
		} Rcv;


        class _Rsp {
        public:
            union {
                class _Hdr {
                    friend _Rsp;
                    LqHttpConn* Conn;
                    class _HdrsInterator {
                        friend _Hdr;
                        LqHttpConn* Conn;
                        const char* Name;
                        _HdrsInterator(LqHttpConn* NewConn, const char* NewName): Conn(NewConn), Name(NewName) {}
                    public: _HdrsInterator& operator =(const _HdrsInterator&) = delete; DEF_CMP_STRING
						inline operator LqString() const { const char* hvs = ""; size_t Len = 0; LqHttpConnRspHdrGet(Conn, Name, &hvs, &Len); return LqString(hvs, Len); }
                        inline const char* operator=(const char* Val) const { LqHttpConnRspHdrInsert(Conn, Name, Val); return Val; }
                        inline LqString& operator=(const LqString& Val) const { LqHttpConnRspHdrInsert(Conn, Name, Val.c_str()); return (LqString&)Val; }
                        inline int Printf(const char* Format, ...) {
                            va_list Va;
							int Res;
							char Buf[10000];
                            va_start(Va, Format);
							Res = LqFbuf_svnprintf(Buf, sizeof(Buf) - 3, Format, Va);
							LqHttpConnRspHdrInsert(Conn, Name, Buf);
							va_end(Va);
                            return Res;
                        }
                        inline bool Remove() { return LqHttpConnRspHdrInsert(Conn, Name, NULL); }
                    };
                public: DISABLE_COPY_CONSTRUCT(_Hdr)
                    inline operator HdrsType() const {
						HdrsType Res;
						const char* StartName = NULL, *StartVal;
						size_t Len, LenVal;
						LqHttpConnLock(Conn);
						while(LqHttpConnRspHdrEnumNext(Conn, &StartName, &Len, &StartVal, &LenVal))
							Res.push_back(std::pair<LqString, LqString>(LqString(StartName, Len), LqString(StartVal, LenVal)));
						LqHttpConnUnlock(Conn);
						return Res;
					}
                    inline _HdrsInterator operator[](const char* Index) const { return _HdrsInterator(Conn, Index); }
                    inline _HdrsInterator operator[](const LqString& Index) const { return _HdrsInterator(Conn, Index.c_str()); }
                } Hdrs;
				class _Status {
					LqHttpConn* Conn;
				public:  DISABLE_COPY_CONSTRUCT(_Status)
					inline operator int() const { return LqHttpConnGetData(Conn)->RspStatus; }
					inline int operator=(int NewVal) const { return LqHttpConnGetData(Conn)->RspStatus = NewVal; }
				} Status;
				class _IsNoBody {
					LqHttpConn* Conn;
				public:  DISABLE_COPY_CONSTRUCT(_IsNoBody)
					inline operator bool() const { return LqHttpConnGetData(Conn)->Flags & LQHTTPCONN_FLAG_NO_BODY; }
					inline bool operator=(bool NewVal) const {
						if(NewVal) 
							LqHttpConnGetData(Conn)->Flags |= LQHTTPCONN_FLAG_NO_BODY; 
						else 
							LqHttpConnGetData(Conn)->Flags &= ~LQHTTPCONN_FLAG_NO_BODY; 
						return NewVal;
					}
				} IsNoBody;
				class _IsClose {
					LqHttpConn* Conn;
				public:  DISABLE_COPY_CONSTRUCT(_IsClose)
					inline operator bool() const { return LqHttpConnGetData(Conn)->Flags & LQHTTPCONN_FLAG_CLOSE; }
						 inline bool operator=(bool NewVal) const {
							 if(NewVal)
								 LqHttpConnGetData(Conn)->Flags |= LQHTTPCONN_FLAG_CLOSE;
							 else
								 LqHttpConnGetData(Conn)->Flags &= ~LQHTTPCONN_FLAG_CLOSE;
							 return NewVal;
						 }
				} IsClose;

				class _ContentLen {
					LqHttpConn* Conn;
				public:  DISABLE_COPY_CONSTRUCT(_ContentLen)
					inline operator LqFileSz() { return LqHttpConnRspGetContentLength(Conn); }
				} ContentLen;
            };
            inline void Error(int Status) { LqHttpConnRspError(Hdrs.Conn, Status); }
        } Rsp;

        class _RemoteIp {
            LqHttpConn* Conn;
        public: DISABLE_COPY_CONSTRUCT(_RemoteIp) DEF_CMP_STRING
            inline operator LqString() const {
				char Buf[256];
				Buf[0] = '\0';
				LqHttpConnGetRemoteIpStr(Conn, Buf, 255);
				return Buf;
			}
            /*@return: For IPv4 - 4, for IPv6 - 6, on error - 0*/
            inline int GetType() const {
				LqConnAddr Addr = {0};
				LqHttpConnGetRemoteIp(Conn, &Addr);
				if(Addr.Addr.sa_family == AF_INET)
					return 4;
				if(Addr.Addr.sa_family == AF_INET6)
					return 6;
				return 0;
            }
        } RemoteIp;

        class _RemotePort {
            LqHttpConn* Conn;
        public: DISABLE_COPY_CONSTRUCT(_RemotePort) DEF_CMP_STRING
            inline operator int() const {
				LqConnAddr Addr = {0};
				LqHttpConnGetRemoteIp(Conn, &Addr);
				if(Addr.Addr.sa_family == AF_INET)
					return ntohs(Addr.AddrInet.sin_port);
				if(Addr.Addr.sa_family == AF_INET6)
					return ntohs(Addr.AddrInet6.sin6_port);
				return 0;
			}
            inline operator LqString() const { return LqToString(operator int()); }
        } RemotePort;

        class __UserData {
            LqHttpConn* Conn;

        public:DISABLE_COPY_CONSTRUCT(__UserData)
            class ___Interator {
            LqHttpConn* Conn;
            void* UniquePointer;
            public:
                inline ___Interator(LqHttpConn* c, void* v): Conn(c), UniquePointer(v) {}
                template<typename TypeResult>
                inline operator TypeResult() const {
                    void* Res = nullptr;
                    LqHttpConnDataGet(Conn, UniquePointer, &Res);
                    return (TypeResult)Res;
                }
                template<typename TypeResult>
                inline TypeResult operator =(TypeResult v) {
                    void* NewVal = (void*)v;
                    LqHttpConnDataStore(Conn, UniquePointer, NewVal);
                    return v;
                }
                inline bool Delete() const {
                    return LqHttpConnDataUnstore(Conn, UniquePointer) != -1;
                }
            };
            ___Interator operator[] (void* UniquePointer) { return ___Interator(Conn, UniquePointer); }

        } UserData;

        class _Row {
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_Row)
            inline operator LqHttpConn*() const { return Conn; }
               inline LqHttpConn* operator =(LqHttpConn* NewConn) { return Conn = NewConn; }
        } Row;

   };
	bool RspFileAuto(const char* PathToFile = nullptr, const char* Boundary = nullptr) {
		return LqHttpConnRspFileAuto(Rcv.Domen.Conn, PathToFile, Boundary);
	}
   
    inline intptr_t TryScanf(int Flags, const char* Fmt, ...) {
		va_list Va;
		intptr_t Res;
		va_start(Va, Fmt);
		Res = LqHttpConnRcvTryScanfVa(Rcv.Domen.Conn, Flags, Fmt, Va);
		va_end(Va);
		return Res;
    }
	inline intptr_t TryRead(void* DestBuf, intptr_t Len) { return LqHttpConnRcvTryRead(Rcv.Domen.Conn, DestBuf, Len); }
	inline intptr_t TryPeek(void* DestBuf, intptr_t Len) { return LqHttpConnRcvTryPeek(Rcv.Domen.Conn, DestBuf, Len); }
	inline bool WriteFilePart(const char* Path, LqFileSz Offset, LqFileSz Length) { return LqHttpConnRspFilePart(Rcv.Domen.Conn, Path, Offset, Length); }
	inline bool WriteFile(const char* Path) { return LqHttpConnRspFile(Rcv.Domen.Conn, Path); }
	inline bool Write(const void* Data, size_t Len) { return LqHttpConnRspWrite(Rcv.Domen.Conn, Data, Len); }
	inline intptr_t Printf(const char* Fmt, ...) {
		va_list Va;
		intptr_t Res;
		va_start(Va, Fmt);
		Res = LqHttpConnRspPrintfVa(Rcv.Domen.Conn, Fmt, Va);
		va_end(Va);
		return Res;
	}
	inline LqHttpConnInterface& operator>>(char* Dst) { 
		LqFileSz LeftSize = Rcv.ContentLenLeft; 
		if(LeftSize > ((LqFileSz)0))
			LqHttpConnRcvTryRead(Rcv.Domen.Conn, Dst, LeftSize); 
		return *this; 
	}
	template<size_t ArgLen>
	inline LqHttpConnInterface& operator >> (char(&Dst)[ArgLen]) {
		LqFileSz LeftSize = Rcv.ContentLenLeft;
		if(LeftSize > ((LqFileSz)0))
			LqHttpConnRcvTryRead(Rcv.Domen.Conn, Dst, lq_min(LeftSize, (LqFileSz)ArgLen));
		return *this;
	}
	inline LqHttpConnInterface& operator>>(LqString& Dst){
		LqHttpConnLock(Rcv.Domen.Conn);
		LqFileSz LeftSize = Rcv.ContentLenLeft;
		if(LeftSize > ((LqFileSz)0)) {
			size_t Len = LqSockBufRcvBufSz(Rcv.Domen.Conn);
			Dst.resize(Len);
			LqHttpConnRcvTryRead(Rcv.Domen.Conn, (char*)Dst.data(), lq_min(LeftSize, (LqFileSz)Len));
		}
		LqHttpConnUnlock(Rcv.Domen.Conn);
		return *this; 
	}
	inline LqHttpConnInterface& operator<<(const char* Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%s", Src); return *this; }
	inline LqHttpConnInterface& operator<<(LqString& Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%s", Src.c_str()); return *this; }
	inline LqHttpConnInterface& operator<<(int Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%i", Src); return *this; }
	inline LqHttpConnInterface& operator<<(long long Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%lli", Src); return *this; }
	inline LqHttpConnInterface& operator<<(long double Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%llg", Src); return *this; }
	inline LqHttpConnInterface& operator<<(float Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%g", Src); return *this; }
	inline LqHttpConnInterface& operator<<(bool Src) { LqHttpConnRspPrintf(Rcv.Domen.Conn, "%s", Src? "true": "false"); return *this; }
    inline LqHttpConnInterface(LqHttpConn* Conn) { Rcv.Domen.Conn = Conn; }
	inline LqHttpConnInterface(const LqHttpConnInterface& Conn) { Rcv.Domen.Conn = Conn.Rcv.Domen.Conn; }
	inline bool BeginLongPoll(void (LQ_CALL *LongPollCloseHandler)(LqHttpConn*) = nullptr) { return LqHttpConnRspBeginLongPoll(Rcv.Domen.Conn, LongPollCloseHandler); }
	inline bool EndLongPoll() { return LqHttpConnRspEndLongPoll(Rcv.Domen.Conn); }
};

#pragma pack(pop)

#define __METHOD_DECLS__
#include "LqAlloc.hpp"