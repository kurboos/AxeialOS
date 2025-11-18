#include <AllTypes.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <String.h>
#include <VFS.h>

static const long __MaxFsTypes__ = 32;
static const long __MaxMounts__  = 64;

static const FsType* __FsReg__[32];
static long          __FsCount__ = 0;

typedef struct __MountEntry__
{
    Superblock* Sb;
    char        Path[1024];

} __MountEntry__;

static __MountEntry__ __Mounts__[64];
static long           __MountCount__ = 0;

static Vnode*  __RootNode__ = 0;
static Dentry* __RootDe__   = 0;

static long  __Umask__          = 0;
static long  __MaxName__        = 256;
static long  __MaxPath__        = 1024;
static long  __DirCacheLimit__  = 0;
static long  __FileCacheLimit__ = 0;
static long  __IoBlockSize__    = 0;
static char  __DefaultFs__[64]  = {0};
static Mutex VfsLock;

static int
__is_sep__(char c)
{
    return c == '/';
}
static const char*
__skip_sep__(const char* __Path)
{
    while (__Path && __is_sep__(*__Path))
    {
        __Path++;
    }
    return __Path;
}
static long
__next_comp__(const char* __Path, char* __Output, long __Cap)
{
    if (!__Path || !*__Path)
    {
        return 0;
    }
    const char* s = __Path;
    long        N = 0;
    while (*s && !__is_sep__(*s))
    {
        if (N + 1 < __Cap)
        {
            __Output[N++] = *s;
        }
        s++;
    }
    __Output[N] = 0;
    return N;
}

static Dentry*
__alloc_dentry__(const char* __Name__, Dentry* __Parent__, Vnode* __Node__)
{
    Dentry* De = (Dentry*)KMalloc(sizeof(Dentry));
    if (!De)
    {
        return 0;
    }
    De->Name   = __Name__;
    De->Parent = __Parent__;
    De->Node   = __Node__;
    De->Flags  = 0;
    return De;
}

static Dentry*
__walk__(Vnode* __StartNode__, Dentry* __StartDe__, const char* __Path__)
{
    if (!__StartNode__ || !__Path__)
    {
        return 0;
    }
    const char* __Path = __Path__;
    if (__is_sep__(*__Path))
    {
        __Path = __skip_sep__(__Path);
    }
    Vnode*  Cur    = __StartNode__;
    Dentry* Parent = __StartDe__;
    char    Comp[256];
    while (*__Path)
    {
        long N = __next_comp__(__Path, Comp, sizeof(Comp));
        if (N <= 0)
        {
            break;
        }
        while (*__Path && !__is_sep__(*__Path))
        {
            __Path++;
        }
        __Path = __skip_sep__(__Path);
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            return 0;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Comp);
        if (!Next)
        {
            return 0;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return 0;
        }
        __builtin_memcpy(Dup, Comp, (size_t)(N + 1));
        Dentry* De = __alloc_dentry__(Dup, Parent, Next);
        if (!De)
        {
            return 0;
        }
        Parent = De;
        Cur    = Next;
    }
    return Parent;
}

static __MountEntry__*
__find_mount__(const char* __Path__)
{
    long Best    = -1;
    long BestLen = -1;
    for (long I = 0; I < __MountCount__; I++)
    {
        const char* Mp = __Mounts__[I].Path;
        long        Ml = (long)strlen(Mp);
        if (Ml <= 0)
        {
            continue;
        }
        if (strncmp(__Path__, Mp, (size_t)Ml) == 0)
        {
            if (Ml > BestLen)
            {
                Best    = I;
                BestLen = Ml;
            }
        }
    }
    return Best >= 0 ? &__Mounts__[Best] : 0;
}

/**
 * @brief Initialize the Virtual File System layer
 *
 * Sets up the VFS subsystem by resetting all internal state variables,
 * clearing filesystem registrations, mount tables, and root filesystem
 * references. This function should be called early during kernel
 * initialization.
 *
 * @return 0 on success (always succeeds)
 */
