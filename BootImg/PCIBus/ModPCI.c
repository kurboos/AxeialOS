#include <Bus.h>
#include <Heaps.h>
#include <Logings.h>

/** @test Some testing for char devices and subchar devices like bus (in this case a pci)*/

typedef struct
{
    int DoesNothing;

} Idkwhattonameit;

static Idkwhattonameit stupidvariable;

static int
PciTestOpen(void* __CtrlCtx__)
{
    PInfo("pci: open (ctx=%p)\n", __CtrlCtx__);
    return 0;
}

static int
PciTestClose(void* __CtrlCtx__)
{
    PInfo("pci: close (ctx=%p)\n", __CtrlCtx__);
    return 0;
}

static long
PciTestRead(void* __CtrlCtx__, void* __Buf__, long __Len__)
{
    PInfo("pci: read (ctx=%p, buf=%p, len=%ld)\n", __CtrlCtx__, __Buf__, __Len__);
    return 0;
}

static long
PciTestWrite(void* __CtrlCtx__, const void* __Buf__, long __Len__)
{
    PInfo("pci: write (ctx=%p, buf=%p, len=%ld)\n", __CtrlCtx__, __Buf__, __Len__);
    return __Len__;
}

static int
PciTestIoctl(void* __CtrlCtx__, unsigned long __Cmd__, void* __Arg__)
{
    PInfo("pci: ioctl (ctx=%p, cmd=0x%lx, arg=%p)\n", __CtrlCtx__, __Cmd__, __Arg__);
    return 0;
}

static CharBus* PciTestBus = NULL;

int
module_init(void)
{
    PciTestBus = (CharBus*)KMalloc(sizeof(CharBus)); /*useless but static works too*/
    if (!PciTestBus)
    {
        PError("pci: KMalloc failed\n");
        return -1;
    }

    PciTestBus->Name      = "pci"; /*Works for now*/
    PciTestBus->CtrlCtx   = &stupidvariable;
    PciTestBus->Ops.Open  = PciTestOpen;
    PciTestBus->Ops.Close = PciTestClose;
    PciTestBus->Ops.Read  = PciTestRead;
    PciTestBus->Ops.Write = PciTestWrite;
    PciTestBus->Ops.Ioctl = PciTestIoctl;

    int Rc = CharRegisterBus(PciTestBus, 12, 0);
    if (Rc != 0)
    {
        PError("pci: CharRegisterBus failed (%d) for some reason\n", Rc);
        return Rc;
    }

    PSuccess("pci: /dev/pci on the way\n");
    return 0;
}

int
module_exit(void)
{
    PInfo("pci: module_exit called\n");
    if (PciTestBus)
    {
        KFree(PciTestBus);
        PciTestBus = NULL;
    }
    return 0;
}