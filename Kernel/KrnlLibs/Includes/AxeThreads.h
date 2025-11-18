#pragma once

#include <AllTypes.h>
#include <SMP.h>
#include <Sync.h>
#include <VMM.h>

/**
 * Thread States
 */
typedef enum
{

    ThreadStateReady,
    ThreadStateRunning,
    ThreadStateBlocked,
    ThreadStateSleeping,
    ThreadStateZombie,
    ThreadStateTerminated

} ThreadState;

/**
 * Thread Types
 * TODO: add more types such as Child and Parent
 * maybe handled by the TCB
 */
typedef enum
{

    ThreadTypeKernel,
    ThreadTypeUser

} ThreadType;

/**
 * Thread Priority
 */
typedef enum
{

    ThreadPriorityIdle,
    ThreadPriorityLow,
    ThreadPriorityNormal,
    ThreadPriorityHigh,
    ThreadPriorityUltra,
    ThreadPrioritySuper,
    ThreadPrioritykernel

} ThreadPriority;

/**
 * CPU Context
 */
typedef struct
{
    /*GPR*/
    uint64_t Rax, Rbx, Rcx, Rdx;
    uint64_t Rsi, Rdi, Rbp, Rsp;
    uint64_t R8, R9, R10, R11;
    uint64_t R12, R13, R14, R15;

    /*CTRS*/
    uint64_t Rip;
    uint64_t Rflags;
    uint16_t Cs, Ss, Ds, Es, Fs, Gs;

    /*FPU/SSE*/
    uint8_t FpuState[512] __attribute__((aligned(16)));

} ThreadContext;

/**
 * TCB
 */
typedef struct Thread
{
    /*Core ID*/
    uint32_t ThreadId;
    uint32_t ProcessId; /*Parent*/
    char     Name[64];

    /*State mgr*/
    ThreadState    State;
    ThreadType     Type;
    ThreadPriority Priority;
    ThreadPriority BasePriority;

    /*CPU snap*/
    ThreadContext Context;
    uint64_t      KernelStack;
    uint64_t      UserStack;
    uint32_t      StackSize;

    /*MM*/
    uint64_t PageDirectory;
    uint64_t VirtualBase;
    uint32_t MemoryUsage;

    /*Scheduling*/
    uint32_t CpuAffinity;
    uint32_t LastCpu;
    uint64_t TimeSlice;
    uint64_t CpuTime;
    uint64_t StartTime;
    uint64_t WakeupTime;

    /*Sync*/
    void*    WaitingOn;
    uint32_t WaitReason;
    uint32_t ExitCode;
    uint32_t Cooldown;

    /*Linked lists*/
    struct Thread* Next;
    struct Thread* Prev;
    struct Thread* Parent;
    struct Thread* Children;

    /*File descr*/
    void*    FileTable[64];
    uint32_t FileCount;

    /*Signals*/
    uint64_t SignalMask;
    void*    SignalHandlers[32];

    /*Statistics*/
    uint64_t ContextSwitches;
    uint64_t PageFaults;
    uint64_t SystemCalls;

    /*Debugging*/
    uint64_t CreationTick;
    uint32_t Flags;
    void*    DebugInfo;

} Thread;

/**
 * TCB Flags
 */
#define ThreadFlagSystem    (1 << 0)
#define ThreadFlagRealtime  (1 << 1)
#define ThreadFlagPinned    (1 << 2)
#define ThreadFlagTraced    (1 << 3)
#define ThreadFlagSuspended (1 << 4)
#define ThreadFlagCritical  (1 << 5)

/**
 * Wait Reasons
 */
#define WaitReasonNone      0
#define WaitReasonMutex     1
#define WaitReasonSemaphore 2
#define WaitReasonIo        3
#define WaitReasonSleep     4
#define WaitReasonSignal    5
#define WaitReasonChild     6

/**
 * Constants
 */
#define UserVirtualBase 0x0000000000400000ULL
#define KStackSize      8192

/**
 * Globals
 */
extern uint32_t NextThreadId;
extern Thread*  ThreadList;
extern SpinLock ThreadListLock;
extern Thread*  CurrentThreads[MaxCPUs];

/**
 * Functions
 */
/*Thread Manager Core*/
void    InitializeThreadManager(void);
Thread* GetCurrentThread(uint32_t __CpuId__);
void    SetCurrentThread(uint32_t __CpuId__, Thread* __ThreadPtr__); //

/*Thread Lifecycle*/
Thread* CreateThread(ThreadType     __Type__,
                     void*          __EntryPoint__,
                     void*          __Argument__,
                     ThreadPriority __Priority__);
void    DestroyThread(Thread* __ThreadPtr__);
void    SuspendThread(Thread* __ThreadPtr__);
void    ResumeThread(Thread* __ThreadPtr__);

/*Thread Properties*/
void SetThreadPriority(Thread* __ThreadPtr__, ThreadPriority __Priority__);
void SetThreadAffinity(Thread* __ThreadPtr__, uint32_t __CpuMask__);

/*Thread Control*/
void ThreadYield(void);
void ThreadSleep(uint64_t __Milliseconds__);
void ThreadExit(uint32_t __ExitCode__);

/*Thread Queries*/
Thread*  FindThreadById(uint32_t __ThreadId__);
uint32_t GetThreadCount(void);

/*Load Balancing*/
uint32_t GetCpuLoad(uint32_t __CpuId__);             //
uint32_t FindLeastLoadedCpu(void);                   //
uint32_t CalculateOptimalCpu(Thread* __ThreadPtr__); //
void     ThreadExecute(Thread* __ThreadPtr__);
void     ThreadExecuteMultiple(Thread** __ThreadArray__, uint32_t __ThreadCount__);
void     LoadBalanceThreads(void); //
void     GetSystemLoadStats(uint32_t* __TotalThreads__,
                            uint32_t* __AverageLoad__,
                            uint32_t* __MaxLoad__,
                            uint32_t* __MinLoad__); //

/*Utilities*/
void WakeSleepingThreads(void);             //
void DumpThreadInfo(Thread* __ThreadPtr__); //
void DumpAllThreads(void);                  //

/**
 * Public
 */
KEXPORT(GetCurrentThread);
KEXPORT(CreateThread);
KEXPORT(DestroyThread);
KEXPORT(SuspendThread);
KEXPORT(ResumeThread);
KEXPORT(SetThreadPriority);
KEXPORT(SetThreadAffinity);
KEXPORT(ThreadYield);
KEXPORT(ThreadSleep);
KEXPORT(ThreadExit);
KEXPORT(FindThreadById);
KEXPORT(GetThreadCount);
KEXPORT(ThreadExecute);
KEXPORT(ThreadExecuteMultiple);
