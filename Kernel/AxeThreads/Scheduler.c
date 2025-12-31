
#include <AxeSchd.h>
#include <IDT.h>
#include <Sync.h>
#include <Timer.h>
// #define __SchdDBG

CpuScheduler CpuSchedulers[MaxCPUs];

static inline void
ThreadFxSave(void* __State__)
{
    __asm__ volatile("fxsave %0" : "=m"(*(char (*)[512])__State__));
}

static inline void
ThreadFxRestore(const void* __State__)
{
    __asm__ volatile("fxrstor %0" ::"m"(*(const char (*)[512])__State__));
}

void
AddThreadToReadyQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    __atomic_store_n(&__ThreadPtr__->State, ThreadStateReady, __ATOMIC_SEQ_CST);
    __atomic_store_n(&__ThreadPtr__->LastCpu, __CpuId__, __ATOMIC_SEQ_CST);
    __ThreadPtr__->Next = NULL;
    __ThreadPtr__->Prev = NULL;

    AcquireSpinLock(&Scheduler->SchedulerLock, __Err__);

    if (!Scheduler->ReadyQueue)
    {
        Scheduler->ReadyQueue = __ThreadPtr__;
    }
    else
    {
        Thread* Tail = Scheduler->ReadyQueue;
        while (Tail->Next)
        {
            Tail = Tail->Next;
        }
        Tail->Next          = __ThreadPtr__;
        __ThreadPtr__->Prev = Tail;
    }

    /* increment while still holding the lock */
    Scheduler->ReadyCount++;

    ReleaseSpinLock(&Scheduler->SchedulerLock, __Err__);
}

Thread*
RemoveThreadFromReadyQueue(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return Error_TO_Pointer(-BadArgs);
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&Scheduler->SchedulerLock, Error);

    Thread* ThreadPtr = Scheduler->ReadyQueue;
    if (!ThreadPtr)
    {
        ReleaseSpinLock(&Scheduler->SchedulerLock, Error);
        return Error_TO_Pointer(-Dangling);
    }

    Scheduler->ReadyQueue = ThreadPtr->Next;
    if (ThreadPtr->Next)
    {
        ThreadPtr->Next->Prev = NULL;
    }

    ThreadPtr->Next = NULL;
    ThreadPtr->Prev = NULL;

    /* decrement while still holding the lock */
    if (Scheduler->ReadyCount > 0)
    {
        Scheduler->ReadyCount--;
    }

    ReleaseSpinLock(&Scheduler->SchedulerLock, Error);
    return ThreadPtr;
}

void
AddThreadToWaitingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    SysErr  err;
    SysErr* Error = &err;

    __atomic_store_n(&__ThreadPtr__->State, ThreadStateBlocked, __ATOMIC_SEQ_CST);
    AcquireSpinLock(&Scheduler->SchedulerLock, Error);

    __ThreadPtr__->Next     = Scheduler->WaitingQueue;
    Scheduler->WaitingQueue = __ThreadPtr__;

    ReleaseSpinLock(&Scheduler->SchedulerLock, Error);
}

void
AddThreadToZombieQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    SysErr  err;
    SysErr* Error = &err;

    __atomic_store_n(&__ThreadPtr__->State, ThreadStateZombie, __ATOMIC_SEQ_CST);
    AcquireSpinLock(&Scheduler->SchedulerLock, __Err__);

    __ThreadPtr__->Next    = Scheduler->ZombieQueue;
    Scheduler->ZombieQueue = __ThreadPtr__;

    ReleaseSpinLock(&Scheduler->SchedulerLock, __Err__);
    __atomic_fetch_sub(&Scheduler->ThreadCount, 1, __ATOMIC_SEQ_CST);
}

void
AddThreadToSleepingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    SysErr  err;
    SysErr* Error = &err;

    __atomic_store_n(&__ThreadPtr__->State, ThreadStateSleeping, __ATOMIC_SEQ_CST);
    AcquireSpinLock(&Scheduler->SchedulerLock, Error);

    __ThreadPtr__->Next      = Scheduler->SleepingQueue;
    Scheduler->SleepingQueue = __ThreadPtr__;

    ReleaseSpinLock(&Scheduler->SchedulerLock, Error);
}

