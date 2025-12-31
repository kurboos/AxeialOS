#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <IDT.h>
#include <KrnPrintf.h>

typedef enum
{

    TIMER_TYPE_NONE,
    TIMER_TYPE_HPET,
    TIMER_TYPE_APIC,
    TIMER_TYPE_PIT

} TimerType;

#define TimerTargetFrequency 1000
#define TimerVector          32

typedef struct
{
    TimerType ActiveTimer;
    uint64_t  ApicBase;
    uint64_t  HpetBase;
    uint32_t  TimerFrequency;
    uint64_t  SystemTicks;
    uint32_t  TimerInitialized;

} TimerManager;

extern TimerManager      Timer;
extern volatile uint32_t TimerInterruptCount;

void     InitializeTimer(SysErr* __Err__);
void     TimerHandler(InterruptFrame* __Frame__, SysErr* __Err__);
uint64_t GetSystemTicks(void);
void     Sleep(uint32_t __Milliseconds__, SysErr* __Err__);
uint32_t GetTimerInterruptCount(void);

int DetectHpetTimer(void);
int DetectApicTimer(void);

int InitializeHpetTimer(void);
int InitializeApicTimer(void);
int InitializePitTimer(void);

uint64_t ReadMsr(uint32_t __Msr__);
void     WriteMsr(uint32_t __Msr__, uint64_t __Value__);

void SetupApicTimerForThisCpu(SysErr* __Err__);
