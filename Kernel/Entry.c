#include <AllTypes.h>
#include <LimineServices.h>
#include <EarlyBootFB.h>
#include <BootConsole.h>
#include <KrnPrintf.h>
#include <GDT.h>
#include <IDT.h>
#include <PMM.h>
#include <VMM.h>
#include <KHeap.h>
#include <Timer.h>
#include <Serial.h>
#include <SMP.h>
#include <SymAP.h>
#include <Sync.h>
#include <APICTimer.h>
#include <AxeThreads.h>
#include <AxeSchd.h>

/*
 * KernelWorkerThread
 *
 * This function serves as the entry point for the kernel worker thread, which operates
 * after the system has completed its initialization phase. The kernel worker thread
 * is designed to handle background tasks that support the ongoing operation of the
 * operating system, such as maintenance routines, periodic checks, or low-priority
 * processing that doesn't require immediate attention.
 *
 * Upon starting, the thread logs its activation on the current CPU core, providing
 * visibility into which processor it is running on. This is particularly useful in
 * symmetric multiprocessing (SMP) environments where threads can be scheduled across
 * multiple cores.
 *
 * The thread then enters an infinite loop where it repeatedly executes the HLT
 * (Halt) instruction. The HLT instruction puts the CPU into a low-power state,
 * ceasing execution until an interrupt occurs. This approach minimizes power
 * consumption while keeping the thread responsive to external events or scheduled
 * tasks. In a multitasking kernel, this allows the scheduler to wake the thread
 * when work becomes available.
 *
 * Parameters:
 *   __Argument__ - A void pointer to any arguments passed to the thread. In the
 *                  current implementation, this parameter is not utilized, but
 *                  it provides flexibility for future enhancements where the
 *                  worker thread might need initialization data or configuration
 *                  parameters.
 *
 * Returns:
 *   This function does not return under normal circumstances. It runs indefinitely
 *   as a background service, only terminating if the system shuts down or if an
 *   unhandled exception occurs.
 *
 * Thread Safety:
 *   This function is designed to be thread-safe within the context of the kernel's
 *   threading model. It does not access shared resources that would require
 *   synchronization, making it suitable for concurrent execution across multiple
 *   CPU cores.
 *
 * Usage Context:
 *   This thread is created during the kernel's initialization sequence in the
 *   _start function. It represents the transition from a single-threaded boot
 *   process to a multi-threaded operating system environment.
 */
