#include <AxeSchd.h>
#include <IDT.h>
#include <Timer.h>

/*
 * Global Scheduler Data
 *
 * CpuSchedulers: Array of per-CPU scheduler structures, one for each CPU core.
 * Each scheduler manages thread queues, statistics, and scheduling state for
 * its respective CPU. This enables SMP (Symmetric Multi-Processing) support
 * with independent scheduling domains per CPU.
 *
 * Thread safety: Each scheduler has its own spinlock for queue protection.
 * Cross-CPU operations must be handled carefully to avoid race conditions.
 */
CpuScheduler CpuSchedulers[MaxCPUs];

/**
 * Add Thread to Ready Queue
 * 
 * @param __CpuId__ CPU identifier where the thread will be added
 * @param __ThreadPtr__ Pointer to the thread to add
 *
 * Adds a thread to the tail of the ready queue on the specified CPU.
 * Sets thread state to ready and updates last CPU assignment atomically.
 * Uses spinlock to protect the ready queue during modification.
 * Increments the ready count atomically.
 */
void
AddThreadToReadyQueue(uint32_t __CpuId__, Thread* __ThreadPtr__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    
    /* Set thread state and last CPU in an atomic operation */
    __atomic_store_n(&__ThreadPtr__->State, ThreadStateReady, __ATOMIC_SEQ_CST);
    __atomic_store_n(&__ThreadPtr__->LastCpu, __CpuId__, __ATOMIC_SEQ_CST);
    __ThreadPtr__->Next = NULL;
    __ThreadPtr__->Prev = NULL;
    
    /* Acquire scheduler spinlock before altering the ready queue */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    /* If the ready queue is empty, add thread as the first element */
    if (!Scheduler->ReadyQueue)
    {
        Scheduler->ReadyQueue = __ThreadPtr__;
    }
    else
    {
        /* Otherwise, walk the list to the tail and append the thread */
        Thread* Tail = Scheduler->ReadyQueue;
        while (Tail->Next)
            Tail = Tail->Next;
        Tail->Next = __ThreadPtr__;
        __ThreadPtr__->Prev = Tail;
    }
    
    /* Release the spinlock after modification */
    ReleaseSpinLock(&Scheduler->SchedulerLock);
    
    /* Atomically increment the count of ready threads */
    __atomic_fetch_add(&Scheduler->ReadyCount, 1, __ATOMIC_SEQ_CST);
}

/**
 * Remove Thread from Ready Queue
 * 
 * @param __CpuId__ CPU identifier from which to remove the thread
 * @return Pointer to the removed thread, or NULL if no thread was ready
 *
 * Removes a thread from the head of the ready queue on the specified CPU.
 * Uses spinlock to protect the ready queue during modification.
 * Decrements the ready count atomically.
 * Returns the removed thread pointer.
 */
Thread*
RemoveThreadFromReadyQueue(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return NULL;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    Thread* ThreadPtr = NULL;
    
    /* Acquire spinlock to modify ready queue safely */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    ThreadPtr = Scheduler->ReadyQueue;
    
    /* If queue is empty, release lock and return NULL */
    if (!ThreadPtr)
    {
        ReleaseSpinLock(&Scheduler->SchedulerLock);
        return NULL;
    }
    
    /* Remove from the head of the queue */
    Scheduler->ReadyQueue = ThreadPtr->Next;
    if (ThreadPtr->Next)
        ThreadPtr->Next->Prev = NULL;
    
    /* Clear thread linkage pointers */
    ThreadPtr->Next = NULL;
    ThreadPtr->Prev = NULL;
    
    /* Release the spinlock after modification */
    ReleaseSpinLock(&Scheduler->SchedulerLock);
    
    /* Atomically decrement the ready count */
    __atomic_fetch_sub(&Scheduler->ReadyCount, 1, __ATOMIC_SEQ_CST);
    return ThreadPtr;
}

/**
 * Add Thread to Waiting Queue
 * 
 * @param __CpuId__ CPU identifier where the thread will be added
 * @param __ThreadPtr__ Pointer to the thread to add
 *
 * Adds a thread to the waiting queue on the specified CPU.
 * Sets thread state to blocked atomically.
 * Thread is added at the head of the waiting queue (LIFO).
 * Uses spinlock for safe queue modification.
 */
