#include <BlockDev.h>

/** @section Block Layer */

/**
 * @brief Open a block disk via BlockDev shim.
 *
 * Resolves the BlockDisk descriptor from the context pointer, validates
 * it, and delegates to the driver-provided Open handler.
 *
 * @param __Ctx__ Device-private context pointer (BlockDisk*).
 * @return Result code from driver Open handler,
 *         -1 if descriptor invalid,
 *          0 if Open handler missing.
 */
static int
BlkDiskOpen(void* __Ctx__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    PDebug("BLK: DiskOpen ctx=%p name=%s drvOpen=%p drvCtx=%p\n",
           __Ctx__,
           D ? D->Name : "(nil)\n",
           D ? (void*)D->Ops.Open : 0,
           D ? D->CtrlCtx : 0);
    if (!D)
    {
        return -1;
    }
    if (!D->Ops.Open)
    {
        return 0;
    }
    return D->Ops.Open(D->CtrlCtx);
}

/**
 * @brief Close a block disk via BlockDev shim.
 *
 * Resolves the BlockDisk descriptor from the context pointer, validates
 * it, and delegates to the driver-provided Close handler.
 *
 * @param __Ctx__ Device-private context pointer (BlockDisk*).
 * @return Result code from driver Close handler,
 *         -1 if descriptor invalid,
 *          0 if Close handler missing.
 */
static int
BlkDiskClose(void* __Ctx__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    PDebug("BLK: DiskClose ctx=%p name=%s drvClose=%p drvCtx=%p\n",
           __Ctx__,
           D ? D->Name : "(nil)\n",
           D ? (void*)D->Ops.Close : 0,
           D ? D->CtrlCtx : 0);
    if (!D)
    {
        return -1;
    }
    if (!D->Ops.Close)
    {
        return 0;
    }
    return D->Ops.Close(D->CtrlCtx);
}

/**
 * @brief Read blocks from a block disk.
 *
 * Validates the BlockDisk descriptor, buffer, and LBA range, then delegates
 * to the driver-provided ReadBlocks handler. Ensures reads do not exceed
 * the disk’s total block count.
 *
 * @param __Ctx__   Device-private context pointer (BlockDisk*).
 * @param __Lba__   Starting logical block address.
 * @param __Buf__   Destination buffer for read data.
 * @param __Count__ Number of blocks requested.
 *
 * @return Number of blocks read (>=0),
 *         0 if invalid arguments or handler missing.
 */
static long
BlkDiskReadBlocks(void* __Ctx__, uint64_t __Lba__, void* __Buf__, long __Count__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    PDebug("BLK: DiskRead ctx=%p name=%s lba=%llu cnt=%ld drvRead=%p drvCtx=%p tot=%llu\n",
           __Ctx__,
           D ? D->Name : "(nil)\n",
           (unsigned long long)__Lba__,
           __Count__,
           D ? (void*)D->Ops.ReadBlocks : 0,
           D ? D->CtrlCtx : 0,
           D ? (unsigned long long)D->TotalBlocks : 0);

    if (!D || !__Buf__ || __Count__ <= 0)
    {
        return 0;
    }
    if (__Lba__ >= D->TotalBlocks)
    {
        return 0;
    }
    if (!D->Ops.ReadBlocks || !D->CtrlCtx)
    {
        PError("BLK: DiskRead missing ops/cctx\n");
        return 0;
    }

    uint64_t max     = D->TotalBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;

    long got = D->Ops.ReadBlocks(D->CtrlCtx, __Lba__, __Buf__, doCount);
    PDebug("BLK: DiskRead -> got=%ld\n", got);
    return (got < 0) ? 0 : got;
}

/**
 * @brief Write blocks to a block disk.
 *
 * Validates the BlockDisk descriptor, buffer, and LBA range, then delegates
 * to the driver-provided WriteBlocks handler. Ensures writes do not exceed
 * the disk’s total block count.
 *
 * @param __Ctx__   Device-private context pointer (BlockDisk*).
 * @param __Lba__   Starting logical block address.
 * @param __Buf__   Source buffer containing data to write.
 * @param __Count__ Number of blocks requested.
 *
 * @return Number of blocks written (>=0),
 *         0 if invalid arguments or handler missing.
 */
static long
BlkDiskWriteBlocks(void* __Ctx__, uint64_t __Lba__, const void* __Buf__, long __Count__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    PDebug("BLK: DiskWrite ctx=%p name=%s lba=%llu cnt=%ld drvWrite=%p drvCtx=%p tot=%llu\n",
           __Ctx__,
           D ? D->Name : "(nil)\n",
           (unsigned long long)__Lba__,
           __Count__,
           D ? (void*)D->Ops.WriteBlocks : 0,
           D ? D->CtrlCtx : 0,
           D ? (unsigned long long)D->TotalBlocks : 0);

    if (!D || !__Buf__ || __Count__ <= 0)
    {
        return 0;
    }
    if (__Lba__ >= D->TotalBlocks)
    {
        return 0;
    }
    if (!D->Ops.WriteBlocks || !D->CtrlCtx)
    {
        PError("BLK: DiskWrite missing ops/cctx\n");
        return 0;
    }

    uint64_t max     = D->TotalBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;

    long put = D->Ops.WriteBlocks(D->CtrlCtx, __Lba__, __Buf__, doCount);
    PDebug("BLK: DiskWrite -> put=%ld\n", put);
    return (put < 0) ? 0 : put;
}