int
VfsInit(void)
{
    AcquireMutex(&VfsLock);
    InitializeMutex(&VfsLock, "vfs-central");
    __FsCount__        = 0;
    __MountCount__     = 0;
    __RootNode__       = 0;
    __RootDe__         = 0;
    __Umask__          = 0;
    __MaxName__        = 256;
    __MaxPath__        = 1024;
    __DirCacheLimit__  = 0;
    __FileCacheLimit__ = 0;
    __IoBlockSize__    = 0;
    __DefaultFs__[0]   = 0;
    PDebug("VFS: Init\n");
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Shutdown the Virtual File System layer
 *
 * Cleans up all mounted filesystems by calling their unmount and release
 * operations, then resets all VFS state. This function should be called
 * during kernel shutdown to ensure proper cleanup.
 *
 * @return 0 on success (always succeeds)
 */
int
VfsShutdown(void)
{
    AcquireMutex(&VfsLock);
    for (long I = 0; I < __MountCount__; I++)
    {
        Superblock* Sb = __Mounts__[I].Sb;
        if (Sb && Sb->Ops && Sb->Ops->Umount)
        {
            Sb->Ops->Umount(Sb);
        }
        if (Sb && Sb->Ops && Sb->Ops->Release)
        {
            Sb->Ops->Release(Sb);
        }
        __Mounts__[I].Sb      = 0;
        __Mounts__[I].Path[0] = 0;
    }
    __MountCount__ = 0;
    __FsCount__    = 0;
    __RootNode__   = 0;
    __RootDe__     = 0;
    PDebug("VFS: Shutdown\n");
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Register a filesystem type with the VFS
 *
 * Adds a new filesystem type to the VFS registry, allowing it to be
 * mounted later. The filesystem type must provide a valid name and
 * mount function.
 *
 * @param __FsType__ Pointer to the filesystem type structure
 * @return 0 on success, -1 on failure (invalid parameters or registry full)
 */
int
VfsRegisterFs(const FsType* __FsType__)
{
    AcquireMutex(&VfsLock);
    if (!__FsType__ || !__FsType__->Name || !__FsType__->Mount)
    {
        PError("VFS: RegisterFs invalid\n");
        return -1;
    }

    if (__FsCount__ >= __MaxFsTypes__)
    {
        PError("VFS: RegisterFs full: %s\n", __FsType__->Name);
        return -1;
    }

    for (long I = 0; I < __FsCount__; I++)
    {
        if (strcmp(__FsReg__[I]->Name, __FsType__->Name) == 0)
        {
            PWarn("VFS: FS exists %s\n", __FsType__->Name);
            return -1;
        }
    }

    __FsReg__[__FsCount__++] = __FsType__;
    PDebug("VFS: FS registered %s\n", __FsType__->Name);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Unregister a filesystem type from the VFS
 *
 * Removes a filesystem type from the VFS registry, preventing new
 * mounts of that type. Existing mounts are not affected.
 *
 * @param __Name__ Name of the filesystem type to unregister
 * @return 0 on success, -1 if filesystem not found
 */
int
VfsUnregisterFs(const char* __Name__)
{
    AcquireMutex(&VfsLock);
    if (!__Name__)
    {
        PError("VFS: UnregisterFs NULL\n");
        return -1;
    }

    for (long I = 0; I < __FsCount__; I++)
    {
        if (strcmp(__FsReg__[I]->Name, __Name__) == 0)
        {
            for (long J = I; J < __FsCount__ - 1; J++)
            {
                __FsReg__[J] = __FsReg__[J + 1];
            }
            __FsReg__[--__FsCount__] = 0;
            PDebug("VFS: FS unregistered %s\n", __Name__);
            return 0;
        }
    }

    PError("VFS: FS not found %s\n", __Name__);
    ReleaseMutex(&VfsLock);
    return -1;
}

/**
 * @brief Find a registered filesystem type by name
 *
 * Searches the VFS registry for a filesystem type with the given name.
 *
 * @param __Name__ Name of the filesystem type to find
 * @return Pointer to the filesystem type structure, or NULL if not found
 */
const FsType*
VfsFindFs(const char* __Name__)
{
    if (!__Name__)
    {
        return 0;
    }

    for (long I = 0; I < __FsCount__; I++)
    {
        if (strcmp(__FsReg__[I]->Name, __Name__) == 0)
        {
            return __FsReg__[I];
        }
    }

    return 0;
}

/**
 * @brief List all registered filesystem types
 *
 * Fills the provided array with names of all registered filesystem types,
 * up to the specified capacity.
 *
 * @param __Out__ Array to store filesystem names
 * @param __Cap__ Maximum number of names to store
 * @return Number of filesystem types stored in the array
 */
long
VfsListFs(const char** __Out__, long __Cap__)
{
    AcquireMutex(&VfsLock);
    if (!__Out__ || __Cap__ <= 0)
    {
        return -1;
    }

    long N = (__FsCount__ < __Cap__) ? __FsCount__ : __Cap__;
    for (long I = 0; I < N; I++)
    {
        __Out__[I] = __FsReg__[I]->Name;
    }

    ReleaseMutex(&VfsLock);
    return N;
}

/**
 * @brief Mount a filesystem at the specified path
 *
 * Mounts a filesystem of the given type at the specified mount point.
 * The filesystem driver is looked up by type name and its mount function
 * is called to create the superblock.
 *
 * @param __Dev__ Device identifier (may be NULL for some filesystems)
 * @param __Path__ Mount point path in the VFS namespace
 * @param __Type__ Filesystem type name (e.g., "ramfs", "ext2")
 * @param __Flags__ Mount flags (currently unused)
 * @param __Opts__ Mount options string (passed to filesystem driver)
 * @return Pointer to the mounted superblock, or NULL on failure
 */
Superblock*
VfsMount(const char* __Dev__,
         const char* __Path__,
         const char* __Type__,
         long        __Flags__,
         const char* __Opts__)
{
    AcquireMutex(&VfsLock);
    const FsType* Fs = VfsFindFs(__Type__);
    if (!Fs)
    {
        PError("VFS: Mount unknown FS %s\n", __Type__);
        return 0;
    }

    if (!__Path__ || !*__Path__)
    {
        PError("VFS: Mount invalid path\n");
        return 0;
    }

    long Plen = (long)strlen(__Path__);
    if (Plen <= 0 || Plen >= __MaxPath__)
    {
        PError("VFS: Mount path len invalid\n");
        return 0;
    }

    if (__MountCount__ >= __MaxMounts__)
    {
        PError("VFS: Mount table full\n");
        return 0;
    }

    Superblock* Sb = Fs->Mount(__Dev__, __Opts__);
    if (!Sb || !Sb->Root)
    {
        PError("VFS: Mount failed %s on %s\n", __Type__, __Path__);
        return 0;
    }

    __MountEntry__* M = &__Mounts__[__MountCount__++];
    M->Sb             = Sb;
    __builtin_memcpy(M->Path, __Path__, (size_t)(Plen + 1));

    if (!__RootNode__ && strcmp(__Path__, "/") == 0)
    {
        __RootNode__ = Sb->Root;
        __RootDe__   = __alloc_dentry__("/", 0, __RootNode__);
        PDebug("VFS: Root mounted /\n");
    }

    PDebug("VFS: Mounted %s at %s\n", __Type__, __Path__);
    (void)__Flags__;
    ReleaseMutex(&VfsLock);
    return Sb;
}

/**
 * @brief Unmount a filesystem from the specified path
 *
 * Unmounts the filesystem mounted at the given path, calling the
 * filesystem's unmount and release operations to clean up resources.
 *
 * @param __Path__ Mount point path to unmount
 * @return 0 on success, -1 if mount point not found
 */
int
VfsUnmount(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__)
    {
        PError("VFS: Unmount NULL\n");
        return -1;
    }

    for (long I = 0; I < __MountCount__; I++)
    {
        if (strcmp(__Mounts__[I].Path, __Path__) == 0)
        {
            Superblock* Sb = __Mounts__[I].Sb;
            if (Sb && Sb->Ops && Sb->Ops->Umount)
            {
                Sb->Ops->Umount(Sb);
            }
            if (Sb && Sb->Ops && Sb->Ops->Release)
            {
                Sb->Ops->Release(Sb);
            }
            for (long J = I; J < __MountCount__ - 1; J++)
            {
                __Mounts__[J] = __Mounts__[J + 1];
            }
            __Mounts__[--__MountCount__].Sb    = 0;
            __Mounts__[__MountCount__].Path[0] = 0;

            if (strcmp(__Path__, "/") == 0)
            {
                __RootNode__ = 0;
                __RootDe__   = 0;
            }
            PDebug("VFS: Unmounted %s\n", __Path__);
            return 0;
        }
    }

    PError("VFS: Unmount path not found %s\n", __Path__);
    ReleaseMutex(&VfsLock);
    return -1;
}

/**
 * @brief Switch the root filesystem to a new path
 *
 * Changes the root filesystem reference to point to the filesystem
 * containing the specified path. This effectively changes the "/"
 * mount point.
 *
 * @param __NewRoot__ Path to the new root directory
 * @return 0 on success, -1 on failure (path not found or invalid)
 */
int
VfsSwitchRoot(const char* __NewRoot__)
{
    AcquireMutex(&VfsLock);
    if (!__NewRoot__)
    {
        PError("VFS: SwitchRoot NULL\n");
        return -1;
    }

    Dentry* De = VfsResolve(__NewRoot__);
    if (!De || !De->Node)
    {
        PError("VFS: SwitchRoot resolve failed %s\n", __NewRoot__);
        return -1;
    }

    __RootNode__ = De->Node;
    __RootDe__   = De;
    PDebug("VFS: Root switched to %s\n", __NewRoot__);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Create a bind mount (not implemented)
 *
 * This function is a placeholder for bind mount functionality, which
 * allows mounting an existing directory at another location in the
 * filesystem tree.
 *
 * @param __Src__ Source path to bind mount
 * @param __Dst__ Destination path for the bind mount
 * @return -1 (not implemented)
 */
int
VfsBindMount(const char* __Src__, const char* __Dst__)
{
    AcquireMutex(&VfsLock);
    if (!__Src__ || !__Dst__)
    {
        return -1;
    }

    __MountEntry__* M = __find_mount__(__Src__);
    if (!M || !M->Sb)
    {
        return -1;
    }

    if (__MountCount__ >= __MaxMounts__)
    {
        return -1;
    }

    long N = (long)strlen(__Dst__);
    if (N <= 0 || N >= __MaxPath__)
    {
        return -1;
    }

    __MountEntry__* New = &__Mounts__[__MountCount__++];
    New->Sb             = M->Sb;
    __builtin_memcpy(New->Path, __Dst__, (size_t)(N + 1));

    PDebug("VFS: Bind mount %s -> %s\n", __Src__, __Dst__);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Move a mount point
 *
 * This function is for move mount functionality, which
 * allows relocating an existing mount point to a new location.
 *
 * @param __Src__ Current mount point path
 * @param __Dst__ New mount point path
 * @return 0 on OK and -1 on Error
 */
int
VfsMoveMount(const char* __Src__, const char* __Dst__)
{
    AcquireMutex(&VfsLock);
    if (!__Src__ || !__Dst__)
    {
        return -1;
    }

    __MountEntry__* M = __find_mount__(__Src__);
    if (!M || !M->Sb)
    {
        return -1;
    }

    long N = (long)strlen(__Dst__);
    if (N <= 0 || N >= __MaxPath__)
    {
        return -1;
    }

    __builtin_memcpy(M->Path, __Dst__, (size_t)(N + 1));
    PDebug("VFS: Move mount %s -> %s\n", __Src__, __Dst__);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Remount a filesystem with new options (stub implementation)
 *
 * Attempts to remount the filesystem at the given path with new flags
 * and options. Currently only checks if the mount exists.
 *
 * @param __Path__ Mount point path
 * @param __Flags__ New mount flags (unused)
 * @param __Opts__ New mount options (unused)
 * @return 0 if mount exists, -1 if not found
 */
int
VfsRemount(const char* __Path__, long __Flags__, const char* __Opts__)
{
    AcquireMutex(&VfsLock);
    (void)__Flags__;
    (void)__Opts__;
    __MountEntry__* M = __find_mount__(__Path__);
    if (!M || !M->Sb)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Resolve a path to a dentry
 *
 * Converts a path string to a dentry structure by walking the filesystem
 * tree. Handles absolute and relative paths, and considers mounted
 * filesystems.
 *
 * @param __Path__ Path to resolve
 * @return Pointer to resolved dentry, or NULL on failure
 */
Dentry*
VfsResolve(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__)
    {
        return 0;
    }

    if (!__RootNode__)
    {
        return 0;
    }

    if (strcmp(__Path__, "/") == 0)
    {
        return __RootDe__;
    }

    __MountEntry__* M = __find_mount__(__Path__);
    if (!M)
    {
        /*Walk from the global root for non-mounted prefixes*/
        return __walk__(__RootNode__, __RootDe__, __Path__);
    }

    /*Strip the mount path prefix before walking from the mount root*/
    const char* Mp = M->Path;
    long        Ml = (long)strlen(Mp);

    /*If the path is exactly the mount point, return a dentry for the mount root*/
    if (strncmp(__Path__, Mp, (size_t)Ml) == 0 && __Path__[Ml] == '\0')
    {
        /*Construct a dentry anchored at mount root*/
        Dentry* De = __alloc_dentry__(Mp, __RootDe__, M->Sb->Root);
        return De ? De : 0;
    }

    /*Otherwise, walk the subpath tail from the mount's root*/
    const char* Tail = __Path__ + Ml;
    /*Skip extra slashes to normalize*/
    while (*Tail == '/')
    {
        Tail++;
    }

    /*if tail is empty after stripping, we are at mount root*/
    if (!*Tail)
    {
        Dentry* De = __alloc_dentry__(Mp, __RootDe__, M->Sb->Root);
        return De ? De : 0;
    }

    ReleaseMutex(&VfsLock);
    return __walk__(M->Sb->Root, __RootDe__, Tail);
}

/**
 * @brief Resolve a relative path from a base dentry
 *
 * Resolves a relative path starting from the given base dentry.
 * Handles absolute paths by delegating to VfsResolve.
 *
 * @param __Base__ Base dentry to start resolution from
 * @param __Rel__ Relative path to resolve
 * @return Pointer to resolved dentry, or NULL on failure
 */
Dentry*
VfsResolveAt(Dentry* __Base__, const char* __Rel__)
{
    AcquireMutex(&VfsLock);
    if (!__Base__ || !__Base__->Node || !__Rel__)
    {
        return 0;
    }

    if (!*__Rel__)
    {
        return __Base__;
    }

    if (__is_sep__(*__Rel__))
    {
        return VfsResolve(__Rel__);
    }

    ReleaseMutex(&VfsLock);
    return __walk__(__Base__->Node, __Base__, __Rel__);
}

/**
 * @brief Look up a child vnode by name from a base dentry
 *
 * Searches for a child vnode with the given name in the directory
 * represented by the base dentry.
 *
 * @param __Base__ Base dentry to search in
 * @param __Name__ Name of the child to look up
 * @return Pointer to the found vnode, or NULL if not found
 */
Vnode*
VfsLookup(Dentry* __Base__, const char* __Name__)
{
    AcquireMutex(&VfsLock);
    if (!__Base__ || !__Base__->Node || !__Name__)
    {
        return 0;
    }

    if (!__Base__->Node->Ops || !__Base__->Node->Ops->Lookup)
    {
        return 0;
    }

    ReleaseMutex(&VfsLock);
    return __Base__->Node->Ops->Lookup(__Base__->Node, __Name__);
}

/**
 * @brief Create parent directories for a path
 *
 * Ensures that all parent directories for the given path exist.
 * Currently only checks if the path resolves.
 *
 * @param __Path__ Path for which to create parent directories
 * @param __Perm__ Permissions for created directories (unused)
 * @return 0 if path exists, -1 if not found
 */
int
VfsMkpath(const char* __Path__, long __Perm__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__)
    {
        return -1;
    }
    const char* p = __Path__;
    if (__is_sep__(*p))
    {
        p = __skip_sep__(p);
    }

    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    char    Comp[256];

    while (*p)
    {
        long N = __next_comp__(p, Comp, sizeof(Comp));
        if (N <= 0)
        {
            break;
        }
        while (*p && !__is_sep__(*p))
        {
            p++;
        }
        p = __skip_sep__(p);

        Vnode* Next = Cur->Ops ? Cur->Ops->Lookup(Cur, Comp) : 0;
        if (!Next)
        {
            if (!Cur->Ops || !Cur->Ops->Mkdir)
            {
                return -1;
            }
            VfsPerm perm = {.Mode = __Perm__, .Uid = 0, .Gid = 0};
            if (Cur->Ops->Mkdir(Cur, Comp, perm) != 0)
            {
                return -1;
            }
            Next = Cur->Ops->Lookup(Cur, Comp);
            if (!Next)
            {
                return -1;
            }
        }
        char* Dup = (char*)KMalloc(N + 1);
        __builtin_memcpy(Dup, Comp, N + 1);
        De  = __alloc_dentry__(Dup, De, Next);
        Cur = Next;
    }
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Resolve a path to its canonical form
 *
 * Converts a path to its canonical absolute form, resolving any
 * symbolic links and relative components.
 *
 * @param __Path__ Path to canonicalize
 * @param __Buf__ Buffer to store the canonical path
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
VfsRealpath(const char* __Path__, char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }

    long L = (long)strlen(__Path__);
    if (L >= __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Buf__, __Path__, (size_t)(L + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Open a file for reading/writing
 *
 * Opens a file at the specified path with the given flags, creating
 * a File structure for subsequent operations.
 *
 * @param __Path__ Path to the file to open
 * @param __Flags__ Open flags (e.g., read-only, write-only, create)
 * @return Pointer to the opened File structure, or NULL on failure
 */
File*
VfsOpen(const char* __Path__, long __Flags__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        PError("VFS: Open resolve failed %s\n", __Path__);
        return 0;
    }

    if (!De->Node->Ops || !De->Node->Ops->Open)
    {
        PError("VFS: Open not supported %s\n", __Path__);
        return 0;
    }

    File* F = (File*)KMalloc(sizeof(File));
    if (!F)
    {
        return 0;
    }

    F->Node   = De->Node;
    F->Offset = 0;
    F->Flags  = __Flags__;
    F->Refcnt = 1;
    F->Priv   = 0;

    if (De->Node->Ops->Open(De->Node, F) != 0)
    {
        KFree(F);
        PError("VFS: Open failed %s\n", __Path__);
        return 0;
    }

    PDebug("VFS: Open %s\n", __Path__);
    ReleaseMutex(&VfsLock);
    return F;
}

/**
 * @brief Open a file relative to a base dentry
 *
 * Opens a file at a path relative to the given base dentry with the
 * specified flags, creating a File structure for subsequent operations.
 *
 * @param __Base__ Base dentry to resolve the relative path from
 * @param __Rel__ Relative path to the file to open
 * @param __Flags__ Open flags (e.g., read-only, write-only, create)
 * @return Pointer to the opened File structure, or NULL on failure
 */
File*
VfsOpenAt(Dentry* __Base__, const char* __Rel__, long __Flags__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolveAt(__Base__, __Rel__);
    if (!De || !De->Node)
    {
        return 0;
    }

    if (!De->Node->Ops || !De->Node->Ops->Open)
    {
        return 0;
    }

    File* F = (File*)KMalloc(sizeof(File));
    if (!F)
    {
        return 0;
    }

    F->Node   = De->Node;
    F->Offset = 0;
    F->Flags  = __Flags__;
    F->Refcnt = 1;
    F->Priv   = 0;

    if (De->Node->Ops->Open(De->Node, F) != 0)
    {
        KFree(F);
        return 0;
    }

    ReleaseMutex(&VfsLock);
    return F;
}

/**
 * @brief Close an open file
 *
 * Closes the specified file, releasing associated resources and
 * calling the filesystem's close operation if available.
 *
 * @param __File__ Pointer to the File structure to close
 * @return 0 on success, -1 on failure
 */
int
VfsClose(File* __File__)
{
    AcquireMutex(&VfsLock);
    if (!__File__)
    {
        return -1;
    }

    if (__File__->Node && __File__->Node->Ops && __File__->Node->Ops->Close)
    {
        __File__->Node->Ops->Close(__File__);
    }

    KFree(__File__);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Read data from an open file
 *
 * Reads up to __Len__ bytes from the file into the provided buffer,
 * updating the file's offset.
 *
 * @param __File__ Pointer to the open File structure
 * @param __Buf__ Buffer to store the read data
 * @param __Len__ Maximum number of bytes to read
 * @return Number of bytes read, or -1 on failure
 */
long
VfsRead(File* __File__, void* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Read)
    {
        return -1;
    }

    long Got = __File__->Node->Ops->Read(__File__, __Buf__, __Len__);
    if (Got > 0)
    {
        __File__->Offset += Got;
    }
    ReleaseMutex(&VfsLock);
    return Got;
}

/**
 * @brief Write data to an open file
 *
 * Writes up to __Len__ bytes from the buffer to the file,
 * updating the file's offset.
 *
 * @param __File__ Pointer to the open File structure
 * @param __Buf__ Buffer containing the data to write
 * @param __Len__ Number of bytes to write
 * @return Number of bytes written, or -1 on failure
 */
long
VfsWrite(File* __File__, const void* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Write)
    {
        return -1;
    }

    long Put = __File__->Node->Ops->Write(__File__, __Buf__, __Len__);
    if (Put > 0)
    {
        __File__->Offset += Put;
    }
    ReleaseMutex(&VfsLock);
    return Put;
}

/**
 * @brief Seek to a position in an open file
 *
 * Changes the file offset based on the specified offset and whence
 * parameter (SEEK_SET, SEEK_CUR, SEEK_END).
 *
 * @param __File__ Pointer to the open File structure
 * @param __Off__ Offset value
 * @param __Whence__ Reference point for the offset
 * @return New file offset, or -1 on failure
 */
long
VfsLseek(File* __File__, long __Off__, int __Whence__)
{
    AcquireMutex(&VfsLock);
    if (!__File__)
    {
        return -1;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Lseek)
    {
        return -1;
    }

    long New = __File__->Node->Ops->Lseek(__File__, __Off__, __Whence__);
    if (New >= 0)
    {
        __File__->Offset = New;
    }
    ReleaseMutex(&VfsLock);
    return New;
}

/**
 * @brief Perform an I/O control operation on an open file
 *
 * Sends a control command to the file's underlying filesystem driver,
 * potentially with additional arguments.
 *
 * @param __File__ Pointer to the open File structure
 * @param __Cmd__ I/O control command
 * @param __Arg__ Pointer to command arguments (may be NULL)
 * @return 0 on success, -1 on failure
 */
int
VfsIoctl(File* __File__, unsigned long __Cmd__, void* __Arg__)
{
    AcquireMutex(&VfsLock);
    if (!__File__)
    {
        return -1;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Ioctl)
    {
        return -1;
    }

    ReleaseMutex(&VfsLock);
    return __File__->Node->Ops->Ioctl(__File__, __Cmd__, __Arg__);
}

/**
 * @brief Synchronize file data to storage
 *
 * Forces any pending writes to the file to be written to the underlying
 * storage device.
 *
 * @param __File__ Pointer to the open File structure
 * @return 0 on success, -1 on failure
 */
int
VfsFsync(File* __File__)
{
    AcquireMutex(&VfsLock);
    if (!__File__ || !__File__->Node || !__File__->Node->Ops)
    {
        return -1;
    }

    if (!__File__->Node->Ops->Sync)
    {
        return 0;
    }

    ReleaseMutex(&VfsLock);
    return __File__->Node->Ops->Sync(__File__->Node);
}

/**
 * @brief Get file status information
 *
 * Retrieves status information about the file, such as size, permissions,
 * and timestamps, storing it in the provided buffer.
 *
 * @param __File__ Pointer to the open File structure
 * @param __Buf__ Buffer to store the file status
 * @return 0 on success, -1 on failure
 */
int
VfsFstats(File* __File__, VfsStat* __Buf__)
{
    AcquireMutex(&VfsLock);
    if (!__File__ || !__Buf__)
    {
        return -1;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Stat)
    {
        return -1;
    }

    ReleaseMutex(&VfsLock);
    return __File__->Node->Ops->Stat(__File__->Node, __Buf__);
}

/**
 * @brief Get file status information by path
 *
 * Retrieves status information about the file at the specified path,
 * storing it in the provided buffer.
 *
 * @param __Path__ Path to the file
 * @param __Buf__ Buffer to store the file status
 * @return 0 on success, -1 on failure
 */
int
VfsStats(const char* __Path__, VfsStat* __Buf__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Buf__)
    {
        return -1;
    }

    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        return -1;
    }

    if (!De->Node->Ops || !De->Node->Ops->Stat)
    {
        return -1;
    }

    ReleaseMutex(&VfsLock);
    return De->Node->Ops->Stat(De->Node, __Buf__);
}

/**
 * @brief Read directory entries
 *
 * Reads directory entries from the specified path into the provided buffer.
 * The format of the entries depends on the filesystem implementation.
 *
 * @param __Path__ Path to the directory
 * @param __Buf__ Buffer to store directory entries
 * @param __BufLen__ Size of the buffer
 * @return Number of bytes written to buffer, or -1 on failure
 */
long
VfsReaddir(const char* __Path__, void* __Buf__, long __BufLen__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Buf__ || __BufLen__ <= 0)
    {
        return -1;
    }

    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        return -1;
    }

    if (!De->Node->Ops || !De->Node->Ops->Readdir)
    {
        return -1;
    }

    ReleaseMutex(&VfsLock);
    return De->Node->Ops->Readdir(De->Node, __Buf__, __BufLen__);
}

/**
 * @brief Read directory entries from an open file
 *
 * Reads directory entries from an open directory file into the provided buffer.
 * The file must be opened for reading.
 *
 * @param __Dir__ Pointer to the open directory File structure
 * @param __Buf__ Buffer to store directory entries
 * @param __BufLen__ Size of the buffer
 * @return Number of bytes written to buffer, or -1 on failure
 */
long
VfsReaddirF(File* __Dir__, void* __Buf__, long __BufLen__)
{
    AcquireMutex(&VfsLock);
    if (!__Dir__ || !__Buf__ || __BufLen__ <= 0)
    {
        return -1;
    }

    if (!__Dir__->Node || !__Dir__->Node->Ops || !__Dir__->Node->Ops->Readdir)
    {
        return -1;
    }

    ReleaseMutex(&VfsLock);
    return __Dir__->Node->Ops->Readdir(__Dir__->Node, __Buf__, __BufLen__);
}

/**
 * @brief Create a new file
 *
 * Creates a new file at the specified path with the given flags and permissions.
 * The parent directory must exist.
 *
 * @param __Path__ Path where the file should be created
 * @param __Flags__ Creation flags (e.g., exclusive creation)
 * @param __Perm__ Permissions for the new file
 * @return 0 on success, -1 on failure
 */
int
VfsCreate(const char* __Path__, long __Flags__, VfsPerm __Perm__)
{
    AcquireMutex(&VfsLock);
    Dentry* Parent = 0;
    char    Name[256];
    if (!__Path__)
    {
        return -1;
    }
    const char* __Path = __Path__;
    if (__is_sep__(*__Path))
    {
        __Path = __skip_sep__(__Path);
    }
    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    while (*__Path)
    {
        long N = __next_comp__(__Path, Name, sizeof(Name));
        if (N <= 0)
        {
            break;
        }
        while (*__Path && !__is_sep__(*__Path))
        {
            __Path++;
        }
        __Path = __skip_sep__(__Path);
        Parent = De;
        if (*__Path == 0)
        {
            break;
        }
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return -1;
        }
        __builtin_memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            return -1;
        }
        Cur = Next;
    }
    if (!Parent || !Parent->Node || !Parent->Node->Ops || !Parent->Node->Ops->Create)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return Parent->Node->Ops->Create(Parent->Node, Name, __Flags__, __Perm__);
}

