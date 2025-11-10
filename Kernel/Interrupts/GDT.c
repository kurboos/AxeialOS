#include <GDT.h>

/*
 * Global GDT Storage
 *
 * GdtEntries - Array of GDT descriptors, sized for maximum possible entries
 * GdtPtr - GDTR register structure containing GDT base address and limit
 */
GdtEntry
GdtEntries[MaxGdt/*max*/];

GdtPointer
GdtPtr;

/*
 * Per-CPU TSS Support
 *
 * CpuTssSelectors - Array of TSS segment selectors, one per CPU
 * CpuTssStructures - Array of TSS structures, one per CPU for SMP support
 */
uint16_t
CpuTssSelectors[MaxCPUs];

TaskStateSegment
CpuTssStructures[MaxCPUs];

/*
 * SetGdtEntry - Configure a GDT Entry
 *
 * This function sets up a single GDT descriptor with the specified parameters.
 * GDT descriptors are 8 bytes long and contain segment base, limit, access rights,
 * and granularity information.
 *
 * Parameters:
 *   __Index__      - Index in the GDT array (0-based)
 *   __Base__       - 32-bit segment base address (mostly ignored in 64-bit mode)
 *   __Limit__      - 20-bit segment limit (size of segment)
 *   __Access__     - Access byte containing type, privilege, and flags
 *   __Granularity__ - Granularity byte containing size flags and limit extension
 *
 * The function splits the base and limit values across the descriptor fields
 * as required by the x86 GDT format.
 */
void
SetGdtEntry(int __Index__, uint32_t __Base__, uint32_t __Limit__, uint8_t __Access__, uint8_t __Granularity__)
{
    /*Set lower 16 bits of base address*/
    GdtEntries[__Index__].BaseLow
    = (__Base__ & 0xFFFF);

    /*Set middle 8 bits of base address*/
    GdtEntries[__Index__].BaseMiddle
    = (__Base__ >> 16) & 0xFF;

    /*Set upper 8 bits of base address*/
    GdtEntries[__Index__].BaseHigh
    = (__Base__ >> 24) & 0xFF;

    /*Set lower 16 bits of segment limit*/
    GdtEntries[__Index__].LimitLow
    = (__Limit__ & 0xFFFF);

    /*Set granularity byte with upper 4 bits of limit and granularity flags*/
    GdtEntries[__Index__].Granularity
    = ((__Limit__ >> 16) & 0x0F) | (__Granularity__ & 0xF0);

    /*Set access byte containing segment type and permissions*/
    GdtEntries[__Index__].Access
    = __Access__;

    /*Debug logging to verify GDT entry configuration*/
    PDebug("GDT[%d]: Base=0x%x, Limit=0x%x, Access=0x%x, Gran=0x%x\n",
    __Index__, __Base__, __Limit__, (unsigned int)__Access__, (unsigned int)__Granularity__);
}

/*
 * InitializeGdt - Set up the Global Descriptor Table
 *
 * This function initializes the GDT for x86-64 long mode operation. It:
 * 1. Sets up the GDTR register structure
 * 2. Clears all GDT entries
 * 3. Configures standard x86-64 segment descriptors
 * 4. Loads the GDT into the CPU
 * 5. Reloads segment registers for proper operation
 * 6. Initializes the Task State Segment
 *
 * In 64-bit mode, most segmentation is ignored, but the GDT is still required
 * for basic CPU operation and privilege level management.
 */
void
InitializeGdt(void)
{
    PInfo("Initializing GDT ...\n");

    /*Set GDTR limit (size of GDT minus 1) and base address*/
    GdtPtr.Limit = (sizeof(GdtEntry) * MaxGdt) - 1;
    GdtPtr.Base = (uint64_t)&GdtEntries;

    /*Initialize all GDT entries to zero*/
    for (int Index = 0; Index < MaxGdt; Index++)
        GdtEntries[Index] = (GdtEntry){0, 0, 0, 0, 0, 0};

    /*Configure standard x86-64 GDT entries*/
    SetGdtEntry(GdtNullIndex,       GdtBaseIgnored, GdtLimitIgnored, GdtAccessNull,         GdtGranNull);
    SetGdtEntry(GdtKernelCodeIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessKernelCode64, GdtGranCode64);
    SetGdtEntry(GdtKernelDataIndex, GdtBaseIgnored, GdtLimitIgnored, GdtAccessKernelData64, GdtGranData64);
    SetGdtEntry(GdtUserDataIndex,   GdtBaseIgnored, GdtLimitIgnored, GdtAccessUserData64,   GdtGranData64);
    SetGdtEntry(GdtUserCodeIndex,   GdtBaseIgnored, GdtLimitIgnored, GdtAccessUserCode64,   GdtGranCode64);

    /*Load GDT into CPU using LGDT instruction*/
    __asm__ volatile("lgdt %0" : : "m"(GdtPtr) : "memory");

    /*Reload segment registers for x86-64 long mode*/
    /*This is a complex sequence that properly sets up segment registers*/
    __asm__ volatile(
        "mov %0, %%ax\n\t"        /*Load data segment selector*/
        "mov %%ax, %%ds\n\t"      /*Set DS register*/
        "mov %%ax, %%es\n\t"      /*Set ES register*/
        "mov %%ax, %%fs\n\t"      /*Set FS register*/
        "mov %%ax, %%gs\n\t"      /*Set GS register*/
        "mov %%ax, %%ss\n\t"      /*Set SS register*/
        "pushq %1\n\t"            /*Push code segment selector*/
        "pushq $1f\n\t"           /*Push return address*/
        "lretq\n\t"               /*Return to new code segment*/
        "1:\n\t"                  /*Local label for return*/
        :
        : "i"(GdtSegmentReloadValue), "i"(GdtKernelCodePush)
        : "rax", "memory"
    );

    PSuccess("GDT init... OK\n");

    /*Initialize Task State Segment for interrupt handling*/
    InitializeTss();
}
