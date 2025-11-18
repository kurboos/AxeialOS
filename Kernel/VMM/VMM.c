#include <VMM.h>

/** @brief Global VMM State. */
VirtualMemoryManager Vmm = {0};

/**
 * @brief Initialize the Virtual Memory Manager (VMM).
 *
 * @details Sets up the kernel’s virtual memory environment by:
 * 			Reading the current CR3 register to determine the kernel PML4.
 * 			Allocating and initializing the kernel’s VirtualMemorySpace structure.
 * 			Linking physical and virtual addresses for the PML4.
 *
 * @return void
 *
 * @note Must be called before creating or switching virtual spaces.
 */
void
InitializeVmm(void)
{

    PInfo("Initializing Virtual Memory Manager...\n");

    Vmm.HhdmOffset = Pmm.HhdmOffset;
    PDebug("Using HHDM offset: 0x%016lx\n", Vmm.HhdmOffset);

    uint64_t CurrentCr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(CurrentCr3));
    Vmm.KernelPml4Physical = CurrentCr3 & 0xFFFFFFFFFFFFF000ULL; /* Clear lower 12 bits */

    PDebug("Current PML4 at: 0x%016lx\n", Vmm.KernelPml4Physical);

    Vmm.KernelSpace = (VirtualMemorySpace*)PhysToVirt(AllocPage());
    if (!Vmm.KernelSpace)
    {
        PError("Failed to allocate kernel virtual space\n");
        return;
    }

    Vmm.KernelSpace->PhysicalBase = Vmm.KernelPml4Physical; /* Physical address of PML4 */
    Vmm.KernelSpace->Pml4 =
        (uint64_t*)PhysToVirt(Vmm.KernelPml4Physical); /* Virtual address for PML4 */
    Vmm.KernelSpace->RefCount = 1;                     /* Initialize reference count */

    PSuccess("VMM initialized with kernel space at 0x%016lx\n", Vmm.KernelPml4Physical);
}

/**
 * @brief Create a new virtual memory space.
 *
 * @details Allocates a new VirtualMemorySpace structure and a fresh PML4 table.
 * 			Initializes the PML4 entries, copying kernel mappings for higher-half
 * 			addresses.
 *
 * @return Pointer to the newly created VirtualMemorySpace, or NULL on failure.
 *
 * @note Caller is responsible for destroying the space when no longer needed.
 */
VirtualMemorySpace*
CreateVirtualSpace(void)
{

    if (!Vmm.KernelSpace || !Vmm.KernelSpace->Pml4)
    {
        PError("VMM not properly initialized\n");
        return 0;
    }

    uint64_t SpacePhys = AllocPage();
    if (!SpacePhys)
    {
        PError("Failed to allocate virtual space structure\n");
        return 0;
    }

    VirtualMemorySpace* Space = (VirtualMemorySpace*)PhysToVirt(SpacePhys);
    if (!Space)
    {
        PError("HHDM conversion failed for space structure\n");
        FreePage(SpacePhys);
        return 0;
    }

    uint64_t Pml4Phys = AllocPage();
    if (!Pml4Phys)
    {
        PError("Failed to allocate PML4\n");
        FreePage(SpacePhys);
        return 0;
    }

    Space->PhysicalBase = Pml4Phys;
    Space->Pml4         = (uint64_t*)PhysToVirt(Pml4Phys);
    Space->RefCount     = 1;

    if (!Space->Pml4)
    {
        PError("HHDM conversion failed for PML4\n");
        FreePage(SpacePhys);
        FreePage(Pml4Phys);
        return 0;
    }

    for (uint64_t Index = 0; Index < PageTableEntries; Index++)
    {
        Space->Pml4[Index] = 0;
    }

    for (uint64_t Index = 256; Index < PageTableEntries; Index++)
    {
        Space->Pml4[Index] = Vmm.KernelSpace->Pml4[Index];
    }

    PDebug("Created virtual space: PML4=0x%016lx\n", Pml4Phys);
    return Space;
}

