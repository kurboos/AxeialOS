#pragma once

/*





    ONE HUGE NOTE: This driver is deprecated because of the kernel rewrite!





*/

#include <APICTimer.h>
#include <AllTypes.h>
#include <AxeSchd.h>
#include <AxeThreads.h>
#include <BootConsole.h>
#include <BootImg.h>
#include <CharBus.h>
#include <DevFS.h>
#include <EarlyBootFB.h>
#include <GDT.h>
#include <IDT.h>
#include <KExports.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <LimineServices.h>
#include <ModELF.h>
#include <ModMemMgr.h>
#include <PMM.h>
#include <POSIXFd.h>
#include <POSIXProc.h>
#include <POSIXProcFS.h>
#include <POSIXSignals.h>
#include <SMP.h>
#include <Serial.h>
#include <SymAP.h>
#include <Sync.h>
#include <Syscall.h>
#include <Timer.h>
#include <VFS.h>
#include <VMM.h>

extern PciCtrlCtx* PciCtxHeap;
extern CharBus*    PciBus;
extern uint64_t    PciCanary;

int          InRangeU8(int __Value__);
int          InRangeDev(int __Value__);
int          InRangeFunc(int __Value__);
int          ValidBuf(const void* __Ptr__, int __Len__);
int          ValidCfgWindow(int __Off__, int __Len__);
int          NonZeroVidDid(uint16_t __Vid__, uint16_t __Did__);
void         GuardCtx(PciCtrlCtx* __Ctx__);
int          IsCtxSane(const PciCtrlCtx* __Ctx__);
unsigned int PciMakeCfgAddr(int __Bus__, int __Dev__, int __Func__, int __Off__);
void         PciOut32(unsigned short __Port__, unsigned int __Val__);
unsigned int PciIn32(unsigned short __Port__);
int          IsValidCfgValue(unsigned int __Val__);
uint32_t
     PciEcamLoad(const PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Off__);
void PciEcamStore(const PciCtrlCtx* __Ctx__,
                  int               __Bus__,
                  int               __Dev__,
                  int               __Func__,
                  int               __Off__,
                  uint32_t          __Val__);
unsigned int PciCfgRead32Legacy(int __Bus__, int __Dev__, int __Func__, int __Off__);
void PciCfgWrite32Legacy(int __Bus__, int __Dev__, int __Func__, int __Off__, unsigned int __Val__);
unsigned int PciCfgRead32(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Off__);
void         PciCfgWrite32(
            PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Off__, unsigned int __Val__);
int      PciCfgRead(PciCtrlCtx* __Ctx__,
                    int         __Bus__,
                    int         __Dev__,
                    int         __Func__,
                    int         __Off__,
                    void*       __Buf__,
                    int         __Len__);
int      PciCfgWrite(PciCtrlCtx* __Ctx__,
                     int         __Bus__,
                     int         __Dev__,
                     int         __Func__,
                     int         __Off__,
                     const void* __Buf__,
                     int         __Len__);
uint32_t PciReadBarRaw(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, int __Index__);
uint32_t PciSizeBar(PciCtrlCtx* __Ctx__,
                    int         __Bus__,
                    int         __Dev__,
                    int         __Func__,
                    int         __Index__,
                    uint32_t    __BarVal__);
void     PciCollectBars(PciCtrlCtx* __Ctx__, PciDevice* __Dev__);
uint8_t
     PciFindNextCap(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, uint8_t __Start__);
void PciCollectCaps(PciCtrlCtx* __Ctx__, PciDevice* __Dev__);
void PciEnableBmIoMem(PciCtrlCtx* __Ctx__, PciDevice* __Dev__, int __Enable__);
int  PciSetPowerState(PciCtrlCtx* __Ctx__, PciDevice* __Dev__, int __DState__);
int  PciEnableMsi(PciCtrlCtx* __Ctx__, PciDevice* __Dev__, int __Enable__);
int  PciProbeFunc(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__);
void PciScanBus(PciCtrlCtx* __Ctx__, int __Bus__);
int  PciEnumerate(PciCtrlCtx* __Ctx__);
int  PciFindByBdf(PciCtrlCtx* __Ctx__, int __Bus__, int __Dev__, int __Func__, PciDevice* __Out__);
int  PciFindByVendor(
     PciCtrlCtx* __Ctx__, uint16_t __Vid__, uint16_t __Did__, int __Index__, PciDevice* __Out__);
int  PciOpen(void* __CtrlCtx__);
int  PciClose(void* __CtrlCtx__);
long PciRead(void* __CtrlCtx__, void* __Buf__, long __Len__);
long PciWrite(void* __CtrlCtx__, const void* __Buf__, long __Len__);
int  PciIoctl(void* __CtrlCtx__, unsigned long __Cmd__, void* __Arg__);
int  PciInitContext(PciCtrlCtx** __OutCtx__);
void PciFreeContext(PciCtrlCtx* __Ctx__);
int  module_init(void);
int  module_exit(void);
