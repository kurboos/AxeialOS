#pragma once

#include <AllTypes.h>
#include <IDT.h>
#include <KrnPrintf.h>

/**
 * Timer Types
 */
typedef enum
{

    TIMER_TYPE_NONE,
    TIMER_TYPE_HPET,
    TIMER_TYPE_APIC,
    TIMER_TYPE_PIT

} TimerType;

/**
 * Main Constants
 */
#define TimerTargetFrequency 1000
#define TimerVector          32

/**
 * Timer Manager
 */
typedef struct
{

    TimerType ActiveTimer;
    uint64_t  ApicBase;
    uint64_t  HpetBase;
    uint32_t  TimerFrequency;
    uint64_t  SystemTicks;
    uint32_t  TimerInitialized;

} TimerManager;

/**
 * Globals
 */
extern TimerManager      Timer;
extern volatile uint32_t TimerInterruptCount;

/**
 * Functions
 */
void     InitializeTimer(void);
void     TimerHandler(InterruptFrame* __Frame__);
uint64_t GetSystemTicks(void);
void     Sleep(uint32_t __Milliseconds__);
uint32_t GetTimerInterruptCount(void);

/**
 * Detection
 */
int DetectHpetTimer(void);
int DetectApicTimer(void);

/**
 * Initialization
 */
int InitializeHpetTimer(void);
int InitializeApicTimer(void);
int InitializePitTimer(void);

/**
 * MSR Helpers
 */
uint64_t ReadMsr(uint32_t __Msr__);
void     WriteMsr(uint32_t __Msr__, uint64_t __Value__);

/**
 * AP Functions
 */
void SetupApicTimerForThisCpu(void);
