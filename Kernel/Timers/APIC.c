#include <Timer.h>          /* Global timer management structures */
#include <APICTimer.h>      /* APIC timer constants and register definitions */
#include <VMM.h>            /* Virtual memory mapping functions */
#include <PerCPUData.h>     /* Per-CPU data structures */
#include <SymAP.h>          /* Symmetric Application Processor definitions */
#include <LimineSMP.h>      /* Limine SMP request structures */

/*
 * CheckApicSupport - Verify CPU Support for APIC
 *
 * Checks whether the CPU supports the Advanced Programmable Interrupt Controller
 * by examining the CPUID instruction results. The APIC support is indicated by
 * bit 9 in the EDX register of CPUID function 1.
 *
 * Returns:
 * - 1: APIC is supported by the CPU.
 * - 0: APIC is not supported, and an error is logged.
 *
 * This function must be called before attempting to use APIC functionality.
 */
static int
CheckApicSupport(void)
{
    uint32_t Eax, Ebx, Ecx, Edx;

    /*
     * Execute CPUID with EAX=1 to get processor feature information.
     * Bit 9 of EDX indicates APIC support.
     */
    __asm__ volatile("cpuid" : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx) : "a"(1));

    if (!(Edx & (1 << 9)))
    {
        PError("APIC: CPU does not support APIC!\n");
        return 0;
    }

    PDebug("APIC: CPU supports APIC (CPUID.1:EDX.APIC = 1)\n");
    return 1;
}

/*
 * DetectApicTimer - Detect and Validate APIC Timer Hardware
 *
 * Performs comprehensive detection of the APIC timer hardware, including CPU
 * support verification, APIC base address discovery, enabling APIC if necessary,
 * and validating APIC accessibility and version information.
 *
 * The detection process includes:
 * 1. Checking CPU support for APIC via CPUID.
 * 2. Reading and potentially enabling the APIC base MSR.
 * 3. Mapping the APIC registers to virtual memory.
 * 4. Verifying APIC responsiveness by reading the version register.
 * 5. Confirming availability of the timer Local Vector Table entry.
 *
 * Returns:
 * - 1: APIC timer detected and validated successfully.
 * - 0: Detection failed, with specific error messages logged.
 *
 * This function must succeed before InitializeApicTimer can be called.
 */
int
DetectApicTimer(void)
{
    PDebug("APIC: detecting...\n");

    /*
     * Verify that the CPU supports APIC functionality.
     */
    if (!CheckApicSupport())
        return 0;

    /*
     * Read the APIC base address from the IA32_APIC_BASE MSR.
     */
    uint64_t ApicBaseMsrValue = ReadMsr(TimerApicBaseMsr);
    PDebug("APIC: Base MSR = 0x%016llX\n", ApicBaseMsrValue);

    /*
     * Check if the APIC is enabled in the MSR. If not, attempt to enable it.
     */
    if (!(ApicBaseMsrValue & TimerApicBaseEnable))
    {
        PWarn("APIC: Not enabled in MSR, attempting to enable...\n");
        ApicBaseMsrValue |= TimerApicBaseEnable;
        WriteMsr(TimerApicBaseMsr, ApicBaseMsrValue);

        /*
         * Verify that the APIC was successfully enabled.
         */
        ApicBaseMsrValue = ReadMsr(TimerApicBaseMsr);
        if (!(ApicBaseMsrValue & TimerApicBaseEnable))
        {
            PError("APIC: Failed to enable APIC!\n");
            return 0;
        }
        PDebug("APIC: Successfully enabled\n");
    }

    /*
     * Extract the physical base address and map it to virtual memory.
     */
    uint64_t ApicPhysBase = ApicBaseMsrValue & 0xFFFFF000;
    Timer.ApicBase = (uint64_t)PhysToVirt(ApicPhysBase);
    PDebug("APIC: Physical base = 0x%016llX, Virtual base = 0x%016llX\n",
           ApicPhysBase, Timer.ApicBase);

    /*
     * Test APIC accessibility by reading the version register.
     * Invalid values indicate the APIC is not responding.
     */
    volatile uint32_t *ApicVersionReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegVersion);
    uint32_t VersionValue = *ApicVersionReg;

    if (VersionValue == 0xFFFFFFFF || VersionValue == 0x00000000)
    {
        PError("APIC: Invalid version register (0x%08X)\n", VersionValue);
        return 0;
    }

    /*
     * Extract version and maximum LVT entry information.
     */
    uint32_t ApicVersion = VersionValue & 0xFF;
    uint32_t MaxLvtEntry = (VersionValue >> 16) & 0xFF;

    PDebug("APIC: Version = 0x%02X, Max LVT = %u\n", ApicVersion, MaxLvtEntry);

    /*
     * Ensure the timer LVT entry (entry 3) is available.
     */
    if (MaxLvtEntry < 3)
    {
        PError("APIC: Timer LVT entry not available (Max LVT = %u)\n", MaxLvtEntry);
        return 0;
    }

    PSuccess("APIC Timer detected successfully\n");
    return 1;
}

