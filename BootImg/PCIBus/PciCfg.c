#include "Pci.h"
#include <EveryType.h>

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

unsigned int
PciMakeCfgAddr(int __Bus__, int __Dev__, int __Func__, int __Off__)
{
    return (unsigned int)(0x80000000u | ((unsigned int)__Bus__ << 16) |
                          ((unsigned int)__Dev__ << 11) | ((unsigned int)__Func__ << 8) |
                          ((unsigned int)(__Off__ & ~3)));
}

void
PciOut32(unsigned short __Port__, unsigned int __Val__)
{
    __asm__ volatile("outl %0, %1" : : "a"(__Val__), "Nd"(__Port__));
}

unsigned int
PciIn32(unsigned short __Port__)
{
    unsigned int Ret;
    __asm__ volatile("inl %1, %0" : "=a"(Ret) : "Nd"(__Port__));
    return Ret;
}

int
IsValidCfgValue(unsigned int __Val__)
{
    return (__Val__ != 0xFFFFFFFFu && __Val__ != 0x00000000u);
}

uint32_t
PciEcamLoad(const PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Off__)
{
    uint64_t Base = __Ctx__->EcamBase;
    uint64_t Addr = Base + (uint64_t)__Bus__ * __Ctx__->EcamStrideBus +
                    (uint64_t)__Dev__ * __Ctx__->EcamStrideDev +
                    (uint64_t)__Func__ * __Ctx__->EcamStrideFunc +
                    (uint64_t)(__Off__ & ~3) * __Ctx__->EcamStrideOff;
    return *((volatile uint32_t*)Addr);
}

void
PciEcamStore(const PciCtrlCtx* __Ctx__,
             int               __Bus__,
             int               __Dev__,
             int               __Func__,
             int               __Off__,
             uint32_t          __Val__)
{
    uint64_t Base = __Ctx__->EcamBase;
    uint64_t Addr = Base + (uint64_t)__Bus__ * __Ctx__->EcamStrideBus +
                    (uint64_t)__Dev__ * __Ctx__->EcamStrideDev +
                    (uint64_t)__Func__ * __Ctx__->EcamStrideFunc +
                    (uint64_t)(__Off__ & ~3) * __Ctx__->EcamStrideOff;
    *((volatile uint32_t*)Addr) = __Val__;
}

unsigned int
PciCfgRead32Legacy(int __Bus__, int __Dev__, int __Func__, int __Off__)
{
    if (!InRangeU8(__Bus__) || !InRangeDev(__Dev__) || !InRangeFunc(__Func__) || __Off__ < 0 ||
        __Off__ >= 256)
    {
        return 0xFFFFFFFFu;
    }
    unsigned int Addr = PciMakeCfgAddr(__Bus__, __Dev__, __Func__, __Off__);
    PciOut32(0xCF8, Addr);
    return PciIn32(0xCFC);
}

void
PciCfgWrite32Legacy(int __Bus__, int __Dev__, int __Func__, int __Off__, unsigned int __Val__)
{
    if (!InRangeU8(__Bus__) || !InRangeDev(__Dev__) || !InRangeFunc(__Func__) || __Off__ < 0 ||
        __Off__ >= 256)
    {
        return;
    }
    unsigned int Addr = PciMakeCfgAddr(__Bus__, __Dev__, __Func__, __Off__);
    PciOut32(0xCF8, Addr);
    PciOut32(0xCFC, __Val__);
}

unsigned int
PciCfgRead32(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Off__)
{
    if (__Ctx__->UseEcam)
    {
        return PciEcamLoad(__Ctx__, __Bus__, __Dev__, __Func__, __Off__);
    }
    return PciCfgRead32Legacy(__Bus__, __Dev__, __Func__, __Off__);
}

void
PciCfgWrite32(
    PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Off__, unsigned int __Val__)
{
    if (__Ctx__->UseEcam)
    {
        PciEcamStore(__Ctx__, __Bus__, __Dev__, __Func__, __Off__, __Val__);
        return;
    }
    PciCfgWrite32Legacy(__Bus__, __Dev__, __Func__, __Off__, __Val__);
}

int
PciCfgRead(PciCtrlCtx* __Ctx__,
           int         __Bus__,
           int         __Dev__,
           int         __Func__,
           int         __Off__,
           void*       __Buf__,
           int         __Len__)
{
    if (!InRangeU8(__Bus__) || !InRangeDev(__Dev__) || !InRangeFunc(__Func__))
    {
        return -1;
    }
    if (!ValidBuf(__Buf__, __Len__))
    {
        return -1;
    }
    if (!ValidCfgWindow(__Off__, __Len__))
    {
        return -1;
    }

    int            Done = 0;
    unsigned char* Dst  = (unsigned char*)__Buf__;
    while (Done < __Len__)
    {
        int          Cur  = __Off__ + Done;
        unsigned int Word = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, (Cur & ~3));
        if (Word == 0xFFFFFFFFu)
        {
            return -1;
        }
        int Shift = (Cur & 3) * 8;
        Dst[Done] = (unsigned char)((Word >> Shift) & 0xFF);
        Done++;
    }
    return Done;
}

int
PciCfgWrite(PciCtrlCtx* __Ctx__,
            int         __Bus__,
            int         __Dev__,
            int         __Func__,
            int         __Off__,
            const void* __Buf__,
            int         __Len__)
{
    if (!InRangeU8(__Bus__) || !InRangeDev(__Dev__) || !InRangeFunc(__Func__))
    {
        return -1;
    }
    if (!ValidBuf(__Buf__, __Len__))
    {
        return -1;
    }
    if (!ValidCfgWindow(__Off__, __Len__))
    {
        return -1;
    }

    int                  Done = 0;
    const unsigned char* Src  = (const unsigned char*)__Buf__;
    while (Done < __Len__)
    {
        int          Cur  = __Off__ + Done;
        unsigned int Word = PciCfgRead32(__Ctx__, __Bus__, __Dev__, __Func__, (Cur & ~3));
        if (Word == 0xFFFFFFFFu)
        {
            return -1;
        }
        int          Shift     = (Cur & 3) * 8;
        unsigned int ClearMask = ~(0xFFu << Shift);
        unsigned int Patch     = ((unsigned int)Src[Done]) << Shift;
        Word                   = (Word & ClearMask) | Patch;
        PciCfgWrite32(__Ctx__, __Bus__, __Dev__, __Func__, (Cur & ~3), Word);
        Done++;
    }
    return Done;
}