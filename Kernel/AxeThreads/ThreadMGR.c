#include <AxeSchd.h>
#include <AxeThreads.h>
#include <KHeap.h>
#include <PerCPUData.h>
#include <SMP.h>
#include <Sync.h>
#include <Timer.h>
#include <VMM.h>

/** @brief Thread Globals and static Spinlock */
uint32_t        NextThreadId = 1;
Thread*         ThreadList   = NULL;
SpinLock        ThreadListLock;
Thread*         CurrentThreads[MaxCPUs];
static SpinLock CurrentThreadLock; /*Mutexes would have been fine ig*/

/**
 * @brief Initialize Thread Manager
 *
 * This function initializes the thread management subsystem,
 * preparing necessary data structures and locks for safe
 * concurrent thread operations.
 * It sets the thread ID counter to start at 1,
 * clears the global thread list,
 * and sets the current executing threads on all CPUs to NULL.
 */
void
InitializeThreadManager(void)
{
    InitializeSpinLock(&ThreadListLock, "ThreadList");
    InitializeSpinLock(&CurrentThreadLock, "CurrentThread");
    NextThreadId = 1;
    ThreadList   = NULL;

    /*
     * Initialize current threads array to NULL for all CPUs.
     * This prevents accessing invalid thread pointers on startup.
     */
    for (uint32_t CpuIndex = 0; CpuIndex < MaxCPUs; CpuIndex++)
    {
        CurrentThreads[CpuIndex] = NULL;
    }

    PSuccess("Thread Manager initialized\n");
}

/**
 * @brief Allocate Thread ID
 *
 * Atomically increments and returns a new unique thread ID.
 * This mechanism prevents race conditions when multiple threads
 * are simultaneously created and ensures unique identification.
 *
 * @return Allocated thread ID (uint32_t).
 */
uint32_t
AllocateThreadId(void)
{
    return __atomic_fetch_add(&NextThreadId, 1, __ATOMIC_SEQ_CST);
}

/**
 * @brief Get Current Thread
 *
 * Provides safe concurrent access to retrieve the currently
 * running thread on a specified CPU. Uses spinlock to prevent
 * data races during access to the CurrentThreads array.
 *
 * @param __CpuId__ The CPU index from which to get the current thread.
 * @return Pointer to the current Thread object or NULL if no thread is running.
 */
Thread*
GetCurrentThread(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return NULL;
    }

    AcquireSpinLock(&CurrentThreadLock);
    Thread* Result = CurrentThreads[__CpuId__];
    ReleaseSpinLock(&CurrentThreadLock);

    return Result;
}

/**
 * @brief Set Current Thread
 *
 * Updates the currently executing thread pointer for the given CPU.
 * Ensures thread safety via the CurrentThreadLock spinlock to protect
 * against concurrent modifications.
 *
 * @param __CpuId__ The CPU index to update.
 * @param __ThreadPtr__ The Thread pointer to set as current.
 */
void
SetCurrentThread(uint32_t __CpuId__, Thread* __ThreadPtr__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return;
    }

    AcquireSpinLock(&CurrentThreadLock);
    CurrentThreads[__CpuId__] = __ThreadPtr__;
    ReleaseSpinLock(&CurrentThreadLock);
}

/**
 * @brief Create Thread
 *
 * Allocates and initializes a new thread control block (TCB).
 * Prepares stacks (kernel and possibly user stacks),
 * initializes the CPU context needed for scheduling,
 * sets scheduling attributes like priority and affinity,
 * and adds the thread to the global thread list.
 *
 * This function performs exhaustive error checking on allocations,
 * freeing partial resources when failures occur to prevent leaks.
 *
 * @param __Type__ ThreadType enum indicating kernel or user thread.
 * @param __EntryPoint__ Entry function pointer where thread execution starts.
 * @param __Argument__ Pointer to the argument passed to thread entry.
 * @param __Priority__ Thread priority level controlling scheduling behavior.
 * @return Pointer to allocated and initialized Thread structure or NULL if failure.
 */
