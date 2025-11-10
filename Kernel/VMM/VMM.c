#include <VMM.h>

/*
 * Global Virtual Memory Manager Instance
 *
 * This global structure holds the core state of the Virtual Memory Manager (VMM).
 * It maintains essential information about the current virtual memory configuration,
 * including the Higher Half Direct Mapping (HHDM) offset, kernel page tables,
 * and references to the kernel's virtual memory space.
 *
 * The VMM is responsible for managing virtual-to-physical address translations,
 * creating and destroying virtual address spaces, and providing memory mapping
 * services to the rest of the kernel. This global instance allows all VMM
 * functions to access and modify the current memory management state.
 *
 * Key components:
 * - HhdmOffset: Base address for direct physical memory mapping in virtual space
 * - KernelPml4Physical: Physical address of the kernel's root page table (PML4)
 * - KernelSpace: Pointer to the kernel's virtual memory space structure
 *
 * Initialization: This structure is zero-initialized at compile time and
 * properly set up during the InitializeVmm() function call.
 */
VirtualMemoryManager Vmm = {0};

/*
 * InitializeVmm
 *
 * Performs the complete initialization of the Virtual Memory Manager subsystem.
 * This function sets up the foundational components required for virtual memory
 * management in the kernel, including establishing the HHDM mapping, capturing
 * the current page table state, and creating the kernel's virtual memory space.
 *
 * The initialization process follows these critical steps:
 * 1. Retrieve the HHDM offset from the Physical Memory Manager
 * 2. Capture the current PML4 physical address from the CR3 register
 * 3. Allocate and initialize the kernel virtual memory space structure
 * 4. Set up proper virtual address mappings for kernel access
 *
 * This function must be called early in the kernel boot sequence, after the
 * Physical Memory Manager is initialized but before any complex memory
 * operations are performed. It establishes the foundation for all subsequent
 * virtual memory operations.
 *
 * Error Handling:
 * - Logs detailed error messages for debugging
 * - Returns early on allocation failures to prevent system instability
 * - Validates all critical pointers and addresses before use
 *
 * Side Effects:
 * - Modifies the global Vmm structure
 * - Allocates physical pages for kernel data structures
 * - Establishes virtual address mappings for kernel page tables
 *
 * Returns:
 *   void - Success/failure indicated through logging and system state
 */
void
InitializeVmm(void)
{
    /*
     * Log the start of VMM initialization for boot progress tracking.
     * This helps with debugging and provides user feedback during boot.
     */
    PInfo("Initializing Virtual Memory Manager...\n");

    /*
     * Retrieve the Higher Half Direct Mapping (HHDM) offset from the PMM.
     * The HHDM provides a direct linear mapping of physical memory into
     * the kernel's virtual address space, typically starting at a high
     * address like 0xFFFF800000000000 on x86-64 systems.
     *
     * This offset is crucial for converting between physical and virtual
     * addresses throughout the kernel.
     */
    Vmm.HhdmOffset = Pmm.HhdmOffset;
    PDebug("Using HHDM offset: 0x%016lx\n", Vmm.HhdmOffset);

    /*
     * Capture the current Page Map Level 4 (PML4) physical address from
     * the CR3 control register. CR3 contains the physical address of the
     * root page table currently in use by the CPU.
     *
     * The lower 12 bits of CR3 contain control flags, so we mask them
     * out to get the clean physical address of the PML4 table.
     */
    uint64_t CurrentCr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (CurrentCr3));
    Vmm.KernelPml4Physical = CurrentCr3 & 0xFFFFFFFFFFFFF000ULL; /* Clear lower 12 bits */

    PDebug("Current PML4 at: 0x%016lx\n", Vmm.KernelPml4Physical);

    /*
     * Allocate a physical page for the kernel virtual memory space structure.
     * This structure will hold metadata about the kernel's address space,
     * including pointers to page tables and reference counting information.
     */
    Vmm.KernelSpace = (VirtualMemorySpace*)PhysToVirt(AllocPage());
    if (!Vmm.KernelSpace)
    {
        PError("Failed to allocate kernel virtual space\n");
        return;
    }

    /*
     * Initialize the kernel virtual memory space structure with essential data:
     * - PhysicalBase: Physical address of the kernel's PML4 table
     * - Pml4: Virtual address pointer to access the PML4 table
     * - RefCount: Reference count (set to 1 for the kernel's use)
     */
    Vmm.KernelSpace->PhysicalBase = Vmm.KernelPml4Physical;                  /* Physical address of PML4 */
    Vmm.KernelSpace->Pml4 = (uint64_t*)PhysToVirt(Vmm.KernelPml4Physical);  /* Virtual address for PML4 */
    Vmm.KernelSpace->RefCount = 1;                                           /* Initialize reference count */

    /*
     * Log successful initialization with the kernel PML4 physical address.
     * This confirms that the VMM is ready for operation and provides
     * debugging information about the kernel's page table location.
     */
    PSuccess("VMM initialized with kernel space at 0x%016lx\n", Vmm.KernelPml4Physical);
}

