
#include <AxeSchd.h>
#include <AxeThreads.h>
#include <KHeap.h>
#include <PerCPUData.h>
#include <SMP.h>
#include <Sync.h>
#include <Timer.h>
#include <VMM.h>

uint32_t        NextThreadId = 1;
Thread*         ThreadList   = NULL;
SpinLock        ThreadListLock;
Thread*         CurrentThreads[MaxCPUs];
static SpinLock CurrentThreadLock; /*Mutexes would have been fine ig*/
Thread*         IdleThread;

static void
Idler(void* __Arg__)
{
    for (;;)
    {
        __asm__("hlt");
    }
}

void
InitializeThreadManager(SysErr* __Err__)
{
    InitializeSpinLock(&ThreadListLock, "ThreadList", __Err__);
    InitializeSpinLock(&CurrentThreadLock, "CurrentThread", __Err__);
    NextThreadId = 1;
    ThreadList   = NULL;

    for (uint32_t CpuIndex = 0; CpuIndex < MaxCPUs; CpuIndex++)
    {
        CurrentThreads[CpuIndex] = NULL;
    }

    SysErr  err;
    SysErr* Error = &err;

    /*Idle thread*/
    IdleThread = CreateThread(ThreadTypeKernel, Idler, NULL, ThreadPriorityIdle);

    if (Probe_IF_Error(IdleThread))
    {
        SlotError(__Err__, -BadAlloc);
        return;
    }

    PSuccess("Thread Manager initialized\n");
}

uint32_t
AllocateThreadId(void)
{
    return __atomic_fetch_add(&NextThreadId, 1, __ATOMIC_SEQ_CST);
}

Thread*
GetCurrentThread(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return Error_TO_Pointer(-Limits);
    }

    SysErr  err;
    SysErr* Error = &err;

    AcquireSpinLock(&CurrentThreadLock, Error);
    Thread* Result = CurrentThreads[__CpuId__];
    ReleaseSpinLock(&CurrentThreadLock, Error);

    return Result;
}

void
SetCurrentThread(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        SlotError(__Err__, -Limits);
        return;
    }

    AcquireSpinLock(&CurrentThreadLock, __Err__);
    CurrentThreads[__CpuId__] = __ThreadPtr__;
    ReleaseSpinLock(&CurrentThreadLock, __Err__);
}

