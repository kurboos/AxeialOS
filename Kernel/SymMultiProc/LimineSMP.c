#include <SMP.h>                /* SMP manager and CPU structures */
#include <LimineServices.h>     /* Limine service interfaces */
#include <LimineSMP.h>          /* Limine SMP protocol definitions */
#include <Timer.h>              /* Timer functions for timeouts */
#include <VMM.h>                /* Virtual memory management for APIC access */

/*
 * Global Variables
 *
 * Smp: Main SMP manager structure containing CPU count, online CPUs, and per-CPU data.
 * SMPLock: Spinlock for synchronizing access to SMP data structures.
 * CpuStartupCount: Atomic counter tracking the number of successfully started APs.
 */
SmpManager Smp;
SpinLock SMPLock;
volatile uint32_t CpuStartupCount = 0;

/*
 * GetCurrentCpuId - Retrieve the Current CPU's Identifier
 *
 * Determines the logical CPU ID of the currently executing processor by reading the
 * Local APIC ID register. This function maps the hardware APIC ID to a logical CPU
 * number used internally by the kernel for CPU-specific operations.
 *
 * The function reads the IA32_APIC_BASE MSR to locate the APIC base address, then
 * accesses the APIC ID register to extract the 8-bit APIC ID. It searches the SMP
 * CPU table to find the corresponding logical CPU number.
 *
 * Returns:
 * - The logical CPU ID if found in the SMP table.
 * - The raw APIC ID if not found (fallback for unknown CPUs).
 *
 * This approach ensures compatibility with systems where the APIC ID may not be
 * contiguous or may not match the logical CPU numbering.
 */
uint32_t
GetCurrentCpuId(void)
{
    /*
     * Read the IA32_APIC_BASE MSR to obtain the physical base address of the Local APIC.
     * The MSR value contains the base address in bits 12-35, masked to extract it.
     */
    uint64_t ApicBaseMsr = ReadMsr(0x1B);  /* IA32_APIC_BASE Model-Specific Register */
    uint64_t ApicPhysBase = ApicBaseMsr & 0xFFFFF000;  /* Extract 4KB-aligned base address */

    /*
     * Calculate the virtual address of the APIC ID register (offset 0x20 from base).
     * Read the register and extract the 8-bit APIC ID from bits 24-31.
     */
    volatile uint32_t* ApicIdReg = (volatile uint32_t*)(PhysToVirt(ApicPhysBase) + 0x20);
    uint32_t ApicId = (*ApicIdReg >> 24) & 0xFF;

    /*
     * Search the SMP CPU table to map the APIC ID to a logical CPU number.
     * This mapping allows the kernel to use consistent CPU numbering internally.
     */
    for (uint32_t Index = 0; Index < Smp.CpuCount; Index++)
    {
        if (Smp.Cpus[Index].ApicId == ApicId)
            return Index;
    }

    /*
     * If the APIC ID is not found in the table, return the raw APIC ID as a fallback.
     * This handles cases where the CPU is not properly registered in the SMP table.
     */
    return ApicId;
}

/*
 * InitializeSmp - Initialize Symmetric Multiprocessing Subsystem
 *
 * This function initializes the SMP subsystem using information provided by the Limine
 * bootloader. It detects available CPUs, starts Application Processors (APs), and waits
 * for them to come online. If SMP is not supported, it falls back to single-CPU mode.
 *
 * The initialization process:
 * 1. Checks for Limine SMP response; falls back to single CPU if unavailable.
 * 2. Parses the SMP response to identify CPUs and their properties.
 * 3. Initializes the SMP manager with CPU count and BSP information.
 * 4. Starts APs by setting their entry point to ApEntryPoint.
 * 5. Waits for APs to signal successful startup with a timeout mechanism.
 * 6. Logs the final SMP configuration and online CPU count.
 *
 * This function is typically called early in kernel initialization, after basic
 * memory and interrupt setup but before full system services are available.
 */