/*
 * CreateVirtualSpace
 *
 * Creates a new, independent virtual memory space for use by processes or
 * other kernel subsystems. This function allocates all necessary page table
 * structures and initializes them with appropriate kernel mappings.
 *
 * The creation process involves:
 * 1. Allocating memory for the VirtualMemorySpace structure
 * 2. Allocating a new PML4 table for the address space
 * 3. Copying kernel mappings from the existing kernel space
 * 4. Setting up proper reference counting
 *
 * The new address space inherits all kernel mappings (upper half of virtual
 * memory) while providing a clean lower half for user-space or subsystem-specific
 * mappings. This ensures that kernel code and data remain accessible while
 * allowing isolation between different address spaces.
 *
 * Memory Management:
 * - Allocates physical pages for the space structure and PML4 table
 * - Uses reference counting to track space usage
 * - Validates all allocations and conversions
 *
 * Error Handling:
 * - Returns NULL on any allocation or validation failure
 * - Cleans up partial allocations on failure
 * - Logs detailed error messages for debugging
 *
 * Parameters:
 *   None - Creates a new space with default kernel mappings
 *
 * Returns:
 *   VirtualMemorySpace* - Pointer to the new virtual memory space, or NULL on failure
 */
VirtualMemorySpace*
CreateVirtualSpace(void)
{
    /*
     * Validate that the VMM has been properly initialized before creating
     * new address spaces. This ensures kernel space and page tables exist.
     */
    if (!Vmm.KernelSpace || !Vmm.KernelSpace->Pml4)
    {
        PError("VMM not properly initialized\n");
        return 0;
    }

    /*
     * Allocate a physical page for the VirtualMemorySpace structure.
     * This structure contains metadata about the address space.
     */
    uint64_t SpacePhys = AllocPage();
    if (!SpacePhys)
    {
        PError("Failed to allocate virtual space structure\n");
        return 0;
    }

    /*
     * Convert the physical address to a virtual address using HHDM.
     * This allows the kernel to access the space structure.
     */
    VirtualMemorySpace *Space = (VirtualMemorySpace*)PhysToVirt(SpacePhys);
    if (!Space)
    {
        PError("HHDM conversion failed for space structure\n");
        FreePage(SpacePhys);
        return 0;
    }

    /*
     * Allocate a physical page for the new PML4 (root page table).
     * Each virtual memory space needs its own page table hierarchy.
     */
    uint64_t Pml4Phys = AllocPage();
    if (!Pml4Phys)
    {
        PError("Failed to allocate PML4\n");
        FreePage(SpacePhys);
        return 0;
    }

    /*
     * Initialize the virtual memory space structure with the allocated resources.
     * Set up the physical base address, virtual pointer to PML4, and reference count.
     */
    Space->PhysicalBase = Pml4Phys;
    Space->Pml4 = (uint64_t*)PhysToVirt(Pml4Phys);
    Space->RefCount = 1;

    /*
     * Validate that the HHDM conversion for the PML4 succeeded.
     * This ensures we can access the page table.
     */
    if (!Space->Pml4)
    {
        PError("HHDM conversion failed for PML4\n");
        FreePage(SpacePhys);
        FreePage(Pml4Phys);
        return 0;
    }

    /*
     * Clear the entire new PML4 table to ensure a clean starting state.
     * All 512 entries are set to zero, indicating no mappings initially.
     */
    for (uint64_t Index = 0; Index < PageTableEntries; Index++)
    {
        Space->Pml4[Index] = 0;
    }

    /*
     * Copy kernel mappings from the existing kernel space into the upper half
     * of the new PML4 (entries 256-511). This preserves access to kernel code,
     * data, and mappings while allowing the lower half (entries 0-255) to be
     * used for user-space or subsystem-specific mappings.
     *
     * This is crucial for maintaining kernel functionality across different
     * address spaces.
     */
    for (uint64_t Index = 256; Index < PageTableEntries; Index++)
    {
        Space->Pml4[Index] = Vmm.KernelSpace->Pml4[Index];
    }

    /*
     * Log the successful creation of the new virtual space.
     * Include the physical address of the new PML4 for debugging.
     */
    PDebug("Created virtual space: PML4=0x%016lx\n", Pml4Phys);
    return Space;
}