/**
 * @brief Remove a file or symbolic link
 *
 * Removes the file or symbolic link at the specified path.
 * The file must not be currently open.
 *
 * @param __Path__ Path to the file to remove
 * @return 0 on success, -1 on failure
 */
int
VfsUnlink(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    Dentry* Base = 0;
    char    Name[256];
    if (!__Path__)
    {
        return -1;
    }
    const char* __Path = __Path__;
    if (__is_sep__(*__Path))
    {
        __Path = __skip_sep__(__Path);
    }
    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    while (*__Path)
    {
        long N = __next_comp__(__Path, Name, sizeof(Name));
        if (N <= 0)
        {
            break;
        }
        while (*__Path && !__is_sep__(*__Path))
        {
            __Path++;
        }
        __Path = __skip_sep__(__Path);
        if (*__Path == 0)
        {
            Base = De;
            break;
        }
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return -1;
        }
        __builtin_memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            return -1;
        }
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Unlink)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return Base->Node->Ops->Unlink(Base->Node, Name);
}

/**
 * @brief Create a directory
 *
 * Creates a new directory at the specified path with the given permissions.
 * Parent directories must exist.
 *
 * @param __Path__ Path where the directory should be created
 * @param __Perm__ Permissions for the new directory
 * @return 0 on success, -1 on failure
 */
