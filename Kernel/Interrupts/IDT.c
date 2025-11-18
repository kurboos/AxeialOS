#include <IDT.h>

/** @brief Globals */
IdtEntry IdtEntries[256];

IdtPointer IdtPtr;

/** @brief Exception Labels */
const char* ExceptionNames[32] = {"Division Error",
                                  "Debug Exception",
                                  "Non-Maskable Interrupt",
                                  "Breakpoint",
                                  "Overflow",
                                  "Bound Range Exceeded",
                                  "Invalid Opcode",
                                  "Device Not Available",
                                  "Double Fault",
                                  "Coprocessor Segment Overrun",
                                  "Invalid TSS",
                                  "Segment Not Present",
                                  "Stack Fault",
                                  "General Protection Fault",
                                  "Page Fault",
                                  "Reserved",
                                  "x87 FPU Error",
                                  "Alignment Check",
                                  "Machine Check",
                                  "SIMD Floating-Point Exception"};

/**
 * @brief Configure an entry in the Interrupt Descriptor Table (IDT).
 *
 * @details Sets up an IDT entry with the given handler address, code segment selector,
 *			and type/attributes. Splits the 64-bit handler address into low, mid, and high
 *			parts across the IDT entry fields.
 *
 * @param __Index__   Index of the IDT entry (0–255).
 * @param __Handler__ Address of the interrupt handler function.
 * @param __Selector__ Code segment selector for the handler.
 * @param __Flags__   Type and attribute flags (e.g., interrupt gate).
 *
 * @return void
 */
void
SetIdtEntry(int __Index__, uint64_t __Handler__, uint16_t __Selector__, uint8_t __Flags__)
{
    IdtEntries[__Index__].OffsetLow  = __Handler__ & 0xFFFF;
    IdtEntries[__Index__].Selector   = __Selector__;
    IdtEntries[__Index__].Ist        = 0;
    IdtEntries[__Index__].TypeAttr   = __Flags__;
    IdtEntries[__Index__].OffsetMid  = (__Handler__ >> 16) & 0xFFFF;
    IdtEntries[__Index__].OffsetHigh = (__Handler__ >> 32) & 0xFFFFFFFF;
    IdtEntries[__Index__].Reserved   = 0;
}

/**
 * @brief Initialize the legacy Programmable Interrupt Controller (PIC).
 *
 * @details Programs the PIC with initialization control words (ICWs):
 * 			ICW1: Start initialization sequence.
 * 			ICW2: Remap IRQs to vectors 32–47.
 * 			ICW3: Configure master/slave cascade.
 * 			ICW4: Set 8086/88 mode.
 * 			Finally, masks all IRQs since APIC is used instead of PIC.
 *
 * @return void
 *
 * @note Provides compatibility for legacy hardware.
 */
void
InitializePic(void)
{
    /*ICW1: Initialize PIC - Send initialization command to start setup sequence*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw1Init), "Nd"((uint16_t)PicMasterCommand));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw1Init), "Nd"((uint16_t)PicSlaveCommand));

    /*ICW2: Remap IRQs - Move IRQ vectors from 0-15 to 32-47 to avoid CPU exception conflicts*/
    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)PicIcw2MasterBase), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)PicIcw2SlaveBase), "Nd"((uint16_t)PicSlaveData));

    /*ICW3: Setup cascade - Configure master PIC to communicate with slave PIC on IRQ2*/
    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)PicIcw3MasterCascade), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)PicIcw3SlaveCascade), "Nd"((uint16_t)PicSlaveData));

    /*ICW4: x86 mode - Set 8086/88 mode and other operational parameters*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw4Mode), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw4Mode), "Nd"((uint16_t)PicSlaveData));

    /*Mask ALL IRQs - Disable all interrupts since we're using APIC instead of PIC*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicMaskAll), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicMaskAll), "Nd"((uint16_t)PicSlaveData));

    PDebug("PIC initialized (all IRQs masked)\n");
}

/**
 * @brief Initialize the Interrupt Descriptor Table (IDT).
 *
 * @details Clears all IDT entries.
 *			Sets up ISR stubs for CPU exceptions (vectors 0–19).
 *			Maps IRQ handlers for hardware interrupts (vectors 32–47).
 *			Initializes the PIC for compatibility.
 *			Loads the IDT into the CPU with `lidt`.
 *			Enables interrupts globally with `sti`.
 *
 * @return void
 *
 * @note Must be called during kernel initialization before handling interrupts.
 */
