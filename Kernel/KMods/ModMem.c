#include <ModMemMgr.h>

ModuleMemoryManager ModMem = {0, 0, 0};

/*

    'ModMemMgr' has been deprecated (Partially) as a more optimized and faster allocation through
    direct PMM and VMM

*/

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