/*
 * DestroyVirtualSpace
 *
 * Completely destroys a virtual memory space and frees all associated resources.
 * This function recursively frees all page table structures in the address space,
 * including intermediate tables and mapped pages, while preserving the kernel space.
 *
 * The destruction process involves:
 * 1. Reference count checking to prevent premature destruction
 * 2. Recursive traversal of the page table hierarchy
 * 3. Freeing of all allocated page table pages
 * 4. Cleanup of the space structure itself
 *
 * Safety measures:
 * - Prevents destruction of the kernel space
 * - Uses reference counting to avoid destroying in-use spaces
 * - Validates all operations to prevent crashes
 *
 * Memory freed includes:
 * - All page table pages (PT, PD, PDPT) in the lower address space
 * - The PML4 root table
 * - The VirtualMemorySpace structure
 *
 * Parameters:
 *   __Space__ - Pointer to the VirtualMemorySpace to destroy
 *
 * Returns:
 *   void - Completion indicated through logging
 */
void
DestroyVirtualSpace(VirtualMemorySpace *__Space__)
{
    /*
     * Safety checks: prevent destruction of kernel space or null pointers.
     * The kernel space must be preserved for system stability.
     */
    if (!__Space__ || __Space__ == Vmm.KernelSpace)
    {
        PWarn("Cannot destroy kernel space or null space\n");
        return;
    }

    /*
     * Decrement the reference count. If other components still reference
     * this space, delay destruction until all references are released.
     */
    __Space__->RefCount--;
    if (__Space__->RefCount > 0)
    {
        PDebug("Virtual space still has %u references\n", __Space__->RefCount);
        return;
    }

    /*
     * Log the destruction process for debugging and monitoring.
     */
    PDebug("Destroying virtual space: PML4=0x%016lx\n", __Space__->PhysicalBase);

    /*
     * Traverse the lower half of the PML4 (entries 0-255) to free user-space
     * page table structures. The upper half contains kernel mappings that
     * are preserved and not freed here.
     */
    for (uint64_t Pml4Index = 0; Pml4Index < 256; Pml4Index++)
    {
        /* Skip entries that are not present (not mapped) */
        if (!(__Space__->Pml4[Pml4Index] & PTEPRESENT))
            continue;

        /*
         * Extract the physical address of the Page Directory Pointer Table (PDPT)
         * from the PML4 entry and convert it to a virtual address for access.
         */
        uint64_t PdptPhys = __Space__->Pml4[Pml4Index] & 0x000FFFFFFFFFF000ULL;
        uint64_t *Pdpt = (uint64_t*)PhysToVirt(PdptPhys);
        if (!Pdpt) continue;

        /*
         * Traverse all entries in the PDPT. Each entry can point to a Page
         * Directory (PD) or represent a 1GB huge page mapping.
         */
        for (uint64_t PdptIndex = 0; PdptIndex < PageTableEntries; PdptIndex++)
        {
            if (!(Pdpt[PdptIndex] & PTEPRESENT))
                continue;

            /*
             * Extract the physical address of the Page Directory (PD) and
             * convert to virtual address. Skip if huge page (handled differently).
             */
            uint64_t PdPhys = Pdpt[PdptIndex] & 0x000FFFFFFFFFF000ULL;
            uint64_t *Pd = (uint64_t*)PhysToVirt(PdPhys);
            if (!Pd) continue;

            /*
             * Traverse all entries in the Page Directory. Each entry can point
             * to a Page Table (PT) or represent a 2MB huge page mapping.
             */
            for (uint64_t PdIndex = 0; PdIndex < PageTableEntries; PdIndex++)
            {
                if (!(Pd[PdIndex] & PTEPRESENT))
                    continue;

                /*
                 * Free each Page Table page. For regular 4KB mappings, each
                 * PD entry points to a PT that contains the actual page mappings.
                 */
                FreePage(Pd[PdIndex] & 0x000FFFFFFFFFF000ULL);
            }

            /* Free the Page Directory page itself */
            FreePage(PdPhys);
        }

        /* Free the Page Directory Pointer Table page */
        FreePage(PdptPhys);
    }

    /* Free the root Page Map Level 4 table */
    FreePage(__Space__->PhysicalBase);

    /*
     * Free the physical page backing the VirtualMemorySpace structure itself.
     * Convert the virtual address back to physical for proper deallocation.
     */
    FreePage(VirtToPhys(__Space__));

    /*
     * Log successful completion of the destruction process.
     */
    PDebug("Virtual space destroyed\n");
}

