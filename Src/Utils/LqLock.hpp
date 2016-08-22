#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqLocker - Block shared object for read or write.
*/

#include "LqOs.h"
#include <atomic>
#include <thread>


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

/*
    Read/write locker.

*/

template<typename TypeFlag = unsigned>
class LqLocker
{
    std::atomic<TypeFlag> Locker;
public:
    inline LqLocker(): Locker(1) {}

    inline bool TryLockRead()
    {
        TypeFlag v = Locker;
        if(v != 0)
            return Locker.compare_exchange_strong(v, v + 1);
        return false;
    }
    inline void LockRead()
    {
        while(true)
        {
            TypeFlag v = Locker;
            if(v == 0)
                continue;
            if(Locker.compare_exchange_strong(v, v + 1)) break;
        }
    }
    inline void LockReadYield()
    {
        while(true)
        {
            TypeFlag v = Locker;
            if(v == 0)
            {
                std::this_thread::yield();
                continue;
            }
            if(Locker.compare_exchange_strong(v, v + 1)) break;
        }
    }
    inline void UnlockRead() { --Locker; }

    inline bool TryLockWrite()
    {
        TypeFlag v = 1;
        return Locker.compare_exchange_strong(v, 0);
    }
    inline void LockWrite() { for(TypeFlag v = 1; !Locker.compare_exchange_strong(v, 0); v = 1); }
    inline void LockWriteYield()
    {
        for(TypeFlag v = 1; !Locker.compare_exchange_strong(v, 0); v = 1)
            std::this_thread::yield();
    }
    inline void UnlockWrite() { Locker = 1; }
    inline void RelockFromWriteToRead() { Locker = 2; }

    /* Use only thread owner*/
    inline bool IsLockRead() { return Locker > 1; }
    inline bool IsLockWrite() { return Locker == 0; }
    /* Common unlock. Use for read/write lock */
    inline void Unlock()
    {
        if(IsLockRead())
            UnlockRead();
        else if(IsLockWrite())
            UnlockWrite();
    }
};



/*

    [ThreadOwner]
        EnterSafeRegion(); //Wait while some another thread working with data

    [thread1]
        OccupyRead();           //Occupy place
        WaitRegionYield();  //Wait until ThreadOwner reaches EnterSafeRegion()
        making some jobs...
        ReleaseRead();          //Release place

    [thread2]
        OccupyWrite();
        WaitRegionYield();
        making some jobs...
        ReleaseWrite();
*/
template<typename TypeFlag = unsigned>
class LqSafeRegion
{
    std::atomic<TypeFlag> SafeRegionWaiter;
    static const TypeFlag TstBit = 1 << (sizeof(TypeFlag) * 8 - 1);
public:
    inline LqSafeRegion(): SafeRegionWaiter(1) {}

    /*
    For thread owner
    */
    bool EnterSafeRegion()
    {
        /*
        SafeRegionWaiter = 0 - Wait writing thread
        SafeRegionWaiter = 1 - Not have waiting threads
        SafeRegionWaiter = 2..n - Have waiting read threads
        SafeRegionWaiter & 0x800..0 - ThreadOwner wait some operations
        */
        if(SafeRegionWaiter != 1)
        {
            SafeRegionWaiter |= TstBit;
            for(TypeFlag v = (TstBit + 1); !SafeRegionWaiter.compare_exchange_strong(v, 1); v = (TstBit + 1))
                std::this_thread::yield();
            return true;
        }
        return false;
    }

    bool EnterSafeRegionAndSwitchToWriteMode()
    {
        TypeFlag v = 1;
        if(!SafeRegionWaiter.compare_exchange_strong(v, 0))
        {
            SafeRegionWaiter |= TstBit;
            for(v = (TstBit + 1); !SafeRegionWaiter.compare_exchange_strong(v, 0); v = (TstBit + 1))
                std::this_thread::yield();
            return true;
        }
        return false;
    }

    bool EnterSafeRegionAndSwitchToReadMode()
    {
        TypeFlag v = 1;
        if(!SafeRegionWaiter.compare_exchange_strong(v, 2))
        {
            SafeRegionWaiter |= TstBit;
            for(v = (TstBit + 1); !SafeRegionWaiter.compare_exchange_strong(v, 2); v = (TstBit + 1))
                std::this_thread::yield();
            return true;
        }
        return false;
    }

    /*
    For thread clients
    */
    inline bool TryOccupyRead()
    {
        TypeFlag v = SafeRegionWaiter;
        if(v & ~TstBit)
            return SafeRegionWaiter.compare_exchange_strong(v, v + 1);
        return false;
    }
    void OccupyRead() { while(!TryOccupyRead()); }
    void OccupyReadYield() { while(!TryOccupyRead()) std::this_thread::yield(); }
    void ReleaseRead() { --SafeRegionWaiter; }

    inline bool TryOccupyWrite()
    {
        TypeFlag v = 1;
        return SafeRegionWaiter.compare_exchange_strong(v, 0);
    }
    void OccupyWrite() { while(!TryOccupyWrite()); }
    void OccupyWriteYield() { while(!TryOccupyWrite()) std::this_thread::yield(); }
    void ReleaseWrite() { ++SafeRegionWaiter; }

    bool TryWaitRegion() const { return SafeRegionWaiter & TstBit; }
    void WaitRegion() const { while(!TryWaitRegion()); }
    void WaitRegionYield() const { while(!TryWaitRegion()) std::this_thread::yield(); }
};

#pragma pack(pop)