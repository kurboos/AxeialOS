#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <GDT.h>
#include <KrnPrintf.h>

#define PicMasterCommand 0x20
#define PicMasterData    0x21
#define PicSlaveCommand  0xA0
#define PicSlaveData     0xA1

#define PicIcw1Init          0x11
#define PicIcw2MasterBase    0x20
#define PicIcw2SlaveBase     0x28
#define PicIcw3MasterCascade 0x04
#define PicIcw3SlaveCascade  0x02
#define PicIcw4Mode          0x01
#define PicEoi               0x20

#define PicMaskAllExceptTimer 0xFE
#define PicMaskAll            0xFF

#define IdtMaxEntries    256
#define IdtIrqBase       32
#define IdtMaxIsrEntries 20

#define RflagsCarryFlag     0
#define RflagsParityFlag    2
#define RflagsAuxFlag       4
#define RflagsZeroFlag      6
#define RflagsSignFlag      7
#define RflagsTrapFlag      8
#define RflagsInterruptFlag 9
#define RflagsDirectionFlag 10
#define RflagsOverflowFlag  11

#define MaxIdt 256

typedef struct
{
    uint16_t OffsetLow;
    uint16_t Selector;
    uint8_t  Ist;
    uint8_t  TypeAttr;
    uint16_t OffsetMid;
    uint32_t OffsetHigh;
    uint32_t Reserved;
} __attribute__((packed)) IdtEntry;

typedef struct
{
    uint16_t Limit;
    uint64_t Base;
} __attribute__((packed)) IdtPointer;

typedef struct
{
    uint64_t Rax, Rbx, Rcx, Rdx, Rsi, Rdi, Rbp;    /* highest address */
    uint64_t R8, R9, R10, R11, R12, R13, R14, R15; /* lowest address */
    uint64_t IntNo;                                /* Stub */
    uint64_t ErrCode;                              /* Stub */
    uint64_t Rip;                                  /* CPU */
    uint64_t Cs;                                   /* CPU */
    uint64_t Rflags;                               /* CPU */
    uint64_t Rsp;                                  /* CPU */
    uint64_t Ss;                                   /* CPU */
} __attribute__((packed)) InterruptFrame;

extern IdtEntry    IdtEntries[IdtMaxEntries];
extern IdtPointer  IdtPtr;
extern const char* ExceptionNames[32];

#define IdtTypeInterruptGate 0x8E
#define IdtTypeTrapGate      0x8F

void SetIdtEntry(
    int __Index__, uint64_t __Handler__, uint16_t __Selector__, uint8_t __Flags__, SysErr* __Err__);
void InitializePic(SysErr* __Err__);
void InitializeIdt(SysErr* __Err__);
void IsrHandler(InterruptFrame* __Frame__);
void IrqHandler(InterruptFrame* __Frame__);

/*Helper*/
void DumpControlRegisters(SysErr* __Err__);
void DumpInstruction(uint64_t __Rip__, SysErr* __Err__);
void DumpMemory(uint64_t __Address__, int __Bytes__, SysErr* __Err__);

extern void Isr0(void);
extern void Isr1(void);
extern void Isr2(void);
extern void Isr3(void);
extern void Isr4(void);
extern void Isr5(void);
extern void Isr6(void);
extern void Isr7(void);
extern void Isr8(void);
extern void Isr9(void);
extern void Isr10(void);
extern void Isr11(void);
extern void Isr12(void);
extern void Isr13(void);
extern void Isr14(void);
extern void Isr15(void);
extern void Isr16(void);
extern void Isr17(void);
extern void Isr18(void);
extern void Isr19(void);

extern void Irq0(void);
extern void Irq1(void);
extern void Irq2(void);
extern void Irq3(void);
extern void Irq4(void);
extern void Irq5(void);
extern void Irq6(void);
extern void Irq7(void);
extern void Irq8(void);
extern void Irq9(void);
extern void Irq10(void);
extern void Irq11(void);
extern void Irq12(void);
extern void Irq13(void);
extern void Irq14(void);
extern void Irq15(void);

KEXPORT(SetIdtEntry);