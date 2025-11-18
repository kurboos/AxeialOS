#pragma once

#include <AllTypes.h>
#include <DevFS.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <String.h>
#include <VFS.h>

/** @note Only diffrence between each subchar device will be the protocols followed for ioctl or
 * anything. */

/**
 * Enums
 */
typedef enum CharIoProtocol
{
    /* Subsystem tags (upper 16 bits, when used alone for grouping) */
    CHARIOC_PCI        = 0x0001,
    CHARIOC_USB        = 0x0002,
    CHARIOC_NET        = 0x0003,
    CHARIOC_TTY        = 0x0004,
    CHARIOC_SENSOR     = 0x0005,
    CHARIOC_STORAGECTL = 0x0006,
    CHARIOC_GENERIC    = 0x00FF,
    PCI_GET_COUNT      = 0x0001, /* returns long* count */
    PCI_GET_DEVICE     = 0x0002, /* in: {Bus,Dev,Func}, out: PciDevice */
    PCI_GET_VD         = 0x0003, /* in: {VendorId,DeviceId,Index}, out: PciDevice */
    PCI_ENABLE_BM      = 0x0004, /* in: {Bus,Dev,Func,Enable} -> int */
    PCI_RESYNC_CACHE   = 0x0005, /* no args -> int */
    PCI_READ_CFG       = 0x0006, /* in: {Bus,Dev,Func,Off,Len}, out: bytes */
    PCI_WRITE_CFG      = 0x0007, /* in: {Bus,Dev,Func,Off,Len,DataPtr} -> int */
    USB_GET_DEVCOUNT   = 0x0001, /* returns long* count */
    USB_GET_DEVINFO    = 0x0002, /* in: {Index}, out: UsbDevInfo */
    USB_CTRL_XFER      = 0x0003, /* in: UsbCtrlReq*, -> int */
    USB_BULK_XFER      = 0x0004, /* in: UsbBulkReq*, -> int */
    NET_GET_IFCOUNT    = 0x0001, /* returns long* count */
    NET_GET_IFINFO     = 0x0002, /* in: {Index}, out: NetIfInfo */
    NET_SET_MAC        = 0x0003, /* in: {Index, MAC[6]} -> int */
    NET_UP             = 0x0004, /* in: {Index} -> int */
    NET_DOWN           = 0x0005, /* in: {Index} -> int */
    NET_TX             = 0x0006, /* in: {Index, Buf, Len} -> int */
    NET_RX             = 0x0007, /* in: {Index, Buf, Cap} -> int */
    TTY_SET_BAUD       = 0x0001, /* in: {Baud} -> int */
    TTY_SET_MODE       = 0x0002, /* in: {Databits,Parity,Stop} -> int */
    TTY_GET_STATUS     = 0x0003, /* out: TtyStatus */
    TTY_FLUSH          = 0x0004, /* -> int */
    SENSOR_GET_COUNT   = 0x0001, /* returns long* count */
    SENSOR_GET_INFO    = 0x0002, /* in: {Index}, out: SensorInfo */
    SENSOR_READ_VALUE  = 0x0003, /* in: {Index}, out: SensorValue */
    SCTL_GET_ADAPTERS  = 0x0001, /* returns long* count */
    SCTL_GET_INFO      = 0x0002, /* in: {Index}, out: ScsiCtlInfo */
    SCTL_RESET_BUS     = 0x0003, /* -> int */
    GEN_PING           = 0x0001, /* in/out: optional payload -> int */
    GEN_GET_VERSION    = 0x0002, /* out: {Major,Minor,Patch} */
    GEN_GET_CAPS       = 0x0003  /* out: bitmask */
} CharIoProtocol;

/**
 * Structures
 */

/** Char bus desc (physical/logical char device) */
typedef struct CharBus
{
    const char* Name;    /* e.g., "pci", "ttyS0", "hid0" */
    void*       CtrlCtx; /* driver/controller-private context */
    CharDevOps  Ops;     /* driver ops (filled by caller) */

} CharBus;

/**
 * Functions
 */
int CharRegisterBus(CharBus* __Bus__, int __Major__, int __Minor__);
int CharMakeName(char* __Out__, long __Cap__, const char* __Prefix__, long __Index__);
int CharMakeSubName(char* __Out__, long __Cap__, const char* __Base__, long __SubIndex__);

/**
 * Public
 */
KEXPORT(CharRegisterBus);
KEXPORT(CharMakeName);
KEXPORT(CharMakeSubName);