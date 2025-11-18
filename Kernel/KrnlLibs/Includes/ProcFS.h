#pragma once

#include <AllTypes.h>
#include <AxeThreads.h>
#include <KExports.h>
#include <Process.h>
#include <VFS.h>

/**
 * Enums
 */
typedef enum ProcFsNodeKind
{
    PNodeNone    = 0, /**< Invalid */
    PNodeDir     = 1, /**< Directory */
    PNodeFile    = 2, /**< Regular file */
    PNodeSymlink = 3  /**< Symlink (reserved) */

} ProcFsNodeKind;

typedef enum ProcFsEntryType
{
    PEntryNone   = 0, /**< Invalid */
    PEntryStat   = 1, /**< /proc/<pid>/stat    (single-line summary) */
    PEntryStatus = 2, /**< /proc/<pid>/status  (multi-line details) */
    PEntryFdsDir = 3, /**< /proc/<pid>/fd      (directory listing of FDs) */
    PEntryFdItem = 4  /**< /proc/<pid>/fd/<N>  (per-FD entry view) */

} ProcFsEntryType;

/**
 * Sturctures
 */
typedef struct ProcFsNode
{
    ProcFsNodeKind  Kind;  /**< Node kind */
    ProcFsEntryType Entry; /**< Entry type */
    long            Pid;   /**< Associated PID (or 0 for non-PID nodes) */
    long            Fd;    /**< FD index for fd items (or -1 if not applicable) */

} ProcFsNode;

typedef struct ProcFsContext
{
    Superblock* Super;         /**< Mounted superblock pointer */
    char        MountPath[64]; /**< Path where /proc is mounted */

} ProcFsContext;

/**
 * @brief Kind of procfs entry.
 */
typedef enum ProcFsEntryKind
{
    PF_DIR  = 0,
    PF_FILE = 1
} ProcFsEntryKind;

/**
 * @brief Child entry metadata stored in a directory's private context.
 */
typedef struct ProcFsChild
{
    char*           Name; /**< entry name */
    Vnode*          Node; /**< child vnode */
    ProcFsEntryKind Kind; /**< dir or file */
} ProcFsChild;

/**
 * @brief Directory private context: dynamic array of children.
 */
typedef struct ProcFsDirPriv
{
    ProcFsChild* Children;
    long         Count;
    long         Cap;
    long         Pid;     /**< pid for /proc/<pid> directories, 0 for root */
    long         IsFdDir; /**< 1 if /proc/<pid>/fd */
} ProcFsDirPriv;

/**
 * @brief File private context: ties file to process and role.
 */
typedef struct ProcFsFilePriv
{
    long            Pid;   /**< process id */
    long            Fd;    /**< fd index if fd item, else -1 */
    ProcFsEntryType Entry; /**< stat/status/fd item */
} ProcFsFilePriv;

/**
 * Functions
 */
int         ProcFsInit(void);
Superblock* ProcFsMountImpl(void* __Device__, void* __Options__);
int         ProcFsRegisterMount(const char* __MountPath__, Superblock* __Super__);
int         ProcFsExposeProcess(Process* __Proc__);
int         ProcFsRemoveProcess(long __Pid__);
long        ProcFsMakeStat(Process* __Proc__, char* __Buf__, long __Cap__);
long        ProcFsMakeStatus(Process* __Proc__, char* __Buf__, long __Cap__);
long        ProcFsListFds(Process* __Proc__, char* __Buf__, long __Cap__);
long        ProcFsList(Vnode* __Node__, void* __Buffer__, long __Length__);
int         ProcFsResolve(const char*      __Path__,
                          long*            __OutPid__,
                          long*            __OutFd__,
                          ProcFsEntryType* __OutEntry__);
/*Nodes*/
Vnode* ProcFsAllocNode(
    Superblock* __Sb__, VnodeType __Type__, ProcFsEntryType __Entry__, long __Pid__, long __Fd__);
void ProcFsFreeNode(Vnode* __Node__);
/*Externs*/
Vnode* __procfs_alloc_dir__(Superblock* Sb,
                            long        Pid,
                            long        IsFdDir); /** @todo Remove it and declare as static */