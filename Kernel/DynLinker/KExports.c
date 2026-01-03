#include <Errnos.h>
#include <KExports.h>
#include <KrnPrintf.h>
#include <String.h>

void*
KexpLookup(const char* __Name__)
{
    if (Probe_IF_Error(__Name__) || !__Name__)
    {
        return Error_TO_Pointer(-BadArgs);
    }

    /* Iterate */
    const KExport* Cur = __start_kexports;
    const KExport* End = __stop_kexports;

    while (Cur < End)
    {
        if (Cur->Name && strcmp(Cur->Name, __Name__) == 0)
        {
            return Cur->Addr;
        }

        Cur++;
    }

    /* Symbol not found */
    return Error_TO_Pointer(-NoSuch);
}

void
KexpDump(SysErr* __Err__ _unused)
{
    const KExport* Cur = __start_kexports;
    const KExport* End = __stop_kexports;

    PInfo("Listing all kernel exports:\n");

    /* Iterate */
    while (Cur < End)
    {
        KrnPrintf("  %s => %p\n", Cur->Name, Cur->Addr);
        Cur++;
    }
}
