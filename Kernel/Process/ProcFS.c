#include <AllTypes.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <ProcFS.h>
#include <Process.h>
#include <SMP.h>
#include <String.h>
#include <Sync.h>
#include <VFS.h>

/** @section Statics */
static ProcFsContext __ProcFsCtx__  = {0};
static SpinLock      __ProcFsLock__ = {0};

static int ProcFsMkdir(Vnode* __Parent__, const char* __Name__, VfsPerm __Perm__);
static int ProcFsRmdir(Vnode* __Parent__, const char* __Name__);
static int ProcFsCreate(Vnode* __Parent__, const char* __Name__, long __Flags__, VfsPerm __Perm__);
static int ProcFsUnlink(Vnode* __Parent__, const char* __Name__);
static Vnode* ProcFsLookup(Vnode* __Parent__, const char* __Name__);
static long   ProcFsReaddir(Vnode* __Dir__, void* __Buf__, long __Len__);
static long   ProcFsFileRead(File* __File__, void* __Buf__, long __Len__);
static long   ProcFsFileWrite(File* __File__, const void* __Buf__, long __Len__);
static int    ProcFsStat(Vnode* __Node__, VfsStat* __Stat__);
static int    ProcFsOpen(Vnode* __Node__, File* __File__);
static int    ProcFsClose(File* __File__);

/**
 * @brief ProcFS ops table: required by VFS.
 */
static const VnodeOps __ProcFsOps__ = {.Open     = ProcFsOpen,
                                       .Close    = ProcFsClose,
                                       .Read     = ProcFsFileRead,
                                       .Write    = ProcFsFileWrite,
                                       .Lseek    = 0,
                                       .Ioctl    = 0,
                                       .Stat     = ProcFsStat,
                                       .Readdir  = ProcFsReaddir,
                                       .Lookup   = ProcFsLookup,
                                       .Create   = ProcFsCreate,
                                       .Unlink   = ProcFsUnlink,
                                       .Mkdir    = ProcFsMkdir,
                                       .Rmdir    = ProcFsRmdir,
                                       .Symlink  = 0,
                                       .Readlink = 0,
                                       .Link     = 0,
                                       .Rename   = 0,
                                       .Chmod    = 0,
                                       .Chown    = 0,
                                       .Truncate = 0,
                                       .Sync     = 0,
                                       .Map      = 0,
                                       .Unmap    = 0};

/**
 * @brief Allocate a vnode bound to ProcFS, with ProcFsNode payload.
 */
Vnode*
ProcFsAllocNode(
    Superblock* __Sb__, VnodeType __Type__, ProcFsEntryType __Entry__, long __Pid__, long __Fd__)
{
    Vnode* Node = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Node)
    {
        return 0;
    }

    ProcFsNode* Priv = (ProcFsNode*)KMalloc(sizeof(ProcFsNode));
    if (!Priv)
    {
        KFree(Node);
        return 0;
    }

    Priv->Kind  = (__Type__ == VNodeDIR) ? PNodeDir : PNodeFile;
    Priv->Entry = __Entry__;
    Priv->Pid   = __Pid__;
    Priv->Fd    = __Fd__;

    extern const VnodeOps __ProcFsOps__;
    Node->Type   = __Type__;
    Node->Ops    = &__ProcFsOps__;
    Node->Sb     = __Sb__;
    Node->Priv   = Priv;
    Node->Refcnt = 1;
    return Node;
}

/**
 * @brief Free a vnode and its ProcFsNode payload.
 */
void
ProcFsFreeNode(Vnode* __Node__)
{
    if (!__Node__)
    {
        return;
    }
    if (__Node__->Priv)
    {
        KFree(__Node__->Priv);
    }
    KFree(__Node__);
}

/**
 * @brief Ensure capacity for directory children.
 */
static int
__procfs_dir_reserve__(ProcFsDirPriv* Dir, long Need)
{
    PDebug("ProcFS: reserve dir=%p need=%ld cap=%ld count=%ld\n",
           (void*)Dir,
           Need,
           Dir ? Dir->Cap : -1,
           Dir ? Dir->Count : -1);
    if (!Dir)
    {
        return -1;
    }
    if (Dir->Cap >= Need)
    {
        PDebug("ProcFS: reserve skip (enough cap)\n");
        return 0;
    }
    long NewCap = (Dir->Cap == 0) ? 8 : Dir->Cap * 2;
    while (NewCap < Need)
    {
        NewCap *= 2;
    }
    PDebug("ProcFS: reserve realloc newcap=%ld\n", NewCap);

    ProcFsChild* NewArr = (ProcFsChild*)KMalloc(sizeof(ProcFsChild) * (size_t)NewCap);
    if (!NewArr)
    {
        PError("ProcFS: reserve alloc failed newcap=%ld\n", NewCap);
        return -1;
    }
    __builtin_memset(NewArr, 0, sizeof(ProcFsChild) * (size_t)NewCap);
    for (long I = 0; I < Dir->Count; I++)
    {
        NewArr[I] = Dir->Children[I];
    }
    if (Dir->Children)
    {
        KFree(Dir->Children);
    }
    Dir->Children = NewArr;
    Dir->Cap      = NewCap;
    PDebug("ProcFS: reserve ok cap=%ld\n", Dir->Cap);
    return 0;
}

/**
 * @brief Find child index by name within a directory.
 */
static long
__procfs_dir_find__(ProcFsDirPriv* Dir, const char* Name)
{
    PDebug("ProcFS: find dir=%p name='%s' count=%ld\n",
           (void*)Dir,
           Name ? Name : "(null)",
           Dir ? Dir->Count : -1);
    if (!Dir || !Name)
    {
        return -1;
    }
    for (long I = 0; I < Dir->Count; I++)
    {
        if (strcmp(Dir->Children[I].Name, Name) == 0)
        {
            PDebug("ProcFS: find hit idx=%ld\n", I);
            return I;
        }
    }
    PDebug("ProcFS: find miss\n");
    return -1;
}

/**
 * @brief Create a directory vnode with private context.
 */
Vnode*
__procfs_alloc_dir__(Superblock* Sb, long Pid, long IsFdDir)
{
    PDebug("ProcFS: alloc dir sb=%p pid=%ld isFd=%ld\n", (void*)Sb, Pid, IsFdDir);
    Vnode* Node = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Node)
    {
        PError("ProcFS: alloc dir vnode failed\n");
        return 0;
    }
    ProcFsDirPriv* Priv = (ProcFsDirPriv*)KMalloc(sizeof(ProcFsDirPriv));
    if (!Priv)
    {
        PError("ProcFS: alloc dir priv failed\n");
        KFree(Node);
        return 0;
    }

    __builtin_memset(Priv, 0, sizeof(*Priv));
    Priv->Pid     = Pid;
    Priv->IsFdDir = IsFdDir;

    Node->Type   = VNodeDIR;
    Node->Ops    = &__ProcFsOps__;
    Node->Sb     = Sb;
    Node->Priv   = Priv;
    Node->Refcnt = 1;
    PDebug("ProcFS: alloc dir ok node=%p priv=%p\n", (void*)Node, (void*)Priv);
    return Node;
}

/**
 * @brief Create a file vnode with private context.
 */
