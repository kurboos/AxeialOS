#include "KrnCommon.h"
#include <Errnos.h>

/** Devs */
#define __Kernel__

SpinLock TestLock;

/*Worker*/
void
KernelWorkerThread(void* __Argument__)
{
    SysErr  err;
    SysErr* Error = &err;

    PInfo("[Starting post kernel init]\n");

    PInfo("Kernel Worker started on CPU %u\n", GetCurrentCpuId());

    /*Testing*/
    __TEST__Proc();

    PSuccess("[Post kernel init complete]\n");

    /*ig this can be our idle thread???*/
    for (;;)
    {
        __asm__("hlt");
    }
}

void
_start(void)
{
    SysErr  err;
    SysErr* Error = &err;

    if (EarlyLimineFrambuffer.response && EarlyLimineFrambuffer.response->framebuffer_count > 0)
    {
        struct limine_framebuffer* FrameBuffer = EarlyLimineFrambuffer.response->framebuffers[0];

        /*Locks*/
        InitializeSpinLock(&TestLock, "TestLock", Error);
        InitializeSpinLock(&SMPLock, "SMP", Error);

        InitializeSerial();

        /*Console*/
        if (FrameBuffer->address)
        {
            KickStartConsole(
                (uint32_t*)FrameBuffer->address, FrameBuffer->width, FrameBuffer->height);
            InitializeSpinLock(&ConsoleLock, "Console", Error);
            ClearConsole();

            PInfo("AxeKrnl Kernel Booting...\n");
        }

        PInfo("[Starting early kernel init]\n");

        /*CPU/IDT/GDT/ISR/IRQ/TSS*/
        InitializeGdt(Error);
        InitializeIdt(Error);

        /*FPU,SSE,Floats*/
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

        /*Memory managers*/
        InitializePmm(Error);
        InitializeVmm(Error);
        InitializeKHeap(Error);

        /*Timer*/
        InitializeTimer(Error);

        /*Syscall*/
        InitSyscall();
        SetIdtEntry(0x80, (uint64_t)SysEntASM, KernelCodeSelector, 0xEE, Error);

        /*Threading/SMP*/
        InitializeSmp(Error);
        InitializeThreadManager(Error);
        InitializeScheduler(Error);

        /*Modules*/
        ModMemInit(Error);
        InitializeBootImage();

        /*Udev/Devfs*/
        DevFsInit();
        Superblock* SuperBlk = DevFsMountImpl(0, 0);
        if (!SuperBlk)
        {
            PError("devfs failed\n");
        }

        if (VfsRegisterPseudoFs("/dev", SuperBlk) != SysOkay)
        {
            PError("mount udev/devfs failed\n");
        }
        DevFsRegisterSeedDevices();

        /*Procfs*/
        if (ProcFsInit() != SysOkay)
        {
            PError("procfs init failed\n");
        }

        PSuccess("[Early kernel init complete]\n");

        /*Kernel worker*/

        Thread* KernelWorker =
            CreateThread(ThreadTypeKernel, KernelWorkerThread, NULL, ThreadPrioritykernel);
        if (KernelWorker)
        {
            ThreadExecute(KernelWorker, Error);
            PSuccess("Ctl Transfer to Worker\n");
        }
    }

    for (;;)
    {
        __asm__("hlt");
    }
}
