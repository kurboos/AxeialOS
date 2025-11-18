#include <BlockDev.h>

/**
 * @brief Register GPT partitions for a disk
 *
 * Parses a GPT header and entries to register partitions
 * as block devices under DevFS.
 *
 * @param __Disk__       Pointer to the BlockDisk structure
 * @param __GptHeader__  Pointer to GPT header data
 * @param __GptEntries__ Pointer to GPT entries array
 * @param __EntryCount__ Number of GPT entries
 * @return 0 on success, negative error code on failure
 *
 * @deprecated Not yet made
 */
int
BlockRegisterGPTPartitions(BlockDisk*  __Disk__,
                           const void* __GptHeader__,
                           const void* __GptEntries__,
                           long        __EntryCount__)
{
    (void)__GptHeader__;
    (void)__GptEntries__;
    (void)__EntryCount__;

    /*TODO*/

    return 0;
}

/**
 * @brief Register MBR partitions for a disk
 *
 * Parses an MBR sector to register partitions
 * as block devices under DevFS.
 *
 * @param __Disk__      Pointer to the BlockDisk structure
 * @param __MbrSector__ Pointer to raw MBR sector data
 * @return 0 on success, negative error code on failure
 *
 * @deprecated Not yet made
 */
int
BlockRegisterMBRPartitions(BlockDisk* __Disk__, const void* __MbrSector__)
{
    (void)__MbrSector__;

    /*TODO*/

    return 0;
}