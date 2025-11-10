#include <KrnPrintf.h>
#include <Serial.h>

/*
 * KrnPrintf - Main Kernel Printf Function
 *
 * Thread-safe formatted output to the console. Processes format specifiers
 * and outputs the result through PutChar, which automatically handles
 * serial mirroring, scrolling, and cursor management.
 *
 * Format specifiers supported:
 * - %d, %i: Signed decimal integers
 * - %u: Unsigned decimal integers
 * - %x, %X: Hexadecimal (lowercase/uppercase)
 * - %o: Octal
 * - %b: Binary
 * - %s: Null-terminated strings
 * - %c: Single characters
 * - %p: Pointers (hex with 0x prefix)
 * - %%: Literal percent sign
 *
 * Parameters:
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock.
 */
void
KrnPrintf(const char *__Format__, ...)
{
    AcquireSpinLock(&ConsoleLock);
    __builtin_va_list args;
    __builtin_va_start(args, __Format__);

    while (*__Format__)
    {
        if (*__Format__ == '%')
        {
            __Format__++;
            ProcessFormatSpecifier(&__Format__, &args);
        }
        else
        {
            PutChar(*__Format__);
            __Format__++;
        }
    }

    __builtin_va_end(args);
    ReleaseSpinLock(&ConsoleLock);
}

/*
 * KrnPrintfColor - Colored Kernel Printf Function
 *
 * Same as KrnPrintf but allows specifying custom text and background colors
 * for the output. The colors are temporarily applied for this print operation
 * and then restored to their previous values.
 *
 * This is useful for highlighting different types of messages (errors, warnings,
 * debug info) with appropriate colors while maintaining thread safety.
 *
 * Parameters:
 * - __FG__: 32-bit foreground (text) color for this output
 * - __BG__: 32-bit background color for this output
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock. Color state is properly
 * restored even if an exception occurs during processing.
 */
void
KrnPrintfColor(uint32_t __FG__, uint32_t __BG__, const char *__Format__, ...)
{
    AcquireSpinLock(&ConsoleLock);
    uint32_t OldFG = Console.TXColor;
    uint32_t OldBG = Console.BGColor;

    SetBGColor(__FG__, __BG__);

    __builtin_va_list args;
    __builtin_va_start(args, __Format__);

    while (*__Format__)
    {
        if (*__Format__ == '%')
        {
            __Format__++;
            ProcessFormatSpecifier(&__Format__, &args);
        }
        else
        {
            PutChar(*__Format__);
            __Format__++;
        }
    }

    __builtin_va_end(args);
    SetBGColor(OldFG, OldBG);
    ReleaseSpinLock(&ConsoleLock);
}

/*
 * ProcessFormatSpecifier - Parse and Process Printf Format Specifiers
 *
 * Parses a complete printf format specifier including flags, width, precision,
 * length modifiers, and the conversion specifier. Then dispatches to the
 * appropriate handler function based on the specifier type.
 *
 * Supported format: %[flags][width][.precision][length]specifier
 *
 * Flags: - (left align), + (show sign), ' ' (space prefix), # (alternate form), 0 (zero pad)
 * Width: minimum field width, or * for argument
 * Precision: maximum characters for strings, or * for argument
 * Length: hh, h, l, ll, z, t, j for different integer sizes
 * Specifiers: d,i,u,x,X,o,b,s,c,p,n,f,F,e,E,g,G,%
 *
 * Parameters:
 * - __Format__: Pointer to format string pointer (modified during parsing)
 * - __Args__: Variable argument list pointer
 *
 * This function is the core of the printf parsing engine, handling all
 * standard printf formatting options except floating point (not implemented).
 */
