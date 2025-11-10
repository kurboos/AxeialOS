#include <PMM.h>

/*
 * InitializeBitmap - Set up the PMM allocation bitmap
 *
 * This function initializes the memory allocation bitmap by:
 * 1. Calculating the required bitmap size based on total pages
 * 2. Finding a suitable location in usable physical memory
 * 3. Mapping the bitmap to virtual memory for access
 * 4. Clearing all bits to indicate all pages are initially free
 *
 * The bitmap must be placed in physical memory and protected from
 * allocation to prevent self-corruption.
 */
void
InitializeBitmap(void)
{
    /*Calculate bitmap size in 64-bit entries*/
    Pmm.BitmapSize = (Pmm.TotalPages + BitsPerUint64 - 1) / BitsPerUint64;
    uint64_t BitmapBytes = Pmm.BitmapSize * sizeof(uint64_t);

    PInfo("Bitmap requires %lu KB for %lu pages\n", BitmapBytes / 1024, Pmm.TotalPages);

    /*Find a usable memory region large enough for the bitmap*/
    uint64_t BitmapPhys = 0;
    for (uint32_t Index = 0; Index < Pmm.RegionCount; Index++)
    {
        if (Pmm.Regions[Index].Type == MemoryTypeUsable &&
            Pmm.Regions[Index].Length >= BitmapBytes)
        {
            BitmapPhys = Pmm.Regions[Index].Base;
            PDebug("Found bitmap location in region %u\n", Index);
            break;
        }
    }

    if (BitmapPhys == 0)
    {
        PError("No suitable region for PMM bitmap\n");
        return;
    }

    /*Map bitmap physical address to virtual address for access*/
    Pmm.Bitmap = (uint64_t*)PhysToVirt(BitmapPhys);

    /*Initialize all bits to 0 (free)*/
    for (uint64_t Index = 0; Index < Pmm.BitmapSize; Index++)
        Pmm.Bitmap[Index] = 0;

    PSuccess("PMM bitmap initialized at 0x%016lx\n", BitmapPhys);
}

/*
 * SetBitmapBit - Mark a page as allocated
 *
 * Sets the bit corresponding to the specified page index, indicating
 * that the page is now in use and unavailable for allocation.
 *
 * Parameters:
 *   __PageIndex__ - Index of the page to mark as used
 */
void
SetBitmapBit(uint64_t __PageIndex__)
{
    uint64_t ByteIndex = __PageIndex__ / BitsPerUint64;
    uint64_t BitIndex = __PageIndex__ % BitsPerUint64;
    Pmm.Bitmap[ByteIndex] |= (1ULL << BitIndex);
}

/*
 * ClearBitmapBit - Mark a page as free
 *
 * Clears the bit corresponding to the specified page index, indicating
 * that the page is now available for allocation.
 *
 * Parameters:
 *   __PageIndex__ - Index of the page to mark as free
 */
void
ClearBitmapBit(uint64_t __PageIndex__)
{
    uint64_t ByteIndex = __PageIndex__ / BitsPerUint64;
    uint64_t BitIndex = __PageIndex__ % BitsPerUint64;
    Pmm.Bitmap[ByteIndex] &= ~(1ULL << BitIndex);
}

/*
 * TestBitmapBit - Check if a page is allocated
 *
 * Tests the bit corresponding to the specified page index to determine
 * if the page is currently allocated or free.
 *
 * Parameters:
 *   __PageIndex__ - Index of the page to check
 *
 * Returns:
 *   1 if page is allocated (bit set), 0 if free (bit clear)
 */
int
TestBitmapBit(uint64_t __PageIndex__)
{
    uint64_t ByteIndex = __PageIndex__ / BitsPerUint64;
    uint64_t BitIndex = __PageIndex__ % BitsPerUint64;
    return (Pmm.Bitmap[ByteIndex] & (1ULL << BitIndex)) != 0;
}
