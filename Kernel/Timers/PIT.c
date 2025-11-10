#include <Timer.h>  /* Timer management structures and constants */

/*
 * InitializePitTimer - Initialize the Programmable Interval Timer
 *
 * Initializes the legacy PIT (8254) timer chip for periodic interrupt generation.
 * The PIT is configured in mode 3 (square wave generator) with the calculated
 * divisor to achieve the target interrupt frequency. This serves as a fallback
 * timer when more advanced timing hardware (APIC/HPET) is unavailable.
 *
 * The initialization process includes:
 * 1. Calculating the frequency divisor based on PIT base frequency.
 * 2. Programming the PIT control word for channel 0, mode 3.
 * 3. Setting the low and high bytes of the divisor value.
 * 4. Updating the global timer state with the configured frequency.
 *
 * Returns:
 * - 1: PIT timer initialized successfully.
 * - 0: Initialization failed (though this function currently always returns 1).
 *
 * Note: The PIT uses I/O ports 0x40-0x43 for data and control. Channel 0
 * (port 0x40) is used for timer interrupts, connected to IRQ 0.
 */
int
InitializePitTimer(void)
{
    PInfo("Initializing PIT Timer...\n");

    /*
     * Calculate the divisor for the target frequency.
     * PIT base frequency is 1,193,182 Hz (approximately 1.193 MHz).
     * Divisor = Base Frequency / Target Frequency
     */
    uint16_t Divisor = 1193182 /* PIT base frequency */ / TimerTargetFrequency;

    /*
     * Program the PIT control word (port 0x43).
     * 0x36 = 00110110b:
     * - Bits 7-6: Channel 0 (00)
     * - Bits 5-4: Access mode - lobyte/hibyte (11)
     * - Bits 3-1: Mode 3 - square wave generator (011)
     * - Bit 0: Binary mode (0)
     *
     * TODO: Define this as a macro for better maintainability.
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x36), "Nd"((uint16_t)0x43));

    /*
     * Send the low byte of the divisor to channel 0 data port (0x40).
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(Divisor & 0xFF)), "Nd"((uint16_t)0x40));

    /*
     * Send the high byte of the divisor to channel 0 data port (0x40).
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(Divisor >> 8)), "Nd"((uint16_t)0x40));

    /*
     * Update the global timer state with the configured frequency.
     */
    Timer.TimerFrequency = TimerTargetFrequency;
    PSuccess("PIT Timer initialized at %u Hz\n", Timer.TimerFrequency);

    return 1;
}
