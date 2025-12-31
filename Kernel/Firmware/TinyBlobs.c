#include <FirmBlobs.h>

const unsigned char*
FirmData(const FirmwareHandle* __Handle__)
{
    return __Handle__ ? __Handle__->Blob.Data : Nothing;
}

long
FirmSize(const FirmwareHandle* __Handle__)
{
    return __Handle__ ? __Handle__->Blob.Size : Nothing;
}