void
ProcessFormatSpecifier(const char **__Format__, __builtin_va_list *__Args__)
{
    FormatFlags Flags = {0};

    /* Parse flags: -, +, space, #, 0 */
    while (1)
    {
        switch (**__Format__)
        {
            case '-': Flags.LeftAlign = 1; (*__Format__)++; continue;
            case '+': Flags.ShowSign = 1; (*__Format__)++; continue;
            case ' ': Flags.SpacePrefix = 1; (*__Format__)++; continue;
            case '#': Flags.AlternateForm = 1; (*__Format__)++; continue;
            case '0': Flags.ZeroPad = 1; (*__Format__)++; continue;
            default: break;
        }
        break;
    }

    /* Parse width: either number or * for argument */
    if (**__Format__ == '*')
    {
        Flags.Width = __builtin_va_arg(*__Args__, int);
        (*__Format__)++;
    }
    else
    {
        while (**__Format__ >= '0' && **__Format__ <= '9')
        {
            Flags.Width = Flags.Width * 10 + (**__Format__ - '0');
            (*__Format__)++;
        }
    }

    /* Parse precision: . followed by number or * */
    if (**__Format__ == '.')
    {
        Flags.HasPrecision = 1;
        (*__Format__)++;
        if (**__Format__ == '*')
        {
            Flags.Precision = __builtin_va_arg(*__Args__, int);
            (*__Format__)++;
        }
        else
        {
            while (**__Format__ >= '0' && **__Format__ <= '9')
            {
                Flags.Precision = Flags.Precision * 10 + (**__Format__ - '0');
                (*__Format__)++;
            }
        }
    }

    /* Parse length modifiers: hh, h, l, ll, z, t, j */
    switch (**__Format__)
    {
        case 'l':
            (*__Format__)++;
            if (**__Format__ == 'l')
            {
                Flags.Length = 2; /* ll */
                (*__Format__)++;
            }
            else
                Flags.Length = 1; /* l */
            break;
        case 'h':
            (*__Format__)++;
            if (**__Format__ == 'h')
            {
                Flags.Length = -2; /* hh */
                (*__Format__)++;
            }
            else
                Flags.Length = -1; /* h */
            break;
        case 'z': Flags.Length = 3; (*__Format__)++; break;
        case 't': Flags.Length = 4; (*__Format__)++; break;
        case 'j': Flags.Length = 5; (*__Format__)++; break;
    }

    /* Process the conversion specifier */
    switch (**__Format__)
    {
        case 'd':
        case 'i':
            ProcessInteger(__Args__, &Flags, 10, 1);
            break;
        case 'u':
            ProcessInteger(__Args__, &Flags, 10, 0);
            break;
        case 'x':
            ProcessInteger(__Args__, &Flags, 16, 0);
            break;
        case 'X':
            Flags.Length |= 0x80; /* Uppercase flag */
            ProcessInteger(__Args__, &Flags, 16, 0);
            break;
        case 'o':
            ProcessInteger(__Args__, &Flags, 8, 0);
            break;
        case 'b':
            ProcessInteger(__Args__, &Flags, 2, 0);
            break;
        case 's':
            ProcessString(__Args__, &Flags);
            break;
        case 'c':
            ProcessChar(__Args__, &Flags);
            break;
        case 'p':
            ProcessPointer(__Args__, &Flags);
            break;
        case 'n':
            /* Not implemented for security reasons */
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            /* Floating point not implemented in kernel */
            PutPrint("(float)");
            break;
        case '%':
            PutChar('%');
            break;
        default:
            /* Unknown specifier - print as-is */
            PutChar('%');
            PutChar(**__Format__);
            break;
    }
    (*__Format__)++;
}

/*
 * ProcessInteger - Process Integer Format Specifiers
 *
 * Extracts an integer value from the variable argument list according to
 * the specified length modifier, converts it to the requested base, and
 * applies formatting flags (padding, prefixes, etc.).
 *
 * Handles all integer types: char, short, int, long, long long, and their
 * unsigned variants. Supports decimal, hexadecimal, octal, and binary output.
 *
 * Parameters:
 * - __Args__: Variable argument list pointer
 * - __Flags__: Formatting flags (width, precision, alignment, etc.)
 * - __Base__: Number base (2=binary, 8=octal, 10=decimal, 16=hex)
 * - __Signed__: Whether to treat the value as signed (1) or unsigned (0)
 *
 * Length modifiers:
 * - hh: signed/unsigned char
 * - h: signed/unsigned short
 * - (none): signed/unsigned int
 * - l: signed/unsigned long
 * - ll: signed/unsigned long long
 */