void
MigrateThreadToCpu(Thread* __ThreadPtr__, uint32_t __TargetCpuId__, SysErr* __Err__)
{
    if (!__ThreadPtr__ || __TargetCpuId__ >= MaxCPUs)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    /* Only migrate if thread is ready to run */
    if (__ThreadPtr__->State == ThreadStateReady)
    {
        __ThreadPtr__->LastCpu = __TargetCpuId__;
        AddThreadToReadyQueue(__TargetCpuId__, __ThreadPtr__, __Err__);
    }
}

uint32_t
GetCpuThreadCount(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return Nothing;
    }
    return __atomic_load_n(&CpuSchedulers[__CpuId__].ThreadCount, __ATOMIC_SEQ_CST);
}

uint32_t
GetCpuReadyCount(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return Nothing;
    }
    return __atomic_load_n(&CpuSchedulers[__CpuId__].ReadyCount, __ATOMIC_SEQ_CST);
}

uint64_t
GetCpuContextSwitches(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return Nothing;
    }
    return __atomic_load_n(&CpuSchedulers[__CpuId__].ContextSwitches, __ATOMIC_SEQ_CST);
}

uint32_t
GetCpuLoadAverage(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return Nothing;
    }
    return __atomic_load_n(&CpuSchedulers[__CpuId__].LoadAverage, __ATOMIC_SEQ_CST);
}

void
WakeupSleepingThreads(uint32_t __CpuId__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler    = &CpuSchedulers[__CpuId__];
    uint64_t      CurrentTicks = GetSystemTicks();

    AcquireSpinLock(&Scheduler->SchedulerLock, __Err__);

    Thread* Current = Scheduler->SleepingQueue;
    Thread* Prev    = NULL;

    while (Current)
    {
        Thread* Next = Current->Next;

        if (__atomic_load_n(&Current->WakeupTime, __ATOMIC_SEQ_CST) <= CurrentTicks)
        {
            /* unlink from sleeping */
            if (Prev)
            {
                Prev->Next = Next;
            }
            else
            {
                Scheduler->SleepingQueue = Next;
            }

            __atomic_store_n(&Current->WaitReason, WaitReasonNone, __ATOMIC_SEQ_CST);
            __atomic_store_n(&Current->WakeupTime, 0, __ATOMIC_SEQ_CST);
            Current->State = ThreadStateReady;
            Current->Prev  = NULL;
            Current->Next  = NULL;

            /* splice into ready tail under lock */
            if (!Scheduler->ReadyQueue)
            {
                Scheduler->ReadyQueue = Current;
            }
            else
            {
                Thread* Tail = Scheduler->ReadyQueue;
                while (Tail->Next)
                {
                    Tail = Tail->Next;
                }
                Tail->Next    = Current;
                Current->Prev = Tail;
            }

            Scheduler->ReadyCount++;
        }
        else
        {
            Prev = Current;
        }

        Current = Next;
    }

    ReleaseSpinLock(&Scheduler->SchedulerLock, __Err__);
}

void
CleanupZombieThreads(uint32_t __CpuId__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    AcquireSpinLock(&Scheduler->SchedulerLock, __Err__);

    Thread* Current        = Scheduler->ZombieQueue;
    Scheduler->ZombieQueue = NULL;

    ReleaseSpinLock(&Scheduler->SchedulerLock, __Err__);

    while (Current)
    {
        Thread* Next = Current->Next;
        DestroyThread(Current, __Err__);
        Current = Next;
    }
}

void
InitializeCpuScheduler(uint32_t __CpuId__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    /* Reset all queues to empty */
    Scheduler->ReadyQueue    = NULL;
    Scheduler->WaitingQueue  = NULL;
    Scheduler->ZombieQueue   = NULL;
    Scheduler->SleepingQueue = NULL;
    Scheduler->CurrentThread = NULL;
    Scheduler->NextThread    = NULL;
    Scheduler->IdleThread    = NULL;

    /* Reset all */
    __atomic_store_n(&Scheduler->ThreadCount, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->ReadyCount, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->ContextSwitches, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->IdleTicks, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->LoadAverage, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->ScheduleTicks, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->LastSchedule, 0, __ATOMIC_SEQ_CST);

    InitializeSpinLock(&Scheduler->SchedulerLock, "CpuScheduler", __Err__);

    PDebug("CPU %u scheduler initialized\n", __CpuId__);
}

