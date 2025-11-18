#include <AllTypes.h>

/**
 * @brief Align a value to a power-of-two boundary
 *
 * Calculates the smallest value greater than or equal to __Value__ that is
 * a multiple of __Align__. Used for CPIO record alignment requirements where
 * names and data must be aligned to 4-byte boundaries.
 *
 * @param __Value__ Input value to align (byte offset)
 * @param __Align__ Alignment boundary in bytes (must be power of 2, typically 4)
 * @return Aligned value (next multiple of __Align__)
 *
 * @note Uses bitwise operations for efficiency
 * @note Assumes __Align__ is a power of 2
 */
uint32_t
CpioAlignUp(uint32_t __Value__, uint32_t __Align__)
{
    return (__Value__ + (__Align__ - 1)) & ~(__Align__ - 1);
}

/**
 * @brief Parse an 8-character ASCII hexadecimal string into uint32
 *
 * Converts a fixed-width 8-character ASCII hexadecimal string to a 32-bit
 * unsigned integer. Used for parsing CPIO "newc" header fields which store
 * all numeric values as 8-character ASCII hex strings.
 *
 * @param __Hex__ Pointer to exactly 8 ASCII hex digits (0-9, a-f, A-F)
 * @return Parsed 32-bit unsigned integer value
 *
 * @note Case-insensitive: accepts both uppercase and lowercase hex digits
 * @note No error checking - assumes valid input as per CPIO specification
 * @note CPIO "newc" format requires exactly 8 characters per numeric field
 */
uint32_t
CpioParseHex(const char* __Hex__)
{
    uint32_t Value = 0;
    for (uint32_t Index = 0; Index < 8; Index++)
    {
        char     C = __Hex__[Index];
        uint32_t D = 0;

        if (C >= '0' && C <= '9')
        {
            D = (uint32_t)(C - '0');
        }
        else if (C >= 'a' && C <= 'f')
        {
            D = (uint32_t)(C - 'a' + 10);
        }
        else if (C >= 'A' && C <= 'F')
        {
            D = (uint32_t)(C - 'A' + 10);
        }

        Value = (Value << 4) | D;
    }
    return Value;
}
