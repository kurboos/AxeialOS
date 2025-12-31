#include "Pci.h"

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

int
PciOpen(void* __CtrlCtx__)
{
    (void)__CtrlCtx__;
    return 0;
}

int
PciClose(void* __CtrlCtx__)
{
    (void)__CtrlCtx__;
    return 0;
}

long
PciRead(void* __CtrlCtx__, void* __Buf__, long __Len__)
{
    (void)__CtrlCtx__;
    (void)__Buf__;
    if (__Len__ < 0)
    {
        return -1;
    }
    return 0;
}

long
PciWrite(void* __CtrlCtx__, const void* __Buf__, long __Len__)
{
    (void)__CtrlCtx__;
    (void)__Buf__;
    if (__Len__ < 0)
    {
        return -1;
    }
    return __Len__;
}

int
PciIoctl(void* __CtrlCtx__, unsigned long __Cmd__, void* __Arg__)
{
    PciCtrlCtx* Ctx = (PciCtrlCtx*)__CtrlCtx__;
    if (!Ctx)
    {
        Ctx = PciCtxHeap;
    }
    if (!Ctx || !IsCtxSane(Ctx))
    {
        return -1;
    }

    switch (__Cmd__)
    {
        case GenericPing:
            {
                return 0;
            }
        case GenericGetVersion:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                uint32_t* Out = (uint32_t*)__Arg__;
                Out[0]        = 1u;
                Out[1]        = 0u;
                Out[2]        = 0u;
                return 0;
            }
        case GenericGetCaps:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                uint32_t* Out = (uint32_t*)__Arg__;
                Out[0]        = 0x00000001u;
                return 0;
            }

        case BusGetCount:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                uint32_t* Out   = (uint32_t*)__Arg__;
                uint32_t  Count = Ctx->DevCount;
                if (Count > Ctx->DevCap)
                {
                    Count = Ctx->DevCap;
                }
                *Out = Count;
                return 0;
            }
        case BusGetInfo:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                uint32_t* Out = (uint32_t*)__Arg__;
                Out[0]        = 256u;
                Out[1]        = 32u;
                Out[2]        = 8u;
                return 0;
            }
        case BusEnumerate:
        case BusRescan:
            {
                return PciEnumerate(Ctx);
            }
        case BusReset:
            {
                return -1;
            }

        case DeviceGetInfo:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciAddrReq* Req = (PciAddrReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                PciDevice OutDev;
                int       Idx = PciFindByBdf(Ctx, Req->Bus, Req->Dev, Req->Func, &OutDev);
                if (Idx < 0)
                {
                    return -1;
                }
                *((PciDevice*)__Arg__) = OutDev;
                return 0;
            }
        case DeviceGetByAddress:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciAddrReq* Req = (PciAddrReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                PciDevice OutDev;
                int       Idx = PciFindByBdf(Ctx, Req->Bus, Req->Dev, Req->Func, &OutDev);
                if (Idx < 0)
                {
                    return -1;
                }
                *((PciDevice*)__Arg__) = OutDev;
                return 0;
            }
        case DeviceGetByVendor:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciVendorReq* Req = (PciVendorReq*)__Arg__;
                if (!NonZeroVidDid(Req->VendorId, Req->DeviceId) || Req->Index < 0)
                {
                    return -1;
                }
                PciDevice OutDev;
                int Idx = PciFindByVendor(Ctx, Req->VendorId, Req->DeviceId, Req->Index, &OutDev);
                if (Idx < 0)
                {
                    return -1;
                }
                *((PciDevice*)__Arg__) = OutDev;
                return 0;
            }
        case DeviceEnable:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciBmReq* Req = (PciBmReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                unsigned int CmdReg = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04);
                if (CmdReg == 0xFFFFFFFFu)
                {
                    return -1;
                }
                unsigned short Cmd16 = (unsigned short)(CmdReg & 0xFFFF);
                Cmd16 |= (1u << 0);
                Cmd16 |= (1u << 1);
                Cmd16 |= (1u << 2);
                unsigned int NewReg = (CmdReg & 0xFFFF0000u) | Cmd16;
                PciCfgWrite32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04, NewReg);
                return 0;
            }
        case DeviceDisable:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciBmReq* Req = (PciBmReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                unsigned int CmdReg = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04);
                if (CmdReg == 0xFFFFFFFFu)
                {
                    return -1;
                }
                unsigned short Cmd16 = (unsigned short)(CmdReg & 0xFFFF);
                Cmd16 &= ~(1u << 2);
                unsigned int NewReg = (CmdReg & 0xFFFF0000u) | Cmd16;
                PciCfgWrite32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04, NewReg);
                return 0;
            }
        case DeviceReset:
            {
                return -1;
            }

        case ConfigRead:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciCfgReq* Req = (PciCfgReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                if (!ValidBuf(Req->Buf, Req->Len))
                {
                    return -1;
                }
                if (!ValidCfgWindow(Req->Off, Req->Len))
                {
                    return -1;
                }
                int Rc =
                    PciCfgRead(Ctx, Req->Bus, Req->Dev, Req->Func, Req->Off, Req->Buf, Req->Len);
                return (Rc < 0) ? -1 : 0;
            }
        case ConfigWrite:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciCfgReq* Req = (PciCfgReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                if (!ValidBuf(Req->Buf, Req->Len))
                {
                    return -1;
                }
                if (!ValidCfgWindow(Req->Off, Req->Len))
                {
                    return -1;
                }
                int Rc =
                    PciCfgWrite(Ctx, Req->Bus, Req->Dev, Req->Func, Req->Off, Req->Buf, Req->Len);
                return (Rc < 0) ? -1 : 0;
            }
        case ConfigMapRegion:
        case ConfigUnmapRegion:
        case ConfigGetAddressing:
            {
                return -1;
            }

        case PowerGetState:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciAddrReq* Req = (PciAddrReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                PciDevice DevTmp;
                int       Idx = PciFindByBdf(Ctx, Req->Bus, Req->Dev, Req->Func, &DevTmp);
                if (Idx < 0)
                {
                    return -1;
                }
                uint32_t CapPtrReg = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x34);
                uint8_t  CapPtr    = (uint8_t)(CapPtrReg & 0xFF);
                if (CapPtr == 0)
                {
                    return -1;
                }
                unsigned int Pmc = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, CapPtr + 0x02);
                if (!IsValidCfgValue(Pmc))
                {
                    return -1;
                }
                uint16_t PmCs         = (uint16_t)(Pmc & 0xFFFF);
                *((uint16_t*)__Arg__) = (PmCs & 0x0003u);
                return 0;
            }
        case PowerSetState:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciPowerReq* Req = (PciPowerReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                PciDevice DevTmp;
                int       Idx = PciFindByBdf(Ctx, Req->Bus, Req->Dev, Req->Func, &DevTmp);
                if (Idx < 0)
                {
                    return -1;
                }
                int Rc = PciSetPowerState(Ctx, &DevTmp, Req->DState);
                if (Rc < 0)
                {
                    return -1;
                }
                Ctx->Devices[Idx] = DevTmp;
                return 0;
            }

        case DmaEnableBusMaster:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciBmReq* Req = (PciBmReq*)__Arg__;
                Req->Enable   = 1;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                unsigned int CmdReg = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04);
                if (CmdReg == 0xFFFFFFFFu)
                {
                    return -1;
                }
                unsigned short Cmd16 = (unsigned short)(CmdReg & 0xFFFF);
                Cmd16 |= (1u << 2);
                unsigned int NewReg = (CmdReg & 0xFFFF0000u) | Cmd16;
                PciCfgWrite32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04, NewReg);
                return 0;
            }
        case DmaDisableBusMaster:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciBmReq* Req = (PciBmReq*)__Arg__;
                Req->Enable   = 0;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                unsigned int CmdReg = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04);
                if (CmdReg == 0xFFFFFFFFu)
                {
                    return -1;
                }
                unsigned short Cmd16 = (unsigned short)(CmdReg & 0xFFFF);
                Cmd16 &= ~(1u << 2);
                unsigned int NewReg = (CmdReg & 0xFFFF0000u) | Cmd16;
                PciCfgWrite32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x04, NewReg);
                return 0;
            }
        case DmaMapBuffer:
        case DmaUnmapBuffer:
            {
                return -1;
            }

        case IntGetMode:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciIntReq* Req = (PciIntReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                unsigned int CapPtrReg = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, 0x34);
                uint8_t      CapPtr    = (uint8_t)(CapPtrReg & 0xFF);
                uint8_t      Ptr       = CapPtr;
                int          Guard     = 0;
                int          Mode      = 0;
                while (Ptr != 0 && Guard < 64)
                {
                    unsigned int CapHdr = PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, Ptr);
                    if (!IsValidCfgValue(CapHdr))
                    {
                        break;
                    }
                    uint8_t Id   = (uint8_t)(CapHdr & 0xFF);
                    uint8_t Next = (uint8_t)((CapHdr >> 8) & 0xFF);
                    if (Id == 0x05)
                    {
                        unsigned int Ctrl =
                            PciCfgRead32(Ctx, Req->Bus, Req->Dev, Req->Func, Ptr + 0x02);
                        Mode = ((Ctrl & 0x1u) != 0) ? 1 : 0;
                        break;
                    }
                    Ptr = Next;
                    Guard++;
                }
                *((int*)__Arg__) = Mode;
                return 0;
            }
        case IntSetMode:
            {
                if (!__Arg__)
                {
                    return -1;
                }
                PciIntReq* Req = (PciIntReq*)__Arg__;
                if (!InRangeU8(Req->Bus) || !InRangeDev(Req->Dev) || !InRangeFunc(Req->Func))
                {
                    return -1;
                }
                PciDevice DevTmp;
                int       Idx = PciFindByBdf(Ctx, Req->Bus, Req->Dev, Req->Func, &DevTmp);
                if (Idx < 0)
                {
                    return -1;
                }
                int Rc = -1;
                if (Req->Mode == 0)
                {
                    Rc = PciEnableMsi(Ctx, &DevTmp, 0);
                }
                if (Req->Mode == 1)
                {
                    Rc = PciEnableMsi(Ctx, &DevTmp, 1);
                }
                if (Rc < 0)
                {
                    return -1;
                }
                Ctx->Devices[Idx] = DevTmp;
                return 0;
            }
        case IntEnable:
        case IntDisable:
            {
                return -1;
            }

        case LinkGetTopology:
        case LinkGetBandwidth:
        case LinkTrain:
            {
                return -1;
            }

        default:
            return -1;
    }
}