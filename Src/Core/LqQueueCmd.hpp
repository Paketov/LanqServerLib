/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
* LqQueueCmd - Command queue. Class to work with the incoming commands for workers and worker boss.
* Thread safe.
*          Push()             PushBegin()
*             \                  \
*   LqQueueCmd -> [o][o][o][o][o]  >
*                /
*            Fork()                     LqQueueCmd::Interator::Pop
*              /                         /
* LqQueueCmd::Interator >[o][o][o][o][o]>
* LqQueueCmd              > [empty] >
*/

#ifndef __LQ_QUEUE_CMD_H_1_
#define __LQ_QUEUE_CMD_H_1_

#include "LqOs.h"
#include "LqLock.hpp"
#include "LqLog.h"
#include "Lanq.h"


#pragma pack(push)
#pragma pack(LQSTRUCT_ALIGN_MEM)

template<typename TypeCommand>
class LqQueueCmd {
    /*POD Structures*/
    struct ElementHeader {
        ElementHeader*          Next;
        TypeCommand             Type;
    };

    template<typename TypeVal>
    struct Element {
        ElementHeader           Header;
        char                    Value[sizeof(TypeVal)];
    };

    ElementHeader               *Begin, *End;
    mutable LqLocker<size_t>    Locker;
public:

    struct Interator {
        class {
            friend LqQueueCmd;
            friend Interator;
            ElementHeader* Cmd;
        public:
            /*
            * Get type of command.
            */
            inline operator TypeCommand() const { return Cmd->Type; }
        } Type;

        inline Interator() { Type.Cmd = NULL; }

        inline Interator DeattachLast() {
            Interator Ret;
            Ret.Type.Cmd = Type.Cmd;
            Type.Cmd = Type.Cmd->Next;
            return Ret;
        }

        /*
        * Is have last command. Use for termunate in loop.
        */
        operator bool() const;

        /*
        * Get value command.
        */
        template<typename ValType>
        ValType& Val();

        /*
        * Remove command from forked queue.
        */
        template<typename ValType>
        void Pop();

        void Pop();

        /*
        * Remove command from forked queue. Without call desructor!
        */
        void JustPop();
    };

    /*
    * Use this method in performing loop, for rapid separation queue.
    * After call we have forked not locket commands, and we can handle command and adding new command parallel.
    *  @return - forked queue.
    * Fork and work =)
    */
    Interator Fork();

    inline bool IsHaveCmds() { return Begin != nullptr; }

    LqQueueCmd();

    ~LqQueueCmd();

    bool InsertBegin(Interator& Inter);

    /*
    * Add only type command to the end of the queue.
    */
    bool Push(TypeCommand tCommand);

    /*
    * Add data command to the end of the queue.
    *  @Val - value
    */
    //template<typename ValType>
    //bool Push(TypeCommand tCommand, ValType Val);

    template<typename ValType, typename... _Args>
    bool Push(TypeCommand tCommand, _Args&&... _Ax);

    bool Push(LqClientHdr* Val, void* WorkerOwner);
    /*
    * Add only type command to the start of the queue.
    */
    bool PushBegin(TypeCommand tCommand);


    /*
    * Add data command to the start of the queue.
    *  @Val - value
    */
    template<typename ValType, typename... _Args>
    bool PushBegin(TypeCommand tCommand, _Args&&... _Ax);


    //template<typename ValType>
    //bool PushBegin(TypeCommand tCommand, ValType Val);


    Interator SeparateBegin() {
        Interator i;
        Locker.LockWriteYield();
        i.Type.Cmd = Begin;
        Begin = End = nullptr;
        return i;
    }

    void SeparatePush(Interator& Source) {
        ElementHeader* NewCommand = Source.Type.Cmd;
        Source.Type.Cmd = Source.Type.Cmd->Next;
        NewCommand->Next = nullptr;
        if(End != nullptr)
            End->Next = NewCommand;
        else
            Begin = NewCommand;
        End = NewCommand;
    }

    bool SeparateIsEnd(Interator& Source) {
        if(!Source) {
            Locker.UnlockWrite();
            return true;
        }
        return false;
    }

};

#pragma pack(pop)

#endif

#if defined(__METHOD_DECLS__) && !defined(__LQ_QUEUE_CMD_H_2_)
#define __LQ_QUEUE_CMD_H_2_

#include "LqAlloc.hpp"

template<typename TypeCommand>
inline LqQueueCmd<TypeCommand>::Interator::operator bool() const {
    return Type.Cmd != nullptr;
}

template<typename TypeCommand>
template<typename ValType>
inline ValType& LqQueueCmd<TypeCommand>::Interator::Val() {
    return *((ValType*)((Element<ValType>*)Type.Cmd)->Value);
}

template<typename TypeCommand>
template<typename ValType>
void LqQueueCmd<TypeCommand>::Interator::Pop() {
    Element<ValType>* DelCommand = (Element<ValType>*)Type.Cmd;
    Type.Cmd = Type.Cmd->Next;
    ((ValType*)(DelCommand->Value))->~ValType();
    LqFastAlloc::Delete(DelCommand);
}

template<typename TypeCommand>
void LqQueueCmd<TypeCommand>::Interator::Pop() {
    ElementHeader* DelCommand = Type.Cmd;
    Type.Cmd = Type.Cmd->Next;
    LqFastAlloc::Delete(DelCommand);
}

