#include <KExports.h>
#include <KrnPrintf.h>
#include <String.h>

/**
 * @brief Resolve a symbol name against the kernel export table
 *
 * @details Searches the kernel's exported symbol table for a symbol with the given name.
 * 			This enables dynamic resolution of kernel functions and variables by name.
 *
 * @param __Name__ Symbol name string to look up
 * @return Address of the symbol if found, NULL if not found or input invalid
 *
 * @note The export table is defined by linker sections __start_kexports
 *       and __stop_kexports, populated at build time.
 */
void*
KexpLookup(const char* __Name__)
{
    /* Validate input parameter */
    if (!__Name__)
    {
        return 0;
    }

    /* Iterate through the export table */
    const KExport* Cur = __start_kexports;
    const KExport* End = __stop_kexports;

    while (Cur < End)
    {
        /* Check if symbol name matches */
        if (Cur->Name && strcmp(Cur->Name, __Name__) == 0)
        {
            return Cur->Addr;
        }

        Cur++;
    }

    /* Symbol not found */
    return 0;
}

/**
 * @brief Print all exported symbols for diagnostics
 *
 * @details Iterates through the entire kernel export table and prints each symbol's
 * 			name and address. Useful for debugging and verifying symbol exports.
 *
 * @note Uses KrnPrintf for output, which goes to the kernel console/serial
 */
void
KexpDump(void)
{
    /* Get pointers to export table boundaries */
    const KExport* Cur = __start_kexports;
    const KExport* End = __stop_kexports;

    /* Print header message */
    PInfo("KExports: Listing all kernel exports:\n");

    /* Iterate and print each symbol */
    while (Cur < End)
    {
        KrnPrintf("  %s => %p\n", Cur->Name, Cur->Addr);
        Cur++;
    }
}