/**
 * @brief Perform an ioctl on a block disk.
 *
 * Validates the BlockDisk descriptor and delegates to the driver-provided
 * Ioctl handler.
 *
 * @param __Ctx__ Device-private context pointer (BlockDisk*).
 * @param __Cmd__ Ioctl command code.
 * @param __Arg__ Optional argument pointer.
 *
 * @return Result code from driver Ioctl handler,
 *         -1 if descriptor invalid,
 *          0 if Ioctl handler missing.
 */
static int
BlkDiskIoctl(void* __Ctx__, unsigned long __Cmd__, void* __Arg__)
{
    BlockDisk* D = (BlockDisk*)__Ctx__;
    PDebug("BLK: DiskIoctl ctx=%p name=%s cmd=%lu drvIoctl=%p drvCtx=%p\n",
           __Ctx__,
           D ? D->Name : "(nil)\n",
           __Cmd__,
           D ? (void*)D->Ops.Ioctl : 0,
           D ? D->CtrlCtx : 0);
    if (!D)
    {
        return -1;
    }
    if (!D->Ops.Ioctl || !D->CtrlCtx)
    {
        return 0;
    }
    return D->Ops.Ioctl(D->CtrlCtx, __Cmd__, __Arg__);
}

/**
 * @brief Open a block partition.
 *
 * Validates the BlockPart descriptor and its parent BlockDisk.
 *
 * @param __Ctx__ Device-private context pointer (BlockPart*).
 * @return 0 on success, -1 if descriptor or parent invalid.
 */
static int
BlkPartOpen(void* __Ctx__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    PDebug("BLK: PartOpen ctx=%p name=%s parent=%p parentName=%s\n",
           __Ctx__,
           P ? P->Name : "(nil)\n",
           P ? (void*)P->Parent : 0,
           (P && P->Parent && P->Parent->Name) ? P->Parent->Name : "(nil)");
    if (!P || !P->Parent)
    {
        return -1;
    }
    return 0;
}

/**
 * @brief Close a block partition.
 *
 * Currently a no-op; validates context and returns success.
 *
 * @param __Ctx__ Device-private context pointer (BlockPart*).
 * @return Always 0.
 */
static int
BlkPartClose(void* __Ctx__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    PDebug("BLK: PartClose ctx=%p name=%s\n", __Ctx__, P ? P->Name : "(nil)");
    (void)__Ctx__;
    return 0;
}

/**
 * @brief Read blocks from a block partition.
 *
 * Validates the BlockPart descriptor, buffer, and LBA range, then delegates
 * to the parent BlockDisk’s ReadBlocks handler. Ensures reads do not exceed
 * the partition’s block count.
 *
 * @param __Ctx__   Device-private context pointer (BlockPart*).
 * @param __Lba__   Starting logical block address relative to partition.
 * @param __Buf__   Destination buffer for read data.
 * @param __Count__ Number of blocks requested.
 *
 * @return Number of blocks read (>=0),
 *         0 if invalid arguments or handler missing.
 */
static long
BlkPartReadBlocks(void* __Ctx__, uint64_t __Lba__, void* __Buf__, long __Count__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    BlockDisk* D = P ? P->Parent : 0;

    PDebug("BLK: PartRead ctx=%p name=%s lba=%llu cnt=%ld start=%llu nblk=%llu parent=%p "
           "drvRead=%p drvCtx=%p\n",
           __Ctx__,
           P ? P->Name : "(nil)\n",
           (unsigned long long)__Lba__,
           __Count__,
           P ? (unsigned long long)P->StartLba : 0,
           P ? (unsigned long long)P->NumBlocks : 0,
           (void*)D,
           D ? (void*)D->Ops.ReadBlocks : 0,
           D ? D->CtrlCtx : 0);

    if (!P || !D || !__Buf__ || __Count__ <= 0)
    {
        return 0;
    }
    if (__Lba__ >= P->NumBlocks)
    {
        return 0;
    }
    if (!D->Ops.ReadBlocks || !D->CtrlCtx)
    {
        PError("BLK: PartRead missing parent ops/cctx\n");
        return 0;
    }

    uint64_t max     = P->NumBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;
    uint64_t diskLba = P->StartLba + __Lba__;

    long got = D->Ops.ReadBlocks(D->CtrlCtx, diskLba, __Buf__, doCount);
    PDebug("BLK: PartRead -> got=%ld\n", got);
    return (got < 0) ? 0 : got;
}

/**
 * @brief Write blocks to a block partition.
 *
 * Validates the BlockPart descriptor, buffer, and LBA range, then delegates
 * to the parent BlockDisk’s WriteBlocks handler. Ensures writes do not exceed
 * the partition’s block count.
 *
 * @param __Ctx__   Device-private context pointer (BlockPart*).
 * @param __Lba__   Starting logical block address relative to partition.
 * @param __Buf__   Source buffer containing data to write.
 * @param __Count__ Number of blocks requested.
 *
 * @return Number of blocks written (>=0),
 *         0 if invalid arguments or handler missing.
 */
