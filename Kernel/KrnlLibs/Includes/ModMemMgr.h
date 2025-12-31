#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <KrnPrintf.h>
#include <PMM.h>
#include <VMM.h>

#define ModTextBase 0xffffffff90000000ULL
#define ModTextSize 0x08000000ULL /* 128 MB */
#define ModDataBase 0xffffffff98000000ULL
#define ModDataSize 0x08000000ULL /* 128 MB */

typedef struct
{
    uint64_t TextCursor;
    uint64_t DataCursor;
    uint8_t  Initialized;

} ModuleMemoryManager;

extern ModuleMemoryManager ModMem;

void  ModMemInit(SysErr* __Err__);
void* ModMalloc(size_t __Size__, int __IsText__);
void  ModFree(void* __Addr__, size_t __Size__, SysErr* __Err__);

KEXPORT(ModMemInit);
KEXPORT(ModMalloc);
KEXPORT(ModFree);