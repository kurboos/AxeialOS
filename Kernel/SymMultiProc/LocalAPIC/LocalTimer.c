#include <APICTimer.h>
#include <Timer.h>
#include <VMM.h>

void
SetupApicTimerForThisCpu(SysErr* __Err__)
{
    if (Timer.ApicBase == 0 || Timer.TimerFrequency == 0)
    {
        SlotError(__Err__, -NotInit);
        return; /* Cannot proceed without BSP timer setup */
    }

    uint64_t ApicBaseMsr = ReadMsr(0x1B);

    uint64_t ApicPhysBase = ApicBaseMsr & 0xFFFFF000;
    uint64_t ApicVirtBase = (uint64_t)PhysToVirt(ApicPhysBase);
    PDebug("APIC bases same as BSP? %s\n", (ApicVirtBase == Timer.ApicBase) ? "YUP" : "NOPE");

    volatile uint32_t* SpuriousReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegSpuriousInt);
    volatile uint32_t* LvtTimer    = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegLvtTimer);
    volatile uint32_t* TimerDivide = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerDivide);
    volatile uint32_t* TimerInitCount =
        (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerInitCount);
    volatile uint32_t* EoiReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegEoi);
    volatile uint32_t* TprReg =
        (volatile uint32_t*)(Timer.ApicBase + 0x080); /* Task Priority Register */

    uint32_t CurrentSpurious  = *SpuriousReg;
    uint32_t CurrentLvt       = *LvtTimer;
    uint32_t CurrentInitCount = *TimerInitCount;
    uint32_t CurrentTpr       = *TprReg;

    *TimerInitCount = 0; /* Setting initial count to 0 stops the timer */

    *LvtTimer = TimerApicTimerMasked; /* Mask timer interrupts */

    *TprReg = 0; /* Clear task priority */

    *EoiReg      = 0;            /* Send EOI to acknowledge any pending interrupts */
    *SpuriousReg = 0x100 | 0xFF; /* Enable APIC (bit 8) with vector 0xFF */

    *TimerDivide = TimerApicTimerDivideBy16;

    uint32_t InitialCount = Timer.TimerFrequency / TimerTargetFrequency;
    if (InitialCount == 0)
    {
        InitialCount = 1; /* Ensure minimum count of 1 */
    }
    PDebug("Calculated InitialCount = %u\n", InitialCount);

    *LvtTimer =
        TimerVector | TimerApicTimerPeriodic | (0 << 8); /* Vector | Periodic | Priority 0 */

    *TimerInitCount = InitialCount;

    PDebug("Local APIC timer configured at %u Hz\n", Timer.TimerFrequency);
}
