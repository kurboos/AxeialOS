#pragma once

#include <EveryType.h>

/**
 * Constants
 */
#define IdtTypeInterruptGate 0x8E
#define IdtTypeTrapGate      0x8F

/**
 * Functions
 */
void SetIdtEntry(int __Index__, uint64_t __Handler__, uint16_t __Selector__, uint8_t __Flags__);