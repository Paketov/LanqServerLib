/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqConn... - Functions for work with connections.
*/


#include "LqConn.h"
#include "LqDef.hpp"
#include "LqFile.h"
#include "LqWrkBoss.hpp"
#include "LqTime.h"
#include "LqAtm.hpp"
#include "LqLog.h"

#include <string.h>
#include <wchar.h>

#if defined(__linux__)
# include <sys/sendfile.h>
# include <fcntl.h>
#elif defined(__FreeBSD__)
# include <sys/uio.h>
# include <fcntl.h>
#elif defined(__APPLE__)
# include <sys/uio.h>
# include <fcntl.h>
#elif defined(LQPLATFORM_WINDOWS)

# ifndef WSA_VERSION
#  define WSA_VERSION MAKEWORD(2, 2)
# endif

# include <io.h>
static struct _wsa_data {
    LPWSADATA wsa;
    _wsa_data() {
        static WSADATA wd;
        WSAStartup(WSA_VERSION, &wd);
        wsa = &wd;
    }
    ~_wsa_data() {
        WSACleanup();
    }
} wsa_data;
#endif

/*
*                   Response functions.
*/
typedef struct LqConnSslInfo {
	const void* MethodSSL; /* Example SSLv23_method()*/
	const char* CertFile; /* Example: "server.pem"*/
	const char* KeyFile; /*Example: "server.key"*/
	const char* CipherList;
	int TypeCertFile; /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
	const char* CAFile;
	const char* DhpFile;
} LqConnSslInfo;

LQ_EXTERN_C int LQ_CALL LqConnBind(
	const char* Host,
	const char* Port, 
	int RouteProto, 
	int SockType, 
	int TransportProto, 
	int MaxConnections, 
	bool IsNonBlock
) {
    static const int True = 1;
    int s;
    addrinfo *Addrs = nullptr, HostInfo = {0};
    HostInfo.ai_family = (RouteProto == -1) ? AF_INET : RouteProto;
    HostInfo.ai_socktype = (SockType == -1) ? SOCK_STREAM : SockType; // SOCK_STREAM;
    HostInfo.ai_flags = AI_PASSIVE;//AI_ALL;
    HostInfo.ai_protocol = (TransportProto == -1) ? IPPROTO_TCP : TransportProto; // IPPROTO_TCP;
    int res;
    if((res = getaddrinfo(((Host != nullptr) && (*Host != '\0')) ? Host : (const char*)nullptr, Port, &HostInfo, &Addrs)) != 0) {
        LqLogErr("LqConnBind() getaddrinfo(%s, %s, *, *) failed \"%s\" \n",
            ((Host != nullptr) && (*Host != '\0')) ? Host : "NULL",
                   Port,
                   gai_strerror(res));
        return -1;
    }

    for(auto i = Addrs; i != nullptr; i = i->ai_next) {
        if((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
            continue;
        LqDescrSetInherit(s, 0);
        if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&True, sizeof(True)) == -1) {
            LqLogErr("LqConnBind() setsockopt(%i, SOL_SOCKET, SO_REUSEADDR, &1, sizeof(1)) failed \"%s\"\n", s, strerror(lq_errno));
            continue;
        }
        if(IsNonBlock) {
            if(LqConnSwitchNonBlock(s, 1)) {
                LqLogErr("LqConnBind() LqConnSwitchNonBlock(%i, 1) failed \"%s\"\n", s, strerror(lq_errno));
                continue;
            }
        }
        if(i->ai_family == AF_INET6) {
            if(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&True, sizeof(True)) == -1) {
                LqLogErr("LqConnBind() setsockopt(%i, IPPROTO_IPV6, IPV6_V6ONLY, &1, sizeof(1)) failed \"%s\"\n", s, strerror(lq_errno));
                continue;
            }
        }
        if(bind(s, i->ai_addr, i->ai_addrlen) == -1) {
            LqLogErr("LqConnBind() bind(%i, *, %i) failed \"%s\"\n", s, (int)i->ai_addrlen, strerror(lq_errno));
            closesocket(s);
            s = -1;
            continue;
        }
        if(listen(s, MaxConnections) == -1) {
            LqLogErr("LqConnBind() listen(%s, %i) failed \"%s\"\n", s, MaxConnections, strerror(lq_errno));
            closesocket(s);
            s = -1;
            continue;
        }
        break;
    }

    if(Addrs != nullptr)
        freeaddrinfo(Addrs);
    if(s == -1) {
        LqLogErr("LqConnBind() not binded to sock\n");
        return -1;
    }
    return s;
}

