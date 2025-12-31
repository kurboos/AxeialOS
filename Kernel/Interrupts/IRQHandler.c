#include <IDT.h>
#include <Timer.h>

void
IrqHandler(InterruptFrame* __Frame__)
{
    /*APIC*/ //<-- Currently the Timer in use always
    if (__Frame__->IntNo == 32)
    {
        SysErr  err;
        SysErr* Error = &err;
        TimerHandler(__Frame__, Error);
        return; /*APIC will send EOI*/
    }

    /*Legacy PIC interrupts > Handle EOI*/
    /*If interrupt came from slave PIC (vectors 40-47), EOI to slave first*/
    if (__Frame__->IntNo >= 40)
    {
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0xA0));
    }

    /* EOI to PIC */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x20));
}
