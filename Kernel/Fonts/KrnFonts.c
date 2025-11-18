#include <KrnFont.h>

/**
 * @brief Render a single character to the framebuffer.
 *
 * @details Retrieves the 8x16 bitmap for the given character from the kernel font map
 * 			and draws it pixel-by-pixel into the framebuffer at the specified position.
 * 			Each bit in the font bitmap corresponds to one pixel; set bits are drawn
 * 			using the provided 32-bit color value.
 *
 * @param __FrameBuffer__ Pointer to the framebuffer memory (32-bit pixels).
 * @param __FrameBufferW__ Width of the framebuffer in pixels.
 * @param __PosX__ X-coordinate (column) of the character’s top-left corner.
 * @param __PosY__ Y-coordinate (row) of the character’s top-left corner.
 * @param __Char__ Character to render.
 * @param __32bitColor__ ARGB color value used for drawing pixels.
 *
 * @return void
 *
 * @note Assumes an 8x16 fixed-width font defined in KrnlFontMap.
 */
void
DisplayChar(uint32_t* __FrameBuffer__,
            uint32_t  __FrameBufferW__,
            uint32_t  __PosX__,
            uint32_t  __PosY__,
            char      __Char__,
            uint32_t  __32bitColor__)
{
    /* Retrieve the 16-byte bitmap for this character */
    const uint8_t* Character = KrnlFontMap[(uint8_t)__Char__];

    /* Render each row of the 8x16 character */
    for (int MapRow = 0; MapRow < FontH; MapRow++)
    {
        uint8_t Line = Character[MapRow];

        /* Process each column (bit) in the row */
        for (int Column = 0; Column < FontW; Column++)
        {
            /* Check if bit is set (MSB is leftmost pixel) */
            if (Line & (0x80 >> Column))
            {
                /* Calculate framebuffer offset and set pixel */
                __FrameBuffer__[(__PosY__ + MapRow) * __FrameBufferW__ + (__PosX__ + Column)] =
                    __32bitColor__;
            }
        }
    }
}

/**
 * @brief Render a null-terminated string to the framebuffer.
 *
 * @details Iterates through each character in the string and calls DisplayChar
 * 			to render it. Advances the X position by the font width after each
 * 			character to ensure proper spacing.
 *
 * @param __FrameBuffer__ Pointer to the framebuffer memory (32-bit pixels).
 * @param __FrameBufferW__ Width of the framebuffer in pixels.
 * @param __PosX__ Starting X-coordinate for the string.
 * @param __PosY__ Starting Y-coordinate for the string.
 * @param __String__ Pointer to the null-terminated string to render.
 * @param __32bitColor__ ARGB color value used for drawing pixels.
 *
 * @return void
 *
 * @note Useful for rendering kernel messages, debugging output,
 *       or simple text overlays directly to the framebuffer.
 */
void
DisplayString(uint32_t*   __FrameBuffer__,
              uint32_t    __FrameBufferW__,
              uint32_t    __PosX__,
              uint32_t    __PosY__,
              const char* __String__,
              uint32_t    __32bitColor__)
{
    uint32_t PosX = __PosX__;

    /* Render each character in sequence */
    while (*__String__)
    {
        DisplayChar(__FrameBuffer__, __FrameBufferW__, PosX, __PosY__, *__String__, __32bitColor__);
        PosX += FontW; /* Advance to next character position */
        __String__++;  /* Move to next character in string */
    }
}
