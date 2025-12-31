#include <Errnos.h>
#include <PMM.h>

void
PmmDumpStats(SysErr* __Err__)
{
    PInfo("PMM Statistics:\n");
    KrnPrintf("  Total Pages: %lu (%lu MB)\n",
              Pmm.Stats.TotalPages,
              (Pmm.Stats.TotalPages * PageSize) / (1024 * 1024));

    KrnPrintf("  Used Pages:  %lu (%lu MB)\n",
              Pmm.Stats.UsedPages,
              (Pmm.Stats.UsedPages * PageSize) / (1024 * 1024));

    KrnPrintf("  Free Pages:  %lu (%lu MB)\n",
              Pmm.Stats.FreePages,
              (Pmm.Stats.FreePages * PageSize) / (1024 * 1024));

    KrnPrintf("  Memory Usage: %lu%%\n", (Pmm.Stats.UsedPages * 100) / Pmm.Stats.TotalPages);

    KrnPrintf("  Bitmap Size: %lu entries (%lu KB)\n",
              Pmm.BitmapSize,
              (Pmm.BitmapSize * sizeof(uint64_t)) / 1024);
}

void
PmmDumpRegions(SysErr* __Err__)
{
    PInfo("Memory Regions (%u total):\n", Pmm.RegionCount);

    const char* TypeNames[] = {"Usable", "Reserved", "Kernel", "Bad"};

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
