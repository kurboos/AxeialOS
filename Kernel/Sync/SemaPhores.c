#include <SMP.h>  /* Symmetric multiprocessing functions */
#include <Sync.h> /* Synchronization primitives definitions */

/**
 * @brief Initialize a semaphore.
 *
 * @details Sets the initial count, clears the wait queue, and initializes
 * 			the internal queue lock. Assigns a name for debugging.
 *
 * @param __Semaphore__ Pointer to the Semaphore structure.
 * @param __InitialCount__ Initial resource count.
 * @param __Name__ Human-readable name for debugging.
 *
 * @return void
 */
void
InitializeSemaphore(Semaphore* __Semaphore__, int32_t __InitialCount__, const char* __Name__)
{
    __Semaphore__->Count     = __InitialCount__; /* Set initial count */
    __Semaphore__->WaitQueue = 0;                /* No waiting threads initially */
    InitializeSpinLock(&__Semaphore__->QueueLock, "SemaphoreQueue"); /* Initialize queue lock */
    __Semaphore__->Name = __Name__;                                  /* Assign name for debugging */
}

/**
 * @brief Acquire a semaphore.
 *
 * @details Spins until the count is greater than zero, then decrements it atomically.
 *
 * @param __Semaphore__ Pointer to the Semaphore structure.
 *
 * @return void
 */
void
AcquireSemaphore(Semaphore* __Semaphore__)
{
    while (1)
    {

        int32_t CurrentCount = __atomic_load_n(&__Semaphore__->Count, __ATOMIC_ACQUIRE);

        if (CurrentCount > 0)
        {

            if (__atomic_compare_exchange_n(&__Semaphore__->Count,
                                            &CurrentCount,
                                            CurrentCount - 1,
                                            false,
                                            __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED))
            {
                break; /* Successfully acquired */
            }
            /* Compare-exchange failed, retry */
        }

        /* Count is zero or compare-exchange failed, spin with pause */
        __asm__ volatile("pause");
    }
}

/**
 * @brief Release a semaphore.
 *
 * @details Atomically increments the semaphore count, potentially unblocking waiters.
 *
 * @param __Semaphore__ Pointer to the Semaphore structure.
 *
 * @return void
 */
void
ReleaseSemaphore(Semaphore* __Semaphore__)
{

    __atomic_fetch_add(&__Semaphore__->Count, 1, __ATOMIC_RELEASE);
}

/**
 * @brief Attempt to acquire a semaphore without blocking.
 *
 * @details Decrements the count if greater than zero, otherwise fails immediately.
 *
 * @param __Semaphore__ Pointer to the Semaphore structure.
 *
 * @return true if acquired successfully, false otherwise.
 */
bool
TryAcquireSemaphore(Semaphore* __Semaphore__)
{

    int32_t CurrentCount = __atomic_load_n(&__Semaphore__->Count, __ATOMIC_ACQUIRE);

    if (CurrentCount > 0)
    {

        if (__atomic_compare_exchange_n(&__Semaphore__->Count,
                                        &CurrentCount,
                                        CurrentCount - 1,
                                        false,
                                        __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
        {
            return true; /* Successfully acquired */
        }
    }

    /* Count is zero or compare-exchange failed */
    return false;
}
