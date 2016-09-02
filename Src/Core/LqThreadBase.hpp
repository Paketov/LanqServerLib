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

class LQ_IMPORTEXPORT LqThreadBase:
    protected LqSystemThread
{
protected:
    LqThreadPriorEnm                                    Priority;
    ullong                                              AffinMask;
    mutable LqLocker<uchar>                             ThreadParamLocker;
    mutable LqLocker<uchar>                             StartThreadLocker;
    volatile bool                                       IsShouldEnd;
    volatile bool                                       IsOut;
    char*                                               Name;
	void*                                               UserData;
	void(*EnterHandler)(void *UserData);
	void(*ExitHandler)(void *UserData);
    static void BeginThreadHelper(LqThreadBase* This);
    virtual void BeginThread() = 0;
    virtual void NotifyThread() = 0;
public:

    LqThreadBase(const char* NewName);
    ~LqThreadBase();

    intptr_t ThreadId() const;

    void SetPriority(LqThreadPriorEnm New);
    LqThreadPriorEnm GetPriority() const;

    ullong GetAffinity() const;
    void SetAffinity(ullong Mask);

    void WaitEnd();

    bool StartAsync();
    bool StartSync();

    bool EndWorkAsync();
    bool EndWorkSync();

    bool IsThreadEnd() const;

	bool IsJoinable() const;

    bool IsThisThread();

    static int GetName(intptr_t Id, char* DestBuf, size_t Len);

    LqString DebugInfo() const;
};

#pragma pack(pop)