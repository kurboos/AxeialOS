#include <AllTypes.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <String.h>
#include <VFS.h>

/*extremely unix-like VFS*/

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
        return Nothing;
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
        return Error_TO_Pointer(-BadAlloc);
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
        return Error_TO_Pointer(-NotCanonical);
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
            return Error_TO_Pointer(-NoOperations);
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Comp);
        if (!Next)
        {
            return Error_TO_Pointer(-CannotLookup);
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            return Error_TO_Pointer(-BadAlloc);
        }
        memcpy(Dup, Comp, (size_t)(N + 1));
        Dentry* De = __alloc_dentry__(Dup, Parent, Next);
        if (!De)
        {
            return Error_TO_Pointer(-BadAlloc);
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
    return Best >= Nothing ? &__Mounts__[Best] : Nothing;
}

int
VfsInit(void)
{
    SysErr  err;
    SysErr* Error = &err;

    AcquireMutex(&VfsLock, Error);
    InitializeMutex(&VfsLock, "vfs-central", Error);

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

    PDebug("Init\n");
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsShutdown(void)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    for (long I = 0; I < __MountCount__; I++)
    {
        Superblock* Sb = __Mounts__[I].Sb;
        if (Sb && Sb->Ops && Sb->Ops->Umount)
        {
            Sb->Ops->Umount(Sb);
        }
        if (Sb && Sb->Ops && Sb->Ops->Release)
        {
            SysErr  err;
            SysErr* Error = &err;
            Sb->Ops->Release(Sb, Error);
        }
        __Mounts__[I].Sb      = 0;
        __Mounts__[I].Path[0] = 0;
    }
    __MountCount__ = 0;
    __FsCount__    = 0;
    __RootNode__   = 0;
    __RootDe__     = 0;

    PDebug("Shutdown\n");

    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsRegisterFs(const FsType* __FsType__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    if (!__FsType__ || !__FsType__->Name || !__FsType__->Mount)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (__FsCount__ >= __MaxFsTypes__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooMany;
    }

    for (long I = 0; I < __FsCount__; I++)
    {
        if (strcmp(__FsReg__[I]->Name, __FsType__->Name) == 0)
        {
            PWarn("FileSystem exists %s\n", __FsType__->Name);
            ReleaseMutex(&VfsLock, Error);
            return -Redefined;
        }
    }

    __FsReg__[__FsCount__++] = __FsType__;
    PDebug("FileSystem registered %s\n", __FsType__->Name);
    ReleaseMutex(&VfsLock, Error);

    return SysOkay;
}

int
VfsUnregisterFs(const char* __Name__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    if (!__Name__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
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
            PDebug("FileSystem unregistered %s\n", __Name__);
            return SysOkay;
        }
    }

    PError("FileSystem not found %s\n", __Name__);
    ReleaseMutex(&VfsLock, Error);
    return -NoSuch;
}

const FsType*
VfsFindFs(const char* __Name__)
{
    if (!__Name__)
    {
        return Error_TO_Pointer(-BadArgs);
    }

    for (long I = 0; I < __FsCount__; I++)
    {
        if (strcmp(__FsReg__[I]->Name, __Name__) == 0)
        {
            return __FsReg__[I];
        }
    }

    return Error_TO_Pointer(-NoSuch);
}

long
VfsListFs(const char** __Out__, long __Cap__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Out__ || __Cap__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    long N = (__FsCount__ < __Cap__) ? __FsCount__ : __Cap__;
    for (long I = 0; I < N; I++)
    {
        __Out__[I] = __FsReg__[I]->Name;
    }

    ReleaseMutex(&VfsLock, Error);
    return N;
}

