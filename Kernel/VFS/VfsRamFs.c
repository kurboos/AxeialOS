#include <AllTypes.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <RamFs.h>
#include <String.h>

const VnodeOps __RamVfsOps__ = {
    .Open     = RamVfsOpen,     /**< Open file/directory handle */
    .Close    = RamVfsClose,    /**< Close file handle and free resources */
    .Read     = RamVfsRead,     /**< Read data from file */
    .Write    = RamVfsWrite,    /**< Write to file (not implemented - read-only) */
    .Lseek    = RamVfsLseek,    /**< Seek to position within file */
    .Ioctl    = RamVfsIoctl,    /**< I/O control operations (not implemented) */
    .Stat     = RamVfsStat,     /**< Get file/directory metadata */
    .Readdir  = RamVfsReaddir,  /**< Read directory entries */
    .Lookup   = RamVfsLookup,   /**< Lookup child by name in directory */
    .Create   = RamVfsCreate,   /**< Create new file in directory */
    .Unlink   = RamVfsUnlink,   /**< Remove file (not implemented) */
    .Mkdir    = RamVfsMkdir,    /**< Create new directory */
    .Rmdir    = RamVfsRmdir,    /**< Remove directory (not implemented) */
    .Symlink  = RamVfsSymlink,  /**< Create symlink (not implemented) */
    .Readlink = RamVfsReadlink, /**< Read symlink target (not implemented) */
    .Link     = RamVfsLink,     /**< Create hard link (not implemented) */
    .Rename   = RamVfsRename,   /**< Rename/move file (not implemented) */
    .Chmod    = RamVfsChmod,    /**< Change permissions (no-op) */
    .Chown    = RamVfsChown,    /**< Change ownership (no-op) */
    .Truncate = RamVfsTruncate, /**< Truncate file (not implemented) */
    .Sync     = RamVfsSync,     /**< Synchronize file (no-op) */
    .Map      = RamVfsMap,      /**< Memory map file (not implemented) */
    .Unmap    = RamVfsUnmap     /**< Unmap memory (not implemented) */
};

const SuperOps __RamVfsSuperOps__ = {
    .Sync    = RamVfsSuperSync,    /**< Sync filesystem to storage (no-op) */
    .StatFs  = RamVfsSuperStatFs,  /**< Get filesystem statistics */
    .Release = RamVfsSuperRelease, /**< Release superblock resources */
    .Umount  = RamVfsSuperUmount   /**< Unmount filesystem (no-op) */
};

int
RamFsRegister(void)
{
    static FsType __RamFsType__ = {.Name = "ramfs", .Mount = RamFsMountImpl, .Priv = 0};

    if (VfsRegisterFs(&__RamFsType__) != SysOkay)
    {
        return -NotRecorded;
    }

    PSuccess("Registered with VFS\n");
    return SysOkay;
}

Superblock*
RamFsMountImpl(const char* __Dev__ _unused, const char* __Opts__ _unused)
{
    SysErr  err;
    SysErr* Error = &err;

    if (!RamFS.Root)
    {
        return Error_TO_Pointer(-NotRooted);
    }

    Superblock* Sb = (Superblock*)KMalloc(sizeof(Superblock));
    if (!Sb)
    {
        return Error_TO_Pointer(-BadAlloc);
    }

    Vnode* Root = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Root)
    {
        KFree(Sb, Error);
        return Error_TO_Pointer(-BadAlloc);
    }

    RamVfsPrivNode* Priv = (RamVfsPrivNode*)KMalloc(sizeof(RamVfsPrivNode));
    if (!Priv)
    {
        KFree(Root, Error);
        KFree(Sb, Error);
        return Error_TO_Pointer(-BadAlloc);
    }

    Priv->Node   = RamFS.Root;
    Root->Type   = VNodeDIR;
    Root->Ops    = &__RamVfsOps__;
    Root->Sb     = Sb;
    Root->Priv   = Priv;
    Root->Refcnt = 1;

    Sb->Type  = 0;
    Sb->Dev   = 0;
    Sb->Flags = 0;
    Sb->Root  = Root;
    Sb->Ops   = &__RamVfsSuperOps__;
    Sb->Priv  = 0;

    PDebug("Superblock created\n");
    return Sb;
}