static Vnode*
__procfs_alloc_file__(Superblock* Sb, long Pid, long Fd, ProcFsEntryType Entry)
{
    PDebug("ProcFS: alloc file sb=%p pid=%ld fd=%ld entry=%d\n", (void*)Sb, Pid, Fd, (int)Entry);
    Vnode* Node = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Node)
    {
        PError("ProcFS: alloc file vnode failed\n");
        return 0;
    }
    ProcFsFilePriv* Priv = (ProcFsFilePriv*)KMalloc(sizeof(ProcFsFilePriv));
    if (!Priv)
    {
        PError("ProcFS: alloc file priv failed\n");
        KFree(Node);
        return 0;
    }

    Priv->Pid   = Pid;
    Priv->Fd    = Fd;
    Priv->Entry = Entry;

    Node->Type   = VNodeFILE;
    Node->Ops    = &__ProcFsOps__;
    Node->Sb     = Sb;
    Node->Priv   = Priv;
    Node->Refcnt = 1;
    PDebug("ProcFS: alloc file ok node=%p priv=%p\n", (void*)Node, (void*)Priv);
    return Node;
}

/**
 * @brief Attach a child into a directory's private children array.
 */
static int
__procfs_dir_attach__(ProcFsDirPriv* Dir, const char* Name, Vnode* Child, ProcFsEntryKind Kind)
{
    PDebug("ProcFS: attach dir=%p name='%s' child=%p kind=%d\n",
           (void*)Dir,
           Name ? Name : "(null)",
           (void*)Child,
           (int)Kind);
    if (!Dir || !Name || !Child)
    {
        return -1;
    }
    if (__procfs_dir_reserve__(Dir, Dir->Count + 1) != 0)
    {
        PError("ProcFS: attach reserve failed\n");
        return -1;
    }

    size_t N   = strlen(Name);
    char*  Dup = (char*)KMalloc(N + 1);
    if (!Dup)
    {
        PError("ProcFS: attach name dup alloc failed\n");
        return -1;
    }
    __builtin_memcpy(Dup, Name, N + 1);

    Dir->Children[Dir->Count].Name = Dup;
    Dir->Children[Dir->Count].Node = Child;
    Dir->Children[Dir->Count].Kind = Kind;
    Dir->Count++;
    PDebug("ProcFS: attach ok count=%ld\n", Dir->Count);
    return 0;
}

/**
 * @brief Detach and free a child by index.
 */
static int
__procfs_dir_detach_idx__(ProcFsDirPriv* Dir, long idx)
{
    PDebug("ProcFS: detach dir=%p idx=%ld count=%ld\n", (void*)Dir, idx, Dir ? Dir->Count : -1);
    if (!Dir || idx < 0 || idx >= Dir->Count)
    {
        return -1;
    }
    ProcFsChild C = Dir->Children[idx];
    if (C.Node)
    {
        if (C.Node->Priv)
        {
            if (C.Node->Type == VNodeDIR)
            {
                ProcFsDirPriv* d = (ProcFsDirPriv*)C.Node->Priv;
                PDebug("ProcFS: detach freeing nested dir priv=%p children=%ld\n",
                       (void*)d,
                       d ? d->Count : -1);
                for (long I = 0; I < d->Count; I++)
                {
                    if (d->Children[I].Name)
                    {
                        KFree(d->Children[I].Name);
                    }
                    if (d->Children[I].Node)
                    {
                        if (d->Children[I].Node->Priv)
                        {
                            KFree(d->Children[I].Node->Priv);
                        }
                        KFree(d->Children[I].Node);
                    }
                }
                if (d->Children)
                {
                    KFree(d->Children);
                }
            }
            KFree(C.Node->Priv);
        }
        KFree(C.Node);
    }
    if (C.Name)
    {
        KFree(C.Name);
    }

    for (long I = idx + 1; I < Dir->Count; I++)
    {
        Dir->Children[I - 1] = Dir->Children[I];
    }
    Dir->Count--;
    PDebug("ProcFS: detach ok newcount=%ld\n", Dir->Count);
    return 0;
}

/** @section Operations */

/**
 * @brief Create /proc/<pid> directory in-memory.
 */
static int
ProcFsMkdir(Vnode* __Parent__, const char* __Name__, VfsPerm __Perm__)
{
    PDebug("ProcFS: Mkdir parent=%p name='%s' mode=0x%lx\n",
           (void*)__Parent__,
           __Name__ ? __Name__ : "(null)",
           (unsigned long)__Perm__.Mode);
    (void)__Perm__;
    if (!__Parent__ || !__Name__)
    {
        return -1;
    }
    if (__Parent__->Type != VNodeDIR)
    {
        return -1;
    }

    ProcFsDirPriv* Root = (ProcFsDirPriv*)__Parent__->Priv;
    if (!Root)
    {
        return -1;
    } /* root must have dir priv */

    long Pid = 0;
    for (long I = 0; __Name__[I]; I++)
    {
        char Char = __Name__[I];
        if (Char < '0' || Char > '9')
        {
            PError("ProcFS: Mkdir non-numeric name '%s'\n", __Name__);
            return -1;
        }
        Pid = Pid * 10 + (Char - '0');
    }
    if (Pid <= 0)
    {
        PError("ProcFS: Mkdir invalid pid=%ld\n", Pid);
        return -1;
    }

    if (__procfs_dir_find__(Root, __Name__) >= 0)
    {
        PError("ProcFS: Mkdir duplicate '%s'\n", __Name__);
        return -1;
    }

    Vnode* PidDir = __procfs_alloc_dir__(__Parent__->Sb, Pid, 0);
    if (!PidDir)
    {
        return -1;
    }

    if (__procfs_dir_attach__(Root, __Name__, PidDir, PF_DIR) != 0)
    {
        ProcFsFreeNode(PidDir);
        PError("ProcFS: Mkdir attach pid dir failed\n");
        return -1;
    }

    ProcFsDirPriv* PidPriv = (ProcFsDirPriv*)PidDir->Priv;
    Vnode*         Stat    = __procfs_alloc_file__(PidDir->Sb, Pid, -1, PEntryStat);
    Vnode*         Status  = __procfs_alloc_file__(PidDir->Sb, Pid, -1, PEntryStatus);
    Vnode*         FdDir   = __procfs_alloc_dir__(PidDir->Sb, Pid, 1);

    if (!Stat || !Status || !FdDir)
    {
        PError("ProcFS: Mkdir std children alloc failed\n");
        return -1;
    }

    if (__procfs_dir_attach__(PidPriv, "stat", Stat, PF_FILE) != 0)
    {
        PError("ProcFS: Mkdir attach stat failed\n");
        return -1;
    }
    if (__procfs_dir_attach__(PidPriv, "status", Status, PF_FILE) != 0)
    {
        PError("ProcFS: Mkdir attach status failed\n");
        return -1;
    }
    if (__procfs_dir_attach__(PidPriv, "fd", FdDir, PF_DIR) != 0)
    {
        PError("ProcFS: Mkdir attach fd dir failed\n");
        return -1;
    }

    PDebug("ProcFS: Mkdir ok pid=%ld\n", Pid);
    return 0;
}

