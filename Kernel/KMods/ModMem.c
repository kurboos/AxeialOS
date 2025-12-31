#include <ModMemMgr.h>

ModuleMemoryManager ModMem = {0, 0, 0};

void
ModMemInit(SysErr* __Err__)
{
    ModMem.TextCursor  = 0;
    ModMem.DataCursor  = 0;
    ModMem.Initialized = 1;

    PDebug("[Text=%#llx..%#llx Data=%#llx..%#llx\n",
           (unsigned long long)ModTextBase,
           (unsigned long long)(ModTextBase + ModTextSize - 1),
           (unsigned long long)ModDataBase,
           (unsigned long long)(ModDataBase + ModDataSize - 1));
}

void*
ModMalloc(size_t __Size__, int __IsText__)
{
    if (!ModMem.Initialized || __Size__ == 0)
    {
        return NULL;
    }

    size_t Pages = (__Size__ + PageSize - 1) / PageSize;

    uint64_t Start =
        __IsText__ ? (ModTextBase + ModMem.TextCursor) : (ModDataBase + ModMem.DataCursor);
    uint64_t End   = Start + Pages * PageSize;
    uint64_t Limit = __IsText__ ? (ModTextBase + ModTextSize) : (ModDataBase + ModDataSize);

    if (End > Limit)
    {
        return Error_TO_Pointer(-Limits);
    }
    for (size_t I = 0; I < Pages; ++I)
    {
        uint64_t Phys = AllocPage();
        if (!Phys)
        {
            return Error_TO_Pointer(-NotCanonical);
        }

        uint64_t Virt = Start + I * PageSize;

        uint64_t Flags = PTEPRESENT | PTEGLOBAL;
        if (__IsText__)
        {
            Flags |= PTEWRITABLE; /* allow section copy */
        }
        else
        {
            /* Data/Rodata/Bss: writable + NX */
            Flags |= PTEWRITABLE;
            Flags |= PTENOEXECUTE;
        }

        /* Map the page */
        if (MapPage(Vmm.KernelSpace, Virt, Phys, Flags) != SysOkay)
        {
            return Error_TO_Pointer(-NotCanonical);
        }
    }

    if (__IsText__)
    {
        ModMem.TextCursor += Pages * PageSize;
    }
    else
    {
        ModMem.DataCursor += Pages * PageSize;
    }

    PDebug("Alloc %zu pages at %p (%s)\n", Pages, (void*)Start, __IsText__ ? "Text" : "Data");

    return (void*)Start;
}

void
ModFree(void* __Addr__, size_t __Size__, SysErr* __Err__)
{
    if (!__Addr__ || __Size__ == 0)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    size_t   Pages = (__Size__ + PageSize - 1) / PageSize;
    uint64_t Virt  = (uint64_t)__Addr__;

    /* Linear free */
    for (size_t I = 0; I < Pages; ++I)
    {
        uint64_t Va   = Virt + I * PageSize;
        uint64_t Phys = GetPhysicalAddress(Vmm.KernelSpace, Va);
        if (Phys)
        {
            UnmapPage(Vmm.KernelSpace, Va);
            FreePage(Phys, __Err__);
        }
    }

    PDebug("Freed %zu pages at %p\n", Pages, __Addr__);
}