/*
 * InitializeApicTimer - Initialize and Configure APIC Timer
 *
 * Initializes the APIC timer for the Bootstrap Processor, including stopping
 * any existing timers, configuring APIC registers, calibrating the timer
 * frequency, and setting up periodic interrupts. Also configures per-CPU
 * APIC base addresses for SMP support.
 *
 * The initialization process includes:
 * 1. Disabling interrupts and stopping existing timers.
 * 2. Configuring APIC registers (spurious, LVT timer, divider).
 * 3. Calibrating timer frequency through empirical measurement.
 * 4. Setting up periodic timer interrupts with calculated frequency.
 * 5. Configuring per-CPU APIC base addresses.
 *
 * Returns:
 * - 1: APIC timer initialized successfully.
 * - 0: Initialization failed (though this function currently always returns 1).
 *
 * Note: Interrupts are disabled during initialization and re-enabled by the caller.
 * The timer is initially masked and unmasked at the end.
 */
int
InitializeApicTimer(void)
{
    PInfo("APIC: Starting initialization...\n");

    /*
     * Disable interrupts to prevent timer-related interruptions during setup.
     */
    __asm__ volatile("cli");

    /*
     * Set up pointers to key APIC timer registers.
     */
    volatile uint32_t *SpuriousReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegSpuriousInt);
    volatile uint32_t *LvtTimer = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegLvtTimer);
    volatile uint32_t *TimerDivide = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerDivide);
    volatile uint32_t *TimerInitCount = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerInitCount);
    volatile uint32_t *TimerCurrCount = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerCurrCount);
    volatile uint32_t *EoiReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegEoi);
    volatile uint32_t *TprReg = (volatile uint32_t*)(Timer.ApicBase + 0x080);  /* Task Priority Register */

    /*
     * Stop any existing timer activity (e.g., from PIT).
     */
    *TimerInitCount = 0;
    *LvtTimer = TimerApicTimerMasked;

    /*
     * Clear the Task Priority Register and send End-of-Interrupt.
     */
    *TprReg = 0;
    *EoiReg = 0;

    /*
     * Enable the APIC by setting the Spurious Interrupt Vector Register.
     */
    *SpuriousReg = 0x100 | 0xFF;

    /*
     * Set the timer divider to divide by 16.
     */
    *TimerDivide = TimerApicTimerDivideBy16;

    /*
     * Calibrate the timer frequency by measuring ticks over a known time period.
     * Use a busy-wait loop of approximately 10ms to measure tick count.
     */
    *TimerInitCount = 0xFFFFFFFF;
    uint32_t StartCount = *TimerCurrCount;

    for (uint32_t i = 0; i < 10000; i++)
        __asm__ volatile("outb %%al, $0x80" : : "a"((uint8_t)0));  /* Short delay */

    uint32_t EndCount = *TimerCurrCount;
    uint32_t TicksIn10ms = StartCount - EndCount;
    Timer.TimerFrequency = TicksIn10ms * 100;

    /*
     * Fallback to default frequency if calibration seems unreasonable.
     */
    if (Timer.TimerFrequency < 1000000)
        Timer.TimerFrequency = 100000000;  /* Default APIC frequency */

    /*
     * Calculate the initial count for the target interrupt frequency.
     */
    uint32_t InitialCount = Timer.TimerFrequency / TimerTargetFrequency;
    if (InitialCount == 0)
        InitialCount = 1;

    /*
     * Stop the timer completely before final configuration.
     */
    *TimerInitCount = 0;
    while (*TimerCurrCount != 0)
        __asm__ volatile("nop");

    /*
     * Configure the Local Vector Table timer register (initially masked).
     */
    *LvtTimer = TimerVector | TimerApicTimerPeriodic | TimerApicTimerMasked;

    /*
     * Start the timer with the calculated initial count (still masked).
     */
    *TimerInitCount = InitialCount;

    /*
     * Mark APIC as the active timer type.
     */
    Timer.ActiveTimer = TIMER_TYPE_APIC;

    /*
     * Configure per-CPU APIC base addresses for all CPUs.
     * Note: CPU count population issue mentioned in TODO.
     */
    struct limine_smp_response *SmpResponse = EarlyLimineSmp.response;
    for (uint32_t CpuIndex = 0; CpuIndex < SmpResponse->cpu_count; CpuIndex++)
    {
        PerCpuData* CpuData = GetPerCpuData(CpuIndex);
        CpuData->ApicBase = Timer.ApicBase;

        PDebug("APIC: Set CPU %u APIC base to 0x%llx\n", CpuIndex, CpuData->ApicBase);
    }

    PSuccess("APIC Timer initialized at %u Hz\n", Timer.TimerFrequency);

    /*
     * Unmask the timer to enable interrupts (interrupts re-enabled by caller).
     */
    *LvtTimer = TimerVector | TimerApicTimerPeriodic;
    return 1;
}
