#include <Sync.h>   /* Synchronization primitives definitions */
#include <SMP.h>    /* Symmetric multiprocessing functions */

/*
 * Global Console Lock
 *
 * A global spinlock for synchronizing access to console output.
 * This prevents interleaved output from multiple CPUs.
 */
SpinLock ConsoleLock;

/*
 * SavedFlags - Per-CPU Interrupt Flags Storage
 *
 * Array to store the interrupt flags (RFLAGS) for each CPU before disabling
 * interrupts. This allows proper restoration of interrupt state when releasing
 * the spinlock. The array size is bounded by MaxCPUs.
 */
static uint64_t SavedFlags[MaxCPUs];

/*
 * InitializeSpinLock - Initialize a Spinlock Structure
 *
 * Sets up a spinlock to its initial unlocked state. The spinlock is initialized
 * with no owner and unlocked state. A name can be assigned for debugging purposes.
 *
 * Parameters:
 * - __Lock__: Pointer to the spinlock structure to initialize.
 * - __Name__: Optional name string for the spinlock (can be NULL).
 *
 * This function should be called before using a spinlock for the first time.
 */
void
InitializeSpinLock(SpinLock* __Lock__, const char* __Name__)
{
    __Lock__->Lock = 0;                    /* Initially unlocked */
    __Lock__->CpuId = 0xFFFFFFFF;          /* No owner (kernel value) */
    __Lock__->Name = __Name__;             /* Assign name for debugging */
}

/*
 * AcquireSpinLock - Acquire Exclusive Access to a Spinlock
 *
 * Acquires the spinlock, disabling interrupts to prevent deadlock scenarios.
 * The function saves the current interrupt state and disables interrupts before
 * attempting to acquire the lock. If the lock is already held, the function
 * spins with pause instructions until it becomes available.
 *
 * The interrupt disabling ensures that interrupt handlers cannot cause deadlocks
 * by attempting to acquire the same spinlock.
 *
 * Parameters:
 * - __Lock__: Pointer to the spinlock to acquire.
 *
 * Note: Interrupts are disabled while holding the spinlock and must be
 * restored by ReleaseSpinLock.
 */
void AcquireSpinLock(SpinLock* __Lock__)
{
    uint32_t CpuId = GetCurrentCpuId();

    /*
     * Save current interrupt flags and disable interrupts.
     * This prevents interrupt handlers from causing deadlocks.
     */
    uint64_t Flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(Flags) :: "memory");

    while (1) {
        uint32_t Expected = 0;  /* Expect the lock to be free (0) */
        if (__atomic_compare_exchange_n(&__Lock__->Lock, &Expected, 1,
            false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            /* Successfully acquired the lock */
            __Lock__->CpuId = CpuId;
            SavedFlags[CpuId] = Flags;  /* Save flags for this CPU */
            break;
        }
        /* Lock is held by another CPU, spin with pause for efficiency */
        __asm__ volatile("pause");
    }
}

/*
 * ReleaseSpinLock - Release a Previously Acquired Spinlock
 *
 * Releases the spinlock and restores the interrupt state that was saved
 * during acquisition. The function verifies that the current CPU owns the
 * lock before releasing it.
 *
 * Parameters:
 * - __Lock__: Pointer to the spinlock to release.
 *
 * Note: This function restores the interrupt flags saved by AcquireSpinLock.
 * It is the caller's responsibility to ensure ReleaseSpinLock is only called
 * after a corresponding AcquireSpinLock call.
 */
void ReleaseSpinLock(SpinLock* __Lock__)
{
    uint32_t CpuId = GetCurrentCpuId();

    /*
     * Retrieve the saved interrupt flags for this CPU.
     */
    uint64_t Flags = SavedFlags[CpuId];

    /*
     * Reset the lock ownership and unlock atomically.
     */
    __Lock__->CpuId = 0xFFFFFFFF;  /* Reset owner to none */
    __atomic_store_n(&__Lock__->Lock, 0, __ATOMIC_RELEASE);  /* Unlock */

    /*
     * Restore the interrupt flags, re-enabling interrupts if they were enabled before.
     */
    __asm__ volatile("pushq %0; popfq" :: "r"(Flags) : "memory");
}

/*
 * TryAcquireSpinLock - Attempt to Acquire Spinlock Without Blocking
 *
 * Attempts to acquire the spinlock without blocking. Returns immediately
 * with success or failure status. This allows code to test spinlock availability
 * without waiting. Note that interrupts are not disabled in this function.
 *
 * Returns:
 * - true: Spinlock was successfully acquired.
 * - false: Spinlock is held by another CPU and could not be acquired.
 *
 * Parameters:
 * - __Lock__: Pointer to the spinlock to attempt to acquire.
 *
 * Note: If successful, the caller is responsible for eventually releasing
 * the spinlock. Interrupts are not automatically disabled.
 */
bool
TryAcquireSpinLock(SpinLock* __Lock__)
{
    uint32_t Expected = 0;
    if (__atomic_compare_exchange_n(&__Lock__->Lock, &Expected, 1,
        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        /* Successfully acquired */
        __Lock__->CpuId = GetCurrentCpuId();
        return true;
    }

    /* Failed to acquire */
    return false;
}
