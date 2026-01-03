#include <Serial.h>

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

void
SerialPutString(const char* __String__)
{
    while (*__String__)
    {
        SerialPutChar(*__String__);
        __String__++;
    }
}
