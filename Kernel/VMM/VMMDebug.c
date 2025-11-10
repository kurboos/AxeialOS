#include <VMM.h>

/*
 * IsValidPhysicalAddress
 *
 * This function performs comprehensive validation of a physical memory address
 * to ensure it corresponds to actual, accessible RAM. In kernel development,
 * especially with virtual memory management, it's crucial to validate addresses
 * before attempting to access or map them to prevent system crashes or undefined
 * behavior.
 *
 * The validation process involves multiple checks:
 * 1. Rejecting null/zero addresses which are typically invalid for data access
 * 2. Ensuring page alignment (4KB boundaries) as required by x86-64 paging
 * 3. Verifying the address falls within known physical memory regions
 *
 * This function is essential for memory safety in the Virtual Memory Manager (VMM)
 * and helps prevent accessing non-existent or reserved memory regions that could
 * cause hardware exceptions or data corruption.
 *
 * Parameters:
 *   __PhysAddr__ - The 64-bit physical address to be validated against the
 *                  system's memory map.
 *
 * Returns:
 *   1 (true) if the address is valid and safe to access
 *   0 (false) if the address is invalid or potentially unsafe
 *
 * Note: The return type is int rather than bool for C compatibility, though
 *       bool could be used in C99+ environments.
 */
static int
IsValidPhysicalAddress(uint64_t __PhysAddr__)
{
    /*
     * First sanity check: reject address zero. Physical address 0 is typically
     * reserved or unmapped in most system architectures, and accessing it
     * could cause issues or represent uninitialized pointers.
     */
    if (__PhysAddr__ == 0)
        return 0;

    /*
     * Page alignment verification: x86-64 requires page table entries to be
     * aligned to 4KB (0x1000) boundaries. Misaligned addresses cannot be
     * properly mapped in page tables and would cause page faults.
     */
    if ((__PhysAddr__ & 0xFFF) != 0)
        return 0;

    /*
     * Memory region validation: iterate through all physical memory regions
     * provided by the Physical Memory Manager (PMM). Each region represents
     * a contiguous block of RAM that can be safely accessed.
     *
     * The PMM provides this information from the bootloader (Limine) and
     * includes details about usable, reserved, and ACPI memory regions.
     */
    for (uint32_t Index = 0; Index < Pmm.RegionCount; Index++)
    {
        /*
         * Calculate the start and end addresses of the current memory region.
         * Regions are defined by a base address and length, representing
         * contiguous physical memory blocks.
         */
        uint64_t RegionStart = Pmm.Regions[Index].Base;
        uint64_t RegionEnd = RegionStart + Pmm.Regions[Index].Length;

        /*
         * Check if the provided address falls within this memory region's bounds.
         * The address must be >= start and < end to be considered valid.
         */
        if (__PhysAddr__ >= RegionStart && __PhysAddr__ < RegionEnd)
        {
            return 1; /* Address is valid within this region */
        }
    }

    /*
     * Address not found in any valid memory region. This could indicate:
     * - Memory-mapped I/O regions
     * - Reserved system memory
     * - Non-existent physical addresses
     * - Addresses beyond the system's physical memory capacity
     */
    return 0;
}

/*
 * IsValidHhdmAddress
 *
 * Validates virtual addresses within the Higher Half Direct Mapping (HHDM) range.
 * The HHDM is a special region in the virtual address space where physical memory
 * is directly mapped, allowing kernel code to access physical RAM through simple
 * virtual address calculations.
 *
 * The HHDM offset (provided by the Limine bootloader) defines the starting point
 * of this direct mapping. Virtual addresses above this offset can be converted
 * to physical addresses by subtracting the offset, enabling direct physical
 * memory access without complex page table traversals.
 *
 * This validation is critical for:
 * - Preventing access to unmapped virtual memory
 * - Ensuring HHDM conversions produce valid physical addresses
 * - Maintaining memory safety in kernel operations
 *
 * Parameters:
 *   __VirtAddr__ - The 64-bit virtual address to validate within HHDM space.
 *
 * Returns:
 *   1 (true) if the virtual address is within HHDM and maps to valid physical RAM
 *   0 (false) if the address is outside HHDM or maps to invalid physical memory
 */
