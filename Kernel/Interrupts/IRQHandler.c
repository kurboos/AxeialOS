#include <IDT.h>
#include <Timer.h>

/*
 * IrqHandler - Main Hardware Interrupt Dispatcher
 *
 * This function is called by the common IRQ stub (IrqCommonStub) whenever a hardware
 * interrupt occurs. It examines the interrupt vector number and dispatches to the
 * appropriate device handler.
 *
 * Parameters:
 *   __Frame__ - Pointer to the interrupt frame containing CPU state and interrupt info
 *               The IntNo field contains the interrupt vector number (32-47 for IRQs)
 *
 * The function handles two main categories of interrupts:
 * 1. APIC Timer (vector 32) - High-precision timer interrupts
 * 2. Legacy PIC interrupts (vectors 33-47) - Older hardware devices
 *
 * For PIC interrupts, proper End-of-Interrupt (EOI) signaling is crucial to allow
 * further interrupts from the same device.
 */
void
IrqHandler(InterruptFrame *__Frame__)
{
    /*Handle APIC Timer on IRQ0 - Vector 32 is the first IRQ (IRQ0)*/
    if (__Frame__->IntNo == 32)
    {
        TimerHandler(__Frame__);  /*Dispatch to timer subsystem*/
        return; /*APIC handles its own EOI, no need for PIC EOI*/
    }

    /*Legacy PIC interrupts - Handle EOI (End of Interrupt) signaling*/
    /*If interrupt came from slave PIC (vectors 40-47), send EOI to slave first*/
    if (__Frame__->IntNo >= 40)
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0xA0));
    /*Always send EOI to master PIC to acknowledge the interrupt*/
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x20));
}