void
AddThreadToWaitingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    
    /* Set thread state to blocked atomically */
    __atomic_store_n(&__ThreadPtr__->State, ThreadStateBlocked, __ATOMIC_SEQ_CST);
    
    /* Acquire spinlock before modifying the waiting queue */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    /* Add thread at head of waiting queue */
    __ThreadPtr__->Next = Scheduler->WaitingQueue;
    Scheduler->WaitingQueue = __ThreadPtr__;
    
    /* Release spinlock after modification */
    ReleaseSpinLock(&Scheduler->SchedulerLock);
}

/**
 * Add Thread to Zombie Queue
 * 
 * @param __CpuId__ CPU identifier where the thread will be added
 * @param __ThreadPtr__ Pointer to the thread to add
 *
 * Marks a thread as zombie and adds it to the zombie queue.
 * The thread count is decremented to reflect thread termination.
 * Uses spinlock for safe queue modification.
 */
void
AddThreadToZombieQueue(uint32_t __CpuId__, Thread* __ThreadPtr__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    
    /* Set thread state to zombie atomically */
    __atomic_store_n(&__ThreadPtr__->State, ThreadStateZombie, __ATOMIC_SEQ_CST);
    
    /* Acquire spinlock before modifying zombie queue */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    /* Insert thread at the head of zombie queue */
    __ThreadPtr__->Next = Scheduler->ZombieQueue;
    Scheduler->ZombieQueue = __ThreadPtr__;
    
    /* Release spinlock */
    ReleaseSpinLock(&Scheduler->SchedulerLock);
    
    /* Decrement overall thread count atomically */
    __atomic_fetch_sub(&Scheduler->ThreadCount, 1, __ATOMIC_SEQ_CST);
}

/**
 * Add Thread to Sleeping Queue
 * 
 * @param __CpuId__ CPU identifier where the thread will be added
 * @param __ThreadPtr__ Pointer to the thread to add
 *
 * Adds thread to the sleeping queue.
 * Sets thread state to sleeping atomically.
 * Thread is added at the head of sleeping queue.
 * Uses spinlock to protect the queue during modification.
 */
void
AddThreadToSleepingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__)
{
    if (__CpuId__ >= MaxCPUs || !__ThreadPtr__)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    
    /* Atomically set thread state to sleeping */
    __atomic_store_n(&__ThreadPtr__->State, ThreadStateSleeping, __ATOMIC_SEQ_CST);
    
    /* Lock scheduler queue for safe insertion */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    /* Insert at head of sleeping queue */
    __ThreadPtr__->Next = Scheduler->SleepingQueue;
    Scheduler->SleepingQueue = __ThreadPtr__;
    
    /* Unlock after modification */
    ReleaseSpinLock(&Scheduler->SchedulerLock);
}

/**
 * Migrate Thread to Different CPU
 * 
 * @param __ThreadPtr__ Pointer to the thread to migrate
 * @param __TargetCpuId__ Target CPU id for migration
 *
 * Migrates a thread only if it is in ready state.
 * Updates last CPU assignment and adds it to the target CPU's ready queue.
 */
void
MigrateThreadToCpu(Thread* __ThreadPtr__, uint32_t __TargetCpuId__)
{
    if (!__ThreadPtr__ || __TargetCpuId__ >= MaxCPUs)
        return;
        
    /* Only migrate if thread is ready to run */
    if (__ThreadPtr__->State == ThreadStateReady)
    {
        __ThreadPtr__->LastCpu = __TargetCpuId__;
        AddThreadToReadyQueue(__TargetCpuId__, __ThreadPtr__);
    }
}

/**
 * Get number of threads assigned to a CPU
 * 
 * @param __CpuId__ CPU identifier
 * @return Number of threads currently assigned
 *
 * Atomically loads the thread count for the CPU.
 */
uint32_t
GetCpuThreadCount(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return 0;
    return __atomic_load_n(&CpuSchedulers[__CpuId__].ThreadCount, __ATOMIC_SEQ_CST);
}

/**
 * Get number of ready threads in CPU's ready queue
 * 
 * @param __CpuId__ CPU identifier
 * @return Number of ready threads
 *
 * Atomically loads the ready thread count.
 */
uint32_t
GetCpuReadyCount(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return 0;
    return __atomic_load_n(&CpuSchedulers[__CpuId__].ReadyCount, __ATOMIC_SEQ_CST);
}

/**
 * Get total number of context switches on CPU
 * 
 * @param __CpuId__ CPU identifier
 * @return Number of context switches
 *
 * Atomically loads context switch count.
 */
uint64_t
GetCpuContextSwitches(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return 0;
    return __atomic_load_n(&CpuSchedulers[__CpuId__].ContextSwitches, __ATOMIC_SEQ_CST);
}

