#pragma once

#include <AllTypes.h>
#include <ModELF.h>

/**
 * Structs
 */
typedef struct ModuleRecord
{
    const char*    Name;
    void**         SectionBases;
    Elf64_Shdr*    ShTbl;
    __ElfSymbol__* Syms;
    Elf64_Sym*     SymBuf;
    char*          StrBuf;
    long           SectionCount;
    uint8_t*       ZeroStub;

    void (*InitFn)(void);
    void (*ExitFn)(void);

    int                  RefCount;
    struct ModuleRecord* Next;

} ModuleRecord;

/**
 * Globals
 */
extern ModuleRecord* ModuleListHead;

/**
 * Functions
 */
int           ModuleRegistryInit(void);
int           ModuleRegistryAdd(ModuleRecord* __Rec__);
ModuleRecord* ModuleRegistryFind(const char* __Name__);
int           ModuleRegistryRemove(ModuleRecord* __Rec__);