#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqHttpCore... - Main handlers of HTTP protocol.
*   C++ Part.
*/

struct LqHttpConn;
struct LqHttpProto;
struct LqHttpQuery;

#include "LqConn.h"
#include "LqHttp.h"
#include "LqFile.h"
#include "LqFileTrd.h"
#include "LqFileChe.hpp"
#include "LqHttpPth.hpp"
#include "LqMd5.h"
#include "LqHttpAct.h"
#include "LqHttpRcv.h"
#include "LqHttpRsp.h"
#include "LqHttpConn.h"
#include "LqDfltRef.hpp"
#include "LqHndls.hpp"
#include "LqShdPtr.hpp"
#include "LqPtdArr.hpp"

#include <stdarg.h>
#include <vector>

#define LqHttpGetReg(ConnectionPointer) ((LqHttpProto*)((LqConn*)ConnectionPointer)->Proto)

void __LqHttpMdlDelete(LqHttpMdl*);

typedef LqShdPtr<LqHttpMdl, __LqHttpMdlDelete, false, false> LqHttpMdlPtr;


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

struct LqCachedFileHdr
{
private:
    LqMd5  Hash;
public:
    char*  Etag;
    char*  MimeType;
    void   GetMD5(const void * CacheInterator, LqMd5* Dest);

    void Cache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, const LqFileStat* Stat);
    void Recache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime, const LqFileStat * Stat);
    inline void Uncache(const char* Path, void* Buf, size_t SizeBuf, time_t LastModifTime) const {}
};


struct LqHttpProto
{
    LqHttpProtoBase                 Base;
	LqHttpDmnTbl                    Dmns;
    LqFileChe<LqCachedFileHdr>      Cache;

	LqPtdArr<LqHttpMdlPtr>          Modules;

    LqString                        Port;
    LqString                        Host;

    uintptr_t                       CountPointers;

    LqHndls<LqHttpNotifyFn>         StartQueryHndls;
    LqHndls<LqHttpNotifyFn>         EndResponseHndls;

    LqHndls<LqHttpNotifyFn>         ConnectHndls;
    LqHndls<LqHttpNotifyFn>         DisconnectHndls;
};

#pragma pack(pop)

#define DISABLE_COPY_CONSTRUCT(NameClass) NameClass(const NameClass&) = delete; NameClass& operator =(const NameClass&) = delete;
#define DEF_CMP_STRING \
inline bool operator==(const LqString& Str) const { return Str == operator LqString(); }\
inline bool operator!=(const LqString& Str) const { return Str != operator LqString(); }\
inline bool operator==(const char* Str) const { return Str == operator LqString(); }\
inline bool operator!=(const char* Str) const { return Str != operator LqString(); }



#pragma pack(push)
#pragma pack(1)

/*
* C++ interface of Lanq API
* Use for simply create web apps.
*/


class LqHttpConnInterface
{
public:

    typedef std::vector<std::pair<LqString, LqString>> ArgsType;
    typedef std::vector<std::pair<LqString, LqString>> HdrsType;
    union
    {
        class _Quer
        {
        public:

            inline operator bool() const { return LqHttpActGetClassByConn(Domen.Conn) == LQHTTPACT_CLASS_QER; }

            union
            {
                class _Method
                {
                    LqHttpConn* Conn;
                public: DISABLE_COPY_CONSTRUCT(_Method) DEF_CMP_STRING
                    inline operator LqString() const { if(Conn->Query.Method == nullptr) return ""; return LqString(Conn->Query.Method, Conn->Query.MethodLen); }
                } Method;
                class _Domen
                {
                    friend LqHttpConnInterface;
                    friend _Quer;
                    LqHttpConn* Conn;
                public: DISABLE_COPY_CONSTRUCT(_Domen) DEF_CMP_STRING
                    inline operator LqString() const { if(Conn->Query.Host == nullptr) return ""; return LqString(Conn->Query.Host, Conn->Query.HostLen); }
                } Domen;
                class _User
                {
                    LqHttpConn* Conn;
                public: DISABLE_COPY_CONSTRUCT(_User) DEF_CMP_STRING
                    inline operator LqString() const { if(Conn->Query.UserInfo == nullptr) return ""; return LqString(Conn->Query.UserInfo, Conn->Query.UserInfoLen); }
                } User;

