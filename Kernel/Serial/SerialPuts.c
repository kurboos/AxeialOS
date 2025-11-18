#include <Serial.h> /* Serial port constants and register definitions */

/**
 * @brief Send a single character over the serial port.
 *
 * @details Waits until the transmitter holding register is empty,
 * 			then writes the character to the serial data register.
 *
 * @param __Char__ Character to send.
 *
 * @return void
 *
 * @note Blocks until the character can be transmitted.
 */
void
SerialPutChar(char __Char__)
{

    uint8_t Status;
    do
    {
        __asm__ volatile("inb %1, %0"
                         : "=a"(Status)
                         : "Nd"((uint16_t)(SerialPort1 + SerialLineStatusReg)));
    } while ((Status & 0x20) == 0);

    __asm__ volatile("outb %0, %1"
                     :
                     : "a"((uint8_t)__Char__), "Nd"((uint16_t)(SerialPort1 + SerialDataReg)));
}

/**
 * @brief Send a null-terminated string over the serial port.
 *
 * @details Iterates through the string and sends each character
 * 			using SerialPutChar until the null terminator is reached.
 *
 * @param __String__ Pointer to the string to send.
 *
 * @return void
 *
 * @note Useful for logging and debugging messages.
 */
void
SerialPutString(const char* __String__)
{

    while (*__String__)
    {
        SerialPutChar(*__String__);
        __String__++;
    }
}
