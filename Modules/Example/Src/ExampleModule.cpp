
#include "LqHttpMdl.h"
#include "LqHttp.h"
#include "LqHttpPth.h"
#include "LqHttpAtz.h"
#include "LqConn.h"

#include <string>

LqHttpMdl Mod;



LQ_EXTERN_C LQ_EXPORT LqHttpMdlRegistratorEnm LQ_CALL LqHttpMdlRegistrator(LqHttp* Reg, uintptr_t ModuleHandle, const char* LibPath, void* UserData)
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
            if(Command[0] == '?'){
                LqFbuf_printf((LqFbuf*)Data, "Hello to console shell from module !");
            }
        };

    Mod.FreeNotifyProc =
        [](LqHttpMdl* This) -> uintptr_t
        {
            return This->Handle;
        };


    LqHttpPthRegisterDirRedirection
    (
        Reg,
        &Mod,
        "my.com",
        "/",
        "http://192.168.1.93/",
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
        "E:\\serv\\www\\index.html",
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
        [](LqHttpConn* c) {
            if(LqStrSame(LqHttpConnGetRcvHdrs(c)->Method, "POST")) {
                if(LqHttpConnRcvGetBoundary(c, NULL, NULL) > 0) {
                    LqHttpConnRcvMultipartFileNext(
                        c, 
                        NULL,
                        [](LqHttpConnRcvResult* Res) -> bool {
                            if(Res->IsFoundedSeq) {
                                LqHttpConnRspError(Res->HttpConn, 200);
                                return true; /* Must commit*/
                            }
                            LqHttpConnRspError(Res->HttpConn, 500);
                            return false; /* Must cancel */
                        },
                        NULL,
                        NULL,
                        -((LqFileSz)1),
                        0666,
                        true,
                        true
                        );
                } else {
                    LqHttpConnRcvFile(c, NULL, NULL, NULL, -((LqFileSz)1), 0666, true, true);
                }
            } else {
                LqHttpConnRspFileAuto(c, NULL, NULL);
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
        "E:\\serv\\www",
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
            LqHttpConnRspPrintf(c, "helloooooo weoooooooorllllllddd");
            LqHttpConnRspPrintf(c, "&br<b>%i<b>", 56);
        },
        LQHTTPATZ_PERM_READ | LQHTTPATZ_PERM_CHECK,
        nullptr,
        0
    );

    LqHttpAtzRelease(c);

    return LQHTTPMDL_REG_OK;
}