int
VfsMkdir(const char* __Path__, VfsPerm __Perm__)
{
    AcquireMutex(&VfsLock);
    Dentry* Base = 0;
    char    Name[256];
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock);
        return -1;
    }
    const char* __PathCur = __Path__;
    if (__is_sep__(*__PathCur))
    {
        __PathCur = __skip_sep__(__PathCur);
    }
    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    while (*__PathCur)
    {
        long N = __next_comp__(__PathCur, Name, sizeof(Name));
        if (N <= 0)
        {
            break;
        }
        while (*__PathCur && !__is_sep__(*__PathCur))
        {
            __PathCur++;
        }
        __PathCur = __skip_sep__(__PathCur);
        if (*__PathCur == 0)
        {
            Base = De;
            break;
        }
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            ReleaseMutex(&VfsLock);
            return -1;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock);
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock);
            return -1;
        }
        __builtin_memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            ReleaseMutex(&VfsLock);
            return -1;
        }
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Mkdir)
    {
        ReleaseMutex(&VfsLock);
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return Base->Node->Ops->Mkdir(Base->Node, Name, __Perm__);
}

/**
 * @brief Remove a directory
 *
 * Removes the directory at the specified path. The directory must be empty.
 *
 * @param __Path__ Path to the directory to remove
 * @return 0 on success, -1 on failure
 */