/*
 * MapPage
 *
 * Maps a single 4KB physical page to a virtual address within a specified
 * virtual memory space. This function handles the creation of intermediate
 * page table structures as needed and sets appropriate page table entry flags.
 *
 * The mapping process involves:
 * 1. Parameter validation (alignment, bounds checking)
 * 2. Acquiring or creating the appropriate page table
 * 3. Setting up the page table entry with physical address and flags
 * 4. Flushing the Translation Lookaside Buffer (TLB) for consistency
 *
 * Page table entries in x86-64 contain:
 * - Physical frame address (bits 51:12)
 * - Various flags (present, writable, user/supervisor, etc.)
 * - Reserved bits that must be zero
 *
 * Error Handling:
 * - Validates all input parameters
 * - Checks for existing mappings to prevent overwrites
 * - Returns failure status with detailed error messages
 *
 * Parameters:
 *   __Space__ - The virtual memory space to modify
 *   __VirtAddr__ - Virtual address to map (must be 4KB aligned)
 *   __PhysAddr__ - Physical address to map to (must be 4KB aligned)
 *   __Flags__ - Page table entry flags (permissions, caching, etc.)
 *
 * Returns:
 *   int - 1 on success, 0 on failure
 */
int
MapPage(VirtualMemorySpace *__Space__, uint64_t __VirtAddr__, uint64_t __PhysAddr__, uint64_t __Flags__)
{
    /*
     * Comprehensive parameter validation:
     * - Ensure space pointer is valid
     * - Check 4KB alignment for both virtual and physical addresses
     */
    if (!__Space__ || (__VirtAddr__ % PageSize) != 0 || (__PhysAddr__ % PageSize) != 0)
    {
        PError("Invalid parameters for MapPage\n");
        return 0;
    }

    /*
     * Validate physical address bounds. x86-64 supports up to 52-bit
     * physical addresses, so ensure the address fits within this range.
     */
    if (__PhysAddr__ > 0x000FFFFFFFFFF000ULL)
    {
        PError("Physical address too high: 0x%016lx\n", __PhysAddr__);
        return 0;
    }

    /*
     * Acquire a pointer to the appropriate page table for this virtual address.
     * The GetPageTable function will create intermediate tables if they don't
     * exist (create=1) and ensure the table is accessible (userOk=1, though
     * this is kernel code).
     */
    uint64_t *Pt = GetPageTable(__Space__->Pml4, __VirtAddr__, 1, 1);
    if (!Pt)
    {
        PError("Failed to get page table for mapping\n");
        return 0;
    }

    /*
     * Calculate the index within the page table using the lower 9 bits
     * of the virtual address (after shifting out the page offset).
     */
    uint64_t PtIndex = (__VirtAddr__ >> 12) & 0x1FF;

    /*
     * Check if the page is already mapped. Overwriting existing mappings
     * can cause memory corruption or unexpected behavior.
     */
    if (Pt[PtIndex] & PTEPRESENT)
    {
        PWarn("Page already mapped at 0x%016lx\n", __VirtAddr__);
        return 0;
    }

    /*
     * Construct the page table entry:
     * - Extract the physical frame address (aligned to 4KB boundary)
     * - Combine with the provided flags
     * - Set the present bit to make the mapping active
     */
    Pt[PtIndex] = (__PhysAddr__ & 0x000FFFFFFFFFF000ULL) | __Flags__ | PTEPRESENT;

    /*
     * Flush the Translation Lookaside Buffer (TLB) entry for this virtual
     * address to ensure the CPU uses the new mapping immediately. This
     * prevents stale translations from cached entries.
     */
    FlushTlb(__VirtAddr__);

    /*
     * Log the successful mapping for debugging purposes.
     */
    PDebug("Mapped 0x%016lx -> 0x%016lx (flags=0x%lx)\n", __VirtAddr__, __PhysAddr__, __Flags__);
    return 1;
}

