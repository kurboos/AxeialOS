#pragma once

#include <AllTypes.h>
#include <PMM.h>
#include <VMM.h>
#include <KrnPrintf.h>

/**
 * Constants
 */
#define MaxSlabSizes            8
#define SlabMagic              0xDEADBEEF
#define FreeObjectMagic        0xFEEDFACE

/**
 * Slab Object
 */
typedef
struct SlabObject
{

    struct SlabObject *Next;
    uint32_t Magic;

} SlabObject;

/**
 * Slab
 */
typedef
struct Slab
{

    struct Slab *Next;
    SlabObject *FreeList;
    uint32_t ObjectSize;
    uint32_t FreeCount;
    uint32_t Magic;

} Slab;

/**
 * Slab Cache
 */
typedef
struct
{

    Slab *Slabs;
    uint32_t ObjectSize;
    uint32_t ObjectsPerSlab;

} SlabCache;

/**
 * Kernel Heap Manager
 */
typedef
struct
{

    SlabCache Caches[MaxSlabSizes];
    uint32_t SlabSizes[MaxSlabSizes];
    uint32_t CacheCount;

} KernelHeapManager;

/**
 * Globals
 */
extern KernelHeapManager KHeap;

/**
 * Cor
 */
void InitializeKHeap(void);
void* KMalloc(size_t __Size__);
void KFree(void* __Ptr__);

/**
 * Internals
 */
SlabCache* GetSlabCache(size_t __Size__);
Slab* AllocateSlab(uint32_t __ObjectSize__);
void FreeSlab(Slab* __Slab__);
