#include <KrnPrintf.h>

/**
 * @brief Print an error message to the console.
 *
 * @details Acquires the console spinlock for thread-safe output.
 *			Temporarily sets foreground/background colors to error style.
 *			Prints the "[ERROR]:" prefix followed by the formatted message.
 *			Restores previous colors and releases the spinlock.
 *
 * @param __Format__ Format string with optional specifiers.
 * @param ...        Variable arguments matching the format string.
 *
 * @return void
 */
void
PError(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    AcquireSpinLock(&ConsoleLock);
    uint32_t OldFG = Console.TXColor;
    uint32_t OldBG = Console.BGColor;

    SetBGColor(ClrError, ClrInvisible);
    PutPrint("[ERROR]:");
    SetBGColor(ClrNormal, ClrInvisible);

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

/**
 * @brief Print a warning message to the console.
 *
 * @details Uses warning color scheme.
 * 			Prints the "[WARN]:" prefix followed by the formatted message.
 * 			Thread-safe via console spinlock.
 *
 * @param __Format__ Format string with optional specifiers.
 * @param ...        Variable arguments matching the format string.
 *
 * @return void
 */
void
PWarn(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    AcquireSpinLock(&ConsoleLock);
    uint32_t OldFG = Console.TXColor;
    uint32_t OldBG = Console.BGColor;

    SetBGColor(ClrWarn, ClrInvisible);
    PutPrint("[WARN]:");
    SetBGColor(ClrNormal, ClrInvisible);

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

/**
 * @brief Print an informational message to the console.
 *
 * @details Uses informational color scheme.
 *			Prints the "[INFO]:" prefix followed by the formatted message.
 *			Thread-safe via console spinlock.
 *
 * @param __Format__ Format string with optional specifiers.
 * @param ...        Variable arguments matching the format string.
 *
 * @return void
 */
void
PInfo(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    AcquireSpinLock(&ConsoleLock);
    uint32_t OldFG = Console.TXColor;
    uint32_t OldBG = Console.BGColor;

    SetBGColor(ClrInfo, ClrInvisible);
    PutPrint("[INFO]:");
    SetBGColor(ClrNormal, ClrInvisible);

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

/**
 * @brief Print a debug message to the console.
 *
 * @details Uses debug color scheme.
 * 			Prints the "[DEBUG]:" prefix followed by the formatted message.
 * 			Intended for development and diagnostic output.
 *
 * @param __Format__ Format string with optional specifiers.
 * @param ...        Variable arguments matching the format string.
 *
 * @return void
 *
 * @note This function is named `_PDebug` to avoid conflicts with macros.
 */
void
_PDebug(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    AcquireSpinLock(&ConsoleLock);
    uint32_t OldFG = Console.TXColor;
    uint32_t OldBG = Console.BGColor;

    SetBGColor(ClrDebug, ClrInvisible);
    PutPrint("[DEBUG]:");

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

/**
 * @brief Print a success message to the console.
 *
 * @details Uses success color scheme.
 * 			Prints the "[OK]:" prefix followed by the formatted message.
 * 			Thread-safe via console spinlock.
 *
 * @param __Format__ Format string with optional specifiers.
 * @param ...        Variable arguments matching the format string.
 *
 * @return void
 */
void
PSuccess(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    AcquireSpinLock(&ConsoleLock);
    uint32_t OldFG = Console.TXColor;
    uint32_t OldBG = Console.BGColor;

    SetBGColor(ClrSuccess, ClrInvisible);
    PutPrint("[OK]:");
    SetBGColor(ClrNormal, ClrInvisible);

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