Thread*
CreateThread(ThreadType     __Type__,
             void*          __EntryPoint__,
             void*          __Argument__,
             ThreadPriority __Priority__)
{
    PDebug("CreateThread: Entry - Type=%u, EntryPoint=%p, Arg=%p\n",
           __Type__,
           __EntryPoint__,
           __Argument__);

    /**
     * Allocate memory for the thread control block (TCB).
     * Use kernel heap allocator KMalloc for dynamic memory.
     * If allocation fails, release locks and return NULL to signal error.
     */
    PDebug("CreateThread: About to allocate TCB (size=%zu)\n", sizeof(Thread));
    Thread* NewThread = (Thread*)KMalloc(sizeof(Thread));
    if (!NewThread)
    {
        PError("CreateThread: Failed to allocate thread\n");
        ReleaseSpinLock(&ThreadListLock);
        return NULL;
    }
    PDebug("CreateThread: TCB allocated at %p\n", NewThread);

    /**
     * Zero out the memory block for the thread control block.
     * This prevents any uninitialized residual data in the TCB fields.
     */
    PDebug("CreateThread: Clearing TCB\n");
    for (size_t Index = 0; Index < sizeof(Thread); Index++)
    {
        ((uint8_t*)NewThread)[Index] = 0;
    }
    PDebug("CreateThread: TCB cleared\n");

    /**
     * Assign unique thread identifier atomically to avoid collisions.
     * Initialize core thread fields such as state, type, priority, and process ID.
     * The process ID is defaulted to 1 and can be updated later as needed.
     */
    PDebug("CreateThread: Allocating thread ID\n");
    NewThread->ThreadId = AllocateThreadId();
    PDebug("CreateThread: Thread ID allocated: %u\n", NewThread->ThreadId);

    NewThread->ProcessId    = 1;
    NewThread->State        = ThreadStateReady;
    NewThread->Type         = __Type__;
    NewThread->Priority     = __Priority__;
    NewThread->BasePriority = __Priority__;
    PDebug("CreateThread: Core fields initialized\n");

    /**
     * Compose a descriptive thread name using its ID.
     * This aids debugging and logging the thread's identity.
     */
    PDebug("CreateThread: Setting thread name\n");
    KrnPrintf(NewThread->Name, "Thread-%u", NewThread->ThreadId);
    PDebug("CreateThread: Thread name set to: %s\n", NewThread->Name);

    /**
     * Allocate proper stacks dependent on thread type.
     * For kernel threads, only kernel stack is allocated.
     * For user threads, kernel and user stacks are both allocated.
     * Stack sizes are fixed at 8192 bytes.
     * Handle allocation failures by cleanup and returning NULL.
     */
    PDebug("CreateThread: About to allocate stacks\n");
    if (__Type__ == ThreadTypeKernel)
    {
        PDebug("CreateThread: Allocating kernel stack (8192 bytes)\n");
        void* KernelStackBase = KMalloc(8192);
        if (!KernelStackBase)
        {
            PError("CreateThread: Failed to allocate kernel stack\n");
            KFree(NewThread);
            ReleaseSpinLock(&ThreadListLock);
            return NULL;
        }
        NewThread->KernelStack =
            (uint64_t)KernelStackBase + 8192; /** Stack grows downwards; store top */
        NewThread->UserStack = 0;             /** Kernel thread has no user stack */
        NewThread->StackSize = 8192;
        PDebug("CreateThread: Kernel stack allocated at %p (top: %p)\n",
               KernelStackBase,
               (void*)NewThread->KernelStack);
    }
    else
    {
        PDebug("CreateThread: Allocating kernel and user stacks\n");
        void* KernelStackBase = KMalloc(8192);
        void* UserStackBase   = KMalloc(8192);
        if (!KernelStackBase || !UserStackBase)
        {
            PError("CreateThread: Failed to allocate stacks\n");
            if (KernelStackBase)
            {
                KFree(KernelStackBase);
            }
            if (UserStackBase)
            {
                KFree(UserStackBase);
            }
            KFree(NewThread);
            ReleaseSpinLock(&ThreadListLock);
            return NULL;
        }
        NewThread->KernelStack = (uint64_t)KernelStackBase + 8192;
        NewThread->UserStack   = (uint64_t)UserStackBase + 8192;
        NewThread->StackSize   = 8192;
        PDebug("CreateThread: Stacks allocated - Kernel: %p, User: %p\n",
               (void*)NewThread->KernelStack,
               (void*)NewThread->UserStack);
    }

    /**
     * Initialize CPU register context for this thread.
     * The instruction pointer (RIP) initialized to provided entry point.
     * Stack pointer (RSP) to top of allocated stack minus space for saved registers.
     * Flags register initialized with interrupts enabled.
     * Segment registers set appropriately depending on thread being kernel or user.
     * Argument pointer passed via RDI as per calling convention.
     */
    PDebug("CreateThread: Initializing thread context\n");
    NewThread->Context.Rip    = (uint64_t)__EntryPoint__;
    NewThread->Context.Rsp    = NewThread->KernelStack - 16;
    NewThread->Context.Rflags = 0x202;
    if (__Type__ == ThreadTypeKernel)
    {
        NewThread->Context.Cs = KernelCodeSelector;
        NewThread->Context.Ss = KernelDataSelector;
    }
    else
    {
        NewThread->Context.Cs  = UserCodeSelector;
        NewThread->Context.Ss  = UserDataSelector;
        NewThread->Context.Rsp = NewThread->UserStack - 16;
    }

    NewThread->Context.Ds  = NewThread->Context.Ss;
    NewThread->Context.Es  = NewThread->Context.Ss;
    NewThread->Context.Fs  = NewThread->Context.Ss;
    NewThread->Context.Gs  = NewThread->Context.Ss;
    NewThread->Context.Rdi = (uint64_t)__Argument__;
    PDebug("CreateThread: Context initialized - RIP=%p, RSP=%p\n",
           (void*)NewThread->Context.Rip,
           (void*)NewThread->Context.Rsp);

    /**
     * Initialize scheduling and affinity related fields.
     * CpuAffinity set to all CPUs available by default (0xFFFFFFFF).
     * LastCpu is invalid initially; thread not yet assigned.
     * TimeSlice set to default for scheduling quantum.
     * Cooldown zero indicating no frequency throttling yet.
     * StartTime and CreationTick timestamp acquisition via system tick counter.
     * WaitReason initialized to none (thread is not blocked).
     */
    PDebug("CreateThread: Initializing scheduling fields\n");
    NewThread->CpuAffinity = 0xFFFFFFFF;
    NewThread->LastCpu     = 0xFFFFFFFF;
    NewThread->TimeSlice   = 10;
    NewThread->Cooldown    = 0;
    PDebug("CreateThread: About to call GetSystemTicks\n");
    NewThread->StartTime    = GetSystemTicks();
    NewThread->CreationTick = GetSystemTicks();
    PDebug("CreateThread: System ticks retrieved\n");
    NewThread->WaitReason = WaitReasonNone;

    /**
     * Initialize memory management fields.
     * PageDirectory and VirtualBase initialized to defaults.
     * MemoryUsage calculated based on stack usage and converted to KB.
     */
    NewThread->PageDirectory = 0;
    NewThread->VirtualBase   = 0x400000;
    NewThread->MemoryUsage   = (NewThread->StackSize * 2) / 1024;
    PDebug("CreateThread: Scheduling and memory fields initialized\n");

    /**
     * Insert new thread at the head of the global thread doubly linked list.
     * Maintains easy traversal of all threads for management.
     */
    PDebug("CreateThread: Adding to thread list (current head: %p)\n", ThreadList);
    NewThread->Next = ThreadList;
    if (ThreadList)
    {
        ThreadList->Prev = NewThread;
    }
    ThreadList = NewThread;
    PDebug("CreateThread: Added to thread list (new head: %p)\n", ThreadList);

    PDebug("Created thread %u (%s)\n",
           NewThread->ThreadId,
           __Type__ == ThreadTypeKernel ? "Kernel" : "User");
    PDebug("CreateThread: Returning thread %p\n", NewThread);

    return NewThread;
}

