#pragma once

#include <AllTypes.h>
#include <KrnPrintf.h>
#include <LimineServices.h>

/**
 * Maximum CPUs Supported
 */
#define MaxCPUs 256

/**
 * CPU Status
 */
typedef
enum
{

    CPU_STATUS_OFFLINE,
    CPU_STATUS_STARTING,
    CPU_STATUS_ONLINE,
    CPU_STATUS_FAILED
	
} CpuStatus;

/**
 * CPU Information
 */
typedef
struct
{

    uint32_t ApicId;
    uint32_t CpuNumber;
    CpuStatus Status;
    volatile uint32_t Started;
    struct limine_smp_info *LimineInfo;

} CpuInfo;

/**
 * SMP Manager
 */
typedef
struct
{

    uint32_t CpuCount;
    uint32_t OnlineCpus;
    uint32_t BspApicId;
    CpuInfo Cpus[MaxCPUs];

} SmpManager;

/**
 * Globals
 */
extern SmpManager Smp;
extern volatile uint32_t CpuStartupCount;

/**
 * Functions
 */
void InitializeSmp(void);
void ApEntryPoint(struct limine_smp_info *__CpuInfo__);
uint32_t GetCurrentCpuId(void);
void PerCpuInterruptInit(uint32_t __CpuId__, uint64_t __InterruptStack__);