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

#include <string>
#include <sstream>



#if defined(LQPLATFORM_WINDOWS)
#include <Windows.h>
#include "LqHashTable.hpp"

static int __GetRealPrior(LqThreadPriorEnm p)
{
    switch(p)
    {
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

static LqThreadPriorEnm __GetPrior(int Code)
{
    switch(Code)
    {
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

static int pthread_setname_np(DWORD dwThreadID, const char* threadName)
{
    static const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO
    {
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
    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
#pragma warning(pop)
    NameThread = LqStrDuplicate(threadName);
    return 0;
}

static int pthread_getname_np(DWORD dwThreadID, char* StrBufDest, size_t Len)
{
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

static int __GetRealPrior(LqThreadPriorEnm p)
{
    switch(p)
    {
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

static LqThreadPriorEnm __GetPrior(int Code)
{
    switch(Code)
    {
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
    IsOut(true), 
    UserData(nullptr), 
    ExitHandler([](void*) {}),
    EnterHandler([](void*) {})
{
    if(NewName != nullptr)
        Name = LqStrDuplicate(NewName);
    else
        Name = nullptr;
}

LqThreadBase::~LqThreadBase()
{
    if(Name != nullptr)
        free(Name);
    EndWorkSync();
    if(joinable())
        this->thread::detach();
}

LqThreadPriorEnm LqThreadBase::GetPriority() const
{
    return Priority;
}

void LqThreadBase::SetPriority(LqThreadPriorEnm New)
{
    ThreadParamLocker.LockWrite();
    if(Priority != New)
    {
        Priority = New;
        if(!IsOut)
        {
			try
			{
#if defined(LQPLATFORM_WINDOWS)
            SetThreadPriority(native_handle(), __GetRealPrior(Priority));
#else
            sched_param schedparams;
            schedparams.sched_priority = __GetRealPrior(Priority);
            pthread_setschedparam(native_handle(), SCHED_OTHER, &schedparams);
#endif
			} catch(...)
			{
			}
        }
    }
    ThreadParamLocker.UnlockWrite();
}

ullong LqThreadBase::GetAffinity() const
{
    return AffinMask;
}

void LqThreadBase::SetAffinity(ullong Mask)
{
    ThreadParamLocker.LockWrite();
    if(AffinMask != Mask)
    {
        AffinMask = Mask;
        if(!IsOut)
        {
#if defined(LQPLATFORM_WINDOWS)
            SetThreadAffinityMask(native_handle(), Mask);
#elif !defined(LQPLATFORM_ANDROID)
            pthread_setaffinity_np(native_handle(), sizeof(Mask), (const cpu_set_t*)&Mask);
#endif
        }

    }
    ThreadParamLocker.UnlockWrite();
}

void LqThreadBase::WaitEnd()
{
    if(!this->IsOut)
    {
		bool IsCallHandler = false;
        try
        {
            this->join();
        } catch(...)
		{
			/*Is thread killed by system (Actual for Windows)*/
			IsOut = true;
			IsCallHandler = true;
		}
		if(IsCallHandler)
			ExitHandler(UserData);
    }
}

void LqThreadBase::BeginThreadHelper(LqThreadBase* This)
{
    while(!This->joinable())
        LqThreadYield();

#if defined(LQPLATFORM_WINDOWS)
	pthread_setname_np(This->ThreadId(), This->Name);
#elif defined(LQPLATFORM_LINUX) || defined(LQPLATFORM_ANDROID)
    pthread_setname_np(pthread_self(), This->Name);
#elif defined(LQPLATFORM_FREEBSD)
    pthread_setname_np(pthread_self(), This->Name, nullptr);
#elif defined(LQPLATFORM_APPLE)
    pthread_setname_np(This->Name);
#endif

    if(This->Priority == LQTHREAD_PRIOR_NONE)
    {
#if defined(LQPLATFORM_WINDOWS)
        This->Priority = __GetPrior(GetThreadPriority(GetCurrentThread()));
#else
        sched_param schedparams;
        pthread_getschedparam(pthread_self(), LqDfltPtr(), &schedparams);
        This->Priority = __GetPrior(schedparams.sched_priority);
#endif
    } else
    {
#if defined(LQPLATFORM_WINDOWS)
        SetThreadPriority(GetCurrentThread(), __GetRealPrior(This->Priority));
#else
        sched_param schedparams;
        schedparams.sched_priority = __GetRealPrior(This->Priority);
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &schedparams);
#endif
    }


    if(This->AffinMask == 0)
    {
#if defined(LQPLATFORM_WINDOWS)
        GROUP_AFFINITY ga = {0};
        GetThreadGroupAffinity(GetCurrentThread(), &ga);
        This->AffinMask = ga.Mask;
#elif !defined(LQPLATFORM_ANDROID)
        pthread_getaffinity_np(pthread_self(), sizeof(This->AffinMask), (cpu_set_t*)&This->AffinMask);
#endif
    } else
    {
#if defined(LQPLATFORM_WINDOWS)
        SetThreadAffinityMask(GetCurrentThread(), This->AffinMask);
#elif !defined(LQPLATFORM_ANDROID)
        pthread_setaffinity_np(pthread_self(), sizeof(This->AffinMask), (const cpu_set_t*)&This->AffinMask);
#endif
    }
    This->ThreadParamLocker.UnlockWrite();

    This->EnterHandler(This->UserData); //Call user defined handler

    This->IsShouldEnd = false;

    //Enter main func
    This->BeginThread();


#ifdef LQPLATFORM_WINDOWS
    if(NameThread != nullptr)
    {
        free(NameThread);
        NameThread = nullptr;
    }
#endif

    This->IsOut = true;

    This->ExitHandler(This->UserData);
}

int LqThreadBase::GetName(intptr_t Id, char* DestBuf, size_t Len)
{
#if !defined(LQPLATFORM_ANDROID)
    return pthread_getname_np(Id, DestBuf, Len);
#else
    return -1;
#endif
}

bool LqThreadBase::StartAsync()
{
    StartThreadLocker.LockWrite();
    if(this->IsOut)
    {
        this->IsShouldEnd = true;
        this->IsOut = false;
        if(joinable())
            this->thread::detach();
        this->thread::operator=(LqSystemThread(LqThreadBase::BeginThreadHelper, this));
        StartThreadLocker.UnlockWrite();
        return true;
    }
    StartThreadLocker.UnlockWrite();
    return false;
}

bool LqThreadBase::StartSync()
{
    StartThreadLocker.LockWrite();
    if(this->IsOut)
    {
        this->IsShouldEnd = true;
        this->IsOut = false;
        if(joinable())
            this->thread::detach();
        this->thread::operator=(LqSystemThread(LqThreadBase::BeginThreadHelper, this));
        //Wait until thread come to safe region
        while(this->IsShouldEnd)
            std::this_thread::yield();
        StartThreadLocker.UnlockWrite();
        return true;
    }
    StartThreadLocker.UnlockWrite();
    return false;
}

bool LqThreadBase::IsThreadEnd() const
{
    return this->IsOut;
}

bool LqThreadBase::IsJoinable() const
{
    return this->joinable();
}

bool LqThreadBase::IsThisThread()
{
    return std::this_thread::get_id() == this->get_id();
}

bool LqThreadBase::EndWorkAsync()
{
    bool r = false;
    StartThreadLocker.LockWrite();
    if(!LqThreadBase::IsOut)
    {
        r = LqThreadBase::IsShouldEnd = true;
        if(!IsThisThread())
            NotifyThread();
    }
    StartThreadLocker.UnlockWrite();
    return r;
}

bool LqThreadBase::EndWorkSync()
{
    bool r = false;
    bool IsCallHandler = false;
    StartThreadLocker.LockWrite();
    if(!LqThreadBase::IsOut)
    {
        r = LqThreadBase::IsShouldEnd = true;
        if(!IsThisThread())
        {
            NotifyThread();
            while(!IsOut)
            {
                try
                {
                    this->join();
                } catch(...)
                {
                    IsOut = true;
                    IsCallHandler = true;
                }
            }
        }
    }
    StartThreadLocker.UnlockWrite();
    if(IsCallHandler)
        ExitHandler(UserData);
    return r;
}

intptr_t LqThreadBase::ThreadId() const
{
	get_id();
    std::basic_stringstream<char> r;
    r << get_id();
    intptr_t id = 0;
    r >> id;
    return id;
}

LqString LqThreadBase::DebugInfo() const
{
    const char* Prior;
    switch(Priority)
    {
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
    sprintf
    (
        Buf,
        "Thread id: %llu\n"
        "Is work: %c\n"
        "Priority: %s\n"
        "Affinity mask: 0x%016llx\n",
        (ullong)ThreadId(),
        (char)((!IsThreadEnd()) ? '1' : '0'),
        Prior,
        AffinMask
    );
    return Buf;
}