static long
BlkPartWriteBlocks(void* __Ctx__, uint64_t __Lba__, const void* __Buf__, long __Count__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    BlockDisk* D = P ? P->Parent : 0;

    PDebug("BLK: PartWrite ctx=%p name=%s lba=%llu cnt=%ld start=%llu nblk=%llu parent=%p "
           "drvWrite=%p drvCtx=%p\n",
           __Ctx__,
           P ? P->Name : "(nil)\n",
           (unsigned long long)__Lba__,
           __Count__,
           P ? (unsigned long long)P->StartLba : 0,
           P ? (unsigned long long)P->NumBlocks : 0,
           (void*)D,
           D ? (void*)D->Ops.WriteBlocks : 0,
           D ? D->CtrlCtx : 0);

    if (!P || !D || !__Buf__ || __Count__ <= 0)
    {
        return 0;
    }
    if (__Lba__ >= P->NumBlocks)
    {
        return 0;
    }
    if (!D->Ops.WriteBlocks || !D->CtrlCtx)
    {
        PError("BLK: PartWrite missing parent ops/cctx\n");
        return 0;
    }

    uint64_t max     = P->NumBlocks - __Lba__;
    long     doCount = (__Count__ > (long)max) ? (long)max : __Count__;
    uint64_t diskLba = P->StartLba + __Lba__;

    long put = D->Ops.WriteBlocks(D->CtrlCtx, diskLba, __Buf__, doCount);
    PDebug("BLK: PartWrite -> put=%ld\n", put);
    return (put < 0) ? 0 : put;
}

/**
 * @brief Perform an ioctl on a block partition.
 *
 * Currently a stub; validates the BlockPart descriptor.
 *
 * @param __Ctx__ Device-private context pointer (BlockPart*).
 * @param __Cmd__ Ioctl command code (unused).
 * @param __Arg__ Optional argument pointer (unused).
 *
 * @return 0 on success, -1 if descriptor invalid.
 */
static int
BlkPartIoctl(void* __Ctx__, unsigned long __Cmd__, void* __Arg__)
{
    BlockPart* P = (BlockPart*)__Ctx__;
    PDebug("BLK: PartIoctl ctx=%p name=%s cmd=%lu\n", __Ctx__, P ? P->Name : "(nil)\n", __Cmd__);
    (void)__Cmd__;
    (void)__Arg__;
    if (!P)
    {
        return -1;
    }
    return 0;
}

/** @section Mains */

/**
 * @brief Register a block disk with DevFS.
 *
 * Wraps a BlockDisk structure into a BlockDevOps table and registers
 * it with DevFS as a block device. This makes the disk accessible
 * under /dev/<name>.
 *
 * @param __Disk__ Pointer to the BlockDisk structure.
 * @return 0 on success, negative error code on failure.
 *
 * @note The BlockDisk descriptor must remain valid for the lifetime
 *       of the device node.
 */
int
BlockRegisterDisk(BlockDisk* __Disk__)
{
    if (!__Disk__ || !__Disk__->Name || __Disk__->BlockSize <= 0)
    {
        return -1;
    }

    PDebug("BLK: RegisterDisk disk=%p name=%s drvCtx=%p opsR=%p opsW=%p opsO=%p opsC=%p opsI=%p "
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
    if (DiskRC != 0)
    {
        PError("block: register disk %s failed (%d)\n\n", __Disk__->Name, DiskRC);
        return DiskRC;
    }

    PInfo("block: /dev/%s ready (blocks=%llu, bsize=%ld)\n\n",
          __Disk__->Name,
          (unsigned long long)__Disk__->TotalBlocks,
          __Disk__->BlockSize);
    return 0;
}

/**
 * @brief Register a block partition with DevFS.
 *
 * Wraps a BlockPart structure into a BlockDevOps table and registers
 * it with DevFS as a block device. This makes the partition accessible
 * under /dev/<name>.
 *
 * @param __Part__ Pointer to the BlockPart structure.
 * @return 0 on success, negative error code on failure.
 *
 * @note The BlockPart descriptor and its parent BlockDisk must remain
 *       valid for the lifetime of the device node.
 */
int
BlockRegisterPartition(BlockPart* __Part__)
{
    if (!__Part__ || !__Part__->Name || !__Part__->Parent)
    {
        return -1;
    }

    PDebug("BLK: RegisterPart part=%p name=%s parent=%p parentName=%s drvCtx=%p pOpsSz=%ld\n",
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
    if (DiskRC != 0)
    {
        PError("block: register partition %s failed (%d)\n\n", __Part__->Name, DiskRC);
        return DiskRC;
    }

    PInfo("block: /dev/%s ready (start=%llu, blocks=%llu, bsize=%ld)\n\n",
          __Part__->Name,
          (unsigned long long)__Part__->StartLba,
          (unsigned long long)__Part__->NumBlocks,
          __Part__->BlockSize);

    return 0;
}