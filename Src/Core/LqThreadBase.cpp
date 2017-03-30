/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqThreadBase - Thread implementation.
*/


#include "LqOs.h"
#include "LqThreadBase.hpp"
#include "LqDfltRef.hpp"
#include "LqStr.h"
#include "LqSbuf.h"
#include "LqErr.h"

#include <string>
#include <sstream>



#if defined(LQPLATFORM_WINDOWS)
#include <Windows.h>
#include <process.h>

static int __GetRealPrior(LqThreadPriorEnm p) {
    switch(p) {
        case LQTHREAD_PRIOR_IDLE:           return THREAD_PRIORITY_IDLE;
        case LQTHREAD_PRIOR_LOWER:          return THREAD_PRIORITY_LOWEST;
        case LQTHREAD_PRIOR_LOW:            return THREAD_PRIORITY_BELOW_NORMAL;
        case LQTHREAD_PRIOR_NORMAL:         return THREAD_PRIORITY_NORMAL;
        case LQTHREAD_PRIOR_HIGH:           return THREAD_PRIORITY_ABOVE_NORMAL;
        case LQTHREAD_PRIOR_HIGHER:         return THREAD_PRIORITY_HIGHEST;
        case LQTHREAD_PRIOR_REALTIME:       return THREAD_PRIORITY_TIME_CRITICAL;
        default:                            return THREAD_PRIORITY_NORMAL;
    }
}

static LqThreadPriorEnm __GetPrior(int Code) {
    switch(Code) {
        case THREAD_PRIORITY_IDLE:          return LQTHREAD_PRIOR_IDLE;
        case THREAD_PRIORITY_LOWEST:        return LQTHREAD_PRIOR_LOWER;
        case THREAD_PRIORITY_BELOW_NORMAL:  return LQTHREAD_PRIOR_LOW;
        case THREAD_PRIORITY_NORMAL:        return LQTHREAD_PRIOR_NORMAL;
        case THREAD_PRIORITY_ABOVE_NORMAL:  return LQTHREAD_PRIOR_HIGH;
        case THREAD_PRIORITY_HIGHEST:       return LQTHREAD_PRIOR_HIGHER;
        case THREAD_PRIORITY_TIME_CRITICAL: return LQTHREAD_PRIOR_REALTIME;
        default:                            return LQTHREAD_PRIOR_NONE;
    }
}

thread_local char* NameThread = nullptr;

static int pthread_setname_np(DWORD dwThreadID, const char* threadName) {
    static const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO {
        DWORD dwType; // Must be 0x1000.
        LPCSTR szName; // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags; // _Reserved for future use, must be zero.
    } THREADNAME_INFO;
#pragma pack(pop)

    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = dwThreadID;
    info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable: 6320 6322)
    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
#pragma warning(pop)
    NameThread = LqStrDuplicate(threadName);
    return 0;
}

static int pthread_getname_np(DWORD dwThreadID, char* StrBufDest, size_t Len) {
    if(NameThread != nullptr)
        LqStrCopyMax(StrBufDest, NameThread, Len);
    else
        return -1;
    return 0;
}


#else
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <string.h>

static int __GetRealPrior(LqThreadPriorEnm p) {
    switch(p) {
        case LQTHREAD_PRIOR_IDLE: return 45;
        case LQTHREAD_PRIOR_LOWER: return 51;
        case LQTHREAD_PRIOR_LOW: return 57;
        case LQTHREAD_PRIOR_NORMAL: return 63;
        case LQTHREAD_PRIOR_HIGH: return 69;
        case LQTHREAD_PRIOR_HIGHER: return 75;
        case LQTHREAD_PRIOR_REALTIME: return 81;
        default: return 63;
    }
}

static LqThreadPriorEnm __GetPrior(int Code) {
    switch(Code) {
        case 45: return LQTHREAD_PRIOR_IDLE;
        case 51: return LQTHREAD_PRIOR_LOWER;
        case 57: return LQTHREAD_PRIOR_LOW;
        case 63: return LQTHREAD_PRIOR_NORMAL;
        case 69: return LQTHREAD_PRIOR_HIGH;
        case 75: return LQTHREAD_PRIOR_HIGHER;
        case 81: return LQTHREAD_PRIOR_REALTIME;
        default: return LQTHREAD_PRIOR_NONE;
    }
}

#if defined(LQPLATFORM_ANDROID)
thread_local char* NameThread = nullptr;
#endif

#endif

LqThreadBase::LqThreadBase(const char* NewName):
    IsShouldEnd(true),
    Priority(LQTHREAD_PRIOR_NONE),
    AffinMask(0),
    UserData(nullptr),
    ThreadHandler(0),
    CurThreadId(-((intptr_t)1)),
    ExitHandler([](void*) {}),
    EnterHandler([](void*) {})
{
    IsStarted = false;
    if(NewName != nullptr)
        Name = LqStrDuplicate(NewName);
    else
        Name = nullptr;
}

