#include <Errnos.h>
#include <PMM.h>

void
InitializeBitmap(SysErr* __Err__)
{
    /*In 64-bit entries*/
    Pmm.BitmapSize       = (Pmm.TotalPages + BitsPerUint64 - 1) / BitsPerUint64;
    uint64_t BitmapBytes = Pmm.BitmapSize * sizeof(uint64_t);

    uint64_t BitmapPhys = 0;
    for (uint32_t Index = 0; Index < Pmm.RegionCount; Index++)
    {
        if (Pmm.Regions[Index].Type == MemoryTypeUsable && Pmm.Regions[Index].Length >= BitmapBytes)
        {
            BitmapPhys = Pmm.Regions[Index].Base;
            PDebug("Found bitmap location in region %u\n", Index);
            break;
        }
    }

    if (BitmapPhys == 0)
    {
        SlotError(__Err__, -NoSuch);
        return;
    }

    Pmm.Bitmap = (uint64_t*)PhysToVirt(BitmapPhys);

    /*Initialize all bits to 0 (free)*/
    for (uint64_t Index = 0; Index < Pmm.BitmapSize; Index++)
    {
        Pmm.Bitmap[Index] = 0;
    }

    PSuccess("Bitmap initialized at 0x%016lx\n", BitmapPhys);
}

void
SetBitmapBit(uint64_t __PageIndex__, SysErr* __Err__)
{
    uint64_t ByteIndex = __PageIndex__ / BitsPerUint64;
    uint64_t BitIndex  = __PageIndex__ % BitsPerUint64;
    Pmm.Bitmap[ByteIndex] |= (1ULL << BitIndex);
}

void
ClearBitmapBit(uint64_t __PageIndex__, SysErr* __Err__)
{
    uint64_t ByteIndex = __PageIndex__ / BitsPerUint64;
    uint64_t BitIndex  = __PageIndex__ % BitsPerUint64;
    Pmm.Bitmap[ByteIndex] &= ~(1ULL << BitIndex);
}

int
TestBitmapBit(uint64_t __PageIndex__)
{
    uint64_t ByteIndex = __PageIndex__ / BitsPerUint64;
    uint64_t BitIndex  = __PageIndex__ % BitsPerUint64;
    return (Pmm.Bitmap[ByteIndex] & (1ULL << BitIndex)) != Nothing;
}