static int
IsValidHhdmAddress(uint64_t __VirtAddr__)
{
    /*
     * Range check: ensure the virtual address is at or above the HHDM offset.
     * Addresses below this offset are in the lower half of virtual memory,
     * which may contain user space, kernel code/data, or unmapped regions.
     */
    if (__VirtAddr__ < Vmm.HhdmOffset)
        return 0;

    /*
     * Convert HHDM virtual address to physical address. The HHDM provides
     * a direct linear mapping where virtual_address = physical_address + offset.
     * Therefore, physical_address = virtual_address - offset.
     */
    uint64_t PhysAddr = __VirtAddr__ - Vmm.HhdmOffset;

    /*
     * Validate the resulting physical address using the physical memory map.
     * Even within HHDM, not all addresses correspond to actual RAM.
     */
    return IsValidPhysicalAddress(PhysAddr);
}

/*
 * IsSafeToAccess
 *
 * Performs safety validation on a pointer before dereferencing it. This function
 * combines NULL pointer checks with virtual address validation to prevent common
 * memory access errors that could crash the kernel.
 *
 * In kernel development, pointer validation is especially important because:
 * - NULL pointer dereferences cause immediate system crashes
 * - Invalid pointer access can corrupt memory or trigger page faults
 * - Kernel pointers often reference sensitive system structures
 *
 * This function specifically validates pointers within the HHDM range, which
 * is where most kernel data structures and dynamically allocated memory reside.
 *
 * Parameters:
 *   __Ptr__ - Pointer to a uint64_t that needs safety validation before access.
 *
 * Returns:
 *   1 (true) if the pointer is non-NULL and points to a valid HHDM address
 *   0 (false) if the pointer is NULL or points to an invalid/unmapped address
 */
static int
IsSafeToAccess(uint64_t *__Ptr__)
{
    /*
     * NULL pointer check: prevent dereferencing null pointers, which would
     * cause immediate undefined behavior and likely system crashes.
     */
    if (!__Ptr__)
        return 0;

    /*
     * Extract the virtual address from the pointer for validation.
     * In C, pointers are essentially unsigned integers representing
     * memory addresses.
     */
    uint64_t VirtAddr = (uint64_t)__Ptr__;

    /*
     * Validate the virtual address using HHDM-specific checks.
     * This ensures the pointer references accessible physical memory
     * through the direct mapping.
     */
    return IsValidHhdmAddress(VirtAddr);
}

/*
 * VmmDumpSpace
 *
 * Provides detailed diagnostic output for a virtual memory space, including
 * page table structure analysis and mapping statistics. This function is
 * essential for debugging memory management issues, understanding virtual
 * memory layout, and verifying correct page table setup.
 *
 * The function traverses the four-level x86-64 page table hierarchy:
 * - PML4 (Page Map Level 4)
 * - PDPT (Page Directory Pointer Table)
 * - PD (Page Directory)
 * - PT (Page Table)
 *
 * It handles both regular 4KB pages and huge pages (2MB and 1GB) efficiently,
 * counting mapped pages and validating table integrity throughout the traversal.
 *
 * Extensive validation ensures memory safety during the dump process, preventing
 * crashes from accessing invalid page tables or corrupted structures.
 *
 * Parameters:
 *   __Space__ - Pointer to the VirtualMemorySpace structure to analyze and dump.
 *               This structure contains the root of the page table hierarchy
 *               and associated metadata.
 *
 * Returns:
 *   void - Output is sent to kernel logging facilities (console/serial).
 *          No return value as this is a diagnostic function.
 *
 * Side Effects:
 *   - Generates console/serial output with detailed memory space information
 *   - May access multiple levels of page tables (safe due to validation)
 *   - Provides statistics on memory usage and table validation status
 */
