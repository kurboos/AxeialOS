#pragma once

#include <AxeThreads.h>
#include <IDT.h>

/**
 * Per-CPU Scheduler
 */
typedef
struct
{

    Thread* ReadyQueue;		  /*Ready queue*/
    Thread* WaitingQueue;     /*Blocked threads*/
    Thread* ZombieQueue;      /*Terminated threads*/
    Thread* SleepingQueue;    /*Sleeping threads*/
    Thread* CurrentThread;    /*Currently running thread*/
    Thread* NextThread;       /*Next thread to run*/
    Thread* IdleThread;       /*Idle thread for this CPU*/
    uint32_t ThreadCount;     /*Total threads on this CPU*/
    uint32_t ReadyCount;      /*Ready threads count*/
    uint32_t Priority;        /*Current priority level*/
    uint64_t LastSchedule;    /*Last schedule time*/
    uint64_t ScheduleTicks;   /*Schedule counter*/
    SpinLock SchedulerLock;   /*Protect scheduler state*/
    uint64_t ContextSwitches; /*Context switch count*/
    uint64_t IdleTicks;       /*Time spent idle*/
    uint32_t LoadAverage;     /*Load average*/
    
} CpuScheduler;

/**
 * Globals
 */
extern CpuScheduler CpuSchedulers[MaxCPUs];

/**
 * Functions
 */
void InitializeScheduler(void);
void InitializeCpuScheduler(uint32_t __CpuId__);
void Schedule(uint32_t __CpuId__, InterruptFrame* __Frame__);
Thread* GetNextThread(uint32_t __CpuId__);
void AddThreadToReadyQueue(uint32_t __CpuId__, Thread* __ThreadPtr__);
Thread* RemoveThreadFromReadyQueue(uint32_t __CpuId__);
void AddThreadToWaitingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__);
void AddThreadToZombieQueue(uint32_t __CpuId__, Thread* __ThreadPtr__);
void AddThreadToSleepingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__);
void SaveInterruptFrameToThread(Thread* __ThreadPtr__, InterruptFrame* __Frame__);
void LoadThreadContextToInterruptFrame(Thread* __ThreadPtr__, InterruptFrame* __Frame__);
uint32_t GetCpuThreadCount(uint32_t __CpuId__);
uint32_t GetCpuReadyCount(uint32_t __CpuId__);
uint64_t GetCpuContextSwitches(uint32_t __CpuId__);
uint32_t GetCpuLoadAverage(uint32_t __CpuId__);
void WakeupSleepingThreads(uint32_t __CpuId__);
void CleanupZombieThreads(uint32_t __CpuId__);
void DumpCpuSchedulerInfo(uint32_t __CpuId__);
void DumpAllSchedulers(void);