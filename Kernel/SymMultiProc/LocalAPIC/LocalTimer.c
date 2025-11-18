#include <APICTimer.h> /* APIC timer constants and definitions */
#include <Timer.h>     /* Global timer management structures */
#include <VMM.h>       /* Virtual memory mapping functions */

/**
 * @brief Configure the Local APIC timer for the current CPU.
 *
 * @details This function is called by Application Processors (APs) to configure their
 * 			Local APIC timer using the frequency and base address initialized by the BSP.
 * 			It performs the following steps:
 * 			Verifies that the BSP has initialized the timer system.
 * 			Reads and maps the AP’s APIC base from the IA32_APIC_BASE MSR.
 * 			Validates register addresses and current values for debugging.
 * 			Stops any existing timer by setting the initial count to zero.
 * 			Masks the LVT timer and clears the Task Priority Register (TPR).
 * 			Sends an End Of Interrupt (EOI) to acknowledge pending interrupts.
 * 			Enables the APIC via the spurious interrupt register.
 * 			Sets the timer divider and calculates the initial count.
 * 			Configures the LVT timer for periodic mode and starts the timer.
 *
 * @return void
 *
 * @note This function must be called during AP startup after the BSP
 *       has completed global timer initialization.
 *
 * @see InitializeApicTimer, ApEntryPoint
 */
void
SetupApicTimerForThisCpu(void)
{

    if (Timer.ApicBase == 0 || Timer.TimerFrequency == 0)
    {
        PWarn("AP: Timer not initialized by BSP\n");
        return; /* Cannot proceed without BSP timer setup */
    }

    PDebug("AP: BSP Timer.ApicBase = 0x%016llx\n", Timer.ApicBase);
    PDebug("AP: BSP Timer.TimerFrequency = %u Hz\n", Timer.TimerFrequency);

    uint64_t ApicBaseMsr = ReadMsr(0x1B);
    PDebug("AP: My APIC Base MSR = 0x%016llx\n", ApicBaseMsr);

    uint64_t ApicPhysBase = ApicBaseMsr & 0xFFFFF000;
    uint64_t ApicVirtBase = (uint64_t)PhysToVirt(ApicPhysBase);
    PDebug("AP: My APIC Physical = 0x%016llx, Virtual = 0x%016llx\n", ApicPhysBase, ApicVirtBase);
    PDebug("AP: Same as BSP? %s\n", (ApicVirtBase == Timer.ApicBase) ? "YUP" : "NOPE");

    volatile uint32_t* SpuriousReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegSpuriousInt);
    volatile uint32_t* LvtTimer    = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegLvtTimer);
    volatile uint32_t* TimerDivide = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerDivide);
    volatile uint32_t* TimerInitCount =
        (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerInitCount);
    volatile uint32_t* EoiReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegEoi);
    volatile uint32_t* TprReg =
        (volatile uint32_t*)(Timer.ApicBase + 0x080); /* Task Priority Register */

    PDebug("AP: Register addresses:\n");
    PDebug("  SpuriousReg = 0x%016llx\n", (uint64_t)SpuriousReg);
    PDebug("  LvtTimer = 0x%016llx\n", (uint64_t)LvtTimer);
    PDebug("  TimerInitCount = 0x%016llx\n", (uint64_t)TimerInitCount);

    PDebug("AP: Reading current register values...\n");
    uint32_t CurrentSpurious  = *SpuriousReg;
    uint32_t CurrentLvt       = *LvtTimer;
    uint32_t CurrentInitCount = *TimerInitCount;
    uint32_t CurrentTpr       = *TprReg;

    PDebug("AP: Current values:\n");
    PDebug("  Spurious = 0x%08x\n", CurrentSpurious);
    PDebug("  LVT Timer = 0x%08x\n", CurrentLvt);
    PDebug("  Init Count = 0x%08x\n", CurrentInitCount);
    PDebug("  TPR = 0x%08x\n", CurrentTpr);

    PDebug("AP: Stopping existing timer...\n");
    *TimerInitCount = 0; /* Setting initial count to 0 stops the timer */
    PDebug("AP: Set InitCount to 0\n");

    *LvtTimer = TimerApicTimerMasked; /* Mask timer interrupts */
    PDebug("AP: Masked LVT Timer\n");

    PDebug("AP: Clearing TPR and sending EOI...\n");
    *TprReg = 0; /* Clear task priority */
    PDebug("AP: Cleared TPR\n");

    *EoiReg = 0; /* Send EOI to acknowledge any pending interrupts */
    PDebug("AP: Sent EOI\n");

    PDebug("AP: Enabling APIC...\n");
    *SpuriousReg = 0x100 | 0xFF; /* Enable APIC (bit 8) with vector 0xFF */
    PDebug("AP: Set Spurious register\n");

    PDebug("AP: Setting divider...\n");
    *TimerDivide = TimerApicTimerDivideBy16;
    PDebug("AP: Set timer divider\n");

    uint32_t InitialCount = Timer.TimerFrequency / TimerTargetFrequency;
    if (InitialCount == 0)
    {
        InitialCount = 1; /* Ensure minimum count of 1 */
    }
    PDebug("AP: Calculated InitialCount = %u\n", InitialCount);

    PDebug("AP: Configuring LVT Timer (unmasked)...\n");
    *LvtTimer =
        TimerVector | TimerApicTimerPeriodic | (0 << 8); /* Vector | Periodic | Priority 0 */
    PDebug("AP: Set LVT Timer to 0x%08x (unmasked)\n", TimerVector | TimerApicTimerPeriodic);

    PDebug("AP: Starting timer (masked)...\n");
    *TimerInitCount = InitialCount;
    PDebug("AP: Set InitCount to %u\n", InitialCount);

    PDebug("AP: Local APIC timer configured at %u Hz\n", Timer.TimerFrequency);
}
