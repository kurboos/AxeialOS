#include <GDT.h>
#include <SymAP.h>

/** @brief Global TSS */
TaskStateSegment Tss;

/**
 * @brief Set a Task State Segment (TSS) descriptor in the GDT.
 *
 * @details Configures two consecutive GDT entries to represent a 64-bit TSS:
 * 			The first entry contains the lower 32 bits of the base and the limit.
 * 			The second entry contains the upper 32 bits of the base.
 *
 * @param __Index__ Index in the GDT where the TSS descriptor begins.
 * @param __Base__  Base address of the TSS structure.
 * @param __Limit__ Size of the TSS structure minus one.
 *
 * @return void
 *
 * @note Each TSS requires two GDT entries.
 */
void
SetTssEntry(int __Index__, uint64_t __Base__, uint32_t __Limit__)
{
    /*First entry - Main TSS descriptor with lower 32 bits of base*/
    GdtEntries[__Index__].LimitLow    = __Limit__ & 0xFFFF;
    GdtEntries[__Index__].BaseLow     = __Base__ & 0xFFFF;
    GdtEntries[__Index__].BaseMiddle  = (__Base__ >> 16) & 0xFF;
    GdtEntries[__Index__].Access      = GdtAccessTss64; /*TSS access byte*/
    GdtEntries[__Index__].Granularity = ((__Limit__ >> 16) & 0x0F) | GdtGranTss64;
    GdtEntries[__Index__].BaseHigh    = (__Base__ >> 24) & 0xFF;

    /*Second entry - Upper 32 bits of base address*/
    GdtEntries[__Index__ + 1].LimitLow    = (__Base__ >> 32) & 0xFFFF;
    GdtEntries[__Index__ + 1].BaseLow     = (__Base__ >> 48) & 0xFFFF;
    GdtEntries[__Index__ + 1].BaseMiddle  = 0;
    GdtEntries[__Index__ + 1].Access      = 0;
    GdtEntries[__Index__ + 1].Granularity = 0;
    GdtEntries[__Index__ + 1].BaseHigh    = 0;

    /*Debug logging to verify TSS descriptor setup*/
    PDebug("TSS[%d]: Base=0x%lx, Limit=0x%x\n", __Index__, __Base__, __Limit__);
}

/**
 * @brief Initialize the Task State Segment (TSS).
 *
 * @details Clears the TSS structure.
 * 			Sets the kernel stack pointer (Rsp0) for privilege level 0.
 * 			Configures the I/O permission bitmap offset.
 * 			Adds the TSS descriptor to the GDT.
 * 			Loads the TSS selector into the Task Register (TR).
 *
 * @return void
 *
 * @note Must be called during BSP initialization to enable proper
 *       interrupt handling and privilege transitions.
 */
void
InitializeTss(void)
{
    /*Initialize TSS structure to all zeros*/
    for (size_t Iteration = 0; Iteration < sizeof(TaskStateSegment); Iteration++)
    {
        ((uint8_t*)&Tss)[Iteration] = 0;
    }

    /*Get current stack pointer for privilege level 0 stack*/
    uint64_t CurrentStack;
    __asm__ volatile("mov %%rsp, %0" : "=r"(CurrentStack));

    /*Configure TSS fields for kernel operation*/
    Tss.Rsp0      = CurrentStack;             /*Stack pointer for privilege level 0*/
    Tss.IoMapBase = sizeof(TaskStateSegment); /*I/O permission bitmap offset*/

    /*Add TSS descriptor to GDT (uses 2 entries)*/
    SetTssEntry(GdtTssIndex, (uint64_t)&Tss, sizeof(TaskStateSegment) - 1);

    /*Store BSP TSS information in per-CPU arrays*/
    CpuTssSelectors[0]  = TssSelector;
    CpuTssStructures[0] = Tss;

    /*Load TSS selector into Task Register (TR)*/
    __asm__ volatile("ltr %0" : : "r"((uint16_t)TssSelector));

    /** @test Print BSP TSS descriptor contents for verification*/
    PDebug("BSP TSS[5]: LimitLow=0x%04x, BaseLow=0x%04x, BaseMiddle=0x%02x, Access=0x%02x, "
           "Gran=0x%02x, BaseHigh=0x%02x\n",
           GdtEntries[5].LimitLow,
           GdtEntries[5].BaseLow,
           GdtEntries[5].BaseMiddle,
           GdtEntries[5].Access,
           GdtEntries[5].Granularity,
           GdtEntries[5].BaseHigh);

    PDebug("BSP TSS[6]: LimitLow=0x%04x, BaseLow=0x%04x, BaseMiddle=0x%02x, Access=0x%02x, "
           "Gran=0x%02x, BaseHigh=0x%02x\n",
           GdtEntries[6].LimitLow,
           GdtEntries[6].BaseLow,
           GdtEntries[6].BaseMiddle,
           GdtEntries[6].Access,
           GdtEntries[6].Granularity,
           GdtEntries[6].BaseHigh);

    PSuccess("TSS init... OK\n");
}