int
RamVfsOpen(Vnode* __Node__, File* __File__)
{
    if (!__Node__ || !__File__)
    {
        return -BadArgs;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Node__->Priv;
    if (!PN || !PN->Node)
    {
        return -NotCanonical;
    }

    if (PN->Node->Type == RamFSNode_Directory)
    {
        __File__->Node   = __Node__;
        __File__->Offset = 0;
        __File__->Refcnt = 1;
        __File__->Priv   = 0;
        return SysOkay;
    }

    if (PN->Node->Type == RamFSNode_File)
    {
        RamVfsPrivFile* PF = (RamVfsPrivFile*)KMalloc(sizeof(RamVfsPrivFile));
        if (!PF)
        {
            return -BadAlloc;
        }

        PF->Node   = PN->Node;
        PF->Offset = 0;

        __File__->Node   = __Node__;
        __File__->Offset = 0;
        __File__->Refcnt = 1;
        __File__->Priv   = PF;
        return 0;
    }

    return -NoSuch;
}

int
RamVfsClose(File* __File__)
{
    if (!__File__)
    {
        return -BadArgs;
    }

    if (__File__->Priv)
    {
        SysErr  err;
        SysErr* Error = &err;
        KFree(__File__->Priv, Error);
        __File__->Priv = 0;
    }

    return SysOkay;
}

long
RamVfsRead(File* __File__, void* __Buf__, long __Len__)
{
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        return -BadArgs;
    }

    RamVfsPrivFile* PF = (RamVfsPrivFile*)__File__->Priv;
    if (!PF || !PF->Node)
    {
        return -Dangling;
    }

    size_t Got = RamFSRead(PF->Node, (size_t)PF->Offset, __Buf__, (size_t)__Len__);
    if (Got > 0)
    {
        PF->Offset += (long)Got;
        __File__->Offset += (long)Got;
        return (long)Got;
    }

    return Nothing;
}

long
RamVfsWrite(File* __File__ _unused, const void* __Buf__ _unused, long __Len__ _unused)
{
    return -Impilict;
}

long
RamVfsLseek(File* __File__, long __Off__, int __Whence__)
{
    if (!__File__)
    {
        return -BadEntry;
    }

    RamVfsPrivFile* PF   = (RamVfsPrivFile*)__File__->Priv;
    long            Size = 0;

    if (PF && PF->Node && PF->Node->Type == RamFSNode_File)
    {
        Size = (long)PF->Node->Size;
    }

    long Base = 0;
    if (__Whence__ == VSeekSET)
    {
        Base = 0;
    }
    else if (__Whence__ == VSeekCUR)
    {
        Base = __File__->Offset;
    }
    else if (__Whence__ == VSeekEND)
    {
        Base = Size;
    }
    else
    {
        return -NotCanonical;
    }

    long New = Base + __Off__;
    if (New < 0)
    {
        New = 0;
    }
    if (PF && PF->Node && New > (long)PF->Node->Size)
    {
        New = (long)PF->Node->Size;
    }

    __File__->Offset = New;
    if (PF)
    {
        PF->Offset = New;
    }
    return New;
}

int
RamVfsIoctl(File* __File__ _unused, unsigned long __Cmd__ _unused, void* __Arg__ _unused)
{
    return -Impilict;
}

