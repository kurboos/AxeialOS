#pragma once

#include <AllTypes.h>
#include <Errnos.h>

typedef struct KExport
{
    const char* Name; /* Symbol name (string) */
    void*       Addr; /* Symbol address */

} KExport;

#define KEXPORT(__SYM__)                                                                           \
    __attribute__((used, section(".kexports"))) static const KExport _kexp_##__SYM__ = {           \
        #__SYM__, (void*)&__SYM__};

extern const KExport __start_kexports[];
extern const KExport __stop_kexports[];

void* KexpLookup(const char* __Name__);
void  KexpDump(SysErr* __Err__ _unused);