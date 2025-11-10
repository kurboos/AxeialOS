#include <KHeap.h>

/*
 * Kernel Heap Manager Global Instance
 *
 * The global kernel heap manager that coordinates all heap operations.
 * Uses a slab allocator design with multiple cache sizes for efficient
 * small object allocation and direct page allocation for large objects.
 */
KernelHeapManager KHeap;

/*
 * InitializeKHeap - Initialize the Kernel Heap System
 *
 * Sets up the slab allocator with predefined cache sizes for small objects.
 * The heap uses a multi-cache slab allocator where objects of different
 * sizes are managed by separate caches to reduce fragmentation and improve
 * allocation performance.
 *
 * Cache sizes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes
 * Objects larger than 2048 bytes are allocated directly from the PMM.
 *
 * Parameters: None
 * Returns: None
 *
 * Side effects:
 * - Initializes global KHeap structure
 * - Sets up slab caches for different object sizes
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
    KHeap.CacheCount = MaxSlabSizes;

    /*Initialize each slab cache with its object size and capacity*/
    for (uint32_t Index = 0; Index < MaxSlabSizes; Index++)
    {
        SlabCache *Cache = &KHeap.Caches[Index];
        Cache->Slabs = 0;  /*No slabs allocated initially*/
        Cache->ObjectSize = KHeap.SlabSizes[Index];
        /*Calculate how many objects fit in a page minus slab header*/
        Cache->ObjectsPerSlab = (PageSize - sizeof(Slab)) / Cache->ObjectSize;

        /*Ensure at least one object per slab, even for large objects*/
        if (Cache->ObjectsPerSlab == 0)
            Cache->ObjectsPerSlab = 1;
    }

    PSuccess("KHeap initialized with %u slab caches\n", KHeap.CacheCount);
}

/*
 * KMalloc - Kernel Memory Allocation
 *
 * Allocates memory from the kernel heap. Uses slab allocation for small objects
 * and direct page allocation for large objects. All allocated memory is zeroed.
 *
 * For objects <= 2048 bytes: Uses slab allocator with appropriate cache size
 * For objects > 2048 bytes: Allocates directly from physical memory manager
 *
 * Parameters:
 * - __Size__: Size of memory to allocate in bytes
 *
 * Returns:
 * - Pointer to allocated memory, or NULL on failure
 *
 * Thread safety: Not thread-safe, assumes single-threaded kernel context
 */
void*
KMalloc(size_t __Size__)
{
    /*Reject zero-sized allocations*/
    if (__Size__ == 0)
        return 0;

    /*Large allocations bypass slab system and go directly to PMM*/
    if (__Size__ > 2048)
    {
        /*Calculate pages needed, rounding up*/
        uint64_t Pages = (__Size__ + PageSize - 1) / PageSize;
        uint64_t PhysAddr = AllocPages(Pages);
        if (!PhysAddr)
            return 0;  /*Out of memory*/
        return PhysToVirt(PhysAddr);
    }

    /*Find the appropriate slab cache for this size*/
    SlabCache *Cache = GetSlabCache(__Size__);
    if (!Cache)
        return 0;  /*No suitable cache found*/

    /*Search for a slab with available objects*/
    Slab *CurrentSlab = Cache->Slabs;
    while (CurrentSlab)
    {
        if (CurrentSlab->FreeCount > 0)
            break;  /*Found a slab with free objects*/
        CurrentSlab = CurrentSlab->Next;
    }

    /*If no slab has free objects, allocate a new one*/
    if (!CurrentSlab)
    {
        CurrentSlab = AllocateSlab(Cache->ObjectSize);
        if (!CurrentSlab)
            return 0;  /*Failed to allocate new slab*/

        /*Add new slab to the front of the cache's slab list*/
        CurrentSlab->Next = Cache->Slabs;
        Cache->Slabs = CurrentSlab;
    }

    /*Remove object from free list*/
    SlabObject *Object = CurrentSlab->FreeList;
    if (!Object)
        return 0;  /*Should not happen if FreeCount > 0*/

    CurrentSlab->FreeList = Object->Next;
    CurrentSlab->FreeCount--;

    /*Zero out the allocated object for security*/
    uint8_t *ObjectBytes = (uint8_t*)Object;
    for (uint32_t Index = 0; Index < Cache->ObjectSize; Index++)
        ObjectBytes[Index] = 0;

    return (void*)Object;
}

/*
 * KFree - Kernel Memory Deallocation
 *
 * Frees memory allocated by KMalloc. Determines whether the pointer belongs
 * to a slab allocation or a large page allocation and handles accordingly.
 *
 * For slab objects: Returns the object to its slab's free list
 * For large allocations: Returns the pages to the physical memory manager
 *
 * Parameters:
 * - __Ptr__: Pointer to memory to free (must be from KMalloc)
 *
 * Returns: None
 *
 * Thread safety: Not thread-safe, assumes single-threaded kernel context
 */
void
KFree(void* __Ptr__)
{
    /*Ignore null pointers*/
    if (!__Ptr__)
        return;

    /*Calculate the slab address by masking off the page offset*/
    uint64_t ObjectAddr = (uint64_t)__Ptr__;
    uint64_t SlabAddr = ObjectAddr & ~(PageSize - 1);
    Slab *TargetSlab = (Slab*)SlabAddr;

    /*Check if this is a valid slab allocation*/
    if (TargetSlab->Magic != SlabMagic)
    {
        /*Not a slab allocation - must be a large page allocation*/
        uint64_t PhysAddr = VirtToPhys(__Ptr__);
        FreePage(PhysAddr);
        return;
    }

    /*Return object to slab's free list*/
    SlabObject *Object = (SlabObject*)__Ptr__;
    Object->Next = TargetSlab->FreeList;
    Object->Magic = FreeObjectMagic;  /*Mark as free for debugging*/
    TargetSlab->FreeList = Object;
    TargetSlab->FreeCount++;
}