/**
 * @brief Remove /proc/<pid> directory in-memory.
 */
static int
ProcFsRmdir(Vnode* __Parent__, const char* __Name__)
{
    PDebug(
        "ProcFS: Rmdir parent=%p name='%s'\n", (void*)__Parent__, __Name__ ? __Name__ : "(null)");
    if (!__Parent__ || !__Name__)
    {
        return -1;
    }
    if (__Parent__->Type != VNodeDIR)
    {
        return -1;
    }

    ProcFsDirPriv* Root = (ProcFsDirPriv*)__Parent__->Priv;
    if (!Root)
    {
        return -1;
    }

    long idx = __procfs_dir_find__(Root, __Name__);
    if (idx < 0)
    {
        PError("ProcFS: Rmdir not found '%s'\n", __Name__);
        return -1;
    }

    int rc = __procfs_dir_detach_idx__(Root, idx);
    PDebug("ProcFS: Rmdir result=%d\n", rc);
    return rc;
}

/**
 * @brief Create file under /proc/<pid> or /proc/<pid>/fd (generate fd/<N>).
 */
static int
ProcFsCreate(Vnode* __Parent__, const char* __Name__, long __Flags__, VfsPerm __Perm__)
{
    PDebug("ProcFS: Create parent=%p name='%s' flags=0x%lx mode=0x%lx\n",
           (void*)__Parent__,
           __Name__ ? __Name__ : "(null)",
           (unsigned long)__Flags__,
           (unsigned long)__Perm__.Mode);
    (void)__Flags__;
    (void)__Perm__;
    if (!__Parent__ || !__Name__)
    {
        return -1;
    }

    if (__Parent__->Type != VNodeDIR)
    {
        return -1;
    }
    ProcFsDirPriv* Dir = (ProcFsDirPriv*)__Parent__->Priv;
    if (!Dir)
    {
        return -1;
    }

    if (!Dir->IsFdDir)
    {
        if (strcmp(__Name__, "stat") == 0 || strcmp(__Name__, "status") == 0)
        {
            if (__procfs_dir_find__(Dir, __Name__) >= 0)
            {
                PError("ProcFS: Create duplicate '%s'\n", __Name__);
                return -1;
            }
            Vnode* F =
                __procfs_alloc_file__(__Parent__->Sb,
                                      Dir->Pid,
                                      -1,
                                      strcmp(__Name__, "stat") == 0 ? PEntryStat : PEntryStatus);
            if (!F)
            {
                PError("ProcFS: Create file alloc failed '%s'\n", __Name__);
                return -1;
            }
            int rc = __procfs_dir_attach__(Dir, __Name__, F, PF_FILE);
            PDebug("ProcFS: Create attach '%s' rc=%d\n", __Name__, rc);
            return rc;
        }
        else if (strcmp(__Name__, "fd") == 0)
        {
            if (__procfs_dir_find__(Dir, "fd") >= 0)
            {
                PError("ProcFS: Create fd dir duplicate\n");
                return -1;
            }
            Vnode* FdDir = __procfs_alloc_dir__(__Parent__->Sb, Dir->Pid, 1);
            if (!FdDir)
            {
                PError("ProcFS: Create fd dir alloc failed\n");
                return -1;
            }
            int rc = __procfs_dir_attach__(Dir, "fd", FdDir, PF_DIR);
            PDebug("ProcFS: Create fd dir attach rc=%d\n", rc);
            return rc;
        }
        PError("ProcFS: Create invalid name '%s'\n", __Name__);
        return -1;
    }
    else
    {
        long Fd = 0;
        for (long I = 0; __Name__[I]; I++)
        {
            char Char = __Name__[I];
            if (Char < '0' || Char > '9')
            {
                PError("ProcFS: Create fd item non-numeric '%s'\n", __Name__);
                return -1;
            }
            Fd = Fd * 10 + (Char - '0');
        }
        if (Fd < 0)
        {
            PError("ProcFS: Create fd item invalid fd=%ld\n", Fd);
            return -1;
        }

        if (__procfs_dir_find__(Dir, __Name__) >= 0)
        {
            PError("ProcFS: Create fd item duplicate '%s'\n", __Name__);
            return -1;
        }
        Vnode* FdItem = __procfs_alloc_file__(__Parent__->Sb, Dir->Pid, Fd, PEntryFdItem);
        if (!FdItem)
        {
            PError("ProcFS: Create fd item alloc failed fd=%ld\n", Fd);
            return -1;
        }
        int rc = __procfs_dir_attach__(Dir, __Name__, FdItem, PF_FILE);
        PDebug("ProcFS: Create fd item attach name='%s' fd=%ld rc=%d\n", __Name__, Fd, rc);
        return rc;
    }
}

/**
 * @brief Unlink file under /proc/<pid> or /proc/<pid>/fd.
 */
static int
ProcFsUnlink(Vnode* __Parent__, const char* __Name__)
{
    PDebug(
        "ProcFS: Unlink parent=%p name='%s'\n", (void*)__Parent__, __Name__ ? __Name__ : "(null)");
    if (!__Parent__ || !__Name__)
    {
        return -1;
    }
    if (__Parent__->Type != VNodeDIR)
    {
        return -1;
    }

    ProcFsDirPriv* Dir = (ProcFsDirPriv*)__Parent__->Priv;
    if (!Dir)
    {
        return -1;
    }

    long idx = __procfs_dir_find__(Dir, __Name__);
    if (idx < 0)
    {
        PError("ProcFS: Unlink not found '%s'\n", __Name__);
        return -1;
    }
    int rc = __procfs_dir_detach_idx__(Dir, idx);
    PDebug("ProcFS: Unlink rc=%d\n", rc);
    return rc;
}

/**
 * @brief Lookup child by name (returns child vnode).
 */
static Vnode*
ProcFsLookup(Vnode* __Parent__, const char* __Name__)
{
    PDebug(
        "ProcFS: Lookup parent=%p name='%s'\n", (void*)__Parent__, __Name__ ? __Name__ : "(null)");
    if (!__Parent__ || !__Name__)
    {
        return 0;
    }
    if (__Parent__->Type != VNodeDIR)
    {
        return 0;
    }

    ProcFsDirPriv* Dir = (ProcFsDirPriv*)__Parent__->Priv;
    if (!Dir)
    {
        return 0;
    }

    long idx = __procfs_dir_find__(Dir, __Name__);
    if (idx < 0)
    {
        PDebug("ProcFS: Lookup miss '%s'\n", __Name__);
        return 0;
    }

    Vnode* Child = Dir->Children[idx].Node;
    PDebug("ProcFS: Lookup hit '%s' node=%p\n", __Name__, (void*)Child);
    return Child;
}

/**
 * @brief Readdir: serialize names line-by-line into buffer.
 */
