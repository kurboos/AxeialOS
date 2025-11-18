#pragma once

#include <EveryType.h>

/*KHeap*/

/**
 * Constants
 * NOTE: These are optional because Modules
 * may not need 'em.
 */
#define MaxSlabSizes    8
#define SlabMagic       0xDEADBEEF
#define FreeObjectMagic 0xFEEDFACE

/**
 * Core
 */
void* KMalloc(size_t __Size__);
void  KFree(void* __Ptr__);

/*Module*/

/**
 * Constants
 */
#define ModTextBase 0xffffffff90000000ULL
#define ModTextSize 0x08000000ULL /* 128 MB */
#define ModDataBase 0xffffffff98000000ULL
#define ModDataSize 0x08000000ULL /* 128 MB */

/**
 * Functions
 */
void  ModMemInit(void);
void* ModMalloc(size_t __Size__, int __IsText__);
void  ModFree(void* __Addr__, size_t __Size__);