void
ProcessInteger(__builtin_va_list *__Args__, FormatFlags *__Flags__, int __Base__, int __Signed__)
{
    char Buffer[64];
    int64_t Value = 0;
    uint64_t UValue = 0;
    int IsNegative = 0;

    /* Extract value based on length modifier and signedness */
    if (__Signed__)
    {
        switch (__Flags__->Length)
        {
            case -2: Value = (signed char)__builtin_va_arg(*__Args__, int); break;
            case -1: Value = (short)__builtin_va_arg(*__Args__, int); break;
            case 0:  Value = __builtin_va_arg(*__Args__, int); break;
            case 1:  Value = __builtin_va_arg(*__Args__, long); break;
            case 2:  Value = __builtin_va_arg(*__Args__, long long); break;
            default: Value = __builtin_va_arg(*__Args__, int); break;
        }
        if (Value < 0)
        {
            IsNegative = 1;
            UValue = -Value;
        }
        else
            UValue = Value;
    }
    else
    {
        switch (__Flags__->Length)
        {
            case -2: UValue = (unsigned char)__builtin_va_arg(*__Args__, unsigned int); break;
            case -1: UValue = (unsigned short)__builtin_va_arg(*__Args__, unsigned int); break;
            case 0:  UValue = __builtin_va_arg(*__Args__, unsigned int); break;
            case 1:  UValue = __builtin_va_arg(*__Args__, unsigned long); break;
            case 2:  UValue = __builtin_va_arg(*__Args__, unsigned long long); break;
            default: UValue = __builtin_va_arg(*__Args__, unsigned int); break;
        }
    }

    /* Convert unsigned value to string in specified base */
    UnsignedToStringEx(UValue, Buffer, __Base__, (__Flags__->Length & 0x80) ? 1 : 0);

    /* Apply formatting (padding, prefixes, alignment) */
    FormatOutput(Buffer, __Flags__, IsNegative, __Base__);
}

/*
 * ProcessString - Process String Format Specifiers
 *
 * Extracts a null-terminated string from the argument list and outputs it
 * with optional width formatting and precision limiting.
 *
 * Handles null strings by printing "(null)". Precision limits the maximum
 * number of characters printed. Width controls field width with left or
 * right alignment.
 *
 * Parameters:
 * - __Args__: Variable argument list pointer
 * - __Flags__: Formatting flags (width, precision, alignment)
 *
 * Note: Does not handle wide character strings (not implemented).
 */
void
ProcessString(__builtin_va_list *__Args__, FormatFlags *__Flags__)
{
    const char *Str = __builtin_va_arg(*__Args__, const char*);
    if (!Str) Str = "(null)";

    int Len = StringLength(Str);
    if (__Flags__->HasPrecision && __Flags__->Precision < Len)
        Len = __Flags__->Precision;

    /* Right-align padding */
    if (!__Flags__->LeftAlign && __Flags__->Width > Len)
    {
        for (int i = 0; i < __Flags__->Width - Len; i++)
            PutChar(' ');
    }

    /* Output string characters */
    for (int i = 0; i < Len; i++)
        PutChar(Str[i]);

    /* Left-align padding */
    if (__Flags__->LeftAlign && __Flags__->Width > Len)
    {
        for (int i = 0; i < __Flags__->Width - Len; i++)
            PutChar(' ');
    }
}

/*
 * ProcessChar - Process Character Format Specifiers
 *
 * Extracts a single character from the argument list and outputs it with
 * optional width formatting for alignment.
 *
 * Characters are promoted to int in variable argument lists, so we cast
 * back to char. Width controls field width with left or right alignment.
 *
 * Parameters:
 * - __Args__: Variable argument list pointer
 * - __Flags__: Formatting flags (width, alignment)
 *
 * Note: Does not handle wide characters (not implemented).
 */
