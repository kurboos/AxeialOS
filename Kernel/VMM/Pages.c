#include <VMM.h>

/*
 * GetPageTable
 *
 * This function is a core component of the Virtual Memory Manager (VMM), responsible
 * for navigating and potentially constructing the hierarchical page table structure
 * used in x86-64 architecture. The page tables form a four-level tree: Page Map Level 4 (PML4),
 * Page Directory Pointer Table (PDPT), Page Directory (PD), and Page Table (PT).
 *
 * The function takes a virtual address and traverses down to the specified level,
 * creating intermediate page tables if requested. This is essential for mapping
 * virtual addresses to physical frames, enabling virtual memory functionality.
 *
 * Key Concepts:
 * - x86-64 uses 4-level paging with 9-bit indices at each level
 * - Virtual addresses are 48-bit (canonical), split into 4x9-bit indices + 12-bit offset
 * - Each level can point to the next level or directly to huge pages (2MB/1GB)
 *
 * Parameters:
 *   __Pml4__ - Pointer to the root PML4 table (must be virtually accessible)
 *   __VirtAddr__ - The virtual address to resolve in the page table hierarchy
 *   __Level__ - Target level in the hierarchy:
 *               1 = Page Table (PT) - contains individual 4KB mappings
 *               2 = Page Directory (PD) - can contain 2MB huge pages
 *               3 = Page Directory Pointer Table (PDPT) - can contain 1GB huge pages
 *               4 = Page Map Level 4 (PML4) - root of the hierarchy
 *   __Create__ - Boolean flag controlling behavior:
 *                1 = Create missing intermediate tables
 *                0 = Return NULL if any intermediate table is missing
 *
 * Returns:
 *   uint64_t* - Pointer to the page table at the requested level, or NULL if:
 *               - The path doesn't exist and creation is disabled
 *               - Memory allocation fails during table creation
 *               - Invalid parameters are provided
 *
 * Memory Allocation:
 *   When creating tables, allocates 4KB pages from the Physical Memory Manager (PMM)
 *   Newly allocated tables are zero-initialized to ensure clean state
 *
 * Error Handling:
 *   - Validates presence of entries at each level
 *   - Handles allocation failures gracefully
 *   - Logs errors for debugging purposes
 *
 * Usage Context:
 *   Called by higher-level VMM functions like MapPage() and UnmapPage() to
 *   access the appropriate page table entries for virtual address operations.
 */
uint64_t*
GetPageTable(uint64_t *__Pml4__, uint64_t __VirtAddr__, int __Level__, int __Create__)
{
    /*
     * Extract the 9-bit indices for each level of the page table hierarchy.
     * x86-64 virtual address breakdown (48-bit canonical):
     * Bits 47:39 = PML4 index (9 bits)
     * Bits 38:30 = PDPT index (9 bits)
     * Bits 29:21 = PD index (9 bits)
     * Bits 20:12 = PT index (9 bits)
     * Bits 11:0  = Page offset (12 bits)
     */
    uint32_t Pml4Index = (__VirtAddr__ >> 39) & 0x1FF;
    uint32_t PdptIndex = (__VirtAddr__ >> 30) & 0x1FF;
    uint32_t PdIndex   = (__VirtAddr__ >> 21) & 0x1FF;

    /*
     * Initialize traversal starting from the PML4 (root level).
     * CurrentTable points to the table we're currently examining.
     * CurrentIndex is the entry index within that table.
     */
    uint64_t *CurrentTable = __Pml4__;
    uint32_t CurrentIndex = Pml4Index;

    /*
     * Traverse down the page table hierarchy from level 4 (PML4) towards
     * the target level. At each level, check if the entry exists, and
     * create it if requested and necessary.
     *
     * The loop runs while Level > __Level__, meaning we stop one level
     * above the target to return the correct table pointer.
     */
    for (int Level = 4; Level > __Level__; Level--)
    {
        /*
         * Check if the current table entry is present (marked with PTEPRESENT).
         * If not present, we need to either create the next level table or fail.
         */
        if (!(CurrentTable[CurrentIndex] & PTEPRESENT))
        {
            /*
             * If creation is not requested, return NULL immediately.
             * This allows read-only operations to fail gracefully.
             */
            if (!__Create__)
            {
                return NULL;
            }

            /*
             * Allocate a new 4KB page to serve as the next level page table.
             * This page will contain 512 entries (8 bytes each = 4096 bytes).
             */
            uint64_t NewTablePhys = AllocPage();
            if (!NewTablePhys)
            {
                /*
                 * Allocation failure - log error and return NULL.
                 * This prevents system instability from partial table creation.
                 */
                PError("Failed to allocate page table at level %d\n", Level - 1);
                return NULL;
            }

            /*
             * Convert the physical address to a virtual address using HHDM
             * so the kernel can access and initialize the new table.
             */
            uint64_t *NewTable = (uint64_t*)PhysToVirt(NewTablePhys);

            /*
             * Zero-initialize all 512 entries in the new page table.
             * This ensures no stale data and provides a clean slate for mappings.
             */
            for (uint32_t Index = 0; Index < PageTableEntries; Index++)
            {
                NewTable[Index] = 0;
            }

            /*
             * Link the new table into the current level's entry.
             * Set flags: PTEPRESENT (entry is valid), PTEWRITABLE (allow writes),
             * PTEUSER (allow user-mode access - though kernel code may restrict this).
             */
            CurrentTable[CurrentIndex] = NewTablePhys | PTEPRESENT | PTEWRITABLE | PTEUSER;

            /*
             * Log the creation for debugging and memory usage tracking.
             */
            PDebug("Created page table at level %d: 0x%016lx\n", Level - 1, NewTablePhys);
        }

        /*
         * Move to the next level table. Extract the physical address of the
         * next table from the current entry (masking out flag bits).
         */
        uint64_t NextTablePhys = CurrentTable[CurrentIndex] & 0xFFFFFFFFFFFFF000ULL;

        /*
         * Convert to virtual address for continued traversal.
         */
        CurrentTable = (uint64_t*)PhysToVirt(NextTablePhys);

        /*
         * Update the index for the next level based on the virtual address.
         * This determines which entry in the next table to examine.
         *
         * Note: Using a switch statement for clarity, though an enum could
         * provide better type safety in larger codebases.
         */
        switch (Level - 1)
        {
            case 3: /* Moving to PDPT level */
                CurrentIndex = PdptIndex;
                break;

            case 2: /* Moving to PD level */
                CurrentIndex = PdIndex;
                break;

            case 1: /* Reached PT level - this is our target */
                return CurrentTable;
        }
    }

    /*
     * If we exit the loop without returning, it means the target level
     * was 4 (PML4), so we return the current table (which is the PML4).
     */
    return CurrentTable;
}

