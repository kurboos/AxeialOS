#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <KrnPrintf.h>
#include <SMP.h>

#define MaxGdt                   8 /*Base + 2 entries per CPU TSS*/
#define GetCpuTssSelector(cpuid) ((GdtTssIndex + ((cpuid) * 2)) * 8) /*First TSS at index 5*/
#define GdtNullIndex             0
#define GdtKernelCodeIndex       1
#define GdtKernelDataIndex       2
#define GdtUserDataIndex         3
#define GdtUserCodeIndex         4
#define GdtTssIndex              5

#define GdtAccessNull         0x00
#define GdtAccessKernelCode64 0x9A /*Present, Ring 0, Code, Readable*/
#define GdtAccessKernelData64 0x92 /*Present, Ring 0, Data, Writable*/
#define GdtAccessUserData64   0xF2 /*Present, Ring 3, Data, Writable*/
#define GdtAccessUserCode64   0xFA /*Present, Ring 3, Code, Readable*/
#define GdtAccessTss64        0x89 /*Present, Ring 0, TSS Available*/

#define GdtGranNull   0x00
#define GdtGranCode64 0x20 /*L-bit set for 64-bit code*/
#define GdtGranData64 0x00 /*No special flags for data*/
#define GdtGranTss64  0x00 /*No granularity for TSS*/

#define GdtBaseIgnored  0 /*Base ignored in 64-bit mode*/
#define GdtLimitIgnored 0 /*Limit ignored in 64-bit mode*/

#define GdtSegmentReloadValue 0x10 /*Kernel data selector*/
#define GdtKernelCodePush     0x08 /*Kernel code selector*/

typedef struct
{
    uint16_t LimitLow;
    uint16_t BaseLow;
    uint8_t  BaseMiddle;
    uint8_t  Access;
    uint8_t  Granularity;
    uint8_t  BaseHigh;
} __attribute__((packed)) GdtEntry;

typedef struct
{
    uint16_t Limit;
    uint64_t Base;
} __attribute__((packed)) GdtPointer;

typedef struct
{
    uint32_t Reserved0;
    uint64_t Rsp0;
    uint64_t Rsp1;
    uint64_t Rsp2;
    uint64_t Reserved1;
    uint64_t Ist1;
    uint64_t Ist2;
    uint64_t Ist3;
    uint64_t Ist4;
    uint64_t Ist5;
    uint64_t Ist6;
    uint64_t Ist7;
    uint64_t Reserved2;
    uint16_t Reserved3;
    uint16_t IoMapBase;
} __attribute__((packed)) TaskStateSegment;

extern GdtEntry         GdtEntries[MaxGdt];
extern GdtPointer       GdtPtr;
extern TaskStateSegment Tss;

#define KernelCodeSelector 0x08
#define KernelDataSelector 0x10
#define UserDataSelector   0x1B
#define UserCodeSelector   0x23
#define TssSelector        0x28

void SetGdtEntry(int      __Index__,
                 uint32_t __Base__,
                 uint32_t __Limit__,
                 uint8_t  __Access__,
                 uint8_t  __Granularity__,
                 SysErr*  __Err__);
void InitializeGdt(SysErr* __Err__);
void SetTssEntry(int __Index__, uint64_t __Base__, uint32_t __Limit__, SysErr* __Err__);
void InitializeTss(SysErr* __Err__);