void
ProcessChar(__builtin_va_list *__Args__, FormatFlags *__Flags__)
{
    char Ch = (char)__builtin_va_arg(*__Args__, int);

    /* Right-align padding */
    if (!__Flags__->LeftAlign && __Flags__->Width > 1)
    {
        for (int i = 0; i < __Flags__->Width - 1; i++)
            PutChar(' ');
    }

    PutChar(Ch);

    /* Left-align padding */
    if (__Flags__->LeftAlign && __Flags__->Width > 1)
    {
        for (int i = 0; i < __Flags__->Width - 1; i++)
            PutChar(' ');
    }
}

/*
 * ProcessPointer - Process Pointer Format Specifiers
 *
 * Extracts a pointer from the argument list and outputs it in hexadecimal
 * format with the standard "0x" prefix.
 *
 * Pointers are converted to uintptr_t for consistent handling across
 * different architectures, then formatted as lowercase hexadecimal.
 *
 * Parameters:
 * - __Args__: Variable argument list pointer
 * - __Flags__: Formatting flags (unused for pointers)
 *
 * Output format: 0x followed by hexadecimal digits (lowercase).
 */
void
ProcessPointer(__builtin_va_list *__Args__, FormatFlags *__Flags__)
{
    void *Ptr = __builtin_va_arg(*__Args__, void*);
    char Buffer[32];

    PutPrint("0x");
    UnsignedToStringEx((uintptr_t)Ptr, Buffer, 16, 0);
    PutPrint(Buffer);
}

/*
 * FormatOutput - Apply Formatting to Numeric Output
 *
 * Applies width, alignment, padding, and prefix formatting to numeric strings.
 * Handles sign prefixes (-, +, space), alternate forms (0x, 0), and padding
 * with spaces or zeros.
 *
 * Parameters:
 * - __Buffer__: Null-terminated string containing the number
 * - __Flags__: Formatting flags (width, alignment, padding, prefixes)
 * - __IsNegative__: Whether the original value was negative
 * - __Base__: Number base for alternate form prefixes
 *
 * Formatting order: [left padding] [prefix] [zero padding] [number] [right padding]
 */
void
FormatOutput(const char *__Buffer__, FormatFlags *__Flags__, int __IsNegative__, int __Base__)
{
    int Len = StringLength(__Buffer__);
    int PrefixLen = 0;
    char Prefix[4] = {0};

    /* Build sign/space prefix */
    if (__IsNegative__)
        Prefix[PrefixLen++] = '-';
    else if (__Flags__->ShowSign)
        Prefix[PrefixLen++] = '+';
    else if (__Flags__->SpacePrefix)
        Prefix[PrefixLen++] = ' ';

    /* Build alternate form prefix */
    if (__Flags__->AlternateForm)
    {
        if (__Base__ == 16)
        {
            Prefix[PrefixLen++] = '0';
            Prefix[PrefixLen++] = 'x';
        }
        else if (__Base__ == 8 && __Buffer__[0] != '0')
            Prefix[PrefixLen++] = '0';
    }

    int TotalLen = Len + PrefixLen;
    int PadLen = (__Flags__->Width > TotalLen) ? __Flags__->Width - TotalLen : 0;

    /* Left padding (spaces) */
    if (!__Flags__->LeftAlign && !__Flags__->ZeroPad)
    {
        for (int i = 0; i < PadLen; i++)
            PutChar(' ');
    }

    /* Output prefix */
    for (int i = 0; i < PrefixLen; i++)
        PutChar(Prefix[i]);

    /* Zero padding */
    if (!__Flags__->LeftAlign && __Flags__->ZeroPad)
    {
        for (int i = 0; i < PadLen; i++)
            PutChar('0');
    }

    /* Output the number */
    PutPrint(__Buffer__);

    /* Right padding (spaces) */
    if (__Flags__->LeftAlign)
    {
        for (int i = 0; i < PadLen; i++)
            PutChar(' ');
    }
}