int
RamVfsStat(Vnode* __Node__, VfsStat* __Out__)
{
    if (!__Node__ || !__Out__)
    {
        return -BadArgs;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Node__->Priv;
    if (!PN || !PN->Node)
    {
        return -Dangling;
    }

    __Out__->Ino        = (long)(uintptr_t)PN->Node;
    __Out__->Size       = (PN->Node->Type == RamFSNode_File) ? (long)PN->Node->Size : Nothing;
    __Out__->Blocks     = 0;
    __Out__->BlkSize    = 0;
    __Out__->Nlink      = 1;
    __Out__->Rdev       = 0;
    __Out__->Dev        = 0;
    __Out__->Flags      = 0;
    __Out__->Type       = __Node__->Type;
    __Out__->Perm.Mode  = 0;
    __Out__->Perm.Uid   = 0;
    __Out__->Perm.Gid   = 0;
    __Out__->Atime.Sec  = 0;
    __Out__->Atime.Nsec = 0;
    __Out__->Mtime.Sec  = 0;
    __Out__->Mtime.Nsec = 0;
    __Out__->Ctime.Sec  = 0;
    __Out__->Ctime.Nsec = 0;
    return SysOkay;
}

long
RamVfsReaddir(Vnode* __Dir__, void* __Buf__, long __BufLen__)
{
    if (!__Dir__ || !__Buf__ || __BufLen__ <= 0)
    {
        return -BadArgs;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return -Dangling;
    }

    if (PN->Node->Type != RamFSNode_Directory)
    {
        return -BadEntry;
    }

    long       Max = __BufLen__;
    RamFSNode* Tmp[RamFSMaxChildren];
    uint32_t   Cnt = RamFSListChildren(PN->Node, Tmp, RamFSMaxChildren);

    long       Wrote = 0;
    VfsDirEnt* DE    = (VfsDirEnt*)__Buf__;

    for (uint32_t I = 0; I < Cnt && Wrote < Max; I++)
    {
        RamFSNode*  C  = Tmp[I];
        long        N  = 0;
        const char* Nm = C->Name;
        while (Nm[N] && N < 255)
        {
            DE[Wrote].Name[N] = Nm[N];
            N++;
        }
        DE[Wrote].Name[N] = 0;
        DE[Wrote].Type    = (C->Type == RamFSNode_Directory) ? VNodeDIR : VNodeFILE;
        DE[Wrote].Ino     = (long)(uintptr_t)C;
        Wrote++;
    }

    return Wrote; /* return count of entries */
}

/** Fixed this shi because it wasn't looking inside childrens*/
Vnode*
RamVfsLookup(Vnode* __Dir__, const char* __Name__)
{
    if (!__Dir__ || !__Name__)
    {
        return Error_TO_Pointer(-BadArgs);
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node || PN->Node->Type != RamFSNode_Directory)
    {
        return Error_TO_Pointer(-BadEntry);
    }

    RamFSNode* Child = 0;
    for (uint32_t I = 0; I < PN->Node->ChildCount; I++)
    {
        RamFSNode* C = PN->Node->Children[I];
        if (!C || !C->Name)
        {
            continue;
        }
        if (strcmp(C->Name, __Name__) == 0)
        {
            Child = C;
            break;
        }
    }
    if (!Child)
    {
        return Error_TO_Pointer(-BadEntry);
    }

    Vnode* V = (Vnode*)KMalloc(sizeof(Vnode));
    if (!V)
    {
        return Error_TO_Pointer(-BadAlloc);
    }

    RamVfsPrivNode* Priv = (RamVfsPrivNode*)KMalloc(sizeof(RamVfsPrivNode));
    if (!Priv)
    {
        SysErr  err;
        SysErr* Error = &err;
        KFree(V, Error);
        return Error_TO_Pointer(-BadAlloc);
    }

    Priv->Node = Child;
    V->Type    = (Child->Type == RamFSNode_Directory) ? VNodeDIR : VNodeFILE;
    V->Ops     = &__RamVfsOps__;
    V->Sb      = __Dir__->Sb;
    V->Priv    = Priv;
    V->Refcnt  = 1;

    return V;
}

int
RamVfsCreate(Vnode* __Dir__, const char* __Name__, long __Flags__ _unused, VfsPerm __Perm__ _unused)
{
    if (!__Dir__ || !__Name__)
    {
        return -BadArgs;
    }
    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return -Dangling;
    }
    if (PN->Node->Type != RamFSNode_Directory)
    {
        return -BadEntry;
    }

    char* Path = RamFSJoinPath(PN->Node->Name ? PN->Node->Name : "/", __Name__);
    if (!Path)
    {
        return -NotCanonical;
    }

    SysErr     err;
    SysErr*    Error = &err;
    RamFSNode* Leaf  = RamFSAttachPath(RamFS.Root, Path, RamFSNode_File, 0, 0);
    KFree(Path, Error);
    return Leaf ? SysOkay : -NotCanonical;
}

int
RamVfsUnlink(Vnode* __Dir__ _unused, const char* __Name__ _unused)
{
    return -Impilict;
}

