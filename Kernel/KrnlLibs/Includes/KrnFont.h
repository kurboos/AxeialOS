#pragma once
#include <AllTypes.h>

/*8x16*/
#define __F16

/**
 * Macros/Constants
 * Only 4 font sizes are Supported
 * :-> F04, F06, F08, F016
 */

/*F016*/
#ifdef __F16
#    define FontW 8
#    define FontH 16
#endif

/*maxsize of the bitmap*/
#define MaxFontMap 16
/*max amount of entries in the bitmap*/
#define MaxFontEntries 256

/**
 * Font Bitmap Fowarded, Located in KrnFont.c in /Kernel/Fonts/
 */
extern const uint8_t KrnlFontMap[256][16];

/**
 * Prototypes
 */
void DisplayChar(uint32_t* __FrameBuffer__,
                 uint32_t  __FrameBufferW__,
                 uint32_t  __PosX__,
                 uint32_t  __PosY__,
                 char      __Char__,
                 uint32_t  __32bitColor__);
void DisplayString(uint32_t*   __FrameBuffer__,
                   uint32_t    __FrameBufferW__,
                   uint32_t    __PosX__,
                   uint32_t    __PosY__,
                   const char* __String__,
                   uint32_t    __32bitColor__);
