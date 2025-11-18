#include <Timer.h> /* Timer management structures and constants */

/**
 * @brief Initialize the Programmable Interval Timer (PIT).
 *
 * @details Configures the PIT (channel 0) to generate periodic interrupts
 * 			at the target frequency specified by @c TimerTargetFrequency.
 * 			The divisor is calculated from the PIT base frequency (1,193,182 Hz)
 * 			and programmed into the PIT control registers.
 *
 * @return 1 on successful initialization.
 *
 * @note This function is used as a fallback if APIC or HPET timers
 *       are not available. The PIT is legacy hardware but ensures
 *       basic timer functionality.
 */
int
InitializePitTimer(void)
{
    PInfo("Initializing PIT Timer...\n");

    uint16_t Divisor = 1193182 /* PIT base frequency */ / TimerTargetFrequency;

    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x36), "Nd"((uint16_t)0x43));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(Divisor & 0xFF)), "Nd"((uint16_t)0x40));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(Divisor >> 8)), "Nd"((uint16_t)0x40));

    Timer.TimerFrequency = TimerTargetFrequency;
    PSuccess("PIT Timer initialized at %u Hz\n", Timer.TimerFrequency);

    return 1;
}
