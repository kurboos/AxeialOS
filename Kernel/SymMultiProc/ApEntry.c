#include <SymAP.h>          /* Symmetric Multiprocessing Application Processor definitions */
#include <Timer.h>          /* Timer management interfaces */
#include <VMM.h>            /* Virtual Memory Management functions */
#include <APICTimer.h>      /* APIC Timer specific constants and functions */
#include <AxeSchd.h>        /* Axe Scheduler definitions */
#include <AxeThreads.h>     /* Thread management interfaces */

/*
 * ApEntryPoint - Entry Point for Application Processors
 *
 * This function serves as the initial execution point for each Application Processor (AP)
 * in the system. It is called by the Limine bootloader with CPU-specific information.
 * The function performs the necessary setup to make the AP operational, including stack
 * allocation, interrupt initialization, timer setup, and scheduler initialization.
 *
 * Parameters:
 * - __CpuInfo__: Pointer to limine_smp_info structure containing CPU-specific data,
 *                such as the Local APIC ID, provided by the bootloader.
 *
 * The function does not return; it enters an infinite idle loop after setup.
 */
void
ApEntryPoint(struct limine_smp_info *__CpuInfo__)
{
    /*
     * Determine the CPU number by matching the APIC ID from the SMP info with the
     * system's CPU array. This allows the kernel to track and manage each CPU individually.
     */
    uint32_t CpuNumber = 0;
    for (uint32_t Index = 0; Index < Smp.CpuCount; Index++)
    {
        if (Smp.Cpus[Index].ApicId == __CpuInfo__->lapic_id)
        {
            CpuNumber = Index;
            break;
        }
    }

    /*
     * Update the CPU's status to online and mark it as started before the BSP times out.
     * This synchronization ensures the BSP knows the AP is active.
     */
    Smp.Cpus[CpuNumber].Status = CPU_STATUS_ONLINE;
    Smp.Cpus[CpuNumber].Started = 1; /* Boolean flag indicating startup completion */

    /*
     * Atomically increment global counters for CPU startup and online CPUs.
     * These counters are used for synchronization and monitoring across the system.
     */
    __atomic_fetch_add(&CpuStartupCount, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&Smp.OnlineCpus, 1, __ATOMIC_SEQ_CST);

    /*
     * Allocate a dedicated stack for this CPU to ensure thread-safe execution.
     * The stack size is defined by SMPCPUStackSize, and we allocate full pages for it.
     * If allocation fails, log an error and halt the CPU indefinitely.
     */
    uint64_t CpuStackPhys = AllocPages(SMPCPUStackSize / 0x1000);
    if (!CpuStackPhys)
    {
        PError("AP: Failed to allocate stack for CPU %u\n", CpuNumber);
        while(1) __asm__("hlt"); /* Infinite loop of shame; CPU cannot proceed */
    }

    /*
     * Convert the physical address of the allocated stack to a virtual address
     * for use in the kernel's virtual address space.
     */
    void* CpuStack = PhysToVirt(CpuStackPhys);

    /*
     * Switch the stack pointer to the top of the newly allocated stack.
     * Subtract 16 bytes to account for potential alignment and safety margins.
     */
    uint64_t NewStackTop = (uint64_t)CpuStack + SMPCPUStackSize - 16;
    __asm__ volatile("movq %0, %%rsp" : : "r"(NewStackTop));

    /*
     * Log successful initialization of the CPU with its stack address.
     */
    PInfo("AP: CPU %u online with stack at 0x%016lx\n", CpuNumber, NewStackTop);

    /*
     * Initialize per-CPU interrupt handling structures and handlers.
     * This sets up interrupt stacks and routing specific to this CPU.
     */
    PerCpuInterruptInit(CpuNumber, NewStackTop);

    /*
     * Set up the Local APIC timer for this CPU, configuring it for periodic interrupts
     * based on the BSP's timer settings.
     */
    SetupApicTimerForThisCpu();

    /*
     * Initialize the scheduler data structures for this Application Processor.
     * This prepares the CPU for thread scheduling and context switching.
     */
    InitializeCpuScheduler(CpuNumber);

    /*
     * Enable interrupts on this CPU to allow it to respond to timer and other events.
     */
    __asm__ volatile("sti");

    /*
     * Enter an infinite idle loop, halting the CPU until an interrupt occurs.
     * This is the normal operational state for an AP waiting for tasks to execute.
     */
    for(;;)
    {
        __asm__ volatile("hlt");
    }
}
