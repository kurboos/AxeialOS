#include "KrnCommon.h"

/*Proc test*/
void
__TEST__Proc(void)
{
    PosixProc* Proc = PosixProcCreate();
    if (!Proc)
    {
        PError("PosixProcCreate failed\n");
        return;
    }

    PSuccess("Created process pid=%ld ppid=%ld\n", Proc->Pid, Proc->Ppid);

    /* Execve test */
    const char* argv[] = {"echo", "hello", NULL};
    const char* envp[] = {NULL};
    if (PosixProcExecve(Proc, "/Test.elf", argv, envp) != SysOkay)
    {
        PError("Execve failed for pid=%ld\n", Proc->Pid);
    }
}
