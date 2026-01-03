#pragma once

#include <AllTypes.h>

#define SysOkay 0
#define SysErro -1

typedef struct
{
    int ErrCode;
} SysErr;

/*for voids*/
#define SlotError(__PtrError__, __CodeEnum__)                                                      \
    do                                                                                             \
    {                                                                                              \
        if ((__PtrError__) != NULL)                                                                \
        {                                                                                          \
            (__PtrError__)->ErrCode = (__CodeEnum__);                                              \
        }                                                                                          \
    } while (0)

/*linux-like idioms*/
#define Error_TO_Pointer(__Code__) ((void*)(intptr_t)(__Code__))
#define Pointer_TO_Error(__Ptr__)  ((int)(intptr_t)(__Ptr__))
#define Probe_IF_Error(__Ptr__)    ((intptr_t)(__Ptr__) >= (intptr_t)(-4095))

/*For bools or binary returns, syserro and sysokay work too*/

enum ErrCodes
{
    Nothing,

    /*All Codes*/
    NotCanonical,
    Limits,
    Impilict,
    BadArgs,
    TooBig,
    TooSmall,
    TooMany,
    TooLess,
    NoWrite,
    NoRead,
    NoSuch,
    Missing,
    Overflow,
    NotInit,
    BadAlloc,
    Dangling,
    NotRecorded,
    NotRooted,
    BadEntry,
    NoOperations,
    CannotLookup,
    Redefined,
    BadEntity,
    ErrReturn,
    Depleted,
    BadSystemcall,
    Recursion,
    Busy,

};

/*
    SysErr     err;
    SysErr*    Error = &err;
*/

/*useful*/
#define _unused __attribute((unused))