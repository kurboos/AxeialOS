#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <KrnPrintf.h>
#include <PMM.h>
#include <VMM.h>

#define MaxSlabSizes    8
#define SlabMagic       0xDEADBEEF
#define FreeObjectMagic 0xFEEDFACE

typedef struct SlabObject
{
    struct SlabObject* Next;
    uint32_t           Magic;

} SlabObject;

typedef struct Slab
{
    struct Slab* Next;
    SlabObject*  FreeList;
    uint32_t     ObjectSize;
    uint32_t     FreeCount;
    uint32_t     Magic;

} Slab;

typedef struct
{
    Slab*    Slabs;
    uint32_t ObjectSize;
    uint32_t ObjectsPerSlab;

} SlabCache;

typedef struct
{
    SlabCache Caches[MaxSlabSizes];
    uint32_t  SlabSizes[MaxSlabSizes];
    uint32_t  CacheCount;

} KernelHeapManager;

extern KernelHeapManager KHeap;

void  InitializeKHeap(SysErr* __Err__);
void* KMalloc(size_t __Size__);
void  KFree(void* __Ptr__, SysErr* __Err__);

SlabCache* GetSlabCache(size_t __Size__);
Slab*      AllocateSlab(uint32_t __ObjectSize__);
void       FreeSlab(Slab* __Slab__, SysErr* __Err__);

KEXPORT(KMalloc);
KEXPORT(KFree);
