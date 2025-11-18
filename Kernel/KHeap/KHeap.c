#include <KHeap.h>

/** @brief Global KHeap State */
KernelHeapManager KHeap;

/**
 * @brief Initialize the kernel heap manager.
 *
 * @details Sets up slab caches for standard object sizes (powers of two).
 *			Each cache is initialized with its object size and capacity.
 *
 * @return void
 *
 * @note Must be called before using KMalloc or KFree.
 */
void
InitializeKHeap(void)
{
    /*Set up standard slab cache sizes - powers of 2 for efficiency*/
    KHeap.SlabSizes[0] = 16;
    KHeap.SlabSizes[1] = 32;
    KHeap.SlabSizes[2] = 64;
    KHeap.SlabSizes[3] = 128;
    KHeap.SlabSizes[4] = 256;
    KHeap.SlabSizes[5] = 512;
    KHeap.SlabSizes[6] = 1024;
    KHeap.SlabSizes[7] = 2048;
    KHeap.CacheCount   = MaxSlabSizes;

    /*Initialize each slab cache with its object size and capacity*/
    for (uint32_t Index = 0; Index < MaxSlabSizes; Index++)
    {
        SlabCache* Cache  = &KHeap.Caches[Index];
        Cache->Slabs      = 0; /*No slabs allocated initially*/
        Cache->ObjectSize = KHeap.SlabSizes[Index];
        /*Calculate how many objects fit in a page minus slab header*/
        Cache->ObjectsPerSlab = (PageSize - sizeof(Slab)) / Cache->ObjectSize;

        /*Ensure at least one object per slab, even for large objects*/
        if (Cache->ObjectsPerSlab == 0)
        {
            Cache->ObjectsPerSlab = 1;
        }
    }

    PSuccess("KHeap initialized with %u slab caches\n", KHeap.CacheCount);
}

/**
 * @brief Allocate memory from the kernel heap.
 *
 * @details For small allocations (≤ 2048 bytes), uses the slab allocator.
 * 			For large allocations (> 2048 bytes), allocates directly from PMM.
 * 			Zeroes out allocated objects for security.
 *
 * @param __Size__ Requested allocation size in bytes.
 *
 * @return Pointer to allocated memory, or NULL if allocation fails.
 */
void*
KMalloc(size_t __Size__)
{
    /*Reject zero-sized allocations*/
    if (__Size__ == 0)
    {
        return 0;
    }

    /*Large allocations bypass slab system and go directly to PMM*/
    if (__Size__ > 2048)
    {
        /*Calculate pages needed, rounding up*/
        uint64_t Pages    = (__Size__ + PageSize - 1) / PageSize;
        uint64_t PhysAddr = AllocPages(Pages);
        if (!PhysAddr)
        {
            return 0; /*Out of memory*/
        }
        return PhysToVirt(PhysAddr);
    }

    /*Find the appropriate slab cache for this size*/
    SlabCache* Cache = GetSlabCache(__Size__);
    if (!Cache)
    {
        return 0; /*No suitable cache found*/
    }

    /*Search for a slab with available objects*/
    Slab* CurrentSlab = Cache->Slabs;
    while (CurrentSlab)
    {
        if (CurrentSlab->FreeCount > 0)
        {
            break; /*Found a slab with free objects*/
        }
        CurrentSlab = CurrentSlab->Next;
    }

    /*If no slab has free objects, allocate a new one*/
    if (!CurrentSlab)
    {
        CurrentSlab = AllocateSlab(Cache->ObjectSize);
        if (!CurrentSlab)
        {
            return 0; /*Failed to allocate new slab*/
        }

        /*Add new slab to the front of the cache's slab list*/
        CurrentSlab->Next = Cache->Slabs;
        Cache->Slabs      = CurrentSlab;
    }

    /*Remove object from free list*/
    SlabObject* Object = CurrentSlab->FreeList;
    if (!Object)
    {
        return 0; /*Should not happen if FreeCount > 0*/
    }

    CurrentSlab->FreeList = Object->Next;
    CurrentSlab->FreeCount--;

    /*Zero out the allocated object for security*/
    uint8_t* ObjectBytes = (uint8_t*)Object;
    for (uint32_t Index = 0; Index < Cache->ObjectSize; Index++)
    {
        ObjectBytes[Index] = 0;
    }

    return (void*)Object;
}

/**
 * @brief Free memory allocated from the kernel heap.
 *
 * @details If the pointer belongs to a slab, returns it to the slab’s free list.
 * 			If it was a large allocation, frees the corresponding physical page.
 *
 * @param __Ptr__ Pointer to memory to free.
 *
 * @return void
 */
void
KFree(void* __Ptr__)
{
    /*Ignore null pointers*/
    if (!__Ptr__)
    {
        return;
    }

    /*Calculate the slab address by masking off the page offset*/
    uint64_t ObjectAddr = (uint64_t)__Ptr__;
    uint64_t SlabAddr   = ObjectAddr & ~(PageSize - 1);
    Slab*    TargetSlab = (Slab*)SlabAddr;

    /*Check if this is a valid slab allocation*/
    if (TargetSlab->Magic != SlabMagic)
    {
        /*Not a slab allocation - must be a large page allocation*/
        uint64_t PhysAddr = VirtToPhys(__Ptr__);
        FreePage(PhysAddr);
        return;
    }

    /*Return object to slab's free list*/
    SlabObject* Object   = (SlabObject*)__Ptr__;
    Object->Next         = TargetSlab->FreeList;
    Object->Magic        = FreeObjectMagic; /*Mark as free for debugging*/
    TargetSlab->FreeList = Object;
    TargetSlab->FreeCount++;
}
