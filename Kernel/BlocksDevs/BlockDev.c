
#include <BlockDev.h>

static int
BlkDiskOpen(void* __Ctx__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    if (!D)
    {
        return -BadEntity;
    }
    if (!D->Ops.Open)
    {
        return SysOkay;
    }
    return D->Ops.Open(D->CtrlCtx);
}

static int
BlkDiskClose(void* __Ctx__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    if (!D)
    {
        return -BadEntity;
    }
    if (!D->Ops.Close)
    {
        return SysOkay;
    }
    return D->Ops.Close(D->CtrlCtx);
}

static long
BlkDiskReadBlocks(void* __Ctx__, uint64_t __Lba__, void* __Buf__, long __Count__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;

    if (!D || !__Buf__ || __Count__ <= 0)
    {
        return Nothing;
    }
    if (__Lba__ >= D->TotalBlocks)
    {
        return Nothing;
    }
    if (!D->Ops.ReadBlocks || !D->CtrlCtx)
    {
        return Nothing;
    }

    uint64_t max     = D->TotalBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;

    long got = D->Ops.ReadBlocks(D->CtrlCtx, __Lba__, __Buf__, doCount);
    return (got < 0) ? Nothing : got;
}

static long
BlkDiskWriteBlocks(void* __Ctx__, uint64_t __Lba__, const void* __Buf__, long __Count__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;

    if (!D || !__Buf__ || __Count__ <= 0)
    {
        return Nothing;
    }
    if (__Lba__ >= D->TotalBlocks)
    {
        return Nothing;
    }
    if (!D->Ops.WriteBlocks || !D->CtrlCtx)
    {
        return Nothing;
    }

    uint64_t max     = D->TotalBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;

    long put = D->Ops.WriteBlocks(D->CtrlCtx, __Lba__, __Buf__, doCount);
    return (put < 0) ? Nothing : put;
}

static int
BlkDiskIoctl(void* __Ctx__, unsigned long __Cmd__, void* __Arg__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    if (!D)
    {
        return -BadEntity;
    }
    if (!D->Ops.Ioctl || !D->CtrlCtx)
    {
        return SysOkay;
    }
    return D->Ops.Ioctl(D->CtrlCtx, __Cmd__, __Arg__);
}

static int
BlkPartOpen(void* __Ctx__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    if (!P || !P->Parent)
    {
        return -BadEntity;
    }
    return SysOkay;
}

static int
BlkPartClose(void* __Ctx__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    return SysOkay;
}

static long
BlkPartReadBlocks(void* __Ctx__, uint64_t __Lba__, void* __Buf__, long __Count__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    BlockDisk* D = P ? P->Parent : Nothing;

    if (!P || !D || !__Buf__ || __Count__ <= 0)
    {
        return Nothing;
    }
    if (__Lba__ >= P->NumBlocks)
    {
        return Nothing;
    }
    if (!D->Ops.ReadBlocks || !D->CtrlCtx)
    {
        return Nothing;
    }

    uint64_t max     = P->NumBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;
    uint64_t diskLba = P->StartLba + __Lba__;

    long got = D->Ops.ReadBlocks(D->CtrlCtx, diskLba, __Buf__, doCount);
    return (got < 0) ? Nothing : got;
}

static long
BlkPartWriteBlocks(void* __Ctx__, uint64_t __Lba__, const void* __Buf__, long __Count__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    BlockDisk* D = P ? P->Parent : Nothing;

    if (!P || !D || !__Buf__ || __Count__ <= 0)
    {
        return Nothing;
    }
    if (__Lba__ >= P->NumBlocks)
    {
        return Nothing;
    }
    if (!D->Ops.WriteBlocks || !D->CtrlCtx)
    {
        return Nothing;
    }

    uint64_t max     = P->NumBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;
    uint64_t diskLba = P->StartLba + __Lba__;

    long put = D->Ops.WriteBlocks(D->CtrlCtx, diskLba, __Buf__, doCount);
    return (put < 0) ? Nothing : put;
}

static int
BlkPartIoctl(void* __Ctx__, unsigned long __Cmd__, void* __Arg__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    if (!P)
    {
        return -BadEntity;
    }
    return SysOkay;
}

int
BlockRegisterDisk(BlockDisk* __Disk__)
{
    if (!__Disk__ || !__Disk__->Name || __Disk__->BlockSize <= 0)
    {
        return -BadArgs;
    }

    PDebug("RegisterDisk disk=%p name=%s drvCtx=%p opsR=%p opsW=%p opsO=%p opsC=%p opsI=%p "
           "bsz=%ld\n",
           __Disk__,
           __Disk__->Name,
           __Disk__->CtrlCtx,
           (void*)__Disk__->Ops.ReadBlocks,
           (void*)__Disk__->Ops.WriteBlocks,
           (void*)__Disk__->Ops.Open,
           (void*)__Disk__->Ops.Close,
           (void*)__Disk__->Ops.Ioctl,
           __Disk__->BlockSize);

    BlockDevOps Ops = {.Open        = BlkDiskOpen,
                       .Close       = BlkDiskClose,
                       .ReadBlocks  = BlkDiskReadBlocks,
                       .WriteBlocks = BlkDiskWriteBlocks,
                       .Ioctl       = BlkDiskIoctl,
                       .BlockSize   = __Disk__->BlockSize};

    int DiskRC = DevFsRegisterBlockDevice(__Disk__->Name, 8, 0, Ops, (void*)__Disk__);
    if (DiskRC != SysOkay)
    {
        return DiskRC;
    }

    PSuccess("block /dev/%s registered (blocks=%llu, bsize=%ld)\n\n",
             __Disk__->Name,
             (unsigned long long)__Disk__->TotalBlocks,
             __Disk__->BlockSize);
    return SysOkay;
}

int
BlockRegisterPartition(BlockPart* __Part__)
{
    if (!__Part__ || !__Part__->Name || !__Part__->Parent)
    {
        return -BadArgs;
    }

    PDebug("RegisterPart part=%p name=%s parent=%p parentName=%s drvCtx=%p pOpsSz=%ld\n",
           __Part__,
           __Part__->Name,
           (void*)__Part__->Parent,
           (__Part__->Parent && __Part__->Parent->Name) ? __Part__->Parent->Name : "(nil)\n",
           __Part__->Parent ? __Part__->Parent->CtrlCtx : 0,
           __Part__->BlockSize);

    BlockDevOps Ops = {.Open        = BlkPartOpen,
                       .Close       = BlkPartClose,
                       .ReadBlocks  = BlkPartReadBlocks,
                       .WriteBlocks = BlkPartWriteBlocks,
                       .Ioctl       = BlkPartIoctl,
                       .BlockSize   = __Part__->BlockSize};

    int DiskRC = DevFsRegisterBlockDevice(__Part__->Name, 8, 0, Ops, (void*)__Part__);
    if (DiskRC != SysOkay)
    {
        return DiskRC;
    }

    PSuccess("Block /dev/%s registered (start=%llu, blocks=%llu, bsize=%ld)\n\n",
             __Part__->Name,
             (unsigned long long)__Part__->StartLba,
             (unsigned long long)__Part__->NumBlocks,
             __Part__->BlockSize);

    return SysOkay;
}