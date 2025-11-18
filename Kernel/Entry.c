/**
 * @file Entry point of the kernel and
 * 		 the early boot init system as
 * 		 well as the main init system.
 *
 * @brief Used for external and internal testing
 * 		  for the kernel.
 *
 * @see read and visit All headers below for traces and
 * 		the main interfaces of the kernel.
 */
#include <APICTimer.h>
#include <AllTypes.h>
#include <AxeSchd.h>
#include <AxeThreads.h>
#include <BootConsole.h>
#include <BootImg.h>
#include <DevFS.h>
#include <EarlyBootFB.h>
#include <GDT.h>
#include <IDT.h>
#include <KExports.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <LimineServices.h>
#include <ModELF.h>
#include <ModMemMgr.h>
#include <PMM.h>
#include <ProcFS.h>
#include <PubELF.h>
#include <SMP.h>
#include <Serial.h>
#include <SymAP.h>
#include <Sync.h>
#include <Timer.h>
#include <VFS.h>
#include <VMM.h>

/** @test Sensitive Testing Purposes */
static SpinLock TestLock;

/**
 * @brief kernel Worker, Handles Post init.
 *
 * @details Handles Post init after the AxeThread manager,
 * 			As the kernel now handles threads and only
 * 			accessible through interrupts.
 *
 * @param __Argument__ handles the argument passed to the
 * 					   thread.
 *
 * @deprecated __Argument__ NULL
 * 			   as it is not used
 *
 * @return Doesn't return anything, runs forever.
 *
 * @see Also consider checking the corresponding header
 * 		files for tracement and to understand the post
 * 		Init. @section Headers
 *
 * @internal Internal Function for kernel Work.
 */
void
KernelWorkerThread(void* __Argument__)
{

    PInfo("Kernel Worker: Started on CPU %u\n", GetCurrentCpuId());

    /** @subsection Kernel Modules */
    /** @see Kmods */
    ModMemInit();
    InitializeBootImage();

    /** @subsection DevFS */
    VfsPerm Perm;
    Perm.Mode = VModeRUSR | VModeWUSR | VModeXUSR | VModeRGRP | VModeXGRP | VModeROTH | VModeXOTH;
    Perm.Uid  = 0;
    Perm.Gid  = 0;

    if (VfsMkdir("/dev", Perm) != 0)
    {
        PError("Failed to create /dev\n");
    }

    DevFsInit();
    Superblock* SuperBlk = DevFsMountImpl(0, 0);
    if (!SuperBlk)
    {
        PError("Boot: DevFsMountImpl failed\n");
    }

    if (VfsRegisterPseudoFs("/dev", SuperBlk) != 0)
    {
        PError("Boot: mount devfs failed\n");
    }
    DevFsRegisterSeedDevices();

    /** @subsection Procfs */
    if (ProcInit() != 0)
    {
        PError("Init: ProcInit failed\n");
        return;
    }

    if (ProcFsInit() != 0)
    {
        PError("Init: ProcFsInit failed\n");
        return;
    }

    Process* InitProc = ProcFind(1);
    if (InitProc)
    {
        ProcFsExposeProcess(InitProc);
    }

    /** @subsection Load All Drivers (Phase Ramdisk)*/
    InitRamDiskDevDrvs();

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Enrty point of the kernel
 *
 * @details Handles the early boot init
 * 			system and neccessary post
 * 			init system.
 *
 * @param void Doesn't take any.
 *
 * @return Doesn't return anything,
 * 		   runs forever.
 *
 * @see Consider checking the above Header(#include) files
 * 		to trace the init system.
 *
 * @internal Internal Function for kernel Work.
 *
 */
void
_start(void)
{
    /** @subsection Frambuffer / Early Framebuffer */
    if (EarlyLimineFrambuffer.response && EarlyLimineFrambuffer.response->framebuffer_count > 0)
    {
        struct limine_framebuffer* FrameBuffer = EarlyLimineFrambuffer.response->framebuffers[0];

        /** @subsection Testing Utils */
        InitializeSpinLock(&TestLock, "TestLock");

        /** @subsection UART / Debugging Utils */
        InitializeSerial();

        if (FrameBuffer->address)
        {
            /** @subsection Early Boot Console */
            KickStartConsole(
                (uint32_t*)FrameBuffer->address, FrameBuffer->width, FrameBuffer->height);
            InitializeSpinLock(&ConsoleLock, "Console");
            ClearConsole();

            PInfo("AxeialOS Kernel Booting...\n");
        }

        /** @subsection Interrupts */
        InitializeGdt();
        InitializeIdt();

        /** @subsection BSP FPU */

        unsigned long Cr0, Cr4;

        /* Read CR0 and CR4 */
        __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
        __asm__ volatile("mov %%cr4, %0" : "=r"(Cr4));

        /* CR0: clear EM (bit 2), set MP (bit 1), clear TS (bit 3) */
        Cr0 &= ~(1UL << 2); /* EM = 0 */
        Cr0 |= (1UL << 1);  /* MP = 1 */
        Cr0 &= ~(1UL << 3); /* TS = 0 */
        __asm__ volatile("mov %0, %%cr0" ::"r"(Cr0) : "memory");

        /* CR4: set OSFXSR (bit 9) and OSXMMEXCPT (bit 10) for SSE */
        Cr4 |= (1UL << 9) | (1UL << 10);
        __asm__ volatile("mov %0, %%cr4" ::"r"(Cr4) : "memory");

        /* Initialize x87/SSE state */
        __asm__ volatile("fninit");

        /** @subsection Memory */
        InitializePmm();
        InitializeVmm();
        InitializeKHeap();

        /** @subsection Threading/Scheduler */
        InitializeTimer();
        InitializeThreadManager();
        InitializeSpinLock(&SMPLock, "SMP");
        InitializeSmp();
        InitializeScheduler();

        /** @subsection Kernel Worker / Kernel Post Init */
        Thread* KernelWorker =
            CreateThread(ThreadTypeKernel, KernelWorkerThread, NULL, ThreadPrioritykernel);
        if (KernelWorker)
        {
            ThreadExecute(KernelWorker);
            PSuccess("Ctl Transfer to Worker\n");
        }
    }

    for (;;)
    {
        __asm__("hlt");
    }
}
