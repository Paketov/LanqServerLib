
#include "LqHttpMdl.h"
#include "LqHttpConn.h"
#include "LqHttpRsp.h"
#include "LqHttpRcv.h"
#include "LqHttpPth.h"
#include "LqHttpAtz.h"
#include "LqHttpAct.h"

#if defined(LQPLATFORM_WINDOWS)
#include <Windows.h>
#endif

#include <string>

LqHttpMdl Mod;



LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{

	LqHttpPthDmnCreate(Reg, "my.com");

	auto c = LqHttpAtzCreate(LQHTTPATZ_TYPE_BASIC, "main");
	LqHttpAtzAdd(c, 
		LQHTTPATZ_PERM_CHECK|LQHTTPATZ_PERM_CREATE|LQHTTPATZ_PERM_CREATE_SUBDIR|LQHTTPATZ_PERM_DELETE|LQHTTPATZ_PERM_WRITE|LQHTTPATZ_PERM_READ|LQHTTPATZ_PERM_MODIFY, 
		"Admin", "Password");
	LqHttpMdlInit(Reg, &Mod, "Example2", ModuleHandle);

	Mod.FreeModuleNotifyProc = 
		[](LqHttpMdl* This) -> uintptr_t
		{
			return This->Handle;
		};

	Mod.ReciveCommandProc =
		[](LqHttpMdl* Mdl, const char* Command, void* Data)
		{
			const char * Cmd = Command;
			if(Command[0] == '?')
			{
				fprintf((FILE*)Data, "Hello to console shell from module !");
			}
		};
	
	Mod.FreeModuleNotifyProc =
		[](LqHttpMdl* This) -> uintptr_t
		{
			printf("Unload notification\n");
			return This->Handle;
		};		


	LqHttpPthRegisterDirRedirection
	(
		Reg,
		&Mod,
		"my.com",
		"/",
		"http://192.168.1.2/",
		301,
		LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
		nullptr,
		0
	);
	LqHttpPthRegisterFile
	(
		Reg,
		&Mod,
		"*",
		"/",
		"C:\\Users\\andr\\Desktop\\serv\\index.html",
		LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
		nullptr,
		1
	);

	LqHttpPthRegisterExecDir
	(
		Reg,
		&Mod,
		"*",
		"/dir2/",
		true,
		[](LqHttpConn* c)
		{
			if(c->ActionState == LQHTTPACT_STATE_HANDLE_PROCESS)
			{
				if(std::string(c->Query.Method, c->Query.MethodLen) == "POST")
				{
					if(LqHttpRcvMultipartHdrRecive(c) != LQHTTPRCV_FILE_OK)
					{
						LqHttpEvntActSetIgnore(c);
						if(LqHttpRspError(c, 500)) 
							LqHttpActSwitchToClose(c);
					}
					return;
				}
				std::string Name = "C:\\Users\\andr\\Desktop\\serv";
				Name.append(c->Query.Path, c->Query.PathLen);

				for(char* r = (char*)Name.c_str(); *r != '\0'; r++)
				{
					if(*r == '/')
						*r = LQHTTPPTH_SEPARATOR;
				}
				LqHttpRspFileAuto(c, Name.c_str());
				LqHttpEvntActSetIgnore(c);
			} else if(c->ActionState == LQHTTPACT_STATE_MULTIPART_RCV_HDRS)
			{

				if(c->ActionResult != LQHTTPACT_RES_OK)
				{
					LqHttpEvntActSetIgnore(c);
					if(LqHttpRspError(c, 500)) LqHttpActSwitchToClose(c);
					return;
				}

				char* HdrVal = nullptr, *HdrEnd = nullptr;
				LqHttpRcvHdrSearch(c, 1000, "content-disposition", nullptr, &HdrVal, &HdrEnd);
				
				if(HdrVal = nullptr)
				{
					if(LqHttpRspError(c, 200)) 
						LqHttpActSwitchToClose(c);
					return;
				}
				std::string Name = "C:\\Users\\andr\\Desktop\\serv\\dir2\\";
				LqHttpRcvMultipartInFile(c, "C:\\Users\\andr\\Desktop\\serv\\dir2\\dest_data.bin", 0666, true, true);
				LqHttpEvntActSetIgnore(c);
				return;
			} else if(c->ActionState == LQHTTPACT_STATE_MULTIPART_RCV_FILE)
			{
			}
		},
		0xff,
		c,
		0
	);

	LqHttpPthRegisterDir
	(
		Reg,
		&Mod,
		"*",
		"/",
		"C:\\Users\\andr\\Desktop\\serv",
		true,
		LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK | LQHTTPATZ_PERM_CREATE,
		c,
		0
	);

	LqHttpPthRegisterExecFile
	(
		Reg,
		&Mod,
		"*",
		"/hello",
		[](LqHttpConn* c)
		{
			LqHttpConnLock(c);

			LqHttpEvntActSetIgnore(c);

			std::string Path(c->Query.Path, c->Query.PathLen);

			LqHttpRspStatus(c, 200);

			LqHttpRspHdrAdd(c, "Content-Type", "text/html");
			LqHttpRspHdrAdd(c, "Connection", "Keep-Alive");
			LqHttpRspHdrAdd(c, "Cache-Control", "no-cache");


			LqHttpRspContentWritePrintf(c, "helloooooo weoooooooorllllllddd");
			LqHttpRspContentWritePrintf(c, "&br<b>%i<b>", 56);

			auto Sz = LqHttpRspContentGetSz(c);
			LqHttpRspHdrAddPrintf(c, "Content-Length", "%i", (int)Sz);
			LqHttpConnUnlock(c);
		},
		LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
		nullptr,
		0
	);

	LqHttpAtzRelease(c);
	
	return LQHTTPMDL_REG_OK;
}



#if defined(LQPLATFORM_WINDOWS)


BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch(ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			// A process is loading the DLL.
			break;

		case DLL_THREAD_ATTACH:
			// A process is creating a new thread.
			break;

		case DLL_THREAD_DETACH:
			// A thread exits normally.
			break;

		case DLL_PROCESS_DETACH:
			// A process unloads the DLL.
			break;
	}
	return TRUE;
}

#else

extern "C" void _init(void)
{
	printf("Module init\n");
}

extern "C" void _fini(void)
{
	printf("Module uninit\n");
}

#endif