Superblock*
VfsMount(const char*    __Dev__,
         const char*    __Path__,
         const char*    __Type__,
         long __Flags__ _unused,
         const char*    __Opts__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    const FsType* Fs = VfsFindFs(__Type__);
    if (!Fs)
    {
        return Error_TO_Pointer(-BadEntity);
    }

    if (!__Path__ || !*__Path__)
    {
        return Error_TO_Pointer(-NotCanonical);
    }

    long Plen = (long)strlen(__Path__);
    if (Plen <= 0 || Plen >= __MaxPath__)
    {
        return Error_TO_Pointer(-Limits);
    }

    if (__MountCount__ >= __MaxMounts__)
    {
        return Error_TO_Pointer(-TooMany);
    }

    Superblock* Sb = Fs->Mount(__Dev__, __Opts__);
    if (!Sb || !Sb->Root)
    {
        return Error_TO_Pointer(-NotRooted);
    }

    __MountEntry__* M = &__Mounts__[__MountCount__++];
    M->Sb             = Sb;
    memcpy(M->Path, __Path__, (size_t)(Plen + 1));

    if (!__RootNode__ && strcmp(__Path__, "/") == 0)
    {
        __RootNode__ = Sb->Root;
        __RootDe__   = __alloc_dentry__("/", 0, __RootNode__);
        PDebug("Root mounted /\n");
    }

    PDebug("Mounted %s at %s\n", __Type__, __Path__);

    ReleaseMutex(&VfsLock, Error);
    return Sb;
}

int
VfsUnmount(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
                SysErr  err;
                SysErr* Error = &err;
                Sb->Ops->Release(Sb, Error);
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
            PDebug("Unmounted %s\n", __Path__);
            return SysOkay;
        }
    }

    ReleaseMutex(&VfsLock, Error);
    return -NoSuch;
}

int /*chroot???*/
VfsSwitchRoot(const char* __NewRoot__)
{
    SysErr  err;
    SysErr* Error = &err;

    AcquireMutex(&VfsLock, Error);
    if (!__NewRoot__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotRooted;
    }

    Dentry* De = VfsResolve(__NewRoot__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -CannotLookup;
    }

    __RootNode__ = De->Node;
    __RootDe__   = De;
    PDebug("Chrooted to %s\n", __NewRoot__);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsBindMount(const char* __Src__, const char* __Dst__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Src__ || !__Dst__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    __MountEntry__* M = __find_mount__(__Src__);
    if (!M || !M->Sb)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoSuch;
    }

    if (__MountCount__ >= __MaxMounts__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooMany;
    }

    long N = (long)strlen(__Dst__);
    if (N <= 0 || N >= __MaxPath__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }

    __MountEntry__* New = &__Mounts__[__MountCount__++];
    New->Sb             = M->Sb;
    memcpy(New->Path, __Dst__, (size_t)(N + 1));

    PDebug("Bind mount %s -> %s\n", __Src__, __Dst__);
    ReleaseMutex(&VfsLock, Error);

    return SysOkay;
}

int
VfsMoveMount(const char* __Src__, const char* __Dst__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Src__ || !__Dst__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    __MountEntry__* M = __find_mount__(__Src__);
    if (!M || !M->Sb)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoSuch;
    }

    long N = (long)strlen(__Dst__);
    if (N <= 0 || N >= __MaxPath__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }

    memcpy(M->Path, __Dst__, (size_t)(N + 1));
    PDebug("Move mount %s -> %s\n", __Src__, __Dst__);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsRemount(const char* __Path__, long __Flags__ _unused, const char* __Opts__ _unused)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    __MountEntry__* M = __find_mount__(__Path__);
    if (!M || !M->Sb)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoSuch;
    }

    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

Dentry*
VfsResolve(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;

    AcquireMutex(&VfsLock, Error);
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-NotCanonical);
    }

    if (!__RootNode__)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-NotRooted);
    }

    if (strcmp(__Path__, "/") == 0)
    {
        return __RootDe__;
    }

    /*explaining this section*/

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
        return De ? De : Error_TO_Pointer(-NoSuch);
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
        return De ? De : Error_TO_Pointer(-BadAlloc);
    }

    ReleaseMutex(&VfsLock, Error);
    return __walk__(M->Sb->Root, __RootDe__, Tail);
}