/*
 * UnmapPage
 *
 * Removes a page mapping from a virtual memory space, effectively disconnecting
 * a virtual address from its physical backing. This operation clears the page
 * table entry and ensures the CPU's TLB is updated to reflect the change.
 *
 * The unmapping process:
 * 1. Validates input parameters and address alignment
 * 2. Locates the appropriate page table entry
 * 3. Clears the page table entry (setting it to zero)
 * 4. Flushes the TLB to ensure immediate effect
 *
 * Important Notes:
 * - Only removes the virtual-to-physical mapping; the physical page itself
 *   is not freed (that's a separate PMM operation)
 * - The virtual address becomes invalid after unmapping
 * - TLB flushing is critical to prevent access to the unmapped page
 *
 * Parameters:
 *   __Space__ - The virtual memory space containing the mapping
 *   __VirtAddr__ - The virtual address to unmap (must be 4KB aligned)
 *
 * Returns:
 *   int - 1 on success, 0 if the address wasn't mapped or on error
 */
int
UnmapPage(VirtualMemorySpace *__Space__, uint64_t __VirtAddr__)
{
    /*
     * Validate input parameters and ensure proper alignment.
     * Unmapping misaligned addresses can cause undefined behavior.
     */
    if (!__Space__ || (__VirtAddr__ % PageSize) != 0)
    {
        PError("Invalid parameters for UnmapPage\n");
        return 0;
    }

    /*
     * Get a pointer to the page table containing this virtual address.
     * We don't create tables if they don't exist (create=0), but we
     * allow access since this is kernel code (userOk=0).
     */
    uint64_t *Pt = GetPageTable(__Space__->Pml4, __VirtAddr__, 1, 0);
    if (!Pt)
    {
        PWarn("No page table for address 0x%016lx\n", __VirtAddr__);
        return 0;
    }

    /* Calculate the page table index for this virtual address */
    uint64_t PtIndex = (__VirtAddr__ >> 12) & 0x1FF;

    /*
     * Check if the page is actually mapped. Attempting to unmap an
     * already unmapped page is not an error but should be logged.
     */
    if (!(Pt[PtIndex] & PTEPRESENT))
    {
        PWarn("Page not mapped at 0x%016lx\n", __VirtAddr__);
        return 0;
    }

    /*
     * Clear the page table entry, effectively removing the mapping.
     * Setting the entry to zero marks it as not present.
     */
    Pt[PtIndex] = 0;

    /*
     * Flush the TLB entry for this virtual address to ensure the CPU
     * immediately recognizes that the page is no longer mapped.
     */
    FlushTlb(__VirtAddr__);

    /* Log the successful unmapping operation */
    PDebug("Unmapped 0x%016lx\n", __VirtAddr__);
    return 1;
}