void
InitializeIdt(void)
{
    PInfo("Initializing IDT ...\n");

    /*Set up IDT pointer with base address and limit*/
    IdtPtr.Limit = sizeof(IdtEntries) - 1;
    IdtPtr.Base  = (uint64_t)&IdtEntries;

    /*Clear all IDT entries to prevent undefined behavior*/
    for (int Index = 0; Index < IdtMaxEntries; Index++)
    {
        SetIdtEntry(Index, 0, 0, 0);
    }

    /*Set up Interrupt Service Routines for CPU exceptions (vectors 0-19)*/
    for (int Index = 0; Index < IdtMaxIsrEntries; Index++)
    {
        uint64_t HandlerAddr = 0;
        /*Map each exception vector to its corresponding ISR stub*/
        switch (Index)
        {
            case 0:
                HandlerAddr = (uint64_t)Isr0;
                break; // Division Error
            case 1:
                HandlerAddr = (uint64_t)Isr1;
                break; // Debug Exception
            case 2:
                HandlerAddr = (uint64_t)Isr2;
                break; // Non-Maskable Interrupt
            case 3:
                HandlerAddr = (uint64_t)Isr3;
                break; // Breakpoint
            case 4:
                HandlerAddr = (uint64_t)Isr4;
                break; // Overflow
            case 5:
                HandlerAddr = (uint64_t)Isr5;
                break; // Bound Range Exceeded
            case 6:
                HandlerAddr = (uint64_t)Isr6;
                break; // Invalid Opcode
            case 7:
                HandlerAddr = (uint64_t)Isr7;
                break; // Device Not Available
            case 8:
                HandlerAddr = (uint64_t)Isr8;
                break; // Double Fault
            case 9:
                HandlerAddr = (uint64_t)Isr9;
                break; // Coprocessor Segment Overrun
            case 10:
                HandlerAddr = (uint64_t)Isr10;
                break; // Invalid TSS
            case 11:
                HandlerAddr = (uint64_t)Isr11;
                break; // Segment Not Present
            case 12:
                HandlerAddr = (uint64_t)Isr12;
                break; // Stack Fault
            case 13:
                HandlerAddr = (uint64_t)Isr13;
                break; // General Protection Fault
            case 14:
                HandlerAddr = (uint64_t)Isr14;
                break; // Page Fault
            case 15:
                HandlerAddr = (uint64_t)Isr15;
                break; // Reserved
            case 16:
                HandlerAddr = (uint64_t)Isr16;
                break; // x87 FPU Error
            case 17:
                HandlerAddr = (uint64_t)Isr17;
                break; // Alignment Check
            case 18:
                HandlerAddr = (uint64_t)Isr18;
                break; // Machine Check
            case 19:
                HandlerAddr = (uint64_t)Isr19;
                break; // SIMD Floating-Point Exception
        }
        /*Configure IDT entry as interrupt gate with kernel code segment*/
        SetIdtEntry(Index, HandlerAddr, KernelCodeSelector, IdtTypeInterruptGate);
    }

    /*Set up Interrupt Request handlers for hardware interrupts (vectors 32-47)*/
    uint64_t IrqHandlers[] = {(uint64_t)Irq0,
                              (uint64_t)Irq1,
                              (uint64_t)Irq2,
                              (uint64_t)Irq3,
                              (uint64_t)Irq4,
                              (uint64_t)Irq5,
                              (uint64_t)Irq6,
                              (uint64_t)Irq7,
                              (uint64_t)Irq8,
                              (uint64_t)Irq9,
                              (uint64_t)Irq10,
                              (uint64_t)Irq11,
                              (uint64_t)Irq12,
                              (uint64_t)Irq13,
                              (uint64_t)Irq14,
                              (uint64_t)Irq15};

    /*Map each IRQ handler to its corresponding vector (32-47)*/
    for (int Index = 0; Index < 16; Index++)
    {
        SetIdtEntry(
            IdtIrqBase + Index, IrqHandlers[Index], KernelCodeSelector, IdtTypeInterruptGate);
    }

    /*Initialize legacy PIC for compatibility (though we use APIC)*/
    InitializePic();

    /*Load the IDT into the CPU's IDTR register*/
    __asm__ volatile("lidt %0" : : "m"(IdtPtr));

    /*Enable CPU interrupts globally*/
    __asm__ volatile("sti");

    PSuccess("IDT init... OK\n");
}

