#include <KrnFont.h>

/*
 * DisplayChar - Render a Single Character to Framebuffer
 *
 * Renders a single character from the kernel font map onto the specified
 * framebuffer at the given position. Each character is 8x16 pixels and
 * uses a bitmap representation where each bit corresponds to a pixel.
 *
 * Parameters:
 * - __FrameBuffer__: Pointer to the linear framebuffer memory
 * - __FrameBufferW__: Width of the framebuffer in pixels
 * - __PosX__: X coordinate (column) to start rendering
 * - __PosY__: Y coordinate (row) to start rendering
 * - __Char__: ASCII character code to render (0-255)
 * - __32bitColor__: 32-bit ARGB color value for foreground pixels
 *
 * Algorithm:
 * - Look up character bitmap in KrnlFontMap array
 * - For each of 16 rows in the character:
 *   - Extract the row's 8-bit pattern
 *   - For each of 8 columns (bits 7-0):
 *     - If bit is set, write color to framebuffer at calculated position
 *
 * Thread safety: Not thread-safe. Caller must ensure exclusive framebuffer access.
 * Performance: O(1) - fixed 128 pixel operations per character.
 */
void
DisplayChar(
uint32_t *__FrameBuffer__,
uint32_t __FrameBufferW__,
uint32_t __PosX__,
uint32_t __PosY__,
char __Char__,
uint32_t __32bitColor__)
{
    /* Retrieve the 16-byte bitmap for this character */
    const uint8_t *Character = KrnlFontMap[(uint8_t)__Char__];

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
                __FrameBuffer__[(__PosY__ + MapRow) * __FrameBufferW__ + (__PosX__ + Column)] = __32bitColor__;
            }
        }
    }
}

/*
 * DisplayString - Render a String to Framebuffer
 *
 * Renders a null-terminated string of characters to the framebuffer by
 * repeatedly calling DisplayChar for each character in sequence.
 * Characters are rendered horizontally with no spacing between them.
 *
 * Parameters:
 * - __FrameBuffer__: Pointer to the linear framebuffer memory
 * - __FrameBufferW__: Width of the framebuffer in pixels
 * - __PosX__: Starting X coordinate for the first character
 * - __PosY__: Y coordinate for all characters in the string
 * - __String__: Pointer to null-terminated string to render
 * - __32bitColor__: 32-bit ARGB color value for all characters
 *
 * Algorithm:
 * - Initialize current X position to starting position
 * - For each character in string until null terminator:
 *   - Render character at current position
 *   - Advance X position by character width (8 pixels)
 *
 * Thread safety: Not thread-safe. Caller must ensure exclusive framebuffer access.
 * Performance: O(n) where n is string length.
 * Limitations: No bounds checking - will overflow framebuffer if string is too long.
 */
void
DisplayString(
uint32_t *__FrameBuffer__,
uint32_t __FrameBufferW__,
uint32_t __PosX__,
uint32_t __PosY__,
const char *__String__,
uint32_t __32bitColor__)
{
    uint32_t PosX = __PosX__;

    /* Render each character in sequence */
    while (*__String__)
    {
        DisplayChar(__FrameBuffer__, __FrameBufferW__, PosX, __PosY__, *__String__, __32bitColor__);
        PosX += FontW;  /* Advance to next character position */
        __String__++;   /* Move to next character in string */
    }
}