/**
 * @brief Destroy Thread
 *
 * Removes thread from all system data structures and frees associated resources.
 * Sets thread state to terminated for identification.
 * Removes thread from the global doubly linked list safely.
 * Frees kernel and user stacks if allocated.
 * Deallocates the thread control block to avoid memory leaks.
 *
 * @param __ThreadPtr__ Pointer to the thread to be destroyed.
 */
void
DestroyThread(Thread* __ThreadPtr__)
{
    if (!__ThreadPtr__)
    {
        return;
    }

    /**
     * Mark the thread's state as terminated to denote it should no longer be scheduled.
     */
    __ThreadPtr__->State = ThreadStateTerminated;

    /**
     * Remove the thread from the global thread list.
     * Use spinlock to prevent concurrent modification issues.
     */
    AcquireSpinLock(&ThreadListLock);

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

    ReleaseSpinLock(&ThreadListLock);

    /**
     * Free memory allocated for stacks.
     * Stacks were allocated with extra space; adjust the pointer accordingly.
     */
    if (__ThreadPtr__->KernelStack)
    {
        KFree((void*)(__ThreadPtr__->KernelStack - __ThreadPtr__->StackSize));
    }

    if (__ThreadPtr__->UserStack)
    {
        KFree((void*)(__ThreadPtr__->UserStack - __ThreadPtr__->StackSize));
    }

    /**
     * Free the thread control block memory itself.
     */
    KFree(__ThreadPtr__);

    PDebug("Destroyed thread %u\n", __ThreadPtr__->ThreadId);
}

