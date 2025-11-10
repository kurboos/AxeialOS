#include <Sync.h>   /* Synchronization primitives definitions */
#include <SMP.h>    /* Symmetric multiprocessing functions */

/*
 * InitializeSemaphore - Initialize a Semaphore Structure
 *
 * Sets up a semaphore with an initial count and optional name. The semaphore
 * count determines how many concurrent accesses are allowed. A count of 1
 * creates a binary semaphore, while higher counts allow multiple concurrent
 * accesses.
 *
 * Parameters:
 * - __Semaphore__: Pointer to the semaphore structure to initialize.
 * - __InitialCount__: Initial count value (must be >= 0).
 * - __Name__: Optional name string for the semaphore (can be NULL).
 *
 * This function should be called before using a semaphore for the first time.
 */
void
InitializeSemaphore(Semaphore* __Semaphore__, int32_t __InitialCount__, const char* __Name__)
{
    __Semaphore__->Count = __InitialCount__;        /* Set initial count */
    __Semaphore__->WaitQueue = 0;                   /* No waiting threads initially */
    InitializeSpinLock(&__Semaphore__->QueueLock, "SemaphoreQueue");  /* Initialize queue lock */
    __Semaphore__->Name = __Name__;                 /* Assign name for debugging */
}

/*
 * AcquireSemaphore - Acquire Access to a Semaphore
 *
 * Decrements the semaphore count, blocking if the count is already zero.
 * This operation is atomic and thread-safe across multiple CPUs. If the
 * semaphore count is greater than zero, it is decremented and the function
 * returns immediately. If the count is zero, the function spins until the
 * count becomes positive.
 *
 * Parameters:
 * - __Semaphore__: Pointer to the semaphore to acquire.
 *
 * Note: This function will block indefinitely if the semaphore is never
 * released by another thread.
 */
void
AcquireSemaphore(Semaphore* __Semaphore__)
{
    while (1)
    {
        /*
         * Atomically load the current count to check availability.
         */
        int32_t CurrentCount = __atomic_load_n(&__Semaphore__->Count, __ATOMIC_ACQUIRE);

        if (CurrentCount > 0)
        {
            /*
             * Attempt to decrement the count atomically.
             * If successful, we have acquired the semaphore.
             */
            if (__atomic_compare_exchange_n(&__Semaphore__->Count, &CurrentCount,
                CurrentCount - 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            {
                break;  /* Successfully acquired */
            }
            /* Compare-exchange failed, retry */
        }

        /* Count is zero or compare-exchange failed, spin with pause */
        __asm__ volatile("pause");
    }
}

/*
 * ReleaseSemaphore - Release a Semaphore
 *
 * Increments the semaphore count, potentially allowing waiting threads to
 * proceed. This operation is atomic and signals to other threads that a
 * resource has been released or an event has occurred.
 *
 * Parameters:
 * - __Semaphore__: Pointer to the semaphore to release.
 *
 * Note: It is the caller's responsibility to ensure ReleaseSemaphore is only
 * called after a corresponding AcquireSemaphore call.
 */
void
ReleaseSemaphore(Semaphore* __Semaphore__)
{
    /*
     * Atomically increment the semaphore count.
     * This may wake up waiting threads in AcquireSemaphore.
     */
    __atomic_fetch_add(&__Semaphore__->Count, 1, __ATOMIC_RELEASE);
}

/*
 * TryAcquireSemaphore - Attempt to Acquire Semaphore Without Blocking
 *
 * Attempts to decrement the semaphore count without blocking. Returns
 * immediately with success or failure status. This allows threads to
 * test semaphore availability without waiting.
 *
 * Returns:
 * - true: Semaphore was successfully acquired (count decremented).
 * - false: Semaphore is not available (count is zero).
 *
 * Parameters:
 * - __Semaphore__: Pointer to the semaphore to attempt to acquire.
 */
bool
TryAcquireSemaphore(Semaphore* __Semaphore__)
{
    /*
     * Atomically load the current count to check availability.
     */
    int32_t CurrentCount = __atomic_load_n(&__Semaphore__->Count, __ATOMIC_ACQUIRE);

    if (CurrentCount > 0)
    {
        /*
         * Attempt to decrement the count atomically.
         * If successful, we have acquired the semaphore.
         */
        if (__atomic_compare_exchange_n(&__Semaphore__->Count, &CurrentCount,
            CurrentCount - 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            return true;  /* Successfully acquired */
        }
    }

    /* Count is zero or compare-exchange failed */
    return false;
}
