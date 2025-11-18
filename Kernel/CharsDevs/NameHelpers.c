/**
 * @brief Construct a character device name with a numeric index.
 *
 * Builds a device name string by concatenating a prefix with a decimal index.
 * For example, given prefix "tty" and index 0, the output will be "tty0".
 *
 * The function writes the result into the provided output buffer, ensuring
 * that the string is null-terminated if there is sufficient capacity.
 *
 * @param __Out__   Pointer to the output buffer where the name will be written.
 * @param __Cap__   Capacity of the output buffer in bytes.
 * @param __Prefix__ Prefix string to use (e.g., "tty", "hid").
 * @param __Index__  Numeric index to append to the prefix.
 *
 * @return Number of characters written (excluding the null terminator),
 *         or -1 if the buffer is invalid or insufficient.
 *
 * @note The output buffer must be large enough to hold the prefix, the
 *       decimal representation of the index, and the terminating null byte.
 *       If the buffer is too small, the function returns -1 without writing.
 */
int
CharMakeName(char* __Out__, long __Cap__, const char* __Prefix__, long __Index__)
{
    if (!__Out__ || !__Prefix__ || __Cap__ <= 0)
    {
        return -1;
    }
    long Pos = 0;
    while (__Prefix__[Pos] && Pos < __Cap__)
    {
        __Out__[Pos] = __Prefix__[Pos];
        Pos++;
    }
    if (Pos >= __Cap__)
    {
        return -1;
    }

    /* Append decimal index */
    char          Tmp[20];
    int           Len = 0;
    unsigned long N   = (unsigned long)__Index__;
    if (N == 0)
    {
        if (Pos >= __Cap__)
        {
            return -1;
        }
        __Out__[Pos++] = '0';
    }
    else
    {
        while (N > 0 && Len < (int)sizeof(Tmp))
        {
            Tmp[Len++] = (char)('0' + (N % 10));
            N /= 10;
        }
        if (Pos + Len > __Cap__)
        {
            return -1;
        }
        for (int I = Len - 1; I >= 0; --I)
        {
            __Out__[Pos++] = Tmp[I];
        }
    }
    if (Pos < __Cap__)
    {
        __Out__[Pos] = '\0';
    }
    return (int)Pos;
}

/**
 * @brief Construct a subdevice name from a base name and subindex.
 *
 * Builds a subdevice name string by appending a decimal subindex to a base
 * device name. For example, given base "hid" and subindex 1, the output
 * will be "hid1".
 *
 * This function is a thin wrapper around CharMakeName, reusing its logic
 * for buffer handling and index formatting.
 *
 * @param __Out__      Pointer to the output buffer where the name will be written.
 * @param __Cap__      Capacity of the output buffer in bytes.
 * @param __Base__     Base device name string (e.g., "hid", "pci").
 * @param __SubIndex__ Numeric subindex to append to the base name.
 *
 * @return Number of characters written (excluding the null terminator),
 *         or -1 if the buffer is invalid or insufficient.
 *
 * @see CharMakeName
 */
int
CharMakeSubName(char* __Out__, long __Cap__, const char* __Base__, long __SubIndex__)
{
    /* base + decimal subindex (e.g., "hid" + 1 -> "hid1") */
    return CharMakeName(__Out__, __Cap__, __Base__, __SubIndex__);
}