#include <IDT.h>

/*
 * Global IDT Entries Array
 *
 * This array holds all 256 IDT entries. Each entry describes an interrupt gate
 * that points to the appropriate handler function. The array is initialized
 * during IDT setup and loaded into the CPU's IDTR register.
 *
 * Index 0-31: CPU exceptions (Division Error, Page Fault, etc.)
 * Index 32-255: Hardware interrupts (IRQs) and software interrupts
 */
IdtEntry
IdtEntries[256];

/*
 * IDT Pointer Structure
 *
 * This structure contains the base address and limit of the IDT.
 * It is used with the LIDT instruction to load the IDT into the CPU.
 * The limit is set to the size of the IDT entries array minus 1.
 */
IdtPointer
IdtPtr;

/*
 * Exception Names Array
 *
 * An array of strings containing human-readable names for CPU exceptions.
 * These are used for debugging and error reporting when exceptions occur.
 * The array corresponds to exception vectors 0-31 as defined by the x86-64 architecture.
 *
 * Note: Some entries are marked as "Reserved" for future use or are not implemented
 * in current processor generations.
 */
const char
*ExceptionNames[32]=
{
    "Division Error", "Debug Exception", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception"
};

/*
 * SetIdtEntry - Configure an IDT Entry
 *
 * This function sets up a single entry in the Interrupt Descriptor Table.
 * Each IDT entry is an interrupt gate descriptor that contains information
 * about how to handle a specific interrupt or exception.
 *
 * Parameters:
 *   __Index__   - The index in the IDT (0-255) where this entry will be placed
 *   __Handler__ - The 64-bit address of the interrupt handler function
 *   __Selector__ - The code segment selector for the handler (usually kernel code segment)
 *   __Flags__   - Type and attributes flags (e.g., interrupt gate, privilege level)
 *
 * The handler address is split into three parts to fit the x86-64 IDT entry format:
 * - OffsetLow:  Bits 0-15 of the address
 * - OffsetMid:  Bits 16-31 of the address
 * - OffsetHigh: Bits 32-63 of the address
 *
 * IST (Interrupt Stack Table) is set to 0, using the current stack.
 * Reserved field is set to 0 as required by the architecture.
 */
void
SetIdtEntry(int __Index__, uint64_t __Handler__, uint16_t __Selector__, uint8_t __Flags__)
{
    IdtEntries[__Index__].OffsetLow = __Handler__ & 0xFFFF;
    IdtEntries[__Index__].Selector = __Selector__;
    IdtEntries[__Index__].Ist = 0;
    IdtEntries[__Index__].TypeAttr = __Flags__;
    IdtEntries[__Index__].OffsetMid = (__Handler__ >> 16) & 0xFFFF;
    IdtEntries[__Index__].OffsetHigh = (__Handler__ >> 32) & 0xFFFFFFFF;
    IdtEntries[__Index__].Reserved = 0;
}

/*
 * InitializePic - Initialize the Programmable Interrupt Controller
 *
 * This function initializes the legacy 8259 PIC (Programmable Interrupt Controller).
 * The PIC is responsible for handling hardware interrupts from devices like keyboards,
 * timers, and serial ports. In modern systems, the PIC is often replaced by the APIC,
 * but initialization is still required for compatibility.
 *
 * The initialization follows the standard PIC programming sequence:
 * 1. ICW1: Send initialization command to both master and slave PICs
 * 2. ICW2: Remap IRQ vectors to avoid conflicts with CPU exceptions (0-31)
 * 3. ICW3: Configure master-slave relationship (cascade mode)
 * 4. ICW4: Set x86 mode and other operational parameters
 *
 * After initialization, all IRQs are masked because the system uses APIC instead.
 * The PIC is kept initialized but disabled to prevent spurious interrupts.
 *
 * Note: This is legacy PIC initialization. Modern systems use APIC (Advanced
 * Programmable Interrupt Controller) for interrupt handling, but PIC setup
 * is still required for backward compatibility.
 */