static long
ProcFsReaddir(Vnode* __Dir__, void* __Buf__, long __Len__)
{
    if (!__Dir__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    if (__Dir__->Type != VNodeDIR)
    {
        return -1;
    }

    long Max = __Len__ / (long)sizeof(VfsDirEnt);
    if (Max <= 0)
    {
        return -1;
    }

    ProcFsDirPriv* Dir   = (ProcFsDirPriv*)__Dir__->Priv;
    VfsDirEnt*     DE    = (VfsDirEnt*)__Buf__;
    long           Wrote = 0;

    /* . */
    if (Wrote < Max)
    {
        DE[Wrote].Name[0] = '.';
        DE[Wrote].Name[1] = '\0';
        DE[Wrote].Type    = VNodeDIR;
        DE[Wrote].Ino     = (long)(uintptr_t)__Dir__;
        Wrote++;
    }
    /* .. */
    if (Wrote < Max)
    {
        DE[Wrote].Name[0] = '.';
        DE[Wrote].Name[1] = '.';
        DE[Wrote].Name[2] = '\0';
        DE[Wrote].Type    = VNodeDIR;
        DE[Wrote].Ino     = (long)(uintptr_t)__Dir__; /*Or parent if tracked*/
        Wrote++;
    }

    if (!Dir)
    {
        return Wrote * (long)sizeof(VfsDirEnt);
    }

    for (long I = 0; I < Dir->Count && Wrote < Max; I++)
    {
        const char* Nm = Dir->Children[I].Name;
        long        N  = 0;
        while (Nm && Nm[N] && N < 255)
        {
            DE[Wrote].Name[N] = Nm[N];
            N++;
        }
        DE[Wrote].Name[N] = '\0';
        DE[Wrote].Type    = (Dir->Children[I].Kind == PF_DIR) ? VNodeDIR : VNodeFILE;
        DE[Wrote].Ino     = (long)I; /*synthetic*/
        Wrote++;
    }

    return Wrote * (long)sizeof(VfsDirEnt);
}

/**
 * @brief Read: generate content for stat/status/fd entries.
 */
static long
ProcFsFileRead(File* __File__, void* __Buf__, long __Len__)
{
    PDebug("ProcFS: Read file=%p buf=%p len=%ld\n", (void*)__File__, __Buf__, __Len__);
    if (!__File__ || !__Buf__ || __Len__ <= 0)
    {
        return -1;
    }
    if (!__File__->Node || __File__->Node->Type != VNodeFILE)
    {
        return -1;
    }

    ProcFsFilePriv* Priv = (ProcFsFilePriv*)__File__->Node->Priv;
    if (!Priv)
    {
        return -1;
    }

    PDebug("ProcFS: Read pid=%ld fd=%ld entry=%d\n", Priv->Pid, Priv->Fd, (int)Priv->Entry);
    Process* P = ProcFind(Priv->Pid);
    if (!P)
    {
        PError("ProcFS: Read no process pid=%ld\n", Priv->Pid);
        return -1;
    }

    if (Priv->Entry == PEntryStat)
    {
        long N = ProcFsMakeStat(P, (char*)__Buf__, __Len__);
        PDebug("ProcFS: Read stat bytes=%ld\n", N);
        return N;
    }
    else if (Priv->Entry == PEntryStatus)
    {
        long N = ProcFsMakeStatus(P, (char*)__Buf__, __Len__);
        PDebug("ProcFS: Read status bytes=%ld\n", N);
        return N;
    }
    else if (Priv->Entry == PEntryFdItem)
    {
        if (Priv->Fd < 0 || Priv->Fd >= P->FdCap)
        {
            PError("ProcFS: Read fd out of range fd=%ld cap=%ld\n", Priv->Fd, P->FdCap);
            return -1;
        }
        ProcFd* E = &P->FdTable[Priv->Fd];
        if (E->Kind == PFdNone || E->Refcnt <= 0)
        {
            PError("ProcFS: Read fd invalid kind=%d ref=%ld\n", (int)E->Kind, E->Refcnt);
            return -1;
        }

        char* B       = (char*)__Buf__;
        long  Used    = 0;
        char  Num[32] = {0};

        StringCopy(B + Used, "fd=", 3);
        Used += 3;
        long val = Priv->Fd, tmp = 0;
        char digs[32];
        if (val == 0)
        {
            B[Used++] = '0';
        }
        else
        {
            while (val > 0)
            {
                digs[tmp++] = (char)('0' + (val % 10));
                val /= 10;
            }
            for (long I = tmp - 1; I >= 0; I--)
            {
                B[Used++] = digs[I];
            }
        }
        if (Used + 1 >= __Len__)
        {
            PError("ProcFS: Read fd item overflow used=%ld len=%ld\n", Used, __Len__);
            return -1;
        }
        B[Used++] = '\n';
        B[Used]   = '\0';
        PDebug("ProcFS: Read fd item bytes=%ld\n", Used);
        return Used;
    }
    PError("ProcFS: Read unknown entry=%d\n", (int)Priv->Entry);
    return -1;
}

/**
 * @brief Open a vnode in ProcFS.
 *
 * Initializes the VFS File handle for directories and files under /proc.
 * For directories, the File is set up without a private payload.
 * For files (stat/status/fd items), the File is bound to the vnode; content
 * generation happens in ProcFsFileRead(), so we do not allocate extra File->Priv here.
 *
 * @param __Node__ Vnode to open (directory or file)
 * @param __File__ File structure to initialize
 *
 * @return 0 on success, -1 on failure.
 */
static int
ProcFsOpen(Vnode* __Node__, File* __File__)
{
    PDebug("ProcFS: Open node=%p file=%p type=%d\n",
           (void*)__Node__,
           (void*)__File__,
           __Node__ ? (int)__Node__->Type : -1);

    if (!__Node__ || !__File__)
    {
        return -1;
    }

    /* Directories: attach node, zero offset, no priv */
    if (__Node__->Type == VNodeDIR)
    {
        __File__->Node   = __Node__;
        __File__->Offset = 0;
        __File__->Refcnt = 1;
        __File__->Priv   = 0;
        return 0;
    }

    /* Files: stat/status/fd items are synthesized on read */
    if (__Node__->Type == VNodeFILE)
    {
        /* Validate expected private payload */
        if (!__Node__->Priv)
        {
            PError("ProcFS: Open file missing priv\n");
            return -1;
        }

        __File__->Node   = __Node__;
        __File__->Offset = 0;
        __File__->Refcnt = 1;
        __File__->Priv   = 0; /* no extra per-open state needed */
        return 0;
    }

    PError("ProcFS: Open unsupported vnode type=%d\n", (int)__Node__->Type);
    return -1;
}

/**
 * @brief Close a ProcFS file handle.
 *
 * Releases any per-file private context (none used currently) and
 * returns success. Content is synthesized on demand and does not
 * maintain per-open state.
 *
 * @param __File__ File structure to close.
 *
 * @return 0 on success, -1 on failure.
 */
static int
ProcFsClose(File* __File__)
{
    PDebug(
        "ProcFS: Close file=%p node=%p\n", (void*)__File__, __File__ ? (void*)__File__->Node : 0);

    if (!__File__)
    {
        return -1;
    }

    /* No per-open context; clear pointer if present */
    if (__File__->Priv)
    {
        KFree(__File__->Priv);
        __File__->Priv = 0;
    }
    return 0;
}

/**
 * @brief Write: disabled (read-only).
 */
static long
ProcFsFileWrite(File* __File__, const void* __Buf__, long __Len__)
{
    PError("ProcFS: Write attempted file=%p len=%ld (read-only)\n", (void*)__File__, __Len__);
    (void)__File__;
    (void)__Buf__;
    (void)__Len__;
    return -1;
}

/**
 * @brief Stat: basic attributes for vnode.
 */
static int
ProcFsStat(Vnode* __Node__, VfsStat* __Stat__)
{
    PDebug("ProcFS: Stat node=%p out=%p\n", (void*)__Node__, (void*)__Stat__);
    if (!__Node__ || !__Stat__)
    {
        return -1;
    }
    __Stat__->Type      = __Node__->Type;
    __Stat__->Perm.Mode = VModeRUSR | VModeRGRP | VModeROTH;
    __Stat__->Perm.Uid  = 0;
    __Stat__->Perm.Gid  = 0;
    return 0;
}

/** @section Helpers */

/**
 * @brief Convert signed long to decimal string.
 *
 * @param __Value__  Value to convert.
 * @param __Buf__    Destination buffer.
 * @param __Cap__    Buffer capacity.
 *
 * @return Written length (excluding terminator) on success, -1 on failure.
 */
static long
IntToStr(long __Value__, char* __Buf__, long __Cap__)
{
    PDebug("ProcFS: IntToStr value=%ld buf=%p cap=%ld\n", __Value__, (void*)__Buf__, __Cap__);
    if (!__Buf__ || __Cap__ <= 0)
    {
        return -1;
    }

    char Tmp[32];
    long Idx      = 0;
    int  Negative = 0;

    if (__Value__ == 0)
    {
        if (__Cap__ < 2)
        {
            return -1;
        }
        __Buf__[0] = '0';
        __Buf__[1] = '\0';
        PDebug("ProcFS: IntToStr zero -> '0'\n");
        return 1;
    }

    if (__Value__ < 0)
    {
        Negative  = 1;
        __Value__ = -__Value__;
    }

    while (__Value__ > 0 && Idx < (long)sizeof(Tmp))
    {
        int Digit  = (int)(__Value__ % 10);
        Tmp[Idx++] = (char)('0' + Digit);
        __Value__ /= 10;
    }

    long Out = 0;
    if (Negative)
    {
        if (Out >= __Cap__ - 1)
        {
            return -1;
        }
        __Buf__[Out++] = '-';
    }
    for (long I = Idx - 1; I >= 0; I--)
    {
        if (Out >= __Cap__ - 1)
        {
            return -1;
        }
        __Buf__[Out++] = Tmp[I];
    }
    __Buf__[Out] = '\0';
    PDebug("ProcFS: IntToStr wrote='%s' len=%ld\n", __Buf__, Out);
    return Out;
}

/**
 * @brief Safe string append into a buffer.
 *
 * @param __Dst__ Destination buffer (NUL-terminated).
 * @param __Cap__ Capacity of destination buffer.
 * @param __Src__ Source string to append.
 *
 * @return 0 on success, -1 on overflow/failure.
 */
static int
StrAppend(char* __Dst__, long __Cap__, const char* __Src__)
{
    PDebug("ProcFS: StrAppend dst='%s' cap=%ld src='%s'\n",
           __Dst__ ? __Dst__ : "(null)",
           __Cap__,
           __Src__ ? __Src__ : "(null)");
    if (!__Dst__ || !__Src__ || __Cap__ <= 0)
    {
        return -1;
    }

    long Cur = (long)strlen(__Dst__);
    long Add = (long)strlen(__Src__);

    /* Need Add bytes plus 1 byte for the final NUL */
    if (Cur + Add + 1 > __Cap__)
    {
        PError("ProcFS: StrAppend overflow cur=%ld add=%ld cap=%ld\n", Cur, Add, __Cap__);
        return -1;
    }

    /* Control the terminator explicitly */
    __builtin_memcpy(__Dst__ + Cur, __Src__, (size_t)Add);
    __Dst__[Cur + Add] = '\0';

    PDebug("ProcFS: StrAppend ok -> '%s'\n", __Dst__);
    return 0;
}

/**
 * @brief Internal: bind ProcFsNode to a path's vnode.
 *
 * @param __Path__ Absolute path.
 * @param __Node__ ProcFsNode payload to bind (owned by VFS after bind).
 *
 * @return 0 on success, -1 on failure.
 *
 * @deprecated Not needed
 */

/*
static int
ProcFsBindNode(const char *__Path__, ProcFsNode *__Node__)
{
    PDebug("ProcFS: BindNode path='%s' node=%p\n", __Path__ ? __Path__ : "(null)", (void*)__Node__);
    if (!__Path__ || !__Node__)
    {
        return -1;
    }
    Dentry *Dent = VfsResolve(__Path__);
    if (!Dent || !Dent->Node)
    {
        PError("ProcFS: BindNode resolve failed path='%s'\n", __Path__); return -1;
    }
    Dent->Node->Priv = (void*)__Node__;
    PDebug("ProcFS: BindNode ok vnode=%p\n", (void*)Dent->Node);
    return 0;
}
*/

/** @section Lifecycle */

/**
 * @brief Initialize ProcFS and register filesystem type.
 *
 * @return 0 on success, -1 on failure.
 */
int
ProcFsInit(void)
{
    InitializeSpinLock(&__ProcFsLock__, "ProcFS");
    VfsPerm mp = {.Mode = VModeRUSR | VModeXUSR | VModeRGRP | VModeXGRP | VModeROTH | VModeXOTH,
                  .Uid  = 0,
                  .Gid  = 0};

    if (VfsExists("/proc") == 0)
    {
        if (VfsMkdir("/proc", mp) != 0)
        {
            PError("ProcFS: mkdir /proc failed\n");
            return -1;
        }
    }

    Superblock* Sb = ProcFsMountImpl(0, 0);
    if (!Sb)
    {
        PError("ProcFS: mount impl failed\n");
        return -1;
    }

    if (VfsRegisterPseudoFs("/proc", Sb) != 0)
    {
        PError("ProcFS: register pseudo failed\n");
        return -1;
    }

    Dentry* De = VfsResolve("/proc");
    if (De && Sb->Root)
    {
        De->Node = Sb->Root;
    }

    StringCopy(__ProcFsCtx__.MountPath, "/proc", (uint32_t)strlen("/proc"));
    __ProcFsCtx__.Super = Sb;

    PSuccess("ProcFS: mounted at /proc\n");
    return 0;
}

/**
 * @brief Mount ProcFS and return its superblock.
 *
 * @param __Device__  Reserved (NULL/0).
 * @param __Options__ Reserved (NULL/0).
 *
 * @return Superblock* on success, NULL on failure.
 */
Superblock*
ProcFsMountImpl(void* __Device__, void* __Options__)
{
    (void)__Device__;
    (void)__Options__;

    Superblock* Sb = (Superblock*)KMalloc(sizeof(Superblock));
    if (!Sb)
    {
        PError("ProcFS: Sb alloc failed\n");
        return 0;
    }
    __builtin_memset(Sb, 0, sizeof(Superblock));

    Vnode* Root = (Vnode*)KMalloc(sizeof(Vnode));
    if (!Root)
    {
        PError("ProcFS: Root vnode alloc failed\n");
        KFree(Sb);
        return 0;
    }

    ProcFsDirPriv* RPriv = (ProcFsDirPriv*)KMalloc(sizeof(ProcFsDirPriv));
    if (!RPriv)
    {
        PError("ProcFS: Root priv alloc failed\n");
        KFree(Root);
        KFree(Sb);
        return 0;
    }
    __builtin_memset(RPriv, 0, sizeof(*RPriv));
    RPriv->Pid     = 0;
    RPriv->IsFdDir = 0;

    Root->Type   = VNodeDIR;
    Root->Ops    = &__ProcFsOps__;
    Root->Sb     = Sb;
    Root->Priv   = RPriv;
    Root->Refcnt = 1;

    Sb->Root = Root;
    Sb->Ops  = 0;

    PDebug(
        "ProcFS: Superblock created Sb=%p Root=%p Priv=%p\n", (void*)Sb, (void*)Root, Root->Priv);

    return Sb;
}

/**
 * @brief Register ProcFS at a given mount path.
 *
 * @param __MountPath__ Path (e.g., "/proc").
 * @param __Super__     Superblock.
 *
 * @return 0 on success, -1 on failure.
 */
int
ProcFsRegisterMount(const char* __MountPath__, Superblock* __Super__)
{
    PDebug("ProcFS: RegisterMount path='%s' super=%p\n",
           __MountPath__ ? __MountPath__ : "(null)",
           (void*)__Super__);
    if (!__MountPath__ || !__Super__)
    {
        return -1;
    }
    if (VfsRegisterPseudoFs(__MountPath__, __Super__) != 0)
    {
        PError("ProcFS: VFS register failed\n");
        return -1;
    }
    PDebug("ProcFS: RegisterMount ok\n");
    return 0;
}

/** @section Exposure */

/**
 * @brief Create /proc/<pid> subtree: stat, status, fd/.
 *
 * @param __Proc__ Process to expose.
 *
 * @return 0 on success, -1 on failure.
 */
int
ProcFsExposeProcess(Process* __Proc__)
{
    if (!__Proc__ || !__ProcFsCtx__.Super || !__ProcFsCtx__.Super->Root)
    {
        return -1;
    }

    Vnode*         Root     = __ProcFsCtx__.Super->Root;
    ProcFsDirPriv* RootPriv = (ProcFsDirPriv*)Root->Priv;
    if (!RootPriv)
    {
        return -1;
    }

    char PidName[32] = {0};
    if (IntToStr(__Proc__->PID, PidName, sizeof(PidName)) < 0)
    {
        return -1;
    }

    /* /proc/<pid> dir */
    Vnode* PidDir = __procfs_alloc_dir__(Root->Sb, __Proc__->PID, 0);
    if (!PidDir)
    {
        return -1;
    }
    if (__procfs_dir_attach__(RootPriv, PidName, PidDir, PF_DIR) != 0)
    {
        ProcFsFreeNode(PidDir);
        return -1;
    }

    /* children under pid */
    ProcFsDirPriv* PidPriv = (ProcFsDirPriv*)PidDir->Priv;
    Vnode*         Stat    = __procfs_alloc_file__(PidDir->Sb, __Proc__->PID, -1, PEntryStat);
    Vnode*         Status  = __procfs_alloc_file__(PidDir->Sb, __Proc__->PID, -1, PEntryStatus);
    Vnode*         FdDir   = __procfs_alloc_dir__(PidDir->Sb, __Proc__->PID, 1);
    if (!Stat || !Status || !FdDir)
    {
        return -1;
    }

    if (__procfs_dir_attach__(PidPriv, "stat", Stat, PF_FILE) != 0)
    {
        return -1;
    }
    if (__procfs_dir_attach__(PidPriv, "status", Status, PF_FILE) != 0)
    {
        return -1;
    }
    if (__procfs_dir_attach__(PidPriv, "fd", FdDir, PF_DIR) != 0)
    {
        return -1;
    }

    /* /proc/<pid>/fd/<N> */
    ProcFsDirPriv* FdPriv = (ProcFsDirPriv*)FdDir->Priv;
    for (long I = 0; I < __Proc__->FdCount; I++)
    {
        ProcFd* Entry = &__Proc__->FdTable[I];
        if (Entry->Kind == PFdNone || Entry->Refcnt <= 0)
        {
            continue;
        }

        char FdName[32] = {0};
        if (IntToStr(I, FdName, sizeof(FdName)) < 0)
        {
            continue;
        }

        Vnode* FdItem = __procfs_alloc_file__(FdDir->Sb, __Proc__->PID, I, PEntryFdItem);
        if (!FdItem)
        {
            continue;
        }
        __procfs_dir_attach__(FdPriv, FdName, FdItem, PF_FILE);
    }

    return 0;
}

/**
 * @brief Remove /proc/<pid> subtree recursively.
 *
 * @param __Pid__ PID to detach from ProcFS.
 *
 * @return 0 on success, -1 on failure.
 */
int
ProcFsRemoveProcess(long __Pid__)
{
    if (!__ProcFsCtx__.Super || !__ProcFsCtx__.Super->Root)
    {
        return -1;
    }

    Vnode*         Root     = __ProcFsCtx__.Super->Root;
    ProcFsDirPriv* RootPriv = (ProcFsDirPriv*)Root->Priv;
    if (!RootPriv)
    {
        return -1;
    }

    char PidName[32] = {0};
    if (IntToStr(__Pid__, PidName, sizeof(PidName)) < 0)
    {
        return -1;
    }

    long idx = __procfs_dir_find__(RootPriv, PidName);
    if (idx < 0)
    {
        return -1;
    }

    return __procfs_dir_detach_idx__(RootPriv, idx);
}

/** @section Generators */

/**
 * @brief Generate stat content for /proc/<pid>/stat.
 *
 * @param __Proc__ Process pointer.
 * @param __Buf__  Output buffer.
 * @param __Cap__  Buffer capacity.
 *
 * @details Semi POSIX
 *
 * @return Written bytes on success, -1 on failure.
 */
long
ProcFsMakeStat(Process* __Proc__, char* __Buf__, long __Cap__)
{
    PDebug("ProcFS: MakeStat proc=%p pid=%ld buf=%p cap=%ld\n",
           (void*)__Proc__,
           __Proc__ ? __Proc__->PID : -1,
           (void*)__Buf__,
           __Cap__);
    if (!__Proc__ || !__Buf__ || __Cap__ <= 0)
    {
        return -1;
    }

    char Tmp[256];
    Tmp[0]             = '\0';
    const long TmpCap  = (long)sizeof(Tmp);
    char       Num[32] = {0};

    const char* Comm = (__Proc__->Cwd[0] != '\0') ? __Proc__->Cwd : "?";

    if (IntToStr(__Proc__->PID, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, " (") != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Comm) != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, ") ") != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, __Proc__->Zombie ? "Z " : "R ") != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "ppid=") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->PPID, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, " pgid=") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->PGID, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, " sid=") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->SID, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, " fds=") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->FdCount, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, " exit=") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->ExitCode, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\n") != 0)
    {
        return -1;
    }

    long Written = (long)strlen(Tmp);
    if (Written + 1 > __Cap__)
    {
        return -1;
    } /* do not touch __Buf__ */
    __Buf__[0] = '\0';
    if (StrAppend(__Buf__, __Cap__, Tmp) != 0)
    {
        return -1;
    }
    return Written;
}