int
RamVfsMkdir(Vnode* __Dir__, const char* __Name__, VfsPerm __Perm__ _unused)
{
    if (!__Dir__ || !__Name__)
    {
        return -BadArgs;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return -Dangling;
    }

    if (PN->Node->Type != RamFSNode_Directory)
    {
        return -BadEntry;
    }

    char* Path = RamFSJoinPath(PN->Node->Name ? PN->Node->Name : "/", __Name__);
    if (!Path)
    {
        return -NotCanonical;
    }

    SysErr     err;
    SysErr*    Error = &err;
    RamFSNode* Leaf  = RamFSAttachPath(RamFS.Root, Path, RamFSNode_Directory, 0, 0);
    KFree(Path, Error);

    return Leaf ? SysOkay : -NotCanonical;
}

int
RamVfsRmdir(Vnode* __Dir__, const char* __Name__)
{
    return -Impilict;
}

int
RamVfsSymlink(Vnode* __Dir__, const char* __Name__, const char* __Target__, VfsPerm __Perm__)
{
    return -Impilict;
}

int
RamVfsReadlink(Vnode* __Node__, VfsNameBuf* __Buf__)
{
    return -Impilict;
}

int
RamVfsLink(Vnode* __Dir__, Vnode* __Src__, const char* __Name__)
{
    return -Impilict;
}

int
RamVfsRename(Vnode*      __OldDir__,
             const char* __OldName__,
             Vnode*      __NewDir__,
             const char* __NewName__,
             long        __Flags__)
{
    return -Impilict;
}

int
RamVfsChmod(Vnode* __Node__ _unused, long __Mode__ _unused)
{
    return SysOkay;
}

int
RamVfsChown(Vnode* __Node__ _unused, long __Uid__ _unused, long __Gid__ _unused)
{
    return SysOkay;
}

int
RamVfsTruncate(Vnode* __Node__ _unused, long __Len__ _unused)
{
    return -Impilict;
}

int
RamVfsSync(Vnode* __Node__ _unused)
{
    return SysOkay;
}

int
RamVfsMap(Vnode* __Node__ _unused,
          void** __Out__  _unused,
          long __Off__    _unused,
          long __Len__    _unused)
{
    return -Impilict;
}

int
RamVfsUnmap(Vnode* __Node__ _unused, void* __Addr__ _unused, long __Len__ _unused)
{
    return -Impilict;
}

int
RamVfsSuperSync(Superblock* __Sb__ _unused)
{
    return SysOkay;
}

int
RamVfsSuperStatFs(Superblock* __Sb__, VfsStatFs* __Out__)
{
    if (!__Sb__ || !__Out__)
    {
        return -BadArgs;
    }

    __Out__->TypeId  = RamFSMagic;
    __Out__->Bsize   = 0;
    __Out__->Blocks  = 0;
    __Out__->Bfree   = 0;
    __Out__->Bavail  = 0;
    __Out__->Files   = 0;
    __Out__->Ffree   = 0;
    __Out__->Namelen = 255;
    __Out__->Flags   = 0;

    return SysOkay;
}

void
RamVfsSuperRelease(Superblock* __Sb__, SysErr* __Err__)
{
    if (!__Sb__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    if (__Sb__->Root)
    {
        RamVfsPrivNode* PN = (RamVfsPrivNode*)__Sb__->Root->Priv;
        if (PN)
        {
            KFree(PN, __Err__);
        }
        KFree(__Sb__->Root, __Err__);
        __Sb__->Root = 0;
    }

    KFree(__Sb__, __Err__);
}

int
RamVfsSuperUmount(Superblock* __Sb__ _unused)
{
    return SysOkay;
}

int
BootMountRamFs(const void* __Initrd__, size_t __Len__)
{
    if (!__Initrd__ || __Len__ == 0)
    {
        return -BadArgs;
    }

    /* Parse the cpio archive */
    if (!RamFSMount(__Initrd__, __Len__))
    {
        return -NotCanonical;
    }

    if (RamFsRegister() != SysOkay)
    {
        return -NotRecorded;
    }

    /* Mount as root '/' */
    if (!VfsMount(0, "/", "ramfs", VMFlgNONE, 0))
    {
        return -NotRooted;
    }

    PSuccess("RamFS from BootImg/initrd mounted as '/' (root)\n");
    return SysOkay;
}
