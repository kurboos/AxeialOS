#include <Timer.h> /* Timer-related definitions and constants */

/**
 * @brief Read a Model-Specific Register (MSR).
 *
 * @details Executes the `rdmsr` instruction to read the 64-bit value of the given MSR.
 * 			The result is constructed from the low 32 bits (EAX) and high 32 bits (EDX).
 *
 * @param __Msr__ The identifier of the MSR to read.
 *
 * @return The 64-bit value stored in the specified MSR.
 *
 * @note MSRs are CPU-specific registers used for configuration and status.
 *       Accessing invalid MSRs may cause a fault.
 */
uint64_t
ReadMsr(uint32_t __Msr__)
{
    uint32_t Low, High;

    __asm__ volatile("rdmsr" : "=a"(Low), "=d"(High) : "c"(__Msr__));

    return ((uint64_t)High << 32) | Low;
}

/**
 * @brief Write a value to a Model-Specific Register (MSR).
 *
 * @details Executes the `wrmsr` instruction to write a 64-bit value into the given MSR.
 * 			The value is split into low (EAX) and high (EDX) 32-bit parts before writing.
 *
 * @param __Msr__   The identifier of the MSR to write to.
 * @param __Value__ The 64-bit value to store in the MSR.
 *
 * @return void
 *
 * @note Writing to certain MSRs can change CPU behavior and should be done
 *       with caution. Incorrect writes may cause system instability.
 */
void
WriteMsr(uint32_t __Msr__, uint64_t __Value__)
{

    uint32_t Low  = (uint32_t)__Value__;
    uint32_t High = (uint32_t)(__Value__ >> 32);

    __asm__ volatile("wrmsr" : : "a"(Low), "d"(High), "c"(__Msr__));
}