Dentry*
VfsResolveAt(Dentry* __Base__, const char* __Rel__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    if (!__Base__ || !__Base__->Node || !__Rel__)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-Dangling);
    }

    if (!*__Rel__)
    {
        return __Base__;
    }

    if (__is_sep__(*__Rel__))
    {
        return VfsResolve(__Rel__);
    }

    ReleaseMutex(&VfsLock, Error);
    return __walk__(__Base__->Node, __Base__, __Rel__);
}

Vnode*
VfsLookup(Dentry* __Base__, const char* __Name__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Base__ || !__Base__->Node || !__Name__)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-BadArgs);
    }

    if (!__Base__->Node->Ops || !__Base__->Node->Ops->Lookup)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-NoOperations);
    }

    ReleaseMutex(&VfsLock, Error);
    return __Base__->Node->Ops->Lookup(__Base__->Node, __Name__);
}

int
VfsMkpath(const char* __Path__, long __Perm__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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

        Vnode* Next = Cur->Ops ? Cur->Ops->Lookup(Cur, Comp) : Nothing;
        if (!Next)
        {
            if (!Cur->Ops || !Cur->Ops->Mkdir)
            {
                ReleaseMutex(&VfsLock, Error);
                return -NoOperations;
            }
            VfsPerm perm = {.Mode = __Perm__, .Uid = 0, .Gid = 0};
            if (Cur->Ops->Mkdir(Cur, Comp, perm) != 0)
            {
                ReleaseMutex(&VfsLock, Error);
                return -ErrReturn;
            }
            Next = Cur->Ops->Lookup(Cur, Comp);
            if (!Next)
            {
                ReleaseMutex(&VfsLock, Error);
                return -CannotLookup;
            }
        }
        char* Dup = (char*)KMalloc(N + 1);
        memcpy(Dup, Comp, N + 1);
        De  = __alloc_dentry__(Dup, De, Next);
        Cur = Next;
    }
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsRealpath(const char* __Path__, char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__ || !__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    long L = (long)strlen(__Path__);
    if (L >= __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Buf__, __Path__, (size_t)(L + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

File*
VfsOpen(const char* __Path__, long __Flags__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-BadEntity);
    }

    if (!De->Node->Ops || !De->Node->Ops->Open)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-NoOperations);
    }

    File* F = (File*)KMalloc(sizeof(File));
    if (!F)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-BadAlloc);
    }

    F->Node   = De->Node;
    F->Offset = 0;
    F->Flags  = __Flags__;
    F->Refcnt = 1;
    F->Priv   = 0;

    if (De->Node->Ops->Open(De->Node, F) != SysOkay)
    {
        KFree(F, Error);
        return Error_TO_Pointer(-ErrReturn);
    }

    PDebug("Open %s\n", __Path__);
    ReleaseMutex(&VfsLock, Error);
    return F;
}

File*
VfsOpenAt(Dentry* __Base__, const char* __Rel__, long __Flags__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolveAt(__Base__, __Rel__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-BadEntity);
    }

    if (!De->Node->Ops || !De->Node->Ops->Open)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-NoOperations);
    }

    File* F = (File*)KMalloc(sizeof(File));
    if (!F)
    {
        ReleaseMutex(&VfsLock, Error);
        return Error_TO_Pointer(-BadAlloc);
    }

    F->Node   = De->Node;
    F->Offset = 0;
    F->Flags  = __Flags__;
    F->Refcnt = 1;
    F->Priv   = 0;

    if (De->Node->Ops->Open(De->Node, F) != SysOkay)
    {
        KFree(F, Error);
        return Error_TO_Pointer(-ErrReturn);
    }

    ReleaseMutex(&VfsLock, Error);
    return F;
}

