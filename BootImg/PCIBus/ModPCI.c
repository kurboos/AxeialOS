#include "Pci.h"

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

PciCtrlCtx* PciCtxHeap = NULL;
CharBus*    PciBus     = NULL;
uint64_t    PciCanary  = 0xA55AC0DECAFEBABEULL; /*Alignment check*/

uint32_t
PciReadBarRaw(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Index__)
{
    int Off = 0x10 + (__Index__ * 4);
    return PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, Off);
}

uint32_t
PciSizeBar(
    PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Index__, uint32_t __BarVal__)
{
    int Off = 0x10 + (__Index__ * 4);
    PciCfgWrite32(__Ctx__, __Bus__, __Dev__, __Func__, Off, 0xFFFFFFFFu);
    uint32_t SizeMask = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, Off);
    PciCfgWrite32(__Ctx__, __Bus__, __Dev__, __Func__, Off, __BarVal__);
    if (__BarVal__ & 0x01)
    {
        uint32_t Size = ~(SizeMask & ~0x03u) + 1;
        return Size;
    }
    else
    {
        uint32_t Size = ~(SizeMask & ~0x0Fu) + 1;
        return Size;
    }
}

void
PciCollectBars(PciCtrlCtx* __Ctx__, PciDevice* __Dev__)
{
    for (int I = 0; I < 6; I++)
    {
        uint32_t Raw     = PciReadBarRaw(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, I);
        __Dev__->Bars[I] = Raw;
        uint8_t Type     = (Raw & 0x01) ? 1u : 0u;
        if ((Raw & 0x07) == 0x04)
        {
            Type = 2u;
        }
        __Dev__->BarTypes[I] = Type;
        uint32_t Sz = PciSizeBar(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, I, Raw);
        __Dev__->BarSizes[I] = Sz;
        if (((Raw & 0x07) == 0x04) && I < 5)
        {
            uint32_t RawHi =
                PciReadBarRaw(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, I + 1);
            uint64_t Raw64   = ((uint64_t)RawHi << 32) | Raw;
            __Dev__->Bars[I] = Raw64;
            uint32_t SzHi =
                PciSizeBar(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, I + 1, RawHi);
            uint64_t Size64      = ((uint64_t)SzHi << 32) | Sz;
            __Dev__->BarSizes[I] = Size64;
            I++;
        }
    }
}

uint8_t
PciFindNextCap(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, uint8_t __Start__)
{
    uint8_t Ptr   = __Start__;
    int     Guard = 0;
    while (Ptr != 0 && Guard < 64)
    {
        unsigned int CapHdr = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, Ptr);
        if (!IsValidCfgValue(CapHdr))
        {
            return 0;
        }
        uint8_t Next = (uint8_t)((CapHdr >> 8) & 0xFF);
        uint8_t Id   = (uint8_t)(CapHdr & 0xFF);
        if (Id == 0x05 || Id == 0x11 || Id == 0x10 || Id == 0x01)
        {
            return Ptr;
        }
        Ptr = Next;
        Guard++;
    }
    return 0;
}

void
PciCollectCaps(PciCtrlCtx* __Ctx__, PciDevice* __Dev__)
{
    unsigned int StCmd = PciCfgRead32(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, 0x04);
    uint16_t     Cmd   = (uint16_t)(StCmd & 0xFFFF);
    uint16_t     Sts   = (uint16_t)((StCmd >> 16) & 0xFFFF);
    __Dev__->Command   = Cmd;
    __Dev__->Status    = Sts;

    unsigned int Int       = PciCfgRead32(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, 0x3C);
    __Dev__->InterruptLine = (uint8_t)(Int & 0xFF);
    __Dev__->InterruptPin  = (uint8_t)((Int >> 8) & 0xFF);

    unsigned int CapPtrReg = PciCfgRead32(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, 0x34);
    uint8_t      CapPtr    = (uint8_t)(CapPtrReg & 0xFF);
    __Dev__->CapPtr        = CapPtr;

    uint8_t Msi = PciFindNextCap(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, CapPtr);
    __Dev__->MsiCapOffset  = 0;
    __Dev__->MsixCapOffset = 0;
    __Dev__->PcieCapOffset = 0;
    __Dev__->PmCapOffset   = 0;

    uint8_t Ptr   = CapPtr;
    int     Guard = 0;
    while (Ptr != 0 && Guard < 64)
    {
        unsigned int CapHdr = PciCfgRead32(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, Ptr);
        if (!IsValidCfgValue(CapHdr))
        {
            break;
        }
        uint8_t Id   = (uint8_t)(CapHdr & 0xFF);
        uint8_t Next = (uint8_t)((CapHdr >> 8) & 0xFF);
        if (Id == 0x05 && __Dev__->MsiCapOffset == 0)
        {
            __Dev__->MsiCapOffset = Ptr;
        }
        if (Id == 0x11 && __Dev__->MsixCapOffset == 0)
        {
            __Dev__->MsixCapOffset = Ptr;
        }
        if (Id == 0x10 && __Dev__->PcieCapOffset == 0)
        {
            __Dev__->PcieCapOffset = Ptr;
        }
        if (Id == 0x01 && __Dev__->PmCapOffset == 0)
        {
            __Dev__->PmCapOffset = Ptr;
        }
        Ptr = Next;
        Guard++;
    }
}