template<typename TypeCommand>
void LqQueueCmd<TypeCommand>::Interator::JustPop() {
    ElementHeader* DelCommand = Type.Cmd;
    Type.Cmd = Type.Cmd->Next;
    LqFastAlloc::JustDelete(DelCommand);
}

template<typename TypeCommand>
typename LqQueueCmd<TypeCommand>::Interator LqQueueCmd<TypeCommand>::Fork() {
    Interator i;
    Locker.LockWriteYield();
    i.Type.Cmd = Begin;
    Begin = End = nullptr;
    Locker.UnlockWrite();
    return i;
}

template<typename TypeCommand>
LqQueueCmd<TypeCommand>::LqQueueCmd() {
    Begin = End = nullptr;
}

template<typename TypeCommand>
LqQueueCmd<TypeCommand>::~LqQueueCmd() {
    for(auto i = Begin; i != nullptr; ) {
        auto Type = i;
        i = i->Next;
        LqFastAlloc::JustDelete(Type);
    }
}

template<typename TypeCommand>
bool LqQueueCmd<TypeCommand>::InsertBegin(Interator& Inter) {
    if(Inter.Type.Cmd == nullptr)
        return true;
    Locker.LockWriteYield();
    ElementHeader** i = &Inter.Type.Cmd;
    for(; *i; i = &(*i)->Next);
    *i = Begin;
    Begin = Inter.Type.Cmd;
    if(End == nullptr)
        End = Begin;
    Inter.Type.Cmd = nullptr;
    Locker.UnlockWrite();
    return true;
}

template<typename TypeCommand>
bool LqQueueCmd<TypeCommand>::Push(TypeCommand tCommand) {
    auto NewCommand = LqFastAlloc::New<ElementHeader>();
    if(NewCommand == nullptr) {
        LqLogErr("LqQueueCmd<TypeCommand>::Push() not alloc memory\n");
        return false;
    }
    NewCommand->Next = nullptr;
    NewCommand->Type = tCommand;

    Locker.LockWriteYield();
    if(End != nullptr)
        End->Next = NewCommand;
    else
        Begin = NewCommand;
    End = NewCommand;
    Locker.UnlockWrite();
    return true;
}

template<typename TypeCommand>
template<typename ValType, typename... _Args>
inline bool LqQueueCmd<TypeCommand>::Push(TypeCommand tCommand, _Args&&... _Ax) {
    auto NewCommand = LqFastAlloc::New<Element<ValType>>();
    if(NewCommand == nullptr) {
        LqLogErr("LqQueueCmd<TypeCommand>::Push<ValType>() not alloc memory\n");
        return false;
    }
    new(NewCommand->Value) ValType(_Ax...);
    NewCommand->Header.Next = nullptr;
    NewCommand->Header.Type = tCommand;

    Locker.LockWriteYield();
    if(End != nullptr)
        End->Next = &(NewCommand->Header);
    else
        Begin = &(NewCommand->Header);
    End = &(NewCommand->Header);
    Locker.UnlockWrite();
    return true;
}

template<typename TypeCommand>
bool LqQueueCmd<TypeCommand>::Push(LqClientHdr* Val, void* WorkerOwner) {
    auto NewCommand = LqFastAlloc::New<Element<LqClientHdr*>>();
    if(NewCommand == nullptr) {
        LqLogErr("LqQueueCmd<TypeCommand>::Push() not alloc memory\n");
        return false;
    }
    *((LqClientHdr**)NewCommand->Value) = Val;
    NewCommand->Header.Next = nullptr;
    NewCommand->Header.Type = 0;
    Locker.LockWriteYield();
    LqAtmLkWr(Val->Lk);
    Val->WrkOwner = WorkerOwner;
    LqAtmUlkWr(Val->Lk);
    if(End != nullptr)
        End->Next = &(NewCommand->Header);
    else
        Begin = &(NewCommand->Header);
    End = &(NewCommand->Header);
    Locker.UnlockWrite();
    return true;
}

template<typename TypeCommand>
template<typename ValType, typename... _Args>
bool LqQueueCmd<TypeCommand>::PushBegin(TypeCommand tCommand, _Args&&... _Ax) {
    auto NewCommand = LqFastAlloc::New<Element<ValType>>();
    if(NewCommand == nullptr) {
        LqLogErr("LqQueueCmd<TypeCommand>::PushBegin<ValType>() not alloc memory\n");
        return false;
    }
    new(NewCommand->Value) ValType(_Ax...);
    NewCommand->Header.Type = tCommand;
    Locker.LockWriteYield();
    NewCommand->Header.Next = Begin;
    Begin = &(NewCommand->Header);
    if(End == nullptr)
        End = &(NewCommand->Header);
    Locker.UnlockWrite();
    return true;
}

template<typename TypeCommand>
bool LqQueueCmd<TypeCommand>::PushBegin(TypeCommand tCommand) {
    auto NewCommand = LqFastAlloc::New<ElementHeader>();
    if(NewCommand == nullptr) {
        LqLogErr("LqQueueCmd<TypeCommand>::PushBegin() not alloc memory\n");
        return false;
    }
    NewCommand->Type = tCommand;

    Locker.LockWriteYield();
    NewCommand->Next = Begin;
    Begin = NewCommand;
    if(End == nullptr)
        End = NewCommand;
    Locker.UnlockWrite();
    return true;
}


#endif