/**
 * @brief Dump memory contents in hex format.
 *
 * @details Prints a hex dump of the specified memory region, 16 bytes per line.
 *
 * @param __Address__ Starting address to dump.
 * @param __Bytes__   Number of bytes to display.
 *
 * @return void
 */
void
DumpMemory(uint64_t __Address__, int __Bytes__)
{
    KrnPrintf("Memory dump at 0x%lx:\n", __Address__);
    for (int Iteration = 0; Iteration < __Bytes__; Iteration += 16)
    {
        KrnPrintf("0x%lx: ", __Address__ + Iteration);
        for (int J = 0; J < 16 && (Iteration + J) < __Bytes__; J++)
        {
            uint8_t* ptr = (uint8_t*)(__Address__ + Iteration + J);
            KrnPrintf("%02x ", *ptr);
        }
        KrnPrintf("\n");
    }
}

/**
 * @brief Dump instruction bytes at a given RIP.
 *
 * @details Prints the first 16 bytes of machine code at the given instruction pointer.
 *
 * @param __Rip__ Instruction pointer address.
 *
 * @return void
 */
void
DumpInstruction(uint64_t __Rip__)
{
    KrnPrintf("Instruction bytes at RIP (0x%lx):\n", __Rip__);
    uint8_t* instr = (uint8_t*)__Rip__;
    KrnPrintf("0x%lx: ", __Rip__);
    for (int Iteration = 0; Iteration < 16; Iteration++)
    {
        KrnPrintf("%02x ", instr[Iteration]);
    }
    KrnPrintf("\n");
}

/**
 * @brief Dump CPU control registers.
 *
 * @details Reads CR0, CR2, CR3, and CR4 using inline assembly and prints their values.
 *
 * @return void
 */
void
DumpControlRegisters(void)
{
    uint64_t CR0, CR2, CR3, CR4;

    /*Read control registers using inline assembly*/
    __asm__ volatile("movq %%cr0, %0" : "=r"(CR0));
    __asm__ volatile("movq %%cr2, %0" : "=r"(CR2));
    __asm__ volatile("movq %%cr3, %0" : "=r"(CR3));
    __asm__ volatile("movq %%cr4, %0" : "=r"(CR4));

    KrnPrintf("Control Registers:\n");
    KrnPrintf("  CR0: 0x%016lx  CR2: 0x%016lx\n", CR0, CR2);
    KrnPrintf("  CR3: 0x%016lx  CR4: 0x%016lx\n", CR3, CR4);
}

/**
 * @brief ISR stub for CPU exceptions.
 *
 * @details Pushes dummy error code and vector number onto the stack,
 *			then jumps to the common ISR handler stub.
 *
 * @param num Exception vector number.
 */
#define ISR_STUB(num)                                                                              \
    void Isr##num(void)                                                                            \
    {                                                                                              \
        __asm__ volatile("pushq $0\n\t"                                                            \
                         "pushq $" #num "\n\t"                                                     \
                         "jmp IsrCommonStub\n\t");                                                 \
    }

/**
 * @brief ISR stub for exceptions with error codes.
 *
 * @details	Pushes the vector number (with error code already on stack),
 * 			then jumps to the common ISR handler stub.
 *
 * @param num Exception vector number.
 */
#define ISR_STUB_ERR(num)                                                                          \
    void Isr##num(void)                                                                            \
    {                                                                                              \
        __asm__ volatile("pushq $" #num "\n\t"                                                     \
                         "jmp IsrCommonStub\n\t");                                                 \
    }

/**
 * @brief IRQ stub for hardware interrupts.
 *
 * @details Pushes dummy error code and interrupt vector number,
 * 			then jumps to the common IRQ handler stub.
 *
 * @param num      IRQ number (0–15).
 * @param int_num  Interrupt vector number (32–47).
 */
