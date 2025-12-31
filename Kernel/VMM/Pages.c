#include <VMM.h>

uint64_t*
GetPageTable(uint64_t* __Pml4__, uint64_t __VirtAddr__, int __Level__, int __Create__)
{
    uint32_t  Pml4Index    = (__VirtAddr__ >> 39) & 0x1FF;
    uint32_t  PdptIndex    = (__VirtAddr__ >> 30) & 0x1FF;
    uint32_t  PdIndex      = (__VirtAddr__ >> 21) & 0x1FF;
    uint64_t* CurrentTable = __Pml4__;
    uint32_t  CurrentIndex = Pml4Index;

    for (int Level = 4; Level > __Level__; Level--)
    {
        if (!(CurrentTable[CurrentIndex] & PTEPRESENT))
        {
            if (!__Create__)
            {
                return Error_TO_Pointer(-BadAlloc);
            }

            uint64_t NewTablePhys = AllocPage();
            if (!NewTablePhys)
            {
                return Error_TO_Pointer(-BadAlloc);
            }

            uint64_t* NewTable = (uint64_t*)PhysToVirt(NewTablePhys);

            for (uint32_t Index = 0; Index < PageTableEntries; Index++)
            {
                NewTable[Index] = 0;
            }

            CurrentTable[CurrentIndex] = NewTablePhys | PTEPRESENT | PTEWRITABLE | PTEUSER;

            PDebug("Created page table at level %d: 0x%016lx\n", Level - 1, NewTablePhys);
        }

        uint64_t NextTablePhys = CurrentTable[CurrentIndex] & 0xFFFFFFFFFFFFF000ULL;

        CurrentTable = (uint64_t*)PhysToVirt(NextTablePhys);

        switch (Level - 1)
        {
            case 3: /* Moving to PDPT level */
                CurrentIndex = PdptIndex;
                break;

            case 2: /* Moving to PD level */
                CurrentIndex = PdIndex;
                break;

            case 1: /* Reached PT level - this is our target */
                return CurrentTable;
        }
    }

    return CurrentTable;
}

void
FlushTlb(uint64_t __VirtAddr__, SysErr* __Err__ __attribute((unused)))
{
    __asm__ volatile("invlpg (%0)" ::"r"(__VirtAddr__) : "memory");
}

void
FlushAllTlb(SysErr* __Err__ __attribute((unused)))
{
    uint64_t Cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(Cr3));

    __asm__ volatile("mov %0, %%cr3" ::"r"(Cr3) : "memory");
}