int
VfsRmdir(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    Dentry* Base = 0;
    char    Name[256];
    if (!__Path__)
    {
        return -1;
    }
    const char* p = __Path__;
    if (__is_sep__(*p))
    {
        p = __skip_sep__(p);
    }
    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    while (*p)
    {
        long N = __next_comp__(p, Name, sizeof(Name));
        if (N <= 0)
        {
            break;
        }
        while (*p && !__is_sep__(*p))
        {
            p++;
        }
        p = __skip_sep__(p);
        if (*p == 0)
        {
            Base = De;
            break;
        }
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc(N + 1);
        __builtin_memcpy(Dup, Name, N + 1);
        De  = __alloc_dentry__(Dup, De, Next);
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Rmdir)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return Base->Node->Ops->Rmdir(Base->Node, Name);
}

/**
 * @brief Create a symbolic link
 *
 * Creates a symbolic link at __LinkPath__ pointing to __Target__ with
 * the specified permissions.
 *
 * @param __Target__ Path that the symbolic link should point to
 * @param __LinkPath__ Path where the symbolic link should be created
 * @param __Perm__ Permissions for the symbolic link
 * @return 0 on success, -1 on failure
 */
int
VfsSymlink(const char* __Target__, const char* __LinkPath__, VfsPerm __Perm__)
{
    AcquireMutex(&VfsLock);
    Dentry* Base = 0;
    char    Name[256];
    if (!__LinkPath__ || !__Target__)
    {
        return -1;
    }
    const char* __Path = __LinkPath__;
    if (__is_sep__(*__Path))
    {
        __Path = __skip_sep__(__Path);
    }
    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    while (*__Path)
    {
        long N = __next_comp__(__Path, Name, sizeof(Name));
        if (N <= 0)
        {
            break;
        }
        while (*__Path && !__is_sep__(*__Path))
        {
            __Path++;
        }
        __Path = __skip_sep__(__Path);
        if (*__Path == 0)
        {
            Base = De;
            break;
        }
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return -1;
        }
        __builtin_memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            return -1;
        }
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Symlink)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return Base->Node->Ops->Symlink(Base->Node, Name, __Target__, __Perm__);
}

/**
 * @brief Read the target of a symbolic link
 *
 * Reads the target path that the symbolic link at __Path__ points to,
 * storing it in the provided buffer.
 *
 * @param __Path__ Path to the symbolic link
 * @param __Buf__ Buffer to store the target path
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
VfsReadlink(const char* __Path__, char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }

    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        return -1;
    }
    if (!De->Node->Ops || !De->Node->Ops->Readlink)
    {
        return -1;
    }

    VfsNameBuf NB;
    NB.Buf = __Buf__;
    NB.Len = __Len__;
    ReleaseMutex(&VfsLock);
    return De->Node->Ops->Readlink(De->Node, &NB);
}

/**
 * @brief Create a hard link
 *
 * Creates a hard link at __NewPath__ pointing to the same inode as __OldPath__.
 *
 * @param __OldPath__ Path to the existing file
 * @param __NewPath__ Path where the hard link should be created
 * @return 0 on success, -1 on failure
 */
int
VfsLink(const char* __OldPath__, const char* __NewPath__)
{
    AcquireMutex(&VfsLock);
    if (!__OldPath__ || !__NewPath__)
    {
        return -1;
    }

    Dentry* OldDe   = VfsResolve(__OldPath__);
    Dentry* NewBase = 0;
    char    Name[256];

    if (!OldDe || !OldDe->Node)
    {
        return -1;
    }

    const char* __Path = __NewPath__;
    if (__is_sep__(*__Path))
    {
        __Path = __skip_sep__(__Path);
    }
    Vnode*  Cur = __RootNode__;
    Dentry* De  = __RootDe__;
    while (*__Path)
    {
        long N = __next_comp__(__Path, Name, sizeof(Name));
        if (N <= 0)
        {
            break;
        }
        while (*__Path && !__is_sep__(*__Path))
        {
            __Path++;
        }
        __Path = __skip_sep__(__Path);
        if (*__Path == 0)
        {
            NewBase = De;
            break;
        }
        if (!Cur || !Cur->Ops || !Cur->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return -1;
        }
        __builtin_memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            return -1;
        }
        Cur = Next;
    }

    if (!NewBase || !NewBase->Node || !NewBase->Node->Ops || !NewBase->Node->Ops->Link)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return NewBase->Node->Ops->Link(NewBase->Node, OldDe->Node, Name);
}

/**
 * @brief Rename or move a file or directory
 *
 * Renames or moves the file or directory from __OldPath__ to __NewPath__.
 * If __NewPath__ exists, it may be overwritten depending on __Flags__.
 *
 * @param __OldPath__ Current path of the file/directory
 * @param __NewPath__ New path for the file/directory
 * @param __Flags__ Rename flags (e.g., no overwrite)
 * @return 0 on success, -1 on failure
 */
int
VfsRename(const char* __OldPath__, const char* __NewPath__, long __Flags__)
{
    AcquireMutex(&VfsLock);
    Dentry* OldBase = 0;
    Dentry* NewBase = 0;
    char    OldName[256];
    char    NewName[256];

    if (!__OldPath__ || !__NewPath__)
    {
        return -1;
    }

    const char* po = __OldPath__;
    if (__is_sep__(*po))
    {
        po = __skip_sep__(po);
    }
    Vnode*  CurO = __RootNode__;
    Dentry* DeO  = __RootDe__;
    while (*po)
    {
        long N = __next_comp__(po, OldName, sizeof(OldName));
        if (N <= 0)
        {
            break;
        }
        while (*po && !__is_sep__(*po))
        {
            po++;
        }
        po = __skip_sep__(po);
        if (*po == 0)
        {
            OldBase = DeO;
            break;
        }
        if (!CurO || !CurO->Ops || !CurO->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = CurO->Ops->Lookup(CurO, OldName);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return -1;
        }
        __builtin_memcpy(Dup, OldName, (size_t)(N + 1));
        DeO = __alloc_dentry__(Dup, DeO, Next);
        if (!DeO)
        {
            return -1;
        }
        CurO = Next;
    }

    const char* pn = __NewPath__;
    if (__is_sep__(*pn))
    {
        pn = __skip_sep__(pn);
    }
    Vnode*  CurN = __RootNode__;
    Dentry* DeN  = __RootDe__;
    while (*pn)
    {
        long N = __next_comp__(pn, NewName, sizeof(NewName));
        if (N <= 0)
        {
            break;
        }
        while (*pn && !__is_sep__(*pn))
        {
            pn++;
        }
        pn = __skip_sep__(pn);
        if (*pn == 0)
        {
            NewBase = DeN;
            break;
        }
        if (!CurN || !CurN->Ops || !CurN->Ops->Lookup)
        {
            return -1;
        }
        Vnode* Next = CurN->Ops->Lookup(CurN, NewName);
        if (!Next)
        {
            return -1;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return -1;
        }
        __builtin_memcpy(Dup, NewName, (size_t)(N + 1));
        DeN = __alloc_dentry__(Dup, DeN, Next);
        if (!DeN)
        {
            return -1;
        }
        CurN = Next;
    }

    if (!OldBase || !NewBase)
    {
        return -1;
    }
    if (!OldBase->Node || !NewBase->Node)
    {
        return -1;
    }
    if (!OldBase->Node->Ops || !OldBase->Node->Ops->Rename)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return OldBase->Node->Ops->Rename(OldBase->Node, OldName, NewBase->Node, NewName, __Flags__);
}

/**
 * @brief Change file permissions
 *
 * Changes the permissions (mode) of the file or directory at __Path__.
 *
 * @param __Path__ Path to the file/directory
 * @param __Mode__ New permissions mode
 * @return 0 on success, -1 on failure
 */
int
VfsChmod(const char* __Path__, long __Mode__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        return -1;
    }
    if (!De->Node->Ops || !De->Node->Ops->Chmod)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return De->Node->Ops->Chmod(De->Node, __Mode__);
}

/**
 * @brief Change file ownership
 *
 * Changes the owner (UID) and group (GID) of the file or directory at __Path__.
 *
 * @param __Path__ Path to the file/directory
 * @param __Uid__ New user ID (owner)
 * @param __Gid__ New group ID
 * @return 0 on success, -1 on failure
 */
int
VfsChown(const char* __Path__, long __Uid__, long __Gid__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        return -1;
    }
    if (!De->Node->Ops || !De->Node->Ops->Chown)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return De->Node->Ops->Chown(De->Node, __Uid__, __Gid__);
}

/**
 * @brief Truncate a file to a specified length
 *
 * Changes the size of the file at __Path__ to __Len__ bytes. If __Len__
 * is smaller than the current size, data is lost. If larger, the file
 * is extended (usually with zeros).
 *
 * @param __Path__ Path to the file
 * @param __Len__ New length of the file
 * @return 0 on success, -1 on failure
 */
