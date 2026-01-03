#include <KHeap.h>
#include <KrnPrintf.h>
#include <PCIBus.h>
#include <String.h>

void
PciDumpDevice(PciDevice* __Device__, SysErr* __Err__)
{
    if (Probe_IF_Error(__Device__) || !__Device__)
    {
        SlotError(__Err__, -BadArgs);
        return;
    }

    PInfo("PCI Device %02x:%02x.%x\n", __Device__->Bus, __Device__->Device, __Device__->Function);
    PInfo("  Vendor: %04x, Device: %04x\n", __Device__->VendorId, __Device__->DeviceId);
    PInfo("  Class: %02x, SubClass: %02x, ProgIf: %02x\n",
          __Device__->ClassCode,
          __Device__->SubClass,
          __Device__->ProgInterface);
    PInfo("  Command: %04x, Status: %04x\n", __Device__->Command, __Device__->Status);

    for (uint8_t BarIndex = 0; BarIndex < 6; BarIndex++)
    {
        if (__Device__->Bars[BarIndex] != 0)
        {
            PInfo("  BAR%u: %016llx (Size: %016llx, Type: %u)\n",
                  BarIndex,
                  __Device__->Bars[BarIndex],
                  __Device__->BarSizes[BarIndex],
                  __Device__->BarTypes[BarIndex]);
        }
    }
}

void
PciDumpAllDevices(SysErr* __Err__)
{
    if (!PciBus.Initialized)
    {
        SlotError(__Err__, -NotInit);
        return;
    }

    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&PciBus.BusLock, Error);

    for (uint32_t DeviceIndex = 0; DeviceIndex < PciBus.DeviceCount; DeviceIndex++)
    {
        PciDumpDevice(&PciBus.Devices[DeviceIndex], __Err__);
    }

    ReleaseSpinLock(&PciBus.BusLock, Error);

    PInfo("Total devices: %u\n", PciBus.DeviceCount);
}