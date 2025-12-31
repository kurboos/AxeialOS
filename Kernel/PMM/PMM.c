#include <Errnos.h>
#include <PMM.h>

PhysicalMemoryManager Pmm = {0};

uint64_t
FindFreePage(void)
{
    uint64_t StartHint = Pmm.LastAllocHint;

    /*Look from hint forward to end of memory*/
    for (uint64_t Index = StartHint; Index < Pmm.TotalPages; Index++)
    {
        if (!TestBitmapBit(Index))
        {
            Pmm.LastAllocHint = Index + 1;
            return Index;
        }
    }

    /*Look from beginning to hint if not found above*/
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

void
InitializePmm(SysErr* __Err__)
{
    if (!HhdmRequest.response)
    {
        SlotError(__Err__, -NotCanonical);
        return;
    }
    Pmm.HhdmOffset = HhdmRequest.response->offset;
    PDebug("HHDM offset: 0x%016lx\n", Pmm.HhdmOffset);

    ParseMemoryMap(__Err__);
    if (Pmm.RegionCount == 0)
    {
        SlotError(__Err__, -NoSuch);
        return;
    }

    InitializeBitmap(__Err__);
    if (Pmm.Bitmap == 0)
    {
        SlotError(__Err__, -NotInit);
        return;
    }

    /*Markup*/
    MarkMemoryRegions(__Err__);

    /*Calculate final memory statistics*/
    Pmm.Stats.TotalPages = Pmm.TotalPages;
    Pmm.Stats.UsedPages  = 0;
    Pmm.Stats.FreePages  = 0;

    for (uint64_t Index = 0; Index < Pmm.TotalPages; Index++)
    {
        if (TestBitmapBit(Index))
        {
            Pmm.Stats.UsedPages++;
        }
        else
        {
            Pmm.Stats.FreePages++;
        }
    }

    PSuccess("PMM initialized: %lu MB total, %lu MB free\n",
             (Pmm.Stats.TotalPages * PageSize) / (1024 * 1024),
             (Pmm.Stats.FreePages * PageSize) / (1024 * 1024));
}

uint64_t
AllocPage(void)
{
    uint64_t PageIndex = FindFreePage();

    if (PageIndex == PmmBitmapNotFound)
    {
        return Nothing;
    }

    SysErr  err;
    SysErr* Error = &err;

    /*Mark page as used in bitmap*/
    SetBitmapBit(PageIndex, Error);
    Pmm.Stats.UsedPages++;
    Pmm.Stats.FreePages--;

    uint64_t PhysAddr = PageIndex * PageSize;
    PDebug("Allocated page: 0x%016lx (index %lu)\n", PhysAddr, PageIndex);

    return PhysAddr;
}

void
FreePage(uint64_t __PhysAddr__, SysErr* __Err__)
{
    if (PmmValidatePage(__PhysAddr__) != SysOkay)
    {
        SlotError(__Err__, -NotCanonical);
        return;
    }

    uint64_t PageIndex = __PhysAddr__ / PageSize;

    if (!TestBitmapBit(PageIndex))
    {
        SlotError(__Err__, -Overflow);
        return;
    }

    /*Mark page as free in bitmap*/
    ClearBitmapBit(PageIndex, __Err__);
    Pmm.Stats.UsedPages--;
    Pmm.Stats.FreePages++;

    PDebug("Freed a page: 0x%016lx (index %lu)\n", __PhysAddr__, PageIndex);
}

uint64_t
AllocPages(size_t __Count__)
{
    if (__Count__ == 0)
    {
        return Nothing;
    }

    if (__Count__ == 1)
    {
        return AllocPage();
    }

    if (__Count__ > Pmm.Stats.FreePages)
    {
        return Nothing;
    }

    /*Search for contiguous free block*/
    for (uint64_t StartIndex = 0; StartIndex <= Pmm.TotalPages - __Count__; StartIndex++)
    {
        int Found = 1;

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
            {
                SysErr  err;
                SysErr* Error = &err;
                SetBitmapBit(StartIndex + Offset, Error);
            }

            Pmm.Stats.UsedPages += __Count__;
            Pmm.Stats.FreePages -= __Count__;

            uint64_t PhysAddr = StartIndex * PageSize;
            PDebug("Allocated %lu contiguous pages at: 0x%016lx\n", __Count__, PhysAddr);

            return PhysAddr;
        }
    }

    return Nothing;
}

void
FreePages(uint64_t __PhysAddr__, size_t __Count__, SysErr* __Err__)
{
    if (__Count__ == 0)
    {
        SlotError(__Err__, -TooLess);
        return;
    }

    PDebug("Freeing %lu pages starting at 0x%016lx\n", __Count__, __PhysAddr__);

    /*Linearly free*/
    for (size_t Index = 0; Index < __Count__; Index++)
    {
        FreePage(__PhysAddr__ + (Index * PageSize), __Err__);
    }
}

int
PmmValidatePage(uint64_t __PhysAddr__)
{
    if (__PhysAddr__ == 0)
    {
        return -NotCanonical;
    }
    if ((__PhysAddr__ % PageSize) != 0)
    {
        return -NotCanonical;
    }
    if ((__PhysAddr__ / PageSize) >= Pmm.TotalPages)
    {
        return -TooMany;
    }
    return SysOkay;
}
