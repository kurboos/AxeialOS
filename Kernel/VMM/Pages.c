#include <VMM.h>

/**
 * @brief Retrieve or create a page table for a given virtual address.
 *
 * @details Walks the page table hierarchy starting from the provided PML4,
 * 			down to the requested level (PDPT, PD, or PT). If the required
 * 			intermediate tables are missing and @p __Create__ is non-zero,
 * 			new tables are allocated and initialized.
 *
 * @param __Pml4__    Pointer to the root Page Map Level 4 (PML4).
 * @param __VirtAddr__ Virtual address for which the page table is needed.
 * @param __Level__   Target level (1 = PT, 2 = PD, 3 = PDPT).
 * @param __Create__  If non-zero, missing tables are created.
 *
 * @return Pointer to the page table at the requested level,
 *         or NULL if not present and @p __Create__ is zero.
 *
 * @note This function is critical for mapping and unmapping pages.
 */
uint64_t*
GetPageTable(uint64_t* __Pml4__, uint64_t __VirtAddr__, int __Level__, int __Create__)
{

    uint32_t Pml4Index = (__VirtAddr__ >> 39) & 0x1FF;
    uint32_t PdptIndex = (__VirtAddr__ >> 30) & 0x1FF;
    uint32_t PdIndex   = (__VirtAddr__ >> 21) & 0x1FF;

    uint64_t* CurrentTable = __Pml4__;
    uint32_t  CurrentIndex = Pml4Index;

    for (int Level = 4; Level > __Level__; Level--)
    {

        if (!(CurrentTable[CurrentIndex] & PTEPRESENT))
        {

            if (!__Create__)
            {
                return NULL;
            }

            uint64_t NewTablePhys = AllocPage();
            if (!NewTablePhys)
            {

                PError("Failed to allocate page table at level %d\n", Level - 1);
                return NULL;
            }

            uint64_t* NewTable = (uint64_t*)PhysToVirt(NewTablePhys);

            for (uint32_t Index = 0; Index < PageTableEntries; Index++)
            {
                NewTable[Index] = 0;
            }

            CurrentTable[CurrentIndex] = NewTablePhys | PTEPRESENT | PTEWRITABLE | PTEUSER;

            PDebug("Created page table at level %d: 0x%016lx\n", Level - 1, NewTablePhys);
        }

        uint64_t NextTablePhys = CurrentTable[CurrentIndex] & 0xFFFFFFFFFFFFF000ULL;

        CurrentTable = (uint64_t*)PhysToVirt(NextTablePhys);

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

    return CurrentTable;
}

/**
 * @brief Flush a single TLB entry.
 *
 * @details Invalidates the Translation Lookaside Buffer (TLB) entry
 * 			corresponding to the given virtual address. This ensures
 * 			that subsequent memory accesses use updated page table mappings.
 *
 * @param __VirtAddr__ Virtual address whose TLB entry should be flushed.
 *
 * @return void
 */
void
FlushTlb(uint64_t __VirtAddr__)
{

    __asm__ volatile("invlpg (%0)" ::"r"(__VirtAddr__) : "memory");
}

/**
 * @brief Flush the entire TLB.
 *
 * @details Reloads the CR3 register to invalidate all cached TLB entries.
 * 			This forces the CPU to re-read page table mappings from memory.
 *
 * @return void
 *
 * @note This operation affects all virtual addresses and should
 *       be used with caution.
 */
void
FlushAllTlb(void)
{
    uint64_t Cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(Cr3));

    __asm__ volatile("mov %0, %%cr3" ::"r"(Cr3) : "memory");
}
