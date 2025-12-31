#include <APICTimer.h>
#include <LimineSMP.h>
#include <PerCPUData.h>
#include <SymAP.h>
#include <Timer.h>
#include <VMM.h>

static int
CheckApicSupport(void)
{
    uint32_t Eax, Ebx, Ecx, Edx;

    __asm__ volatile("cpuid" : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx) : "a"(1));

    if (!(Edx & (1 << 9)))
    {
        return -Impilict;
    }

    PDebug("CPU supports APIC (CPUID.1:EDX.APIC = 1)\n");
    return SysOkay;
}

int
DetectApicTimer(void)
{
    if (CheckApicSupport() != SysOkay)
    {
        return -Impilict;
    }

    uint64_t ApicBaseMsrValue = ReadMsr(TimerApicBaseMsr);
    PDebug("Base MSR = 0x%016llX\n", ApicBaseMsrValue);

    if (!(ApicBaseMsrValue & TimerApicBaseEnable))
    {
        ApicBaseMsrValue |= TimerApicBaseEnable;
        WriteMsr(TimerApicBaseMsr, ApicBaseMsrValue);

        ApicBaseMsrValue = ReadMsr(TimerApicBaseMsr);
        if (!(ApicBaseMsrValue & TimerApicBaseEnable))
        {
            return -NotCanonical;
        }
        PDebug("APIC Successfully enabled\n");
    }

    uint64_t ApicPhysBase = ApicBaseMsrValue & 0xFFFFF000;
    Timer.ApicBase        = (uint64_t)PhysToVirt(ApicPhysBase);
    PDebug("Physical base = 0x%016llX, Virtual base = 0x%016llX\n", ApicPhysBase, Timer.ApicBase);

    volatile uint32_t* ApicVersionReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegVersion);
    uint32_t           VersionValue   = *ApicVersionReg;

    if (VersionValue == 0xFFFFFFFF || VersionValue == 0x00000000)
    {
        return -NotCanonical;
    }

    uint32_t ApicVersion = VersionValue & 0xFF;
    uint32_t MaxLvtEntry = (VersionValue >> 16) & 0xFF;

    PDebug("Version = 0x%02X, Max LVT = %u\n", ApicVersion, MaxLvtEntry);

    if (MaxLvtEntry < 3)
    {
        return -NotInit;
    }

    return SysOkay;
}

int
InitializeApicTimer(void)
{
    __asm__ volatile("cli");

    volatile uint32_t* SpuriousReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegSpuriousInt);
    volatile uint32_t* LvtTimer    = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegLvtTimer);
    volatile uint32_t* TimerDivide = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerDivide);
    volatile uint32_t* TimerInitCount =
        (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerInitCount);
    volatile uint32_t* TimerCurrCount =
        (volatile uint32_t*)(Timer.ApicBase + TimerApicRegTimerCurrCount);
    volatile uint32_t* EoiReg = (volatile uint32_t*)(Timer.ApicBase + TimerApicRegEoi);
    volatile uint32_t* TprReg =
        (volatile uint32_t*)(Timer.ApicBase + 0x080); /* Task Priority Register */

    *TimerInitCount = 0;
    *LvtTimer       = TimerApicTimerMasked;

    *TprReg = 0;
    *EoiReg = 0;

    *SpuriousReg = 0x100 | 0xFF;

    *TimerDivide = TimerApicTimerDivideBy16;

    *TimerInitCount     = 0xFFFFFFFF;
    uint32_t StartCount = *TimerCurrCount;

    for (uint32_t I = 0; I < 10000; I++)
    {
        __asm__ volatile("outb %%al, $0x80" : : "a"((uint8_t)0)); /* Short delay */
    }

    uint32_t EndCount    = *TimerCurrCount;
    uint32_t TicksIn10ms = StartCount - EndCount;
    Timer.TimerFrequency = TicksIn10ms * 100;

    if (Timer.TimerFrequency < 1000000)
    {
        Timer.TimerFrequency = 100000000; /* Default APIC frequency */
    }

    uint32_t InitialCount = Timer.TimerFrequency / TimerTargetFrequency;
    if (InitialCount == 0)
    {
        InitialCount = 1;
    }

    *TimerInitCount = 0;
    while (*TimerCurrCount != 0)
    {
        __asm__ volatile("nop");
    }

    *LvtTimer = TimerVector | TimerApicTimerPeriodic | TimerApicTimerMasked;

    *TimerInitCount = InitialCount;

    Timer.ActiveTimer = TIMER_TYPE_APIC;

    struct limine_smp_response* SmpResponse = EarlyLimineSmp.response;
    for (uint32_t CpuIndex = 0; CpuIndex < SmpResponse->cpu_count; CpuIndex++)
    {
        PerCpuData* CpuData = GetPerCpuData(CpuIndex);
        CpuData->ApicBase   = Timer.ApicBase;

        PDebug("Set CPU %u APIC base to 0x%llx\n", CpuIndex, CpuData->ApicBase);
    }

    PSuccess("APIC Timer initialized at %u Hz\n", Timer.TimerFrequency);

    *LvtTimer = TimerVector | TimerApicTimerPeriodic;
    return SysOkay;
}