Thread*
CreateThread(ThreadType     __Type__,
             void*          __EntryPoint__,
             void*          __Argument__,
             ThreadPriority __Priority__)
{
    SysErr  err;
    SysErr* Error     = &err;
    Thread* NewThread = (Thread*)KMalloc(sizeof(Thread));
    if (Probe_IF_Error(NewThread) || !NewThread)
    {
        ReleaseSpinLock(&ThreadListLock, Error);
        return Error_TO_Pointer(-BadAlloc);
    }
    PDebug("TCB allocated at %p\n", NewThread);

    for (size_t Index = 0; Index < sizeof(Thread); Index++)
    {
        ((uint8_t*)NewThread)[Index] = 0;
    }

    NewThread->ThreadId = AllocateThreadId();
    PDebug("Thread ID allocated: %u\n", NewThread->ThreadId);

    NewThread->ProcessId    = 1;
    NewThread->State        = ThreadStateReady;
    NewThread->Type         = __Type__;
    NewThread->Priority     = __Priority__;
    NewThread->BasePriority = __Priority__;
    KrnPrintf(NewThread->Name, "Thread-%u", NewThread->ThreadId);
    PDebug("Thread name set to: %s\n", NewThread->Name);

    /*ring 0*/
    if (__Type__ == ThreadTypeKernel)
    {
        void* KernelStackBase = KMalloc(8192);
        if (Probe_IF_Error(KernelStackBase) || !KernelStackBase)
        {
            KFree(NewThread, Error);
            ReleaseSpinLock(&ThreadListLock, Error);
            return Error_TO_Pointer(-BadAlloc);
        }
        NewThread->KernelStack =
            (uint64_t)KernelStackBase + 8192; /** Stack grows downwards; store top */
        NewThread->UserStack = 0;             /** Kernel thread has no user stack */
        NewThread->StackSize = 8192;
        PDebug("CreateThread: Kernel stack allocated at %p (top: %p)\n",
               KernelStackBase,
               (void*)NewThread->KernelStack);
    }

    /*ring 3*/
    else
    {
        void* KernelStackBase = KMalloc(8192);
        void* UserStackBase   = KMalloc(8192);
        if (Probe_IF_Error(KernelStackBase) || !KernelStackBase || Probe_IF_Error(UserStackBase) ||
            !UserStackBase)
        {
            if (KernelStackBase)
            {
                KFree(KernelStackBase, Error);
            }
            if (UserStackBase)
            {
                KFree(UserStackBase, Error);
            }
            KFree(NewThread, Error);
            ReleaseSpinLock(&ThreadListLock, Error);
            return Error_TO_Pointer(-BadAlloc);
        }
        NewThread->KernelStack = (uint64_t)KernelStackBase + 8192;
        NewThread->UserStack   = (uint64_t)UserStackBase + 8192;
        NewThread->StackSize   = 8192;
        PDebug("Stacks allocated - Kernel: %p, User: %p\n",
               (void*)NewThread->KernelStack,
               (void*)NewThread->UserStack);
    }

    NewThread->Context.Rip    = (uint64_t)__EntryPoint__;
    NewThread->Context.Rsp    = (NewThread->KernelStack & ~0xFULL) - 16;
    NewThread->Context.Rflags = 0x202;

    /*ring0*/
    if (__Type__ == ThreadTypeKernel)
    {
        NewThread->Context.Cs = KernelCodeSelector;
        NewThread->Context.Ss = KernelDataSelector;
    }

    /*ring3*/
    else
    {
        NewThread->Context.Cs  = UserCodeSelector;
        NewThread->Context.Ss  = UserDataSelector;
        NewThread->Context.Rsp = (NewThread->UserStack & ~0xFULL) - 16;
    }

    NewThread->Context.Ds  = NewThread->Context.Ss;
    NewThread->Context.Es  = NewThread->Context.Ss;
    NewThread->Context.Fs  = NewThread->Context.Ss;
    NewThread->Context.Gs  = NewThread->Context.Ss;
    NewThread->Context.Rdi = (uint64_t)__Argument__;
    PDebug("RIP=%p, RSP=%p\n", (void*)NewThread->Context.Rip, (void*)NewThread->Context.Rsp);
    NewThread->CpuAffinity  = 0xFFFFFFFF;
    NewThread->LastCpu      = 0xFFFFFFFF;
    NewThread->TimeSlice    = 10;
    NewThread->Cooldown     = 0;
    NewThread->StartTime    = GetSystemTicks();
    NewThread->CreationTick = GetSystemTicks();
    NewThread->WaitReason   = WaitReasonNone;

    NewThread->PageDirectory = 0;
    NewThread->VirtualBase   = UserVirtualBase;
    NewThread->MemoryUsage   = (NewThread->StackSize * 2) / 1024;

    PDebug("current head: %p\n", ThreadList);
    NewThread->Next = ThreadList;
    if (ThreadList)
    {
        ThreadList->Prev = NewThread;
    }
    ThreadList = NewThread;
    PDebug("new head: %p\n", ThreadList);

    PDebug("Created thread %u (%s)\n",
           NewThread->ThreadId,
           __Type__ == ThreadTypeKernel ? "Kernel" : "User");

    return NewThread;
}

void
DestroyThread(Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    __ThreadPtr__->State = ThreadStateTerminated;

    AcquireSpinLock(&ThreadListLock, __Err__);

    if (__ThreadPtr__->Prev)
    {
        __ThreadPtr__->Prev->Next = __ThreadPtr__->Next;
    }
    else
    {
        ThreadList = __ThreadPtr__->Next;
    }

    if (__ThreadPtr__->Next)
    {
        __ThreadPtr__->Next->Prev = __ThreadPtr__->Prev;
    }

    ReleaseSpinLock(&ThreadListLock, __Err__);

    if (__ThreadPtr__->KernelStack)
    {
        KFree((void*)(__ThreadPtr__->KernelStack - __ThreadPtr__->StackSize), __Err__);
    }

    if (__ThreadPtr__->UserStack)
    {
        KFree((void*)(__ThreadPtr__->UserStack - __ThreadPtr__->StackSize), __Err__);
    }

    KFree(__ThreadPtr__, __Err__);

    PDebug("Destroyed thread %u\n", __ThreadPtr__->ThreadId);
}