int
VfsTruncate(const char* __Path__, long __Len__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        return -1;
    }
    if (!De->Node->Ops || !De->Node->Ops->Truncate)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return De->Node->Ops->Truncate(De->Node, __Len__);
}

/**
 * @brief Increment vnode reference count
 *
 * Increases the reference count of the vnode, indicating that another
 * component is using it.
 *
 * @param __Node__ Pointer to the vnode
 * @return New reference count, or -1 on failure
 */
int
VnodeRefInc(Vnode* __Node__)
{
    AcquireMutex(&VfsLock);
    if (!__Node__)
    {
        return -1;
    }
    __Node__->Refcnt++;
    ReleaseMutex(&VfsLock);
    return (int)__Node__->Refcnt;
}

/**
 * @brief Decrement vnode reference count
 *
 * Decreases the reference count of the vnode. When it reaches zero,
 * the vnode may be freed.
 *
 * @param __Node__ Pointer to the vnode
 * @return New reference count, or -1 on failure
 */
int
VnodeRefDec(Vnode* __Node__)
{
    AcquireMutex(&VfsLock);
    if (!__Node__)
    {
        return -1;
    }
    if (__Node__->Refcnt > 0)
    {
        __Node__->Refcnt--;
    }
    ReleaseMutex(&VfsLock);
    return (int)__Node__->Refcnt;
}

/**
 * @brief Get vnode attributes
 *
 * Retrieves attributes of the vnode, such as size and timestamps,
 * storing them in the provided buffer.
 *
 * @param __Node__ Pointer to the vnode
 * @param __Buf__ Buffer to store the attributes
 * @return 0 on success, -1 on failure
 */
int
VnodeGetAttr(Vnode* __Node__, VfsStat* __Buf__)
{
    AcquireMutex(&VfsLock);
    if (!__Node__ || !__Buf__)
    {
        return -1;
    }
    if (!__Node__->Ops || !__Node__->Ops->Stat)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return __Node__->Ops->Stat(__Node__, __Buf__);
}

/**
 * @brief Set vnode attributes (stub)
 *
 * Sets attributes of the vnode from the provided buffer.
 * Currently not implemented.
 *
 * @param __Node__ Pointer to the vnode
 * @param __Buf__ Buffer containing the new attributes
 * @return -1 (not implemented)
 */
int
VnodeSetAttr(Vnode* __Node__, const VfsStat* __Buf__)
{
    AcquireMutex(&VfsLock);
    (void)__Node__;
    (void)__Buf__;
    ReleaseMutex(&VfsLock);
    return -1;
}

/**
 * @brief Invalidate a dentry
 *
 * Marks the dentry as invalid, indicating that its cached information
 * may no longer be accurate.
 *
 * @param __De__ Pointer to the dentry to invalidate
 * @return 0 on success, -1 on failure
 */
