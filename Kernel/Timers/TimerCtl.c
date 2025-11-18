#include <APICTimer.h>  /* APIC timer constants and functions */
#include <AxeSchd.h>    /* Scheduler functions */
#include <AxeThreads.h> /* Thread management functions */
#include <HPETTimer.h>  /* HPET timer constants and functions */
#include <PerCPUData.h> /* Per-CPU data structures */
#include <SMP.h>        /* Symmetric multiprocessing functions */
#include <SymAP.h>      /* Symmetric Application Processor definitions */
#include <Timer.h>      /* Timer management structures and definitions */
#include <VMM.h>        /* Virtual memory management (for timer mapping) */

TimerManager Timer;

volatile uint32_t TimerInterruptCount = 0;

/**
 * @brief Initialize the system timer.
 *
 * @details Attempts to detect and initialize one of the available hardware timers
 * 			(APIC, HPET, or PIT). If successful, marks the timer system as initialized
 * 			and enables interrupts.
 *
 * @return void
 *
 * @note Must be called during kernel initialization before using sleep or scheduling.
 */
void
InitializeTimer(void)
{

    Timer.ActiveTimer      = TIMER_TYPE_NONE;
    Timer.SystemTicks      = 0;
    Timer.TimerInitialized = 0;

    if (DetectApicTimer() && InitializeApicTimer())
    {
        /* APIC timer successfully initialized */
    }

    else if (DetectHpetTimer() && InitializeHpetTimer())
    {
        /* HPET timer successfully initialized */
    }

    else if (InitializePitTimer())
    {
        /* PIT timer successfully initialized */
    }

    else
    {

        PError("No timer available!\n");
        return;
    }

    Timer.TimerInitialized = 1;

    PSuccess("Timer system initialized using %s\n",
             Timer.ActiveTimer == TIMER_TYPE_HPET   ? "HPET"
             : Timer.ActiveTimer == TIMER_TYPE_APIC ? "APIC"
                                                    : "PIT");

    __asm__ volatile("sti");
}

/**
 * @brief Handle a timer interrupt.
 *
 * @details Updates per-CPU interrupt and tick counters, increments global system ticks,
 * 			wakes up sleeping threads, and invokes the scheduler. Finally, signals End Of Interrupt
 * (EOI) to the local APIC.
 *
 * @param __Frame__ Pointer to the interrupt frame at the time of the interrupt.
 *
 * @return void
 *
 * @internal Called automatically by the interrupt handler.
 */
void
TimerHandler(InterruptFrame* __Frame__)
{

    uint32_t    CpuId   = GetCurrentCpuId();
    PerCpuData* CpuData = GetPerCpuData(CpuId);

    __atomic_fetch_add(&CpuData->LocalInterrupts, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&CpuData->LocalTicks, 1, __ATOMIC_SEQ_CST);

    __atomic_fetch_add(&TimerInterruptCount, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&Timer.SystemTicks, 1, __ATOMIC_SEQ_CST);

    WakeupSleepingThreads(CpuId);
    Schedule(CpuId, __Frame__);

    volatile uint32_t* EoiReg = (volatile uint32_t*)(CpuData->ApicBase + TimerApicRegEoi);
    *EoiReg                   = 0;
}

/**
 * @brief Get the current system tick count.
 *
 * @details Returns the number of ticks since the timer system was initialized.
 * 			Each tick represents one timer interrupt.
 *
 * @return Current tick count as a 64-bit integer.
 */
uint64_t
GetSystemTicks(void)
{
    return Timer.SystemTicks;
}

/**
 * @brief Sleep for a specified duration.
 *
 * @details Halts the CPU until the requested number of milliseconds has elapsed,
 * 			based on the system tick counter. Uses `hlt` to reduce power consumption
 * 			while waiting. (Blocking)
 *
 * @param __Milliseconds__ Duration to sleep, in milliseconds.
 *
 * @return void
 *
 * @note Requires the timer system to be initialized.
 */
void
Sleep(uint32_t __Milliseconds__)
{

    if (!Timer.TimerInitialized)
    {
        return;
    }

    uint64_t StartTicks = Timer.SystemTicks;
    uint64_t EndTicks   = StartTicks + __Milliseconds__;

    while (Timer.SystemTicks < EndTicks)
    {
        __asm__ volatile("hlt"); /* Halt CPU to save power while waiting */
    }
}

/**
 * @brief Get the total number of timer interrupts.
 *
 * @details Returns the global count of timer interrupts handled since initialization.
 *
 * @return Interrupt count as a 32-bit integer.
 */
uint32_t
GetTimerInterruptCount(void)
{
    return TimerInterruptCount;
}