/*
 * UnsignedToStringEx - Convert Unsigned Integer to String
 *
 * Converts an unsigned 64-bit integer to its string representation in the
 * specified base (2-36). The result is stored in the provided buffer.
 *
 * Parameters:
 * - __Value__: The unsigned integer to convert
 * - __Buffer__: Output buffer (must be large enough)
 * - __Base__: Number base (2=binary, 8=octal, 10=decimal, 16=hex, etc.)
 * - __Uppercase__: 1 for uppercase hex digits (A-F), 0 for lowercase (a-f)
 *
 * The string is built in reverse order then reversed to correct it.
 * Handles the special case of zero correctly.
 */
void
UnsignedToStringEx(uint64_t __Value__, char *__Buffer__, int __Base__, int __Uppercase__)
{
    int Iteration = 0;

    if (__Value__ == 0)
    {
        __Buffer__[Iteration++] = '0';
        __Buffer__[Iteration] = '\0';
        return;
    }

    while (__Value__ != 0)
    {
        int Remainder = __Value__ % __Base__;
        if (Remainder > 9)
            __Buffer__[Iteration++] = (Remainder - 10) + (__Uppercase__ ? 'A' : 'a');
        else
            __Buffer__[Iteration++] = Remainder + '0';
        __Value__ = __Value__ / __Base__;
    }

    __Buffer__[Iteration] = '\0';
    ReverseString(__Buffer__, Iteration);
}

/*
 * PrintInteger - Legacy Integer Printing Function
 *
 * Converts a signed integer to string in specified base and outputs it.
 * This is a legacy function kept for backward compatibility.
 *
 * Parameters:
 * - __Value__: Signed integer to print
 * - __Base__: Number base (2, 8, 10, 16)
 * - __Uppercase__: 1 for uppercase hex digits, 0 for lowercase
 *
 * Note: This function is deprecated in favor of the full printf system.
 */
void PrintInteger(int __Value__, int __Base__, int __Uppercase__)
{
    char Buffer[32];
    IntegerToString(__Value__, Buffer, __Base__);

    if (__Uppercase__)
    {
        for (int Iteration = 0; Buffer[Iteration]; Iteration++)
        {
            if (Buffer[Iteration] >= 'a' && Buffer[Iteration] <= 'f')
                Buffer[Iteration] = Buffer[Iteration] - 'a' + 'A';
        }
    }

    PrintString(Buffer);
}

/*
 * PrintUnsigned - Legacy Unsigned Integer Printing Function
 *
 * Converts an unsigned 32-bit integer to string in specified base and outputs it.
 * This is a legacy function kept for backward compatibility.
 *
 * Parameters:
 * - __Value__: Unsigned integer to print
 * - __Base__: Number base (2, 8, 10, 16)
 * - __Uppercase__: 1 for uppercase hex digits, 0 for lowercase
 *
 * Note: This function is deprecated in favor of the full printf system.
 */
void PrintUnsigned(uint32_t __Value__, int __Base__, int __Uppercase__)
{
    char Buffer[32];
    UnsignedToString(__Value__, Buffer, __Base__);

    if (__Uppercase__)
    {
        for (int Iteration = 0; Buffer[Iteration]; Iteration++)
        {
            if (Buffer[Iteration] >= 'a' && Buffer[Iteration] <= 'f')
                Buffer[Iteration] = Buffer[Iteration] - 'a' + 'A';
        }
    }

    PrintString(Buffer);
}

/*
 * PrintString - Legacy String Printing Function
 *
 * Outputs a null-terminated string to the console. Handles null strings
 * by printing "(null)". This is a legacy function kept for backward compatibility.
 *
 * Parameters:
 * - __String__: Null-terminated string to print, or NULL
 *
 * Note: This function is deprecated in favor of the full printf system.
 */
void PrintString(const char *__String__)
{
    if (__String__ == 0)
    {
        PutPrint("(null)");
        return;
    }
    PutPrint(__String__);
}

/*
 * PrintChar - Legacy Character Printing Function
 *
 * Outputs a single character to the console. This is a legacy function
 * kept for backward compatibility.
 *
 * Parameters:
 * - __Char__: Character to print
 *
 * Note: This function is deprecated in favor of the full printf system.
 */
void PrintChar(char __Char__)
{
    PutChar(__Char__);
}