void
SuspendThread(Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    AcquireSpinLock(&ThreadListLock, __Err__);

    __ThreadPtr__->Flags |= ThreadFlagSuspended;

    if (__ThreadPtr__->State == ThreadStateRunning || __ThreadPtr__->State == ThreadStateReady)
    {
        __ThreadPtr__->State      = ThreadStateBlocked;
        __ThreadPtr__->WaitReason = WaitReasonNone;
    }

    ReleaseSpinLock(&ThreadListLock, __Err__);

    PDebug("Suspended thread %u\n", __ThreadPtr__->ThreadId);
}

void
ResumeThread(Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    __ThreadPtr__->Flags &= ~ThreadFlagSuspended;

    if (__ThreadPtr__->State == ThreadStateBlocked && __ThreadPtr__->WaitReason == WaitReasonNone)
    {
        __ThreadPtr__->State = ThreadStateReady;
    }

    PDebug("Resumed thread %u\n", __ThreadPtr__->ThreadId);
}

void
SetThreadPriority(Thread* __ThreadPtr__, ThreadPriority __Priority__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    __ThreadPtr__->Priority = __Priority__;

    PDebug("Set thread %u priority to %u\n", __ThreadPtr__->ThreadId, __Priority__);
}

void
SetThreadAffinity(Thread* __ThreadPtr__, uint32_t __CpuMask__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    __ThreadPtr__->CpuAffinity = __CpuMask__;

    PDebug("Set thread %u affinity to 0x%x\n", __ThreadPtr__->ThreadId, __CpuMask__);
}

uint32_t
GetCpuLoad(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return 0xFFFFFFFF; /* Bad CPU indicator */
    }

    return GetCpuReadyCount(__CpuId__);
}

uint32_t
FindLeastLoadedCpu(void)
{
    uint32_t BestCpu = 0;
    uint32_t MinLoad = GetCpuLoad(0);

    for (uint32_t CpuIndex = 1; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        uint32_t Load = GetCpuLoad(CpuIndex);
        if (Load < MinLoad)
        {
            MinLoad = Load;
            BestCpu = CpuIndex;
        }
    }

    return BestCpu;
}

uint32_t
CalculateOptimalCpu(Thread* __ThreadPtr__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        return Nothing;
    }

    if (__ThreadPtr__->CpuAffinity != 0xFFFFFFFF)
    {
        uint32_t BestCpu       = 0;
        uint32_t MinLoad       = 0xFFFFFFFF;
        bool     FoundValidCpu = false;

        for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
        {
            if (__ThreadPtr__->CpuAffinity & (1 << CpuIndex))
            {
                uint32_t Load = GetCpuLoad(CpuIndex);
                if (Probe_IF_Error(FoundValidCpu) || !FoundValidCpu || Load < MinLoad)
                {
                    MinLoad       = Load;
                    BestCpu       = CpuIndex;
                    FoundValidCpu = true;
                }
            }
        }

        return FoundValidCpu ? BestCpu : Nothing;
    }

    return FindLeastLoadedCpu();
}

void
ThreadExecute(Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    /* Determine best CPU for this thread */
    uint32_t TargetCpu = CalculateOptimalCpu(__ThreadPtr__);

    AcquireSpinLock(&ThreadListLock, __Err__);
    __ThreadPtr__->LastCpu = TargetCpu;
    __ThreadPtr__->State   = ThreadStateReady;
    ReleaseSpinLock(&ThreadListLock, __Err__);

    /* Enqueue thread */
    AddThreadToReadyQueue(TargetCpu, __ThreadPtr__, __Err__);

    PDebug("Thread %u assigned to CPU %u (Load: %u)\n",
           __ThreadPtr__->ThreadId,
           TargetCpu,
           GetCpuLoad(TargetCpu));
}