/*
 * FlushTlb
 *
 * Invalidates a specific entry in the Translation Lookaside Buffer (TLB),
 * which is the CPU's cache for virtual-to-physical address translations.
 * This ensures that changes to page table entries take effect immediately.
 *
 * The TLB caches recent translations to improve performance, but when
 * page tables are modified (map/unmap operations), stale entries must
 * be flushed to prevent incorrect memory access.
 *
 * This function uses the INVLPG instruction, which invalidates the TLB
 * entry for a specific virtual address without affecting other entries.
 *
 * Parameters:
 *   __VirtAddr__ - The virtual address whose TLB entry should be invalidated.
 *                  Must be aligned to the page boundary for correct operation.
 *
 * Side Effects:
 *   - Causes a TLB flush for the specified address
 *   - May impact performance due to cache invalidation
 *   - Ensures memory consistency after page table modifications
 *
 * Usage:
 *   Called after MapPage() and UnmapPage() operations to ensure the CPU
 *   sees the updated mappings immediately.
 */
void
FlushTlb(uint64_t __VirtAddr__)
{
    /*
     * Execute the INVLPG instruction to invalidate the TLB entry.
     * The 'volatile' keyword prevents compiler optimization that might
     * remove this instruction. The 'memory' clobber tells the compiler
     * that this operation affects all memory accesses.
     */
    __asm__ volatile ("invlpg (%0)" :: "r" (__VirtAddr__) : "memory");
}

/*
 * FlushAllTlb
 *
 * Performs a complete flush of the Translation Lookaside Buffer (TLB),
 * invalidating all cached virtual-to-physical address translations.
 * This is a more expensive operation than flushing a single entry but
 * ensures complete consistency when multiple mappings have changed.
 *
 * The function works by reloading the CR3 register with its current value,
 * which forces the CPU to flush the entire TLB as a side effect of the
 * context switch operation.
 *
 * Use Cases:
 *   - After major page table restructuring
 *   - When switching virtual memory spaces
 *   - During system initialization or cleanup
 *   - When debugging memory mapping issues
 *
 * Performance Impact:
 *   - Significantly more expensive than single-entry flush
 *   - Causes temporary performance degradation due to TLB misses
 *   - Should be used sparingly compared to targeted flushes
 *
 * Parameters:
 *   None - Operates on the current CPU's TLB
 *
 * Returns:
 *   void - TLB flush is performed as a side effect
 */
void
FlushAllTlb(void)
{
    uint64_t Cr3;

    /*
     * Read the current value of CR3, which contains the physical address
     * of the current PML4 table plus control bits.
     */
    __asm__ volatile ("mov %%cr3, %0" : "=r" (Cr3));

    /*
     * Write the same value back to CR3. This operation doesn't change
     * the page tables but triggers a complete TLB flush as a side effect.
     * The 'memory' clobber ensures the compiler understands this affects
     * memory access patterns globally.
     */
    __asm__ volatile ("mov %0, %%cr3" :: "r" (Cr3) : "memory");
}
