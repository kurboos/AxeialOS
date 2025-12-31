#include <LimineSMP.h>
#include <LimineServices.h>
#include <SMP.h>
#include <Timer.h>
#include <VMM.h>

SmpManager        Smp;
SpinLock          SMPLock;
volatile uint32_t CpuStartupCount = 0;

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

void
InitializeSmp(SysErr* __Err__)
{

    if (!EarlyLimineSmp.response)
    {
        PWarn("BSP only\n");
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

    PInfo("via Limine detected %u CPU(s)\n", SmpResponse->cpu_count);
    PInfo("BootStrap LAPIC ID: %u\n", SmpResponse->bsp_lapic_id);

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
            PDebug("BSP CPU %u (LAPIC ID %u)\n", Index, CpuInfo->lapic_id);
        }
        else
        {
            Smp.Cpus[Index].Status = CPU_STATUS_STARTING;
            CpuInfo->goto_address  = ApEntryPoint; /* Set AP entry point */
            StartedAps++;
            PInfo("Starting AP %u (LAPIC ID %u)\n", Index, CpuInfo->lapic_id);
        }
    }

    if (StartedAps > 0)
    {
        PInfo("Waiting for %u APs to start...\n", StartedAps);

#define ApCountTimeout 99999999 /* Large timeout value for AP startup */
        uint32_t Timeout = ApCountTimeout;

        while (CpuStartupCount < StartedAps && Timeout > 0)
        {
            __asm__ volatile("pause");
            Timeout--;
        }

        if (CpuStartupCount > StartedAps)
        {
            PWarn("%u out of %u APs started!\n", CpuStartupCount, StartedAps);
        }
        else
        {
            PSuccess("%u out of %u APs started successfully\n", CpuStartupCount, StartedAps);
        }
    }
}
