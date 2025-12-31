
#include <BlockDev.h>

int
BlockRegisterGPTPartitions(BlockDisk*  __Disk__,
                           const void* __GptHeader__,
                           const void* __GptEntries__,
                           long        __EntryCount__)
{

    /*TODO*/

    return SysOkay;
}

int
BlockRegisterMBRPartitions(BlockDisk* __Disk__, const void* __MbrSector__)
{

    /*TODO*/

    return SysOkay;
}