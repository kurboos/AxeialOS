#pragma once

#include <AxeThreads.h>
#include <Errnos.h>
#include <IDT.h>
#include <Sync.h>

typedef struct
{
    Thread*  ReadyQueue;      /*Ready queue*/
    Thread*  WaitingQueue;    /*Blocked threads*/
    Thread*  ZombieQueue;     /*Terminated threads*/
    Thread*  SleepingQueue;   /*Sleeping threads*/
    Thread*  CurrentThread;   /*Currently running thread*/
    Thread*  NextThread;      /*Next thread to run*/
    Thread*  IdleThread;      /*Idle thread for this CPU*/
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

extern CpuScheduler CpuSchedulers[MaxCPUs];

void    InitializeScheduler(SysErr* __Err__);
void    InitializeCpuScheduler(uint32_t __CpuId__, SysErr* __Err__);
void    Schedule(uint32_t __CpuId__, InterruptFrame* __Frame__, SysErr* __Err__);
Thread* GetNextThread(uint32_t __CpuId__);
void    AddThreadToReadyQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__);
Thread* RemoveThreadFromReadyQueue(uint32_t __CpuId__);
void    AddThreadToWaitingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__);
void    AddThreadToZombieQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__);
void    AddThreadToSleepingQueue(uint32_t __CpuId__, Thread* __ThreadPtr__, SysErr* __Err__);
void SaveInterruptFrameToThread(Thread* __ThreadPtr__, InterruptFrame* __Frame__, SysErr* __Err__);
void LoadThreadContextToInterruptFrame(Thread*         __ThreadPtr__,
                                       InterruptFrame* __Frame__,
                                       SysErr*         __Err__);
uint32_t GetCpuThreadCount(uint32_t __CpuId__);
uint32_t GetCpuReadyCount(uint32_t __CpuId__);
uint64_t GetCpuContextSwitches(uint32_t __CpuId__);
uint32_t GetCpuLoadAverage(uint32_t __CpuId__);
void     WakeupSleepingThreads(uint32_t __CpuId__, SysErr* __Err__);
void     CleanupZombieThreads(uint32_t __CpuId__, SysErr* __Err__);
void     DumpCpuSchedulerInfo(uint32_t __CpuId__, SysErr* __Err__);
void     DumpAllSchedulers(SysErr* __Err__);