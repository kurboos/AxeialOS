#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <SMP.h>
#include <Sync.h>
#include <VMM.h>

typedef enum
{

    ThreadStateReady,
    ThreadStateRunning,
    ThreadStateBlocked,
    ThreadStateSleeping,
    ThreadStateZombie,
    ThreadStateTerminated

} ThreadState;

typedef enum
{

    ThreadTypeKernel,
    ThreadTypeUser

} ThreadType;

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

#define ThreadFlagSystem    (1 << 0)
#define ThreadFlagRealtime  (1 << 1)
#define ThreadFlagPinned    (1 << 2)
#define ThreadFlagTraced    (1 << 3)
#define ThreadFlagSuspended (1 << 4)
#define ThreadFlagCritical  (1 << 5)

#define WaitReasonNone      0
#define WaitReasonMutex     1
#define WaitReasonSemaphore 2
#define WaitReasonIo        3
#define WaitReasonSleep     4
#define WaitReasonSignal    5
#define WaitReasonChild     6

#define UserVirtualBase 0x0000000000400000ULL
#define KStackSize      8192

extern uint32_t NextThreadId;
extern Thread*  ThreadList;
extern SpinLock ThreadListLock;
extern Thread*  CurrentThreads[MaxCPUs];

/*Thread Manager Core*/
void    InitializeThreadManager(SysErr* __Err__);
Thread* GetCurrentThread(uint32_t __CpuId__);
void    SetCurrentThread(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__); //

/*Thread Lifecycle*/
Thread* CreateThread(ThreadType     __Type__,
                     void*          __EntryPoint__,
                     void*          __Argument__,
                     ThreadPriority __Priority__);
void    DestroyThread(Thread* __ThreadPtr__, SysErr* __Err__);
void    SuspendThread(Thread* __ThreadPtr__, SysErr* __Err__);
void    ResumeThread(Thread* __ThreadPtr__, SysErr* __Err__);

/*Thread Properties*/
void SetThreadPriority(Thread* __ThreadPtr__, ThreadPriority __Priority__, SysErr* __Err__);
void SetThreadAffinity(Thread* __ThreadPtr__, uint32_t __CpuMask__, SysErr* __Err__);

/*Thread Control*/
void ThreadYield(SysErr* __Err__);
void ThreadSleep(uint64_t __Milliseconds__, SysErr* __Err__);
void ThreadExit(uint32_t __ExitCode__, SysErr* __Err__);

/*Thread Queries*/
Thread*  FindThreadById(uint32_t __ThreadId__);
uint32_t GetThreadCount(void);

/*Load Balancing*/
uint32_t GetCpuLoad(uint32_t __CpuId__);             //
uint32_t FindLeastLoadedCpu(void);                   //
uint32_t CalculateOptimalCpu(Thread* __ThreadPtr__); //
void     ThreadExecute(Thread* __ThreadPtr__, SysErr* __Err__);
void     ThreadExecuteMultiple(Thread** __ThreadArray__, uint32_t __ThreadCount__, SysErr* __Err__);
void     LoadBalanceThreads(SysErr* __Err__); //
void     GetSystemLoadStats(uint32_t* __TotalThreads__,
                            uint32_t* __AverageLoad__,
                            uint32_t* __MaxLoad__,
                            uint32_t* __MinLoad__,
                            SysErr*   __Err__); //

/*Utilities*/
void WakeSleepingThreads(SysErr* __Err__);                   //
void DumpThreadInfo(Thread* __ThreadPtr__, SysErr* __Err__); //
void DumpAllThreads(SysErr* __Err__);                        //

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