/**
 * @brief Destroy a virtual memory space.
 *
 * @details Decrements the reference count of the given space. If the count reaches zero,
 * 			frees all associated page tables and the space structure itself.
 *
 * @param __Space__ Pointer to the VirtualMemorySpace to destroy.
 *
 * @return void
 *
 * @warning Cannot destroy the kernel’s own space.
 */
void
DestroyVirtualSpace(VirtualMemorySpace* __Space__)
{

    if (!__Space__ || __Space__ == Vmm.KernelSpace)
    {
        PWarn("Cannot destroy kernel space or null space\n");
        return;
    }

    __Space__->RefCount--;
    if (__Space__->RefCount > 0)
    {
        PDebug("Virtual space still has %u references\n", __Space__->RefCount);
        return;
    }

    PDebug("Destroying virtual space: PML4=0x%016lx\n", __Space__->PhysicalBase);

    for (uint64_t Pml4Index = 0; Pml4Index < 256; Pml4Index++)
    {
        /* Skip entries that are not present (not mapped) */
        if (!(__Space__->Pml4[Pml4Index] & PTEPRESENT))
        {
            continue;
        }

        uint64_t  PdptPhys = __Space__->Pml4[Pml4Index] & 0x000FFFFFFFFFF000ULL;
        uint64_t* Pdpt     = (uint64_t*)PhysToVirt(PdptPhys);
        if (!Pdpt)
        {
            continue;
        }

        for (uint64_t PdptIndex = 0; PdptIndex < PageTableEntries; PdptIndex++)
        {
            if (!(Pdpt[PdptIndex] & PTEPRESENT))
            {
                continue;
            }

            uint64_t  PdPhys = Pdpt[PdptIndex] & 0x000FFFFFFFFFF000ULL;
            uint64_t* Pd     = (uint64_t*)PhysToVirt(PdPhys);
            if (!Pd)
            {
                continue;
            }

            for (uint64_t PdIndex = 0; PdIndex < PageTableEntries; PdIndex++)
            {
                if (!(Pd[PdIndex] & PTEPRESENT))
                {
                    continue;
                }

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

    FreePage(VirtToPhys(__Space__));

    PDebug("Virtual space destroyed\n");
}

/**
 * @brief Map a physical page into a virtual memory space.
 *
 * @details Inserts a mapping from a virtual address to a physical address in the given space’s page
 * tables. Ensures alignment and validity of addresses before mapping.
 *
 * @param __Space__   Target VirtualMemorySpace.
 * @param __VirtAddr__ Virtual address to map.
 * @param __PhysAddr__ Physical address to map to.
 * @param __Flags__   Page table flags (writable, user-accessible).
 *
 * @return 1 on success, 0 on failure.
 */
int
MapPage(VirtualMemorySpace* __Space__,
        uint64_t            __VirtAddr__,
        uint64_t            __PhysAddr__,
        uint64_t            __Flags__)
{

    if (!__Space__ || (__VirtAddr__ % PageSize) != 0 || (__PhysAddr__ % PageSize) != 0)
    {
        PError("Invalid parameters for MapPage\n");
        return 0;
    }

    if (__PhysAddr__ > 0x000FFFFFFFFFF000ULL)
    {
        PError("Physical address too high: 0x%016lx\n", __PhysAddr__);
        return 0;
    }

    uint64_t* Pt = GetPageTable(__Space__->Pml4, __VirtAddr__, 1, 1);
    if (!Pt)
    {
        PError("Failed to get page table for mapping\n");
        return 0;
    }

    uint64_t PtIndex = (__VirtAddr__ >> 12) & 0x1FF;

    if (Pt[PtIndex] & PTEPRESENT)
    {
        PWarn("Page already mapped at 0x%016lx\n", __VirtAddr__);
        return 0;
    }

    Pt[PtIndex] = (__PhysAddr__ & 0x000FFFFFFFFFF000ULL) | __Flags__ | PTEPRESENT;

    FlushTlb(__VirtAddr__);

    PDebug("Mapped 0x%016lx -> 0x%016lx (flags=0x%lx)\n", __VirtAddr__, __PhysAddr__, __Flags__);
    return 1;
}

/**
 * @brief Unmap a virtual page from a memory space.
 *
 * @details Removes the mapping for the given virtual address, invalidates the TLB entry,
 * 			and frees the physical page if necessary.
 *
 * @param __Space__   Target VirtualMemorySpace.
 * @param __VirtAddr__ Virtual address to unmap.
 *
 * @return 1 on success, 0 if the page was not mapped or parameters are invalid.
 */
int
UnmapPage(VirtualMemorySpace* __Space__, uint64_t __VirtAddr__)
{

    if (!__Space__ || (__VirtAddr__ % PageSize) != 0)
    {
        PError("Invalid parameters for UnmapPage\n");
        return 0;
    }

    uint64_t* Pt = GetPageTable(__Space__->Pml4, __VirtAddr__, 1, 0);
    if (!Pt)
    {
        PWarn("No page table for address 0x%016lx\n", __VirtAddr__);
        return 0;
    }

    /* Calculate the page table index for this virtual address */
    uint64_t PtIndex = (__VirtAddr__ >> 12) & 0x1FF;

    if (!(Pt[PtIndex] & PTEPRESENT))
    {
        PWarn("Page not mapped at 0x%016lx\n", __VirtAddr__);
        return 0;
    }

    Pt[PtIndex] = 0;

    FlushTlb(__VirtAddr__);

    /* Log the successful unmapping operation */
    PDebug("Unmapped 0x%016lx\n", __VirtAddr__);
    return 1;
}

/**
 * @brief Resolve the physical address for a given virtual address.
 *
 * @details Walks the page tables of the given space to find the physical address
 * 			corresponding to the provided virtual address.
 *
 * @param __Space__   Target VirtualMemorySpace.
 * @param __VirtAddr__ Virtual address to resolve.
 *
 * @return Physical address if mapped, 0 otherwise.
 */
uint64_t
GetPhysicalAddress(VirtualMemorySpace* __Space__, uint64_t __VirtAddr__)
{

    if (!__Space__)
    {
        PError("Invalid space for GetPhysicalAddress\n");
        return 0;
    }

    uint64_t* Pt = GetPageTable(__Space__->Pml4, __VirtAddr__, 1, 0);
    if (!Pt)
    {
        return 0;
    }

    /* Calculate the index in the page table */
    uint64_t PtIndex = (__VirtAddr__ >> 12) & 0x1FF;

    if (!(Pt[PtIndex] & PTEPRESENT))
    {
        return 0;
    }

    uint64_t PhysBase = Pt[PtIndex] & 0x000FFFFFFFFFF000ULL;

    uint64_t Offset = __VirtAddr__ & 0xFFF;

    return PhysBase + Offset;
}

/**
 * @brief Switch the CPU to a new virtual memory space.
 *
 * @details Loads the CR3 register with the physical base of the given space’s PML4,
 * 			effectively switching the active page tables.
 *
 * @param __Space__ Target VirtualMemorySpace to switch to.
 *
 * @return void
 *
 * @note This affects all subsequent memory accesses by the CPU.
 *
 * @deprecated Not used often
 */
void
SwitchVirtualSpace(VirtualMemorySpace* __Space__)
{

    if (!__Space__)
    {
        PError("Cannot switch to null virtual space\n");
        return;
    }

    __asm__ volatile("mov %0, %%cr3" ::"r"(__Space__->PhysicalBase) : "memory");

    PDebug("Switched to virtual space: PML4=0x%016lx\n", __Space__->PhysicalBase);
}