/**
 * @brief Generate status content for /proc/<pid>/status.
 *
 * @param __Proc__ Process pointer.
 * @param __Buf__  Output buffer.
 * @param __Cap__  Buffer capacity.
 *
 * @details Semi POSIX
 *
 * @return Written bytes on success, -1 on failure.
 */
long
ProcFsMakeStatus(Process* __Proc__, char* __Buf__, long __Cap__)
{
    PDebug("ProcFS: MakeStatus proc=%p pid=%ld buf=%p cap=%ld\n",
           (void*)__Proc__,
           __Proc__ ? __Proc__->PID : -1,
           (void*)__Buf__,
           __Cap__);
    if (!__Proc__ || !__Buf__ || __Cap__ <= 0)
    {
        return -1;
    }

    char Tmp[256];
    Tmp[0]             = '\0';
    const long TmpCap  = (long)sizeof(Tmp);
    char       Num[32] = {0};

    const char* NameCwd = (__Proc__->Cwd[0] != '\0') ? __Proc__->Cwd : "?";
    const char* Cwd     = (__Proc__->Cwd[0] != '\0') ? __Proc__->Cwd : "/";
    const char* Root    = (__Proc__->Root[0] != '\0') ? __Proc__->Root : "/";

    if (StrAppend(Tmp, TmpCap, "Name:\t") != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, NameCwd) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nPid:\t") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->PID, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nPPid:\t") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->PPID, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nUid:\t") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->Cred.Uid, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nGid:\t") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->Cred.Gid, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nUmask:\t") != 0)
    {
        return -1;
    }
    if (IntToStr(__Proc__->Cred.Umask, Num, (long)sizeof(Num)) < 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Num) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nCwd:\t") != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Cwd) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nRoot:\t") != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, Root) != 0)
    {
        return -1;
    }

    if (StrAppend(Tmp, TmpCap, "\nState:\t") != 0)
    {
        return -1;
    }
    if (StrAppend(Tmp, TmpCap, __Proc__->Zombie ? "Zombie\n" : "Running\n") != 0)
    {
        return -1;
    }

    long Written = (long)strlen(Tmp);
    if (Written + 1 > __Cap__)
    {
        return -1;
    } /* do not touch __Buf__ */
    __Buf__[0] = '\0';
    if (StrAppend(__Buf__, __Cap__, Tmp) != 0)
    {
        return -1;
    }
    return Written;
}

