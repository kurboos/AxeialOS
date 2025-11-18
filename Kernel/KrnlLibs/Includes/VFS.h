#pragma once

#include <AllTypes.h>
#include <KExports.h>

/**
 * DefStructs
 */
typedef struct Vnode         Vnode;
typedef struct Dentry        Dentry;
typedef struct Superblock    Superblock;
typedef struct File          File;
typedef struct FsType        FsType;
typedef struct VnodeOps      VnodeOps;
typedef struct SuperOps      SuperOps;
typedef struct VfsDirEnt     VfsDirEnt;
typedef struct VfsStat       VfsStat;
typedef struct VfsStatFs     VfsStatFs;
typedef struct VfsPerm       VfsPerm;
typedef struct VfsTimespec   VfsTimespec;
typedef struct VfsMountFlags VfsMountFlags;
typedef struct VfsNameBuf    VfsNameBuf;

/**
 * Enums
 */
/*Node Type*/
typedef enum VnodeType
{
    VNodeNONE,
    VNodeFILE,
    VNodeDIR,
    VNodeDEV,
    VNodeSYM,
    VNodeFIFO,
    VNodeSOCK

} VnodeType;

/*Open Flags*/
typedef enum VfsOpenFlags
{
    VFlgNONE,
    VFlgRDONLY,
    VFlgWRONLY,
    VFlgRDWR,
    VFlgCREATE,
    VFlgTRUNC,
    VFlgAPPEND,
    VFlgEXCL,
    VFlgSYNC,
    VFlgDIRECT

} VfsOpenFlags;

/*Seek Wheel*/
typedef enum VfsSeekWhence
{
    VSeekSET,
    VSeekCUR,
    VSeekEND

} VfsSeekWhence;

/*Permissions*/
typedef enum VfsPermMode
{
    VModeNONE,
    VModeXUSR,
    VModeWUSR,
    VModeRUSR,
    VModeXGRP,
    VModeWGRP,
    VModeRGRP,
    VModeXOTH,
    VModeWOTH,
    VModeROTH

} VfsPermMode;

/*IO*/
typedef enum VfsIoFlags
{
    VIONONE,
    VIOFUA,
    VIOSYNC,
    VIONOCACHE

} VfsIoFlags;

/*Mount*/
typedef enum VfsMountOpt
{
    VMFlgNONE,
    VMFlgRDONLY,
    VMFlgNOEXEC,
    VMFlgNODEV,
    VMFlgNOSUID,
    VMFlgSYNCON,
    VMFlgNOATIME

} VfsMountOpt;

/*Name*/
typedef enum VfsRenameFlags
{
    VnameNONE,
    VnameNOREPLACE,
    VnameEXCHANGE,
    VnameWHITEOUT

} VfsRenameFlags;

/*Notify*/
typedef enum VfsNotifyMask
{
    VNoteNONE,
    VNoteCREATE,
    VNoteDELETE,
    VNoteWRITE,
    VNoteMOVE,
    VNoteATTR,

} VfsNotifyMask;

/**
 * Structs
 */
typedef struct VfsTimespec
{
    long Sec;
    long Nsec;

} VfsTimespec;

typedef struct VfsPerm
{
    long Mode;
    long Uid;
    long Gid;

} VfsPerm;

typedef struct VfsStat
{
    long        Ino;
    long        Size;
    long        Blocks;
    long        BlkSize;
    long        Nlink;
    long        Rdev;
    long        Dev;
    long        Flags;
    VnodeType   Type;
    VfsPerm     Perm;
    VfsTimespec Atime;
    VfsTimespec Mtime;
    VfsTimespec Ctime;

} VfsStat;

typedef struct VfsStatFs
{
    long TypeId;
    long Bsize;
    long Blocks;
    long Bfree;
    long Bavail;
    long Files;
    long Ffree;
    long Namelen;
    long Flags;

} VfsStatFs;

typedef struct VfsDirEnt
{
    char Name[256];
    long Type;
    long Ino;

} VfsDirEnt; /*Dirent*/

typedef struct VfsNameBuf
{
    char* Buf;
    long  Len;

} VfsNameBuf;

/**
 * Operations
 */
typedef struct VnodeOps
{
    int (*Open)(Vnode*, File*);
    int (*Close)(File*);
    long (*Read)(File*, void*, long);
    long (*Write)(File*, const void*, long);
    long (*Lseek)(File*, long, int);
    int (*Ioctl)(File*, unsigned long, void*);
    int (*Stat)(Vnode*, VfsStat*);
    long (*Readdir)(Vnode*, void*, long);
    Vnode* (*Lookup)(Vnode*, const char*);
    int (*Create)(Vnode*, const char*, long, VfsPerm);
    int (*Unlink)(Vnode*, const char*);
    int (*Mkdir)(Vnode*, const char*, VfsPerm);
    int (*Rmdir)(Vnode*, const char*);
    int (*Symlink)(Vnode*, const char*, const char*, VfsPerm);
    int (*Readlink)(Vnode*, VfsNameBuf*);
    int (*Link)(Vnode*, Vnode*, const char*);
    int (*Rename)(Vnode*, const char*, Vnode*, const char*, long);
    int (*Chmod)(Vnode*, long);
    int (*Chown)(Vnode*, long, long);
    int (*Truncate)(Vnode*, long);
    int (*Sync)(Vnode*);
    int (*Map)(Vnode*, void**, long, long);
    int (*Unmap)(Vnode*, void*, long);

} VnodeOps;

