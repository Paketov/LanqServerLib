
#include "LqHttpMdl.h"
#include "LqHttpConn.h"
#include "LqHttpRsp.h"
#include "LqHttpRcv.h"
#include "LqHttpPth.h"
#include "LqHttpAtz.h"
#include "LqHttpAct.h"
#include "LqConn.h"

#include <string>

LqHttpMdl Mod;



LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttpProtoBase* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
{

    LqHttpPthDmnCreate(Reg, "my.com");

    auto c = LqHttpAtzCreate(LQHTTPATZ_TYPE_BASIC, "main");
    LqHttpAtzAdd(c,
                 LQHTTPATZ_PERM_CHECK | LQHTTPATZ_PERM_CREATE | LQHTTPATZ_PERM_CREATE_SUBDIR | LQHTTPATZ_PERM_DELETE | LQHTTPATZ_PERM_WRITE | LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_MODIFY,
                 "Admin", "Password");
    LqHttpMdlInit(Reg, &Mod, "Example2", ModuleHandle);

    Mod.FreeNotifyProc =
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

    Mod.FreeNotifyProc =
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
            if(c->ActionState == LQHTTPACT_STATE_RESPONSE_HANDLE_PROCESS)
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
                        *r = LQ_PATH_SEPARATOR;
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

                if(HdrVal == nullptr)
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
            LqEvntSetFlags(c, 0);

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
            LqEvntSetFlags(c, LqHttpEvntGetFlagByAct(c));
        },
        LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );

    LqHttpAtzRelease(c);

    return LQHTTPMDL_REG_OK;
}