/**
 * @brief Suspend Thread
 *
 * Suspends execution of the specified thread by setting a suspended flag.
 * If the thread is currently running or ready, transition its state to blocked.
 * This prevents the thread from being scheduled until resumed.
 *
 * @param __ThreadPtr__ Pointer to the thread to suspend.
 */
void
SuspendThread(Thread* __ThreadPtr__)
{
    if (!__ThreadPtr__)
    {
        return;
    }

    /**
     * Lock to guard access to thread state fields for safe concurrent modification.
     */
    AcquireSpinLock(&ThreadListLock);

    /**
     * Set suspension flag to indicate thread is suspended.
     */
    __ThreadPtr__->Flags |= ThreadFlagSuspended;

    /**
     * If currently running or ready, move thread state to blocked,
     * clearing any wait reason to avoid confusion.
     */
    if (__ThreadPtr__->State == ThreadStateRunning || __ThreadPtr__->State == ThreadStateReady)
    {
        __ThreadPtr__->State      = ThreadStateBlocked;
        __ThreadPtr__->WaitReason = WaitReasonNone;
    }

    ReleaseSpinLock(&ThreadListLock);

    PDebug("Suspended thread %u\n", __ThreadPtr__->ThreadId);
}

/**
 * @brief Resume Thread
 *
 * Removes suspension on the specified thread.
 * Clears the suspend flag and, if thread is blocked and not waiting,
 * updates its state to ready so it can be scheduled.
 *
 * @param __ThreadPtr__ Pointer to the thread to resume.
 */
void
ResumeThread(Thread* __ThreadPtr__)
{
    if (!__ThreadPtr__)
    {
        return;
    }

    /**
     * Clear the suspended flag to allow scheduling.
     */
    __ThreadPtr__->Flags &= ~ThreadFlagSuspended;

    /**
     * If the thread was blocked without a wait reason,
     * set the state to ready so it can return to run queue.
     */
    if (__ThreadPtr__->State == ThreadStateBlocked && __ThreadPtr__->WaitReason == WaitReasonNone)
    {
        __ThreadPtr__->State = ThreadStateReady;
    }

    PDebug("Resumed thread %u\n", __ThreadPtr__->ThreadId);
}

/**
 * @brief Set Thread Priority
 *
 * Updates the priority of a thread.
 * Priority changes affect scheduling decisions and execution frequency.
 *
 * @param __ThreadPtr__ Pointer to the thread to update.
 * @param __Priority__ New priority level.
 */
void
SetThreadPriority(Thread* __ThreadPtr__, ThreadPriority __Priority__)
{
    if (!__ThreadPtr__)
    {
        return;
    }

    __ThreadPtr__->Priority = __Priority__;

    PDebug("Set thread %u priority to %u\n", __ThreadPtr__->ThreadId, __Priority__);
}

