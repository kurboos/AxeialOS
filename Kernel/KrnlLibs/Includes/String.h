#pragma once

#include <AllTypes.h>

/**
 * Functions
 */
void StringCopy(char *__Dest__, const char *__Src__, uint32_t __MaxLen__); /*strcpy*/
void *__builtin_memcpy(void *__Dest__, const void *__Src__, size_t __Size__); /*memcpy*/
void* memset(void* __Dest__, int __Value__, size_t __Index__); /*memset*/