int
DentryInvalidate(Dentry* __De__)
{
    AcquireMutex(&VfsLock);
    if (!__De__)
    {
        return -1;
    }
    __De__->Flags |= 1;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Revalidate a dentry
 *
 * Clears the invalid flag on the dentry, allowing it to be used again.
 *
 * @param __De__ Pointer to the dentry to revalidate
 * @return 0 on success, -1 on failure
 */
int
DentryRevalidate(Dentry* __De__)
{
    AcquireMutex(&VfsLock);
    if (!__De__)
    {
        return -1;
    }
    __De__->Flags &= ~1;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Attach a vnode to a dentry
 *
 * Associates the given vnode with the dentry.
 *
 * @param __De__ Pointer to the dentry
 * @param __Node__ Pointer to the vnode to attach
 * @return 0 on success, -1 on failure
 */
int
DentryAttach(Dentry* __De__, Vnode* __Node__)
{
    AcquireMutex(&VfsLock);
    if (!__De__ || !__Node__)
    {
        return -1;
    }
    __De__->Node = __Node__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Detach vnode from a dentry
 *
 * Removes the association between the vnode and the dentry.
 *
 * @param __De__ Pointer to the dentry
 * @return 0 on success, -1 on failure
 */
int
DentryDetach(Dentry* __De__)
{
    AcquireMutex(&VfsLock);
    if (!__De__)
    {
        return -1;
    }
    __De__->Node = 0;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the name of a dentry
 *
 * Copies the name of the dentry into the provided buffer.
 *
 * @param __De__ Pointer to the dentry
 * @param __Buf__ Buffer to store the name
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
DentryName(Dentry* __De__, char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__De__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    long N = (long)strlen(__De__->Name);
    if (N >= __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Buf__, __De__->Name, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Set current working directory (stub)
 *
 * Sets the current working directory to the specified path.
 * Currently not implemented.
 *
 * @param __Path__ Path to set as current working directory
 * @return 0 (stub implementation)
 */
int
VfsSetCwd(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get current working directory
 *
 * Retrieves the current working directory path, storing it in the buffer.
 * Currently returns "/".
 *
 * @param __Buf__ Buffer to store the path
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
VfsGetCwd(char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    const char* __Path = "/";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Set the root filesystem
 *
 * Changes the root filesystem to the filesystem containing the specified path.
 *
 * @param __Path__ Path to the new root directory
 * @return 0 on success, -1 on failure
 */
int
VfsSetRoot(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return VfsSwitchRoot(__Path__);
}

/**
 * @brief Get the root filesystem path
 *
 * Retrieves the current root filesystem path, storing it in the buffer.
 * Currently returns "/".
 *
 * @param __Buf__ Buffer to store the path
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
VfsGetRoot(char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    const char* __Path = "/";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Set the umask value
 *
 * Sets the umask value used for creating new files and directories.
 *
 * @param __Mode__ New umask value
 * @return 0 on success
 */
int
VfsSetUmask(long __Mode__)
{
    AcquireMutex(&VfsLock);
    __Umask__ = __Mode__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the current umask value
 *
 * Returns the current umask value used for file and directory creation.
 *
 * @return Current umask value
 */
long
VfsGetUmask(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return __Umask__;
}

/**
 * @brief Subscribe to filesystem notifications (stub)
 *
 * Subscribes to filesystem change notifications for the specified path
 * with the given event mask. Currently not implemented.
 *
 * @param __Path__ Path to monitor for changes
 * @param __Mask__ Bitmask of events to monitor (e.g., file creation, deletion)
 * @return 0 (stub implementation)
 */
int
VfsNotifySubscribe(const char* __Path__, long __Mask__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    (void)__Mask__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Unsubscribe from filesystem notifications (stub)
 *
 * Removes the subscription for filesystem notifications on the specified path.
 * Currently not implemented.
 *
 * @param __Path__ Path to stop monitoring
 * @return 0 (stub implementation)
 */
int
VfsNotifyUnsubscribe(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Poll for filesystem notifications (stub)
 *
 * Checks for pending filesystem notifications on the specified path,
 * storing the event mask in the output parameter. Currently not implemented.
 *
 * @param __Path__ Path to check for notifications
 * @param __OutMask__ Pointer to store the event mask
 * @return 0 (stub implementation)
 */
int
VfsNotifyPoll(const char* __Path__, long* __OutMask__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    if (!__OutMask__)
    {
        return -1;
    }
    *__OutMask__ = 0;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Check access permissions for a path (stub)
 *
 * Checks if the current process has the specified access permissions
 * for the file or directory at the given path. Currently only checks
 * if the path exists.
 *
 * @param __Path__ Path to check access for
 * @param __Mode__ Access mode flags (read, write, execute)
 * @return 0 if accessible, -1 if not found or access denied
 */
int
VfsAccess(const char* __Path__, long __Mode__)
{
    AcquireMutex(&VfsLock);
    (void)__Mode__;
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock);
    return De ? 0 : -1;
}

/**
 * @brief Check if a path exists
 *
 * Determines whether the specified path exists in the filesystem.
 *
 * @param __Path__ Path to check for existence
 * @return 1 if exists, 0 if not found
 */
int
VfsExists(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock);
    return De ? 1 : 0;
}

/**
 * @brief Check if a path is a directory
 *
 * Determines whether the specified path refers to a directory.
 *
 * @param __Path__ Path to check
 * @return 1 if directory, 0 otherwise
 */
int
VfsIsDir(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock);
    return (De && De->Node && De->Node->Type == VNodeDIR) ? 1 : 0;
}

/**
 * @brief Check if a path is a regular file
 *
 * Determines whether the specified path refers to a regular file.
 *
 * @param __Path__ Path to check
 * @return 1 if regular file, 0 otherwise
 */
int
VfsIsFile(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock);
    return (De && De->Node && De->Node->Type == VNodeFILE) ? 1 : 0;
}

/**
 * @brief Check if a path is a symbolic link
 *
 * Determines whether the specified path refers to a symbolic link.
 *
 * @param __Path__ Path to check
 * @return 1 if symbolic link, 0 otherwise
 */
int
VfsIsSymlink(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock);
    return (De && De->Node && De->Node->Type == VNodeSYM) ? 1 : 0;
}

/**
 * @brief Copy a file from source to destination
 *
 * Copies the contents of the file at __Src__ to the location __Dst__.
 * The destination file is created if it doesn't exist, or truncated if it does.
 *
 * @param __Src__ Source file path
 * @param __Dst__ Destination file path
 * @param __Flags__ Copy flags (currently unused)
 * @return 0 on success, -1 on failure
 */
int
VfsCopy(const char* __Src__, const char* __Dst__, long __Flags__)
{
    AcquireMutex(&VfsLock);
    (void)__Flags__;
    File* S = VfsOpen(__Src__, VFlgRDONLY);
    if (!S)
    {
        return -1;
    }
    File* D = VfsOpen(__Dst__, VFlgCREATE | VFlgWRONLY | VFlgTRUNC);
    if (!D)
    {
        VfsClose(S);
        return -1;
    }

    char Buf[4096];
    for (;;)
    {
        long r = VfsRead(S, Buf, sizeof(Buf));
        if (r < 0)
        {
            VfsClose(S);
            VfsClose(D);
            return -1;
        }
        if (r == 0)
        {
            break;
        }
        long w = VfsWrite(D, Buf, r);
        if (w != r)
        {
            VfsClose(S);
            VfsClose(D);
            return -1;
        }
    }

    VfsClose(S);
    VfsClose(D);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Move or rename a file or directory
 *
 * Attempts to rename/move the file or directory from __Src__ to __Dst__.
 * If rename fails (e.g., across filesystems), falls back to copy+delete.
 *
 * @param __Src__ Source path
 * @param __Dst__ Destination path
 * @param __Flags__ Move flags (passed to rename operation)
 * @return 0 on success, -1 on failure
 */
int
VfsMove(const char* __Src__, const char* __Dst__, long __Flags__)
{
    AcquireMutex(&VfsLock);
    int rc = VfsRename(__Src__, __Dst__, __Flags__);
    if (rc == 0)
    {
        return 0;
    }
    rc = VfsCopy(__Src__, __Dst__, __Flags__);
    if (rc != 0)
    {
        return -1;
    }
    ReleaseMutex(&VfsLock);
    return VfsUnlink(__Src__);
}

/**
 * @brief Read entire file into buffer
 *
 * Reads the entire contents of the file at __Path__ into the provided buffer,
 * up to __BufLen__ bytes. The actual number of bytes read is stored in __OutLen__.
 *
 * @param __Path__ Path to the file to read
 * @param __Buf__ Buffer to store file contents
 * @param __BufLen__ Size of the buffer
 * @param __OutLen__ Pointer to store the number of bytes read (may be NULL)
 * @return 0 on success, -1 on failure
 */
int
VfsReadAll(const char* __Path__, void* __Buf__, long __BufLen__, long* __OutLen__)
{
    AcquireMutex(&VfsLock);
    File* F = VfsOpen(__Path__, VFlgRDONLY);
    if (!F)
    {
        return -1;
    }
    long total = 0;
    while (total < __BufLen__)
    {
        long r = VfsRead(F, (char*)__Buf__ + total, __BufLen__ - total);
        if (r < 0)
        {
            VfsClose(F);
            return -1;
        }
        if (r == 0)
        {
            break;
        }
        total += r;
    }
    if (__OutLen__)
    {
        *__OutLen__ = total;
    }
    VfsClose(F);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Write entire buffer to file
 *
 * Writes the entire contents of the buffer to the file at __Path__,
 * creating or truncating the file as needed.
 *
 * @param __Path__ Path to the file to write
 * @param __Buf__ Buffer containing data to write
 * @param __Len__ Number of bytes to write
 * @return 0 on success, -1 on failure
 */
int
VfsWriteAll(const char* __Path__, const void* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    File* F = VfsOpen(__Path__, VFlgCREATE | VFlgWRONLY | VFlgTRUNC);
    if (!F)
    {
        return -1;
    }
    long total = 0;
    while (total < __Len__)
    {
        long w = VfsWrite(F, (const char*)__Buf__ + total, __Len__ - total);
        if (w <= 0)
        {
            VfsClose(F);
            return -1;
        }
        total += w;
    }
    VfsClose(F);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Enumerate mounted filesystems
 *
 * Lists all currently mounted filesystem paths in the provided buffer,
 * separated by newlines.
 *
 * @param __Buf__ Buffer to store the mount table
 * @param __Len__ Size of the buffer
 * @return Number of bytes written, or -1 on failure
 */
int
VfsMountTableEnumerate(char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    long off = 0;
    for (long I = 0; I < __MountCount__; I++)
    {
        const char* __Path = __Mounts__[I].Path;
        long        N      = (long)strlen(__Path);
        if (off + N + 2 >= __Len__)
        {
            break;
        }
        __builtin_memcpy(__Buf__ + off, __Path, (size_t)N);
        off += N;
        __Buf__[off++] = '\n';
    }
    if (off < __Len__)
    {
        __Buf__[off] = 0;
    }
    ReleaseMutex(&VfsLock);
    return (int)off;
}

/**
 * @brief Find a mount point in the mount table
 *
 * Searches for the specified path in the mount table and copies it
 * to the buffer if found.
 *
 * @param __Path__ Path to search for in mount table
 * @param __Buf__ Buffer to store the found path
 * @param __Len__ Size of the buffer
 * @return 0 if found, -1 if not found or buffer too small
 */
int
VfsMountTableFind(const char* __Path__, char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    for (long I = 0; I < __MountCount__; I++)
    {
        if (strcmp(__Mounts__[I].Path, __Path__) == 0)
        {
            long N = (long)strlen(__Mounts__[I].Path);
            if (N >= __Len__)
            {
                return -1;
            }
            __builtin_memcpy(__Buf__, __Mounts__[I].Path, (size_t)(N + 1));
            return 0;
        }
    }
    ReleaseMutex(&VfsLock);
    return -1;
}

/**
 * @brief Get the full path of a vnode (stub)
 *
 * Retrieves the full path of the given vnode, storing it in the buffer.
 * Currently returns "/" for all vnodes.
 *
 * @param __Node__ Pointer to the vnode
 * @param __Buf__ Buffer to store the path
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
VfsNodePath(Vnode* __Node__, char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    (void)__Node__;
    if (!__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    const char* __Path = "/";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the name of a vnode (stub)
 *
 * Retrieves the name of the given vnode, storing it in the buffer.
 * Currently returns an empty string for all vnodes.
 *
 * @param __Node__ Pointer to the vnode
 * @param __Buf__ Buffer to store the name
 * @param __Len__ Size of the buffer
 * @return 0 on success, -1 on failure
 */
int
VfsNodeName(Vnode* __Node__, char* __Buf__, long __Len__)
{
    AcquireMutex(&VfsLock);
    (void)__Node__;
    if (!__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    const char* __Path = "";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Allocate memory for a name string
 *
 * Allocates memory for a string of the specified length using the kernel heap.
 *
 * @param __Out__ Pointer to store the allocated string pointer
 * @param __Len__ Length of the string to allocate (including null terminator)
 * @return 0 on success, -1 on failure
 */
int
VfsAllocName(char** __Out__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__Out__ || __Len__ <= 0)
    {
        return -1;
    }
    *__Out__ = (char*)KMalloc((size_t)__Len__);
    ReleaseMutex(&VfsLock);
    return *__Out__ ? 0 : -1;
}

/**
 * @brief Free memory allocated for a name string
 *
 * Frees memory that was allocated for a name string using VfsAllocName.
 *
 * @param __Name__ Pointer to the string to free
 * @return 0 on success, -1 if pointer is NULL
 */
int
VfsFreeName(char* __Name__)
{
    AcquireMutex(&VfsLock);
    if (!__Name__)
    {
        return -1;
    }
    KFree(__Name__);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Join two path components
 *
 * Concatenates two path components with a '/' separator, storing the
 * result in the provided buffer.
 *
 * @param __A__ First path component
 * @param __B__ Second path component
 * @param __Out__ Buffer to store the joined path
 * @param __Len__ Size of the output buffer
 * @return 0 on success, -1 on failure (buffer too small)
 */
int
VfsJoinPath(const char* __A__, const char* __B__, char* __Out__, long __Len__)
{
    AcquireMutex(&VfsLock);
    if (!__A__ || !__B__ || !__Out__ || __Len__ <= 0)
    {
        return -1;
    }
    long la   = (long)strlen(__A__);
    long lb   = (long)strlen(__B__);
    long need = la + 1 + lb + 1;
    if (need > __Len__)
    {
        return -1;
    }
    __builtin_memcpy(__Out__, __A__, (size_t)la);
    __Out__[la] = '/';
    __builtin_memcpy(__Out__ + la + 1, __B__, (size_t)lb);
    __Out__[la + 1 + lb] = 0;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Set a flag on a file or directory (stub)
 *
 * Sets the specified flag on the file or directory at the given path.
 * Currently not implemented.
 *
 * @param __Path__ Path to the file/directory
 * @param __Flag__ Flag to set
 * @return 0 (stub implementation)
 */
int
VfsSetFlag(const char* __Path__, long __Flag__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    (void)__Flag__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Clear a flag on a file or directory (stub)
 *
 * Clears the specified flag on the file or directory at the given path.
 * Currently not implemented.
 *
 * @param __Path__ Path to the file/directory
 * @param __Flag__ Flag to clear
 * @return 0 (stub implementation)
 */
int
VfsClearFlag(const char* __Path__, long __Flag__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    (void)__Flag__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get flags of a file or directory (stub)
 *
 * Retrieves the flags set on the file or directory at the given path.
 * Currently returns 0.
 *
 * @param __Path__ Path to the file/directory
 * @return Current flags (always 0)
 */
long
VfsGetFlags(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Synchronize all mounted filesystems
 *
 * Forces all mounted filesystems to write their cached data to storage.
 * Calls the sync operation on each mounted superblock.
 *
 * @return 0 on success
 */
int
VfsSyncAll(void)
{
    AcquireMutex(&VfsLock);
    for (long I = 0; I < __MountCount__; I++)
    {
        Superblock* Sb = __Mounts__[I].Sb;
        if (Sb && Sb->Ops && Sb->Ops->Sync)
        {
            Sb->Ops->Sync(Sb);
        }
    }
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Prune filesystem caches (stub)
 *
 * Removes unused entries from filesystem caches to free memory.
 * Currently not implemented.
 *
 * @return 0 (stub implementation)
 */
int
VfsPruneCaches(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Register a device node
 *
 * Registers a device node at the specified path with associated private data.
 * Currently not implemented.
 *
 * @param __Path__ Path where the device node should be created
 * @param __Priv__ Private data associated with the device
 * @param __Flags__ Device flags
 * @return 0 on OK and -1 on FAIL
 */
int
VfsRegisterDevNode(const char* __Path__, void* __Priv__, long __Flags__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Priv__)
    {
        return -1;
    }

    /* Ensure parent directory exists */
    char Buf[1024];
    VfsRealpath(__Path__, Buf, sizeof(Buf));
    const char* Name = strrchr(Buf, '/');
    if (!Name)
    {
        return -1;
    }
    long nlen = (long)strlen(Name + 1);

    char Parent[1024];
    long plen = (long)(Name - Buf);
    __builtin_memcpy(Parent, Buf, plen);
    Parent[plen] = 0;
    VfsMkpath(Parent, 0);

    /* Create vnode for device */
    Vnode* Node = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Node)
    {
        return -1;
    }
    Node->Type   = VNodeDEV;
    Node->Ops    = (VnodeOps*)__Priv__; /* device ops table */
    Node->Sb     = __RootNode__->Sb;
    Node->Priv   = __Priv__;
    Node->Refcnt = 1;

    char* Dup = (char*)KMalloc(nlen + 1);
    __builtin_memcpy(Dup, Name + 1, nlen + 1);
    Dentry* De = __alloc_dentry__(Dup, __RootDe__, Node);
    if (!De)
    {
        KFree(Node);
        return -1;
    }

    PDebug("VFS: Registered devnode %s\n", __Path__);
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Unregister a device node (stub)
 *
 * Removes a device node from the specified path.
 * Currently not implemented.
 *
 * @param __Path__ Path of the device node to remove
 * @return 0 (stub implementation)
 */
int
VfsUnregisterDevNode(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    (void)__Path__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Register a pseudo filesystem
 *
 * Registers a pseudo filesystem (not backed by real storage) at the
 * specified path. The superblock is provided directly.
 *
 * @param __Path__ Mount point path for the pseudo filesystem
 * @param __Sb__ Superblock of the pseudo filesystem
 * @return 0 on success, -1 on failure (invalid parameters or mount table full)
 */
int
VfsRegisterPseudoFs(const char* __Path__, Superblock* __Sb__)
{
    AcquireMutex(&VfsLock);
    if (!__Path__ || !__Sb__)
    {
        return -1;
    }
    if (__MountCount__ >= __MaxMounts__)
    {
        return -1;
    }
    long            N = (long)strlen(__Path__);
    __MountEntry__* M = &__Mounts__[__MountCount__++];
    M->Sb             = __Sb__;
    __builtin_memcpy(M->Path, __Path__, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Unregister a pseudo filesystem
 *
 * Unregisters a pseudo filesystem by unmounting it from its path.
 *
 * @param __Path__ Mount point path of the pseudo filesystem
 * @return Result of the unmount operation
 */
int
VfsUnregisterPseudoFs(const char* __Path__)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return VfsUnmount(__Path__);
}

/**
 * @brief Set the default filesystem type
 *
 * Sets the default filesystem type to use when mounting without
 * specifying a type explicitly.
 *
 * @param __Name__ Name of the default filesystem type
 * @return 0 on success, -1 on failure (invalid name or name too long)
 */
int
VfsSetDefaultFs(const char* __Name__)
{
    AcquireMutex(&VfsLock);
    if (!__Name__)
    {
        return -1;
    }
    long N = (long)strlen(__Name__);
    if (N >= (long)sizeof(__DefaultFs__))
    {
        return -1;
    }
    __builtin_memcpy(__DefaultFs__, __Name__, (size_t)(N + 1));
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the default filesystem type
 *
 * Returns the name of the currently set default filesystem type.
 *
 * @return Name of the default filesystem type
 */
const char*
VfsGetDefaultFs(void)
{
    return __DefaultFs__;
}

/**
 * @brief Set the maximum filename length
 *
 * Sets the maximum allowed length for filenames in the VFS.
 *
 * @param __Len__ Maximum filename length
 * @return 0 on success, -1 if length is invalid
 */
int
VfsSetMaxName(long __Len__)
{
    AcquireMutex(&VfsLock);
    if (__Len__ < 1)
    {
        return -1;
    }
    __MaxName__ = __Len__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the maximum filename length
 *
 * Returns the current maximum allowed length for filenames.
 *
 * @return Maximum filename length
 */
long
VfsGetMaxName(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return __MaxName__;
}

/**
 * @brief Set the maximum path length
 *
 * Sets the maximum allowed length for paths in the VFS.
 *
 * @param __Len__ Maximum path length
 * @return 0 on success, -1 if length is invalid
 */
int
VfsSetMaxPath(long __Len__)
{
    AcquireMutex(&VfsLock);
    if (__Len__ < 1)
    {
        return -1;
    }
    __MaxPath__ = __Len__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the maximum path length
 *
 * Returns the current maximum allowed length for paths.
 *
 * @return Maximum path length
 */
long
VfsGetMaxPath(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return __MaxPath__;
}

/**
 * @brief Set the directory cache limit
 *
 * Sets the maximum number of directory entries to cache in the VFS.
 *
 * @param __Val__ Directory cache limit
 * @return 0 on success
 */
int
VfsSetDirCacheLimit(long __Val__)
{
    AcquireMutex(&VfsLock);
    __DirCacheLimit__ = __Val__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the directory cache limit
 *
 * Returns the current maximum number of directory entries to cache.
 *
 * @return Directory cache limit
 */
long
VfsGetDirCacheLimit(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return __DirCacheLimit__;
}

/**
 * @brief Set the file cache limit
 *
 * Sets the maximum number of file entries to cache in the VFS.
 *
 * @param __Val__ File cache limit
 * @return 0 on success
 */
int
VfsSetFileCacheLimit(long __Val__)
{
    AcquireMutex(&VfsLock);
    __FileCacheLimit__ = __Val__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the file cache limit
 *
 * Returns the current maximum number of file entries to cache.
 *
 * @return File cache limit
 */
long
VfsGetFileCacheLimit(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return __FileCacheLimit__;
}

/**
 * @brief Set the I/O block size
 *
 * Sets the preferred block size for I/O operations in the VFS.
 *
 * @param __Val__ Block size in bytes
 * @return 0 on success
 */
int
VfsSetIoBlockSize(long __Val__)
{
    AcquireMutex(&VfsLock);
    __IoBlockSize__ = __Val__;
    ReleaseMutex(&VfsLock);
    return 0;
}

/**
 * @brief Get the I/O block size
 *
 * Returns the preferred I/O block size for filesystem operations.
 *
 * @return I/O block size in bytes
 */
long
VfsGetIoBlockSize(void)
{
    AcquireMutex(&VfsLock);
    ReleaseMutex(&VfsLock);
    return __IoBlockSize__;
}