/**
 * @brief Set Thread Affinity
 *
 * Defines the CPU affinity mask for the thread.
 * Only CPUs included in this mask can schedule the thread.
 *
 * @param __ThreadPtr__ Pointer to the thread to modify.
 * @param __CpuMask__ Bitmask of CPUs allowed to run the thread.
 */
void
SetThreadAffinity(Thread* __ThreadPtr__, uint32_t __CpuMask__)
{
    if (!__ThreadPtr__)
    {
        return;
    }

    __ThreadPtr__->CpuAffinity = __CpuMask__;

    PDebug("Set thread %u affinity to 0x%x\n", __ThreadPtr__->ThreadId, __CpuMask__);
}

/**
 * @brief Get CPU Load
 *
 * Returns the count of ready threads on the specified CPU.
 * This serves as a heuristic load measurement for scheduling decisions.
 *
 * @param __CpuId__ CPU index.
 * @return Number of ready threads on CPU, or max value if invalid CPU.
 */
uint32_t
GetCpuLoad(uint32_t __CpuId__)
{
    if (__CpuId__ >= MaxCPUs)
    {
        return 0xFFFFFFFF; /* Invalid CPU indicator */
    }

    return GetCpuReadyCount(__CpuId__);
}

/**
 * @brief Find Least Loaded CPU
 *
 * Iterates over all CPUs to find the one with lowest ready thread count.
 * This CPU is considered the least loaded and suitable for thread assignment.
 *
 * @return CPU index with lowest load.
 */
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

/**
 * @brief Calculate Optimal CPU for Thread
 *
 * Determines the best CPU for scheduling a given thread.
 * Honors CPU affinity mask if set, selecting the allowed CPU with lowest load.
 * If no affinity restriction, uses global least loaded CPU heuristic.
 *
 * @param __ThreadPtr__ Pointer to the thread to schedule.
 * @return Selected CPU index for execution.
 */
uint32_t
CalculateOptimalCpu(Thread* __ThreadPtr__)
{
    if (!__ThreadPtr__)
    {
        return 0;
    }

    /**
     * If CPU affinity is restricted, select among allowed CPUs with lowest load.
     */
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
                if (!FoundValidCpu || Load < MinLoad)
                {
                    MinLoad       = Load;
                    BestCpu       = CpuIndex;
                    FoundValidCpu = true;
                }
            }
        }

        return FoundValidCpu ? BestCpu : 0;
    }

    /**
     * No affinity restriction: pick CPU with globally least load.
     */
    return FindLeastLoadedCpu();
}

/**
 * @brief Execute Thread
 *
 * Assigns a thread to run by calculating its optimal CPU based
 * on affinity and load, updating its last CPU assignment,
 * and adding it to the CPU's ready queue for scheduling.
 *
 * @param __ThreadPtr__ Pointer to the thread to execute.
 */
void
ThreadExecute(Thread* __ThreadPtr__)
{
    if (!__ThreadPtr__)
    {
        return;
    }

    /* Determine best CPU for this thread */
    uint32_t TargetCpu = CalculateOptimalCpu(__ThreadPtr__);

    /* Update thread's last CPU assignment */
    __ThreadPtr__->LastCpu = TargetCpu;

    /* Enqueue thread in target CPU’s ready queue */
    AddThreadToReadyQueue(TargetCpu, __ThreadPtr__);

    PDebug("ThreadExecute: Thread %u assigned to CPU %u (Load: %u)\n",
           __ThreadPtr__->ThreadId,
           TargetCpu,
           GetCpuLoad(TargetCpu));
}

/**
 * @brief Execute Multiple Threads with Load Balancing
 *
 * Distributes an array of threads over CPUs by calculating their
 * optimal CPU assignments individually, thereby balancing load.
 *
 * @param __ThreadArray__ Array of thread pointers to schedule.
 * @param __ThreadCount__ Number of threads in array.
 */