                class _Path
                {
                    LqHttpConn* Conn;
                public: DISABLE_COPY_CONSTRUCT(_Path) DEF_CMP_STRING
                    inline operator LqString() const { if(Conn->Query.Path == nullptr) return ""; return LqString(Conn->Query.Path, Conn->Query.PathLen); }
                } Path;
                class _Fragment
                {
                    LqHttpConn* Conn;
                public:  DISABLE_COPY_CONSTRUCT(_Fragment) DEF_CMP_STRING
                    inline operator LqString() const { if(Conn->Query.Fragment == nullptr) return ""; return LqString(Conn->Query.Fragment, Conn->Query.FragmentLen); }
                } Fragment;

                class _ProtoVer
                {
                    LqHttpConn* Conn;
                public: DISABLE_COPY_CONSTRUCT(_ProtoVer) DEF_CMP_STRING
                    inline operator LqString() const { if(Conn->Query.ProtoVer == nullptr) return ""; return LqString(Conn->Query.ProtoVer, Conn->Query.ProtoVerLen); }
                } ProtoVer;

                class _Arg
                {
                    LqHttpConn* Conn;
                    inline LqString Inter(const char * Index, bool& IsHave = LqDfltRef()) const
                    {
                        auto l = LqStrLen(Index);
                        char* c = Conn->Query.Arg, *m = c + Conn->Query.ArgLen;
                        while(c < m)
                        {
                            char* a = c, *ea, *sv, *ev;
                            for(; (*c != '&') && (*c != '=') && (c < m); c++);
                            ea = c;
                            if((*c == '=') && (c < m))
                            {
                                c++;
                                sv = c;
                                for(; (*c != '&') && (c < m); c++);
                                ev = c;
                            } else
                            {
                                sv = ev = c;
                            }
                            c++;
                            if((l == (ea - a)) && LqStrUtf8CmpCaseLen(Index, a, l))
                            {
                                IsHave = true;
                                return LqString(sv, ev - sv);
                            }
                        }
                        IsHave = false;
                        return LqString();
                    }
                public:  DISABLE_COPY_CONSTRUCT(_Arg)

