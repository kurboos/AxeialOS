#include <AllTypes.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <RamFs.h>
#include <String.h>

/**
 * @brief Vnode operations table for RamFS
 *
 * Maps generic VFS operations to RamFS-specific implementations.
 * Provides the interface between VFS layer and RamFS filesystem operations.
 * Many operations are not implemented (return -1) as RamFS is read-only.
 */
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

/**
 * @brief Superblock operations table for RamFS
 *
 * Handles filesystem-level operations for mounted RamFS instances.
 * Manages superblock lifecycle and filesystem statistics.
 * Most operations are no-ops since RamFS is memory-based and read-only.
 */
const SuperOps __RamVfsSuperOps__ = {
    .Sync    = RamVfsSuperSync,    /**< Sync filesystem to storage (no-op) */
    .StatFs  = RamVfsSuperStatFs,  /**< Get filesystem statistics */
    .Release = RamVfsSuperRelease, /**< Release superblock resources */
    .Umount  = RamVfsSuperUmount   /**< Unmount filesystem (no-op) */
};

/**
 * @brief Register the RamFS filesystem type with VFS
 *
 * Registers the RamFS filesystem type with the VFS layer, making it
 * available for mounting. This allows RamFS to be used as a filesystem
 * in the VFS namespace.
 *
 * @return 0 on success, -1 on failure (registration failed)
 */
int
RamFsRegister(void)
{
    static FsType __RamFsType__ = {.Name = "ramfs", .Mount = RamFsMountImpl, .Priv = 0};

    if (VfsRegisterFs(&__RamFsType__) != 0)
    {
        PError("RamFS: VfsRegisterFs failed\n");
        return -1;
    }

    PSuccess("RamFS: Registered with VFS\n");
    return 0;
}

/**
 * @brief Mount a RamFS filesystem instance
 *
 * Creates a new RamFS superblock and root vnode, initializing the filesystem
 * for use in the VFS namespace. Allocates necessary structures and sets up
 * the filesystem root.
 *
 * @param __Dev__ Device name (unused for RamFS, can be NULL)
 * @param __Opts__ Mount options (unused for RamFS, can be NULL)
 * @return Pointer to superblock on success, NULL on failure
 */
Superblock*
RamFsMountImpl(const char* __Dev__, const char* __Opts__)
{
    (void)__Dev__;
    (void)__Opts__;

    if (!RamFS.Root)
    {
        PError("RamFS: Root not initialized\n");
        return 0;
    }

    Superblock* Sb = (Superblock*)KMalloc(sizeof(Superblock));
    if (!Sb)
    {
        PError("RamFS: Sb alloc failed\n");
        return 0;
    }

    Vnode* Root = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Root)
    {
        PError("RamFS: Root vnode alloc failed\n");
        KFree(Sb);
        return 0;
    }

    RamVfsPrivNode* Priv = (RamVfsPrivNode*)KMalloc(sizeof(RamVfsPrivNode));
    if (!Priv)
    {
        PError("RamFS: Priv alloc failed\n");
        KFree(Root);
        KFree(Sb);
        return 0;
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

    PDebug("RamFS: Superblock created\n");
    return Sb;
}

/**
 * @brief Open a file or directory in RamFS
 *
 * Opens a vnode for file or directory access, allocating private data
 * structures as needed. For files, creates a RamVfsPrivFile to track
 * the file position.
 *
 * @param __Node__ Vnode to open
 * @param __File__ File structure to initialize
 * @return 0 on success, -1 on failure
 */