void
InitializePic(void)
{
    /*ICW1: Initialize PIC - Send initialization command to start setup sequence*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw1Init), "Nd"((uint16_t)PicMasterCommand));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw1Init), "Nd"((uint16_t)PicSlaveCommand));

    /*ICW2: Remap IRQs - Move IRQ vectors from 0-15 to 32-47 to avoid CPU exception conflicts*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw2MasterBase), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw2SlaveBase), "Nd"((uint16_t)PicSlaveData));

    /*ICW3: Setup cascade - Configure master PIC to communicate with slave PIC on IRQ2*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw3MasterCascade), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw3SlaveCascade), "Nd"((uint16_t)PicSlaveData));

    /*ICW4: x86 mode - Set 8086/88 mode and other operational parameters*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw4Mode), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicIcw4Mode), "Nd"((uint16_t)PicSlaveData));

    /*Mask ALL IRQs - Disable all interrupts since we're using APIC instead of PIC*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicMaskAll), "Nd"((uint16_t)PicMasterData));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PicMaskAll), "Nd"((uint16_t)PicSlaveData));

    PDebug("PIC initialized (all IRQs masked)\n");
}

/*
 * InitializeIdt - Initialize the Interrupt Descriptor Table
 *
 * This function sets up the entire IDT for the system. It performs the following steps:
 * 1. Initializes the IDT pointer structure with the correct base address and limit
 * 2. Clears all IDT entries to ensure no garbage data
 * 3. Sets up ISR (Interrupt Service Routine) entries for CPU exceptions (vectors 0-19)
 * 4. Sets up IRQ entries for hardware interrupts (vectors 32-47)
 * 5. Initializes the PIC (Programmable Interrupt Controller)
 * 6. Loads the IDT into the CPU using the LIDT instruction
 * 7. Enables interrupts with the STI instruction
 *
 * The IDT is crucial for handling both software-generated exceptions (like divide-by-zero)
 * and hardware-generated interrupts (like keyboard input). Each entry points to a
 * specific handler function that will be called when the corresponding interrupt occurs.
 *
 * Parameters: None
 * Returns: None
 *
 * Side effects:
 * - Modifies global IdtPtr structure
 * - Modifies IdtEntries array
 * - Enables CPU interrupts
 * - Initializes PIC
 */
void
InitializeIdt(void)
{
    PInfo("Initializing IDT ...\n");

    /*Set up IDT pointer with base address and limit*/
    IdtPtr.Limit = sizeof(IdtEntries) - 1;
    IdtPtr.Base = (uint64_t)&IdtEntries;

    /*Clear all IDT entries to prevent undefined behavior*/
    for (int Index = 0; Index < IdtMaxEntries; Index++)
        SetIdtEntry(Index, 0, 0, 0);

    /*Set up Interrupt Service Routines for CPU exceptions (vectors 0-19)*/
    for (int Index = 0; Index < IdtMaxIsrEntries; Index++)
    {
        uint64_t HandlerAddr = 0;
        /*Map each exception vector to its corresponding ISR stub*/
        switch (Index)
        {
            case 0:  HandlerAddr = (uint64_t)Isr0;  break;  // Division Error
            case 1:  HandlerAddr = (uint64_t)Isr1;  break;  // Debug Exception
            case 2:  HandlerAddr = (uint64_t)Isr2;  break;  // Non-Maskable Interrupt
            case 3:  HandlerAddr = (uint64_t)Isr3;  break;  // Breakpoint
            case 4:  HandlerAddr = (uint64_t)Isr4;  break;  // Overflow
            case 5:  HandlerAddr = (uint64_t)Isr5;  break;  // Bound Range Exceeded
            case 6:  HandlerAddr = (uint64_t)Isr6;  break;  // Invalid Opcode
            case 7:  HandlerAddr = (uint64_t)Isr7;  break;  // Device Not Available
            case 8:  HandlerAddr = (uint64_t)Isr8;  break;  // Double Fault
            case 9:  HandlerAddr = (uint64_t)Isr9;  break;  // Coprocessor Segment Overrun
            case 10: HandlerAddr = (uint64_t)Isr10; break;  // Invalid TSS
            case 11: HandlerAddr = (uint64_t)Isr11; break;  // Segment Not Present
            case 12: HandlerAddr = (uint64_t)Isr12; break;  // Stack Fault
            case 13: HandlerAddr = (uint64_t)Isr13; break;  // General Protection Fault
            case 14: HandlerAddr = (uint64_t)Isr14; break;  // Page Fault
            case 15: HandlerAddr = (uint64_t)Isr15; break;  // Reserved
            case 16: HandlerAddr = (uint64_t)Isr16; break;  // x87 FPU Error
            case 17: HandlerAddr = (uint64_t)Isr17; break;  // Alignment Check
            case 18: HandlerAddr = (uint64_t)Isr18; break;  // Machine Check
            case 19: HandlerAddr = (uint64_t)Isr19; break;  // SIMD Floating-Point Exception
        }
        /*Configure IDT entry as interrupt gate with kernel code segment*/
        SetIdtEntry(Index, HandlerAddr, KernelCodeSelector, IdtTypeInterruptGate);
    }

    /*Set up Interrupt Request handlers for hardware interrupts (vectors 32-47)*/
    uint64_t IrqHandlers[] = {
        (uint64_t)Irq0,  (uint64_t)Irq1,  (uint64_t)Irq2,  (uint64_t)Irq3,
        (uint64_t)Irq4,  (uint64_t)Irq5,  (uint64_t)Irq6,  (uint64_t)Irq7,
        (uint64_t)Irq8,  (uint64_t)Irq9,  (uint64_t)Irq10, (uint64_t)Irq11,
        (uint64_t)Irq12, (uint64_t)Irq13, (uint64_t)Irq14, (uint64_t)Irq15
    };

    /*Map each IRQ handler to its corresponding vector (32-47)*/
    for (int Index = 0; Index < 16; Index++)
        SetIdtEntry(IdtIrqBase + Index, IrqHandlers[Index], KernelCodeSelector, IdtTypeInterruptGate);

    /*Initialize legacy PIC for compatibility (though we use APIC)*/
    InitializePic();

    /*Load the IDT into the CPU's IDTR register*/
    __asm__ volatile("lidt %0" : : "m"(IdtPtr));

    /*Enable CPU interrupts globally*/
    __asm__ volatile("sti");

    PSuccess("IDT init... OK\n");
}

/*
 * Exception Debugging and Diagnostic Functions
 *
 * These functions provide detailed diagnostic information when CPU exceptions occur.
 * They are used by the ISR handler to dump relevant system state for debugging purposes.
 * The information includes memory contents, instruction bytes, and CPU control registers.
 */

/*
 * DumpMemory - Display memory contents at a specific address
 *
 * This function dumps a specified number of bytes from memory starting at the given address.
 * It's useful for examining memory contents around fault addresses during debugging.
 *
 * Parameters:
 *   __Address__ - The starting memory address to dump
 *   __Bytes__   - The number of bytes to display
 *
 * The output shows 16 bytes per line in hexadecimal format, with the address
 * of each line displayed at the beginning.
 */
void
DumpMemory(uint64_t __Address__, int __Bytes__)
{
    KrnPrintf("Memory dump at 0x%lx:\n", __Address__);
    for (int Iteration = 0; Iteration < __Bytes__; Iteration += 16)
    {
        KrnPrintf("0x%lx: ", __Address__ + Iteration);
        for (int j = 0; j < 16 && (Iteration + j) < __Bytes__; j++)
        {
            uint8_t *ptr = (uint8_t*)(__Address__ + Iteration + j);
            KrnPrintf("%02x ", *ptr);
        }
        KrnPrintf("\n");
    }
}

/*
 * DumpInstruction - Display instruction bytes at a specific RIP
 *
 * This function shows the raw instruction bytes at the instruction pointer (RIP)
 * where an exception occurred. This helps in understanding what instruction
 * caused the fault.
 *
 * Parameters:
 *   __Rip__ - The instruction pointer address to dump bytes from
 *
 * Displays the next 16 bytes starting from the RIP address in hexadecimal format.
 */
void
DumpInstruction(uint64_t __Rip__)
{
    KrnPrintf("Instruction bytes at RIP (0x%lx):\n", __Rip__);
    uint8_t *instr = (uint8_t*)__Rip__;
    KrnPrintf("0x%lx: ", __Rip__);
    for (int Iteration = 0; Iteration < 16; Iteration++)
    {
        KrnPrintf("%02x ", instr[Iteration]);
    }
    KrnPrintf("\n");
}

/*
 * DumpControlRegisters - Display CPU control register values
 *
 * This function reads and displays the values of the x86-64 control registers.
 * Control registers contain important CPU state information that can help
 * diagnose the cause of exceptions.
 *
 * CR0: Contains system control flags (paging, protection, etc.)
 * CR2: Contains the page fault address (valid during page faults)
 * CR3: Contains the page directory base address (for virtual memory)
 * CR4: Contains additional control flags (PAE, PSE, etc.)
 *
 * Parameters: None
 * Returns: None
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

/*
 * Interrupt and Exception Handler Stubs
 *
 * These macros and functions generate the low-level assembly stubs that handle
 * the transition from interrupt/exception context to C code. Each stub:
 * 1. Pushes a dummy error code (0) for exceptions that don't provide one
 * 2. Pushes the interrupt vector number
 * 3. Jumps to the common stub which saves registers and calls the C handler
 *
 * ISR stubs handle CPU exceptions (vectors 0-19)
 * IRQ stubs handle hardware interrupts (vectors 32-47)
 *
 * The distinction between ISR_STUB and ISR_STUB_ERR is that some exceptions
 * (like double fault, page fault) already push an error code, so ISR_STUB_ERR
 * doesn't push a dummy one.
 */

/*
 * ISR_STUB - Macro for generating Interrupt Service Routine stubs
 *
 * Creates a stub function for CPU exceptions that don't push an error code.
 * The stub pushes a dummy error code (0) and the vector number, then jumps
 * to the common ISR handler.
 */
#define ISR_STUB(num) \
    void Isr##num(void) { \
        __asm__ volatile( \
            "pushq $0\n\t" \
            "pushq $" #num "\n\t" \
            "jmp IsrCommonStub\n\t" \
        ); \
    }

