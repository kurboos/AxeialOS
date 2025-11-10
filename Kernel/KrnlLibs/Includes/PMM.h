#pragma once

#include <AllTypes.h>
#include <KrnPrintf.h>
/*Limine*/
#include <LimineHHDM.h>
#include <LimineMmap.h>

/**
 * PMM Constants
 */
#define PageSize                4096
#define PageSizeBits            12
#define BitsPerByte             8
#define BitsPerUint64           64
#define MaxMemoryRegions        64
#define PmmBitmapNotFound       0xFFFFFFFFFFFFFFFF

/**
 * Memory Region Types
 */
#define MemoryTypeUsable        0
#define MemoryTypeReserved      1
#define MemoryTypeKernel        2
#define MemoryTypeBad           3

/**
 * PMM Statistics
 */
typedef
struct
{

    uint64_t TotalPages;
    uint64_t UsedPages;
    uint64_t FreePages;
    uint64_t ReservedPages;
    uint64_t KernelPages;
    uint64_t BitmapPages;
	
} PmmStats;

/**
 * Memory Region
 */
typedef
struct
{

    uint64_t Base;
    uint64_t Length;
    uint32_t Type;

} MemoryRegion;

/**
 * PMM Manager
 */
typedef
struct
{

    uint64_t *Bitmap;
    uint64_t BitmapSize;
    uint64_t TotalPages;
    uint64_t LastAllocHint;
    uint64_t HhdmOffset;
    MemoryRegion Regions[MaxMemoryRegions];
    uint32_t RegionCount;
    PmmStats Stats;

} PhysicalMemoryManager;

/**
 * Globals
 */
extern PhysicalMemoryManager Pmm;

/**
 * HHDM
 */
void* PhysToVirt(uint64_t __PhysAddr__);
uint64_t VirtToPhys(void* __VirtAddr__);

/**
 * Core PMM
 */
void InitializePmm(void);
uint64_t AllocPage(void);
void FreePage(uint64_t __PhysAddr__);
uint64_t AllocPages(size_t __Count__);
void FreePages(uint64_t __PhysAddr__, size_t __Count__);

/**
 * Utils
 */
void PmmDumpStats(void);
void PmmDumpRegions(void);
int PmmValidatePage(uint64_t __PhysAddr__);

/**
 * Bitmap Functions
 */
void InitializeBitmap(void);
void ParseMemoryMap(void);
void MarkMemoryRegions(void);
void SetBitmapBit(uint64_t __PageIndex__);
void ClearBitmapBit(uint64_t __PageIndex__);
int TestBitmapBit(uint64_t __PageIndex__);