/*
 * PrintPointer - Legacy Pointer Printing Function
 *
 * Outputs a pointer value in hexadecimal format with "0x" prefix.
 * This is a legacy function kept for backward compatibility.
 *
 * Parameters:
 * - __Pointer__: Pointer to print
 *
 * Output format: 0x followed by lowercase hexadecimal digits.
 *
 * Note: This function is deprecated in favor of the full printf system.
 */
void PrintPointer(void *__Pointer__)
{
    PutPrint("0x");
    PrintUnsigned((uint32_t)(uintptr_t)__Pointer__, 16, 0);
}

/*
 * StringLength - Calculate String Length
 *
 * Returns the length of a null-terminated string (excluding the null terminator).
 *
 * Parameters:
 * - __String__: Null-terminated string to measure
 *
 * Returns: Number of characters in the string before the null terminator.
 */
int StringLength(const char *__String__)
{
    int Length = 0;
    while (__String__[Length]) Length++;
    return Length;
}

/*
 * ReverseString - Reverse Characters in String
 *
 * Reverses the order of characters in a string in-place. Used internally
 * by the printf system for number-to-string conversions.
 *
 * Parameters:
 * - __String__: Character array to reverse (modified in-place)
 * - __Length__: Number of characters to reverse (excluding null terminator)
 */
void ReverseString(char *__String__, int __Length__)
{
    int Start = 0;
    int End = __Length__ - 1;

    while (Start < End)
    {
        char Temp = __String__[Start];
        __String__[Start] = __String__[End];
        __String__[End] = Temp;
        Start++;
        End--;
    }
}

/*
 * IntegerToString - Convert Signed Integer to String
 *
 * Converts a signed integer to its string representation in the specified base.
 * Handles negative numbers for decimal base. This is a legacy function used
 * internally by the printf system.
 *
 * Parameters:
 * - __Value__: Signed integer to convert
 * - __Buffer__: Output buffer (must be large enough)
 * - __Base__: Number base (2, 8, 10, 16)
 *
 * The string is built in reverse order then reversed to correct it.
 */
void IntegerToString(int __Value__, char *__Buffer__, int __Base__)
{
    int Iteration = 0;
    int IsNegative = 0;

    if (__Value__ == 0)
    {
        __Buffer__[Iteration++] = '0';
        __Buffer__[Iteration] = '\0';
        return;
    }

    if (__Value__ < 0 && __Base__ == 10)
    {
        IsNegative = 1;
        __Value__ = -__Value__;
    }

    while (__Value__ != 0)
    {
        int Remainder = __Value__ % __Base__;
        __Buffer__[Iteration++] = (Remainder > 9) ? (Remainder - 10) + 'a' : Remainder + '0';
        __Value__ = __Value__ / __Base__;
    }

    if (IsNegative) __Buffer__[Iteration++] = '-';

    __Buffer__[Iteration] = '\0';
    ReverseString(__Buffer__, Iteration);
}

/*
 * UnsignedToString - Convert Unsigned Integer to String
 *
 * Converts an unsigned 32-bit integer to its string representation in the
 * specified base. This is a legacy function used internally by the printf system.
 *
 * Parameters:
 * - __Value__: Unsigned integer to convert
 * - __Buffer__: Output buffer (must be large enough)
 * - __Base__: Number base (2, 8, 10, 16)
 *
 * The string is built in reverse order then reversed to correct it.
 * Handles the special case of zero correctly.
 */
void UnsignedToString(uint32_t __Value__, char *__Buffer__, int __Base__)
{
    int Iteration = 0;

    if (__Value__ == 0)
    {
        __Buffer__[Iteration++] = '0';
        __Buffer__[Iteration] = '\0';
        return;
    }

    while (__Value__ != 0)
    {
        int Remainder = __Value__ % __Base__;
        __Buffer__[Iteration++] = (Remainder > 9) ? (Remainder - 10) + 'a' : Remainder + '0';
        __Value__ = __Value__ / __Base__;
    }

    __Buffer__[Iteration] = '\0';
    ReverseString(__Buffer__, Iteration);
}