/**
 * @brief Generate a listing for /proc/<pid>/fd directory.
 *
 * @param __Proc__ Process pointer.
 * @param __Buf__  Output buffer.
 * @param __Cap__  Buffer capacity.
 *
 * @return Written bytes on success, -1 on failure.
 */
long
ProcFsListFds(Process* __Proc__, char* __Buf__, long __Cap__)
{
    PDebug("ProcFS: ListFds proc=%p pid=%ld buf=%p cap=%ld\n",
           (void*)__Proc__,
           __Proc__ ? __Proc__->PID : -1,
           (void*)__Buf__,
           __Cap__);
    if (!__Proc__ || !__Buf__ || __Cap__ <= 0)
    {
        return -1;
    }
    __Buf__[0] = '\0';

    char Num[32] = {0};
    for (long I = 0; I < __Proc__->FdCount; I++)
    {
        ProcFd* Entry = &__Proc__->FdTable[I];
        if (Entry->Kind == PFdNone || Entry->Refcnt <= 0)
        {
            continue;
        }

        if (IntToStr(I, Num, sizeof(Num)) < 0)
        {
            return -1;
        }
        if (StrAppend(__Buf__, __Cap__, Num) != 0)
        {
            return -1;
        }
        if (StrAppend(__Buf__, __Cap__, "\n") != 0)
        {
            return -1;
        }
    }
    long Wrote = (long)strlen(__Buf__);
    PDebug("ProcFS: ListFds bytes=%ld\n", Wrote);
    return Wrote;
}

