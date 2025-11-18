#include <AllTypes.h>
#include <KHeap.h>
#include <KMods.h>
#include <KrnPrintf.h>
#include <ModMemMgr.h>
#include <String.h>
#include <VFS.h>

/*Globals*/
ModuleRecord* ModuleListHead = 0;

/**
 * @brief Initialize the module registry
 *
 * Sets up the global registry list head. Safe to call multiple times.
 *
 * @return 0 on success
 */
int
ModuleRegistryInit(void)
{
    ModuleListHead = ModuleListHead;
    return 0;
}

/**
 * @brief Add a module record to the registry
 *
 * Inserts the given record at the head of the global registry list.
 *
 * @param __Rec__ Module record to add
 * @return 0 on success, -1 on failure
 */
int
ModuleRegistryAdd(ModuleRecord* __Rec__)
{
    if (!__Rec__)
    {
        PError("MOD: Registry add invalid record\n");
        return -1;
    }

    __Rec__->Next  = ModuleListHead;
    ModuleListHead = __Rec__;
    return 0;
}

/**
 * @brief Find a module record by name
 *
 * Linear search through the registry for the given module name.
 *
 * @param __Name__ Module name/path string
 * @return Pointer to ModuleRecord on success, NULL if not found
 */
ModuleRecord*
ModuleRegistryFind(const char* __Name__)
{
    if (!__Name__)
    {
        PError("MOD: Registry find invalid name\n");
        return 0;
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
    return 0;
}

/**
 * @brief Remove a module record from the registry
 *
 * Unlinks the given record from the global list. Does not free the record.
 *
 * @param __Rec__ Module record to remove
 * @return 0 on success, -1 if not found
 */
int
ModuleRegistryRemove(ModuleRecord* __Rec__)
{
    if (!__Rec__)
    {
        PError("MOD: Registry remove invalid record\n");
        return -1;
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
            return 0;
        }
        Prev = It;
        It   = It->Next;
    }
    PError("MOD: Registry remove not found\n");
    return -1;
}
