#pragma once

#include <AllTypes.h>
#include <AxeThreads.h>

/**
 * Enums
 */
typedef enum ProcSignal
{

    PSigNone = 0,  /**< No signal */
    PSigINT  = 2,  /**< Interrupt (Ctrl-C style) */
    PSigKILL = 9,  /**< Force kill (default: terminate) */
    PSigTERM = 15, /**< Terminate (default: terminate) */
    PSigSTOP = 19, /**< Stop (default: suspend) */
    PSigCONT = 18, /**< Continue (default: resume) */
    PSigCHLD = 17  /**< Child status changed */

} ProcSignal;

typedef struct ProcSigHandler
{

    void (*Handler)(int); /**< Handler function (kernel trampoline) */
    uint64_t Mask;        /**< Signals blocked during handler */
    int      Flags;       /**< Future flags (e.g., restart semantics) */

} ProcSigHandler;

typedef enum ProcFdKind
{

    PFdNone  = 0, /**< Unused descriptor */
    PFdChar  = 1, /**< Char device (ctx pointer) */
    PFdBlock = 2, /**< Block device (ctx pointer) */
    PFdVnode = 3  /**< VFS File* */

} ProcFdKind;

/**
 * Sturctures
 */
typedef struct ProcFd
{

    long       Fd;     /**< Descriptor index */
    ProcFdKind Kind;   /**< Descriptor class */
    void*      Obj;    /**< Bound object (File*, device ctx) */
    long       Flags;  /**< Open flags (subset) */
    long       Refcnt; /**< Reference count for dup/dup2 */

} ProcFd;

typedef struct ProcCred
{

    long Uid;   /**< User ID */
    long Gid;   /**< Group ID */
    long Umask; /**< POSIX umask bits */

} ProcCred;

typedef struct Process
{

    /* Identity and job control */
    long PID;  /**< Process ID */
    long PPID; /**< Parent process ID */
    long PGID; /**< Process group ID */
    long SID;  /**< Session ID */

    Thread* MainThread; /**< Pointer to primary thread (TCB) */

    ProcFd* FdTable;  /**< Descriptor table (dynamic array) */
    long    FdCount;  /**< Number of occupied descriptors (high water) */
    long    FdCap;    /**< Table capacity */
    long    FdStdin;  /**< FD index for stdin (usually 0) */
    long    FdStdout; /**< FD index for stdout (usually 1) */
    long    FdStderr; /**< FD index for stderr (usually 2) */

    char Cwd[256];  /**< Current working directory */
    char Root[256]; /**< Root directory (internal chroot-like) */

    ProcCred Cred; /**< UID/GID/umask */

    uint64_t       SigMask;      /**< Blocked signals mask */
    uint64_t       PendingSigs;  /**< Pending signals bitmap */
    ProcSigHandler SigTable[32]; /**< Signal handler table (minimal) */

    const char* TtyName; /**< TTY name (e.g., "tty0") */
    void*       TtyCtx;  /**< TTY driver context (CharDev) */

    int ExitCode; /**< Exit code */
    int Zombie;   /**< Exited but not reaped */

} Process;

typedef struct ProcTable
{

    Process** Items; /**< Array of process pointers */
    long      Count; /**< Number of entries */
    long      Cap;   /**< Capacity */

} ProcTable;

/**
 * Functions
 */
int      ProcInit(void);
Process* ProcCreate(long __ParentPid__);
Process* ProcFork(Process* __Parent__);
int      ProcExec(Process*           __Proc__,
                  const char*        __Path__,
                  const char* const* __Argv__,
                  const char* const* __Envp__); /*(Indirect)*/
int      ProcExecve(Process*           __Proc__,
                    const char*        __Path__,
                    const char* const* __Argv__,
                    const char* const* __Envp__); /*Elfloader(Direct)*/
int      ProcExit(Process* __Proc__, int __Code__);
Process* ProcFind(long __Pid__);
int      ProcFdEnsure(Process* __Proc__, long __Need__);
long     ProcFdAlloc(Process* __Proc__, long __Flags__);
int      ProcFdBind(Process* __Proc__, long __Fd__, ProcFdKind __Kind__, void* __Obj__);
int      ProcFdClose(Process* __Proc__, long __Fd__);
ProcFd*  ProcFdGet(Process* __Proc__, long __Fd__);
int      ProcSignalSend(long __Pid__, ProcSignal __Sig__);
int      ProcSignalMask(Process* __Proc__, uint64_t __Mask__, int __SetOrClear__);
int      ProcSignalSetHandler(
         Process* __Proc__, int __Sig__, void (*__Handler__)(int), uint64_t __Mask__, int __Flags__);
void     ProcDeliverPendingSignalsForCurrent(void);
int      ProcSetJobControl(Process* __Proc__, long __PGID__, long __SID__);
int      ProcAttachTty(Process* __Proc__, const char* __TtyName__, void* __TtyCtx__);
int      ProcDetachTty(Process* __Proc__);
ProcCred ProcGetCred(Process* __Proc__);
int      ProcSetUidGid(Process* __Proc__, long __Uid__, long __Gid__);
int      ProcSetUmask(Process* __Proc__, long __Umask__);
long     ProcWaitPid(long __Pid__, int* __OutStatus__, int __Options__);
int      ProcReap(Process* __Parent__, long __ChildPid__);
long     GetPid(void);

/**
 * Public
 */
KEXPORT(ProcInit);
KEXPORT(ProcCreate);
KEXPORT(ProcFork);
KEXPORT(ProcExec);
KEXPORT(ProcExecve);
KEXPORT(ProcExit);
KEXPORT(ProcFind);

KEXPORT(ProcFdEnsure);
KEXPORT(ProcFdAlloc);
KEXPORT(ProcFdBind);
KEXPORT(ProcFdClose);
KEXPORT(ProcFdGet);

KEXPORT(ProcSignalSend);
KEXPORT(ProcSignalMask);
KEXPORT(ProcSignalSetHandler);
KEXPORT(ProcDeliverPendingSignalsForCurrent);

KEXPORT(ProcSetJobControl);
KEXPORT(ProcAttachTty);
KEXPORT(ProcDetachTty);

KEXPORT(ProcGetCred);
KEXPORT(ProcSetUidGid);
KEXPORT(ProcSetUmask);

KEXPORT(ProcWaitPid);
KEXPORT(ProcReap);

KEXPORT(GetPid);