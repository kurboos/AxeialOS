#include <GDT.h>

/** @brief General GDT Globals */
GdtEntry GdtEntries[MaxGdt /*max*/];

GdtPointer GdtPtr;

uint16_t CpuTssSelectors[MaxCPUs];

TaskStateSegment CpuTssStructures[MaxCPUs];

/**
 * @brief Set a standard GDT entry.
 *
 * @details Configures a single GDT entry with base, limit, access, and granularity fields.
 * 			Used for code, data, and user segments in x86-64 long mode.
 *
 * @param __Index__      Index in the GDT.
 * @param __Base__       Base address of the segment.
 * @param __Limit__      Segment limit.
 * @param __Access__     Access byte (segment type and permissions).
 * @param __Granularity__ Granularity byte (flags and upper limit bits).
 *
 * @return void
 */
void
SetGdtEntry(int      __Index__,
            uint32_t __Base__,
            uint32_t __Limit__,
            uint8_t  __Access__,
            uint8_t  __Granularity__)
{
    /*Set lower 16 bits of base address*/
    GdtEntries[__Index__].BaseLow = (__Base__ & 0xFFFF);

    /*Set middle 8 bits of base address*/
    GdtEntries[__Index__].BaseMiddle = (__Base__ >> 16) & 0xFF;

    /*Set upper 8 bits of base address*/
    GdtEntries[__Index__].BaseHigh = (__Base__ >> 24) & 0xFF;

    /*Set lower 16 bits of segment limit*/
    GdtEntries[__Index__].LimitLow = (__Limit__ & 0xFFFF);

    /*Set granularity byte with upper 4 bits of limit and granularity flags*/
    GdtEntries[__Index__].Granularity = ((__Limit__ >> 16) & 0x0F) | (__Granularity__ & 0xF0);

    /*Set access byte containing segment type and permissions*/
    GdtEntries[__Index__].Access = __Access__;

    /*Debug logging to verify GDT entry configuration*/
    PDebug("GDT[%d]: Base=0x%x, Limit=0x%x, Access=0x%x, Gran=0x%x\n",
           __Index__,
           __Base__,
           __Limit__,
           (unsigned int)__Access__,
           (unsigned int)__Granularity__);
}

/**
 * @brief Initialize the Global Descriptor Table (GDT).
 *
 * @details Sets up the GDTR with base and limit.
 * 			Clears all GDT entries.
 * 			Configures standard kernel and user code/data segments.
 * 			Loads the GDT into the CPU using `lgdt`.
 * 			Reloads segment registers for long mode.
 * 			Initializes the Task State Segment (TSS).
 *
 * @return void
 *
 * @note Must be called during kernel initialization before enabling interrupts.
 */
void
InitializeGdt(void)
{
    PInfo("Initializing GDT ...\n");

    /*Set GDTR limit (size of GDT minus 1) and base address*/
    GdtPtr.Limit = (sizeof(GdtEntry) * MaxGdt) - 1;
    GdtPtr.Base  = (uint64_t)&GdtEntries;

    /*Initialize all GDT entries to zero*/
    for (int Index = 0; Index < MaxGdt; Index++)
    {
        GdtEntries[Index] = (GdtEntry){0, 0, 0, 0, 0, 0};
    }

    /*Configure standard x86-64 GDT entries*/
    SetGdtEntry(GdtNullIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessNull, GdtGranNull);
    SetGdtEntry(
        GdtKernelCodeIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessKernelCode64, GdtGranCode64);
    SetGdtEntry(
        GdtKernelDataIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessKernelData64, GdtGranData64);
    SetGdtEntry(
        GdtUserDataIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessUserData64, GdtGranData64);
    SetGdtEntry(
        GdtUserCodeIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessUserCode64, GdtGranCode64);

    /*Load GDT into CPU using LGDT instruction*/
    __asm__ volatile("lgdt %0" : : "m"(GdtPtr) : "memory");

    /*Reload segment registers for x86-64 long mode*/
    /*This is a complex sequence (Is it?) that properly sets up segment registers*/
    __asm__ volatile("mov %0, %%ax\n\t"   /*Load data segment selector*/
                     "mov %%ax, %%ds\n\t" /*Set DS register*/
                     "mov %%ax, %%es\n\t" /*Set ES register*/
                     "mov %%ax, %%fs\n\t" /*Set FS register*/
                     "mov %%ax, %%gs\n\t" /*Set GS register*/
                     "mov %%ax, %%ss\n\t" /*Set SS register*/
                     "pushq %1\n\t"       /*Push code segment selector*/
                     "pushq $1f\n\t"      /*Push return address*/
                     "lretq\n\t"          /*Return to new code segment*/
                     "1:\n\t"             /*Local label for return*/
                     :
                     : "I"(GdtSegmentReloadValue), "I"(GdtKernelCodePush)
                     : "rax", "memory");

    PSuccess("GDT init... OK\n");

    /*Initialize Task State Segment for interrupt handling*/
    InitializeTss();
}
