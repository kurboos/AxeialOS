#include <LimineSMP.h>      /* Limine SMP protocol definitions */
#include <LimineServices.h> /* Limine service interfaces */
#include <SMP.h>            /* SMP manager and CPU structures */
#include <Timer.h>          /* Timer functions for timeouts */
#include <VMM.h>            /* Virtual memory management for APIC access */

/** @brief Some Globals */
SmpManager        Smp;
SpinLock          SMPLock;
volatile uint32_t CpuStartupCount = 0;

/**
 * @brief Get the current CPU ID.
 *
 * @details Reads the IA32_APIC_BASE MSR to locate the Local APIC base address,
 * 			then extracts the LAPIC ID from the APIC ID register. The LAPIC ID
 * 			is matched against the SMP manager’s CPU list to return the logical
 * 			CPU index.
 *
 * @return The logical CPU ID (index in Smp.Cpus).
 *
 * @note If the LAPIC ID is not found in the SMP manager, the raw LAPIC ID
 *       is returned instead.
 */
uint32_t
GetCurrentCpuId(void)
{

    uint64_t ApicBaseMsr  = ReadMsr(0x1B);            /* IA32_APIC_BASE Model-Specific Register */
    uint64_t ApicPhysBase = ApicBaseMsr & 0xFFFFF000; /* Extract 4KB-aligned base address */

    volatile uint32_t* ApicIdReg = (volatile uint32_t*)(PhysToVirt(ApicPhysBase) + 0x20);
    uint32_t           ApicId    = (*ApicIdReg >> 24) & 0xFF;

    for (uint32_t Index = 0; Index < Smp.CpuCount; Index++)
    {
        if (Smp.Cpus[Index].ApicId == ApicId)
        {
            return Index;
        }
    }

    return ApicId;
}

/**
 * @brief Initialize symmetric multiprocessing (SMP).
 *
 * @details Uses Limine’s SMP protocol to detect and initialize multiple CPUs.
 * 			If no SMP response is available, falls back to single-CPU mode.
 * 			Populates the SmpManager structure with CPU information.
 * 			Marks the BSP (Bootstrap Processor) as online.
 * 			Sets up Application Processors (APs) with the entry point and waits
 * 			for them to signal startup.
 *
 * @return void
 *
 * @note This function must be called during kernel initialization.
 *       It ensures all CPUs are tracked and properly started.
 */
void
InitializeSmp(void)
{
    PInfo("SMP: Initializing using Limine support\n");

    if (!EarlyLimineSmp.response)
    {
        PWarn("SMP: No SMP response from Limine, using single CPU\n");
        Smp.CpuCount          = 1;
        Smp.OnlineCpus        = 1;
        Smp.BspApicId         = 0;
        Smp.Cpus[0].ApicId    = 0;
        Smp.Cpus[0].CpuNumber = 0;
        Smp.Cpus[0].Status    = CPU_STATUS_ONLINE;
        Smp.Cpus[0].Started   = 1;
        return;
    }

    struct limine_smp_response* SmpResponse = EarlyLimineSmp.response;

    PInfo("SMP: Limine detected %u CPU(s)\n", SmpResponse->cpu_count);
    PInfo("SMP: BSP LAPIC ID: %u\n", SmpResponse->bsp_lapic_id);

    Smp.CpuCount    = SmpResponse->cpu_count;
    Smp.OnlineCpus  = 1; /* BSP is already online */
    Smp.BspApicId   = SmpResponse->bsp_lapic_id;
    CpuStartupCount = 0;

    for (uint32_t Index = 0; Index < MaxCPUs; Index++)
    {
        Smp.Cpus[Index].Status     = CPU_STATUS_OFFLINE;
        Smp.Cpus[Index].Started    = 0;
        Smp.Cpus[Index].LimineInfo = NULL;
    }

    uint32_t StartedAps = 0;
    for (uint64_t Index = 0; Index < SmpResponse->cpu_count; Index++)
    {
        struct limine_smp_info* CpuInfo = SmpResponse->cpus[Index];

        Smp.Cpus[Index].ApicId     = CpuInfo->lapic_id;
        Smp.Cpus[Index].CpuNumber  = Index;
        Smp.Cpus[Index].LimineInfo = CpuInfo;

        if (CpuInfo->lapic_id == SmpResponse->bsp_lapic_id)
        {

            Smp.Cpus[Index].Status  = CPU_STATUS_ONLINE;
            Smp.Cpus[Index].Started = 1;
            PDebug("SMP: BSP CPU %u (LAPIC ID %u)\n", Index, CpuInfo->lapic_id);
        }
        else
        {

            Smp.Cpus[Index].Status = CPU_STATUS_STARTING;
            CpuInfo->goto_address  = ApEntryPoint; /* Set AP entry point */
            StartedAps++;
            PInfo("SMP: Starting AP %u (LAPIC ID %u)\n", Index, CpuInfo->lapic_id);
        }
    }

    if (StartedAps > 0)
    {
        PInfo("SMP: Waiting for %u APs to start...\n", StartedAps);

#define ApCountTimeout 99999999 /* Large timeout value for AP startup */
        uint32_t Timeout = ApCountTimeout;

        while (CpuStartupCount < StartedAps && Timeout > 0)
        {
            __asm__ volatile("pause");
            Timeout--;
        }

        if (CpuStartupCount > StartedAps)
        {
            PWarn("SMP: %u out of %u APs started!\n", CpuStartupCount, StartedAps);
        }
        else
        {
            PSuccess("SMP: %u out of %u APs started successfully\n", CpuStartupCount, StartedAps);
        }
    }

    PSuccess("SMP initialized: %u CPU(s) total, %u online\n", Smp.CpuCount, Smp.OnlineCpus);
}
