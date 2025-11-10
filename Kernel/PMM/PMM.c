#include <PMM.h>

/*
 * Global PMM State
 *
 * Pmm - The main Physical Memory Manager structure containing all state
 *       information including memory regions, bitmap, and statistics
 */
PhysicalMemoryManager Pmm = {0};

/*
 * FindFreePage - Locate an available physical page
 *
 * This function searches for a free page using a linear search with allocation
 * hint optimization. It starts from the last allocation hint to improve cache
 * locality and reduce search time for consecutive allocations.
 *
 * Returns:
 *   Page index if found, PmmBitmapNotFound if no free pages available
 *
 * Algorithm:
 * 1. Search from LastAllocHint forward to end of memory
 * 2. If not found, search from beginning to LastAllocHint
 * 3. Update LastAllocHint to next page after found page
 */
uint64_t
FindFreePage(void)
{
    uint64_t StartHint = Pmm.LastAllocHint;

    /*Search from hint forward to end of memory*/
    for (uint64_t Index = StartHint; Index < Pmm.TotalPages; Index++)
    {
        if (!TestBitmapBit(Index))
        {
            Pmm.LastAllocHint = Index + 1;
            return Index;
        }
    }

    /*Search from beginning to hint if not found above*/
    for (uint64_t Index = 0; Index < StartHint; Index++)
    {
        if (!TestBitmapBit(Index))
        {
            Pmm.LastAllocHint = Index + 1;
            return Index;
        }
    }

    return PmmBitmapNotFound;
}

/*
 * InitializePmm - Set up the Physical Memory Manager
 *
 * This function initializes the PMM by:
 * 1. Retrieving the Higher Half Direct Mapping offset from Limine
 * 2. Parsing the system memory map to identify usable/reserved regions
 * 3. Setting up the allocation bitmap in physical memory
 * 4. Marking memory regions as used/free according to their type
 * 5. Calculating and displaying memory statistics
 *
 * The initialization must occur early in the boot process, before any
 * dynamic memory allocation can take place.
 */
void
InitializePmm(void)
{
    PInfo("Initializing Physical Memory Manager...\n");

    /*Retrieve HHDM offset for address translation*/
    if (!HhdmRequest.response)
    {
        PError("Failed to get HHDM from Limine\n");
        return;
    }
    Pmm.HhdmOffset = HhdmRequest.response->offset;
    PDebug("HHDM offset: 0x%016lx\n", Pmm.HhdmOffset);

    /*Parse system memory map from bootloader*/
    ParseMemoryMap();
    if (Pmm.RegionCount == 0)
    {
        PError("No memory regions found\n");
        return;
    }

    /*Initialize the allocation bitmap*/
    InitializeBitmap();
    if (Pmm.Bitmap == 0)
    {
        PError("Failed to initialize PMM bitmap\n");
        return;
    }

    /*Mark memory regions as used/free based on their type*/
    MarkMemoryRegions();

    /*Calculate final memory statistics*/
    Pmm.Stats.TotalPages = Pmm.TotalPages;
    Pmm.Stats.UsedPages = 0;
    Pmm.Stats.FreePages = 0;

    for (uint64_t Index = 0; Index < Pmm.TotalPages; Index++)
    {
        if (TestBitmapBit(Index))
            Pmm.Stats.UsedPages++;
        else
            Pmm.Stats.FreePages++;
    }

    PSuccess("PMM initialized: %lu MB total, %lu MB free\n",
    (Pmm.Stats.TotalPages * PageSize) / (1024 * 1024),
    (Pmm.Stats.FreePages * PageSize) / (1024 * 1024));
}

/*
 * AllocPage - Allocate a single physical page
 *
 * Allocates one 4KB physical page and returns its physical address.
 * This is the most common allocation function used by higher-level
 * memory managers.
 *
 * Returns:
 *   Physical address of allocated page, or 0 on failure
 */
uint64_t
AllocPage(void)
{
    uint64_t PageIndex = FindFreePage();

    if (PageIndex == PmmBitmapNotFound)
    {
        PError("Out of physical memory - no free pages available\n");
        return 0;
    }

    /*Mark page as used in bitmap*/
    SetBitmapBit(PageIndex);
    Pmm.Stats.UsedPages++;
    Pmm.Stats.FreePages--;

    uint64_t PhysAddr = PageIndex * PageSize;
    PDebug("Allocated page: 0x%016lx (index %lu)\n", PhysAddr, PageIndex);

    return PhysAddr;
}