void
VmmDumpSpace(VirtualMemorySpace *__Space__)
{
    /*
     * Initial validation: ensure the virtual memory space pointer is not NULL.
     * A NULL space indicates an uninitialized or invalid memory context.
     */
    if (!__Space__)
    {
        PError("Cannot dump null virtual space\n");
        return;
    }

    /*
     * Validate the physical base address of the PML4 (root page table).
     * The physical address must correspond to actual RAM and be page-aligned.
     */
    if (!IsValidPhysicalAddress(__Space__->PhysicalBase))
    {
        PError("Invalid PML4 physical address: 0x%016lx\n", __Space__->PhysicalBase);
        return;
    }

    /*
     * Validate the virtual address of the PML4. The virtual mapping must
     * be within HHDM and point to accessible memory.
     */
    if (!__Space__->Pml4 || !IsValidHhdmAddress((uint64_t)__Space__->Pml4))
    {
        PError("Invalid PML4 virtual address: 0x%016lx\n", (uint64_t)__Space__->Pml4);
        return;
    }

    /*
     * Print header information for the virtual memory space dump.
     * This provides context about the memory space being analyzed.
     */
    PInfo("Virtual Memory Space Information:\n");
    KrnPrintf("  PML4 Physical: 0x%016lx\n", __Space__->PhysicalBase);
    KrnPrintf("  PML4 Virtual:  0x%016lx\n", (uint64_t)__Space__->Pml4);
    KrnPrintf("  Reference Count: %u\n", __Space__->RefCount);

    /*
     * Initialize counters for tracking mapping statistics and validation status.
     * These will be updated during the page table traversal.
     */
    uint64_t MappedPages = 0;
    uint64_t ValidatedTables = 0;
    uint64_t SkippedTables = 0;

    /*
     * Traverse the PML4 (level 4 page table). Each entry covers 512 GB of
     * virtual address space. Only present entries are processed.
     */
    for (uint64_t Pml4Index = 0; Pml4Index < PageTableEntries; Pml4Index++)
    {
        uint64_t Pml4Entry = __Space__->Pml4[Pml4Index];

        /* Skip non-present entries (not mapped) */
        if (!(Pml4Entry & PTEPRESENT))
            continue;

        /*
         * Extract the physical address of the PDPT from the PML4 entry.
         * Page table entries store physical addresses in bits 51:12.
         */
        uint64_t PdptPhys = Pml4Entry & 0x000FFFFFFFFFF000ULL;
        if (!IsValidPhysicalAddress(PdptPhys))
        {
            SkippedTables++;
            continue;
        }

        /*
         * Convert the PDPT physical address to a virtual address using HHDM.
         * This allows safe access to the page table structure.
         */
        uint64_t *Pdpt = (uint64_t*)PhysToVirt(PdptPhys);
        if (!IsSafeToAccess(Pdpt))
        {
            SkippedTables++;
            continue;
        }

        ValidatedTables++;

        /*
         * Traverse the PDPT (level 3 page table). Each entry covers 1 GB
         * of virtual address space, or maps a 1GB huge page directly.
         */
        for (uint64_t PdptIndex = 0; PdptIndex < PageTableEntries; PdptIndex++)
        {
            uint64_t PdptEntry = Pdpt[PdptIndex];

            if (!(PdptEntry & PTEPRESENT))
                continue;

            /*
             * Check for 1GB huge page mapping. Huge pages map large contiguous
             * regions directly, reducing page table overhead for large allocations.
             */
            if (PdptEntry & PTEHUGEPAGE)
            {
                MappedPages += 262144; /* 1GB / 4KB = 262144 pages */
                continue;
            }

            /*
             * Extract PD physical address and validate it.
             */
            uint64_t PdPhys = PdptEntry & 0x000FFFFFFFFFF000ULL;
            if (!IsValidPhysicalAddress(PdPhys))
                continue;

            /*
             * Convert PD physical address to virtual for access.
             */
            uint64_t *Pd = (uint64_t*)PhysToVirt(PdPhys);
            if (!IsSafeToAccess(Pd))
                continue;

            /*
             * Traverse the PD (level 2 page table). Each entry covers 2 MB
             * of virtual address space, or maps a 2MB huge page directly.
             */
            for (uint64_t PdIndex = 0; PdIndex < PageTableEntries; PdIndex++)
            {
                uint64_t PdEntry = Pd[PdIndex];

                if (!(PdEntry & PTEPRESENT))
                    continue;

                /*
                 * Check for 2MB huge page mapping. 2MB pages provide a good
                 * balance between page table efficiency and memory flexibility.
                 */
                if (PdEntry & PTEHUGEPAGE)
                {
                    MappedPages += 512; /* 2MB / 4KB = 512 pages */
                    continue;
                }

                /*
                 * Extract PT physical address and validate it.
                 */
                uint64_t PtPhys = PdEntry & 0x000FFFFFFFFFF000ULL;
                if (!IsValidPhysicalAddress(PtPhys))
                    continue;

                /*
                 * Convert PT physical address to virtual for access.
                 */
                uint64_t *Pt = (uint64_t*)PhysToVirt(PtPhys);
                if (!IsSafeToAccess(Pt))
                    continue;

                /*
                 * Traverse the PT (level 1 page table). Each entry maps
                 * a single 4KB page. Count all present entries.
                 */
                for (uint64_t PtIndex = 0; PtIndex < PageTableEntries; PtIndex++)
                {
                    if (Pt[PtIndex] & PTEPRESENT)
                        MappedPages++;
                }
            }
        }
    }

    /*
     * Output the final statistics from the page table traversal.
     * This provides a summary of the virtual memory space's state.
     */
    KrnPrintf("  Validated Tables: %lu\n", ValidatedTables);
    KrnPrintf("  Skipped Tables: %lu\n", SkippedTables);
    KrnPrintf("  Mapped Pages: %lu (%lu KB)\n", MappedPages, MappedPages * 4);
}

