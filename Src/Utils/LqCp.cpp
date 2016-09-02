/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqCp... - Code page conversations.
*/


#include "LqOs.h"
#include "LqCp.h"
#include <locale.h>
#include <stdlib.h>

#include <fcntl.h>
#include <stdio.h>


static int CurrentCP = LQCP_ACP;


#if defined(_MSC_VER)
# include <Windows.h>

#define CP_UTF16 ((unsigned)-1)


LQ_EXTERN_C int LQ_CALL LqCpSet(int NewCodePage)
{
    unsigned OrigCp;
    switch(NewCodePage)
    {
        case LQCP_UTF_8:    OrigCp = CP_UTF8; break;
        case LQCP_UTF_7:    OrigCp = CP_UTF7; break;
        case LQCP_UTF_16:   OrigCp = CP_UTF16; break;
        case LQCP_ACP:      OrigCp = CP_ACP; break;
        case LQCP_OEMCP:    OrigCp = CP_OEMCP; break;
        case LQCP_MACCP:    OrigCp = CP_MACCP; break;
        default: return -1;
    }
    SetConsoleCP(OrigCp);
    SetConsoleOutputCP(OrigCp);
    CurrentCP = NewCodePage;
    return CurrentCP;
}

LQ_EXTERN_C int LQ_CALL LqCpGet()
{
    return CurrentCP;
}

LQ_EXTERN_C int LQ_CALL LqCpConvertToWcs(const char* Source, wchar_t* Dest, size_t DestCount)
{
    unsigned OrigCp;
    switch(CurrentCP)
    {
        case LQCP_UTF_8: OrigCp = CP_UTF8; break;
        case LQCP_UTF_7: OrigCp = CP_UTF7; break;
        case LQCP_UTF_16:
        {
            if(DestCount > 0)
                DestCount -= 1;
            auto l = wcsnlen((wchar_t*)Source, DestCount);
            memcpy(Dest, Source, sizeof(wchar_t) * l);
            if(DestCount > 0)
                Dest[l] = L'\0';
            return l + 1;
        }
        case LQCP_ACP:  OrigCp = CP_ACP; break;
        case LQCP_OEMCP:  OrigCp = CP_OEMCP; break;
        case LQCP_MACCP:  OrigCp = CP_MACCP; break;
        default: return -1;
    }
    return MultiByteToWideChar(OrigCp, 0, Source, -1, Dest, DestCount);
}

LQ_EXTERN_C int LQ_CALL LqCpConvertFromWcs(const wchar_t* Source, char* Dest, size_t DestCount)
{
    unsigned OrigCp;
    switch(CurrentCP)
    {
        case LQCP_UTF_8: OrigCp = CP_UTF8; break;
        case LQCP_UTF_7: OrigCp = CP_UTF7; break;
        case LQCP_UTF_16:
        {
            if(DestCount > 0)
                DestCount -= 1;
            auto l = wcsnlen(Source, DestCount);
            memcpy(Dest, Source, sizeof(wchar_t) * l);
            if(DestCount > 0)
                ((wchar_t*)Dest)[l] = L'\0';
            return l + 1;
        }
        case LQCP_ACP:  OrigCp = CP_ACP; break;
        case LQCP_OEMCP:  OrigCp = CP_OEMCP; break;
        case LQCP_MACCP:  OrigCp = CP_MACCP; break;
        default: return -1;
    }
    return WideCharToMultiByte(OrigCp, 0, Source, -1, Dest, DestCount, nullptr, nullptr);
}

#else

LQ_EXTERN_C int LQ_CALL LqCpSet(int NewCodePage)
{
    const char* OrigCp;
    switch(NewCodePage)
    {
        case LQCP_UTF_8:    OrigCp = "UTF-8"; break;
        case LQCP_UTF_7:    OrigCp = "UTF-7"; break;
        case LQCP_UTF_16:   OrigCp = "UTF-16"; break;
        case LQCP_ACP:      OrigCp = ".ACP"; break;
        case LQCP_OEMCP:    OrigCp = ".OCP"; break;
        case LQCP_MACCP:    OrigCp = ".MAC"; break;
        default: return -1;
    }
    if(setlocale(LC_ALL, OrigCp) == nullptr)
    {
        return -1;
    }
    CurrentCP = NewCodePage;
    return NewCodePage;
}

LQ_EXTERN_C int LQ_CALL LqCpGet()
{
    return CurrentCP;
}

LQ_EXTERN_C int LQ_CALL LqCpConvertToWcs(const char* Source, wchar_t* Dest, size_t DestCount)
{
    return mbstowcs(Dest, Source, DestCount);
}

LQ_EXTERN_C int LQ_CALL LqCpConvertFromWcs(const wchar_t* Source, char* Dest, size_t DestCount)
{
    return wcstombs(Dest, Source, DestCount);
}

#endif