LqThreadBase::~LqThreadBase() {
    EndWorkSync();
    if(Name != nullptr)
        free(Name);
}

LqThreadPriorEnm LqThreadBase::GetPriority() const {
    return Priority;
}

bool LqThreadBase::SetPriority(LqThreadPriorEnm New) {
    bool Res = true;
    StartThreadLocker.LockWrite();
    if(Priority != New) {
        Priority = New;
        if(IsThreadRunning()) {
#if defined(LQPLATFORM_WINDOWS)
            SetThreadPriority((HANDLE)NativeHandle(), __GetRealPrior(Priority));
#else
            sched_param schedparams;
            schedparams.sched_priority = __GetRealPrior(Priority);
            pthread_setschedparam(NativeHandle(), SCHED_OTHER, &schedparams);
#endif
        } else {
            lq_errno_set(ENOENT);
            Res = false;
        }
    }
    StartThreadLocker.UnlockWrite();
    return Res;
}

ullong LqThreadBase::GetAffinity() const {
    return AffinMask;
}

uintptr_t LqThreadBase::NativeHandle() {
#if defined(LQPLATFORM_WINDOWS)
    return ThreadHandler;
#else
    return CurThreadId;
#endif
}

bool LqThreadBase::SetAffinity(ullong Mask) {
    bool Res = true;
    StartThreadLocker.LockWrite();
    if(AffinMask != Mask) {
        AffinMask = Mask;
        if(IsThreadRunning()) {
#if defined(LQPLATFORM_WINDOWS)
            SetThreadAffinityMask((HANDLE)NativeHandle(), Mask);
#elif !defined(LQPLATFORM_ANDROID)
            pthread_setaffinity_np(NativeHandle(), sizeof(Mask), (const cpu_set_t*)&Mask);
#endif
        } else {
            lq_errno_set(ENOENT);
            Res = false;
        }
    }
    StartThreadLocker.UnlockWrite();
    return Res;
}

void LqThreadBase::WaitEnd() {
    bool IsNotOut = true;
    if(IsThisThread())
        return;
    while(IsNotOut) {
        StartThreadLocker.LockWriteYield();
        IsNotOut = IsThreadRunning();
        StartThreadLocker.UnlockWrite();
    }
}
#ifdef LQPLATFORM_WINDOWS
unsigned __stdcall LqThreadBase::BeginThreadHelper(void* ProcData) 
#else
void* LqThreadBase::BeginThreadHelper(void* ProcData)
#endif
{
    LqThreadBase* This = (LqThreadBase*)ProcData;

    This->IsStarted = true;
    This->StartThreadLocker.LockWrite();
    
    //
#if defined(LQPLATFORM_WINDOWS)
    pthread_setname_np(This->ThreadId(), This->Name);
#elif defined(LQPLATFORM_LINUX) || defined(LQPLATFORM_ANDROID)
    pthread_setname_np(pthread_self(), This->Name);
#elif defined(LQPLATFORM_FREEBSD)
    pthread_setname_np(pthread_self(), This->Name, nullptr);
#elif defined(LQPLATFORM_APPLE)
    pthread_setname_np(This->Name);
#endif

    if(This->Priority == LQTHREAD_PRIOR_NONE) {
#if defined(LQPLATFORM_WINDOWS)
        This->Priority = __GetPrior(GetThreadPriority(GetCurrentThread()));
#else
        sched_param schedparams;
        pthread_getschedparam(pthread_self(), LqDfltPtr(), &schedparams);
        This->Priority = __GetPrior(schedparams.sched_priority);
#endif
    } else {
#if defined(LQPLATFORM_WINDOWS)
        SetThreadPriority(GetCurrentThread(), __GetRealPrior(This->Priority));
#else
        sched_param schedparams;
        schedparams.sched_priority = __GetRealPrior(This->Priority);
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &schedparams);
#endif
    }


    if(This->AffinMask == 0) {
#if defined(LQPLATFORM_WINDOWS)
        GROUP_AFFINITY ga = {0};
        GetThreadGroupAffinity(GetCurrentThread(), &ga);
        This->AffinMask = ga.Mask;
#elif !defined(LQPLATFORM_ANDROID)
        pthread_getaffinity_np(pthread_self(), sizeof(This->AffinMask), (cpu_set_t*)&This->AffinMask);
#endif
    } else {
#if defined(LQPLATFORM_WINDOWS)
        SetThreadAffinityMask(GetCurrentThread(), This->AffinMask);
#elif !defined(LQPLATFORM_ANDROID)
        pthread_setaffinity_np(pthread_self(), sizeof(This->AffinMask), (const cpu_set_t*)&This->AffinMask);
#endif
    }
    This->StartThreadLocker.UnlockWrite();

    This->EnterHandler(This->UserData); //Call user defined handler

    //Enter main func
    This->BeginThread();


#ifdef LQPLATFORM_WINDOWS
    if(NameThread != nullptr) {
        free(NameThread);
        NameThread = nullptr;
    }