                    inline operator ArgsType()
                {
                    ArgsType Res;
                    char* c = Conn->Query.Arg, *m = c + Conn->Query.ArgLen;
                    while(c < m)
                    {
                        char* a = c, *ea, *sv, *ev;
                        for(; (*c != '&') && (*c != '=') && (c < m); c++);
                        ea = c;
                        if((*c == '=') && (c < m))
                        {
                            c++;
                            sv = c;
                            for(; (*c != '&') && (c < m); c++);
                            ev = c;
                        } else
                        {
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
                         inline operator LqString() const { return LqString(Conn->Query.Arg, Conn->Query.ArgLen); }
                } Args;

                class _ContentLen
                {
                    LqHttpConn* Conn;
                public: DISABLE_COPY_CONSTRUCT(_ContentLen)
                    inline operator LqFileSz() const { return Conn->Query.ContentLen; }
                } ContentLen;

				class _StartLine
				{
					LqHttpConn* Conn;
				public: DISABLE_COPY_CONSTRUCT(_StartLine) DEF_CMP_STRING
					inline operator LqString() const 
					{ 
						if(Conn->Query.Method == nullptr) 
							return "";
						char* c = Conn->Query.Method;
						for(char* m = c + Conn->Query.HeadersEnd;((c[0] != '\r') || (c[1] != '\n')) && (c < m); c++);
						return LqString(Conn->Query.Method, c); 
					}
                    bool Get(char**Start, char** End)
                    {
                        if(Conn->Query.Method == nullptr)
                            return false;
                        char* c = Conn->Query.Method;
                        for(char* m = c + Conn->Query.HeadersEnd; ((c[0] != '\r') || (c[1] != '\n')) && (c < m); c++);
                        *Start = Conn->Query.Method;
                        *End = c;
                        return true;
                    }
				} StartLine;

                class _Hdr
                {
                    LqHttpConn* Conn;
                    class _HdrsInterator
                    {
                        friend _Hdr;
                        LqHttpConn* Conn;
                        const char* Name;
                        _HdrsInterator(LqHttpConn* NewConn, const char* NewName): Conn(NewConn), Name(NewName) {}
                    public: _HdrsInterator& operator =(const _HdrsInterator&) = delete; DEF_CMP_STRING
                        inline operator LqString() const { char* vs = "", *ve = vs; LqHttpRcvHdrSearch(Conn, 0, Name, nullptr, &vs, &ve); return LqString(vs, ve - vs); }
                            inline int Scanf(size_t Deep, const char* FormatStr, ...)
                            {
                                va_list Va;
                                va_start(Va, FormatStr);
                                return LqHttpRcvHdrScanfVa(Conn, Deep, Name, FormatStr, Va);
                            }
                    };
                public: DISABLE_COPY_CONSTRUCT(_Hdr)
                    inline operator HdrsType() const
					{
						HdrsType Res;
						char* HeaderNameResult = nullptr, *HeaderNameResultEnd = nullptr, *HeaderValResult = nullptr, *HeaderValEnd = nullptr;
						while(LqHttpRcvHdrEnum(Conn, &HeaderNameResult, &HeaderNameResultEnd, &HeaderValResult, &HeaderValEnd) >= 0)
							Res.push_back(std::pair<LqString, LqString>(LqString(HeaderNameResult, HeaderNameResultEnd - HeaderNameResult), LqString(HeaderValResult, HeaderValEnd - HeaderValResult)));
						return Res;
					}
                    inline operator LqString() const { return Conn->Buf; }
                    inline LqString operator[](const char* Index) const { return _HdrsInterator(Conn, Index); }
                    inline LqString operator[](const LqString& Index) const { return operator[](Index.c_str()); }
                } Hdrs;



                struct _Stream
                {
                    class _Size
                    {
                        friend _Stream;
                        LqHttpConn* Conn;
                    public: DISABLE_COPY_CONSTRUCT(_Size)
                        inline operator intptr_t() const
                    {
                        if(Conn->ActionState == LQHTTPACT_STATE_RCV_STREAM)
                            return Conn->Query.Stream.Len;
                        return -1;
                    }
                    } Size;
                    DISABLE_COPY_CONSTRUCT(_Stream)
                        inline LqString & operator >> (LqString & Str)
                    {
                        char Buf[1024];
                        for(;;)
                        {
                            intptr_t Readed;
                            if((Readed = LqHttpRcvStreamRead(Size.Conn, Buf, sizeof(Buf))) < 0)
                                return (LqString&)Str;
                            Str.append(Buf, Readed);
                            if(Readed < sizeof(Buf))
                                return Str;
                        }
                        return Str;
                    }
                    inline intptr_t Read(void * Buf, intptr_t BufLen) { return LqHttpRcvStreamRead(Size.Conn, Buf, BufLen); }
                    inline intptr_t Peek(void * Buf, intptr_t BufLen) { return LqHttpRcvStreamPeek(Size.Conn, Buf, BufLen); }

                    inline operator LqString() const
                    {
                        if(Size.Conn->ActionState != LQHTTPACT_STATE_RCV_STREAM)
                            return LqString();
                        char Buf[1024];
                        LqSbufPtr BufPtr;
                        LqSbufPtrSet(&Size.Conn->Query.Stream, &BufPtr);
                        LqString Result;
                        for(;;)
                        {
                            intptr_t Readed;
                            if((Readed = LqSbufReadByPtr(&Size.Conn->Query.Stream, &BufPtr, Buf, sizeof(Buf))) < 0)
                                break;
                            Result.append(Buf, Readed);
                            if(Readed < sizeof(Buf))
                                return Result;
                        }
                        return Result;

                    }

                    inline operator bool() const { return Size.Conn->ActionState == LQHTTPACT_STATE_RCV_STREAM; }
                    inline LqHttpRcvFileResultEnm Set(LqFileSz Sz = LQ_MAX_CONTENT_LEN) { return LqHttpRcvStream(Size.Conn, Sz); }
                } Stream;

                struct _File
                {
                    DISABLE_COPY_CONSTRUCT(_File)
                        union
                    {
                        class _Descriptor
                        {
                            friend _File;
                            LqHttpConn* Conn;
                        public: DISABLE_COPY_CONSTRUCT(_Descriptor)
                            inline operator int() const
                        {
                            if(Conn->ActionState != LQHTTPACT_STATE_RCV_FILE)
                                return -1;
                            return Conn->Query.OutFd;
                        }
                        } Descriptor;
                        class _TargetName
                        {
                            LqHttpConn* Conn;
                        public: DISABLE_COPY_CONSTRUCT(_TargetName) DEF_CMP_STRING
                            inline operator LqString() const { char Buf[LQ_MAX_PATH]; LqFileGetPath(Conn->Query.OutFd, Buf, sizeof(Buf) - 1); return Buf; }
                        } TargetPath;
                    };

                    inline operator bool() const { return (Descriptor.Conn->ActionState == LQHTTPACT_STATE_RCV_FILE) && (LqFileTrdIsTransacted(Descriptor.Conn->Query.OutFd) != 1); }
                    inline LqHttpRcvFileResultEnm Set(int Fd, LqFileSz ReadLen = LQ_MAX_CONTENT_LEN) { return LqHttpRcvFileByFd(Descriptor.Conn, Fd, ReadLen); }
                } File;

                struct _TrdFile
                {
                    DISABLE_COPY_CONSTRUCT(_TrdFile)
                        union
                    {
                        class _TempPath
                        {
                            friend _TrdFile;
                            LqHttpConn* Conn;
                        public: DISABLE_COPY_CONSTRUCT(_TempPath) DEF_CMP_STRING
                            inline  operator LqString() const { char Buf[LQ_MAX_PATH]; LqHttpRcvFileGetTargetName(Conn, Buf, sizeof(Buf) - 1); return Buf; }
                        } TempPath;
                        class _TargetPath
                        {
                            LqHttpConn* Conn;
                        public: DISABLE_COPY_CONSTRUCT(_TargetPath) DEF_CMP_STRING
                            inline operator LqString() const { char Buf[LQ_MAX_PATH]; LqHttpRcvFileGetTargetName(Conn, Buf, sizeof(Buf) - 1); return Buf; }
                        } TargetPath;
                    };
                    inline operator bool() const { return (TempPath.Conn->ActionState == LQHTTPACT_STATE_RCV_FILE) && (LqFileTrdIsTransacted(TempPath.Conn->Query.OutFd) == 1); }

                    inline LqHttpRcvFileResultEnm Set(const char* Path, bool IsCreateSubdir = true, bool IsReplace = true, int Access = 0666, LqFileSz ReadLen = LQ_MAX_CONTENT_LEN)
                    {
                        return LqHttpRcvFile(TempPath.Conn, Path, ReadLen, Access, IsReplace, IsCreateSubdir);
                    }
                    inline LqHttpRcvFileResultEnm Commit() { return LqHttpRcvFileCommit(TempPath.Conn); }
                    inline LqHttpRcvFileResultEnm CommitToPlace(const char* DestPath) { return LqHttpRcvFileCommitToPlace(TempPath.Conn, DestPath); }
                    inline LqHttpRcvFileResultEnm Cancel() { return LqHttpRcvFileCancel(TempPath.Conn); }
                } TrdFile;

                class _Multipart
                {
                public: DISABLE_COPY_CONSTRUCT(_Multipart)
                    union
                {
                    class _Deep
                    {
                        friend _Multipart;
                        LqHttpConn* Conn;
                    public: DISABLE_COPY_CONSTRUCT(_Deep)
                        inline operator size_t() const { return LqHttpRcvMultipartHdrGetDeep(Conn); }
                    } Deep;

                };

                inline operator bool()
                {
                    return (Deep.Conn->ActionState == LQHTTPACT_STATE_MULTIPART_RCV_FILE) ||
                        (Deep.Conn->ActionState == LQHTTPACT_STATE_MULTIPART_RCV_HDRS) ||
                        (Deep.Conn->ActionState == LQHTTPACT_STATE_MULTIPART_RCV_STREAM) ||
                        (Deep.Conn->ActionState == LQHTTPACT_STATE_MULTIPART_SKIP_AND_GET_HDRS) ||
                        (Deep.Conn->ActionState == LQHTTPACT_STATE_MULTIPART_SKIP_TO_HDRS);
                }

                inline LqHttpRcvFileResultEnm RcvHdrs() { return LqHttpRcvMultipartHdrRecive(Deep.Conn); }
                inline LqHttpRcvFileResultEnm SkipContent() { return LqHttpRcvMultipartSkip(Deep.Conn); }
                inline LqHttpRcvFileResultEnm HdrRemoveLast() { return LqHttpRcvMultipartHdrRemoveLast(Deep.Conn); }
                inline LqHttpRcvFileResultEnm HdrRemoveAll() { return LqHttpRcvMultipartHdrRemoveAll(Deep.Conn); }
                inline LqHttpRcvFileResultEnm SevePartInFile(const char* Path, bool IsCreateSubdir = true, bool IsReplace = true, int Access = 0666)
                {
                    return LqHttpRcvMultipartInFile(Deep.Conn, Path, Access, IsCreateSubdir, IsReplace);
                }
                LqHttpRcvFileResultEnm SavePartInStream(LqFileSz ReadLen = LQ_MAX_CONTENT_LEN) { return LqHttpRcvMultipartInStream(Deep.Conn, ReadLen); }
                } Multipart;

            };
        } Quer;