void
ThreadExecuteMultiple(Thread** __ThreadArray__, uint32_t __ThreadCount__)
{
    if (!__ThreadArray__ || __ThreadCount__ == 0)
    {
        return;
    }

    for (uint32_t ThreadIndex = 0; ThreadIndex < __ThreadCount__; ThreadIndex++)
    {
        Thread* ThreadPtr = __ThreadArray__[ThreadIndex];
        if (!ThreadPtr)
        {
            continue;
        }

        uint32_t TargetCpu = CalculateOptimalCpu(ThreadPtr);
        ThreadPtr->LastCpu = TargetCpu;
        AddThreadToReadyQueue(TargetCpu, ThreadPtr);

        PDebug("ThreadExecuteMultiple: Thread %u \u2192 CPU %u (Load: %u)\n",
               ThreadPtr->ThreadId,
               TargetCpu,
               GetCpuLoad(TargetCpu));
    }
}

/**
 * @brief Load Balance Existing Threads
 *
 * Checks current load distributions across CPUs.
 * If significant imbalance found, migrates one thread from the most loaded
 * CPU to the least loaded CPU provided CPU affinity constraints allow it.
 * Ensures better overall system throughput by reducing hotspots.
 */
void
LoadBalanceThreads(void)
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
            /* Check if thread's affinity allows migration */
            if (ThreadToMigrate->CpuAffinity == 0xFFFFFFFF ||
                (ThreadToMigrate->CpuAffinity & (1 << MinCpu)))
            {
                ThreadToMigrate->LastCpu = MinCpu;
                AddThreadToReadyQueue(MinCpu, ThreadToMigrate);

                PDebug("LoadBalance: Migrated Thread %u from CPU %u to CPU %u\n",
                       ThreadToMigrate->ThreadId,
                       MaxCpu,
                       MinCpu);
            }
            else
            {
                PWarn("Migration Failed\n");

                /* Put thread back into original CPU’s ready queue if migration failed */
                AddThreadToReadyQueue(MaxCpu, ThreadToMigrate);
            }
        }
    }
}

/**
 * @brief Get System Load Statistics
 *
 * Computes aggregate load statistics across all CPUs:
 * - Total ready threads
 * - Average load per CPU
 * - Maximum CPU load
 * - Minimum CPU load
 *
 * Inputs are optional pointers; provided pointers receive respective statistics.
 *
 * @param __TotalThreads__ Pointer to receive total ready threads count.
 * @param __AverageLoad__ Pointer to receive average load value.
 * @param __MaxLoad__ Pointer to receive maximum load value.
 * @param __MinLoad__ Pointer to receive minimum load value.
 */
void
GetSystemLoadStats(uint32_t* __TotalThreads__,
                   uint32_t* __AverageLoad__,
                   uint32_t* __MaxLoad__,
                   uint32_t* __MinLoad__)
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
        *__AverageLoad__ = (Smp.CpuCount > 0) ? TotalLoad / Smp.CpuCount : 0;
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

/**
 * @brief Thread Yield
 *
 * Causes the calling thread to voluntarily yield execution
 * by triggering a timer interrupt which will invoke the scheduler.
 * Only effects if the current thread is in running state.
 */
void
ThreadYield(void)
{
    uint32_t CpuId   = GetCurrentCpuId();
    Thread*  Current = GetCurrentThread(CpuId);

    if (Current && Current->State == ThreadStateRunning)
    {
        /**
         * Leave thread state as running and rely on scheduler
         * to manage preemption appropriately.
         */
        __asm__ volatile("int $0x20");
    }
}

/**
 * @brief Thread Sleep
 *
 * Puts the current thread to sleep for specified milliseconds.
 * Sets thread to sleeping state with wait reason sleep,
 * establishes wakeup time, and triggers a scheduling interrupt.
 * If no current thread is found, performs fallback busy-wait with halt.
 *
 * @param __Milliseconds__ Number of milliseconds to sleep.
 */
void
ThreadSleep(uint64_t __Milliseconds__)
{
    uint32_t CpuId   = GetCurrentCpuId();
    Thread*  Current = GetCurrentThread(CpuId);

    if (Current)
    {
        Current->State      = ThreadStateSleeping;
        Current->WaitReason = WaitReasonSleep;
        Current->WakeupTime = GetSystemTicks() + __Milliseconds__;

        /* Trigger scheduler by invoking timer interrupt */
        __asm__ volatile("int $0x20");
    }
    else
    {
        PWarn("Sleep Halt loop Has been jumped!\n");

        /* Busy wait fallback using halt instruction */
        uint64_t WakeupTime = GetSystemTicks() + __Milliseconds__;
        while (GetSystemTicks() < WakeupTime)
        {
            __asm__ volatile("hlt");
        }
    }
}