#endif

    This->ExitHandler(This->UserData);

    This->StartThreadLocker.LockWrite();
#ifdef LQPLATFORM_WINDOWS
    CloseHandle((HANDLE)This->ThreadHandler);
#endif
    This->CurThreadId = -((intptr_t)1);
    This->ThreadHandler = 0;
    This->StartThreadLocker.UnlockWrite();
#ifdef LQPLATFORM_WINDOWS
    return 0;
#else
    return NULL;
#endif
}

int LqThreadBase::GetName(intptr_t Id, char* DestBuf, size_t Len) {
#if !defined(LQPLATFORM_ANDROID)
    return pthread_getname_np(Id, DestBuf, Len);
#else
    return -1;
#endif
}

bool LqThreadBase::StartThreadAsync() {
    bool Res = true;
    StartThreadLocker.LockWrite();
    if(IsThreadRunning()) {
        StartThreadLocker.UnlockWrite();
        lq_errno_set(EALREADY);
        return false;
    }
    IsShouldEnd = false;
    IsStarted = false;
#ifdef LQPLATFORM_WINDOWS
    unsigned threadID;
    uintptr_t Handler = _beginthreadex(NULL, 0, BeginThreadHelper, this, 0, &threadID);
    CurThreadId = threadID;
    ThreadHandler = Handler;
    if(Handler == -1L) {
#else
    pthread_t threadID = 0;
    int Err = pthread_create(&threadID, NULL, BeginThreadHelper, this);
    CurThreadId = threadID;
    if(Err != 0) {
#endif
        Res = false;
        CurThreadId = -((intptr_t)1);
        ThreadHandler = 0;
    }
    StartThreadLocker.UnlockWrite();
    return Res;
}

bool LqThreadBase::StartThreadSync() {
    bool Res = true;
    StartThreadLocker.LockWrite();
    if(IsThreadRunning()) {
        StartThreadLocker.UnlockWrite();
        lq_errno_set(EALREADY);
        return false;
    }
    IsShouldEnd = false;
    IsStarted = false;
#ifdef LQPLATFORM_WINDOWS
    unsigned threadID;
    uintptr_t Handler = _beginthreadex(NULL, 0, BeginThreadHelper, this, 0, &threadID);
    CurThreadId = threadID;
    ThreadHandler = Handler;
    if(Handler == -1L) {
#else
    pthread_t threadID = 0;
    int Err = pthread_create(&threadID, NULL, BeginThreadHelper, this);
    CurThreadId = threadID;
    if(Err != 0){
#endif
        Res = false;
        CurThreadId = -((intptr_t)1);
        ThreadHandler = 0;
    } else {
        while(!IsStarted)
            LqThreadYield();
    }
    StartThreadLocker.UnlockWrite();
    return Res;
}

bool LqThreadBase::IsThreadRunning() const {
    return CurThreadId != -((intptr_t)1);
}

bool LqThreadBase::IsThisThread() {
#ifdef LQPLATFORM_WINDOWS
    return GetCurrentThreadId() == CurThreadId;
#else
    return pthread_equal(pthread_self(), CurThreadId);
#endif
}

bool LqThreadBase::EndWorkAsync() {
    bool r = true;
    StartThreadLocker.LockWrite();
    if(IsThreadRunning()) {
        IsShouldEnd = true;
        if(!IsThisThread())
            NotifyThread();
    } else {
        lq_errno_set(ENOENT);
        r = false;
    }
    StartThreadLocker.UnlockWrite();
    return r;
}

bool LqThreadBase::EndWorkSync() {
    bool Res;
    if(Res = EndWorkAsync()) {
        WaitEnd();
    }
    return Res;
}

intptr_t LqThreadBase::ThreadId() const {
    return CurThreadId;
}

LqString LqThreadBase::DebugInfo() const {
    const char* Prior;
    switch(Priority) {
        case LQTHREAD_PRIOR_IDLE: Prior = "idle"; break;
        case LQTHREAD_PRIOR_LOWER: Prior = "lower"; break;
        case LQTHREAD_PRIOR_LOW:Prior = "low"; break;
        case LQTHREAD_PRIOR_NONE:
        case LQTHREAD_PRIOR_NORMAL: Prior = "normal"; break;
        case LQTHREAD_PRIOR_HIGH:  Prior = "high"; break;
        case LQTHREAD_PRIOR_HIGHER: Prior = "higher"; break;
        case LQTHREAD_PRIOR_REALTIME: Prior = "realtime"; break;
        default: Prior = "unknown";
    }
    char Buf[1024];
    LqFbuf_snprintf (
        Buf,
        sizeof(Buf),
        "--------------\n"
        "Thread id: %llu\n"
        "Is work: %c\n"
        "Priority: %s\n"
        "Affinity mask: 0x%016llx\n",
        (ullong)ThreadId(),
        (char)((IsThreadRunning()) ? '1' : '0'),
        Prior,
        AffinMask
    );
    return Buf;
}