/*
 * ISR_STUB_ERR - Macro for generating ISR stubs for exceptions with error codes
 *
 * Creates a stub function for CPU exceptions that already push an error code
 * on the stack. Only pushes the vector number, then jumps to common handler.
 */
#define ISR_STUB_ERR(num) \
    void Isr##num(void) { \
        __asm__ volatile( \
            "pushq $" #num "\n\t" \
            "jmp IsrCommonStub\n\t" \
        ); \
    }

/*
 * IRQ_STUB - Macro for generating Interrupt Request handler stubs
 *
 * Creates a stub function for hardware interrupts. Similar to ISR_STUB but
 * uses the IrqCommonStub instead of IsrCommonStub.
 */
#define IRQ_STUB(num, int_num) \
    void Irq##num(void) { \
        __asm__ volatile( \
            "pushq $0\n\t" \
            "pushq $" #int_num "\n\t" \
            "jmp IrqCommonStub\n\t" \
        ); \
    }

/*Generate ISR stubs for CPU exceptions 0-19*/
ISR_STUB(0)  ISR_STUB(1)  ISR_STUB(2)  ISR_STUB(3)
ISR_STUB(4)  ISR_STUB(5)  ISR_STUB(6)  ISR_STUB(7)
ISR_STUB_ERR(8)  ISR_STUB(9)  ISR_STUB_ERR(10) ISR_STUB_ERR(11)
ISR_STUB_ERR(12) ISR_STUB_ERR(13) ISR_STUB_ERR(14) ISR_STUB(15)
ISR_STUB(16) ISR_STUB(17) ISR_STUB(18) ISR_STUB(19)