/** @section Resolver */

/**
 * @brief Resolve a ProcFS path to its process and optional FD.
 *
 * @param __Path__      Absolute path inside /proc.
 * @param __OutPid__    Out PID (set when applicable).
 * @param __OutFd__     Out FD index (or -1).
 * @param __OutEntry__  Entry type.
 *
 * @return 0 on success, -1 on failure.
 */
int
ProcFsResolve(const char*      __Path__,
              long*            __OutPid__,
              long*            __OutFd__,
              ProcFsEntryType* __OutEntry__)
{
    PDebug("ProcFS: Resolve path='%s'\n", __Path__ ? __Path__ : "(null)");
    if (!__Path__ || !__OutPid__ || !__OutFd__ || !__OutEntry__)
    {
        return -1;
    }
    *__OutPid__   = 0;
    *__OutFd__    = -1;
    *__OutEntry__ = PEntryNone;

    if (strcmp(__Path__, "/proc") == 0)
    {
        PDebug("ProcFS: Resolve -> root\n");
        return 0;
    }

    long Len = (long)strlen(__Path__);
    if (Len < 7)
    {
        return -1;
    } /* "/proc/x" min */

    const char* PidStart   = __Path__ + 6; /* after "/proc/" */
    char        PidStr[32] = {0};
    long        I          = 0;
    while (PidStart[I] && PidStart[I] != '/' && I < (long)sizeof(PidStr) - 1)
    {
        PidStr[I] = PidStart[I];
        I++;
    }
    PidStr[I] = '\0';

    long Pid = 0;
    for (long J = 0; PidStr[J]; J++)
    {
        if (PidStr[J] < '0' || PidStr[J] > '9')
        {
            return -1;
        }
        Pid = Pid * 10 + (PidStr[J] - '0');
    }
    if (Pid <= 0)
    {
        return -1;
    }

    if (PidStart[I] == '\0')
    {
        *__OutPid__   = Pid;
        *__OutEntry__ = PEntryNone; /* /proc/<pid> directory */
        PDebug("ProcFS: Resolve -> pid=%ld dir\n", Pid);
        return 0;
    }

    const char* Tail = PidStart + I + 1;
    if (strcmp(Tail, "stat") == 0)
    {
        *__OutPid__   = Pid;
        *__OutEntry__ = PEntryStat;
        PDebug("ProcFS: Resolve -> pid=%ld stat\n", Pid);
        return 0;
    }
    else if (strcmp(Tail, "status") == 0)
    {
        *__OutPid__   = Pid;
        *__OutEntry__ = PEntryStatus;
        PDebug("ProcFS: Resolve -> pid=%ld status\n", Pid);
        return 0;
    }
    else if (strncmp(Tail, "fd", 2) == 0)
    {
        *__OutPid__   = Pid;
        *__OutEntry__ = PEntryFdsDir;

        const char* FdTail = Tail + 2;
        if (*FdTail == '/')
        {
            const char* FdStr = FdTail + 1;
            long        Val   = 0;
            for (long J = 0; FdStr[J]; J++)
            {
                if (FdStr[J] < '0' || FdStr[J] > '9')
                {
                    Val = -1;
                    break;
                }
                Val = Val * 10 + (FdStr[J] - '0');
            }
            if (Val >= 0)
            {
                *__OutFd__    = Val;
                *__OutEntry__ = PEntryFdItem;
                PDebug("ProcFS: Resolve -> pid=%ld fd/%ld item\n", Pid, Val);
            }
            else
            {
                PDebug("ProcFS: Resolve -> pid=%ld fd dir\n", Pid);
            }
        }
        else
        {
            PDebug("ProcFS: Resolve -> pid=%ld fd dir\n", Pid);
        }
        return 0;
    }

    PError("ProcFS: Resolve failure tail='%s'\n", Tail);
    return -1;
}

