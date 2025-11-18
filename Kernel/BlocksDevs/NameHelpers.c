#include <BlockDev.h>

/**
 * @brief Construct a block device name
 *
 * Generates a disk name string by appending a suffix letter to a prefix.
 * For example, "sd" + 0 -> "sda", "sd" + 1 -> "sdb".
 *
 * @param __Out__    Output buffer for the name
 * @param __Cap__    Capacity of the output buffer
 * @param __Prefix__ Prefix string (e.g., "sd")
 * @param __Index__  Disk index (0-based)
 * @return 0 on success, -1 on invalid arguments, -2 if buffer too small
 */
int
BlockMakeName(char* __Out__, long __Cap__, const char* __Prefix__, long __Index__)
{
    if (!__Out__ || __Cap__ <= 0 || !__Prefix__)
    {
        return -1;
    }

    /* sd + 0 -> sda, sd + 1 -> sdb ... */
    char suffix = (char)('a' + (__Index__ % 26));

    long pLen  = (long)StringLength(__Prefix__);
    long total = pLen + 1;
    if (total + 1 > __Cap__)
    {
        return -2;
    }

    for (long I = 0; I < pLen; I++)
    {
        __Out__[I] = __Prefix__[I];
    }
    __Out__[pLen]     = suffix;
    __Out__[pLen + 1] = '\0';
    return 0;
}

/**
 * @brief Construct a block partition name
 *
 * Generates a partition name string by appending a numeric suffix
 * to a disk name. For example, "sda" + 1 -> "sda1".
 *
 * @param __Out__       Output buffer for the name
 * @param __Cap__       Capacity of the output buffer
 * @param __DiskName__  Disk name string (e.g., "sda")
 * @param __PartIndex__ Partition index (1-based)
 * @return 0 on success, -1 on invalid arguments, -2 if buffer too small
 */
int
BlockMakePartName(char* __Out__, long __Cap__, const char* __DiskName__, long __PartIndex__)
{
    if (!__Out__ || __Cap__ <= 0 || !__DiskName__)
    {
        return -1;
    }

    /* sda + 1 -> sda1 */
    char num[16] = {0};
    long len     = 0;
    long idx     = __PartIndex__;
    if (idx <= 0)
    {
        idx = 1;
    }

    /* Simple integer to string */
    long t       = idx;
    char tmp[16] = {0};
    long N       = 0;
    while (t > 0)
    {
        tmp[N++] = (char)('0' + (t % 10));
        t /= 10;
    }
    for (long I = 0; I < N; I++)
    {
        num[I] = tmp[N - 1 - I];
    }
    num[N] = '\0';

    long dLen  = (long)StringLength(__DiskName__);
    long nLen  = (long)StringLength(num);
    long total = dLen + nLen;
    if (total + 1 > __Cap__)
    {
        return -2;
    }

    for (long I = 0; I < dLen; I++)
    {
        __Out__[I] = __DiskName__[I];
    }
    for (long I = 0; I < nLen; I++)
    {
        __Out__[dLen + I] = num[I];
    }
    __Out__[total] = '\0';
    return 0;
}