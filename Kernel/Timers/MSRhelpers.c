#include <Timer.h>

uint64_t
ReadMsr(uint32_t __Msr__)
{
    uint32_t Low, High;

    __asm__ volatile("rdmsr" : "=a"(Low), "=d"(High) : "c"(__Msr__));

    return ((uint64_t)High << 32) | Low;
}

void
WriteMsr(uint32_t __Msr__, uint64_t __Value__)
{
    uint32_t Low  = (uint32_t)__Value__;
    uint32_t High = (uint32_t)(__Value__ >> 32);

    __asm__ volatile("wrmsr" : : "a"(Low), "d"(High), "c"(__Msr__));
}