typedef struct SuperOps
{
    int (*Sync)(Superblock*);
    int (*StatFs)(Superblock*, VfsStatFs*);
    void (*Release)(Superblock*);
    int (*Umount)(Superblock*);

} SuperOps;

typedef struct FsType
{
    const char* Name; /*Fat32*/
    Superblock* (*Mount)(const char*, const char*);
    void* Priv; /*?*/

} FsType;

/*Superblock*/
struct Superblock
{
    const FsType*   Type;
    void*           Dev;
    long            Flags;
    Vnode*          Root;
    const SuperOps* Ops;
    void*           Priv;
};

/*Nodes*/
struct Vnode
{
    VnodeType       Type;
    const VnodeOps* Ops;
    Superblock*     Sb;
    void*           Priv;
    long            Refcnt;
};

struct Dentry
{
    const char* Name;
    Dentry*     Parent;
    Vnode*      Node;
    long        Flags;
}; /*Dentry*/

struct File
{
    Vnode* Node;
    long   Offset;
    long   Flags;
    long   Refcnt;
    void*  Priv;
}; /*Filentry*/

/**
 * Functions
 */
int VfsInit(void);
int VfsShutdown(void);

int           VfsRegisterFs(const FsType*);
int           VfsUnregisterFs(const char*);
const FsType* VfsFindFs(const char*);
long          VfsListFs(const char**, long);

Superblock* VfsMount(const char*, const char*, const char*, long, const char*);
int         VfsUnmount(const char*);
int         VfsSwitchRoot(const char*);

int VfsBindMount(const char*, const char*);
int VfsMoveMount(const char*, const char*);
int VfsRemount(const char*, long, const char*);

Dentry* VfsResolve(const char*);
Dentry* VfsResolveAt(Dentry*, const char*);
Vnode*  VfsLookup(Dentry*, const char*);
int     VfsMkpath(const char*, long);
int     VfsRealpath(const char*, char*, long);

File* VfsOpen(const char*, long);
File* VfsOpenAt(Dentry*, const char*, long);
int   VfsClose(File*);
long  VfsRead(File*, void*, long);
long  VfsWrite(File*, const void*, long);
long  VfsLseek(File*, long, int);
int   VfsIoctl(File*, unsigned long, void*);
int   VfsFsync(File*);
int   VfsFstats(File*, VfsStat*);
int   VfsStats(const char*, VfsStat*);

long VfsReaddir(const char*, void*, long);
long VfsReaddirF(File*, void*, long);

int VfsCreate(const char*, long, VfsPerm);
int VfsUnlink(const char*);
int VfsMkdir(const char*, VfsPerm);
int VfsRmdir(const char*);
int VfsSymlink(const char*, const char*, VfsPerm);
int VfsReadlink(const char*, char*, long);
int VfsLink(const char*, const char*);
int VfsRename(const char*, const char*, long);
int VfsChmod(const char*, long);
int VfsChown(const char*, long, long);
int VfsTruncate(const char*, long);

int VnodeRefInc(Vnode*);
int VnodeRefDec(Vnode*);
int VnodeGetAttr(Vnode*, VfsStat*);
int VnodeSetAttr(Vnode*, const VfsStat*);
int DentryInvalidate(Dentry*);
int DentryRevalidate(Dentry*);
int DentryAttach(Dentry*, Vnode*);
int DentryDetach(Dentry*);
int DentryName(Dentry*, char*, long);

int VfsSetCwd(const char*);
int VfsGetCwd(char*, long);
int VfsSetRoot(const char*);
int VfsGetRoot(char*, long);

int  VfsSetUmask(long);
long VfsGetUmask(void);

int VfsNotifySubscribe(const char*, long);
int VfsNotifyUnsubscribe(const char*);
int VfsNotifyPoll(const char*, long*);

int VfsAccess(const char*, long);
int VfsExists(const char*);
int VfsIsDir(const char*);
int VfsIsFile(const char*);
int VfsIsSymlink(const char*);

int VfsCopy(const char*, const char*, long);
int VfsMove(const char*, const char*, long);

int VfsReadAll(const char*, void*, long, long*);
int VfsWriteAll(const char*, const void*, long);

