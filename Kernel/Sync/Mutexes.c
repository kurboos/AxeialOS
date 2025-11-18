#include <SMP.h>  /* Symmetric multiprocessing functions */
#include <Sync.h> /* Synchronization primitives definitions */

/**
 * @brief Initialize a mutex.
 *
 * @details Sets the lock to unlocked, clears the owner, resets recursion count,
 * 			and assigns a name for debugging.
 *
 * @param __Mutex__ Pointer to the Mutex structure.
 * @param __Name__ Human-readable name for debugging.
 *
 * @return void
 */
void
InitializeMutex(Mutex* __Mutex__, const char* __Name__)
{
    __Mutex__->Lock           = 0;          /* Initially unlocked */
    __Mutex__->Owner          = 0xFFFFFFFF; /* No owner (kernel value) */
    __Mutex__->RecursionCount = 0;          /* No recursive locks */
    __Mutex__->Name           = __Name__;   /* Assign name for debugging */
}

/**
 * @brief Acquire a mutex.
 *
 * @details Spins until the lock becomes available. Supports recursive locking
 * 			if the same CPU already owns the mutex.
 *
 * @param __Mutex__ Pointer to the Mutex structure.
 *
 * @return void
 */
void
AcquireMutex(Mutex* __Mutex__)
{
    uint32_t CpuId = GetCurrentCpuId();

    if (__Mutex__->Owner == CpuId)
    {
        __Mutex__->RecursionCount++;
        return;
    }

    while (1)
    {
        uint32_t Expected = 0; /* Expect the lock to be free (0) */
        if (__atomic_compare_exchange_n(
                &__Mutex__->Lock, &Expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            /* Successfully acquired the lock */
            __Mutex__->Owner          = CpuId;
            __Mutex__->RecursionCount = 1;
            break;
        }

        /* Lock is held by another CPU, spin with pause for efficiency */
        __asm__ volatile("pause");
    }
}

/**
 * @brief Release a mutex.
 *
 * @details Decrements recursion count. If it reaches zero, clears the owner
 * 			and unlocks the mutex.
 *
 * @param __Mutex__ Pointer to the Mutex structure.
 *
 * @return void
 */
void
ReleaseMutex(Mutex* __Mutex__)
{
    uint32_t CpuId = GetCurrentCpuId();

    if (__Mutex__->Owner != CpuId)
    {
        return;
    }

    __Mutex__->RecursionCount--;

    if (__Mutex__->RecursionCount == 0)
    {
        __Mutex__->Owner = 0xFFFFFFFF;                           /* Reset owner to kernel/none */
        __atomic_store_n(&__Mutex__->Lock, 0, __ATOMIC_RELEASE); /* Unlock atomically */
    }
}

/**
 * @brief Attempt to acquire a mutex without blocking.
 *
 * @details Acquires the lock if free, or increments recursion count if already owned
 * 			by the current CPU.
 *
 * @param __Mutex__ Pointer to the Mutex structure.
 *
 * @return true if acquired successfully, false otherwise.
 */
bool
TryAcquireMutex(Mutex* __Mutex__)
{
    uint32_t CpuId = GetCurrentCpuId();

    if (__Mutex__->Owner == CpuId)
    {
        __Mutex__->RecursionCount++;
        return true;
    }

    uint32_t Expected = 0;
    if (__atomic_compare_exchange_n(
            &__Mutex__->Lock, &Expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        /* Successfully acquired */
        __Mutex__->Owner          = CpuId;
        __Mutex__->RecursionCount = 1;
        return true;
    }

    /* Failed to acquire */
    return false;
}