/**
 * Get CPU load average metric
 * 
 * @param __CpuId__ CPU identifier
 * @return Load average value
 *
 * Atomically retrieves the current load average of the CPU.
 */
uint32_t
GetCpuLoadAverage(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return 0;
    return __atomic_load_n(&CpuSchedulers[__CpuId__].LoadAverage, __ATOMIC_SEQ_CST);
}

/**
 * Wakeup Sleeping Threads whose wakeup time has elapsed
 * 
 * @param __CpuId__ CPU identifier on which to operate
 *
 * Iterates over the sleeping queue and awakens any thread whose wakeup time
 * has arrived. Removes such threads from the sleeping queue and adds them back
 * to the ready queue for scheduling.
 * Uses spinlock for safe queue access.
 */
void
WakeupSleepingThreads(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    uint64_t CurrentTicks = GetSystemTicks();
    
    /* Lock sleeping queue for safe traversal and modification */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    Thread* Current = Scheduler->SleepingQueue;
    Thread* Prev = NULL;
    
    /* Traverse sleeping queue */
    while (Current)
    {
        Thread* Next = Current->Next;
        
        /* Check if thread's wakeup time reached or passed */
        if (__atomic_load_n(&Current->WakeupTime, __ATOMIC_SEQ_CST) <= CurrentTicks)
        {
            /* Remove thread from sleeping queue */
            if (Prev)
                Prev->Next = Next;
            else
                Scheduler->SleepingQueue = Next;
            
            /* Clear wait reason and wakeup time atomically */
            __atomic_store_n(&Current->WaitReason, WaitReasonNone, __ATOMIC_SEQ_CST);
            __atomic_store_n(&Current->WakeupTime, 0, __ATOMIC_SEQ_CST);
            
            /* Release lock temporarily to safely add to ready queue */
            ReleaseSpinLock(&Scheduler->SchedulerLock);
            
            /* Add thread back to ready queue */
            AddThreadToReadyQueue(__CpuId__, Current);
            
            /* Re-acquire lock and continue */
            AcquireSpinLock(&Scheduler->SchedulerLock);
        }
        else
        {
            /* Move to next thread */
            Prev = Current;
        }
        
        Current = Next;
    }
    
    /* Release lock after processing all sleeping threads */
    ReleaseSpinLock(&Scheduler->SchedulerLock);
}

/**
 * Cleanup Zombie Threads
 * 
 * @param __CpuId__ CPU identifier
 *
 * Removes all zombie threads from the queue for cleanup.
 * Destroys the thread objects after removing them from the queue.
 * Uses spinlock for safe removal from zombie queue.
 */
void
CleanupZombieThreads(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    
    /* Acquire spinlock before clearing zombie queue */
    AcquireSpinLock(&Scheduler->SchedulerLock);
    
    /* Extract zombie queue and clear it quickly under lock */
    Thread* Current = Scheduler->ZombieQueue;
    Scheduler->ZombieQueue = NULL;
    
    ReleaseSpinLock(&Scheduler->SchedulerLock);
    
    /* Destroy freed zombie threads outside the lock to avoid long lock holds */
    while (Current)
    {
        Thread* Next = Current->Next;
        DestroyThread(Current);
        Current = Next;
    }
}

/**
 * Initialize CPU Scheduler
 * 
 * @param __CpuId__ CPU identifier
 *
 * Setup scheduler data structures and counters for the given CPU.
 * Initializes queues to empty and resets counts.
 * Initializes the scheduler spinlock with descriptive name.
 */
void
InitializeCpuScheduler(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    
    /* Reset all thread queues to empty */
    Scheduler->ReadyQueue = NULL;
    Scheduler->WaitingQueue = NULL;
    Scheduler->ZombieQueue = NULL;
    Scheduler->SleepingQueue = NULL;
    Scheduler->CurrentThread = NULL;
    Scheduler->NextThread = NULL;
    Scheduler->IdleThread = NULL;
    
    /* Reset all counters atomically */
    __atomic_store_n(&Scheduler->ThreadCount, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->ReadyCount, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->ContextSwitches, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->IdleTicks, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->LoadAverage, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->ScheduleTicks, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->LastSchedule, 0, __ATOMIC_SEQ_CST);
    
    /* Initialize spinlock with identifier for debug */
    InitializeSpinLock(&Scheduler->SchedulerLock, "CpuScheduler");
    
    PDebug("CPU %u scheduler initialized\n", __CpuId__);
}

