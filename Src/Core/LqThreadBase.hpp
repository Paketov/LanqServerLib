#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqThreadBase - Thread implementation.
*/


#include "LqLock.hpp"
#include "LqDef.hpp"

enum LqThreadPriorEnm {
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
    unsigned long long                                  AffinMask;
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
    static void* BeginThreadHelper(void * ProcData);
#endif

    void(*EnterHandler)(void *UserData);
    void(*ExitHandler)(void *UserData);
    virtual void BeginThread() = 0;
    virtual void NotifyThread() = 0;

    uintptr_t NativeHandle() const;
public:

    LqThreadBase(const char* NewName);
    ~LqThreadBase();

    inline intptr_t ThreadId() const { return CurThreadId; }
    inline bool IsThreadRunning() const { return CurThreadId != -((intptr_t)1); }

    bool SetPriority(LqThreadPriorEnm New);
    LqThreadPriorEnm GetPriority() const;

    ullong GetAffinity() const;
    bool SetAffinity(ullong Mask);

    void WaitEnd() const;

    bool StartThreadAsync();
    bool StartThreadSync();

    bool ExitThreadAsync();
    bool ExitThreadSync();

    bool IsThisThread() const;

    static int GetName(intptr_t Id, char* DestBuf, size_t Len);

    LqString DebugInfo() const;
};

#pragma pack(pop)