void
ThreadExecuteMultiple(Thread** __ThreadArray__, uint32_t __ThreadCount__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadArray__) || !__ThreadArray__ || __ThreadCount__ == 0)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    for (uint32_t ThreadIndex = 0; ThreadIndex < __ThreadCount__; ThreadIndex++)
    {
        Thread* ThreadPtr = __ThreadArray__[ThreadIndex];
        if (Probe_IF_Error(ThreadPtr) || !ThreadPtr)
        {
            continue;
        }

        uint32_t TargetCpu = CalculateOptimalCpu(ThreadPtr);
        AcquireSpinLock(&ThreadListLock, __Err__);
        ThreadPtr->LastCpu = TargetCpu;
        ThreadPtr->State   = ThreadStateReady;
        ReleaseSpinLock(&ThreadListLock, __Err__);
        AddThreadToReadyQueue(TargetCpu, ThreadPtr, __Err__);

        PDebug("Thread %u \u2192 CPU %u (Load: %u)\n",
               ThreadPtr->ThreadId,
               TargetCpu,
               GetCpuLoad(TargetCpu));
    }
}

void
LoadBalanceThreads(SysErr* __Err__)
{
    uint32_t CpuLoads[MaxCPUs];
    uint32_t MaxLoad = 0;
    uint32_t MinLoad = 0xFFFFFFFF;
    uint32_t MaxCpu  = 0;
    uint32_t MinCpu  = 0;

    /* Gather load information */
    for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        CpuLoads[CpuIndex] = GetCpuLoad(CpuIndex);

        if (CpuLoads[CpuIndex] > MaxLoad)
        {
            MaxLoad = CpuLoads[CpuIndex];
            MaxCpu  = CpuIndex;
        }

        if (CpuLoads[CpuIndex] < MinLoad)
        {
            MinLoad = CpuLoads[CpuIndex];
            MinCpu  = CpuIndex;
        }
    }

    /* Only perform migration if load difference is significant */
    if (MaxLoad > MinLoad + 2)
    {
        Thread* ThreadToMigrate = GetNextThread(MaxCpu);
        if (ThreadToMigrate)
        {
            if (ThreadToMigrate->CpuAffinity == 0xFFFFFFFF ||
                (ThreadToMigrate->CpuAffinity & (1 << MinCpu)))
            {
                ThreadToMigrate->LastCpu = MinCpu;
                AddThreadToReadyQueue(MinCpu, ThreadToMigrate, __Err__);

                PDebug("Migrated Thread %u from CPU %u to CPU %u\n",
                       ThreadToMigrate->ThreadId,
                       MaxCpu,
                       MinCpu);
            }
            else
            {
                /* Put thread back into original CPU’s ready queue if migration failed */
                AddThreadToReadyQueue(MaxCpu, ThreadToMigrate, __Err__);
            }
        }
    }
}

void
GetSystemLoadStats(uint32_t*       __TotalThreads__,
                   uint32_t*       __AverageLoad__,
                   uint32_t*       __MaxLoad__,
                   uint32_t*       __MinLoad__,
                   SysErr* __Err__ _unused)
{
    uint32_t TotalLoad = 0;
    uint32_t MaxLoad   = 0;
    uint32_t MinLoad   = 0xFFFFFFFF;

    for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        uint32_t Load = GetCpuLoad(CpuIndex);
        TotalLoad += Load;

        if (Load > MaxLoad)
        {
            MaxLoad = Load;
        }
        if (Load < MinLoad)
        {
            MinLoad = Load;
        }
    }

    if (MinLoad == 0xFFFFFFFF)
    {
        MinLoad = 0;
    }

    if (__TotalThreads__)
    {
        *__TotalThreads__ = TotalLoad;
    }
    if (__AverageLoad__)
    {
        *__AverageLoad__ = (Smp.CpuCount > 0) ? TotalLoad / Smp.CpuCount : Nothing;
    }
    if (__MaxLoad__)
    {
        *__MaxLoad__ = MaxLoad;
    }
    if (__MinLoad__)
    {
        *__MinLoad__ = MinLoad;
    }
}

void
ThreadYield(SysErr* __Err__ _unused)
{
    /*Software Interrupt*/
    __asm__ volatile("int $0x20");
}

