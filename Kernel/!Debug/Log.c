
#include <Errnos.h>
#include <KrnPrintf.h>

void
PError(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ConsoleLock, Error);
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
    ReleaseSpinLock(&ConsoleLock, Error);
}

void
PWarn(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ConsoleLock, Error);
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
    ReleaseSpinLock(&ConsoleLock, Error);
}

void
PInfo(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ConsoleLock, Error);
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
    ReleaseSpinLock(&ConsoleLock, Error);
}

void
_PDebug(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ConsoleLock, Error);
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
    ReleaseSpinLock(&ConsoleLock, Error);
}

void
PSuccess(const char* __Format__, ...)
{
    if (!__Format__)
    {
        __Format__ = "(null)";
    }
    SysErr  err;
    SysErr* Error = &err;
    AcquireSpinLock(&ConsoleLock, Error);
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
    ReleaseSpinLock(&ConsoleLock, Error);
}
