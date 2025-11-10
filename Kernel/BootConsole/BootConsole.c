#include <BootConsole.h>
#include <Serial.h>

/*
 * Global Console State
 *
 * Maintains the current state of the boot console including framebuffer
 * dimensions, cursor position, colors, and rendering parameters.
 */
BootConsole Console = {0};

/*
 * KickStartConsole - Initialize the Boot Graphics Console
 *
 * Sets up the console with the provided framebuffer and dimensions.
 * Calculates console character grid size based on font dimensions.
 * Initializes cursor position to top-left and sets default colors.
 *
 * Parameters:
 * - __FrameBuffer__: Pointer to the linear framebuffer memory
 * - __CW__: Framebuffer width in pixels
 * - __CH__: Framebuffer height in pixels
 *
 * Note: Console dimensions are calculated as CW/FontW x CH/FontH characters.
 * Default colors are white text on black background.
 */
void
KickStartConsole(
uint32_t *__FrameBuffer__,
uint32_t __CW__,
uint32_t __CH__)
{
    Console.FrameBuffer = __FrameBuffer__;
    Console.FrameBufferW = __CW__;
    Console.FrameBufferH = __CH__;
    Console.ConsoleCol = __CW__ / FontW;
    Console.ConsoleRow = __CH__ / FontH;
    Console.CursorX = 0;
    Console.CursorY = 0;
    Console.TXColor = 0xFFFFFF;  /* White text */
    Console.BGColor = 0x000000;  /* Black background */
}

/*
 * ClearConsole - Clear the Entire Console Screen
 *
 * Fills the entire framebuffer with the current background color,
 * effectively clearing all text and graphics from the screen.
 * Resets the cursor position to the top-left corner (0,0).
 *
 * This function performs a complete screen refresh and should be
 * used when a full console reset is needed.
 */
void
ClearConsole(void)
{
    /* Fill entire framebuffer with background color */
    for (uint32_t i = 0; i < Console.FrameBufferW * Console.FrameBufferH; i++)
    {
        Console.FrameBuffer[i] = Console.BGColor;
    }

    /* Reset cursor to top-left position */
    Console.CursorX = 0;
    Console.CursorY = 0;
}

/*
 * ScrollConsole - Scroll the Console Screen Up by One Line
 *
 * Moves all text lines up by one character row to make space for new text
 * at the bottom. The top line is discarded, and the bottom line is cleared
 * with the background color. This implements the classic terminal scroll
 * behavior when reaching the bottom of the screen.
 *
 * The function operates at the pixel level, copying entire character blocks
 * (FontW x FontH pixels) from their current position to one row above.
 * After scrolling, the newly exposed bottom line is filled with background color.
 *
 * Performance note: This is a pixel-by-pixel copy operation that could be
 * optimized with larger block copies, but maintains simplicity for early boot.
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
            uint32_t srcY = (PosY + 1) * FontH;  /* Line below */
            uint32_t dstY = PosY * FontH;        /* Current line */
            uint32_t pixelX = PosX * FontW;      /* Character column start */

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

/*
 * PutChar - Output a Single Character to the Console
 *
 * Renders a character to the framebuffer at the current cursor position
 * and mirrors the output to the serial port for debugging. Handles
 * special control characters (\n, \r) and automatically manages cursor
 * positioning, line wrapping, and scrolling.
 *
 * Control character handling:
 * - '\n' (newline): Moves cursor to start of next line
 * - '\r' (carriage return): Moves cursor to start of current line
 * - Other characters: Renders using bitmap font and advances cursor
 *
 * Automatic behaviors:
 * - Line wrapping when cursor reaches right edge
 * - Screen scrolling when cursor reaches bottom
 * - Serial port mirroring for all characters
 *
 * Parameters:
 * - __Char__: The character to output (including control characters)
 *
 * Thread safety: Serial output is protected by spinlock in SerialPutChar.
 */
void
PutChar(char __Char__)
{
    /* Always mirror output to serial port for debugging */
    SerialPutChar(__Char__);

    if (__Char__ == '\n')  /* Newline: move to next line */
    {
        Console.CursorX = 0;
        Console.CursorY++;
    }
    else if (__Char__ == '\r')  /* Carriage return: move to line start */
    {
        Console.CursorX = 0;
    }
    else  /* Printable character: render and advance cursor */
    {
        uint32_t PixelX = Console.CursorX * FontW;
        uint32_t PixelY = Console.CursorY * FontH;

        DisplayChar(Console.FrameBuffer, Console.FrameBufferW, PixelX, PixelY, __Char__, Console.TXColor);
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

/*
 * PutPrint - Output a Null-Terminated String to the Console
 *
 * Iterates through each character in the provided string and outputs
 * it to the console using PutChar. This handles all control characters
 * and formatting automatically, including newlines and cursor management.
 *
 * The function stops when it encounters the null terminator ('\0').
 * Each character is processed individually, allowing for proper handling
 * of control sequences and automatic scrolling/wrapping.
 *
 * Parameters:
 * - __String__: Pointer to null-terminated string to output
 *
 * This is the primary string output function used by the printf system.
 */
void
PutPrint(const char *__String__)
{
    /* Process each character until null terminator */
    while (*__String__)
    {
        PutChar(*__String__);
        __String__++;
    }
}

/*
 * SetBGColor - Set Console Text and Background Colors
 *
 * Updates the current text (foreground) and background colors used
 * for subsequent character rendering. Colors are specified as 32-bit
 * RGBA values, with the framebuffer format determining interpretation.
 *
 * Parameters:
 * - __FG__: 32-bit foreground (text) color value
 * - __BG__: 32-bit background color value
 *
 * Note: Color changes affect all subsequent PutChar/PutPrint calls
 * until changed again. Existing text on screen is not redrawn.
 */
void
SetBGColor(uint32_t __FG__, uint32_t __BG__)
{
    Console.TXColor = __FG__;
    Console.BGColor = __BG__;
}

/*
 * SetCursor - Set the Console Cursor Position
 *
 * Moves the text cursor to the specified character coordinates.
 * Coordinates are bounds-checked to ensure they remain within
 * the console's character grid dimensions.
 *
 * Parameters:
 * - __CurX__: X coordinate (column) in character units (0-based)
 * - __CurY__: Y coordinate (row) in character units (0-based)
 *
 * Note: Invalid coordinates (outside console bounds) are ignored.
 * The cursor position affects where subsequent PutChar/PutPrint
 * calls will render text.
 */
void
SetCursor(uint32_t __CurX__, uint32_t __CurY__)
{
    /* Bounds checking to prevent cursor from going out of bounds */
    if (__CurX__ < Console.ConsoleCol)
        Console.CursorX = __CurX__;
    if (__CurY__ < Console.ConsoleRow)
        Console.CursorY = __CurY__;
}
