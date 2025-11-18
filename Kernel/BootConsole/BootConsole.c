#include <BootConsole.h>
#include <Serial.h>

/**
 * TODO: Probably Add Double buffering because Its slow.
 * 		 Even tho it is temp' but will be nice to have.
 */

/** @brief Global (Early Boot)Console */
BootConsole Console = {0};

/**
 * @brief Initialize the boot console.
 *
 * @details Sets up the framebuffer console with dimensions, cursor position,
 * 			and default text/background colors. Calculates the number of
 * 			character columns and rows based on font size.
 *
 * @param __FrameBuffer__ Pointer to the framebuffer memory (32-bit pixels).
 * @param __CW__ Width of the framebuffer in pixels.
 * @param __CH__ Height of the framebuffer in pixels.
 *
 * @return void
 *
 * @note Must be called before any console output functions.
 */
void
KickStartConsole(uint32_t* __FrameBuffer__, uint32_t __CW__, uint32_t __CH__)
{
    Console.FrameBuffer  = __FrameBuffer__;
    Console.FrameBufferW = __CW__;
    Console.FrameBufferH = __CH__;
    Console.ConsoleCol   = __CW__ / FontW;
    Console.ConsoleRow   = __CH__ / FontH;
    Console.CursorX      = 0;
    Console.CursorY      = 0;
    Console.TXColor      = 0xFFFFFF; /* White text */
    Console.BGColor      = 0x000000; /* Black background */
}

/**
 * @brief Clear the console.
 *
 * @details Fills the entire framebuffer with the background color and resets
 * 			the cursor to the top-left position.
 *
 * @return void
 */
void
ClearConsole(void)
{
    /* Fill entire framebuffer with background color */
    for (uint32_t I = 0; I < Console.FrameBufferW * Console.FrameBufferH; I++)
    {
        Console.FrameBuffer[I] = Console.BGColor;
    }

    /* Reset cursor to top-left position */
    Console.CursorX = 0;
    Console.CursorY = 0;
}

/**
 * @brief Scroll the console up by one line.
 *
 * @details Moves all framebuffer lines up by one character row, discarding the
 * 			top line. Clears the newly exposed bottom line with the background color.
 *
 * @return void
 */
void
ScrollConsole(void)
{
    /* Move all lines up by one character row */
    for (uint32_t PosY = 0; PosY < Console.ConsoleRow - 1; PosY++)
    {
        for (uint32_t PosX = 0; PosX < Console.ConsoleCol; PosX++)
        {
            /* Calculate source and destination Y positions in pixels */
            uint32_t srcY   = (PosY + 1) * FontH; /* Line below */
            uint32_t dstY   = PosY * FontH;       /* Current line */
            uint32_t pixelX = PosX * FontW;       /* Character column start */

            /* Copy each pixel of the character (FontW x FontH block) */
            for (uint32_t py = 0; py < FontH; py++)
            {
                for (uint32_t px = 0; px < FontW; px++)
                {
                    uint32_t SrcIdx = (srcY + py) * Console.FrameBufferW + (pixelX + px);
                    uint32_t DstIdx = (dstY + py) * Console.FrameBufferW + (pixelX + px);
                    Console.FrameBuffer[DstIdx] = Console.FrameBuffer[SrcIdx];
                }
            }
        }
    }

    /* Clear the newly exposed last line with background color */
    uint32_t lastLineY = (Console.ConsoleRow - 1) * FontH;
    for (uint32_t PosY = lastLineY; PosY < lastLineY + FontH; PosY++)
    {
        for (uint32_t PosX = 0; PosX < Console.FrameBufferW; PosX++)
        {
            Console.FrameBuffer[PosY * Console.FrameBufferW + PosX] = Console.BGColor;
        }
    }
}

/**
 * @brief Output a single character to the console.
 *
 * @details Mirrors the character to the serial port for debugging.
 * 			Handles newline (`\n`) and carriage return (`\r`).
 * 			Renders printable characters to the framebuffer.
 * 			Advances the cursor, wrapping at the right edge.
 * 			Scrolls the console when reaching the bottom.
 *
 * @param __Char__ Character to output.
 *
 * @return void
 */
void
PutChar(char __Char__)
{
    /* Always mirror output to serial port for debugging */
    SerialPutChar(__Char__);

    if (__Char__ == '\n') /* Newline: move to next line */
    {
        Console.CursorX = 0;
        Console.CursorY++;
    }
    else if (__Char__ == '\r') /* Carriage return: move to line start */
    {
        Console.CursorX = 0;
    }
    else /* Printable character: render and advance cursor */
    {
        uint32_t PixelX = Console.CursorX * FontW;
        uint32_t PixelY = Console.CursorY * FontH;

        DisplayChar(
            Console.FrameBuffer, Console.FrameBufferW, PixelX, PixelY, __Char__, Console.TXColor);
        Console.CursorX++;
    }

    /* Handle line wrapping at right edge */
    if (Console.CursorX >= Console.ConsoleCol)
    {
        Console.CursorX = 0;
        Console.CursorY++;
    }

    /* Handle scrolling at bottom of screen */
    if (Console.CursorY >= Console.ConsoleRow)
    {
        ScrollConsole();
        Console.CursorY = Console.ConsoleRow - 1;
    }
}

/**
 * @brief Output a null-terminated string to the console.
 *
 * @details Iterates through the string and calls PutChar for each character.
 *
 * @param __String__ Pointer to the string to output.
 *
 * @return void
 */
void
PutPrint(const char* __String__)
{
    /* Process each character until null terminator */
    while (*__String__)
    {
        PutChar(*__String__);
        __String__++;
    }
}

/**
 * @brief Set console foreground and background colors.
 *
 * @details Updates the text (foreground) and background color values used
 * 			for subsequent rendering.
 *
 * @param __FG__ Foreground (text) color in 32-bit ARGB.
 * @param __BG__ Background color in 32-bit ARGB.
 *
 * @return void
 */
void
SetBGColor(uint32_t __FG__, uint32_t __BG__)
{
    Console.TXColor = __FG__;
    Console.BGColor = __BG__;
}

/**
 * @brief Set the console cursor position.
 *
 * @details Updates the cursor coordinates with bounds checking to ensure
 * 			they remain within the console’s dimensions.
 *
 * @param __CurX__ New cursor X position (column).
 * @param __CurY__ New cursor Y position (row).
 *
 * @return void
 */
void
SetCursor(uint32_t __CurX__, uint32_t __CurY__)
{
    /* Bounds checking to prevent cursor from going out of bounds */
    if (__CurX__ < Console.ConsoleCol)
    {
        Console.CursorX = __CurX__;
    }
    if (__CurY__ < Console.ConsoleRow)
    {
        Console.CursorY = __CurY__;
    }
}
