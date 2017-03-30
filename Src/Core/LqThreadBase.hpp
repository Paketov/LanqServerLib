#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqThreadBase - Thread implementation.
*/


#include "LqLock.hpp"
#include "LqDef.hpp"

#include <thread>


enum LqThreadPriorEnm
{
    LQTHREAD_PRIOR_IDLE,
    LQTHREAD_PRIOR_LOWER,
    LQTHREAD_PRIOR_LOW,
    LQTHREAD_PRIOR_NORMAL,
    LQTHREAD_PRIOR_HIGH,
    LQTHREAD_PRIOR_HIGHER,
    LQTHREAD_PRIOR_REALTIME,
    LQTHREAD_PRIOR_NONE
};

#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_FAST)

class LQ_IMPORTEXPORT LqThreadBase {
protected:
    LqThreadPriorEnm                                    Priority;
    ullong                                              AffinMask;
    mutable LqLocker<uintptr_t>                         StartThreadLocker;
    char*                                               Name;
    void*                                               UserData;
    intptr_t                                            CurThreadId;
    uintptr_t                                           ThreadHandler;
    std::atomic<bool>                                   IsStarted;
    std::atomic<bool>                                   IsShouldEnd;

#ifdef LQPLATFORM_WINDOWS
    static unsigned __stdcall BeginThreadHelper(void* ProcData);
#else
    static void* BeginThreadHelper(void * Data);
#endif

    void(*EnterHandler)(void *UserData);
    void(*ExitHandler)(void *UserData);
    virtual void BeginThread() = 0;
    virtual void NotifyThread() = 0;

    uintptr_t NativeHandle();
public:

    LqThreadBase(const char* NewName);
    ~LqThreadBase();

    intptr_t ThreadId() const;

    bool SetPriority(LqThreadPriorEnm New);
    LqThreadPriorEnm GetPriority() const;

    ullong GetAffinity() const;
    bool SetAffinity(ullong Mask);

    void WaitEnd();

    bool StartThreadAsync();
    bool StartThreadSync();

    bool EndWorkAsync();
    bool EndWorkSync();

    bool IsThreadRunning() const;

    bool IsThisThread();

    static int GetName(intptr_t Id, char* DestBuf, size_t Len);

    LqString DebugInfo() const;
};

#pragma pack(pop)