LQ_EXTERN_C int LQ_CALL LqConnConnect(const char* Address, const char* Port, int RouteProto, int SockType, int TransportProto, void* IpPrtAddress, socklen_t* IpPrtAddressLen, bool IsNonBlock) {
    int s = -1;
    addrinfo hi = {0}, *ah = nullptr, *i;

    hi.ai_family = (RouteProto == -1) ? AF_UNSPEC : RouteProto;
    hi.ai_socktype = (SockType == -1) ? SOCK_STREAM : SockType; // SOCK_STREAM;
    hi.ai_protocol = (TransportProto == -1) ? IPPROTO_TCP : TransportProto; // IPPROTO_TCP;
    hi.ai_flags = 0;//AI_ALL;

    int res;
    if((res = getaddrinfo(((Address != nullptr) && (*Address != '\0')) ? Address : (const char*)nullptr, Port, &hi, &ah)) != 0) {
        LqLogErr("LqConnConnect() getaddrinfo(%s, %s, *, *) failed \"%s\" \n",
            ((Address != nullptr) && (*Address != '\0')) ? Address : "NULL",
                   Port,
                   gai_strerror(res));
        return -1;
    }

    for(i = ah; i != nullptr; i = i->ai_next) {
        if((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
            continue;
        if(IsNonBlock)
            LqConnSwitchNonBlock(s, 1);
        if(connect(s, i->ai_addr, i->ai_addrlen) != -1)
            break;
        if(IsNonBlock && LQERR_IS_WOULD_BLOCK)
            break;
        closesocket(s);
    }
    if(i == nullptr) {
        if(ah != nullptr)
            freeaddrinfo(ah);
        LqLogErr("LqConnConnect() not connected\n");
        return -1;
    }

    if((IpPrtAddress != nullptr) && (IpPrtAddressLen != nullptr)) {
        memcpy(IpPrtAddress, i->ai_addr, lq_min(i->ai_addrlen, *IpPrtAddressLen));
        *IpPrtAddressLen = i->ai_addrlen;
    }
    if(ah != nullptr)
        freeaddrinfo(ah);
    return s;
}

LQ_EXTERN_C int LQ_CALL LqConnStrToRowIp(int TypeIp, const char* SourseStr, LqConnAddr* DestAddress) {
    return (inet_pton(DestAddress->Addr.sa_family = ((TypeIp == 6) ? AF_INET6 : AF_INET), SourseStr, (TypeIp == 6) ? (void*)&DestAddress->AddrInet6.sin6_addr : (void*)&DestAddress->AddrInet.sin_addr) == 1) ? 0 : -1;
}


LQ_EXTERN_C int LQ_CALL LqConnRowIpToStr(LqConnAddr* SourceAddress, char* DestStr, size_t DestStrLen) {
    return
        (inet_ntop(SourceAddress->Addr.sa_family,
        (SourceAddress->Addr.sa_family == AF_INET6) ? (void*)&SourceAddress->AddrInet6.sin6_addr : (void*)&SourceAddress->AddrInet.sin_addr, DestStr, DestStrLen) != nullptr
         ) ? 0 : -1;
}




LQ_EXTERN_C void* LQ_CALL LqConnSslCreate
(
    const void* MethodSSL, /* Example SSLv23_method()*/
    const char* CertFile, /* Example: "server.pem"*/
    const char* KeyFile, /*Example: "server.key"*/
    const char* CipherList,
    int TypeCertFile, /*SSL_FILETYPE_ASN1 (The file is in abstract syntax notation 1 (ASN.1) format.) or SSL_FILETYPE_PEM (The file is in base64 privacy enhanced mail (PEM) format.)*/
    const char* CAFile,
    const char* DhpFile
) {
#ifdef HAVE_OPENSSL

    static const unsigned char dh1024_p[] = {
        0xB1,0x0B,0x8F,0x96,0xA0,0x80,0xE0,0x1D,0xDE,0x92,0xDE,0x5E,
        0xAE,0x5D,0x54,0xEC,0x52,0xC9,0x9F,0xBC,0xFB,0x06,0xA3,0xC6,
        0x9A,0x6A,0x9D,0xCA,0x52,0xD2,0x3B,0x61,0x60,0x73,0xE2,0x86,
        0x75,0xA2,0x3D,0x18,0x98,0x38,0xEF,0x1E,0x2E,0xE6,0x52,0xC0,
        0x13,0xEC,0xB4,0xAE,0xA9,0x06,0x11,0x23,0x24,0x97,0x5C,0x3C,
        0xD4,0x9B,0x83,0xBF,0xAC,0xCB,0xDD,0x7D,0x90,0xC4,0xBD,0x70,
        0x98,0x48,0x8E,0x9C,0x21,0x9A,0x73,0x72,0x4E,0xFF,0xD6,0xFA,
        0xE5,0x64,0x47,0x38,0xFA,0xA3,0x1A,0x4F,0xF5,0x5B,0xCC,0xC0,
        0xA1,0x51,0xAF,0x5F,0x0D,0xC8,0xB4,0xBD,0x45,0xBF,0x37,0xDF,
        0x36,0x5C,0x1A,0x65,0xE6,0x8C,0xFD,0xA7,0x6D,0x4D,0xA7,0x08,
        0xDF,0x1F,0xB2,0xBC,0x2E,0x4A,0x43,0x71,
    };

    static const unsigned char dh1024_g[] = {
        0xA4,0xD1,0xCB,0xD5,0xC3,0xFD,0x34,0x12,0x67,0x65,0xA4,0x42,
        0xEF,0xB9,0x99,0x05,0xF8,0x10,0x4D,0xD2,0x58,0xAC,0x50,0x7F,
        0xD6,0x40,0x6C,0xFF,0x14,0x26,0x6D,0x31,0x26,0x6F,0xEA,0x1E,
        0x5C,0x41,0x56,0x4B,0x77,0x7E,0x69,0x0F,0x55,0x04,0xF2,0x13,
        0x16,0x02,0x17,0xB4,0xB0,0x1B,0x88,0x6A,0x5E,0x91,0x54,0x7F,
        0x9E,0x27,0x49,0xF4,0xD7,0xFB,0xD7,0xD3,0xB9,0xA9,0x2E,0xE1,
        0x90,0x9D,0x0D,0x22,0x63,0xF8,0x0A,0x76,0xA6,0xA2,0x4C,0x08,
        0x7A,0x09,0x1F,0x53,0x1D,0xBF,0x0A,0x01,0x69,0xB6,0xA2,0x8A,
        0xD6,0x62,0xA4,0xD1,0x8E,0x73,0xAF,0xA3,0x2D,0x77,0x9D,0x59,
        0x18,0xD0,0x8B,0xC8,0x85,0x8F,0x4D,0xCE,0xF9,0x7C,0x2A,0x24,
        0x85,0x5E,0x6E,0xEB,0x22,0xB3,0xB2,0xE5,
    };

    SSL_CTX* NewCtx = NULL;

    bool r = false;
    static bool IsLoaded = false;

	if(MethodSSL == NULL)
		MethodSSL = SSLv23_server_method();
	
	do {
		if(!IsLoaded) {
			IsLoaded = true;
			SSL_library_init();
			OpenSSL_add_all_algorithms();
			SSL_load_error_strings();
		}

		if((NewCtx = SSL_CTX_new((const SSL_METHOD*)MethodSSL)) == NULL)
			break;
		SSL_CTX_set_read_ahead(NewCtx, 1);
		SSL_CTX_set_verify(NewCtx, SSL_VERIFY_NONE, NULL);

		if(CipherList != NULL) {
			if(SSL_CTX_set_cipher_list(NewCtx, CipherList) == 1)
				SSL_CTX_set_options(NewCtx, SSL_OP_CIPHER_SERVER_PREFERENCE);
		}

		if(CAFile != NULL) {
			if(!SSL_CTX_load_verify_locations(NewCtx, CAFile, NULL)) {
				SSL_CTX_free(NewCtx);
				NewCtx = NULL;
				break;
			}

		}
		if((SSL_CTX_use_certificate_file(NewCtx, CertFile, TypeCertFile) <= 0) ||
			(SSL_CTX_use_PrivateKey_file(NewCtx, KeyFile, TypeCertFile) <= 0)) {
			SSL_CTX_free(NewCtx);
			NewCtx = NULL;
			break;
		}

		if(SSL_CTX_check_private_key(NewCtx) != 1) {
			SSL_CTX_free(NewCtx);
			NewCtx = NULL;
			break;
		}

		if(DhpFile != NULL) {
			BIO *bio = BIO_new_file(DhpFile, "r");
			if(bio) {
				DH *dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
				BIO_free(bio);
				if(dh) {
					SSL_CTX_set_tmp_dh(NewCtx, dh);
					SSL_CTX_set_options(NewCtx, SSL_OP_SINGLE_DH_USE);
					DH_free(dh);
				}
			}
		} else {
			DH *dh = DH_new();
			if(dh) {
				dh->p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), NULL);
				dh->g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), NULL);
				dh->length = 160;
				if(dh->p && dh->g) {
					SSL_CTX_set_tmp_dh(NewCtx, dh);
					SSL_CTX_set_options(NewCtx, SSL_OP_SINGLE_DH_USE);
				}
				DH_free(dh);
			}
		}
	} while(false);
    return NewCtx;