        class _Rsp
        {
        public:

            inline operator bool() const { return LqHttpActGetClassByConn(Hdrs.Conn) == LQHTTPACT_CLASS_RSP; }

            union
            {
                class _Hdr
                {
                    friend _Rsp;
                    LqHttpConn* Conn;
                    class _HdrsInterator
                    {
                        friend _Hdr;
                        LqHttpConn* Conn;
                        const char* Name;
                        _HdrsInterator(LqHttpConn* NewConn, const char* NewName): Conn(NewConn), Name(NewName) {}
                    public: _HdrsInterator& operator =(const _HdrsInterator&) = delete; DEF_CMP_STRING
                        inline operator LqString() const { char* hvs = "", *hve = hvs; LqHttpRspHdrSearch(Conn, Name, nullptr, &hvs, &hve); return LqString(hvs, hve - hvs); }
                            inline const char* operator=(const char* Val) const { LqHttpRspHdrChange(Conn, Name, Val); return Val; }
                            inline LqString& operator=(const LqString& Val) const { LqHttpRspHdrChangeEx(Conn, Name, LqStrLen(Name), Val.c_str(), Val.length()); return (LqString&)Val; }
                            inline char* Printf(const char* Format, ...)
                            {
                                va_list Va;
                                va_start(Va, Format);
                                Remove();
                                return LqHttpRspHdrAddPrintfVa(Conn, Name, Format, Va);
                            }
                            inline bool Remove() { return LqHttpRspHdrRemove(Conn, Name); }
                    };
                public: DISABLE_COPY_CONSTRUCT(_Hdr)
                    inline operator HdrsType() const
                {
                    HdrsType Res;
                    char* HeaderNameResult = nullptr, *HeaderNameResultEnd = nullptr, *HeaderValResult = nullptr, *HeaderValEnd = nullptr;
                    while(LqHttpRspHdrEnum(Conn, &HeaderNameResult, &HeaderNameResultEnd, &HeaderValResult, &HeaderValEnd) >= 0)
                        Res.push_back(std::pair<LqString, LqString>(LqString(HeaderNameResult, HeaderNameResultEnd - HeaderNameResult), LqString(HeaderValResult, HeaderValEnd - HeaderValResult)));
                    return Res;
                }
                        inline operator LqString()
                        {
                            return LqString(Conn->Buf + Conn->Response.HeadersStart, Conn->Response.HeadersEnd - Conn->Response.HeadersStart);
                        }
                        inline _HdrsInterator operator[](const char* Index) const { return _HdrsInterator(Conn, Index); }
                        inline _HdrsInterator operator[](const LqString& Index) const { return _HdrsInterator(Conn, Index.c_str()); }
                        inline intptr_t AppendSmallContent(const void* Data, intptr_t DataLen) { return LqHttpRspHdrAddSmallContent(Conn, Data, DataLen); }

                } Hdrs;

