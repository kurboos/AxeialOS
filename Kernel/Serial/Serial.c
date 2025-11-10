#include <Serial.h>  /* Serial port constants and register definitions */

/*
 * InitializeSerial - Initialize the Primary Serial Port (COM1)
 *
 * Initializes the serial port hardware for kernel debugging and communication.
 * The initialization configures the UART with standard settings suitable for
 * kernel logging and debugging output. This includes setting the baud rate,
 * data format, FIFO buffers, and modem control signals.
 *
 * The configuration process includes:
 * 1. Disabling interrupts during setup to prevent spurious interrupts.
 * 2. Setting the baud rate divisor for 38400 baud communication.
 * 3. Configuring 8N1 data format (8 bits, no parity, 1 stop bit).
 * 4. Enabling and configuring FIFO buffers with 14-byte threshold.
 * 5. Setting modem control signals (RTS/DSR) and enabling IRQs.
 *
 * Note: This function initializes SerialPort1 (COM1, I/O port 0x3F8).
 * Additional serial ports would require separate initialization calls.
 */
void
InitializeSerial(void)
{
    /*
     * Disable interrupts during UART configuration to prevent
     * spurious interrupts from uninitialized hardware.
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)(SerialPort1 + SerialIntEnableReg)));

    /*
     * Set baud rate to 38400.
     * First, set the Divisor Latch Access Bit (DLAB) in Line Control Register.
     * Then write the divisor values: 0x03 (low byte) and 0x00 (high byte).
     * Divisor = 115200 / 38400 = 3 (0x0003)
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x80), "Nd"((uint16_t)(SerialPort1 + SerialLineCtrlReg)));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x03), "Nd"((uint16_t)(SerialPort1 + SerialDataReg)));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)(SerialPort1 + SerialIntEnableReg)));

    /*
     * Configure line parameters: 8 data bits, no parity, 1 stop bit.
     * Clear DLAB and set word length to 8 bits.
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x03), "Nd"((uint16_t)(SerialPort1 + SerialLineCtrlReg)));

    /*
     * Enable FIFO buffers with 14-byte interrupt threshold.
     * 0xC7 = 11000111b:
     * - Bit 7: FIFO enable
     * - Bit 6: Clear receive FIFO
     * - Bit 5: Clear transmit FIFO
     * - Bits 4-0: 14-byte threshold (0111)
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xC7), "Nd"((uint16_t)(SerialPort1 + SerialFifoCtrlReg)));

    /*
     * Configure modem control register.
     * 0x0B = 00001011b:
     * - Bit 3: Enable IRQs
     * - Bit 1: Request to Send (RTS)
     * - Bit 0: Data Terminal Ready (DTR)
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0B), "Nd"((uint16_t)(SerialPort1 + SerialModemCtrlReg)));
}
