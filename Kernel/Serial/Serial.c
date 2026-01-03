#include <Serial.h>

void
InitializeSerial(void)
{
    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0x00), "Nd"((uint16_t)(SerialPort1 + SerialIntEnableReg)));

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0x80), "Nd"((uint16_t)(SerialPort1 + SerialLineCtrlReg)));

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0x03), "Nd"((uint16_t)(SerialPort1 + SerialDataReg)));

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0x00), "Nd"((uint16_t)(SerialPort1 + SerialIntEnableReg)));

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0x03), "Nd"((uint16_t)(SerialPort1 + SerialLineCtrlReg)));

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0xC7), "Nd"((uint16_t)(SerialPort1 + SerialFifoCtrlReg)));

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)0x0B), "Nd"((uint16_t)(SerialPort1 + SerialModemCtrlReg)));
}