/*Generate IRQ stubs for hardware interrupts 32-47*/
IRQ_STUB(0, 32)   IRQ_STUB(1, 33)   IRQ_STUB(2, 34)   IRQ_STUB(3, 35)
IRQ_STUB(4, 36)   IRQ_STUB(5, 37)   IRQ_STUB(6, 38)   IRQ_STUB(7, 39)
IRQ_STUB(8, 40)   IRQ_STUB(9, 41)   IRQ_STUB(10, 42)  IRQ_STUB(11, 43)
IRQ_STUB(12, 44)  IRQ_STUB(13, 45)  IRQ_STUB(14, 46)  IRQ_STUB(15, 47)

/*
 * Common Interrupt/Exception Handler Stubs
 *
 * These assembly functions provide the low-level context switching between
 * interrupt/exception mode and normal execution. They are responsible for:
 * 1. Saving all CPU registers on the stack
 * 2. Calling the appropriate C handler function
 * 3. Restoring all CPU registers
 * 4. Cleaning up the stack and returning from interrupt
 *
 * The stack layout when these stubs are called:
 *   [esp]     -> Error code (or dummy 0)
 *   [esp+8]   -> Interrupt vector number
 *   [esp+16]  -> RIP (return address)
 *   [esp+24]  -> CS (code segment)
 *   [esp+32]  -> RFLAGS
 *   [esp+40]  -> RSP (user stack, if applicable)
 *   [esp+48]  -> SS (stack segment, if applicable)
 */

/*
 * IsrCommonStub - Common stub for CPU exception handlers
 *
 * This assembly function handles the transition for CPU exceptions (ISRs).
 * It saves the complete CPU state, calls the C IsrHandler function,
 * then restores state and returns from the interrupt.
 */
__asm__(
    "IsrCommonStub:\n\t"
    "pushq %rax\n\t"      /*Save general-purpose registers*/
	"pushq %rbx\n\t"
	"pushq %rcx\n\t"
	"pushq %rdx\n\t"
    "pushq %rsi\n\t"
	"pushq %rdi\n\t"
	"pushq %rbp\n\t"
    "pushq %r8\n\t"       /*Save extended registers*/
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
    "addq $16, %rsp\n\t"  /*Remove error code and vector number from stack*/
    "iretq\n\t"           /*Return from interrupt*/
);

/*
 * IrqCommonStub - Common stub for hardware interrupt handlers
 *
 * This assembly function handles the transition for hardware interrupts (IRQs).
 * Similar to IsrCommonStub but calls the IrqHandler function instead.
 * Hardware interrupts typically don't have error codes, so the stack layout
 * is slightly different from exception handlers.
 */
__asm__(
    "IrqCommonStub:\n\t"
    "pushq %rax\n\t"      /*Save general-purpose registers*/
    "pushq %rbx\n\t"
    "pushq %rcx\n\t"
    "pushq %rdx\n\t"
    "pushq %rsi\n\t"
    "pushq %rdi\n\t"
    "pushq %rbp\n\t"
    "pushq %r8\n\t"       /*Save extended registers*/
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
    "addq $16, %rsp\n\t"  /*Remove dummy error code and vector number*/
    "iretq\n\t"           /*Return from interrupt*/
);