int
RamVfsOpen(Vnode* __Node__, File* __File__)
{
    if (!__Node__ || !__File__)
    {
        PError("RamFS: Open invalid args\n");
        return -1;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Node__->Priv;
    if (!PN || !PN->Node)
    {
        PError("RamFS: Open missing priv\n");
        return -1;
    }

    if (PN->Node->Type == RamFSNode_Directory)
    {
        __File__->Node   = __Node__;
        __File__->Offset = 0;
        __File__->Refcnt = 1;
        __File__->Priv   = 0;
        return 0;
    }

    if (PN->Node->Type == RamFSNode_File)
    {
        RamVfsPrivFile* PF = (RamVfsPrivFile*)KMalloc(sizeof(RamVfsPrivFile));
        if (!PF)
        {
            PError("RamFS: Open file priv alloc failed\n");
            return -1;
        }

        PF->Node   = PN->Node;
        PF->Offset = 0;

        __File__->Node   = __Node__;
        __File__->Offset = 0;
        __File__->Refcnt = 1;
        __File__->Priv   = PF;
        return 0;
    }

    PError("RamFS: Open unknown node type\n");
    return -1;
}

/**
 * @brief Close an open file handle in RamFS
 *
 * Closes a file handle, freeing any private data structures allocated
 * during the open operation.
 *
 * @param __File__ File handle to close
 * @return 0 on success, -1 on failure
 */
int
RamVfsClose(File* __File__)
{
    if (!__File__)
    {
        return -1;
    }

    if (__File__->Priv)
    {
        KFree(__File__->Priv);
        __File__->Priv = 0;
    }

    return 0;
}

/**
 * @brief Read data from a RamFS file
 *
 * Reads up to __Len__ bytes from the current file position into the
 * provided buffer, advancing the file position accordingly.
 *
 * @param __File__ Open file handle
 * @param __Buf__ Buffer to store read data
 * @param __Len__ Maximum number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
long
RamVfsRead(File* __File__, void* __Buf__, long __Len__)
{
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }

    RamVfsPrivFile* PF = (RamVfsPrivFile*)__File__->Priv;
    if (!PF || !PF->Node)
    {
        return -1;
    }

    size_t Got = RamFSRead(PF->Node, (size_t)PF->Offset, __Buf__, (size_t)__Len__);
    if (Got > 0)
    {
        PF->Offset += (long)Got;
        __File__->Offset += (long)Got;
        return (long)Got;
    }

    return 0;
}

/**
 * @brief Write data to a RamFS file
 *
 * Writes up to __Len__ bytes from the provided buffer to the current
 * file position. RamFS is read-only, so this operation always fails.
 *
 * @param __File__ Open file handle
 * @param __Buf__ Buffer containing data to write
 * @param __Len__ Number of bytes to write
 * @return -1 (always fails, read-only filesystem)
 */
long
RamVfsWrite(File* __File__, const void* __Buf__, long __Len__)
{
    (void)__File__;
    (void)__Buf__;
    (void)__Len__;
    return -1;
}

/**
 * @brief Seek to a position in a RamFS file
 *
 * Changes the file offset for subsequent read operations. Supports
 * absolute positioning, relative to current position, and relative
 * to end of file.
 *
 * @param __File__ Open file handle
 * @param __Off__ Offset value (interpretation depends on whence)
 * @param __Whence__ Positioning mode (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New file offset, or -1 on error
 */
long
RamVfsLseek(File* __File__, long __Off__, int __Whence__)
{
    if (!__File__)
    {
        return -1;
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
        return -1;
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

/**
 * @brief Perform I/O control operation on RamFS file
 *
 * Performs device-specific or filesystem-specific control operations.
 * RamFS does not support ioctl operations, so this always fails.
 *
 * @param __File__ Open file handle
 * @param __Cmd__ Control command identifier
 * @param __Arg__ Command-specific argument data
 * @return -1 (always fails, not supported)
 */
int
RamVfsIoctl(File* __File__, unsigned long __Cmd__, void* __Arg__)
{
    (void)__File__;
    (void)__Cmd__;
    (void)__Arg__;
    return -1;
}

/**
 * @brief Get file status information for RamFS vnode
 *
 * Retrieves metadata about a file or directory vnode, including size,
 * permissions, timestamps, and other attributes.
 *
 * @param __Node__ Vnode to get status for
 * @param __Out__ Buffer to store file status information
 * @return 0 on success, -1 on failure
 */
int
RamVfsStat(Vnode* __Node__, VfsStat* __Out__)
{
    if (!__Node__ || !__Out__)
    {
        return -1;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Node__->Priv;
    if (!PN || !PN->Node)
    {
        return -1;
    }

    __Out__->Ino        = (long)(uintptr_t)PN->Node;
    __Out__->Size       = (PN->Node->Type == RamFSNode_File) ? (long)PN->Node->Size : 0;
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
    return 0;
}

/**
 * @brief Read directory entries from RamFS directory
 *
 * Fills the provided buffer with VfsDirEnt structures for each child.
 * Returns the number of entries written.
 *
 * @param __Dir__ Directory vnode to read from
 * @param __Buf__ Buffer to store directory entries
 * @param __BufLen__ Capacity in number of entries
 * @return Number of entries written, or -1 on error
 */
long
RamVfsReaddir(Vnode* __Dir__, void* __Buf__, long __BufLen__)
{
    if (!__Dir__ || !__Buf__ || __BufLen__ <= 0)
    {
        return -1;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return -1;
    }

    if (PN->Node->Type != RamFSNode_Directory)
    {
        return -1;
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

/**
 * @brief Look up a child vnode by name in RamFS directory
 *
 * Searches for a child vnode with the given name within the directory
 * represented by the base directory vnode.
 *
 * @param __Dir__ Directory vnode to search in
 * @param __Name__ Name of the child to look up
 * @return Vnode of the child if found, NULL otherwise
 */
Vnode*
RamVfsLookup(Vnode* __Dir__, const char* __Name__)
{
    if (!__Dir__ || !__Name__)
    {
        return 0;
    }

    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return 0;
    }

    if (PN->Node->Type != RamFSNode_Directory)
    {
        return 0;
    }

    char        Path[512];
    long        L  = 0;
    const char* DN = PN->Node->Name ? PN->Node->Name : "/";
    while (DN[L] && L < 511)
    {
        Path[L] = DN[L];
        L++;
    }
    if (L == 0 || Path[L - 1] != '/')
    {
        Path[L++] = '/';
    }
    long N = 0;
    while (__Name__[N] && L + N < 511)
    {
        Path[L + N] = __Name__[N];
        N++;
    }
    Path[L + N] = 0;

    RamFSNode* Child = RamFSLookup(RamFS.Root, Path);
    if (!Child)
    {
        return 0;
    }

    Vnode* V = (Vnode*)KMalloc(sizeof(Vnode));
    if (!V)
    {
        return 0;
    }

    RamVfsPrivNode* Priv = (RamVfsPrivNode*)KMalloc(sizeof(RamVfsPrivNode));
    if (!Priv)
    {
        KFree(V);
        return 0;
    }

    Priv->Node = Child;
    V->Type    = (Child->Type == RamFSNode_Directory) ? VNodeDIR : VNodeFILE;
    V->Ops     = &__RamVfsOps__;
    V->Sb      = __Dir__->Sb;
    V->Priv    = Priv;
    V->Refcnt  = 1;

    return V;
}

/**
 * @brief Create a new file in RamFS directory
 *
 * Creates a new file with the specified name in the given directory.
 * The file is attached to the RamFS tree structure.
 *
 * @param __Dir__ Directory vnode where file should be created
 * @param __Name__ Name of the new file
 * @param __Flags__ Creation flags (unused)
 * @param __Perm__ Permissions for the new file (unused)
 * @return 0 on success, -1 on failure
 */
int
RamVfsCreate(Vnode* __Dir__, const char* __Name__, long __Flags__, VfsPerm __Perm__)
{
    (void)__Flags__;
    (void)__Perm__;
    if (!__Dir__ || !__Name__)
    {
        return -1;
    }
    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return -1;
    }
    if (PN->Node->Type != RamFSNode_Directory)
    {
        return -1;
    }

    char* Path = RamFSJoinPath(PN->Node->Name ? PN->Node->Name : "/", __Name__);
    if (!Path)
    {
        return -1;
    }

    RamFSNode* Leaf = RamFSAttachPath(RamFS.Root, Path, RamFSNode_File, 0, 0);
    KFree(Path);
    return Leaf ? 0 : -1;
}

/**
 * @brief Remove a file from RamFS directory
 *
 * Removes (unlinks) the file with the specified name from the directory.
 * RamFS is read-only, so this operation always fails.
 *
 * @param __Dir__ Directory vnode containing the file
 * @param __Name__ Name of the file to remove
 * @return -1 (always fails, read-only filesystem)
 */
int
RamVfsUnlink(Vnode* __Dir__, const char* __Name__)
{
    (void)__Dir__;
    (void)__Name__;
    return -1;
}

/**
 * @brief Create a new directory in RamFS
 *
 * Creates a new directory with the specified name in the given directory.
 * The directory is attached to the RamFS tree structure.
 *
 * @param __Dir__ Directory vnode where new directory should be created
 * @param __Name__ Name of the new directory
 * @param __Perm__ Permissions for the new directory (unused)
 * @return 0 on success, -1 on failure
 */
int
RamVfsMkdir(Vnode* __Dir__, const char* __Name__, VfsPerm __Perm__)
{
    (void)__Perm__;
    if (!__Dir__ || !__Name__)
    {
        return -1;
    }
    RamVfsPrivNode* PN = (RamVfsPrivNode*)__Dir__->Priv;
    if (!PN || !PN->Node)
    {
        return -1;
    }
    if (PN->Node->Type != RamFSNode_Directory)
    {
        return -1;
    }

    char* Path = RamFSJoinPath(PN->Node->Name ? PN->Node->Name : "/", __Name__);
    if (!Path)
    {
        return -1;
    }

    RamFSNode* Leaf = RamFSAttachPath(RamFS.Root, Path, RamFSNode_Directory, 0, 0);
    KFree(Path);

    return Leaf ? 0 : -1;
}

/**
 * @brief Remove a directory from RamFS
 *
 * Removes (removes) the directory with the specified name from the parent directory.
 * RamFS is read-only, so this operation always fails.
 *
 * @param __Dir__ Parent directory vnode containing the directory
 * @param __Name__ Name of the directory to remove
 * @return -1 (always fails, read-only filesystem)
 */
int
RamVfsRmdir(Vnode* __Dir__, const char* __Name__)
{
    (void)__Dir__;
    (void)__Name__;
    return -1;
}

/**
 * @brief Create a symbolic link in RamFS
 *
 * Creates a symbolic link with the specified name pointing to the target path.
 * RamFS is read-only, so this operation always fails.
 *
 * @param __Dir__ Directory vnode where symlink should be created
 * @param __Name__ Name of the symbolic link
 * @param __Target__ Target path the symlink should point to
 * @param __Perm__ Permissions for the symlink (unused)
 * @return -1 (always fails, read-only filesystem)
 */
int
RamVfsSymlink(Vnode* __Dir__, const char* __Name__, const char* __Target__, VfsPerm __Perm__)
{
    (void)__Dir__;
    (void)__Name__;
    (void)__Target__;
    (void)__Perm__;
    return -1;
}

/**
 * @brief Read the target of a symbolic link in RamFS
 *
 * Reads the target path that a symbolic link points to.
 * RamFS does not support symbolic links, so this always fails.
 *
 * @param __Node__ Vnode of the symbolic link
 * @param __Buf__ Buffer to store the target path
 * @return -1 (always fails, not supported)
 */
int
RamVfsReadlink(Vnode* __Node__, VfsNameBuf* __Buf__)
{
    (void)__Node__;
    (void)__Buf__;
    return -1;
}

/**
 * @brief Create a hard link in RamFS
 *
 * Creates a hard link with the specified name pointing to the same inode as the source vnode.
 * RamFS is read-only, so this operation always fails.
 *
 * @param __Dir__ Directory vnode where hard link should be created
 * @param __Src__ Source vnode to link to
 * @param __Name__ Name of the hard link
 * @return -1 (always fails, read-only filesystem)
 */
int
RamVfsLink(Vnode* __Dir__, Vnode* __Src__, const char* __Name__)
{
    (void)__Dir__;
    (void)__Src__;
    (void)__Name__;
    return -1;
}

/**
 * @brief Rename/move a file or directory in RamFS
 *
 * Renames or moves a file/directory from one location to another.
 * RamFS is read-only, so this operation always fails.
 *
 * @param __OldDir__ Directory containing the item to rename
 * @param __OldName__ Current name of the item
 * @param __NewDir__ Destination directory
 * @param __NewName__ New name for the item
 * @param __Flags__ Rename flags (unused)
 * @return -1 (always fails, read-only filesystem)
 */
int
RamVfsRename(Vnode*      __OldDir__,
             const char* __OldName__,
             Vnode*      __NewDir__,
             const char* __NewName__,
             long        __Flags__)
{
    (void)__OldDir__;
    (void)__OldName__;
    (void)__NewDir__;
    (void)__NewName__;
    (void)__Flags__;
    return -1;
}

/**
 * @brief Change permissions of a RamFS vnode
 *
 * Changes the access permissions of a file or directory.
 * RamFS does not enforce permissions, so this is a no-op.
 *
 * @param __Node__ Vnode to change permissions for
 * @param __Mode__ New permission mode (unused)
 * @return 0 (always succeeds, no-op)
 */
int
RamVfsChmod(Vnode* __Node__, long __Mode__)
{
    (void)__Node__;
    (void)__Mode__;
    return 0;
}

/**
 * @brief Change ownership of a RamFS vnode
 *
 * Changes the user and group ownership of a file or directory.
 * RamFS does not enforce ownership, so this is a no-op.
 *
 * @param __Node__ Vnode to change ownership for
 * @param __Uid__ New user ID (unused)
 * @param __Gid__ New group ID (unused)
 * @return 0 (always succeeds, no-op)
 */
int
RamVfsChown(Vnode* __Node__, long __Uid__, long __Gid__)
{
    (void)__Node__;
    (void)__Uid__;
    (void)__Gid__;
    return 0;
}

/**
 * @brief Truncate a RamFS file to a specified length
 *
 * Changes the size of a file to the specified length. If the file is
 * shortened, data beyond the new length is lost. RamFS is read-only,
 * so this operation always fails.
 *
 * @param __Node__ Vnode of the file to truncate
 * @param __Len__ New length for the file
 * @return -1 (always fails, read-only filesystem)
 */
int
RamVfsTruncate(Vnode* __Node__, long __Len__)
{
    (void)__Node__;
    (void)__Len__;
    return -1;
}

/**
 * @brief Synchronize RamFS vnode data to storage
 *
 * Ensures that all pending writes to a vnode are flushed to persistent
 * storage. RamFS is memory-based, so this is a no-op.
 *
 * @param __Node__ Vnode to synchronize
 * @return 0 (always succeeds, no-op)
 */
int
RamVfsSync(Vnode* __Node__)
{
    (void)__Node__;
    return 0;
}

/**
 * @brief Memory map a RamFS file into process address space
 *
 * Maps a portion of a file into the calling process's virtual address space.
 * RamFS does not support memory mapping, so this always fails.
 *
 * @param __Node__ Vnode of the file to map
 * @param __Out__ Pointer to store the mapped address
 * @param __Off__ Offset within the file to start mapping
 * @param __Len__ Length of the mapping
 * @return -1 (always fails, not supported)
 */
int
RamVfsMap(Vnode* __Node__, void** __Out__, long __Off__, long __Len__)
{
    (void)__Node__;
    (void)__Out__;
    (void)__Off__;
    (void)__Len__;
    return -1;
}

/**
 * @brief Unmap a previously memory-mapped RamFS file
 *
 * Removes a memory mapping created by RamVfsMap. RamFS does not support
 * memory mapping, so this always fails.
 *
 * @param __Node__ Vnode of the mapped file
 * @param __Addr__ Address of the mapping to unmap
 * @param __Len__ Length of the mapping
 * @return -1 (always fails, not supported)
 */
int
RamVfsUnmap(Vnode* __Node__, void* __Addr__, long __Len__)
{
    (void)__Node__;
    (void)__Addr__;
    (void)__Len__;
    return -1;
}

/**
 * @brief Synchronize RamFS superblock data to storage
 *
 * Ensures that all pending superblock changes are flushed to persistent
 * storage. RamFS is memory-based, so this is a no-op.
 *
 * @param __Sb__ Superblock to synchronize
 * @return 0 (always succeeds, no-op)
 */
int
RamVfsSuperSync(Superblock* __Sb__)
{
    (void)__Sb__;
    return 0;
}

/**
 * @brief Get filesystem statistics for RamFS
 *
 * Retrieves statistical information about the mounted RamFS instance,
 * including block counts, inode counts, and filesystem limits.
 *
 * @param __Sb__ Superblock of the filesystem
 * @param __Out__ Buffer to store filesystem statistics
 * @return 0 on success, -1 on failure
 */
int
RamVfsSuperStatFs(Superblock* __Sb__, VfsStatFs* __Out__)
{
    if (!__Sb__ || !__Out__)
    {
        return -1;
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
    return 0;
}

/**
 * @brief Release resources associated with RamFS superblock
 *
 * Frees all memory and resources allocated for the superblock and its
 * associated structures, including the root vnode and private data.
 *
 * @param __Sb__ Superblock to release
 */
void
RamVfsSuperRelease(Superblock* __Sb__)
{
    if (!__Sb__)
    {
        return;
    }
    if (__Sb__->Root)
    {
        RamVfsPrivNode* PN = (RamVfsPrivNode*)__Sb__->Root->Priv;
        if (PN)
        {
            KFree(PN);
        }
        KFree(__Sb__->Root);
        __Sb__->Root = 0;
    }
    KFree(__Sb__);
}

/**
 * @brief Unmount a RamFS filesystem instance
 *
 * Performs cleanup operations when unmounting a RamFS instance.
 * RamFS is memory-based, so this is a no-op.
 *
 * @param __Sb__ Superblock of the filesystem to unmount
 * @return 0 (always succeeds, no-op)
 */
int
RamVfsSuperUmount(Superblock* __Sb__)
{
    (void)__Sb__;
    return 0;
}

/**
 * @brief Mount initrd image into VFS as /bootimg
 *
 * Parses the CPIO initrd archive into RamFS structures, registers the
 * RamFS driver with VFS, and mounts it at the root filesystem (/).
 * This provides access to boot-time files like kernel modules.
 *
 * @param __Initrd__ Pointer to CPIO archive in memory
 * @param __Len__ Size of the initrd archive in bytes
 * @return 0 on success, -1 on failure
 *
 * @note Called during early boot process after memory initialization
 * @note Mounts at "/" making RamFS the root filesystem
 */
int
BootMountRamFs(const void* __Initrd__, size_t __Len__)
{
    if (!__Initrd__ || __Len__ == 0)
    {
        PError("Boot: initrd invalid\n");
        return -1;
    }

    /* Parse the cpio archive into RamFS structures */
    if (!RamFSMount(__Initrd__, __Len__))
    {
        PError("Boot: RamFSMount failed\n");
        return -1;
    }

    /* Register RamFS driver with VFS */
    if (RamFsRegister() != 0)
    {
        PError("Boot: RamFsRegister failed\n");
        return -1;
    }

    /* Mount RamFS into VFS namespace at / */
    if (!VfsMount(0, "/", "ramfs", VMFlgNONE, 0))
    {
        PError("Boot: VfsMount ramfs failed\n");
        return -1;
    }

    PSuccess("Boot: RamFS mounted at /bootimg\n");
    return 0;
}
