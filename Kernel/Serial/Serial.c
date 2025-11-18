#include <Serial.h> /* Serial port constants and register definitions */

/**
 * @brief Initialize the primary serial port (COM1).
 *
 * @details Configures the UART hardware for standard communication:
 * 			Disables interrupts.
 * 			Sets baud rate divisor (default 115200 / 3).
 * 			Configures line control (8 bits, no parity, one stop bit).
 * 			Enables FIFO with 14-byte threshold.
 * 			Sets modem control to enable RTS/DSR.
 *
 * @return void
 *
 * @note This must be called before using any serial output functions.
 */
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