/**
 * @brief Thread Exit
 *
 * Terminates the currently executing thread with specified exit code.
 * Sets the thread state to zombie to mark it for cleanup.
 * Removes thread from ready queue and decrements thread count.
 * Switches CPU current thread to idle thread.
 * Adds zombie thread to zombie queue and enters infinite halt loop
 * as thread is no longer schedulable.
 *
 * @param __ExitCode__ Exit status code of the thread.
 */
void
ThreadExit(uint32_t __ExitCode__)
{
    uint32_t CpuId   = GetCurrentCpuId();
    Thread*  Current = GetCurrentThread(CpuId);

    if (!Current)
    {
        return;
    }

    Current->State    = ThreadStateZombie;
    Current->ExitCode = __ExitCode__;

    PInfo("Thread %u exiting with code %u\n", Current->ThreadId, __ExitCode__);

    /* Remove from ready queue; scheduler will handle */
    CpuScheduler* Scheduler = &CpuSchedulers[CpuId];
    Thread*       Prev      = NULL;
    Thread*       Curr      = Scheduler->ReadyQueue;

    RemoveThreadFromReadyQueue(CpuId); /* Removes current thread */

    CpuSchedulers[CpuId].ThreadCount--;
    SetCurrentThread(CpuId, CpuSchedulers[CpuId].IdleThread); /* Switch to idle */

    /* Add current thread to zombie queue for deferred cleanup */
    AddThreadToZombieQueue(CpuId, Current);

    /* Halt indefinitely as thread is no longer active */
    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Find Thread by ID
 *
 * Searches the global thread list for a thread with the specified ID.
 * Access is synchronized to allow safe concurrent searches.
 *
 * @param __ThreadId__ The unique identifier of the thread to find.
 * @return Pointer to Thread if found; NULL otherwise.
 */
Thread*
FindThreadById(uint32_t __ThreadId__)
{
    AcquireSpinLock(&ThreadListLock);

    Thread* Current = ThreadList;
    while (Current)
    {
        if (Current->ThreadId == __ThreadId__)
        {
            ReleaseSpinLock(&ThreadListLock);
            return Current;
        }
        Current = Current->Next;
    }

    ReleaseSpinLock(&ThreadListLock);
    return NULL;
}

/**
 * @brief Get Thread Count
 *
 * Counts the number of active threads in the system.
 * Access is synchronized with the thread list lock.
 *
 * @return Total number of threads currently managed.
 */
uint32_t
GetThreadCount(void)
{
    uint32_t Count = 0;

    AcquireSpinLock(&ThreadListLock);
    Thread* Current = ThreadList;
    while (Current)
    {
        Count++;
        Current = Current->Next;
    }
    ReleaseSpinLock(&ThreadListLock);

    return Count;
}

/**
 * @brief Wake Sleeping Threads
 *
 * Iterates through the global thread list and wakes any threads
 * that are sleeping and have reached their wakeup time.
 * Updates the thread state and clears sleep-related flags.
 */
void
WakeSleepingThreads(void)
{
    uint64_t CurrentTicks = GetSystemTicks();

    AcquireSpinLock(&ThreadListLock);
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

    ReleaseSpinLock(&ThreadListLock);
}

/**
 * @brief Dump Thread Info
 *
 * Logs detailed information about the specified thread,
 * including identifiers, state, CPU usage, stacks, memory, and affinity.
 *
 * @param __ThreadPtr__ Pointer to thread to dump information.
 */
void
DumpThreadInfo(Thread* __ThreadPtr__)
{
    if (!__ThreadPtr__)
    {
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

/**
 * @brief Dump All Threads
 *
 * Iterates over the global thread list and writes brief summaries
 * of all threads to the debug output.
 * Also logs the total count of threads.
 */
void
DumpAllThreads(void)
{
    AcquireSpinLock(&ThreadListLock);
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

    ReleaseSpinLock(&ThreadListLock);

    PInfo("Total threads: %u\n", Count);
}