void
SaveInterruptFrameToThread(Thread* __ThreadPtr__, InterruptFrame* __Frame__, SysErr* __Err__)
{
    if (!__ThreadPtr__ || !__Frame__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    ThreadContext* Context = &__ThreadPtr__->Context;

    Context->Rax = __Frame__->Rax;
    Context->Rbx = __Frame__->Rbx;
    Context->Rcx = __Frame__->Rcx;
    Context->Rdx = __Frame__->Rdx;
    Context->Rsi = __Frame__->Rsi;
    Context->Rdi = __Frame__->Rdi;
    Context->Rbp = __Frame__->Rbp;
    Context->R8  = __Frame__->R8;
    Context->R9  = __Frame__->R9;
    Context->R10 = __Frame__->R10;
    Context->R11 = __Frame__->R11;
    Context->R12 = __Frame__->R12;
    Context->R13 = __Frame__->R13;
    Context->R14 = __Frame__->R14;
    Context->R15 = __Frame__->R15;

    Context->Rip    = __Frame__->Rip;
    Context->Rsp    = __Frame__->Rsp;
    Context->Rflags = __Frame__->Rflags;
    Context->Cs     = __Frame__->Cs;
    Context->Ss     = __Frame__->Ss;
}

void
LoadThreadContextToInterruptFrame(Thread* __ThreadPtr__, InterruptFrame* __Frame__, SysErr* __Err__)
{
    if (!__ThreadPtr__ || !__Frame__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    uint64_t __Pd__ = __ThreadPtr__->PageDirectory;
    if (__Pd__)
    {
        __asm__ volatile("mov %0, %%cr3" ::"r"(__Pd__) : "memory");
    }

    /*FPU*/
    ThreadFxRestore(__ThreadPtr__->Context.FpuState);

    ThreadContext* Context = &__ThreadPtr__->Context;

    __Frame__->Rax = Context->Rax;
    __Frame__->Rbx = Context->Rbx;
    __Frame__->Rcx = Context->Rcx;
    __Frame__->Rdx = Context->Rdx;
    __Frame__->Rsi = Context->Rsi;
    __Frame__->Rdi = Context->Rdi;
    __Frame__->Rbp = Context->Rbp;
    __Frame__->R8  = Context->R8;
    __Frame__->R9  = Context->R9;
    __Frame__->R10 = Context->R10;
    __Frame__->R11 = Context->R11;
    __Frame__->R12 = Context->R12;
    __Frame__->R13 = Context->R13;
    __Frame__->R14 = Context->R14;
    __Frame__->R15 = Context->R15;

    __Frame__->Rip    = Context->Rip;
    __Frame__->Rsp    = Context->Rsp;
    __Frame__->Rflags = Context->Rflags;
    __Frame__->Cs     = Context->Cs;
    __Frame__->Ss     = Context->Ss;
}

void
Schedule(uint32_t __CpuId__, InterruptFrame* __Frame__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs || !__Frame__)
    {
        PError("Bad Arguments to the Schedular, CPUID %u\n", __CpuId__);
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler  = &CpuSchedulers[__CpuId__];
    Thread*       Current    = Scheduler->CurrentThread;
    Thread*       NextThread = NULL;

    /*for trace*/
#ifdef __SchdDBG
    DumpCpuSchedulerInfo(__CpuId__, __Err__);
#endif

    __atomic_fetch_add(&Scheduler->ScheduleTicks, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->LastSchedule, GetSystemTicks(), __ATOMIC_SEQ_CST);

    if (Current)
    {
        /*FPU*/
        ThreadFxSave(Current->Context.FpuState);

        /*Save*/
        SaveInterruptFrameToThread(Current, __Frame__, __Err__);

        __atomic_fetch_add(&Current->CpuTime, 1, __ATOMIC_SEQ_CST);

        /* Handle current thread */
        switch (Current->State)
        {
            case ThreadStateRunning:
                /* Thread was preempted normally */
                AddThreadToReadyQueue(__CpuId__, Current, __Err__);
                break;

            case ThreadStateTerminated:
                /* Thread has finished */
                AddThreadToZombieQueue(__CpuId__, Current, __Err__);
                break;

            case ThreadStateBlocked:
                /* Thread is waiting for I/O or resource */
                AddThreadToWaitingQueue(__CpuId__, Current, __Err__);
                break;

            case ThreadStateSleeping:
                /* Thread is sleeping */
                AddThreadToSleepingQueue(__CpuId__, Current, __Err__);
                break;

            case ThreadStateReady:
                /* Thread yielded CPU voluntarily */
                AddThreadToReadyQueue(__CpuId__, Current, __Err__);
                break;

            default:
                /* Unknown state */
                Current->State = ThreadStateReady;
                AddThreadToReadyQueue(__CpuId__, Current, __Err__);
                break;
        }
    }

SelectAgain:

    /*Routine*/
    WakeupSleepingThreads(__CpuId__, __Err__);
    CleanupZombieThreads(__CpuId__, __Err__);

    NextThread = RemoveThreadFromReadyQueue(__CpuId__);

    /* CPU is idle */
    if (!NextThread)
    {
        Scheduler->CurrentThread = NULL;
        __atomic_fetch_add(&Scheduler->IdleTicks, 1, __ATOMIC_SEQ_CST);
        SlotError(__Err__, -NoSuch);
        return;
    }

    if (Probe_IF_Error(NextThread))
    {
        SlotError(__Err__, Pointer_TO_Error(NextThread));
        return;
    }

    /* Override CS and SS selectors based on ring */
    if (NextThread->Type == ThreadTypeUser)
    {
        NextThread->Context.Cs = UserCodeSelector;
        NextThread->Context.Ss = UserDataSelector;
    }
    else
    {
        NextThread->Context.Cs = KernelCodeSelector;
        NextThread->Context.Ss = KernelDataSelector;
    }

    /* Frequency stride based on thread priority */
    uint32_t Stride = 1;
    switch (NextThread->Priority)
    {
        case ThreadPrioritykernel:
            Stride = 1;
            break; /* Kernel threads run constantly */
        case ThreadPrioritySuper:
            Stride = 2;
            break;
        case ThreadPriorityUltra:
            Stride = 4;
            break;
        case ThreadPriorityHigh:
            Stride = 8;
            break;
        case ThreadPriorityNormal:
            Stride = 16;
            break;
        case ThreadPriorityLow:
            Stride = 32;
            break;
        case ThreadPriorityIdle:
            Stride = 64;
            break;

        default:
            Stride = 16;
            break; /* Default to normal priority */
    }

    if (__atomic_load_n(&NextThread->Cooldown, __ATOMIC_SEQ_CST) > 0)
    {
        __atomic_fetch_sub(&NextThread->Cooldown, 1, __ATOMIC_SEQ_CST);
        AddThreadToReadyQueue(__CpuId__, NextThread, __Err__);

        /* Select another thread to run */
        goto SelectAgain;
    }
    else
    {
        /*Reset*/
        __atomic_store_n(&NextThread->Cooldown, Stride - 1, __ATOMIC_SEQ_CST);
    }

    Scheduler->CurrentThread = NextThread;
    NextThread->State        = ThreadStateRunning;
    NextThread->LastCpu      = __CpuId__;
    __atomic_store_n(&NextThread->StartTime, GetSystemTicks(), __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&Scheduler->ContextSwitches, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&NextThread->ContextSwitches, 1, __ATOMIC_SEQ_CST);

    /*Restore*/
    LoadThreadContextToInterruptFrame(NextThread, __Frame__, __Err__);

    SetCurrentThread(__CpuId__, NextThread, __Err__);
}

void
DumpCpuSchedulerInfo(uint32_t __CpuId__, SysErr* __Err__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];

    PInfo("CPU %u Scheduler:\n", __CpuId__);
    PInfo("  Threads: %u, Ready: %u\n",
          __atomic_load_n(&Scheduler->ThreadCount, __ATOMIC_SEQ_CST),
          __atomic_load_n(&Scheduler->ReadyCount, __ATOMIC_SEQ_CST));
    PInfo("  Context Switches: %llu\n",
          __atomic_load_n(&Scheduler->ContextSwitches, __ATOMIC_SEQ_CST));
    PInfo("  Current Thread: %u\n",
          Scheduler->CurrentThread ? Scheduler->CurrentThread->ThreadId : 0);
}

void
DumpAllSchedulers(SysErr* __Err__)
{
    for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        DumpCpuSchedulerInfo(CpuIndex, __Err__);
    }
}

Thread*
GetNextThread(uint32_t __CpuId__)
{
    return RemoveThreadFromReadyQueue(__CpuId__);
}

void
InitializeScheduler(SysErr* __Err__)
{
    for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        InitializeCpuScheduler(CpuIndex, __Err__);
    }

    PSuccess("Scheduler initialized for %u CPUs\n", Smp.CpuCount);
}