/*
 * VmmDumpStats
 *
 * Provides a high-level overview of the Virtual Memory Manager's current state,
 * including configuration parameters and memory layout information. This function
 * is valuable for system diagnostics, debugging memory issues, and understanding
 * the current memory management configuration.
 *
 * The function outputs:
 * - HHDM offset (critical for address translation)
 * - Kernel PML4 physical address (root of kernel page tables)
 * - Physical memory region information from PMM
 * - Detailed kernel virtual memory space analysis (if available)
 *
 * This information helps developers and system administrators understand:
 * - How virtual-to-physical address translation works
 * - The layout of physical memory
 * - The structure of kernel virtual memory
 * - Potential memory management issues
 *
 * Returns:
 *   void - All output is sent to kernel logging facilities.
 *
 * Preconditions:
 *   - VMM must be initialized (HHDM offset set)
 *   - PMM must provide memory region information
 *   - Kernel space should be available for detailed analysis
 */
void
VmmDumpStats(void)
{
    /*
     * Validate VMM initialization. The HHDM offset is set during VMM
     * initialization and is required for proper virtual memory operation.
     */
    if (!Vmm.HhdmOffset)
    {
        PError("VMM not properly initialized - no HHDM offset\n");
        return;
    }

    /*
     * Print header and core VMM configuration parameters.
     * These values are essential for understanding the memory layout.
     */
    PInfo("VMM Statistics:\n");
    KrnPrintf("  HHDM Offset: 0x%016lx\n", Vmm.HhdmOffset);
    KrnPrintf("  Kernel PML4: 0x%016lx\n", Vmm.KernelPml4Physical);

    /*
     * Display information about physical memory regions.
     * The PMM provides this data from the bootloader, showing how
     * physical RAM is organized and what regions are available.
     */
    KrnPrintf("  Memory Map Regions: %u\n", Pmm.RegionCount);
    for (uint32_t Index = 0; Index < Pmm.RegionCount && Index < 5; Index++)
    {
        KrnPrintf("    [%u] 0x%016lx-0x%016lx (%lu MB)\n",
            Index,
            Pmm.Regions[Index].Base,
            Pmm.Regions[Index].Base + Pmm.Regions[Index].Length,
            Pmm.Regions[Index].Length / (1024 * 1024));
    }
    if (Pmm.RegionCount > 5)
    {
        KrnPrintf("    ... and %u more regions\n", Pmm.RegionCount - 5);
    }

    /*
     * Provide detailed analysis of the kernel's virtual memory space.
     * This includes page table structure, mapping statistics, and
     * validation information.
     */
    if (Vmm.KernelSpace)
    {
        KrnPrintf("  Kernel Space: 0x%016lx\n", (uint64_t)Vmm.KernelSpace);
        VmmDumpSpace(Vmm.KernelSpace);
    }
    else
    {
        PWarn("  No kernel space available\n");/*Impossible*/
    }
}