void
PciEnableBmIoMem(PciCtrlCtx* __Ctx__, PciDevice* __Dev__, int __Enable__)
{
    unsigned int CmdSts = PciCfgRead32(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, 0x04);
    uint16_t     Cmd    = (uint16_t)(CmdSts & 0xFFFF);
    if (__Enable__)
    {
        Cmd |= (1u << 2);
        Cmd |= (1u << 0);
        Cmd |= (1u << 1);
    }
    else
    {
        Cmd &= ~(1u << 2);
    }
    unsigned int NewReg = (CmdSts & 0xFFFF0000u) | Cmd;
    PciCfgWrite32(__Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, 0x04, NewReg);
    __Dev__->Command = Cmd;
}

int
PciSetPowerState(PciCtrlCtx* __Ctx__, PciDevice* __Dev__, int __DState__)
{
    if (__Dev__->PmCapOffset == 0)
    {
        return -1;
    }
    unsigned int Pmc = PciCfgRead32(
        __Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, __Dev__->PmCapOffset + 0x02);
    uint16_t PmCs    = (uint16_t)(Pmc & 0xFFFF);
    PmCs             = (PmCs & ~0x0003u) | (uint16_t)(__DState__ & 0x3);
    unsigned int New = (Pmc & 0xFFFF0000u) | PmCs;
    PciCfgWrite32(
        __Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, __Dev__->PmCapOffset + 0x02, New);
    return 0;
}

int
PciEnableMsi(PciCtrlCtx* __Ctx__, PciDevice* __Dev__, int __Enable__)
{
    if (__Dev__->MsiCapOffset == 0)
    {
        return -1;
    }
    unsigned int Ctrl = PciCfgRead32(
        __Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, __Dev__->MsiCapOffset + 0x02);
    uint16_t MsiCtrl = (uint16_t)(Ctrl & 0xFFFF);
    if (__Enable__)
    {
        MsiCtrl |= 0x0001u;
    }
    else
    {
        MsiCtrl &= ~0x0001u;
    }
    unsigned int New = (Ctrl & 0xFFFF0000u) | MsiCtrl;
    PciCfgWrite32(
        __Ctx__, __Dev__->Bus, __Dev__->Dev, __Dev__->Func, __Dev__->MsiCapOffset + 0x02, New);
    return 0;
}

int
PciProbeFunc(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__)
{
    if (!__Ctx__ || !IsCtxSane(__Ctx__))
    {
        return -1;
    }
    if (!InRangeU8(__Bus__) || !InRangeDev(__Dev__) || !InRangeFunc(__Func__))
    {
        return 0;
    }
    GuardCtx(__Ctx__);

    unsigned int Vd = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, 0x00);
    if (!IsValidCfgValue(Vd))
    {
        return 0;
    }
    uint16_t Vid = (uint16_t)(Vd & 0xFFFF);
    uint16_t Did = (uint16_t)((Vd >> 16) & 0xFFFF);
    if (!NonZeroVidDid(Vid, Did))
    {
        return 0;
    }

    unsigned int ClassReg = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, 0x08);
    if (!IsValidCfgValue(ClassReg))
    {
        return 0;
    }
    uint8_t Rev     = (uint8_t)(ClassReg & 0xFF);
    uint8_t ProgIf  = (uint8_t)((ClassReg >> 8) & 0xFF);
    uint8_t SubCls  = (uint8_t)((ClassReg >> 16) & 0xFF);
    uint8_t ClsCode = (uint8_t)((ClassReg >> 24) & 0xFF);

    unsigned int Hdr = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, 0x0C);
    if (!IsValidCfgValue(Hdr))
    {
        return 0;
    }
    uint8_t HeaderType = (uint8_t)((Hdr >> 16) & 0xFF);
    uint8_t Mf         = (uint8_t)((HeaderType & 0x80) ? 1u : 0u);
    uint8_t HtLo       = (uint8_t)(HeaderType & 0x7F);
    if (!(HtLo == 0x00 || HtLo == 0x01 || HtLo == 0x02))
    {
        return 0;
    }

    if (__Ctx__->DevCount >= __Ctx__->DevCap)
    {
        uint32_t   NewCap = (__Ctx__->DevCap == 0) ? 32u : (__Ctx__->DevCap * 2u);
        size_t     NewSz  = sizeof(PciDevice) * NewCap;
        PciDevice* NewArr = (PciDevice*)KMalloc(NewSz);
        if (!NewArr)
        {
            return -1;
        }
        for (uint32_t I = 0; I < __Ctx__->DevCount; I++)
        {
            NewArr[I] = __Ctx__->Devices[I];
        }
        if (__Ctx__->Devices)
        {
            KFree(__Ctx__->Devices);
        }
        __Ctx__->Devices = NewArr;
        __Ctx__->DevCap  = NewCap;
    }

    if (!__Ctx__->Devices)
    {
        return -1;
    }
    if (__Ctx__->DevCount >= __Ctx__->DevCap)
    {
        return -1;
    }

    PciDevice* DevPtr     = &__Ctx__->Devices[__Ctx__->DevCount];
    DevPtr->Bus           = (uint8_t)__Bus__;
    DevPtr->Dev           = (uint8_t)__Dev__;
    DevPtr->Func          = (uint8_t)__Func__;
    DevPtr->VendorId      = Vid;
    DevPtr->DeviceId      = Did;
    DevPtr->ClassCode     = ClsCode;
    DevPtr->SubClass      = SubCls;
    DevPtr->ProgIf        = ProgIf;
    DevPtr->Revision      = Rev;
    DevPtr->HeaderType    = HeaderType;
    DevPtr->MultiFunction = Mf;

    if (HtLo == 0x01)
    {
        unsigned int BusReg    = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, 0x18);
        DevPtr->PrimaryBus     = (uint8_t)(BusReg & 0xFF);
        DevPtr->SecondaryBus   = (uint8_t)((BusReg >> 8) & 0xFF);
        DevPtr->SubordinateBus = (uint8_t)((BusReg >> 16) & 0xFF);
    }
    else
    {
        DevPtr->PrimaryBus     = 0;
        DevPtr->SecondaryBus   = 0;
        DevPtr->SubordinateBus = 0;
    }

    PciCollectCaps(__Ctx__, DevPtr);
    PciCollectBars(__Ctx__, DevPtr);

    __Ctx__->DevCount++;
    return 1;
}

