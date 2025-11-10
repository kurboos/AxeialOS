#include <PMM.h>

/*
 * PmmDumpStats - Display PMM memory statistics
 *
 * Prints comprehensive statistics about the current state of physical memory
 * management, including total, used, and free pages, memory usage percentage,
 * and bitmap size information. This is useful for monitoring memory usage
 * and detecting potential memory leaks or allocation issues.
 */
void
PmmDumpStats(void)
{
    PInfo("PMM Statistics:\n");
    KrnPrintf("  Total Pages: %lu (%lu MB)\n",
    Pmm.Stats.TotalPages, (Pmm.Stats.TotalPages * PageSize) / (1024 * 1024));

    KrnPrintf("  Used Pages:  %lu (%lu MB)\n",
    Pmm.Stats.UsedPages, (Pmm.Stats.UsedPages * PageSize) / (1024 * 1024));

    KrnPrintf("  Free Pages:  %lu (%lu MB)\n",
    Pmm.Stats.FreePages, (Pmm.Stats.FreePages * PageSize) / (1024 * 1024));

    KrnPrintf("  Memory Usage: %lu%%\n",
    (Pmm.Stats.UsedPages * 100) / Pmm.Stats.TotalPages);

    KrnPrintf("  Bitmap Size: %lu entries (%lu KB)\n",
    Pmm.BitmapSize, (Pmm.BitmapSize * sizeof(uint64_t)) / 1024);
}

/*
 * PmmDumpRegions - Display memory region information
 *
 * Lists all memory regions identified during PMM initialization, showing
 * their base addresses, lengths, types, and sizes. This helps verify that
 * the memory map was parsed correctly and that regions are properly classified.
 *
 * Region types:
 * - Usable: Available for general allocation
 * - Reserved: Reserved for hardware/firmware use
 * - Kernel: Contains kernel and bootloader modules
 * - Bad: Faulty memory (if detected)
 */
void
PmmDumpRegions(void)
{
    PInfo("Memory Regions (%u total):\n", Pmm.RegionCount);

    const char *TypeNames[] = {"Usable", "Reserved", "Kernel", "Bad"};

    for (uint32_t Index = 0; Index < Pmm.RegionCount; Index++)
    {
        KrnPrintf("  [%u] 0x%016lx-0x%016lx %s (%lu MB)\n",
        Index,
        Pmm.Regions[Index].Base,
        Pmm.Regions[Index].Base + Pmm.Regions[Index].Length,
        TypeNames[Pmm.Regions[Index].Type],
        Pmm.Regions[Index].Length / (1024 * 1024));
    }
}
