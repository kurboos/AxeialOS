#include <Timer.h>          /* Timer management structures and definitions */
#include <VMM.h>            /* Virtual memory management (for timer mapping) */
#include <APICTimer.h>      /* APIC timer constants and functions */
#include <HPETTimer.h>      /* HPET timer constants and functions */
#include <SMP.h>            /* Symmetric multiprocessing functions */
#include <PerCPUData.h>     /* Per-CPU data structures */
#include <SymAP.h>          /* Symmetric Application Processor definitions */
#include <AxeThreads.h>     /* Thread management functions */
#include <AxeSchd.h>        /* Scheduler functions */

/*
 * Global Timer Manager
 *
 * The central timer management structure containing system-wide timer state,
 * configuration, and statistics.
 */
TimerManager Timer;

/*
 * Global Timer Interrupt Counter
 *
 * Tracks the total number of timer interrupts across all CPUs. This counter
 * is updated atomically to ensure accuracy in SMP environments.
 */
volatile uint32_t TimerInterruptCount = 0;

/*
 * InitializeTimer - Initialize the System Timer Subsystem
 *
 * Initializes the timer system by attempting to use available timer hardware
 * in order of preference: APIC (most common), HPET (high precision), PIT (legacy).
 * The function performs hardware detection, initialization, and configuration,
 * falling back to alternative timers if the preferred ones are unavailable.
 *
 * The initialization process includes:
 * 1. Resetting timer state and counters.
 * 2. Attempting APIC timer detection and initialization.
 * 3. Falling back to HPET if APIC fails.
 * 4. Using PIT as final fallback.
 * 5. Enabling interrupts after successful initialization.
 *
 * If no timer hardware is available, the system logs an error and returns
 * without enabling timing services.
 */
void
InitializeTimer(void)
{
    /*
     * Initialize timer state to known values.
     */
    Timer.ActiveTimer = TIMER_TYPE_NONE;
    Timer.SystemTicks = 0;
    Timer.TimerInitialized = 0;

    /*
     * Attempt to initialize APIC timer (most common and preferred).
     */
    if (DetectApicTimer() && InitializeApicTimer()) {
        /* APIC timer successfully initialized */
    }

    /*
     * Fallback to HPET timer if APIC is unavailable.
     * Note: HPET support is marked as TODO for future implementation.
     */
    else if (DetectHpetTimer() && InitializeHpetTimer()) {
        /* HPET timer successfully initialized */
    }

    /*
     * Final fallback to PIT (Programmable Interval Timer).
     */
    else if (InitializePitTimer()) {
        /* PIT timer successfully initialized */
    }

    else
    {
        /*
         * No timer hardware available - system cannot function properly.
         */
        PError("No timer available!\n");
        return;
    }

    /*
     * Mark timer system as initialized and log success.
     */
    Timer.TimerInitialized = 1;

    PSuccess("Timer system initialized using %s\n",
             Timer.ActiveTimer == TIMER_TYPE_HPET ? "HPET" :
             Timer.ActiveTimer == TIMER_TYPE_APIC ? "APIC" : "PIT");

    /*
     * Enable interrupts now that timer is configured.
     */
    __asm__ volatile("sti");
}

/*
 * TimerHandler - System Timer Interrupt Handler
 *
 * Handles timer interrupts (IRQ0, vector 32) from the active timer hardware.
 * This function is called periodically by the timer hardware and performs
 * essential system maintenance tasks including counter updates, thread
 * scheduling, and interrupt acknowledgment.
 *
 * The handler supports different timer types:
 * - APIC: Direct handling with per-CPU APIC EOI.
 * - PIT: Would be handled via PIC (legacy support).
 * - HPET: Would be handled externally with switch logic.
 *
 * Parameters:
 * - __Frame__: Pointer to the interrupt frame containing CPU state.
 *
 * Note: This function runs at interrupt level and must be efficient.
 */
void
TimerHandler(InterruptFrame *__Frame__)
{
    /*
     * Timer interrupt handling varies by hardware:
     * - PIT: Routes through PIC, no special case handling needed.
     * - HPET: External handling with type switching.
     * - APIC: Direct per-CPU handling (most common case).
     */

    uint32_t CpuId = GetCurrentCpuId();
    PerCpuData* CpuData = GetPerCpuData(CpuId);

    /*
     * Optional debug logging for non-BSP CPUs (commented out for performance).
     */
    //if(CpuId != 0)
    //{
    //    PInfo("Timer interrupt from CPU %u\n", CpuId);
    //}

    /*
     * Update per-CPU statistics atomically.
     */
    __atomic_fetch_add(&CpuData->LocalInterrupts, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&CpuData->LocalTicks, 1, __ATOMIC_SEQ_CST);

    /*
     * Update global timer statistics atomically.
     */
    __atomic_fetch_add(&TimerInterruptCount, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&Timer.SystemTicks, 1, __ATOMIC_SEQ_CST);

    /*
     * Wake up any threads that have finished sleeping.
     */
    WakeupSleepingThreads(CpuId);

    /*
     * Perform thread scheduling for this CPU.
     */
    Schedule(CpuId, __Frame__);

    /*
     * Send End-of-Interrupt to the per-CPU APIC.
     */
    volatile uint32_t *EoiReg = (volatile uint32_t*)(CpuData->ApicBase + TimerApicRegEoi);
    *EoiReg = 0;
}

/*
 * GetSystemTicks - Retrieve Current System Tick Count
 *
 * Returns the current value of the system tick counter, which represents
 * the total number of timer interrupts since system initialization.
 *
 * Returns:
 * - Current system tick count as a 64-bit unsigned integer.
 *
 * Note: This value is updated by the timer interrupt handler and may change
 * between successive calls.
 */
uint64_t
GetSystemTicks(void)
{
    return Timer.SystemTicks;
}

/*
 * Sleep - Sleep for Specified Milliseconds
 *
 * Causes the current thread to sleep for the specified number of milliseconds
 * by busy-waiting on the system tick counter. This is a simple implementation
 * that does not yield the CPU to other threads.
 *
 * Parameters:
 * - __Milliseconds__: Number of milliseconds to sleep.
 *
 * Note: This function uses HLT instruction for power efficiency but does not
 * allow other threads to run during the sleep period.
 */
void
Sleep(uint32_t __Milliseconds__)
{
    /*
     * Return immediately if timer system is not initialized.
     */
    if (!Timer.TimerInitialized)
        return;

    uint64_t StartTicks = Timer.SystemTicks;
    uint64_t EndTicks = StartTicks + __Milliseconds__;

    /*
     * Busy-wait until the required number of ticks have elapsed.
     */
    while (Timer.SystemTicks < EndTicks)
    {
        __asm__ volatile("hlt");  /* Halt CPU to save power while waiting */
    }
}

/*
 * GetTimerInterruptCount - Get Total Timer Interrupt Count
 *
 * Returns the total number of timer interrupts that have occurred across
 * all CPUs since system initialization.
 *
 * Returns:
 * - Total timer interrupt count as a 32-bit unsigned integer.
 */
uint32_t
GetTimerInterruptCount(void)
{
    return TimerInterruptCount;
}