/*
 * FreePage - Release a single physical page
 *
 * Frees a previously allocated physical page, making it available for
 * future allocations. Performs validation to prevent double-free and
 * invalid address errors.
 *
 * Parameters:
 *   __PhysAddr__ - Physical address of the page to free (must be page-aligned)
 */
void
FreePage(uint64_t __PhysAddr__)
{
    if (!PmmValidatePage(__PhysAddr__))
    {
        PError("Invalid physical address for free: 0x%016lx\n", __PhysAddr__);
        return;
    }

    uint64_t PageIndex = __PhysAddr__ / PageSize;

    if (!TestBitmapBit(PageIndex))
    {
        PError("Double free detected at: 0x%016lx\n", __PhysAddr__);
        return;
    }

    /*Mark page as free in bitmap*/
    ClearBitmapBit(PageIndex);
    Pmm.Stats.UsedPages--;
    Pmm.Stats.FreePages++;

    PDebug("Freed page: 0x%016lx (index %lu)\n", __PhysAddr__, PageIndex);
}

/*
 * AllocPages - Allocate multiple contiguous physical pages
 *
 * Allocates a contiguous block of physical pages. This is useful for
 * large allocations that require physically contiguous memory, such
 * as DMA buffers or page tables.
 *
 * Parameters:
 *   __Count__ - Number of contiguous pages to allocate
 *
 * Returns:
 *   Physical address of first page in block, or 0 on failure
 */
uint64_t
AllocPages(size_t __Count__)
{
    if (__Count__ == 0)
    {
        PWarn("Attempted to allocate 0 pages\n");
        return 0;
    }

    if (__Count__ == 1)
        return AllocPage();

    if (__Count__ > Pmm.Stats.FreePages)
    {
        PError("Not enough free pages: requested %lu, available %lu\n", __Count__, Pmm.Stats.FreePages);
        return 0;
    }

    PDebug("Searching for %lu contiguous pages...\n", __Count__);

    /*Search for contiguous free block*/
    for (uint64_t StartIndex = 0; StartIndex <= Pmm.TotalPages - __Count__; StartIndex++)
    {
        int Found = 1;

        /*Check if all pages in range are free*/
        for (size_t Offset = 0; Offset < __Count__; Offset++)
        {
            if (TestBitmapBit(StartIndex + Offset))
            {
                Found = 0;
                break;
            }
        }

        if (Found)
        {
            /*Mark all pages in block as used*/
            for (size_t Offset = 0; Offset < __Count__; Offset++)
                SetBitmapBit(StartIndex + Offset);

            Pmm.Stats.UsedPages += __Count__;
            Pmm.Stats.FreePages -= __Count__;

            uint64_t PhysAddr = StartIndex * PageSize;
            PDebug("Allocated %lu contiguous pages at: 0x%016lx\n", __Count__, PhysAddr);

            return PhysAddr;
        }
    }

    PError("Failed to find %lu contiguous pages\n", __Count__);
    return 0;
}

/*
 * FreePages - Release multiple contiguous physical pages
 *
 * Frees a block of previously allocated contiguous pages. The pages
 * are freed individually to ensure proper validation of each page.
 *
 * Parameters:
 *   __PhysAddr__ - Physical address of first page in block
 *   __Count__    - Number of pages to free
 */
void
FreePages(uint64_t __PhysAddr__, size_t __Count__)
{
    if (__Count__ == 0)
    {
        PWarn("Attempted to free 0 pages\n");
        return;
    }

    PDebug("Freeing %lu pages starting at 0x%016lx\n", __Count__, __PhysAddr__);

    /*Free each page individually*/
    for (size_t Index = 0; Index < __Count__; Index++)
        FreePage(__PhysAddr__ + (Index * PageSize));
}

/*
 * PmmValidatePage - Validate a physical page address
 *
 * Performs comprehensive validation of a physical address to ensure
 * it represents a valid, page-aligned address within the system's
 * physical memory range.
 *
 * Parameters:
 *   __PhysAddr__ - Physical address to validate
 *
 * Returns:
 *   1 if address is valid, 0 otherwise
 */
int
PmmValidatePage(uint64_t __PhysAddr__)
{
    if (__PhysAddr__ == 0)
    {
        PDebug("Invalid page address: NULL\n");
        return 0;
    }
    if ((__PhysAddr__ % PageSize) != 0)
    {
        PDebug("Invalid page address: not aligned to %u bytes\n", PageSize);
        return 0;
    }
    if ((__PhysAddr__ / PageSize) >= Pmm.TotalPages)
    {
        PDebug("Invalid page address: beyond total pages\n");
        return 0;
    }
    return 1;
}
