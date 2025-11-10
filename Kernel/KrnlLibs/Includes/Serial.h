#pragma once

#include <AllTypes.h>

/**
 * Serial Port Constants
 */
#define SerialPort1         0x3F8
#define SerialDataReg       0
#define SerialIntEnableReg  1
#define SerialFifoCtrlReg   2
#define SerialLineCtrlReg   3
#define SerialModemCtrlReg  4
#define SerialLineStatusReg 5

/**
 * Functions
 */
void InitializeSerial(void);
void SerialPutChar(char __Char__);
void SerialPutString(const char* __String__);
