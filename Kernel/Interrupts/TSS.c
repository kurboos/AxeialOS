#include <GDT.h>
#include <SymAP.h>

/*
 * Global TSS Storage
 *
 * Tss - The main TSS structure for the bootstrap processor (BSP)
 *       Additional CPUs will have their own TSS structures in CpuTssStructures array
 */
TaskStateSegment
Tss;

/*
 * SetTssEntry - Configure TSS Descriptor in GDT
 *
 * This function sets up a 64-bit TSS descriptor in the GDT. Unlike regular segment
 * descriptors, TSS descriptors span two consecutive GDT entries (16 bytes total)
 * to accommodate the 64-bit base address.
 *
 * Parameters:
 *   __Index__ - Starting index in GDT array (TSS descriptor uses __Index__ and __Index__+1)
 *   __Base__  - 64-bit base address of the TSS structure
 *   __Limit__ - Size of TSS structure minus 1 (limit field)
 *
 * The function splits the 64-bit base address across both GDT entries as required
 * by the x86-64 TSS descriptor format.
 */
void
SetTssEntry(int __Index__, uint64_t __Base__, uint32_t __Limit__)
{
    /*First entry - Main TSS descriptor with lower 32 bits of base*/
    GdtEntries[__Index__].LimitLow = __Limit__ & 0xFFFF;
    GdtEntries[__Index__].BaseLow = __Base__ & 0xFFFF;
    GdtEntries[__Index__].BaseMiddle = (__Base__ >> 16) & 0xFF;
    GdtEntries[__Index__].Access = GdtAccessTss64;  /*TSS access byte*/
    GdtEntries[__Index__].Granularity = ((__Limit__ >> 16) & 0x0F) | GdtGranTss64;
    GdtEntries[__Index__].BaseHigh = (__Base__ >> 24) & 0xFF;

    /*Second entry - Upper 32 bits of base address*/
    GdtEntries[__Index__ + 1].LimitLow = (__Base__ >> 32) & 0xFFFF;
    GdtEntries[__Index__ + 1].BaseLow = (__Base__ >> 48) & 0xFFFF;
    GdtEntries[__Index__ + 1].BaseMiddle = 0;
    GdtEntries[__Index__ + 1].Access = 0;
    GdtEntries[__Index__ + 1].Granularity = 0;
    GdtEntries[__Index__ + 1].BaseHigh = 0;

    /*Debug logging to verify TSS descriptor setup*/
    PDebug("TSS[%d]: Base=0x%lx, Limit=0x%x\n", __Index__, __Base__, __Limit__);
}

/*
 * InitializeTss - Set up Task State Segment
 *
 * This function initializes the TSS for the bootstrap processor (BSP). It:
 * 1. Clears the TSS structure to ensure clean state
 * 2. Sets up the privilege level 0 stack pointer (RSP0)
 * 3. Configures the I/O permission bitmap base
 * 4. Adds the TSS descriptor to the GDT
 * 5. Stores BSP TSS information in per-CPU arrays
 * 6. Loads the TSS selector into the Task Register (TR)
 *
 * The TSS is essential for proper interrupt handling, especially for switching
 * to the correct kernel stack when handling interrupts from user mode.
 */
void
InitializeTss(void)
{
    /*Initialize TSS structure to all zeros*/
    for (size_t Iteration = 0; Iteration < sizeof(TaskStateSegment); Iteration++)
        ((uint8_t*)&Tss)[Iteration] = 0;

    /*Get current stack pointer for privilege level 0 stack*/
    uint64_t CurrentStack;
    __asm__ volatile("mov %%rsp, %0" : "=r"(CurrentStack));

    /*Configure TSS fields for kernel operation*/
    Tss.Rsp0 = CurrentStack;  /*Stack pointer for privilege level 0*/
    Tss.IoMapBase = sizeof(TaskStateSegment);  /*I/O permission bitmap offset*/

    /*Add TSS descriptor to GDT (uses 2 entries)*/
    SetTssEntry(GdtTssIndex, (uint64_t)&Tss, sizeof(TaskStateSegment) - 1);

    /*Store BSP TSS information in per-CPU arrays*/
    CpuTssSelectors[0] = TssSelector;
    CpuTssStructures[0] = Tss;

    /*Load TSS selector into Task Register (TR)*/
    __asm__ volatile("ltr %0" : : "r"((uint16_t)TssSelector));

    /*DEBUG: Print BSP TSS descriptor contents for verification*/
    PDebug("BSP TSS[5]: LimitLow=0x%04x, BaseLow=0x%04x, BaseMiddle=0x%02x, Access=0x%02x, Gran=0x%02x, BaseHigh=0x%02x\n",
           GdtEntries[5].LimitLow, GdtEntries[5].BaseLow, GdtEntries[5].BaseMiddle,
           GdtEntries[5].Access, GdtEntries[5].Granularity, GdtEntries[5].BaseHigh);
    PDebug("BSP TSS[6]: LimitLow=0x%04x, BaseLow=0x%04x, BaseMiddle=0x%02x, Access=0x%02x, Gran=0x%02x, BaseHigh=0x%02x\n",
           GdtEntries[6].LimitLow, GdtEntries[6].BaseLow, GdtEntries[6].BaseMiddle,
           GdtEntries[6].Access, GdtEntries[6].Granularity, GdtEntries[6].BaseHigh);

    PSuccess("TSS init... OK\n");
}