int
VfsClose(File* __File__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    if (!__File__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (__File__->Node && __File__->Node->Ops && __File__->Node->Ops->Close)
    {
        __File__->Node->Ops->Close(__File__);
    }

    KFree(__File__, Error);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsRead(File* __File__, void* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Read)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    long Got = __File__->Node->Ops->Read(__File__, __Buf__, __Len__);
    if (Got > 0)
    {
        __File__->Offset += Got;
    }
    ReleaseMutex(&VfsLock, Error);
    return Got;
}

long
VfsWrite(File* __File__, const void* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Write)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    long Put = __File__->Node->Ops->Write(__File__, __Buf__, __Len__);
    if (Put > 0)
    {
        __File__->Offset += Put;
    }
    ReleaseMutex(&VfsLock, Error);
    return Put;
}

long
VfsLseek(File* __File__, long __Off__, int __Whence__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__File__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Lseek)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    long New = __File__->Node->Ops->Lseek(__File__, __Off__, __Whence__);
    if (New >= 0)
    {
        __File__->Offset = New;
    }
    ReleaseMutex(&VfsLock, Error);
    return New;
}

int
VfsIoctl(File* __File__, unsigned long __Cmd__, void* __Arg__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__File__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Ioctl)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    ReleaseMutex(&VfsLock, Error);
    return __File__->Node->Ops->Ioctl(__File__, __Cmd__, __Arg__);
}

int
VfsFsync(File* __File__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__File__ || !__File__->Node || !__File__->Node->Ops)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (!__File__->Node->Ops->Sync)
    {
        return SysOkay;
    }

    ReleaseMutex(&VfsLock, Error);
    return __File__->Node->Ops->Sync(__File__->Node);
}

int
VfsFstats(File* __File__, VfsStat* __Buf__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__File__ || !__Buf__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (!__File__->Node || !__File__->Node->Ops || !__File__->Node->Ops->Stat)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    ReleaseMutex(&VfsLock, Error);
    return __File__->Node->Ops->Stat(__File__->Node, __Buf__);
}

int
VfsStats(const char* __Path__, VfsStat* __Buf__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__ || !__Buf__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }

    if (!De->Node->Ops || !De->Node->Ops->Stat)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    ReleaseMutex(&VfsLock, Error);
    return De->Node->Ops->Stat(De->Node, __Buf__);
}

long
VfsReaddir(const char* __Path__, void* __Buf__, long __BufLen__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    if (!__Path__ || !__Buf__ || __BufLen__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }

    if (!De->Node->Ops || !De->Node->Ops->Readdir)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    ReleaseMutex(&VfsLock, Error);
    return De->Node->Ops->Readdir(De->Node, __Buf__, __BufLen__);
}

long
VfsReaddirF(File* __Dir__, void* __Buf__, long __BufLen__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Dir__ || !__Buf__ || __BufLen__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    if (!__Dir__->Node || !__Dir__->Node->Ops || !__Dir__->Node->Ops->Readdir)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    ReleaseMutex(&VfsLock, Error);
    return __Dir__->Node->Ops->Readdir(__Dir__->Node, __Buf__, __BufLen__);
}

int
VfsCreate(const char* __Path__, long __Flags__, VfsPerm __Perm__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* Parent = 0;
    char    Name[256];
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        Cur = Next;
    }
    if (!Parent || !Parent->Node || !Parent->Node->Ops || !Parent->Node->Ops->Create)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return Parent->Node->Ops->Create(Parent->Node, Name, __Flags__, __Perm__);
}

int
VfsUnlink(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* Base = 0;
    char    Name[256];
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Unlink)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return Base->Node->Ops->Unlink(Base->Node, Name);
}

int
VfsMkdir(const char* __Path__, VfsPerm __Perm__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* Base = 0;
    char    Name[256];
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Mkdir)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return Base->Node->Ops->Mkdir(Base->Node, Name, __Perm__);
}

