#pragma once

/**
 * APIC Constants
 */
#define TimerApicBaseMsr             0x1B
#define TimerApicBaseEnable          (1ULL << 11)
#define TimerApicRegVersion          0x030
#define TimerApicRegSpuriousInt      0x0F0
#define TimerApicRegLvtTimer         0x320
#define TimerApicRegTimerInitCount   0x380
#define TimerApicRegTimerCurrCount   0x390
#define TimerApicRegTimerDivide      0x3E0
#define TimerApicRegEoi              0x0B0
#define TimerApicTimerPeriodic       (1 << 17)
#define TimerApicTimerMasked         (1 << 16)
#define TimerApicTimerDivideBy16     0x03