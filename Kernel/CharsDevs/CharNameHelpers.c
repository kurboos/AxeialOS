#include <Errnos.h>

int
CharMakeName(char* __Out__, long __Cap__, const char* __Prefix__, long __Index__)
{
    if (Probe_IF_Error(__Out__) || !__Out__ || Probe_IF_Error(__Prefix__) || !__Prefix__ ||
        __Cap__ <= 0)
    {
        return -BadArgs;
    }
    long Pos = 0;
    while (__Prefix__[Pos] && Pos < __Cap__)
    {
        __Out__[Pos] = __Prefix__[Pos];
        Pos++;
    }
    if (Pos >= __Cap__)
    {
        return -Limits;
    }

    /* Append decimal index */
    char          Tmp[20];
    int           Len = 0;
    unsigned long N   = (unsigned long)__Index__;
    if (N == 0)
    {
        if (Pos >= __Cap__)
        {
            return -Limits;
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
            return -Limits;
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

int
CharMakeSubName(char* __Out__, long __Cap__, const char* __Base__, long __SubIndex__)
{
    /* base + decimal subindex (e.g., "hid" + 1 -> "hid1") */
    return CharMakeName(__Out__, __Cap__, __Base__, __SubIndex__);
}