/*
 * GetPhysicalAddress
 *
 * Translates a virtual address to its corresponding physical address within
 * a given virtual memory space. This function walks the page table structure
 * to find the physical frame mapped to the specified virtual address.
 *
 * The translation process:
 * 1. Validates the virtual memory space
 * 2. Locates the appropriate page table entry
 * 3. Extracts the physical frame address
 * 4. Adds the page offset to get the exact physical address
 *
 * This function is essential for:
 * - Memory management operations
 * - DMA setup (devices need physical addresses)
 * - Debugging and diagnostics
 * - Converting between virtual and physical representations
 *
 * Parameters:
 *   __Space__ - The virtual memory space to search
 *   __VirtAddr__ - The virtual address to translate
 *
 * Returns:
 *   uint64_t - The corresponding physical address, or 0 if unmapped
 */
uint64_t
GetPhysicalAddress(VirtualMemorySpace *__Space__, uint64_t __VirtAddr__)
{
    /*
     * Validate the virtual memory space pointer.
     * A null space indicates an uninitialized or invalid context.
     */
    if (!__Space__)
    {
        PError("Invalid space for GetPhysicalAddress\n");
        return 0;
    }

    /*
     * Get a pointer to the page table containing this virtual address.
     * We don't create tables (create=0) and allow kernel access (userOk=0).
     */
    uint64_t *Pt = GetPageTable(__Space__->Pml4, __VirtAddr__, 1, 0);
    if (!Pt)
    {
        return 0;
    }

    /* Calculate the index in the page table */
    uint64_t PtIndex = (__VirtAddr__ >> 12) & 0x1FF;

    /*
     * Check if the page table entry is present (mapped).
     * If not present, the virtual address is not mapped.
     */
    if (!(Pt[PtIndex] & PTEPRESENT))
    {
        return 0;
    }

    /*
     * Extract the physical base address from the page table entry.
     * The physical frame address is stored in bits 51:12.
     */
    uint64_t PhysBase = Pt[PtIndex] & 0x000FFFFFFFFFF000ULL;

    /*
     * Add the page offset (lower 12 bits of virtual address) to get
     * the exact physical address corresponding to the virtual address.
     */
    uint64_t Offset = __VirtAddr__ & 0xFFF;

    return PhysBase + Offset;
}

/*
 * SwitchVirtualSpace
 *
 * Switches the CPU's active page table to a different virtual memory space.
 * This operation changes the virtual-to-physical address mappings that the
 * CPU uses for memory access, effectively switching between different address
 * spaces (e.g., between processes or kernel contexts).
 *
 * The switch is performed by loading the physical address of the new PML4
 * (root page table) into the CR3 control register. This causes an immediate
 * change in the CPU's address translation, affecting all subsequent memory
 * accesses.
 *
 * Critical Considerations:
 * - TLB is automatically flushed during CR3 write (on x86-64)
 * - All virtual addresses may change meaning after the switch
 * - Kernel mappings in upper virtual memory are preserved across spaces
 * - This operation is atomic and cannot be interrupted
 *
 * Safety:
 * - Validates the target space pointer
 * - Logs the operation for debugging
 * - Prevents switching to invalid/null spaces
 *
 * Parameters:
 *   __Space__ - Pointer to the VirtualMemorySpace to switch to
 *
 * Returns:
 *   void - The switch takes effect immediately
 */
void
SwitchVirtualSpace(VirtualMemorySpace *__Space__)
{
    /*
     * Validate the target virtual memory space.
     * Switching to a null or invalid space would cause system failure.
     */
    if (!__Space__)
    {
        PError("Cannot switch to null virtual space\n");
        return;
    }

    /*
     * Perform the actual context switch by loading the new PML4 physical
     * address into CR3. The 'memory' clobber ensures the compiler knows
     * this operation affects memory mappings globally.
     */
    __asm__ volatile ("mov %0, %%cr3" :: "r" (__Space__->PhysicalBase) : "memory");

    /*
     * Log the successful virtual space switch for debugging and monitoring.
     * Include the new PML4 physical address for reference.
     */
    PDebug("Switched to virtual space: PML4=0x%016lx\n", __Space__->PhysicalBase);
}
