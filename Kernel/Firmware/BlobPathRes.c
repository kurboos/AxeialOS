#include <FirmBlobs.h>

/* Resolve descriptor to absolute path */
int
FirmResolvePath(const FirmwareDesc* __Desc__, char* __OutPath__, long __OutLen__)
{
    if (!__Desc__ || !__Desc__->Name || !__OutPath__ || __OutLen__ <= 0)
    {
        return -BadArgs;
    }

    const char* Prefix = 0;
    switch (__Desc__->Origin)
    {
        case FirmOriginBootImg:
            {
                Prefix = FirmInitramfsPrefix;
                break;
            }
        case FirmOriginRootFS:
            {
                Prefix = FirmRootfsPrefix;
                break;
            }
        default:
            {
                return -NotCanonical;
            }
    }

    char tmp[512];
    if (VfsJoinPath(Prefix, __Desc__->Name, tmp, (long)sizeof(tmp)) != SysOkay)
    {
        return -NotCanonical;
    }

    if (VfsRealpath(tmp, __OutPath__, __OutLen__) != SysOkay)
    {
        return -NotCanonical;
    }

    return SysOkay;
}