void
PciScanBus(PciCtrlCtx* __Ctx__, int __Bus__)
{
    for (int __Dev__ = 0; __Dev__ < 32; __Dev__++)
    {
        int Found0 = PciProbeFunc(__Ctx__, __Bus__, __Dev__, 0);
        if (Found0 > 0)
        {
            unsigned int Hdr        = PciCfgRead32(__Ctx__, __Bus__, __Dev__, 0, 0x0C);
            uint8_t      HeaderType = (uint8_t)((Hdr >> 16) & 0xFF);
            if ((HeaderType & 0x80) != 0)
            {
                for (int __Func__ = 1; __Func__ < 8; __Func__++)
                {
                    PciProbeFunc(__Ctx__, __Bus__, __Dev__, __Func__);
                }
            }

            uint8_t HtLo = HeaderType & 0x7F;
            if (HtLo == 0x01)
            {
                unsigned int BusReg         = PciCfgRead32(__Ctx__, __Bus__, __Dev__, 0, 0x18);
                uint8_t      SecondaryBus   = (uint8_t)((BusReg >> 8) & 0xFF);
                uint8_t      SubordinateBus = (uint8_t)((BusReg >> 16) & 0xFF);

                if (SecondaryBus != 0 && SubordinateBus >= SecondaryBus)
                {
                    for (int B = SecondaryBus; B <= SubordinateBus; B++)
                    {
                        PciScanBus(__Ctx__, B);
                    }
                }
            }
        }
    }
}

int
PciEnumerate(PciCtrlCtx* __Ctx__)
{
    if (!__Ctx__ || !IsCtxSane(__Ctx__))
    {
        return -1;
    }

    __Ctx__->DevCount = 0;
    GuardCtx(__Ctx__);

    PciScanBus(__Ctx__, 0);

    if (__Ctx__->DevCount > __Ctx__->DevCap)
    {
        __Ctx__->DevCount = __Ctx__->DevCap;
    }
    return 0;
}

int
PciFindByBdf(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, PciDevice* __Out__)
{
    if (!__Ctx__ || !__Ctx__->Devices)
    {
        return -1;
    }
    for (uint32_t I = 0; I < __Ctx__->DevCount; I++)
    {
        PciDevice* DevPtr = &__Ctx__->Devices[I];
        if (DevPtr->Bus == (uint8_t)__Bus__ && DevPtr->Dev == (uint8_t)__Dev__ &&
            DevPtr->Func == (uint8_t)__Func__)
        {
            if (__Out__)
            {
                *__Out__ = *DevPtr;
            }
            return (int)I;
        }
    }
    return -1;
}

int
PciFindByVendor(
    PciCtrlCtx* __Ctx__, uint16_t __Vid__, uint16_t __Did__, int __Index__, PciDevice* __Out__)
{
    if (!__Ctx__ || !__Ctx__->Devices)
    {
        return -1;
    }
    int Seen = 0;
    for (uint32_t I = 0; I < __Ctx__->DevCount; I++)
    {
        PciDevice* DevPtr = &__Ctx__->Devices[I];
        if (DevPtr->VendorId == __Vid__ && DevPtr->DeviceId == __Did__)
        {
            if (Seen == __Index__)
            {
                if (__Out__)
                {
                    *__Out__ = *DevPtr;
                }
                return (int)I;
            }
            Seen++;
        }
    }
    return -1;
}