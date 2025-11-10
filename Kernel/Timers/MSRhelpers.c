#include <Timer.h>  /* Timer-related definitions and constants */

/*
 * ReadMsr - Read a Model-Specific Register
 *
 * Reads the value of a specified MSR using the RDMSR instruction. The MSR
 * value is returned as a 64-bit unsigned integer, with the high 32 bits
 * coming from EDX and the low 32 bits from EAX.
 *
 * Parameters:
 * - __Msr__: The MSR index to read (32-bit unsigned integer).
 *
 * Returns:
 * - The 64-bit value read from the MSR.
 *
 * Note: This function requires appropriate privilege level (typically ring 0)
 * and may cause a general protection fault if the MSR is not accessible.
 */
uint64_t
ReadMsr(uint32_t __Msr__)
{
    uint32_t Low, High;

    /*
     * Execute RDMSR instruction.
     * Input: ECX = MSR index
     * Output: EDX:EAX = MSR value (High:Low)
     */
    __asm__ volatile("rdmsr" : "=a"(Low), "=d"(High) : "c"(__Msr__));

    /*
     * Combine high and low 32-bit parts into a single 64-bit value.
     */
    return ((uint64_t)High << 32) | Low;
}

/*
 * WriteMsr - Write to a Model-Specific Register
 *
 * Writes a 64-bit value to a specified MSR using the WRMSR instruction.
 * The value is decomposed into high and low 32-bit parts for the EDX:EAX
 * register pair.
 *
 * Parameters:
 * - __Msr__: The MSR index to write to (32-bit unsigned integer).
 * - __Value__: The 64-bit value to write to the MSR.
 *
 * Note: This function requires appropriate privilege level (typically ring 0)
 * and may cause a general protection fault if the MSR is not accessible or
 * if the value is invalid for the specific MSR.
 */
void
WriteMsr(uint32_t __Msr__, uint64_t __Value__)
{
    /*
     * Decompose the 64-bit value into high and low 32-bit parts.
     */
    uint32_t Low = (uint32_t)__Value__;
    uint32_t High = (uint32_t)(__Value__ >> 32);

    /*
     * Execute WRMSR instruction.
     * Input: ECX = MSR index, EDX:EAX = MSR value (High:Low)
     */
    __asm__ volatile("wrmsr" : : "a"(Low), "d"(High), "c"(__Msr__));
}