int VfsMountTableEnumerate(char*, long);
int VfsMountTableFind(const char*, char*, long);

int VfsNodePath(Vnode*, char*, long);
int VfsNodeName(Vnode*, char*, long);

int VfsAllocName(char**, long);
int VfsFreeName(char*);
int VfsJoinPath(const char*, const char*, char*, long);

int  VfsSetFlag(const char*, long);
int  VfsClearFlag(const char*, long);
long VfsGetFlags(const char*);

int VfsSyncAll(void);
int VfsPruneCaches(void);

int VfsRegisterDevNode(const char*, void*, long);
int VfsUnregisterDevNode(const char*);
int VfsRegisterPseudoFs(const char*, Superblock*);
int VfsUnregisterPseudoFs(const char*);

int         VfsSetDefaultFs(const char*);
const char* VfsGetDefaultFs(void);

int  VfsSetMaxName(long);
long VfsGetMaxName(void);

int  VfsSetMaxPath(long);
long VfsGetMaxPath(void);

int  VfsSetDirCacheLimit(long);
long VfsGetDirCacheLimit(void);

int  VfsSetFileCacheLimit(long);
long VfsGetFileCacheLimit(void);

int  VfsSetIoBlockSize(long);
long VfsGetIoBlockSize(void);

/**
 * Public
 */
KEXPORT(VfsInit);
KEXPORT(VfsShutdown);
KEXPORT(VfsRegisterFs);
KEXPORT(VfsUnregisterFs);
KEXPORT(VfsFindFs);
KEXPORT(VfsListFs);
KEXPORT(VfsMount);
KEXPORT(VfsUnmount);
KEXPORT(VfsSwitchRoot);
KEXPORT(VfsBindMount);
KEXPORT(VfsMoveMount);
KEXPORT(VfsRemount);
KEXPORT(VfsResolve);
KEXPORT(VfsResolveAt);
KEXPORT(VfsLookup);
KEXPORT(VfsMkpath);
KEXPORT(VfsRealpath);
KEXPORT(VfsOpen);
KEXPORT(VfsOpenAt);
KEXPORT(VfsClose);
KEXPORT(VfsRead);
KEXPORT(VfsWrite);
KEXPORT(VfsLseek);
KEXPORT(VfsIoctl);
KEXPORT(VfsFsync);
KEXPORT(VfsFstats);
KEXPORT(VfsStats);
KEXPORT(VfsReaddir);
KEXPORT(VfsReaddirF);
KEXPORT(VfsCreate);
KEXPORT(VfsUnlink);
KEXPORT(VfsMkdir);
KEXPORT(VfsRmdir);
KEXPORT(VfsSymlink);
KEXPORT(VfsReadlink);
KEXPORT(VfsLink);
KEXPORT(VfsRename);
KEXPORT(VfsChmod);
KEXPORT(VfsChown);
KEXPORT(VfsTruncate);
KEXPORT(VnodeRefInc);
KEXPORT(VnodeRefDec);
KEXPORT(VnodeGetAttr);
KEXPORT(VnodeSetAttr);
KEXPORT(DentryInvalidate);
KEXPORT(DentryRevalidate);
KEXPORT(DentryAttach);
KEXPORT(DentryDetach);
KEXPORT(DentryName);
KEXPORT(VfsSetCwd);
KEXPORT(VfsGetCwd);
KEXPORT(VfsSetRoot);
KEXPORT(VfsGetRoot);
KEXPORT(VfsSetUmask);
KEXPORT(VfsGetUmask);
KEXPORT(VfsNotifySubscribe);
KEXPORT(VfsNotifyUnsubscribe);
KEXPORT(VfsNotifyPoll);
KEXPORT(VfsAccess);
KEXPORT(VfsExists);
KEXPORT(VfsIsDir);
KEXPORT(VfsIsFile);
KEXPORT(VfsIsSymlink);
KEXPORT(VfsCopy);
KEXPORT(VfsMove);
KEXPORT(VfsReadAll);
KEXPORT(VfsWriteAll);
KEXPORT(VfsJoinPath);
KEXPORT(VfsSetFlag);
KEXPORT(VfsClearFlag);
KEXPORT(VfsGetFlags);
KEXPORT(VfsSyncAll);
KEXPORT(VfsPruneCaches);
KEXPORT(VfsSetDefaultFs);
KEXPORT(VfsGetDefaultFs);
KEXPORT(VfsSetMaxName);
KEXPORT(VfsGetMaxName);
KEXPORT(VfsSetMaxPath);
KEXPORT(VfsGetMaxPath);
KEXPORT(VfsSetDirCacheLimit);
KEXPORT(VfsGetDirCacheLimit);
KEXPORT(VfsSetFileCacheLimit);
KEXPORT(VfsGetFileCacheLimit);
KEXPORT(VfsSetIoBlockSize);
KEXPORT(VfsGetIoBlockSize);