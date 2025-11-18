#pragma once

#include <AllTypes.h>
#include <KExports.h>

/**
 * Spinlock
 */
typedef struct
{

    volatile uint32_t Lock;
    uint32_t          CpuId;
    const char*       Name;
    uint64_t          Flags;

} SpinLock;

void InitializeSpinLock(SpinLock* __Lock__, const char* __Name__);
void AcquireSpinLock(SpinLock* __Lock__);
void ReleaseSpinLock(SpinLock* __Lock__);
bool TryAcquireSpinLock(SpinLock* __Lock__);

/**
 * Mutex
 */
typedef struct
{
    volatile uint32_t Lock;
    uint32_t          Owner;
    uint32_t          RecursionCount;
    const char*       Name;
} Mutex;

void InitializeMutex(Mutex* __Mutex__, const char* __Name__);
void AcquireMutex(Mutex* __Mutex__);
void ReleaseMutex(Mutex* __Mutex__);
bool TryAcquireMutex(Mutex* __Mutex__);

/**
 * Semaphore
 */
typedef struct
{
    volatile int32_t  Count;
    volatile uint32_t WaitQueue;
    SpinLock          QueueLock;
    const char*       Name;
} Semaphore;

void InitializeSemaphore(Semaphore* __Semaphore__, int32_t __InitialCount__, const char* __Name__);
void AcquireSemaphore(Semaphore* __Semaphore__);
void ReleaseSemaphore(Semaphore* __Semaphore__);
bool TryAcquireSemaphore(Semaphore* __Semaphore__);

/**
 * Global Console Lock
 */
extern SpinLock ConsoleLock;

/**
 * Public
 */
KEXPORT(InitializeSpinLock);
KEXPORT(AcquireSpinLock);
KEXPORT(ReleaseSpinLock);
KEXPORT(TryAcquireSpinLock);

KEXPORT(InitializeMutex);
KEXPORT(AcquireMutex);
KEXPORT(ReleaseMutex);
KEXPORT(TryAcquireMutex);

KEXPORT(InitializeSemaphore);
KEXPORT(AcquireSemaphore);
KEXPORT(ReleaseSemaphore);
KEXPORT(TryAcquireSemaphore);
