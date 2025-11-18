#pragma once

#include <AllTypes.h>
#include <AxeThreads.h>
#include <KHeap.h>
#include <PMM.h>
#include <Process.h>
#include <String.h>
#include <VFS.h>
#include <VMM.h>

/**
 * Structures
 */

/**
 * @brief ELF64 execution image: entry point, user stack pointer, and new address space.
 */
typedef struct ElfExecImage
{

    uint64_t            Entry;           /**< Entry point (final virtual address) */
    uint64_t            UserSp;          /**< Initial user-mode stack pointer */
    VirtualMemorySpace* Space;           /**< Newly created user address space */
    uint64_t            LoadBase;        /**< Chosen base for ET_DYN (0 for ET_EXEC) */
    int                 StackExecutable; /**< 1 if PT_GNU_STACK requests executable stack */

} ElfExecImage;

/**
 * Functions
 */
int      ElfLoadExec(Process*           __Proc__,
                     const char*        __Path__,
                     const char* const* __Argv__,
                     const char* const* __Envp__,
                     ElfExecImage*      __OutImage__);
int      ElfMapLoadSegments(VirtualMemorySpace* __Space__,
                            File*               __File__,
                            void*               __Phdrs__,
                            uint16_t            __Phnum__,
                            uint64_t            __LoadBase__);
uint64_t ElfSetupUserStack(VirtualMemorySpace* __Space__,
                           const char* const*  __Argv__,
                           const char* const*  __Envp__,
                           int                 __StackExecutable__);