#else
	lq_errno_set(ENOSYS);
    return NULL;
#endif
}
LQ_EXTERN_C void LQ_CALL LqConnSslDelete(void* Ctx) {
#ifdef HAVE_OPENSSL
	SSL_CTX_free((SSL_CTX*)Ctx);
#endif
}

LQ_EXTERN_C void LQ_CALL __LqEvntFdDfltHandler(LqEvntFd * Instance, LqEvntFlag Flags) {}

LQ_EXTERN_C void LQ_CALL __LqEvntFdDfltCloseHandler(LqEvntFd*) {}

LQ_EXTERN_C int LQ_CALL LqConnCountPendingData(LqConn* c) {
#ifdef LQPLATFORM_WINDOWS
    u_long res = -1;
    if(ioctlsocket(c->Fd, FIONREAD, &res) == -1)
        return -1;
#else
    int res;
    if(ioctl(c->Fd, FIONREAD, &res) < 0)
        return -1;
#endif
    return res;
}

LQ_EXTERN_C int LQ_CALL LqConnSwitchNonBlock(int Fd, int IsNonBlock) {
#ifdef LQPLATFORM_WINDOWS
    u_long nonBlocking = IsNonBlock;
    if(ioctlsocket(Fd, FIONBIO, &nonBlocking) == -1)
        return -1;
#else
    auto Flags = fcntl(Fd, F_GETFL, 0);
    if(fcntl(Fd, F_SETFL, (IsNonBlock) ? (Flags | O_NONBLOCK) : (Flags & ~O_NONBLOCK)) == -1)
        return -1;
#endif
    return 0;
}
