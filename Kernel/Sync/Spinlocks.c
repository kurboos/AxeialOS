#include <SMP.h>  /* Symmetric multiprocessing functions */
#include <Sync.h> /* Synchronization primitives definitions */

/** @brief Some Globals and Statics */
SpinLock        ConsoleLock;
static uint64_t SavedFlags[MaxCPUs];

/**
 * @brief Initialize a spinlock.
 *
 * @details Sets the lock to an unlocked state, clears the owner CPU ID,
 * 			and assigns a name for debugging purposes.
 *
 * @param __Lock__ Pointer to the SpinLock structure.
 * @param __Name__ Human-readable name for debugging.
 *
 * @return void
 */
void
InitializeSpinLock(SpinLock* __Lock__, const char* __Name__)
{
    __Lock__->Lock  = 0;          /* Initially unlocked */
    __Lock__->CpuId = 0xFFFFFFFF; /* No owner (kernel value) */
    __Lock__->Name  = __Name__;   /* Assign name for debugging */
}

/**
 * @brief Acquire a spinlock.
 *
 * @details Spins until the lock becomes available. Disables interrupts
 * 			while holding the lock to prevent deadlocks. Saves CPU flags
 * 			for later restoration.
 *
 * @param __Lock__ Pointer to the SpinLock structure.
 *
 * @return void
 */
void
AcquireSpinLock(SpinLock* __Lock__)
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

/**
 * @brief Release a spinlock.
 *
 * @details Restores saved CPU flags, clears the lock, and resets the owner.
 *
 * @param __Lock__ Pointer to the SpinLock structure.
 *
 * @return void
 */
void
ReleaseSpinLock(SpinLock* __Lock__)
{
    uint32_t CpuId = GetCurrentCpuId();

    uint64_t Flags = SavedFlags[CpuId];

    __Lock__->CpuId = 0xFFFFFFFF;                           /* Reset owner to none */
    __atomic_store_n(&__Lock__->Lock, 0, __ATOMIC_RELEASE); /* Unlock */

    __asm__ volatile("pushq %0; popfq" ::"r"(Flags) : "memory");
}

/**
 * @brief Attempt to acquire a spinlock without blocking.
 *
 * @details Uses atomic compare-and-exchange to acquire the lock if free.
 *
 * @param __Lock__ Pointer to the SpinLock structure.
 *
 * @return true if acquired successfully, false otherwise.
 */
bool
TryAcquireSpinLock(SpinLock* __Lock__)
{
    uint32_t Expected = 0;
    if (__atomic_compare_exchange_n(
            &__Lock__->Lock, &Expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        /* Successfully acquired */
        __Lock__->CpuId = GetCurrentCpuId();
        return true;
    }

    /* Failed to acquire */
    return false;
}