#define IRQ_STUB(num, int_num)                                                                     \
    void Irq##num(void)                                                                            \
    {                                                                                              \
        __asm__ volatile("pushq $0\n\t"                                                            \
                         "pushq $" #int_num "\n\t"                                                 \
                         "jmp IrqCommonStub\n\t");                                                 \
    }

/*Generate ISR stubs for CPU exceptions 0-19*/
ISR_STUB(0)
ISR_STUB(1)
ISR_STUB(2)
ISR_STUB(3)
ISR_STUB(4) ISR_STUB(5) ISR_STUB(6) ISR_STUB(7) ISR_STUB_ERR(8) ISR_STUB(9) ISR_STUB_ERR(10)
    ISR_STUB_ERR(11) ISR_STUB_ERR(12) ISR_STUB_ERR(13) ISR_STUB_ERR(14) ISR_STUB(15) ISR_STUB(16)
        ISR_STUB(17) ISR_STUB(18) ISR_STUB(19)

    /*Generate IRQ stubs for hardware interrupts 32-47*/
    IRQ_STUB(0, 32) IRQ_STUB(1, 33) IRQ_STUB(2, 34) IRQ_STUB(3, 35) IRQ_STUB(4, 36) IRQ_STUB(5, 37)
        IRQ_STUB(6, 38) IRQ_STUB(7, 39) IRQ_STUB(8, 40) IRQ_STUB(9, 41) IRQ_STUB(10, 42)
            IRQ_STUB(11, 43) IRQ_STUB(12, 44) IRQ_STUB(13, 45) IRQ_STUB(14, 46) IRQ_STUB(15, 47)

                __asm__("IsrCommonStub:\n\t"
                        "pushq %rax\n\t" /*Save general-purpose registers*/
                        "pushq %rbx\n\t"
                        "pushq %rcx\n\t"
                        "pushq %rdx\n\t"
                        "pushq %rsi\n\t"
                        "pushq %rdi\n\t"
                        "pushq %rbp\n\t"
                        "pushq %r8\n\t" /*Save extended registers*/
                        "pushq %r9\n\t"
                        "pushq %r10\n\t"
                        "pushq %r11\n\t"
                        "pushq %r12\n\t"
                        "pushq %r13\n\t"
                        "pushq %r14\n\t"
                        "pushq %r15\n\t"
                        "movq %rsp, %rdi\n\t" /*Pass stack pointer as argument to C handler*/
                        "call IsrHandler\n\t" /*Call the C exception handler*/
                        "popq %r15\n\t"       /*Restore all registers in reverse order*/
                        "popq %r14\n\t"
                        "popq %r13\n\t"
                        "popq %r12\n\t"
                        "popq %r11\n\t"
                        "popq %r10\n\t"
                        "popq %r9\n\t"
                        "popq %r8\n\t"
                        "popq %rbp\n\t"
                        "popq %rdi\n\t"
                        "popq %rsi\n\t"
                        "popq %rdx\n\t"
                        "popq %rcx\n\t"
                        "popq %rbx\n\t"
                        "popq %rax\n\t"
                        "addq $16, %rsp\n\t" /*Remove error code and vector number from stack*/
                        "iretq\n\t"          /*Return from interrupt*/
                );

__asm__("IrqCommonStub:\n\t"
        "pushq %rax\n\t" /*Save general-purpose registers*/
        "pushq %rbx\n\t"
        "pushq %rcx\n\t"
        "pushq %rdx\n\t"
        "pushq %rsi\n\t"
        "pushq %rdi\n\t"
        "pushq %rbp\n\t"
        "pushq %r8\n\t" /*Save extended registers*/
        "pushq %r9\n\t"
        "pushq %r10\n\t"
        "pushq %r11\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "movq %rsp, %rdi\n\t" /*Pass stack pointer as argument to C handler*/
        "call IrqHandler\n\t" /*Call the C interrupt handler*/
        "popq %r15\n\t"       /*Restore all registers in reverse order*/
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %r11\n\t"
        "popq %r10\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rbp\n\t"
        "popq %rdi\n\t"
        "popq %rsi\n\t"
        "popq %rdx\n\t"
        "popq %rcx\n\t"
        "popq %rbx\n\t"
        "popq %rax\n\t"
        "addq $16, %rsp\n\t" /*Remove dummy error code and vector number*/
        "iretq\n\t"          /*Return from interrupt*/
);