				struct _Stat
				{
						LqHttpConn* Conn;
				public:DISABLE_COPY_CONSTRUCT(_Stat)
						inline operator int() const { return Conn->Response.Status; }
				} Stat;

                struct _Stream
                {
                    class _Size
                    {
                        friend _Stream;
                        LqHttpConn* Conn;
                    public: DISABLE_COPY_CONSTRUCT(_Size)
                        inline operator intptr_t() const { return Conn->Response.Stream.Len; }
                    } Size;
                    DISABLE_COPY_CONSTRUCT(_Stream)
                    inline operator bool() const { return Size.Conn->ActionState == LQHTTPACT_STATE_RSP_STREAM; }
                    inline LqString& operator<<(LqString& Str) { LqHttpRspContentWrite(Size.Conn, Str.c_str(), Str.length()); return Str; }
                    inline const char* operator<<(const char* Str) { LqHttpRspContentWrite(Size.Conn, Str, LqStrLen(Str)); return Str; }
                    inline intptr_t Write(void* Buf, size_t Len) { return LqHttpRspContentWrite(Size.Conn, Buf, Len); }
                    inline intptr_t Printf(const char* FormatStr, ...) { va_list Va; va_start(Va, FormatStr); return LqHttpRspContentWritePrintfVa(Size.Conn, FormatStr, Va); }
                    operator LqString() const;
                } Stream;