void
KernelWorkerThread(void* __Argument__)
{
    /*
     * Log the startup of the kernel worker thread, including the CPU ID on which
     * it is running. This provides diagnostic information and confirms that the
     * thread has successfully started. The PInfo function is used for informational
     * logging, which may output to the console, serial port, or other configured
     * logging destinations.
     */
    PInfo("Kernel Worker: Started on CPU %u\n", GetCurrentCpuId());

    /*
     * Enter an infinite loop that repeatedly halts the CPU. The __asm__ volatile
     * construct ensures that the assembly instruction is not optimized away by
     * the compiler, guaranteeing that the HLT instruction is executed as intended.
     *
     * The HLT instruction:
     * - Stops CPU execution until an interrupt is received
     * - Reduces power consumption by putting the core in a low-power state
     * - Allows the kernel to remain responsive to hardware events and scheduled tasks
     *
     * This loop represents the idle state of the kernel worker thread, where it
     * waits for work to be assigned or for system events that require processing.
     */
    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

/*
 * _start
 *
 * This is the primary entry point for the AxeialOS kernel following the transfer
 * of control from the Limine bootloader. As the first function executed in kernel
 * space, _start is responsible for orchestrating the complete initialization of
 * all critical kernel subsystems in the correct sequential order.
 *
 * The initialization sequence follows a logical progression:
 * 1. Verify and set up basic output capabilities (framebuffer, console)
 * 2. Establish fundamental CPU state (GDT, IDT)
 * 3. Initialize memory management systems (PMM, VMM, KHeap)
 * 4. Set up timing and interrupt handling (Timer)
 * 5. Enable multiprocessing capabilities (SMP, Threading, Scheduling)
 * 6. Transition to multi-threaded operation by starting the kernel worker
 *
 * Each initialization step builds upon the previous ones, ensuring that
 * dependencies are satisfied before more complex subsystems are activated.
 * The function concludes by entering an idle loop, allowing the fully initialized
 * kernel to respond to interrupts and manage system operation.
 *
 * Error Handling:
 * If any critical initialization step fails (e.g., no framebuffer available),
 * the function may halt or enter a limited operational mode. However, the
 * current implementation assumes successful initialization of all components.
 *
 * System State Transition:
 * - Entry: Single-threaded, minimal hardware access
 * - Exit: Multi-threaded, full kernel capabilities enabled
 *
 * Returns:
 *   This function does not return. After initialization, the kernel enters
 *   an infinite loop, responding only to interrupts and scheduled tasks.
 *
 * Calling Convention:
 *   This function is called directly by the bootloader and does not follow
 *   standard C calling conventions. It serves as the bridge between bootloader
 *   and kernel execution environments.
 */
void
_start(void)
{
    /*
     * Perform initial validation of the early framebuffer response from Limine.
     * The framebuffer is crucial for providing visual output during kernel
     * initialization and operation. This check ensures that the bootloader
     * has successfully provided at least one framebuffer device.
     *
     * If no framebuffer is available, the kernel may still attempt to initialize
     * other subsystems, but graphical output will be unavailable, potentially
     * limiting debugging and user interaction capabilities.
     */
    if (EarlyLimineFrambuffer.response && EarlyLimineFrambuffer.response->framebuffer_count > 0)
    {
        /*
         * Extract a pointer to the first available framebuffer from the Limine
         * response structure. In multi-monitor setups, additional framebuffers
         * may be available, but this implementation focuses on the primary display.
         */
        struct limine_framebuffer *FrameBuffer = EarlyLimineFrambuffer.response->framebuffers[0];

        /*
         * Initialize the serial port for kernel output. Serial communication
         * provides a reliable fallback for logging and debugging, especially
         * useful when graphical output is unavailable or for remote system
         * monitoring. This must be done early as it may be needed for error
         * reporting during subsequent initialization steps.
         */
        InitializeSerial();

        /*
         * Check if the framebuffer has a valid memory address. A valid address
         * indicates that the framebuffer is properly configured and accessible.
         * If available, proceed with console initialization for graphical text output.
         */
        if (FrameBuffer->address)
        {
            /*
             * Initialize the console subsystem using the framebuffer's memory
             * address, width, and height. The console provides text-based output
             * capabilities, essential for displaying kernel messages, debug
             * information, and user interfaces during early boot and operation.
             *
             * Parameters:
             * - FrameBuffer->address: Base memory address of the framebuffer
             * - FrameBuffer->width: Horizontal resolution in pixels
             * - FrameBuffer->height: Vertical resolution in pixels
             */
            KickStartConsole((uint32_t*)FrameBuffer->address,
                FrameBuffer->width,
                FrameBuffer->height);

            /*
             * Initialize a spinlock to protect concurrent access to the console.
             * In a multi-threaded environment, multiple threads may attempt to
             * write to the console simultaneously, which could result in garbled
             * or interleaved output. The spinlock ensures atomic console operations.
             *
             * The lock is labeled "Console" for debugging and identification purposes.
             */
            InitializeSpinLock(&ConsoleLock, "Console");

            /*
             * Clear the console screen to provide a clean starting state for
             * kernel output. This removes any residual content from previous
             * boot stages or firmware operations.
             */
            ClearConsole();

            /*
             * Log an informational message indicating that the kernel startup
             * process has begun. This provides user feedback and serves as a
             * checkpoint in the boot sequence.
             */
            PInfo("AxeialOS Kernel Starting...\n");
        }

        /*
         * Initialize the Global Descriptor Table (GDT). The GDT defines segment
         * descriptors that control memory segmentation, protection levels, and
         * CPU privilege modes. Proper GDT setup is fundamental to CPU operation
         * in protected mode and is required before most other kernel subsystems
         * can function correctly.
         */
        InitializeGdt();

        /*
         * Initialize the Interrupt Descriptor Table (IDT). The IDT maps interrupt
         * and exception vectors to their corresponding handler functions. This
         * enables the CPU to respond to hardware interrupts, software interrupts,
         * and processor exceptions, forming the basis of the kernel's interrupt
         * handling architecture.
         */
        InitializeIdt();

        /*
         * Initialize the Physical Memory Manager (PMM). The PMM is responsible
         * for tracking and allocating physical memory pages. It maintains data
         * structures (typically bitmaps) to record which memory regions are
         * available, reserved, or in use. This is a critical component for
         * dynamic memory allocation and virtual memory management.
         */
        InitializePmm();

        /*
         * Initialize the Virtual Memory Manager (VMM). The VMM handles the
         * mapping between virtual and physical addresses, implementing paging
         * and address space isolation. It works in conjunction with the PMM
         * to provide virtual memory capabilities, including memory protection
         * and demand paging.
         */
        InitializeVmm();

        /*
         * Initialize the Kernel Heap (KHeap). The KHeap provides dynamic memory
         * allocation services for kernel code, using the underlying PMM and VMM
         * to manage heap memory. This enables kernel subsystems to allocate
         * and deallocate memory as needed during operation.
         */
        InitializeKHeap();

        /*
         * Initialize the APIC timer system. The APIC timer provides high-precision
         * timing services and drives periodic timer interrupts that enable
         * preemptive multitasking, timekeeping, and scheduling. This is essential
         * for implementing time-based operations and thread scheduling.
         */
        InitializeTimer();

        /*
         * Initialize the Thread Manager. This sets up data structures for tracking
         * threads, including thread lists, states, and associated metadata.
         * It also initializes any necessary locks or synchronization primitives
         * for thread-safe operations.
         */
        InitializeThreadManager();

        /*
         * Initialize a spinlock to protect Symmetric Multiprocessing (SMP) operations.
         * SMP involves coordinating multiple CPU cores, which requires careful
         * synchronization to prevent race conditions and ensure data consistency
         * across cores.
         */
        InitializeSpinLock(&SMPLock, "SMP");

        /*
         * Initialize Symmetric Multiprocessing (SMP) support. This enables the
         * kernel to utilize multiple CPU cores simultaneously, bringing additional
         * processor cores online and setting up inter-processor communication
         * mechanisms. SMP is crucial for modern multi-core systems to achieve
         * optimal performance.
         */
        InitializeSmp();

        /*
         * Initialize the scheduler subsystem across all available CPUs. The
         * scheduler manages thread execution, context switching, and CPU time
         * allocation. It prepares each CPU core for multi-threaded operation,
         * enabling the kernel to run multiple tasks concurrently.
         */
        InitializeScheduler();

        /*
         * Create the kernel worker thread, which will handle background tasks
         * and maintain system operation. The thread is created with kernel
         * privilege level and highest priority to ensure it can perform
         * essential maintenance functions.
         *
         * Parameters:
         * - ThreadTypeKernel: Specifies this as a kernel-mode thread
         * - KernelWorkerThread: Function pointer to the thread's entry point
         * - NULL: No arguments passed to the thread
         * - ThreadPrioritykernel: Highest priority level
         */
        Thread* KernelWorker = CreateThread(ThreadTypeKernel, KernelWorkerThread, NULL, ThreadPrioritykernel);
        if (KernelWorker)
        {
            /*
             * Execute the kernel worker thread, transitioning the system from
             * single-threaded initialization to multi-threaded operation. This
             * marks the completion of the boot process and the beginning of
             * normal kernel operation.
             */
            ThreadExecute(KernelWorker);

            /*
             * Log successful transfer of control to the worker thread. This
             * indicates that the kernel has fully initialized and is now
             * running in its operational state.
             */
            PSuccess("Ctl Transfer to Worker\n");
        }
    }

    /*
     * Enter the main kernel idle loop. With all initialization complete, the
     * primary CPU core enters an infinite loop, halting execution until
     * interrupts occur. This represents the kernel's waiting state, where
     * it remains responsive to external events while minimizing power consumption.
     *
     * The HLT instruction puts the CPU in a low-power state, and execution
     * resumes only when hardware interrupts trigger context switches or
     * other system events.
     */
    for (;;)
    {
        __asm__("hlt");
    }
}
