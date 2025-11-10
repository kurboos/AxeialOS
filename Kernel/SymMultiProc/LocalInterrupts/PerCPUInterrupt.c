#include <SMP.h>            /* SMP management structures and constants */
#include <GDT.h>            /* Global Descriptor Table definitions */
#include <SymAP.h>          /* Symmetric Application Processor definitions */
#include <PerCPUData.h>     /* Per-CPU data structure definitions */
#include <VMM.h>            /* Virtual Memory Management for address translation */
#include <Timer.h>          /* Timer interfaces for per-CPU timer data */

/*
 * Per-CPU Data Array
 *
 * Array of PerCpuData structures, one for each possible CPU in the system.
 * This array stores CPU-specific data including descriptor tables, TSS,
 * stack information, and APIC base addresses. The array size is bounded
 * by MaxCPUs to prevent excessive memory usage.
 */
PerCpuData CpuDataArray[MaxCPUs];

/*
 * PerCpuInterruptInit - Initialize Per-CPU Interrupt Handling
 *
 * Initializes all per-CPU data structures and descriptor tables for a specific CPU.
 * This function sets up the CPU's own GDT, IDT, TSS, and APIC configuration,
 * ensuring that interrupt handling and task switching work correctly on this CPU.
 *
 * The initialization process includes:
 * 1. Setting up the CPU's stack pointer in per-CPU data.
 * 2. Copying and customizing GDT from BSP template with CPU-specific TSS.
 * 3. Initializing TSS with kernel stack for interrupt handling.
 * 4. Copying IDT from BSP template.
 * 5. Setting up descriptor table pointers and loading them.
 * 6. Detecting and mapping APIC base address.
 * 7. Reloading segment registers and TSS selector.
 * 8. Verifying that all tables were loaded correctly.
 *
 * Parameters:
 * - __CpuNumber__: The logical CPU number being initialized.
 * - __StackTop__: The top address of the CPU's dedicated kernel stack.
 *
 * This function must be called for each CPU during its startup process,
 * typically from the AP entry point after stack allocation.
 */
