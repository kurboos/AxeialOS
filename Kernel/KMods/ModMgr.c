#include <AllTypes.h>
#include <KHeap.h>
#include <KMods.h>
#include <KrnPrintf.h>
#include <ModMemMgr.h>
#include <String.h>
#include <VFS.h>

/*Globals*/
ModuleRecord* ModuleListHead = 0;

int
ModuleRegistryInit(void)
{
    ModuleListHead = ModuleListHead;
    return SysOkay;
}

int
ModuleRegistryAdd(ModuleRecord* __Rec__)
{
    if (!__Rec__)
    {
        return -BadArgs;
    }

    __Rec__->Next  = ModuleListHead;
    ModuleListHead = __Rec__;
    return SysOkay;
}

ModuleRecord*
ModuleRegistryFind(const char* __Name__)
{
    if (!__Name__)
    {
        return Error_TO_Pointer(-BadArgs);
    }

    ModuleRecord* It = ModuleListHead;
    while (It)
    {
        if (It->Name && strcmp(It->Name, __Name__) == 0)
        {
            return It;
        }
        It = It->Next;
    }
    return Error_TO_Pointer(-NoSuch);
}

int
ModuleRegistryRemove(ModuleRecord* __Rec__)
{
    if (!__Rec__)
    {
        return -BadArgs;
    }

    ModuleRecord* Prev = 0;
    ModuleRecord* It   = ModuleListHead;
    while (It)
    {
        if (It == __Rec__)
        {
            if (Prev)
            {
                Prev->Next = It->Next;
            }
            else
            {
                ModuleListHead = It->Next;
            }
            return SysOkay;
        }
        Prev = It;
        It   = It->Next;
    }
    return -NoSuch;
}
