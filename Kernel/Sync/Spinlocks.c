#include <Errnos.h>
#include <SMP.h>
#include <Sync.h>

SpinLock        ConsoleLock;
static uint64_t SavedFlags[MaxCPUs];

void
InitializeSpinLock(SpinLock* __Lock__, const char* __Name__, SysErr* __Err__ _unused)
{
    __Lock__->Lock  = 0;          /* Initially unlocked */
    __Lock__->CpuId = 0xFFFFFFFF; /* No owner (kernel value) */
    __Lock__->Name  = __Name__;   /* Assign name for debugging */
}

void
AcquireSpinLock(SpinLock* __Lock__, SysErr* __Err__ _unused)
{
    uint32_t CpuId = GetCurrentCpuId();

    uint64_t Flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(Flags)::"memory");

    while (1)
    {
        uint32_t Expected = 0; /* Expect the lock to be free (0) */
        if (__atomic_compare_exchange_n(
                &__Lock__->Lock, &Expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            /* Successfully acquired the lock */
            __Lock__->CpuId   = CpuId;
            SavedFlags[CpuId] = Flags; /* Save flags for this CPU */
            break;
        }
        /* Lock is held by another CPU, spin with pause for efficiency */
        __asm__ volatile("pause");
    }
}

void
ReleaseSpinLock(SpinLock* __Lock__, SysErr* __Err__ _unused)
{
    uint32_t CpuId = GetCurrentCpuId();

    uint64_t Flags = SavedFlags[CpuId];

    __Lock__->CpuId = 0xFFFFFFFF;                           /* Reset owner to none */
    __atomic_store_n(&__Lock__->Lock, 0, __ATOMIC_RELEASE); /* Unlock */

    __asm__ volatile("pushq %0; popfq" ::"r"(Flags) : "memory");
}

bool
TryAcquireSpinLock(SpinLock* __Lock__)
{
    uint32_t Expected = 0;
    if (__atomic_compare_exchange_n(
            &__Lock__->Lock, &Expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        __Lock__->CpuId = GetCurrentCpuId();
        return true;
    }

    return false;
}