void
PerCpuInterruptInit(uint32_t __CpuNumber__, uint64_t __StackTop__)
{
    /*
     * Retrieve the per-CPU data structure for this CPU.
     * All subsequent operations will modify this structure.
     */
    PerCpuData* CpuData = &CpuDataArray[__CpuNumber__];

    PDebug("CPU %u: Initializing per-CPU data at 0x%p\n", __CpuNumber__, CpuData);

    /*
     * Initialize the per-CPU stack top pointer.
     * This stack is used for interrupt handling and kernel operations on this CPU.
     */
    CpuData->StackTop = __StackTop__;

    /*
     * Build the per-CPU GDT by copying the BSP's GDT template.
     * Each CPU needs its own GDT to allow CPU-specific TSS entries.
     */
    for (int Index = 0; Index < MaxGdt; Index++)
        CpuData->Gdt[Index] = GdtEntries[Index];

    PDebug("CPU %u: Copied GDT template\n", __CpuNumber__);

    /*
     * Initialize the per-CPU TSS (Task State Segment).
     * The TSS is zeroed out first, then configured with the CPU's kernel stack
     * and I/O map base for interrupt handling.
     */
    for (size_t Iteration = 0; Iteration < sizeof(TaskStateSegment); Iteration++)
        ((uint8_t*)&CpuData->Tss)[Iteration] = 0;

    CpuData->Tss.Rsp0 = __StackTop__;  /* Kernel stack pointer for ring 0 */
    CpuData->Tss.IoMapBase = sizeof(TaskStateSegment);  /* I/O permission bitmap offset */

    PDebug("CPU %u: TSS initialized with Rsp0=0x%llx\n", __CpuNumber__, CpuData->Tss.Rsp0);

    /*
     * Update the per-CPU GDT with the CPU-specific TSS descriptor.
     * The TSS descriptor spans two GDT entries (indices 5 and 6) in long mode.
     * The base address, limit, and access rights are set according to x86-64 specification.
     */
    uint64_t TssBase = (uint64_t)&CpuData->Tss;
    uint32_t TssLimit = sizeof(TaskStateSegment) - 1;

    /* TSS descriptor low 8 bytes (GDT entry 5) */
    CpuData->Gdt[5].LimitLow = TssLimit & 0xFFFF;
    CpuData->Gdt[5].BaseLow = TssBase & 0xFFFF;
    CpuData->Gdt[5].BaseMiddle = (TssBase >> 16) & 0xFF;
    CpuData->Gdt[5].Access = 0x89;  /* Present, 64-bit TSS */
    CpuData->Gdt[5].Granularity = (TssLimit >> 16) & 0x0F;
    CpuData->Gdt[5].BaseHigh = (TssBase >> 24) & 0xFF;

    /* TSS descriptor high 8 bytes (GDT entry 6) */
    CpuData->Gdt[6].LimitLow = (TssBase >> 32) & 0xFFFF;
    CpuData->Gdt[6].BaseLow = (TssBase >> 48) & 0xFFFF;
    CpuData->Gdt[6].BaseMiddle = 0;
    CpuData->Gdt[6].Access = 0;
    CpuData->Gdt[6].Granularity = 0;
    CpuData->Gdt[6].BaseHigh = 0;

    PDebug("CPU %u: GDT updated with TSS at 0x%llx\n", __CpuNumber__, TssBase);

    /*
     * Copy the IDT from the BSP template to the per-CPU IDT.
     * Each CPU uses the same interrupt handlers, so the IDT can be shared.
     */
    for (int Index = 0; Index < MaxIdt; Index++)
        CpuData->Idt[Index] = IdtEntries[Index];

    PDebug("CPU %u: Copied IDT template\n", __CpuNumber__);

    /*
     * Set up the GDT and IDT pointer structures for this CPU.
     * These pointers are used by the LGDT and LIDT instructions.
     */
    CpuData->GdtPtr.Limit = (sizeof(GdtEntry) * MaxGdt) - 1;
    CpuData->GdtPtr.Base = (uint64_t)CpuData->Gdt;

    CpuData->IdtPtr.Limit = (sizeof(IdtEntry) * MaxIdt) - 1;
    CpuData->IdtPtr.Base = (uint64_t)CpuData->Idt;

    /*
     * Determine the APIC base address for this CPU by reading the IA32_APIC_BASE MSR.
     * Convert the physical address to virtual address for kernel access.
     */
    CpuData->ApicBase = (uint64_t)PhysToVirt(ReadMsr(0x1B) & 0xFFFFF000);

    PDebug("CPU %u: APIC base = 0x%llx\n", __CpuNumber__, CpuData->ApicBase);

    /*
     * Initialize per-CPU timer and interrupt counters.
     * These track local timer ticks and interrupt counts for this CPU.
     */
    CpuData->LocalTicks = 0;
    CpuData->LocalInterrupts = 0;

    /*
     * Load the per-CPU GDT using the LGDT instruction.
     * This makes the CPU-specific GDT active.
     */
    __asm__ volatile("lgdt %0" : : "m"(CpuData->GdtPtr) : "memory");

    /*
     * Load the per-CPU IDT using the LIDT instruction.
     * This activates the interrupt descriptor table for this CPU.
     */
    __asm__ volatile("lidt %0" : : "m"(CpuData->IdtPtr) : "memory");

    /*
     * Reload the code segment register (CS) by performing a far return.
     * This ensures the new GDT is properly active. The far jump targets
     * a local label to continue execution with the updated CS.
     */
    __asm__ volatile(
        "pushq $0x08\n\t"        /* Push kernel code segment selector */
        "leaq 1f(%%rip), %%rax\n\t"  /* Load address of local label */
        "pushq %%rax\n\t"        /* Push return address */
        "lretq\n\t"              /* Far return to reload CS */
        "1:\n\t"                 /* Local label for return target */
        : : : "rax", "memory"
    );

    /*
     * Reload all data segment registers (DS, ES, FS, GS, SS) with kernel data selector.
     * This ensures all segment registers point to the correct descriptors in the new GDT.
     */
    __asm__ volatile(
        "mov $0x10, %%ax\n\t"    /* Kernel data segment selector */
        "mov %%ax, %%ds\n\t"     /* Reload DS */
        "mov %%ax, %%es\n\t"     /* Reload ES */
        "mov %%ax, %%fs\n\t"     /* Reload FS */
        "mov %%ax, %%gs\n\t"     /* Reload GS */
        "mov %%ax, %%ss\n\t"     /* Reload SS */
        : : : "ax", "memory"
    );

    /*
     * Load the Task Register (TR) with the TSS selector.
     * This makes the per-CPU TSS active for task switching and interrupt handling.
     */
    __asm__ volatile("ltr %0" : : "r"((uint16_t)TssSelector) : "memory");

    /*
     * Verify that the descriptor tables and TSS were loaded correctly.
     * Read back the current GDT, IDT, and TR values and compare with expected values.
     */
    GdtPointer VerifyGdt;
    IdtPointer VerifyIdt;
    uint16_t VerifyTr;

    __asm__ volatile("sgdt %0" : "=m"(VerifyGdt));  /* Store current GDT pointer */
    __asm__ volatile("sidt %0" : "=m"(VerifyIdt));  /* Store current IDT pointer */
    __asm__ volatile("str %0" : "=r"(VerifyTr));    /* Store current TR value */

    PDebug("CPU %u: Verification:\n", __CpuNumber__);
    PDebug("  GDT: Expected=0x%llx, Actual=0x%llx\n", CpuData->GdtPtr.Base, VerifyGdt.Base);
    PDebug("  IDT: Expected=0x%llx, Actual=0x%llx\n", CpuData->IdtPtr.Base, VerifyIdt.Base);
    PDebug("  TSS: Expected=0x%x, Actual=0x%x\n", TssSelector, VerifyTr);

    /*
     * Check if the loaded tables match the expected values.
     * Log errors if verification fails, indicating potential initialization issues.
     */
    if (VerifyGdt.Base != CpuData->GdtPtr.Base)
        PError("CPU %u: GDT verification failed!\n", __CpuNumber__);

    if (VerifyIdt.Base != CpuData->IdtPtr.Base)
        PError("CPU %u: IDT verification failed!\n", __CpuNumber__);

    if (VerifyTr != TssSelector)
        PError("CPU %u: TSS verification failed!\n", __CpuNumber__);

    /*
     * Log successful initialization of per-CPU interrupt handling.
     */
    PSuccess("CPU %u: Per-CPU interrupt handling initialized\n", __CpuNumber__);
}

/*
 * GetPerCpuData - Retrieve Per-CPU Data Structure
 *
 * Returns a pointer to the per-CPU data structure for the specified CPU.
 * This allows other kernel components to access CPU-specific information
 * such as descriptor tables, APIC base, and local counters.
 *
 * Parameters:
 * - __CpuNumber__: The logical CPU number whose data is requested.
 *
 * Returns:
 * - Pointer to the PerCpuData structure for the specified CPU.
 *
 * Note: This function assumes the CPU number is valid (less than MaxCPUs).
 * Invalid CPU numbers may return pointers to undefined memory locations.
 */
PerCpuData*
GetPerCpuData(uint32_t __CpuNumber__)
{
    return &CpuDataArray[__CpuNumber__];
}