/**
 * Save Interrupt Frame to Thread Context
 * 
 * @param __ThreadPtr__ Thread to save context to
 * @param __Frame__ Interrupt frame providing CPU register context
 *
 * Copies CPU register state from the interrupt frame to the thread's saved context.
 * This captures the thread's execution state before a context switch.
 */
void 
SaveInterruptFrameToThread(Thread* __ThreadPtr__, InterruptFrame* __Frame__)
{
    if (!__ThreadPtr__ || !__Frame__)
        return;
        
    ThreadContext* Context = &__ThreadPtr__->Context;
    
    /* Save general-purpose registers */
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
    
    /* Save execution state registers */
    Context->Rip = __Frame__->Rip;
    Context->Rsp = __Frame__->Rsp;
    Context->Rflags = __Frame__->Rflags;
    Context->Cs = __Frame__->Cs;
    Context->Ss = __Frame__->Ss;
}

/**
 * Load Thread Context to Interrupt Frame
 * 
 * @param __ThreadPtr__ Thread to load context from
 * @param __Frame__ Interrupt frame to populate with register context
 *
 * Loads saved CPU register state from the thread context into the interrupt frame.
 * Used to restore thread state on a context switch in.
 */
void 
LoadThreadContextToInterruptFrame(Thread* __ThreadPtr__, InterruptFrame* __Frame__)
{
    if (!__ThreadPtr__ || !__Frame__)
        return;
        
    ThreadContext* Context = &__ThreadPtr__->Context;
    
    /* Load general-purpose registers into interrupt frame */
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
    
    /* Load execution state registers */
    __Frame__->Rip = Context->Rip;
    __Frame__->Rsp = Context->Rsp;
    __Frame__->Rflags = Context->Rflags;
    __Frame__->Cs = Context->Cs;
    __Frame__->Ss = Context->Ss;
}

/**
 * Schedule Thread (Per-CPU)
 * 
 * @param __CpuId__ CPU identifier to perform scheduling on
 * @param __Frame__ Interrupt frame to save/restore thread context
 * 
 * This function selects the next thread to run on the CPU.
 * Implements a preemptive priority-based scheduling with frequency cooldown.
 * Handles context saving for current thread, state transitions, waking sleepers,
 * cleaning zombies, and loading the next thread context.
 * Updates CPU and thread statistics including scheduling ticks and context switches.
 */
