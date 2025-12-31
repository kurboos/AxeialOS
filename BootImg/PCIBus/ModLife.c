#include "Pci.h"

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

int
module_init(void)
{
    if (PciInitContext(&PciCtxHeap) != 0)
    {
        return -1;
    }

    CharBus* BusObj = (CharBus*)KMalloc(sizeof(CharBus));
    if (!BusObj)
    {
        PciFreeContext(PciCtxHeap);
        PciCtxHeap = NULL;
        return -1;
    }

    BusObj->Name      = "pci";
    BusObj->CtrlCtx   = (void*)PciCtxHeap;
    BusObj->Ops.Open  = PciOpen;
    BusObj->Ops.Close = PciClose;
    BusObj->Ops.Read  = PciRead;
    BusObj->Ops.Write = PciWrite;
    BusObj->Ops.Ioctl = PciIoctl;

    int Rc = CharRegisterBus(BusObj, 12, 0);
    if (Rc != 0)
    {
        KFree(BusObj);
        PciFreeContext(PciCtxHeap);
        PciCtxHeap = NULL;
        return Rc;
    }

    PciBus = BusObj;

    uint32_t Count = PciCtxHeap ? PciCtxHeap->DevCount : 0;
    uint32_t Cap   = PciCtxHeap ? PciCtxHeap->DevCap : 0;
    if (Cap && Count > Cap)
    {
        Count = Cap;
    }
    PSuccess("pci: /dev/pci ready (%u devices)\n", (unsigned)Count);
    return 0;
}

int
module_exit(void)
{
    if (PciBus)
    {
        KFree(PciBus);
        PciBus = NULL;
    }
    if (PciCtxHeap)
    {
        PciFreeContext(PciCtxHeap);
        PciCtxHeap = NULL;
    }
    return 0;
}