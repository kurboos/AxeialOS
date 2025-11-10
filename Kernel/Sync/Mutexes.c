#include <Sync.h>   /* Synchronization primitives definitions */
#include <SMP.h>    /* Symmetric multiprocessing functions */

/*
 * InitializeMutex - Initialize a Mutex Structure
 *
 * Sets up a mutex to its initial unlocked state. The mutex is initialized with
 * no owner (set to kernel/0xFFFFFFFF), zero recursion count, and unlocked state.
 * A name can be assigned for debugging purposes.
 *
 * Parameters:
 * - __Mutex__: Pointer to the mutex structure to initialize.
 * - __Name__: Optional name string for the mutex (can be NULL).
 *
 * This function should be called before using a mutex for the first time.
 */
void
InitializeMutex(Mutex* __Mutex__, const char* __Name__)
{
    __Mutex__->Lock = 0;                    /* Initially unlocked */
    __Mutex__->Owner = 0xFFFFFFFF;          /* No owner (kernel value) */
    __Mutex__->RecursionCount = 0;         /* No recursive locks */
    __Mutex__->Name = __Name__;             /* Assign name for debugging */
}

/*
 * AcquireMutex - Acquire Exclusive Access to a Mutex
 *
 * Attempts to acquire the mutex, blocking until it becomes available. If the
 * current CPU already owns the mutex, the recursion count is incremented
 * instead of blocking (recursive locking).
 *
 * The function uses an atomic compare-and-exchange operation to acquire the
 * lock, ensuring thread-safety across multiple CPUs. If the lock is already
 * held by another CPU, the function spins with pause instructions to reduce
 * CPU usage while waiting.
 *
 * Parameters:
 * - __Mutex__: Pointer to the mutex to acquire.
 *
 * Note: This function will block indefinitely if a deadlock occurs.
 */
void
AcquireMutex(Mutex* __Mutex__)
{
    uint32_t CpuId = GetCurrentCpuId();

    /*
     * Check if the current CPU already owns this mutex.
     * If so, increment the recursion count and return (recursive locking).
     */
    if (__Mutex__->Owner == CpuId)
    {
        __Mutex__->RecursionCount++;
        return;
    }

    /*
     * Attempt to acquire the lock using atomic operations.
     * Spin until the lock becomes available.
     */
    while (1)
    {
        uint32_t Expected = 0;  /* Expect the lock to be free (0) */
        if (__atomic_compare_exchange_n(&__Mutex__->Lock, &Expected, 1,
            false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            /* Successfully acquired the lock */
            __Mutex__->Owner = CpuId;
            __Mutex__->RecursionCount = 1;
            break;
        }

        /* Lock is held by another CPU, spin with pause for efficiency */
        __asm__ volatile("pause");
    }
}

/*
 * ReleaseMutex - Release a Previously Acquired Mutex
 *
 * Releases the mutex if the current CPU is the owner. If the mutex was acquired
 * recursively, decrements the recursion count instead of fully releasing it.
 * Only when the recursion count reaches zero is the mutex actually unlocked.
 *
 * Parameters:
 * - __Mutex__: Pointer to the mutex to release.
 *
 * Note: If a CPU attempts to release a mutex it doesn't own, the function
 * returns silently without action.
 */
void
ReleaseMutex(Mutex* __Mutex__)
{
    uint32_t CpuId = GetCurrentCpuId();

    /*
     * Verify that the current CPU owns this mutex.
     * If not, ignore the release attempt.
     */
    if (__Mutex__->Owner != CpuId)
        return;

    /*
     * Decrement the recursion count.
     */
    __Mutex__->RecursionCount--;

    /*
     * If recursion count reaches zero, fully release the mutex.
     */
    if (__Mutex__->RecursionCount == 0)
    {
        __Mutex__->Owner = 0xFFFFFFFF;  /* Reset owner to kernel/none */
        __atomic_store_n(&__Mutex__->Lock, 0, __ATOMIC_RELEASE);  /* Unlock atomically */
    }
}

/*
 * TryAcquireMutex - Attempt to Acquire Mutex Without Blocking
 *
 * Attempts to acquire the mutex without blocking. Returns immediately with
 * success or failure status. Supports recursive locking like AcquireMutex.
 *
 * Returns:
 * - true: Mutex was successfully acquired (or recursion count incremented).
 * - false: Mutex is held by another CPU and could not be acquired.
 *
 * Parameters:
 * - __Mutex__: Pointer to the mutex to attempt to acquire.
 */
bool
TryAcquireMutex(Mutex* __Mutex__)
{
    uint32_t CpuId = GetCurrentCpuId();

    /*
     * Check for recursive acquisition.
     */
    if (__Mutex__->Owner == CpuId)
    {
        __Mutex__->RecursionCount++;
        return true;
    }

    /*
     * Attempt to acquire the lock atomically without blocking.
     */
    uint32_t Expected = 0;
    if (__atomic_compare_exchange_n(&__Mutex__->Lock, &Expected, 1,
        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        /* Successfully acquired */
        __Mutex__->Owner = CpuId;
        __Mutex__->RecursionCount = 1;
        return true;
    }

    /* Failed to acquire */
    return false;
}
