#include <KHeap.h>

/**
 * @brief Retrieve the appropriate slab cache for a given size.
 *
 * @details Iterates through the available slab caches and returns the smallest
 * 			cache that can fit the requested allocation size.
 *
 * @param __Size__ Requested allocation size in bytes.
 *
 * @return Pointer to the matching SlabCache, or NULL if none found.
 */
SlabCache*
GetSlabCache(size_t __Size__)
{
    /*Find the smallest cache that can fit the requested size*/
    for (uint32_t Index = 0; Index < MaxSlabSizes; Index++)
    {
        if (__Size__ <= KHeap.SlabSizes[Index])
        {
            return &KHeap.Caches[Index];
        }
    }
    return 0; /*No suitable cache found*/
}

/**
 * @brief Allocate a new slab.
 *
 * @details Allocates a single physical page and initializes it as a slab:
 * 			Sets metadata fields (object size, free count, magic).
 * 			Builds a free list of objects within the slab.
 *
 * @param __ObjectSize__ Size of each object in the slab.
 *
 * @return Pointer to the newly allocated Slab, or NULL if out of memory.
 */
Slab*
AllocateSlab(uint32_t __ObjectSize__)
{
    /*Allocate a single page for the slab*/
    uint64_t PhysAddr = AllocPage();
    if (!PhysAddr)
    {
        return 0; /*Out of memory*/
    }

    Slab* NewSlab = (Slab*)PhysToVirt(PhysAddr);

    /*Initialize slab metadata*/
    NewSlab->Next       = 0; /*Not linked yet*/
    NewSlab->FreeList   = 0; /*Will be set after creating objects*/
    NewSlab->ObjectSize = __ObjectSize__;
    NewSlab->FreeCount  = 0;         /*Will be incremented as objects are added*/
    NewSlab->Magic      = SlabMagic; /*Validation marker*/

    /*Build the free object list starting from the end of the slab header*/
    uint8_t*    ObjectPtr  = (uint8_t*)NewSlab + sizeof(Slab);
    uint8_t*    SlabEnd    = (uint8_t*)NewSlab + PageSize;
    SlabObject* PrevObject = 0; /*Previous object in free list*/

    /*Create objects from low to high addresses, link in reverse order*/
    while ((ObjectPtr + __ObjectSize__) <= SlabEnd)
    {
        SlabObject* Object = (SlabObject*)ObjectPtr;
        Object->Next       = PrevObject;      /*Link to previous free object*/
        Object->Magic      = FreeObjectMagic; /*Mark as free*/
        PrevObject         = Object;          /*Update previous for next iteration*/
        ObjectPtr += __ObjectSize__;          /*Move to next object position*/
        NewSlab->FreeCount++;                 /*Count free objects*/
    }

    /*Set the free list head (last object allocated becomes first in free list)*/
    NewSlab->FreeList = PrevObject;

    return NewSlab;
}

/**
 * @brief Free a slab.
 *
 * @details Converts the slab’s virtual address back to physical and frees the page.
 *
 * @param __Slab__ Pointer to the slab to free.
 *
 * @return void
 */
void
FreeSlab(Slab* __Slab__)
{
    if (!__Slab__)
    {
        return; /*Ignore null pointers*/
    }

    /*Convert virtual address back to physical and free the page*/
    uint64_t PhysAddr = VirtToPhys(__Slab__);
    FreePage(PhysAddr);
}
