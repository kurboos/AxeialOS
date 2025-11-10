#include <LimineServices.h>

/*
 * Limine Requests File
 *
 * This file contains the definitions of various request structures used to communicate
 * with the Limine bootloader. Limine is a modern, advanced bootloader that provides
 * a protocol for kernels to request essential system information and resources
 * during the early boot process. These requests are made by defining volatile
 * structures that the bootloader populates with the requested data.
 *
 * The 'volatile' keyword is used to prevent compiler optimizations that might
 * assume these structures are not modified externally, ensuring that the
 * bootloader's modifications are always visible to the kernel code.
 *
 * Each request structure is initialized with a unique identifier and a revision
 * number, allowing the bootloader to recognize and fulfill the specific request.
 * Some structures are placed in the ".requests" section using GCC attributes
 * to ensure they are retained in the binary and accessible to Limine.
 */

/*
 * HHDM Request Structure
 *
 * This volatile structure represents a request to the Limine bootloader
 * for the Higher Half Direct Mapping (HHDM) base address. The HHDM is a
 * crucial feature in kernel development that provides a direct mapping
 * of physical memory into the virtual address space at a fixed offset
 * in the higher half (typically above 0xFFFF800000000000 on x86-64).
 *
 * This mapping simplifies memory management by allowing the kernel to
 * access physical memory directly through virtual addresses without
 * complex page table manipulations. It's essential for early kernel
 * initialization, physical memory managers, and direct hardware access.
 *
 * The bootloader responds by setting the 'response' field (not shown here)
 * with the base virtual address where physical memory is mapped.
 *
 * Fields:
 *   id        - Identifier for the HHDM request (set to LIMINE_HHDM_REQUEST).
 *   revision  - Revision number of this request structure (set to 0).
 */
volatile struct
limine_hhdm_request
HhdmRequest =
{
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

/*
 * Memory Map Request Structure
 *
 * This volatile structure requests the system's memory map from the Limine bootloader.
 * The memory map is a critical data structure that describes the layout of
 * physical memory, including regions that are available for use, reserved
 * by the firmware, or occupied by hardware. Each entry in the map specifies
 * a base address, length, and type (e.g., usable, reserved, ACPI reclaimable).
 *
 * This information is vital for initializing the kernel's physical memory
 * manager (PMM), as it allows the kernel to identify which memory regions
 * can be allocated, deallocated, or used for specific purposes. Without
 * an accurate memory map, the kernel risks overwriting critical system
 * data or failing to utilize available memory efficiently.
 *
 * The bootloader provides this through the 'response' field, which points
 * to an array of memory map entries.
 *
 * Fields:
 *   id        - Identifier for the memory map request (set to LIMINE_MEMMAP_REQUEST).
 *   revision  - Revision number of this request structure (set to 0).
 */
volatile struct
limine_memmap_request
MemmapRequest =
{
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

/*
 * Framebuffer Request Structure
 *
 * This volatile structure requests a framebuffer device through Limine.
 * A framebuffer is a region of memory that represents the display's pixel
 * data, allowing the kernel to render graphics or text directly to the screen.
 * It provides a linear buffer where each pixel's color can be set, enabling
 * early graphical output before a full GUI is initialized.
 *
 * This is particularly useful for debugging, displaying boot messages,
 * or implementing a basic console. The framebuffer includes details like
 * resolution, pixel format (e.g., RGB), and memory address, which the kernel
 * uses to draw pixels or text.
 *
 * The bootloader may provide multiple framebuffers if multiple displays
 * are available, and the kernel can select or configure them as needed.
 *
 * Fields:
 *   id        - Identifier for the framebuffer request (set to LIMINE_FRAMEBUFFER_REQUEST).
 *   revision  - Revision number of this request structure (set to 0).
 */
volatile struct
limine_framebuffer_request
EarlyLimineFrambuffer =
{

    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0

};

/*
 * RSDP Request Structure
 *
 * Request for the Root System Description Pointer (RSDP) via Limine.
 * The RSDP is the entry point to the Advanced Configuration and Power
 * Interface (ACPI) tables, which provide detailed information about
 * hardware configuration, power management, and system capabilities.
 * ACPI is a standard for device discovery, power control, and thermal
 * management in modern systems.
 *
 * By obtaining the RSDP, the kernel can parse ACPI tables to identify
 * devices, configure interrupts, manage power states, and handle events
 * like battery status or thermal thresholds. This is essential for
 * building a robust, hardware-aware operating system.
 *
 * The structure is marked with __attribute__((used, section(".requests")))
 * to ensure the linker places it in the ".requests" section of the binary,
 * making it discoverable by the Limine bootloader. This attribute prevents
 * the compiler from optimizing away the variable if it appears unused.
 *
 * Fields:
 *   id        - Identifier for the RSDP request (set to LIMINE_RSDP_REQUEST).
 *   revision  - Revision number of this request structure (set to 0).
 */
__attribute__((used, section(".requests")))
volatile struct
limine_rsdp_request
EarlyLimineRsdp =
{
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

/*
 * SMP Request Structure
 *
 * Request for SMP (Symmetric Multiprocessing) configuration information from Limine.
 * SMP allows the kernel to utilize multiple CPU cores simultaneously, improving
 * performance and responsiveness. This request provides details about the
 * available processors, including their APIC IDs, stack spaces, and entry points
 * for bringing up additional cores (Application Processors or APs).
 *
 * The kernel uses this information to initialize per-CPU data structures,
 * set up interrupt handling, and coordinate tasks across cores. Without SMP
 * support, the system would be limited to a single processor, underutilizing
 * modern hardware.
 *
 * Like the RSDP request, this is placed in the ".requests" section to ensure
 * it's included in the binary and accessible to Limine.
 *
 * Fields:
 *   id        - Identifier for the SMP request (set to LIMINE_SMP_REQUEST).
 *   revision  - Revision number of this request structure (set to 0).
 */
__attribute__((used, section(".requests")))
volatile struct
limine_smp_request
EarlyLimineSmp =
{
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};