                struct _File
                {
                    DISABLE_COPY_CONSTRUCT(_File)
                        union
                    {
                        class _Descriptor
                        {
                            friend _File;
                            LqHttpConn* Conn;
                        public: DISABLE_COPY_CONSTRUCT(_Descriptor)
                            inline operator int() const
                        {
                            if(Conn->ActionState != LQHTTPACT_STATE_RSP_FD)
                                return -1;
                            return Conn->Response.Fd;
                        }
                        } Descriptor;
                    };

                    inline operator bool() const { return (Descriptor.Conn->ActionState == LQHTTPACT_STATE_RSP_FD) && (Descriptor.Conn->ActionState == LQHTTPACT_STATE_RSP_CACHE); }

                    LqHttpActResult Set(int Fd, LqFileSz OffsetStart = 0, LqFileSz OffsetEnd = LQ_MAX_CONTENT_LEN) { return LqHttpRspFileByFd(Descriptor.Conn, Fd, OffsetStart, OffsetEnd); }
                    LqHttpActResult Set(const char* Path, LqFileSz OffsetStart = 0, LqFileSz OffsetEnd = LQ_MAX_CONTENT_LEN) { return LqHttpRspFile(Descriptor.Conn, Path, OffsetStart, OffsetEnd); }
                    int SetAuto(const char* Path = nullptr) { return LqHttpRspFileAuto(Descriptor.Conn, Path); }
                } File;

            };
            inline int MakeError(int Status) { return LqHttpRspError(Hdrs.Conn, Status); }
            inline int MakeStatus(int Status) { return LqHttpRspStatus(Hdrs.Conn, Status); }
            inline char* MakeStartLine(int Status) { return LqHttpActSwitchToRspAndSetStartLine(Hdrs.Conn, Status); }
            inline void KeepOnlyHeaders() { return LqHttpActKeepOnlyHeaders(Hdrs.Conn); }
        } Rsp;

        class _RemoteIp
        {
            LqHttpConn* Conn;
        public: DISABLE_COPY_CONSTRUCT(_RemoteIp) DEF_CMP_STRING
            inline operator LqString() const
            {
                char Buf[256];
                Buf[0] = '\0';
                LqHttpConnGetRemoteIpStr(Conn, Buf, 255);
                return Buf;
            }
                /*@return: For IPv4 - 4, for IPv6 - 6, on error - 0*/
            inline int GetType() const
            {
                char Buf[256];
                return LqHttpConnGetRemoteIpStr(Conn, Buf, 255);
            }
        } RemoteIp;

