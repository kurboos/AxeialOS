#pragma once

#include <Errnos.h>
#include <IDT.h>

typedef struct
{

    GdtEntry         Gdt[MaxGdt]; /* GDT*/
    GdtPointer       GdtPtr;
    IdtEntry         Idt[MaxIdt]; /* IDT*/
    IdtPointer       IdtPtr;
    TaskStateSegment Tss;        /* TSS*/
    uint64_t         StackTop;   /* Stack*/
    uint64_t         ApicBase;   /* APIC Base*/
    uint64_t         LocalTicks; /* Timer Data*/
    uint32_t         LocalInterrupts;

} PerCpuData;