int
VfsRmdir(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* Base = 0;
    char    Name[256];
    if (!__Path__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc(N + 1);
        memcpy(Dup, Name, N + 1);
        De  = __alloc_dentry__(Dup, De, Next);
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Rmdir)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return Base->Node->Ops->Rmdir(Base->Node, Name);
}

int
VfsSymlink(const char* __Target__, const char* __LinkPath__, VfsPerm __Perm__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* Base = 0;
    char    Name[256];
    if (!__LinkPath__ || !__Target__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        Cur = Next;
    }
    if (!Base || !Base->Node || !Base->Node->Ops || !Base->Node->Ops->Symlink)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return Base->Node->Ops->Symlink(Base->Node, Name, __Target__, __Perm__);
}

int
VfsReadlink(const char* __Path__, char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__ || !__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }
    if (!De->Node->Ops || !De->Node->Ops->Readlink)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }

    VfsNameBuf NB;
    NB.Buf = __Buf__;
    NB.Len = __Len__;
    ReleaseMutex(&VfsLock, Error);
    return De->Node->Ops->Readlink(De->Node, &NB);
}

int
VfsLink(const char* __OldPath__, const char* __NewPath__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__OldPath__ || !__NewPath__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
    }

    Dentry* OldDe   = VfsResolve(__OldPath__);
    Dentry* NewBase = 0;
    char    Name[256];

    if (!OldDe || !OldDe->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = Cur->Ops->Lookup(Cur, Name);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, Name, (size_t)(N + 1));
        De = __alloc_dentry__(Dup, De, Next);
        if (!De)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        Cur = Next;
    }

    if (!NewBase || !NewBase->Node || !NewBase->Node->Ops || !NewBase->Node->Ops->Link)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return NewBase->Node->Ops->Link(NewBase->Node, OldDe->Node, Name);
}

int
VfsRename(const char* __OldPath__, const char* __NewPath__, long __Flags__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* OldBase = 0;
    Dentry* NewBase = 0;
    char    OldName[256];
    char    NewName[256];

    if (!__OldPath__ || !__NewPath__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = CurO->Ops->Lookup(CurO, OldName);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, OldName, (size_t)(N + 1));
        DeO = __alloc_dentry__(Dup, DeO, Next);
        if (!DeO)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoOperations;
        }
        Vnode* Next = CurN->Ops->Lookup(CurN, NewName);
        if (!Next)
        {
            ReleaseMutex(&VfsLock, Error);
            return -CannotLookup;
        }
        char* Dup = (char*)KMalloc((size_t)(N + 1));
        if (!Dup)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        memcpy(Dup, NewName, (size_t)(N + 1));
        DeN = __alloc_dentry__(Dup, DeN, Next);
        if (!DeN)
        {
            ReleaseMutex(&VfsLock, Error);
            return -BadAlloc;
        }
        CurN = Next;
    }

    if (!OldBase || !NewBase)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }
    if (!OldBase->Node || !NewBase->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }
    if (!OldBase->Node->Ops || !OldBase->Node->Ops->Rename)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return OldBase->Node->Ops->Rename(OldBase->Node, OldName, NewBase->Node, NewName, __Flags__);
}

int
VfsChmod(const char* __Path__, long __Mode__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }
    if (!De->Node->Ops || !De->Node->Ops->Chmod)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return De->Node->Ops->Chmod(De->Node, __Mode__);
}

int
VfsChown(const char* __Path__, long __Uid__, long __Gid__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }
    if (!De->Node->Ops || !De->Node->Ops->Chown)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return De->Node->Ops->Chown(De->Node, __Uid__, __Gid__);
}

int
VfsTruncate(const char* __Path__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    if (!De || !De->Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -Dangling;
    }
    if (!De->Node->Ops || !De->Node->Ops->Truncate)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return De->Node->Ops->Truncate(De->Node, __Len__);
}

int
VnodeRefInc(Vnode* __Node__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Node__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    __Node__->Refcnt++;
    ReleaseMutex(&VfsLock, Error);
    return (int)__Node__->Refcnt;
}

