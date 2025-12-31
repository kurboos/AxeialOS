#include <Errnos.h>
#include <KHeap.h>

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
    return Error_TO_Pointer(-NoSuch); /*No suitable cache found*/
}

Slab*
AllocateSlab(uint32_t __ObjectSize__)
{
    SysErr  err;
    SysErr* Error = &err;

    uint64_t PhysAddr = AllocPage();
    if (!PhysAddr)
    {
        return Error_TO_Pointer(-TooMany); /*Out of memory*/
    }

    Slab* NewSlab = (Slab*)PhysToVirt(PhysAddr);

    NewSlab->Next       = 0; /*Not linked yet*/
    NewSlab->FreeList   = 0; /*Will be set after creating objects*/
    NewSlab->ObjectSize = __ObjectSize__;
    NewSlab->FreeCount  = 0;         /*Will be incremented as objects are added*/
    NewSlab->Magic      = SlabMagic; /*Validation marker*/

    uint8_t*    ObjectPtr  = (uint8_t*)NewSlab + sizeof(Slab);
    uint8_t*    SlabEnd    = (uint8_t*)NewSlab + PageSize;
    SlabObject* PrevObject = 0; /*Previous object in free list*/

    /*Link in reverse order*/
    while ((ObjectPtr + __ObjectSize__) <= SlabEnd)
    {
        SlabObject* Object = (SlabObject*)ObjectPtr;
        Object->Next       = PrevObject;      /*Link to previous free object*/
        Object->Magic      = FreeObjectMagic; /*Mark as free*/
        PrevObject         = Object;          /*Update previous for next iteration*/
        ObjectPtr += __ObjectSize__;          /*Move to next object position*/
        NewSlab->FreeCount++;                 /*Count free objects*/
    }

    NewSlab->FreeList = PrevObject;

    return NewSlab;
}

void
FreeSlab(Slab* __Slab__, SysErr* __Err__)
{
    if (!__Slab__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    uint64_t PhysAddr = VirtToPhys(__Slab__);
    FreePage(PhysAddr, __Err__);
}