        class _RemotePort
        {
            LqHttpConn* Conn;
        public: DISABLE_COPY_CONSTRUCT(_RemotePort) DEF_CMP_STRING
            inline operator int() const { return LqHttpConnGetRemotePort(Conn); }
            inline operator LqString() const { return LqToString(operator int()); }
        } RemotePort;

        class _Action
        {
            friend LqHttpConnInterface;
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_Action)
            inline operator LqHttpActEnm() const { return (LqHttpActEnm)Conn->ActionState; }
        } Action;

        class _ActionResult
        {
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_ActionResult)
            inline operator LqHttpActResultEnm() const { return (LqHttpActResultEnm)Conn->ActionResult; }
        } ActionResult;

        class _EvntHandler
        {
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_EvntHandler)
            inline operator LqHttpEvntHandlerFn() const { return Conn->EventAct; }
            inline LqHttpEvntHandlerFn operator=(LqHttpEvntHandlerFn NewFn) { return Conn->EventAct = NewFn; }
            inline void Ignore() { LqHttpEvntActSetIgnore(Conn); }
        } EvntHandler;

        class _CloseHandler
        {
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_CloseHandler)
            inline operator LqHttpEvntHandlerFn() const { return Conn->EventClose; }
            inline LqHttpEvntHandlerFn operator=(LqHttpEvntHandlerFn NewFn) { return Conn->EventClose = NewFn; }
            inline void Ignore() { LqHttpEvntCloseSetIgnore(Conn); }
        } CloseHandler;

        class _EvntFlag
        {
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_EvntFlag)
            inline operator LqEvntFlag() { return Conn->Flags & (LQEVNT_FLAG_HUP | LQEVNT_FLAG_RDHUP | LQEVNT_FLAG_WR | LQEVNT_FLAG_RD); }
            inline LqEvntFlag operator =(LqEvntFlag NewFlag) { LqEvntSetFlags(Conn, NewFlag, 0); return NewFlag; }
            inline int SetCloseAsync() { return LqEvntSetClose(Conn); }
            inline int SetCloseSync(LqTimeMillisec WaitTime) { return LqEvntSetClose2(Conn, WaitTime); }
            inline int SetCloseSyncForce() { return LqEvntSetClose3(Conn); }
            inline bool IsClose() { return LqConnIsClose(Conn); }
            /*Set default flags for current action*/
            inline int ReturnToDefault() { return LqHttpEvntSetFlagByAct(Conn); }
        } EvntFlag;

		class __UserData
		{
			LqHttpConn* Conn;

		public:DISABLE_COPY_CONSTRUCT(__UserData)
			class ___Interator
			{
				LqHttpConn* Conn;
				void* UniquePointer;
			public:
				inline ___Interator(LqHttpConn* c, void* v): Conn(c), UniquePointer(v) {}
				template<typename TypeResult>
				inline operator TypeResult() const
				{ 
					void* Res = nullptr;
					LqHttpConnDataGet(Conn, UniquePointer, &Res);
					return (TypeResult)Res;
				}
				template<typename TypeResult>
				inline TypeResult operator =(TypeResult v)
				{
					void* NewVal = (void*)v;
					LqHttpConnDataStore(Conn, UniquePointer, NewVal);
					return v;
				}
				inline bool Delete() const
				{
					return LqHttpConnDataUnstore(Conn, UniquePointer) != -1;
				}
			};
			___Interator operator[] (void* UniquePointer) { return ___Interator(Conn, UniquePointer); }

		} UserData;

        class _Row
        {
            LqHttpConn* Conn;
        public:DISABLE_COPY_CONSTRUCT(_Row)
            inline operator LqHttpConn*() const { return Conn; }
               inline LqHttpConn* operator =(LqHttpConn* NewConn) { return Conn = NewConn; }
        } Row;

    };

    inline LqHttpConnInterface(LqHttpConn* Conn)
    {
        Quer.Domen.Conn = Conn;
    }

    inline LqHttpConnInterface(const LqHttpConnInterface& Conn)
    {
        Action.Conn = Conn.Action.Conn;
    }
};

#pragma pack(pop)
