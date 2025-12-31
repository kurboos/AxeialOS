#include <GDT.h>
#include <PerCPUData.h>
#include <SMP.h>
#include <SymAP.h>
#include <Timer.h>
#include <VMM.h>

PerCpuData CpuDataArray[MaxCPUs];

void
PerCpuInterruptInit(uint32_t __CpuNumber__, uint64_t __StackTop__, SysErr* __Err__)
{
    PerCpuData* CpuData = &CpuDataArray[__CpuNumber__];

    PDebug("CPU %u: Initializing per-CPU data at 0x%p\n", __CpuNumber__, CpuData);

    CpuData->StackTop = __StackTop__;

    for (int Index = 0; Index < MaxGdt; Index++)
    {
        CpuData->Gdt[Index] = GdtEntries[Index];
    }

    PDebug("CPU %u: Copied GDT template\n", __CpuNumber__);

    for (size_t Iteration = 0; Iteration < sizeof(TaskStateSegment); Iteration++)
    {
        ((uint8_t*)&CpuData->Tss)[Iteration] = 0;
    }

    CpuData->Tss.Rsp0      = __StackTop__;             /* Kernel stack pointer for ring 0 */
    CpuData->Tss.IoMapBase = sizeof(TaskStateSegment); /* I/O permission bitmap offset */

    PDebug("CPU %u: TSS initialized with Rsp0=0x%llx\n", __CpuNumber__, CpuData->Tss.Rsp0);

    uint64_t TssBase  = (uint64_t)&CpuData->Tss;
    uint32_t TssLimit = sizeof(TaskStateSegment) - 1;

    /* TSS descriptor low 8 bytes (GDT entry 5) */
    CpuData->Gdt[5].LimitLow    = TssLimit & 0xFFFF;
    CpuData->Gdt[5].BaseLow     = TssBase & 0xFFFF;
    CpuData->Gdt[5].BaseMiddle  = (TssBase >> 16) & 0xFF;
    CpuData->Gdt[5].Access      = 0x89; /* Present, 64-bit TSS */
    CpuData->Gdt[5].Granularity = (TssLimit >> 16) & 0x0F;
    CpuData->Gdt[5].BaseHigh    = (TssBase >> 24) & 0xFF;

    /* TSS descriptor high 8 bytes (GDT entry 6) */
    CpuData->Gdt[6].LimitLow    = (TssBase >> 32) & 0xFFFF;
    CpuData->Gdt[6].BaseLow     = (TssBase >> 48) & 0xFFFF;
    CpuData->Gdt[6].BaseMiddle  = 0;
    CpuData->Gdt[6].Access      = 0;
    CpuData->Gdt[6].Granularity = 0;
    CpuData->Gdt[6].BaseHigh    = 0;

    PDebug("CPU %u: GDT updated with TSS at 0x%llx\n", __CpuNumber__, TssBase);

    for (int Index = 0; Index < MaxIdt; Index++)
    {
        CpuData->Idt[Index] = IdtEntries[Index];
    }

    PDebug("CPU %u: Copied IDT template\n", __CpuNumber__);

    CpuData->GdtPtr.Limit = (sizeof(GdtEntry) * MaxGdt) - 1;
    CpuData->GdtPtr.Base  = (uint64_t)CpuData->Gdt;

    CpuData->IdtPtr.Limit = (sizeof(IdtEntry) * MaxIdt) - 1;
    CpuData->IdtPtr.Base  = (uint64_t)CpuData->Idt;

    CpuData->ApicBase = (uint64_t)PhysToVirt(ReadMsr(0x1B) & 0xFFFFF000);

    PDebug("CPU %u: APIC base = 0x%llx\n", __CpuNumber__, CpuData->ApicBase);

    CpuData->LocalTicks      = 0;
    CpuData->LocalInterrupts = 0;

    __asm__ volatile("lgdt %0" : : "m"(CpuData->GdtPtr) : "memory");

    __asm__ volatile("lidt %0" : : "m"(CpuData->IdtPtr) : "memory");

    __asm__ volatile("pushq $0x08\n\t"           /* Push kernel code segment selector */
                     "leaq 1f(%%rip), %%rax\n\t" /* Load address of local label */
                     "pushq %%rax\n\t"           /* Push return address */
                     "lretq\n\t"                 /* Far return to reload CS */
                     "1:\n\t"                    /* Local label for return target */
                     :
                     :
                     : "rax", "memory");

    __asm__ volatile("mov $0x10, %%ax\n\t" /* Kernel data segment selector */
                     "mov %%ax, %%ds\n\t"  /* Reload DS */
                     "mov %%ax, %%es\n\t"  /* Reload ES */
                     "mov %%ax, %%fs\n\t"  /* Reload FS */
                     "mov %%ax, %%gs\n\t"  /* Reload GS */
                     "mov %%ax, %%ss\n\t"  /* Reload SS */
                     :
                     :
                     : "ax", "memory");

    __asm__ volatile("ltr %0" : : "r"((uint16_t)TssSelector) : "memory");

    GdtPointer VerifyGdt;
    IdtPointer VerifyIdt;
    uint16_t   VerifyTr;

    __asm__ volatile("sgdt %0" : "=m"(VerifyGdt)); /* Store current GDT pointer */
    __asm__ volatile("sidt %0" : "=m"(VerifyIdt)); /* Store current IDT pointer */
    __asm__ volatile("str %0" : "=r"(VerifyTr));   /* Store current TR value */

    PDebug("CPU %u: Verification for Tables:\n", __CpuNumber__);
    PDebug("  GDT: Expected=0x%llx, Actual=0x%llx\n", CpuData->GdtPtr.Base, VerifyGdt.Base);
    PDebug("  IDT: Expected=0x%llx, Actual=0x%llx\n", CpuData->IdtPtr.Base, VerifyIdt.Base);
    PDebug("  TSS: Expected=0x%x, Actual=0x%x\n", TssSelector, VerifyTr);

    if (VerifyGdt.Base != CpuData->GdtPtr.Base)
    {
        PError("CPU %u: GDT verification failed!\n", __CpuNumber__);
    }

    if (VerifyIdt.Base != CpuData->IdtPtr.Base)
    {
        PError("CPU %u: IDT verification failed!\n", __CpuNumber__);
    }

    if (VerifyTr != TssSelector)
    {
        PError("CPU %u: TSS verification failed!\n", __CpuNumber__);
    }

    PSuccess("CPU %u: Per-CPU interrupt handling active\n", __CpuNumber__);
}

PerCpuData*
GetPerCpuData(uint32_t __CpuNumber__)
{
    return &CpuDataArray[__CpuNumber__];
}