void
Schedule(uint32_t __CpuId__, InterruptFrame* __Frame__)
{
    if (__CpuId__ >= MaxCPUs || !__Frame__)
        return;
        
    CpuScheduler* Scheduler = &CpuSchedulers[__CpuId__];
    Thread* Current = Scheduler->CurrentThread;
    Thread* NextThread = NULL;
    
    /* Update scheduler tick counters */
    __atomic_fetch_add(&Scheduler->ScheduleTicks, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&Scheduler->LastSchedule, GetSystemTicks(), __ATOMIC_SEQ_CST);
    
    /* If there is a currently running thread */
    if (Current)
    {
        /* Save current thread's CPU context */
        SaveInterruptFrameToThread(Current, __Frame__);
        __atomic_fetch_add(&Current->CpuTime, 1, __ATOMIC_SEQ_CST);
        
        /* Handle current thread's state transitions */
        switch (Current->State)
        {
            case ThreadStateRunning:
                /* Thread was preempted normally, add it back to ready queue */
                AddThreadToReadyQueue(__CpuId__, Current);
                break;
                
            case ThreadStateTerminated:
                /* Thread has finished, move it to zombie queue */
                RemoveThreadFromReadyQueue(__CpuId__);
                AddThreadToZombieQueue(__CpuId__, Current);
                break;
                
            case ThreadStateBlocked:
                /* Thread is waiting for I/O or resource, move to waiting queue */
                RemoveThreadFromReadyQueue(__CpuId__);
                AddThreadToWaitingQueue(__CpuId__, Current);
                break;
                
            case ThreadStateSleeping:
                /* Thread is sleeping, add it to sleeping queue */
                RemoveThreadFromReadyQueue(__CpuId__);
                AddThreadToSleepingQueue(__CpuId__, Current);
                break;
                
            case ThreadStateReady:
                /* Thread yielded CPU voluntarily, add back to ready queue */
                AddThreadToReadyQueue(__CpuId__, Current);
                break;
                
            default:
                /* Unknown state, safeguard by marking ready and enqueue */
                Current->State = ThreadStateReady;
                AddThreadToReadyQueue(__CpuId__, Current);
                break;
        }
    }

SelectAgain:
    /* Attempt to wake up any sleeping threads whose timeout expired */
    WakeupSleepingThreads(__CpuId__);
    
    /* Cleanup any zombie threads */
    CleanupZombieThreads(__CpuId__);
    
    /* Select next thread from ready queue */
    NextThread = RemoveThreadFromReadyQueue(__CpuId__);
    
    /* If no ready thread exists, CPU is idle */
    if (!NextThread)
    {
        Scheduler->CurrentThread = NULL;
        __atomic_fetch_add(&Scheduler->IdleTicks, 1, __ATOMIC_SEQ_CST);
        return;
    }

    /* Override code segment and stack segment selectors based on thread type */
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
    
    /* Determine frequency stride based on thread priority */
    uint32_t Stride = 1;
    switch (NextThread->Priority)
    {
        /**
         * NOTE: Priority MAY vary based on CPU load.
         * Threads on least loaded CPUs may not be restricted by priority.
         */
        case ThreadPrioritykernel: Stride = 1;  break; /* Kernel threads run constantly */
        case ThreadPrioritySuper:  Stride = 2;  break;
        case ThreadPriorityUltra:  Stride = 4;  break;
        case ThreadPriorityHigh:   Stride = 8;  break;
        case ThreadPriorityNormal: Stride = 16; break;
        case ThreadPriorityLow:    Stride = 32; break;
        case ThreadPriorityIdle:   Stride = 64; break;

        default: Stride = 16; break; /* Default to normal priority */
    }
    
    /* Frequency scheduling cooldown to prevent over-scheduling lower priority threads */
    if (__atomic_load_n(&NextThread->Cooldown, __ATOMIC_SEQ_CST) > 0)
    {
        /* Thread is cooling down, decrement cooldown counter */
        __atomic_fetch_sub(&NextThread->Cooldown, 1, __ATOMIC_SEQ_CST);
        
        /* Reinsert thread at end of ready queue */
        AddThreadToReadyQueue(__CpuId__, NextThread);
        
        /* Select another thread to run */
        goto SelectAgain;
    }
    else
    {
        /* Thread is ready, reset cooldown count */
        __atomic_store_n(&NextThread->Cooldown, Stride - 1, __ATOMIC_SEQ_CST);
    }
    
    /* Set the selected thread as current running and update state */
    Scheduler->CurrentThread = NextThread;
    NextThread->State = ThreadStateRunning;
    NextThread->LastCpu = __CpuId__;
    __atomic_store_n(&NextThread->StartTime, GetSystemTicks(), __ATOMIC_SEQ_CST);
    
    /* Update context switch statistics */
    __atomic_fetch_add(&Scheduler->ContextSwitches, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&NextThread->ContextSwitches, 1, __ATOMIC_SEQ_CST);
    
    /* Load the next thread's saved context into the interrupt frame */
    LoadThreadContextToInterruptFrame(NextThread, __Frame__);
    
    /* Update current thread reference for the CPU */
    SetCurrentThread(__CpuId__, NextThread);
}

/**
 * Dump CPU Scheduler Info
 * 
 * @param __CpuId__ CPU identifier
 *
 * Logs detailed scheduler state including thread counts, ready queues,
 * context switch counts and current running thread ID for a CPU.
 */
void
DumpCpuSchedulerInfo(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
        return;
        
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

/**
 * Dump All Schedulers
 * 
 * Prints scheduler info of all CPUs present in the system.
 */
void
DumpAllSchedulers(void)
{
    PInfo("=== Scheduler Status ===\n");
    
    for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        DumpCpuSchedulerInfo(CpuIndex);
    }
}

/**
 * Get Next Thread to Run
 * 
 * @param __CpuId__ CPU identifier
 * @return Pointer to next thread removed from ready queue
 *
 * Removes and returns the next ready thread from the CPU's ready queue.
 */
Thread*
GetNextThread(uint32_t __CpuId__)
{
    return RemoveThreadFromReadyQueue(__CpuId__);
}

/**
 * Global Scheduler Initialization
 * 
 * Initializes the scheduler on all CPUs detected by SMP.
 * Sets up per-CPU data structures and queues.
 * Prints success message on completion.
 */
void 
InitializeScheduler(void)
{
    for (uint32_t CpuIndex = 0; CpuIndex < Smp.CpuCount; CpuIndex++)
    {
        InitializeCpuScheduler(CpuIndex);
    }
    
    PSuccess("Scheduler initialized for %u CPUs\n", Smp.CpuCount);
}