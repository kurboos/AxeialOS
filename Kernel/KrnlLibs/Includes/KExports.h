#pragma once

#include <AllTypes.h>

/**
 * Struct
 */
typedef struct KExport
{

    const char* Name; /* Symbol name (string) */
    void*       Addr; /* Symbol address */

} KExport;

/**
 * Macro
 */
#define KEXPORT(__SYM__)                                                                           \
    __attribute__((used, section(".kexports"))) static const KExport _kexp_##__SYM__ = {           \
        #__SYM__, (void*)&__SYM__};

/**
 * Linker symbols
 */
extern const KExport __start_kexports[];
extern const KExport __stop_kexports[];

/**
 * Functions
 */
void* KexpLookup(const char* __Name__);
void  KexpDump(void);