int
VnodeRefDec(Vnode* __Node__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Node__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    if (__Node__->Refcnt > 0)
    {
        __Node__->Refcnt--;
    }
    ReleaseMutex(&VfsLock, Error);
    return (int)__Node__->Refcnt;
}

int
VnodeGetAttr(Vnode* __Node__, VfsStat* __Buf__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Node__ || !__Buf__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    if (!__Node__->Ops || !__Node__->Ops->Stat)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NoOperations;
    }
    ReleaseMutex(&VfsLock, Error);
    return __Node__->Ops->Stat(__Node__, __Buf__);
}

int
VnodeSetAttr(Vnode* __Node__ _unused, const VfsStat* __Buf__ _unused)
{
    return -Impilict;
}

int
DentryInvalidate(Dentry* __De__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__De__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    __De__->Flags |= 1;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
DentryRevalidate(Dentry* __De__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__De__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    __De__->Flags &= ~1;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
DentryAttach(Dentry* __De__, Vnode* __Node__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__De__ || !__Node__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    __De__->Node = __Node__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
DentryDetach(Dentry* __De__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__De__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    __De__->Node = 0;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
DentryName(Dentry* __De__, char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__De__ || !__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    long N = (long)strlen(__De__->Name);
    if (N >= __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Buf__, __De__->Name, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsSetCwd(const char* __Path__ _unused)
{
    return SysOkay;
}

int
VfsGetCwd(char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    const char* __Path = "/";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsSetRoot(const char* __Path__)
{
    return VfsSwitchRoot(__Path__);
}

int
VfsGetRoot(char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    const char* __Path = "/";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsSetUmask(long __Mode__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    __Umask__ = __Mode__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsGetUmask(void)
{
    return __Umask__;
}

int
VfsNotifySubscribe(const char* __Path__ _unused, long __Mask__ _unused)
{
    return SysOkay;
}

int
VfsNotifyUnsubscribe(const char* __Path__ _unused)
{
    return SysOkay;
}

int
VfsNotifyPoll(const char* __Path__ _unused, long* __OutMask__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__OutMask__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }
    *__OutMask__ = 0;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsAccess(const char* __Path__, long __Mode__ _unused)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock, Error);
    return De ? Nothing : -Dangling;
}

int
VfsExists(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock, Error);
    return De ? SysOkay : -NoSuch;
}

int
VfsIsDir(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock, Error);
    return (De && De->Node && De->Node->Type == VNodeDIR) ? SysOkay : -NoSuch;
}

int
VfsIsFile(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock, Error);
    return (De && De->Node && De->Node->Type == VNodeFILE) ? SysOkay : -NoSuch;
}

int
VfsIsSymlink(const char* __Path__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    Dentry* De = VfsResolve(__Path__);
    ReleaseMutex(&VfsLock, Error);
    return (De && De->Node && De->Node->Type == VNodeSYM) ? SysOkay : -NoSuch;
}

int
VfsCopy(const char* __Src__, const char* __Dst__, long __Flags__ _unused)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);

    File* S = VfsOpen(__Src__, VFlgRDONLY);
    if (!S)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }
    File* D = VfsOpen(__Dst__, VFlgCREATE | VFlgWRONLY | VFlgTRUNC);
    if (!D)
    {
        VfsClose(S);
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }

    char Buf[4096];
    for (;;)
    {
        long r = VfsRead(S, Buf, sizeof(Buf));
        if (r < 0)
        {
            VfsClose(S);
            VfsClose(D);
            ReleaseMutex(&VfsLock, Error);
            return -NoRead;
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
            ReleaseMutex(&VfsLock, Error);
            return -NoWrite;
        }
    }

    VfsClose(S);
    VfsClose(D);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsMove(const char* __Src__, const char* __Dst__, long __Flags__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    int rc = VfsRename(__Src__, __Dst__, __Flags__);
    if (rc == SysOkay)
    {
        return SysOkay;
    }
    rc = VfsCopy(__Src__, __Dst__, __Flags__);
    if (rc != SysOkay)
    {
        ReleaseMutex(&VfsLock, Error);
        return -ErrReturn;
    }
    ReleaseMutex(&VfsLock, Error);
    return VfsUnlink(__Src__);
}

int
VfsReadAll(const char* __Path__, void* __Buf__, long __BufLen__, long* __OutLen__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    File* F = VfsOpen(__Path__, VFlgRDONLY);
    if (!F)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }
    long total = 0;
    while (total < __BufLen__)
    {
        long r = VfsRead(F, (char*)__Buf__ + total, __BufLen__ - total);
        if (r < 0)
        {
            VfsClose(F);
            ReleaseMutex(&VfsLock, Error);
            return -NoRead;
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
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsWriteAll(const char* __Path__, const void* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    File* F = VfsOpen(__Path__, VFlgCREATE | VFlgWRONLY | VFlgTRUNC);
    if (!F)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadEntity;
    }
    long total = 0;
    while (total < __Len__)
    {
        long w = VfsWrite(F, (const char*)__Buf__ + total, __Len__ - total);
        if (w <= 0)
        {
            VfsClose(F);
            ReleaseMutex(&VfsLock, Error);
            return -NoRead;
        }
        total += w;
    }
    VfsClose(F);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsMountTableEnumerate(char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
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
        memcpy(__Buf__ + off, __Path, (size_t)N);
        off += N;
        __Buf__[off++] = '\n';
    }
    if (off < __Len__)
    {
        __Buf__[off] = 0;
    }
    ReleaseMutex(&VfsLock, Error);
    return (int)off;
}

int
VfsMountTableFind(const char* __Path__, char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__ || !__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    for (long I = 0; I < __MountCount__; I++)
    {
        if (strcmp(__Mounts__[I].Path, __Path__) == 0)
        {
            long N = (long)strlen(__Mounts__[I].Path);
            if (N >= __Len__)
            {
                ReleaseMutex(&VfsLock, Error);
                return -TooBig;
            }
            memcpy(__Buf__, __Mounts__[I].Path, (size_t)(N + 1));
            return SysOkay;
        }
    }
    ReleaseMutex(&VfsLock, Error);
    return -NoSuch;
}

int
VfsNodePath(Vnode* __Node__ _unused, char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    const char* __Path = "/";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsNodeName(Vnode* __Node__ _unused, char* __Buf__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Buf__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    const char* __Path = "";
    long        N      = (long)strlen(__Path);
    if (N >= __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Buf__, __Path, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsAllocName(char** __Out__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Out__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    *__Out__ = (char*)KMalloc((size_t)__Len__);
    ReleaseMutex(&VfsLock, Error);
    return *__Out__ ? Nothing : -NotCanonical;
}

int
VfsFreeName(char* __Name__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Name__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    KFree(__Name__, Error);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsJoinPath(const char* __A__, const char* __B__, char* __Out__, long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__A__ || !__B__ || !__Out__ || __Len__ <= 0)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    long la   = (long)strlen(__A__);
    long lb   = (long)strlen(__B__);
    long need = la + 1 + lb + 1;
    if (need > __Len__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__Out__, __A__, (size_t)la);
    __Out__[la] = '/';
    memcpy(__Out__ + la + 1, __B__, (size_t)lb);
    __Out__[la + 1 + lb] = 0;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsSetFlag(const char* __Path__ _unused, long __Flag__ _unused)
{
    return SysOkay;
}

int
VfsClearFlag(const char* __Path__ _unused, long __Flag__ _unused)
{
    return SysOkay;
}

long
VfsGetFlags(const char* __Path__ _unused)
{
    return SysOkay;
}

int
VfsSyncAll(void)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    for (long I = 0; I < __MountCount__; I++)
    {
        Superblock* Sb = __Mounts__[I].Sb;
        if (Sb && Sb->Ops && Sb->Ops->Sync)
        {
            Sb->Ops->Sync(Sb);
        }
    }
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsPruneCaches(void)
{
    return SysOkay;
}

int
VfsRegisterDevNode(const char* __Path__, void* __Priv__, long __Flags__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__ || !__Priv__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }

    /* Ensure parent directory exists */
    char Buf[1024];
    VfsRealpath(__Path__, Buf, sizeof(Buf));
    const char* Name = strrchr(Buf, '/');
    if (!Name)
    {
        ReleaseMutex(&VfsLock, Error);
        return -NotCanonical;
    }
    long nlen = (long)strlen(Name + 1);

    char Parent[1024];
    long plen = (long)(Name - Buf);
    memcpy(Parent, Buf, plen);
    Parent[plen] = 0;
    VfsMkpath(Parent, 0);

    /* Create vnode for device */
    Vnode* Node = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Node)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadAlloc;
    }
    Node->Type   = VNodeDEV;
    Node->Ops    = (VnodeOps*)__Priv__; /* device ops table */
    Node->Sb     = __RootNode__->Sb;
    Node->Priv   = __Priv__;
    Node->Refcnt = 1;

    char* Dup = (char*)KMalloc(nlen + 1);
    memcpy(Dup, Name + 1, nlen + 1);
    Dentry* De = __alloc_dentry__(Dup, __RootDe__, Node);
    if (!De)
    {
        KFree(Node, Error);
        ReleaseMutex(&VfsLock, Error);
        return -BadAlloc;
    }

    PDebug("Registered devnode %s\n", __Path__);
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsUnregisterDevNode(const char* __Path__ _unused)
{
    return SysOkay;
}

int
VfsRegisterPseudoFs(const char* __Path__, Superblock* __Sb__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Path__ || !__Sb__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    if (__MountCount__ >= __MaxMounts__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooMany;
    }
    long            N = (long)strlen(__Path__);
    __MountEntry__* M = &__Mounts__[__MountCount__++];
    M->Sb             = __Sb__;
    memcpy(M->Path, __Path__, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

int
VfsUnregisterPseudoFs(const char* __Path__)
{
    return VfsUnmount(__Path__);
}

int
VfsSetDefaultFs(const char* __Name__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (!__Name__)
    {
        ReleaseMutex(&VfsLock, Error);
        return -BadArgs;
    }
    long N = (long)strlen(__Name__);
    if (N >= (long)sizeof(__DefaultFs__))
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooBig;
    }
    memcpy(__DefaultFs__, __Name__, (size_t)(N + 1));
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

const char*
VfsGetDefaultFs(void)
{
    return __DefaultFs__;
}

int
VfsSetMaxName(long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (__Len__ < 1)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooSmall;
    }
    __MaxName__ = __Len__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsGetMaxName(void)
{
    return __MaxName__;
}

int
VfsSetMaxPath(long __Len__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    if (__Len__ < 1)
    {
        ReleaseMutex(&VfsLock, Error);
        return -TooSmall;
    }
    __MaxPath__ = __Len__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsGetMaxPath(void)
{
    return __MaxPath__;
}

int
VfsSetDirCacheLimit(long __Val__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    __DirCacheLimit__ = __Val__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsGetDirCacheLimit(void)
{
    return __DirCacheLimit__;
}

int
VfsSetFileCacheLimit(long __Val__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    __FileCacheLimit__ = __Val__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsGetFileCacheLimit(void)
{
    return __FileCacheLimit__;
}

int
VfsSetIoBlockSize(long __Val__)
{
    SysErr  err;
    SysErr* Error = &err;
    AcquireMutex(&VfsLock, Error);
    __IoBlockSize__ = __Val__;
    ReleaseMutex(&VfsLock, Error);
    return SysOkay;
}

long
VfsGetIoBlockSize(void)
{
    return __IoBlockSize__;
}