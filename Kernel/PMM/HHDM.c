#include <PMM.h>

/*
 * PhysToVirt - Convert physical address to virtual address
 *
 * Translates a physical memory address to its corresponding virtual address
 * in the Higher Half Direct Mapping region. This allows direct access to
 * physical memory through virtual addressing.
 *
 * Parameters:
 *   __PhysAddr__ - Physical address to convert
 *
 * Returns:
 *   Virtual address corresponding to the physical address
 */
void*
PhysToVirt(uint64_t __PhysAddr__)
{
    return (void*)(__PhysAddr__ + Pmm.HhdmOffset);
}

/*
 * VirtToPhys - Convert virtual address to physical address
 *
 * Translates a virtual address in the HHDM region back to its corresponding
 * physical address. This is the inverse operation of PhysToVirt.
 *
 * Parameters:
 *   __VirtAddr__ - Virtual address in HHDM region to convert
 *
 * Returns:
 *   Physical address corresponding to the virtual address
 */
uint64_t
VirtToPhys(void* __VirtAddr__)
{
    return (uint64_t)__VirtAddr__ - Pmm.HhdmOffset;
}
