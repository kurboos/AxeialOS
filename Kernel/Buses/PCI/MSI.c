#include <KHeap.h>
#include <KrnPrintf.h>
#include <PCIBus.h>
#include <String.h>

int
PciEnableMsi(PciDevice* __Device__, uint64_t __Address__, uint32_t __Data__)
{
    if (Probe_IF_Error(__Device__) || !__Device__)
    {
        return -BadArgs;
    }

    if (__Device__->MsiCapOffset == 0)
    {
        return -NoSuch;
    }

    uint16_t MsiControl = PciConfigRead16(
        __Device__->Bus, __Device__->Device, __Device__->Function, __Device__->MsiCapOffset + 2);

    PciConfigWrite32(__Device__->Bus,
                     __Device__->Device,
                     __Device__->Function,
                     __Device__->MsiCapOffset + 4,
                     (uint32_t)__Address__);

    /*Check if 64-bit capable*/
    if (MsiControl & (1 << 7))
    {
        /*64-bit MSI*/
        PciConfigWrite32(__Device__->Bus,
                         __Device__->Device,
                         __Device__->Function,
                         __Device__->MsiCapOffset + 8,
                         (uint32_t)(__Address__ >> 32));
        PciConfigWrite16(__Device__->Bus,
                         __Device__->Device,
                         __Device__->Function,
                         __Device__->MsiCapOffset + 12,
                         (uint16_t)__Data__);
    }
    else
    {
        /*32-bit MSI*/
        PciConfigWrite16(__Device__->Bus,
                         __Device__->Device,
                         __Device__->Function,
                         __Device__->MsiCapOffset + 8,
                         (uint16_t)__Data__);
    }

    /*Enable MSI*/
    MsiControl |= (1 << 0);
    PciConfigWrite16(__Device__->Bus,
                     __Device__->Device,
                     __Device__->Function,
                     __Device__->MsiCapOffset + 2,
                     MsiControl);

    PDebug("Enabled MSI for device %02x:%02x.%x\n",
           __Device__->Bus,
           __Device__->Device,
           __Device__->Function);
    return SysOkay;
}

int
PciDisableMsi(PciDevice* __Device__)
{
    if (Probe_IF_Error(__Device__) || !__Device__)
    {
        return -BadArgs;
    }

    if (__Device__->MsiCapOffset == 0)
    {
        return -NoSuch;
    }

    uint16_t MsiControl = PciConfigRead16(
        __Device__->Bus, __Device__->Device, __Device__->Function, __Device__->MsiCapOffset + 2);

    /*Disable MSI*/
    MsiControl &= ~(1 << 0);
    PciConfigWrite16(__Device__->Bus,
                     __Device__->Device,
                     __Device__->Function,
                     __Device__->MsiCapOffset + 2,
                     MsiControl);

    return SysOkay;
}
