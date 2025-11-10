#include <Serial.h>  /* Serial port constants and register definitions */

/*
 * SerialPutChar - Transmit a Single Character via Serial Port
 *
 * Transmits a single character through the serial port (COM1). The function
 * waits for the UART's transmit buffer to be empty before sending the character,
 * ensuring that data is not lost due to buffer overflow.
 *
 * Parameters:
 * - __Char__: The character to transmit.
 *
 * Note: This function uses busy-waiting on the Line Status Register's
 * Transmitter Holding Register Empty (THRE) bit (bit 5) to ensure the
 * previous character has been transmitted before sending the next one.
 */
void
SerialPutChar(char __Char__)
{
    /*
     * Poll the Line Status Register until the transmit buffer is empty.
     * Bit 5 (0x20) indicates Transmitter Holding Register Empty (THRE).
     */
    uint8_t Status;
    do {
        __asm__ volatile("inb %1, %0" : "=a"(Status) : "Nd"((uint16_t)(SerialPort1 + SerialLineStatusReg)));
    } while ((Status & 0x20) == 0);

    /*
     * Transmit the character by writing to the Data Register.
     */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)__Char__), "Nd"((uint16_t)(SerialPort1 + SerialDataReg)));
}

/*
 * SerialPutString - Transmit a Null-Terminated String via Serial Port
 *
 * Transmits a complete null-terminated string through the serial port by
 * iteratively calling SerialPutChar for each character in the string.
 *
 * Parameters:
 * - __String__: Pointer to the null-terminated string to transmit.
 *
 * Note: The function stops transmission when it encounters the null terminator
 * ('\0'). It assumes the input string is properly null-terminated.
 */
void
SerialPutString(const char* __String__)
{
    /*
     * Iterate through each character in the string until null terminator.
     */
    while (*__String__)
    {
        SerialPutChar(*__String__);
        __String__++;
    }
}