void
InitializeSmp(void)
{
    PInfo("SMP: Initializing using Limine support\n");

    /*
     * Check if the Limine bootloader provided SMP information.
     * If not available, configure the system for single-CPU operation.
     */
    if (!EarlyLimineSmp.response)
    {
        PWarn("SMP: No SMP response from Limine, using single CPU\n");
        Smp.CpuCount = 1;
        Smp.OnlineCpus = 1;
        Smp.BspApicId = 0;
        Smp.Cpus[0].ApicId = 0;
        Smp.Cpus[0].CpuNumber = 0;
        Smp.Cpus[0].Status = CPU_STATUS_ONLINE;
        Smp.Cpus[0].Started = 1;
        return;
    }

    /*
     * Retrieve the Limine SMP response structure containing CPU information.
     */
    struct limine_smp_response *SmpResponse = EarlyLimineSmp.response;

    /*
     * Log the detected CPU count and BSP LAPIC ID for debugging and verification.
     */
    PInfo("SMP: Limine detected %u CPU(s)\n", SmpResponse->cpu_count);
    PInfo("SMP: BSP LAPIC ID: %u\n", SmpResponse->bsp_lapic_id);

    /*
     * Initialize the SMP manager with basic configuration from Limine response.
     * Set CPU count, initial online count (only BSP is online initially),
     * BSP APIC ID, and reset the startup counter.
     */
    Smp.CpuCount = SmpResponse->cpu_count;
    Smp.OnlineCpus = 1;  /* BSP is already online */
    Smp.BspApicId = SmpResponse->bsp_lapic_id;
    CpuStartupCount = 0;

    /*
     * Initialize all CPU entries in the SMP table to offline state.
     * This ensures clean state before configuring detected CPUs.
     */
    for (uint32_t Index = 0; Index < MaxCPUs; Index++)
    {
        Smp.Cpus[Index].Status = CPU_STATUS_OFFLINE;
        Smp.Cpus[Index].Started = 0;
        Smp.Cpus[Index].LimineInfo = NULL;
    }

    /*
     * Configure each detected CPU based on Limine SMP information.
     * Distinguish between BSP (Bootstrap Processor) and APs (Application Processors).
     */
    uint32_t StartedAps = 0;
    for (uint64_t Index = 0; Index < SmpResponse->cpu_count; Index++)
    {
        struct limine_smp_info *CpuInfo = SmpResponse->cpus[Index];

        /*
         * Set up basic CPU information in the SMP table.
         */
        Smp.Cpus[Index].ApicId = CpuInfo->lapic_id;
        Smp.Cpus[Index].CpuNumber = Index;
        Smp.Cpus[Index].LimineInfo = CpuInfo;

        if (CpuInfo->lapic_id == SmpResponse->bsp_lapic_id)
        {
            /*
             * This is the Bootstrap Processor - mark as online and started.
             */
            Smp.Cpus[Index].Status = CPU_STATUS_ONLINE;
            Smp.Cpus[Index].Started = 1;
            PDebug("SMP: BSP CPU %u (LAPIC ID %u)\n", Index, CpuInfo->lapic_id);
        }
        else
        {
            /*
             * This is an Application Processor - initiate startup.
             * Set status to starting, assign entry point, and increment AP counter.
             */
            Smp.Cpus[Index].Status = CPU_STATUS_STARTING;
            CpuInfo->goto_address = ApEntryPoint;  /* Set AP entry point */
            StartedAps++;
            PInfo("SMP: Starting AP %u (LAPIC ID %u)\n", Index, CpuInfo->lapic_id);
        }
    }

    /*
     * Wait for Application Processors to come online if any were started.
     * Use a timeout mechanism to prevent indefinite waiting.
     */
    if (StartedAps > 0)
    {
        PInfo("SMP: Waiting for %u APs to start...\n", StartedAps);

        /*
         * Define a large timeout value to allow sufficient time for AP startup.
         * The timeout is intentionally large to accommodate slower hardware.
         */
        #define ApCountTimeout 99999999  /* Large timeout value for AP startup */
        uint32_t Timeout = ApCountTimeout;

        /*
         * Poll the CPU startup counter until all APs have started or timeout occurs.
         * Use pause instruction for efficient waiting without busy-looping.
         */
        while (CpuStartupCount < StartedAps && Timeout > 0)
        {
            __asm__ volatile("pause");
            Timeout--;
        }

        /*
         * Check the final state and log appropriate messages.
         * Handle cases where fewer or more APs started than expected.
         */
        if (CpuStartupCount > StartedAps)
        {
            PWarn("SMP: %u out of %u APs started!\n", CpuStartupCount, StartedAps);
        }
        else
        {
            PSuccess("SMP: %u out of %u APs started successfully\n", CpuStartupCount, StartedAps);
        }
    }

    /*
     * Log the final SMP configuration with total and online CPU counts.
     */
    PSuccess("SMP initialized: %u CPU(s) total, %u online\n", Smp.CpuCount, Smp.OnlineCpus);
}
