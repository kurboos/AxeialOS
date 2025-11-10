#include <KrnPrintf.h>

/*
 * PError - Log Error Messages
 *
 * Outputs error messages with red "[ERROR]:" prefix. Used for critical
 * system errors that require immediate attention.
 *
 * Parameters:
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock.
 */
void
PError(const char *__Format__, ...)
{
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

/*
 * PWarn - Log Warning Messages
 *
 * Outputs warning messages with yellow/orange "[WARN]:" prefix. Used for
 * non-critical issues that should be noted but don't prevent operation.
 *
 * Parameters:
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock.
 */
void
PWarn(const char *__Format__, ...)
{
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

/*
 * PInfo - Log Informational Messages
 *
 * Outputs informational messages with cyan "[INFO]:" prefix. Used for
 * general status updates and progress information during normal operation.
 *
 * Parameters:
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock.
 */
void
PInfo(const char *__Format__, ...)
{
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

/*
 * _PDebug - Log Debug Messages (Internal Function)
 *
 * Outputs debug messages with magenta "[DEBUG]:" prefix. Used for detailed
 * debugging information during development. The underscore prefix indicates
 * this is an internal function, typically called through a macro.
 *
 * Parameters:
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock.
 */
void
_PDebug(const char *__Format__, ...)
{
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

/*
 * PSuccess - Log Success Messages
 *
 * Outputs success messages with green "[OK]:" prefix. Used to indicate
 * successful completion of operations or positive status updates.
 *
 * Parameters:
 * - __Format__: Format string with embedded specifiers
 * - ...: Variable arguments corresponding to format specifiers
 *
 * Thread safety: Protected by ConsoleLock spinlock.
 */
void
PSuccess(const char *__Format__, ...)
{
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
