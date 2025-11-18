#pragma once

#include <AllTypes.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <String.h>
#include <VFS.h>

/**
 * Structures
 */

/** Device type */
typedef enum DevType
{
    DevChar,
    DevBlock

} DevType;

/** Character device ops */
typedef struct CharDevOps
{
    int (*Open)(void* __DevCtx__);
    int (*Close)(void* __DevCtx__);
    long (*Read)(void* __DevCtx__, void* __Buf__, long __Len__);
    long (*Write)(void* __DevCtx__, const void* __Buf__, long __Len__);
    int (*Ioctl)(void* __DevCtx__, unsigned long __Cmd__, void* __Arg__);

} CharDevOps;

/** Block device ops */
typedef struct BlockDevOps
{
    int (*Open)(void* __DevCtx__);
    int (*Close)(void* __DevCtx__);
    long (*ReadBlocks)(void* __DevCtx__, uint64_t __Lba__, void* __Buf__, long __Count__);
    long (*WriteBlocks)(void* __DevCtx__, uint64_t __Lba__, const void* __Buf__, long __Count__);
    int (*Ioctl)(void* __DevCtx__, unsigned long __Cmd__, void* __Arg__);
    long BlockSize; /* bytes per block, e.g., 512 or 4096 */

} BlockDevOps;

/** Registry entry for a device */
typedef struct DeviceEntry
{
    const char* Name;    /* "null", "zero", "tty0", "sda" */
    DevType     Type;    /* DevChar / DevBlock */
    uint32_t    Major;   /* optional major classification */
    uint32_t    Minor;   /* optional minor instance */
    void*       Context; /* driver-private context */
    union
    {
        CharDevOps  C;
        BlockDevOps B;

    } Ops;

} DeviceEntry;

/** File-private context for DevFS handles */
typedef struct DevFsFileCtx
{
    const DeviceEntry* Dev;    /* bound device entry */
    uint64_t           Lba;    /* for block devices: current block cursor */
    long               Offset; /* byte offset within current block for raw streaming */

} DevFsFileCtx;

/**
 * Functions
 */
int         DevFsInit(void);
int         DevFsRegister(void);
Superblock* DevFsMountImpl(const char* __Dev__, const char* __Opts__);
int         DevFsRegisterCharDevice(const char* __Name__,
                                    uint32_t    __Major__,
                                    uint32_t    __Minor__,
                                    CharDevOps  __Ops__,
                                    void*       __Context__);
int         DevFsRegisterBlockDevice(const char* __Name__,
                                     uint32_t    __Major__,
                                     uint32_t    __Minor__,
                                     BlockDevOps __Ops__,
                                     void*       __Context__);
int         DevFsUnregisterDevice(const char* __Name__);
int         DevFsRegisterSeedDevices(void);

/**
 * Public
 */
KEXPORT(DevFsRegisterCharDevice);
KEXPORT(DevFsRegisterBlockDevice);
KEXPORT(DevFsUnregisterDevice);
KEXPORT(DevFsRegisterSeedDevices);
