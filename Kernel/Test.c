#include "KrnCommon.h"

/*Proc test*/
void
__TEST__Proc(void)
{
    PosixProc* Proc = PosixProcCreate();
    if (Probe_IF_Error(Proc) || !Proc)
    {
        PError("failed to create proc, errno: %d\n", Pointer_TO_Error(Proc));
        InitComplete = false;
        return;
    }

    PSuccess("Created process pid=%ld ppid=%ld\n", Proc->Pid, Proc->Ppid);

    /* Execve test */
    const char* argv[] = {"echo", "hello", NULL};
    const char* envp[] = {NULL};
    if (PosixProcExecve(Proc, "/Test.elf", argv, envp) != SysOkay)
    {
        PError("Execve failed for pid=%ld\n", Proc->Pid);
        InitComplete = true;
    }
    else
    {
        InitComplete = false;
    }
}

// #define __SUBTEST__Unload

/*DriverManager test*/
void
__TEST__DriverManager(void)
{
    /*if not Already*/
    int Result = InitializeDriverManager();
    if (Result != SysOkay)
    {
        PWarn("DriverManager init failed: %d\n", Result);
    }

    /*Test loading TestDriver*/
    Result = LoadDriver("TestDriver");
    if (Result == SysOkay)
    {
        PSuccess("TestDriver loaded successfully\n");

#ifdef __SUBTEST__Unload
        Result = UnloadDriver("TestDriver");
        if (Result == SysOkay)
        {
            PSuccess("TestDriver unloaded successfully\n");
        }
        else
        {
            PError("TestDriver unload failed: %d\n", Result);
        }
#endif
    }
    else
    {
        PError("TestDriver load failed: %d\n", Result);
    }
}