#include <AllTypes.h>
#include <FirmBlobs.h>
#include <KHeap.h>
#include <String.h>
#include <VFS.h>

/* Request firmware blob */
int
FirmRequest(FirmwareHandle**    __OutHandle__,
            const FirmwareDesc* __Desc__,
            const DeviceEntry*  __Dev__)
{
    if (!__OutHandle__ || !__Desc__)
    {
        return -BadArgs;
    }

    SysErr  err;
    SysErr* Error = &err;

    FirmwareHandle* H = (FirmwareHandle*)KMalloc(sizeof(FirmwareHandle));
    if (!H)
    {
        return -BadAlloc;
    }
    memset(H, 0, sizeof(FirmwareHandle));
    H->Desc        = *__Desc__;
    H->Dev         = __Dev__;
    *__OutHandle__ = H;

    char PathBuf[512];
    if (FirmResolvePath(__Desc__, PathBuf, (long)sizeof(PathBuf)) != SysOkay)
    {
        KFree(H, Error);
        *__OutHandle__ = 0;
        return -NotCanonical;
    }

    File* F = VfsOpen(PathBuf, VFlgRDONLY);
    if (!F)
    {
        KFree(H, Error);
        *__OutHandle__ = 0;
        return -NoSuch;
    }

    VfsStat St;
    if (VfsFstats(F, &St) != SysOkay || St.Size <= 0)
    {
        VfsClose(F);
        KFree(H, Error);
        *__OutHandle__ = 0;
        return -Limits;
    }

    unsigned char* Buf = (unsigned char*)KMalloc((size_t)St.Size);
    if (!Buf)
    {
        VfsClose(F);
        KFree(H, Error);
        *__OutHandle__ = 0;
        return -BadAlloc;
    }

    long Read   = 0;
    int  RcRead = VfsReadAll(PathBuf, Buf, St.Size, &Read);
    VfsClose(F);

    if (RcRead != 0 || Read != St.Size)
    {
        KFree(Buf, Error);
        KFree(H, Error);
        *__OutHandle__ = 0;
        return -NoRead;
    }

    H->Blob.Data = Buf;
    H->Blob.Size = Read;

    PSuccess("Loaded firmware module '%s' size=%ld\n", PathBuf, Read);
    return SysOkay;
}

int
FirmRelease(FirmwareHandle* __Handle__)
{
    SysErr  err;
    SysErr* Error = &err;
    if (!__Handle__)
    {
        return SysOkay; /*already released*/
    }
    if (__Handle__->Blob.Data)
    {
        KFree((void*)__Handle__->Blob.Data, Error);
    }
    KFree(__Handle__, Error);
    return SysOkay;
}