void
ThreadSleep(uint64_t __Milliseconds__, SysErr* __Err__)
{
    uint32_t CpuId   = GetCurrentCpuId();
    Thread*  Current = GetCurrentThread(CpuId);

    if (Current)
    {
        Current->State      = ThreadStateSleeping;
        Current->WaitReason = WaitReasonSleep;
        Current->WakeupTime = GetSystemTicks() + __Milliseconds__;

        __asm__ volatile("int $0x20");
    }
    else
    {
        /*busy wait*/
        uint64_t WakeupTime = GetSystemTicks() + __Milliseconds__;
        while (GetSystemTicks() < WakeupTime)
        {
            __asm__ volatile("hlt");
        }
    }
}

void
ThreadExit(uint32_t __ExitCode__, SysErr* __Err__)
{
    uint32_t CpuId   = GetCurrentCpuId();
    Thread*  Current = GetCurrentThread(CpuId);

    if (Probe_IF_Error(Current) || !Current || Probe_IF_Error(Current))
    {
        SlotError(__Err__, -NoOperations);
        return;
    }

    Current->State    = ThreadStateZombie;
    Current->ExitCode = __ExitCode__;

    PInfo("Thread %u exiting with code %u\n", Current->ThreadId, __ExitCode__);

    AddThreadToZombieQueue(CpuId, Current, __Err__);
    ThreadYield(__Err__);
}

Thread*
FindThreadById(uint32_t __ThreadId__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ThreadListLock, Error);

    Thread* Current = ThreadList;
    while (Current)
    {
        if (Current->ThreadId == __ThreadId__)
        {
            ReleaseSpinLock(&ThreadListLock, Error);
            return Current;
        }
        Current = Current->Next;
    }

    ReleaseSpinLock(&ThreadListLock, Error);
    return Error_TO_Pointer(-NoSuch);
}

uint32_t
GetThreadCount(void)
{
    uint32_t Count = 0;

    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ThreadListLock, Error);
    Thread* Current = ThreadList;
    while (Current)
    {
        Count++;
        Current = Current->Next;
    }
    ReleaseSpinLock(&ThreadListLock, Error);

    return Count;
}

void
WakeSleepingThreads(SysErr* __Err__)
{
    uint64_t CurrentTicks = GetSystemTicks();

    AcquireSpinLock(&ThreadListLock, __Err__);
    Thread* Current = ThreadList;

    while (Current)
    {
        if (Current->State == ThreadStateSleeping && Current->WakeupTime <= CurrentTicks)
        {
            Current->State      = ThreadStateReady;
            Current->WaitReason = WaitReasonNone;
            Current->WakeupTime = 0;
        }
        Current = Current->Next;
    }

    ReleaseSpinLock(&ThreadListLock, __Err__);
}

void
DumpThreadInfo(Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (Probe_IF_Error(__ThreadPtr__) || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    PInfo("Thread %u (%s):\n", __ThreadPtr__->ThreadId, __ThreadPtr__->Name);
    PInfo("  State: %u, Type: %u, Priority: %u\n",
          __ThreadPtr__->State,
          __ThreadPtr__->Type,
          __ThreadPtr__->Priority);
    PInfo("  CPU Time: %llu, Context Switches: %llu\n",
          __ThreadPtr__->CpuTime,
          __ThreadPtr__->ContextSwitches);
    PInfo("  Stack: K=0x%llx U=0x%llx Size=%u\n",
          __ThreadPtr__->KernelStack,
          __ThreadPtr__->UserStack,
          __ThreadPtr__->StackSize);
    PInfo("  Memory: %u KB, Affinity: 0x%x\n",
          __ThreadPtr__->MemoryUsage,
          __ThreadPtr__->CpuAffinity);
}

void
DumpAllThreads(SysErr* __Err__)
{
    AcquireSpinLock(&ThreadListLock, __Err__);
    Thread*  Current = ThreadList;
    uint32_t Count   = 0;

    while (Current)
    {
        PInfo("Thread %u: %s (State: %u, CPU: %u)\n",
              Current->ThreadId,
              Current->Name,
              Current->State,
              Current->LastCpu);
        Current = Current->Next;
        Count++;
    }

    ReleaseSpinLock(&ThreadListLock, __Err__);

    PInfo("Total threads: %u\n", Count);
}