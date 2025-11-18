#include <VMM.h>

/**
 * @brief Check if a physical address is valid.
 *
 * @details Ensures the address is non-zero, page-aligned,
 *          and lies within a PMM-managed memory region.
 *
 * @param __PhysAddr__ Physical address to validate.
 * @return 1 if valid, 0 otherwise.
 */
static int
IsValidPhysicalAddress(uint64_t __PhysAddr__)
{

    if (__PhysAddr__ == 0)
    {
        return 0;
    }

    if ((__PhysAddr__ & 0xFFF) != 0)
    {
        return 0;
    }

    for (uint32_t Index = 0; Index < Pmm.RegionCount; Index++)
    {

        uint64_t RegionStart = Pmm.Regions[Index].Base;
        uint64_t RegionEnd   = RegionStart + Pmm.Regions[Index].Length;

        if (__PhysAddr__ >= RegionStart && __PhysAddr__ < RegionEnd)
        {
            return 1; /* Address is valid within this region */
        }
    }

    return 0;
}

/**
 * @brief Check if a virtual address belongs to the HHDM.
 *
 * @details Converts the virtual address to its physical counterpart
 *          using the HHDM offset, then validates it.
 *
 * @param __VirtAddr__ Virtual address to validate.
 * @return 1 if valid, 0 otherwise.
 */
static int
IsValidHhdmAddress(uint64_t __VirtAddr__)
{

    if (__VirtAddr__ < Vmm.HhdmOffset)
    {
        return 0;
    }

    uint64_t PhysAddr = __VirtAddr__ - Vmm.HhdmOffset;

    return IsValidPhysicalAddress(PhysAddr);
}

/**
 * @brief Check if a pointer is safe to dereference.
 *
 * @details Validates that the pointer is non-null and maps
 *          to a valid HHDM address.
 *
 * @param __Ptr__ Pointer to validate.
 * @return 1 if safe, 0 otherwise.
 */
static int
IsSafeToAccess(uint64_t* __Ptr__)
{

    if (!__Ptr__)
    {
        return 0;
    }

    uint64_t VirtAddr = (uint64_t)__Ptr__;

    return IsValidHhdmAddress(VirtAddr);
}

/**
 * @brief Dump information about a virtual memory space.
 *
 * @details Prints PML4 addresses, reference count, and
 *          counts mapped pages by walking the page tables.
 *
 * @param __Space__ Pointer to the VirtualMemorySpace structure.
 * @return void
 */
void
VmmDumpSpace(VirtualMemorySpace* __Space__)
{

    if (!__Space__)
    {
        PError("Cannot dump null virtual space\n");
        return;
    }

    if (!IsValidPhysicalAddress(__Space__->PhysicalBase))
    {
        PError("Invalid PML4 physical address: 0x%016lx\n", __Space__->PhysicalBase);
        return;
    }

    if (!__Space__->Pml4 || !IsValidHhdmAddress((uint64_t)__Space__->Pml4))
    {
        PError("Invalid PML4 virtual address: 0x%016lx\n", (uint64_t)__Space__->Pml4);
        return;
    }

    PInfo("Virtual Memory Space Information:\n");
    KrnPrintf("  PML4 Physical: 0x%016lx\n", __Space__->PhysicalBase);
    KrnPrintf("  PML4 Virtual:  0x%016lx\n", (uint64_t)__Space__->Pml4);
    KrnPrintf("  Reference Count: %u\n", __Space__->RefCount);

    uint64_t MappedPages     = 0;
    uint64_t ValidatedTables = 0;
    uint64_t SkippedTables   = 0;

    for (uint64_t Pml4Index = 0; Pml4Index < PageTableEntries; Pml4Index++)
    {
        uint64_t Pml4Entry = __Space__->Pml4[Pml4Index];

        /* Skip non-present entries (not mapped) */
        if (!(Pml4Entry & PTEPRESENT))
        {
            continue;
        }

        uint64_t PdptPhys = Pml4Entry & 0x000FFFFFFFFFF000ULL;
        if (!IsValidPhysicalAddress(PdptPhys))
        {
            SkippedTables++;
            continue;
        }

        uint64_t* Pdpt = (uint64_t*)PhysToVirt(PdptPhys);
        if (!IsSafeToAccess(Pdpt))
        {
            SkippedTables++;
            continue;
        }

        ValidatedTables++;

        for (uint64_t PdptIndex = 0; PdptIndex < PageTableEntries; PdptIndex++)
        {
            uint64_t PdptEntry = Pdpt[PdptIndex];

            if (!(PdptEntry & PTEPRESENT))
            {
                continue;
            }

            if (PdptEntry & PTEHUGEPAGE)
            {
                MappedPages += 262144; /* 1GB / 4KB = 262144 pages */
                continue;
            }

            uint64_t PdPhys = PdptEntry & 0x000FFFFFFFFFF000ULL;
            if (!IsValidPhysicalAddress(PdPhys))
            {
                continue;
            }

            uint64_t* Pd = (uint64_t*)PhysToVirt(PdPhys);
            if (!IsSafeToAccess(Pd))
            {
                continue;
            }

            for (uint64_t PdIndex = 0; PdIndex < PageTableEntries; PdIndex++)
            {
                uint64_t PdEntry = Pd[PdIndex];

                if (!(PdEntry & PTEPRESENT))
                {
                    continue;
                }

                if (PdEntry & PTEHUGEPAGE)
                {
                    MappedPages += 512; /* 2MB / 4KB = 512 pages */
                    continue;
                }

                uint64_t PtPhys = PdEntry & 0x000FFFFFFFFFF000ULL;
                if (!IsValidPhysicalAddress(PtPhys))
                {
                    continue;
                }

                uint64_t* Pt = (uint64_t*)PhysToVirt(PtPhys);
                if (!IsSafeToAccess(Pt))
                {
                    continue;
                }

                for (uint64_t PtIndex = 0; PtIndex < PageTableEntries; PtIndex++)
                {
                    if (Pt[PtIndex] & PTEPRESENT)
                    {
                        MappedPages++;
                    }
                }
            }
        }
    }

    KrnPrintf("  Validated Tables: %lu\n", ValidatedTables);
    KrnPrintf("  Skipped Tables: %lu\n", SkippedTables);
    KrnPrintf("  Mapped Pages: %lu (%lu KB)\n", MappedPages, MappedPages * 4);
}

/**
 * @brief Dump overall VMM statistics.
 *
 * @details Prints HHDM offset, kernel PML4, memory map regions,
 *          and kernel space information if available.
 *
 * @return void
 */
void
VmmDumpStats(void)
{

    if (!Vmm.HhdmOffset)
    {
        PError("VMM not properly initialized - no HHDM offset\n");
        return;
    }

    PInfo("VMM Statistics:\n");
    KrnPrintf("  HHDM Offset: 0x%016lx\n", Vmm.HhdmOffset);
    KrnPrintf("  Kernel PML4: 0x%016lx\n", Vmm.KernelPml4Physical);

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

    if (Vmm.KernelSpace)
    {
        KrnPrintf("  Kernel Space: 0x%016lx\n", (uint64_t)Vmm.KernelSpace);
        VmmDumpSpace(Vmm.KernelSpace);
    }
    else
    {
        PWarn("  No kernel space available\n"); /*Impossible*/
    }
}
