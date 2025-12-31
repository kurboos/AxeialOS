#include "Pci.h"

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

int
InRangeU8(int __Value__)
{
    return __Value__ >= 0 && __Value__ <= 255;
}

int
InRangeDev(int __Value__)
{
    return __Value__ >= 0 && __Value__ < 32;
}

int
InRangeFunc(int __Value__)
{
    return __Value__ >= 0 && __Value__ < 8;
}

int
ValidBuf(const void* __Ptr__, int __Len__)
{
    return __Ptr__ != NULL && __Len__ > 0;
}

int
ValidCfgWindow(int __Off__, int __Len__)
{
    return __Off__ >= 0 && __Off__ < 256 && (__Off__ + __Len__) <= 256;
}

int
NonZeroVidDid(uint16_t __Vid__, uint16_t __Did__)
{
    return !(__Vid__ == 0xFFFF || __Vid__ == 0x0000 || __Did__ == 0xFFFF || __Did__ == 0x0000);
}

void
GuardCtx(PciCtrlCtx* __Ctx__)
{
    if (!__Ctx__)
    {
        return;
    }
    if (!__Ctx__->Devices)
    {
        __Ctx__->DevCap   = 0;
        __Ctx__->DevCount = 0;
        return;
    }
    if (__Ctx__->DevCap == 0)
    {
        __Ctx__->DevCap = 32u;
    }
    if (__Ctx__->DevCount > __Ctx__->DevCap)
    {
        __Ctx__->DevCount = __Ctx__->DevCap;
    }
}

int
IsCtxSane(const PciCtrlCtx* __Ctx__)
{
    if (!__Ctx__)
    {
        return 0;
    }
    if (!__Ctx__->Devices && (__Ctx__->DevCount != 0 || __Ctx__->DevCap != 0))
    {
        return 0;
    }
    if (__Ctx__->Devices && __Ctx__->DevCap == 0)
    {
        return 0;
    }
    if (__Ctx__->DevCount > __Ctx__->DevCap)
    {
        return 0;
    }
    return 1;
}
