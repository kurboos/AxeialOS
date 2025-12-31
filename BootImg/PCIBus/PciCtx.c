#include "Pci.h"

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

int
PciInitContext(PciCtrlCtx** __OutCtx__)
{
    if (!__OutCtx__)
    {
        return -1;
    }
    if (PciCanary != 0xA55AC0DECAFEBABEULL)
    {
        return -1;
    }

    PciCtrlCtx* Ctx = (PciCtrlCtx*)KMalloc(sizeof(PciCtrlCtx));
    if (!Ctx)
    {
        return -1;
    }
    memset(Ctx, 0, sizeof(PciCtrlCtx));

    uint32_t InitCap = 128u;
    size_t   InitSz  = sizeof(PciDevice) * InitCap;

    Ctx->Devices = (PciDevice*)KMalloc(InitSz);
    if (!Ctx->Devices)
    {
        KFree(Ctx);
        return -1;
    }
    memset(Ctx->Devices, 0, InitSz);
    Ctx->DevCap         = InitCap;
    Ctx->DevCount       = 0;
    Ctx->UseEcam        = 0;
    Ctx->EcamBase       = 0;
    Ctx->EcamStrideBus  = 4096u * 32u * 8u;
    Ctx->EcamStrideDev  = 4096u * 8u;
    Ctx->EcamStrideFunc = 4096u;
    Ctx->EcamStrideOff  = 1u;

    if (!IsCtxSane(Ctx))
    {
        KFree(Ctx->Devices);
        KFree(Ctx);
        return -1;
    }

    *(__OutCtx__) = Ctx;
    return PciEnumerate(Ctx);
}

void
PciFreeContext(PciCtrlCtx* __Ctx__)
{
    if (!__Ctx__)
    {
        return;
    }
    if (__Ctx__->Devices)
    {
        KFree(__Ctx__->Devices);
    }
    __Ctx__->Devices  = NULL;
    __Ctx__->DevCount = 0;
    __Ctx__->DevCap   = 0;
    KFree(__Ctx__);
}