/** @section VFS Bridges */

/**
 * @brief VFS read bridge for ProcFS files (stat/status/fd items).
 *
 * @param __File__   VFS file handle (contains Node with Priv=ProcFsNode).
 * @param __Buffer__ Destination buffer.
 * @param __Length__ Requested bytes.
 *
 * @return Read bytes on success, -1 on failure.
 */
long
ProcFsRead(File* __File__, void* __Buffer__, long __Length__)
{
    PDebug("ProcFS: ReadBridge file=%p node=%p len=%ld\n",
           (void*)__File__,
           __File__ ? (void*)__File__->Node : 0,
           __Length__);
    if (!__File__ || !__Buffer__ || __Length__ <= 0)
    {
        return -1;
    }
    if (!__File__->Node)
    {
        return -1;
    }
    ProcFsNode* Node = (ProcFsNode*)__File__->Node->Priv;
    if (!Node)
    {
        return -1;
    }

    Process* Proc = ProcFind(Node->Pid);
    if (!Proc)
    {
        return -1;
    }

    if (Node->Entry == PEntryStat)
    {
        long N = ProcFsMakeStat(Proc, (char*)__Buffer__, __Length__);
        PDebug("ProcFS: ReadBridge stat bytes=%ld\n", N);
        return N;
    }
    else if (Node->Entry == PEntryStatus)
    {
        long N = ProcFsMakeStatus(Proc, (char*)__Buffer__, __Length__);
        PDebug("ProcFS: ReadBridge status bytes=%ld\n", N);
        return N;
    }
    else if (Node->Entry == PEntryFdsDir)
    {
        long N = ProcFsListFds(Proc, (char*)__Buffer__, __Length__);
        PDebug("ProcFS: ReadBridge fds bytes=%ld\n", N);
        return N;
    }
    else if (Node->Entry == PEntryFdItem)
    {
        if (Node->Fd < 0 || Node->Fd >= Proc->FdCap)
        {
            return -1;
        }
        ProcFd* Entry = &Proc->FdTable[Node->Fd];
        if (Entry->Kind == PFdNone || Entry->Refcnt <= 0)
        {
            return -1;
        }

        char* Buf = (char*)__Buffer__;
        Buf[0]    = '\0';

        if (StrAppend(Buf, __Length__, "fd=") != 0)
        {
            return -1;
        }
        char Num[32] = {0};
        if (IntToStr(Node->Fd, Num, sizeof(Num)) < 0)
        {
            return -1;
        }
        if (StrAppend(Buf, __Length__, Num) != 0)
        {
            return -1;
        }

        if (StrAppend(Buf, __Length__, " kind=") != 0)
        {
            return -1;
        }
        if (IntToStr((long)Entry->Kind, Num, sizeof(Num)) < 0)
        {
            return -1;
        }
        if (StrAppend(Buf, __Length__, Num) != 0)
        {
            return -1;
        }

        if (StrAppend(Buf, __Length__, " flags=") != 0)
        {
            return -1;
        }
        if (IntToStr(Entry->Flags, Num, sizeof(Num)) < 0)
        {
            return -1;
        }
        if (StrAppend(Buf, __Length__, Num) != 0)
        {
            return -1;
        }

        if (StrAppend(Buf, __Length__, " ref=") != 0)
        {
            return -1;
        }
        if (IntToStr(Entry->Refcnt, Num, sizeof(Num)) < 0)
        {
            return -1;
        }
        if (StrAppend(Buf, __Length__, Num) != 0)
        {
            return -1;
        }

        if (StrAppend(Buf, __Length__, "\n") != 0)
        {
            return -1;
        }
        long N = (long)strlen(Buf);
        PDebug("ProcFS: ReadBridge fd item bytes=%ld\n", N);
        return N;
    }
    PError("ProcFS: ReadBridge unknown entry=%d\n", (int)Node->Entry);
    return -1;
}

/**
 * @brief VFS list/iterate bridge for ProcFS directories (/proc and /proc/<pid>/fd).
 *
 * @param __Node__    Directory vnode (Priv may be NULL for /proc).
 * @param __Buffer__  Destination buffer (serialized entries).
 * @param __Length__  Buffer capacity.
 *
 * @return Written bytes on success, -1 on failure.
 */
long
ProcFsList(Vnode* __Node__, void* __Buffer__, long __Length__)
{
    PDebug("ProcFS: ListBridge node=%p buf=%p len=%ld\n", (void*)__Node__, __Buffer__, __Length__);
    if (!__Buffer__ || __Length__ <= 0)
    {
        return -1;
    }

    char* Buf = (char*)__Buffer__;
    Buf[0]    = '\0';

    ProcFsNode* Node = __Node__ ? (ProcFsNode*)__Node__->Priv : 0;

    if (!Node)
    {
        /* Root /proc listing is managed by VFS directory walkers and process exposure.
           We do not enumerate here to avoid coupling return empty listing. */
        long N = (long)strlen(Buf);
        PDebug("ProcFS: ListBridge root empty bytes=%ld\n", N);
        return N;
    }

    if (Node->Entry == PEntryFdsDir)
    {
        Process* Proc = ProcFind(Node->Pid);
        if (!Proc)
        {
            return -1;
        }

        char Num[32] = {0};
        for (long I = 0; I < Proc->FdCount; I++)
        {
            ProcFd* Entry = &Proc->FdTable[I];
            if (Entry->Kind == PFdNone || Entry->Refcnt <= 0)
            {
                continue;
            }

            if (IntToStr(I, Num, sizeof(Num)) < 0)
            {
                return -1;
            }
            if (StrAppend(Buf, __Length__, Num) != 0)
            {
                return -1;
            }
            if (StrAppend(Buf, __Length__, "\n") != 0)
            {
                return -1;
            }
        }
        long N = (long)strlen(Buf);
        PDebug("ProcFS: ListBridge fds bytes=%ld\n", N);
        return N;
    }

    if (Node->Entry == PEntryNone)
    {
        if (StrAppend(Buf, __Length__, "stat\n") != 0)
        {
            return -1;
        }
        if (StrAppend(Buf, __Length__, "status\n") != 0)
        {
            return -1;
        }
        if (StrAppend(Buf, __Length__, "fd/\n") != 0)
        {
            return -1;
        }
        long N = (long)strlen(Buf);
        PDebug("ProcFS: ListBridge pid dir bytes=%ld\n", N);
        return N;
    }

    PError("ProcFS: ListBridge unsupported entry=%d\n", (int)Node->Entry);
    return -1;
}