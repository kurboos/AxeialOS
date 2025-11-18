/**
 * @file Limine Requests
 *
 * @brief Limine Requests from the
 * 		  Limine headers for kenrel
 * 		  kernel use.
 *
 * @param HhdmRequest			Memory HHDM
 * @param MemmapRequest			Memory Map
 * @param EarlyLimineFrambuffer Early Boot Framebuffer
 * @param EarlyLimineRsdp		ACPI RSDP
 * @param EarlyLimineSmp	    Limine SMP
 * @param LimineMod			    Limine Module for BootImg
 */

#include <LimineServices.h>
#define __Revision 0

/** @brief For HHDM memory layout,
 *  Centrally Handled By the PMM */
__attribute__((used, section(".requests"))) volatile struct limine_hhdm_request HhdmRequest = {
    .id = LIMINE_HHDM_REQUEST, .revision = __Revision};

/** @brief Memory Map for Memory Regions,
 *  Like ACPI,Reserved, Free, etc...*/
__attribute__((used, section(".requests"))) volatile struct limine_memmap_request MemmapRequest = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = __Revision};

/** @brief Early Framebuffer for Early Boot
 *  Console for traceing and debugging the
 *  Init system of the kernel */
__attribute__((
    used,
    section(".requests"))) volatile struct limine_framebuffer_request EarlyLimineFrambuffer = {

    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = __Revision

};

/** @deprecated Not used */
__attribute__((used, section(".requests"))) volatile struct limine_rsdp_request EarlyLimineRsdp = {
    .id = LIMINE_RSDP_REQUEST, .revision = __Revision};

/** @brief This is self-explanatory, Its a multicore
 *  Therefore we would use limine to our full advantage */
__attribute__((used, section(".requests"))) volatile struct limine_smp_request EarlyLimineSmp = {
    .id = LIMINE_SMP_REQUEST, .revision = __Revision};

/** @brief Limine Module for BootImg, This is used to
 *  load the kernel image (Initrd/Initramfs on Linux)
 *  into memory and mount it to VFS for early predisk */
__attribute__((used, section(".requests"))) volatile struct limine_module_request LimineMod = {
    .id = LIMINE_MODULE_REQUEST, .revision = __Revision};