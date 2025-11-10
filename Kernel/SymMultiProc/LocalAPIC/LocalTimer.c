#include <Timer.h>          /* Global timer management structures */
#include <APICTimer.h>      /* APIC timer constants and definitions */
#include <VMM.h>            /* Virtual memory mapping functions */

/*
 * SetupApicTimerForThisCpu - Configure Local APIC Timer for Current CPU
 *
 * Sets up the Local APIC timer for the currently executing Application Processor.
 * This function uses timer configuration values that were previously determined
 * and calibrated by the Bootstrap Processor (BSP) to ensure consistent timing
 * across all CPUs in the system.
 *
 * The setup process includes:
 * 1. Verifying BSP timer initialization and retrieving configuration.
 * 2. Locating this CPU's APIC base address via MSR.
 * 3. Stopping any existing timer activity.
 * 4. Clearing pending interrupts and resetting APIC state.
 * 5. Enabling the APIC and configuring timer registers.
 * 6. Calculating and setting the timer initial count for target frequency.
 * 7. Configuring the Local Vector Table (LVT) for periodic interrupts.
 *
 * This function assumes the BSP has already initialized the global Timer structure
 * with valid ApicBase and TimerFrequency values. If not, the function logs a warning
 * and may not configure the timer correctly.
 */
void
SetupApicTimerForThisCpu(void)
{
    /*
     * Verify that the BSP has initialized the timer subsystem.
     * Without proper BSP initialization, timer configuration cannot proceed.
     */
    if (Timer.ApicBase == 0 || Timer.TimerFrequency == 0)
    {
        PWarn("AP: Timer not initialized by BSP\n");
        return;  /* Cannot proceed without BSP timer setup */
    }

    /*
     * Log BSP timer configuration for debugging purposes.
     */
    PDebug("AP: BSP Timer.ApicBase = 0x%016llx\n", Timer.ApicBase);
    PDebug("AP: BSP Timer.TimerFrequency = %u Hz\n", Timer.TimerFrequency);

    /*
     * Read the IA32_APIC_BASE MSR to determine this CPU's APIC base address.
     * Each CPU may have its own APIC, though in practice they often share the same base.
     */
    uint64_t ApicBaseMsr = ReadMsr(0x1B);
    PDebug("AP: My APIC Base MSR = 0x%016llx\n", ApicBaseMsr);

    /*
     * Extract the physical APIC base address and convert to virtual address.
     * The APIC registers are memory-mapped and must be accessed through virtual addresses.
     */
    uint64_t ApicPhysBase = ApicBaseMsr & 0xFFFFF000;
    uint64_t ApicVirtBase = (uint64_t)PhysToVirt(ApicPhysBase);
    PDebug("AP: My APIC Physical = 0x%016llx, Virtual = 0x%016llx\n", ApicPhysBase, ApicVirtBase);
    PDebug("AP: Same as BSP? %s\n", (ApicVirtBase == Timer.ApicBase) ? "YUP" : "NOPE");

    /*
     * Calculate pointers to key APIC timer registers using the BSP's APIC base.
     * Note: Using Timer.ApicBase instead of ApicVirtBase for register access.
     */
    volatile uint32_t *SpuriousReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegSpuriousInt);
    volatile uint32_t *LvtTimer = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegLvtTimer);
    volatile uint32_t *TimerDivide = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerDivide);
    volatile uint32_t *TimerInitCount = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerInitCount);
    volatile uint32_t *EoiReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegEoi);
    volatile uint32_t *TprReg = (volatile uint32_t*)(Timer.ApicBase + 0x080);  /* Task Priority Register */

    /*
     * Log register addresses for debugging.
     */
    PDebug("AP: Register addresses:\n");
    PDebug("  SpuriousReg = 0x%016llx\n", (uint64_t)SpuriousReg);
    PDebug("  LvtTimer = 0x%016llx\n", (uint64_t)LvtTimer);
    PDebug("  TimerInitCount = 0x%016llx\n", (uint64_t)TimerInitCount);

    /*
     * Read current register values before making changes.
     * This helps with debugging and understanding the initial APIC state.
     */
    PDebug("AP: Reading current register values...\n");
    uint32_t CurrentSpurious = *SpuriousReg;
    uint32_t CurrentLvt = *LvtTimer;
    uint32_t CurrentInitCount = *TimerInitCount;
    uint32_t CurrentTpr = *TprReg;

    PDebug("AP: Current values:\n");
    PDebug("  Spurious = 0x%08x\n", CurrentSpurious);
    PDebug("  LVT Timer = 0x%08x\n", CurrentLvt);
    PDebug("  Init Count = 0x%08x\n", CurrentInitCount);
    PDebug("  TPR = 0x%08x\n", CurrentTpr);

    /*
     * Stop any existing timer activity to prevent conflicts during reconfiguration.
     */
    PDebug("AP: Stopping existing timer...\n");
    *TimerInitCount = 0;  /* Setting initial count to 0 stops the timer */
    PDebug("AP: Set InitCount to 0\n");

    *LvtTimer = TimerApicTimerMasked;  /* Mask timer interrupts */
    PDebug("AP: Masked LVT Timer\n");

    /*
     * Clear the Task Priority Register and send End-of-Interrupt to clear any pending interrupts.
     */
    PDebug("AP: Clearing TPR and sending EOI...\n");
    *TprReg = 0;  /* Clear task priority */
    PDebug("AP: Cleared TPR\n");

    *EoiReg = 0;  /* Send EOI to acknowledge any pending interrupts */
    PDebug("AP: Sent EOI\n");

    /*
     * Enable the APIC by setting the Spurious Interrupt Vector Register.
     * Bit 8 enables the APIC, and bits 0-7 set the spurious interrupt vector.
     */
    PDebug("AP: Enabling APIC...\n");
    *SpuriousReg = 0x100 | 0xFF;  /* Enable APIC (bit 8) with vector 0xFF */
    PDebug("AP: Set Spurious register\n");

    /*
     * Set the timer divider to divide by 16.
     * This affects the timer's frequency and must match BSP configuration.
     */
    PDebug("AP: Setting divider...\n");
    *TimerDivide = TimerApicTimerDivideBy16;
    PDebug("AP: Set timer divider\n");

    /*
     * Calculate the initial count for the timer based on the target frequency.
     * The formula uses the calibrated timer frequency from BSP divided by target frequency.
     */
    uint32_t InitialCount = Timer.TimerFrequency / TimerTargetFrequency;
    if (InitialCount == 0)
        InitialCount = 1;  /* Ensure minimum count of 1 */
    PDebug("AP: Calculated InitialCount = %u\n", InitialCount);

    /*
     * Configure the Local Vector Table (LVT) Timer register for periodic interrupts.
     * The timer is configured with the interrupt vector, periodic mode, and unmasked.
     */
    PDebug("AP: Configuring LVT Timer (unmasked)...\n");
    *LvtTimer = TimerVector | TimerApicTimerPeriodic | (0 << 8);  /* Vector | Periodic | Priority 0 */
    PDebug("AP: Set LVT Timer to 0x%08x (unmasked)\n", TimerVector | TimerApicTimerPeriodic);

    /*
     * Start the timer by setting the initial count.
     * The timer will now generate periodic interrupts at the target frequency.
     */
    PDebug("AP: Starting timer (masked)...\n");
    *TimerInitCount = InitialCount;
    PDebug("AP: Set InitCount to %u\n", InitialCount);

    /*
     * Log successful timer configuration.
     */
    PDebug("AP: Local APIC timer configured at %u Hz\n", Timer.TimerFrequency);
}
