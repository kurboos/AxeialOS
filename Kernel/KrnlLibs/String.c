#include <AllTypes.h>

/*Strcpy*/
void
StringCopy(char *__Dest__, const char *__Src__, uint32_t __MaxLen__)
{
    uint32_t Index = 0;
    while (__Src__[Index] && Index < (__MaxLen__ - 1))
    {
        __Dest__[Index] = __Src__[Index];
        Index++;
    }
    __Dest__[Index] = '\0';
}

/*memcpy*/
void 
*__builtin_memcpy(void *__Dest__, const void *__Src__, size_t __Size__)
{
    char *Dest = __Dest__;
    const char *Src = __Src__;

    while (__Size__--) {
        *Dest++ = *Src++;
    }

    return __Dest__;
}

/*Memset*/
void*
memset(void* __Dest__, int __Value__, size_t __Index__)
{
    unsigned char* ptr = (unsigned char*)__Dest__;

    while (__Index__--)
        *ptr++ = (unsigned char)__Value__;

    return __Dest__;
}