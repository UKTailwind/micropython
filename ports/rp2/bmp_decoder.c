/*
 * @cond
 * The following section will be excluded from the documentation.
 */
#define __BMPDECODER_C__
/***********************************************************************************************************************
PicoMite MMBasic

BmpDecoder.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

// Adapted for MicroPython (Pico Computer 3): file I/O via the stream protocol on
// a Python file (g_bmp_file); GetMemory -> zeroed PSRAM alloc; error -> raised
// exception; the MMBasic display path replaced by a caller-set line callback
// (`linecallback`) that blits each RGB888 line to the framebuffer. Only the
// decode engine (decodeBMP/decodeBMPheader + helpers) is vendored.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "py/runtime.h"
#include "py/stream.h"

// The BMP source: a Python file object (bmp.c sets this before decoding).
mp_obj_t g_bmp_file;

// Decoder result (was a MMBasic header type).
typedef struct {
    int width;
    int height;
    int bitsPerPixel;
    bool success;
    int linesProcessed;
} BMP_Result;

// Per-line output hook (bmp.c installs one that draws to the framebuffer).
// Returns true to continue, false to abort the decode.
bool (*linecallback)(int *width, int *height, uint32_t *lineData, int *screenRow);

// MMBasic memory shims -> PSRAM GC heap (GetMemory returns zeroed memory).
static void *GetMemory(size_t size) {
    void *p = m_malloc_maybe(size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}
static void FreeMemorySafe(void **pp) {
    if (pp != NULL && *pp != NULL) {
        m_free(*pp);
        *pp = NULL;
    }
}
static void bmp_error(const char *message) {
    g_bmp_file = MP_OBJ_NULL;
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s"), message);
}
#define error(msg) bmp_error(msg)

// BMP file header structures
#pragma pack(push, 1)
typedef struct
{
        uint16_t bfType;      // Must be 'BM' (0x4D42)
        uint32_t bfSize;      // File size in bytes
        uint16_t bfReserved1; // Reserved, must be 0
        uint16_t bfReserved2; // Reserved, must be 0
        uint32_t bfOffBits;   // Offset to bitmap data
} BITMAPFILEHEADER;

typedef struct
{
        uint32_t biSize;         // Size of this header (40 bytes)
        int32_t biWidth;         // Width in pixels
        int32_t biHeight;        // Height in pixels (positive = bottom-up)
        uint16_t biPlanes;       // Must be 1
        uint16_t biBitCount;     // Bits per pixel (1, 4, 8, 16, 24, 32)
        uint32_t biCompression;  // Compression type (0 = uncompressed)
        uint32_t biSizeImage;    // Image size (may be 0 for uncompressed)
        int32_t biXPelsPerMeter; // Horizontal resolution
        int32_t biYPelsPerMeter; // Vertical resolution
        uint32_t biClrUsed;      // Number of colors in palette
        uint32_t biClrImportant; // Important colors (0 = all)
} BITMAPINFOHEADER;

typedef struct
{
        uint8_t rgbBlue;
        uint8_t rgbGreen;
        uint8_t rgbRed;
        uint8_t rgbReserved;
} RGBQUAD;
#pragma pack(pop)

// Compression types
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3

// Seek origins
#define SEEK_SET 0
#define SEEK_CUR 1

// Return structure

// Structure to hold line start positions for compressed formats
typedef struct
{
        long *positions; // Array of file positions for each line
        int count;       // Number of lines
} LineStartTable;
// File I/O over the Python stream (origin 0 = SET, non-zero = CUR).
size_t onBMPRead(char *pBufferOut, size_t bytesToRead) {
    const mp_stream_p_t *sp = mp_get_stream(g_bmp_file);
    uint8_t *p = (uint8_t *)pBufferOut;
    size_t got = 0;
    while (got < bytesToRead) {
        int err;
        mp_uint_t n = sp->read(g_bmp_file, p + got, bytesToRead - got, &err);
        if (n == MP_STREAM_ERROR || n == 0) {
            break;
        }
        got += n;
    }
    return got;
}
bool onBMPSeek(int offset, bool origin) {
    const mp_stream_p_t *sp = mp_get_stream(g_bmp_file);
    struct mp_stream_seek_t seek;
    seek.offset = offset;
    seek.whence = origin ? MP_SEEK_CUR : MP_SEEK_SET;
    int err;
    return sp->ioctl(g_bmp_file, MP_STREAM_SEEK, (uintptr_t)(void *)&seek, &err) != MP_STREAM_ERROR;
}

// Cleanup and error function
static void cleanupAndError(char *message, RGBQUAD **palette, uint8_t **rowBuffer,
                            uint32_t **lineData, LineStartTable **lineTable)
{
        if (palette)
                FreeMemorySafe((void **)palette);
        if (rowBuffer)
                FreeMemorySafe((void **)rowBuffer);
        if (lineData)
                FreeMemorySafe((void **)lineData);
        if (lineTable && *lineTable)
        {
                if ((*lineTable)->positions)
                {
                        FreeMemorySafe((void **)&((*lineTable)->positions));
                }
                FreeMemorySafe((void **)lineTable);
        }
        error(message);
        // This function never returns
}

// Helper function to build line start table for RLE compressed images
static LineStartTable *buildLineStartTable(int height, long dataStart)
{
        LineStartTable *table = (LineStartTable *)GetMemory(sizeof(LineStartTable));
        if (!table)
                return NULL;

        table->positions = (long *)GetMemory(height * sizeof(long));
        if (!table->positions)
        {
                FreeMemorySafe((void **)&table);
                return NULL;
        }
        table->count = height;

        // Seek to start of pixel data
        onBMPSeek(dataStart, SEEK_SET);

        int currentLine = 0;
        bool done = false;

        // Scan through RLE data and record line start positions
        while (!done && currentLine < height)
        {
                // Record current file position as start of this line
                table->positions[currentLine] = dataStart;

                // Scan through this line to find its end
                while (1)
                {
                        uint8_t count, value;
                        //            long currentPos;

                        // Remember position before reading
                        if (onBMPRead((char *)&count, 1) != 1)
                        {
                                FreeMemorySafe((void **)&(table->positions));
                                FreeMemorySafe((void **)&table);
                                return NULL;
                        }
                        dataStart += 1;

                        if (count == 0)
                        {
                                // Escape code
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;

                                if (value == 0)
                                {
                                        // End of line
                                        currentLine++;
                                        break;
                                }
                                else if (value == 1)
                                {
                                        // End of bitmap
                                        done = true;
                                        break;
                                }
                                else if (value == 2)
                                {
                                        // Delta
                                        uint8_t dx, dy;
                                        if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        {
                                                FreeMemorySafe((void **)&(table->positions));
                                                FreeMemorySafe((void **)&table);
                                                return NULL;
                                        }
                                        dataStart += 2;
                                        if (dy > 0)
                                        {
                                                currentLine += dy;
                                                break;
                                        }
                                }
                                else
                                {
                                        // Absolute mode
                                        int bytesToRead = value;
                                        int padding = bytesToRead & 1; // Pad to word boundary

                                        // Skip the data
                                        for (int i = 0; i < bytesToRead + padding; i++)
                                        {
                                                uint8_t dummy;
                                                if (onBMPRead((char *)&dummy, 1) != 1)
                                                {
                                                        FreeMemorySafe((void **)&(table->positions));
                                                        FreeMemorySafe((void **)&table);
                                                        return NULL;
                                                }
                                                dataStart += 1;
                                        }
                                }
                        }
                        else
                        {
                                // Encoded mode - skip value byte
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;
                        }
                }
        }

        return table;
}

// Helper function to build line start table for RLE4
static LineStartTable *buildLineStartTableRLE4(int height, long dataStart)
{
        LineStartTable *table = (LineStartTable *)GetMemory(sizeof(LineStartTable));
        if (!table)
                return NULL;

        table->positions = (long *)GetMemory(height * sizeof(long));
        if (!table->positions)
        {
                FreeMemorySafe((void **)&table);
                return NULL;
        }
        table->count = height;

        // Seek to start of pixel data
        onBMPSeek(dataStart, SEEK_SET);

        int currentLine = 0;
        bool done = false;

        // Scan through RLE data and record line start positions
        while (!done && currentLine < height)
        {
                // Record current file position as start of this line
                table->positions[currentLine] = dataStart;

                // Scan through this line to find its end
                while (1)
                {
                        uint8_t count, value;

                        if (onBMPRead((char *)&count, 1) != 1)
                        {
                                FreeMemorySafe((void **)&(table->positions));
                                FreeMemorySafe((void **)&table);
                                return NULL;
                        }
                        dataStart += 1;

                        if (count == 0)
                        {
                                // Escape code
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;

                                if (value == 0)
                                {
                                        // End of line
                                        currentLine++;
                                        break;
                                }
                                else if (value == 1)
                                {
                                        // End of bitmap
                                        done = true;
                                        break;
                                }
                                else if (value == 2)
                                {
                                        // Delta
                                        uint8_t dx, dy;
                                        if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        {
                                                FreeMemorySafe((void **)&(table->positions));
                                                FreeMemorySafe((void **)&table);
                                                return NULL;
                                        }
                                        dataStart += 2;
                                        if (dy > 0)
                                        {
                                                currentLine += dy;
                                                break;
                                        }
                                }
                                else
                                {
                                        // Absolute mode - pixels packed 2 per byte
                                        int bytesToRead = (value + 1) / 2;
                                        int padding = bytesToRead & 1; // Pad to word boundary

                                        // Skip the data
                                        for (int i = 0; i < bytesToRead + padding; i++)
                                        {
                                                uint8_t dummy;
                                                if (onBMPRead((char *)&dummy, 1) != 1)
                                                {
                                                        FreeMemorySafe((void **)&(table->positions));
                                                        FreeMemorySafe((void **)&table);
                                                        return NULL;
                                                }
                                                dataStart += 1;
                                        }
                                }
                        }
                        else
                        {
                                // Encoded mode - skip value byte
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;
                        }
                }
        }

        return table;
}

// Helper function to decode a single RLE8 line
static bool decodeRLE8Line(RGBQUAD *palette, int paletteSize, int width,
                           uint32_t *lineData, uint8_t *rowBuffer)
{
        memset(rowBuffer, 0, width);
        int x = 0;
        bool lineComplete = false;

        while (!lineComplete)
        {
                uint8_t count, value;

                if (onBMPRead((char *)&count, 1) != 1)
                        return false;

                if (count == 0)
                {
                        // Escape code
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;

                        if (value == 0)
                        {
                                // End of line
                                lineComplete = true;
                        }
                        else if (value == 1)
                        {
                                // End of bitmap
                                lineComplete = true;
                        }
                        else if (value == 2)
                        {
                                // Delta - skip
                                uint8_t dx, dy;
                                if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        return false;
                                x += dx;
                        }
                        else
                        {
                                // Absolute mode
                                for (int i = 0; i < value && x < width; i++, x++)
                                {
                                        uint8_t pixel;
                                        if (onBMPRead((char *)&pixel, 1) != 1)
                                                return false;
                                        rowBuffer[x] = pixel;
                                }
                                // Pad to word boundary
                                if (value & 1)
                                {
                                        uint8_t dummy;
                                        onBMPRead((char *)&dummy, 1);
                                }
                        }
                }
                else
                {
                        // Encoded mode
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;
                        for (int i = 0; i < count && x < width; i++, x++)
                        {
                                rowBuffer[x] = value;
                        }
                }
        }

        // Convert to RGB888
        for (int col = 0; col < width; col++)
        {
                uint8_t index = rowBuffer[col];
                if (index < paletteSize)
                {
                        lineData[col] = (palette[index].rgbRed << 16) |
                                        (palette[index].rgbGreen << 8) |
                                        palette[index].rgbBlue;
                }
                else
                {
                        lineData[col] = 0;
                }
        }

        return true;
}

// Helper function to decode a single RLE4 line
static bool decodeRLE4Line(RGBQUAD *palette, int paletteSize, int width,
                           uint32_t *lineData, uint8_t *rowBuffer)
{
        memset(rowBuffer, 0, width);
        int x = 0;
        bool lineComplete = false;

        while (!lineComplete)
        {
                uint8_t count, value;

                if (onBMPRead((char *)&count, 1) != 1)
                        return false;

                if (count == 0)
                {
                        // Escape code
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;

                        if (value == 0)
                        {
                                // End of line
                                lineComplete = true;
                        }
                        else if (value == 1)
                        {
                                // End of bitmap
                                lineComplete = true;
                        }
                        else if (value == 2)
                        {
                                // Delta
                                uint8_t dx, dy;
                                if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        return false;
                                x += dx;
                        }
                        else
                        {
                                // Absolute mode
                                int pixelsToRead = value;
                                int bytesToRead = (pixelsToRead + 1) / 2;

                                for (int i = 0; i < bytesToRead; i++)
                                {
                                        uint8_t byte;
                                        if (onBMPRead((char *)&byte, 1) != 1)
                                                return false;

                                        if (x < width)
                                        {
                                                rowBuffer[x++] = (byte >> 4) & 0x0F;
                                        }
                                        if (pixelsToRead > 1 && x < width)
                                        {
                                                rowBuffer[x++] = byte & 0x0F;
                                        }
                                        pixelsToRead -= 2;
                                }

                                // Pad to word boundary
                                if (((value + 1) / 2) & 1)
                                {
                                        uint8_t dummy;
                                        onBMPRead((char *)&dummy, 1);
                                }
                        }
                }
                else
                {
                        // Encoded mode
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;

                        uint8_t pixel1 = (value >> 4) & 0x0F;
                        uint8_t pixel2 = value & 0x0F;

                        for (int i = 0; i < count && x < width; i++)
                        {
                                rowBuffer[x++] = (i & 1) ? pixel2 : pixel1;
                        }
                }
        }

        // Convert to RGB888
        for (int col = 0; col < width; col++)
        {
                uint8_t index = rowBuffer[col];
                if (index < paletteSize)
                {
                        lineData[col] = (palette[index].rgbRed << 16) |
                                        (palette[index].rgbGreen << 8) |
                                        palette[index].rgbBlue;
                }
                else
                {
                        lineData[col] = 0;
                }
        }

        return true;
}
void decodeBMPheader(int *width, int *height)
{
        BITMAPFILEHEADER fileHeader;
        BITMAPINFOHEADER infoHeader;

        // Set defaults in case of error
        *width = 0;
        *height = 0;

        // Read file header
        if (onBMPRead((char *)&fileHeader, sizeof(BITMAPFILEHEADER)) != sizeof(BITMAPFILEHEADER))
        {
                return;
        }

        // Check BMP signature
        if (fileHeader.bfType != 0x4D42)
        { // 'BM'
                return;
        }

        // Read info header
        if (onBMPRead((char *)&infoHeader, sizeof(BITMAPINFOHEADER)) != sizeof(BITMAPINFOHEADER))
        {
                return;
        }

        // Return dimensions
        *width = infoHeader.biWidth;
        *height = abs(infoHeader.biHeight);
        // Seek back to start of file for decodeBMP
        onBMPSeek(0, 0);
}

// Helper to extract shift amount and bit count from a bitmask
static void getMaskInfo(uint32_t mask, int *shift, int *bits)
{
        *shift = 0;
        *bits = 0;
        if (mask == 0)
                return;
        while ((mask & 1) == 0)
        {
                mask >>= 1;
                (*shift)++;
        }
        while (mask & 1)
        {
                mask >>= 1;
                (*bits)++;
        }
}

BMP_Result decodeBMP(bool topdown)
{
        BMP_Result result = {0};
        BITMAPFILEHEADER fileHeader;
        BITMAPINFOHEADER infoHeader;
        RGBQUAD *palette = NULL;
        uint8_t *rowBuffer = NULL;
        uint32_t *lineData = NULL;
        LineStartTable *lineTable = NULL;
        int col;
        int bottomUp;
        int rowSize;
        int paletteSize = 0;
        long pixelDataStart;

        // Read file header
        if (onBMPRead((char *)&fileHeader, sizeof(BITMAPFILEHEADER)) != sizeof(BITMAPFILEHEADER))
        {
                cleanupAndError("Failed to read file header", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Check BMP signature
        if (fileHeader.bfType != 0x4D42)
        { // 'BM'
                cleanupAndError("Not a valid BMP file (missing BM signature)", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Read info header
        if (onBMPRead((char *)&infoHeader, sizeof(BITMAPINFOHEADER)) != sizeof(BITMAPINFOHEADER))
        {
                cleanupAndError("Failed to read info header", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Validate header - accept BITMAPINFOHEADER (40), BITMAPV4HEADER (108), and BITMAPV5HEADER (124)
        if (infoHeader.biSize != 40 && infoHeader.biSize != 108 && infoHeader.biSize != 124)
        {
                cleanupAndError("Unsupported BMP header format", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Skip any extra header bytes for V4/V5 headers (we already read 40 bytes)
        if (infoHeader.biSize > 40)
        {
                int extraBytes = infoHeader.biSize - 40;
                onBMPSeek(extraBytes, SEEK_CUR);
        }

        // Check compression and bit depth combinations
        if (infoHeader.biCompression == BI_RLE8 && infoHeader.biBitCount != 8)
        {
                cleanupAndError("RLE8 compression requires 8-bit color", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biCompression == BI_RLE4 && infoHeader.biBitCount != 4)
        {
                cleanupAndError("RLE4 compression requires 4-bit color", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biCompression == BI_BITFIELDS && infoHeader.biBitCount != 16)
        {
                cleanupAndError("BI_BITFIELDS only supported with 16-bit color", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biCompression != BI_RGB &&
            infoHeader.biCompression != BI_RLE8 &&
            infoHeader.biCompression != BI_RLE4 &&
            infoHeader.biCompression != BI_BITFIELDS)
        {
                cleanupAndError("Unsupported compression format", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biBitCount != 1 && infoHeader.biBitCount != 4 &&
            infoHeader.biBitCount != 8 && infoHeader.biBitCount != 16 &&
            infoHeader.biBitCount != 24)
        {
                cleanupAndError("Unsupported bit depth", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Set result dimensions
        result.width = infoHeader.biWidth;
        result.height = abs(infoHeader.biHeight);
        result.bitsPerPixel = infoHeader.biBitCount;
        bottomUp = (infoHeader.biHeight > 0);

        // Read palette if needed (1-bit, 4-bit and 8-bit)
        if (infoHeader.biBitCount <= 8)
        {
                paletteSize = infoHeader.biClrUsed;
                if (paletteSize == 0)
                {
                        paletteSize = 1 << infoHeader.biBitCount; // 2 for 1-bit, 16 for 4-bit, 256 for 8-bit
                }

                palette = (RGBQUAD *)GetMemory(paletteSize * sizeof(RGBQUAD));
                if (!palette)
                {
                        cleanupAndError("Failed to allocate palette", &palette, &rowBuffer, &lineData, &lineTable);
                }

                if (onBMPRead((char *)palette, paletteSize * sizeof(RGBQUAD)) !=
                    paletteSize * sizeof(RGBQUAD))
                {
                        cleanupAndError("Failed to read palette", &palette, &rowBuffer, &lineData, &lineTable);
                }
        }

        // Allocate line data buffer for callback (RGB888 packed into uint32_t)
        lineData = (uint32_t *)GetMemory(result.width * sizeof(uint32_t));
        if (!lineData)
        {
                cleanupAndError("Failed to allocate line data buffer", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Calculate pixel data start position
        pixelDataStart = fileHeader.bfOffBits;

        // Seek to pixel data (skip any additional headers/data)
        size_t bytesRead = sizeof(BITMAPFILEHEADER) + infoHeader.biSize;
        if (palette)
                bytesRead += paletteSize * sizeof(RGBQUAD);

        // Read bitfield masks for BI_BITFIELDS format
        uint32_t redMask = 0x7C00; // Default: RGB555
        uint32_t greenMask = 0x03E0;
        uint32_t blueMask = 0x001F;
        int redShift = 10, redBits = 5;
        int greenShift = 5, greenBits = 5;
        int blueShift = 0, blueBits = 5;
        int redMax = 31, greenMax = 31, blueMax = 31;

        if (infoHeader.biCompression == BI_BITFIELDS && infoHeader.biBitCount == 16 &&
            bytesRead + 12 <= fileHeader.bfOffBits)
        {
                if (onBMPRead((char *)&redMask, 4) != 4 ||
                    onBMPRead((char *)&greenMask, 4) != 4 ||
                    onBMPRead((char *)&blueMask, 4) != 4)
                {
                        cleanupAndError("Failed to read bitfield masks", &palette, &rowBuffer, &lineData, &lineTable);
                }
                bytesRead += 12;
                getMaskInfo(redMask, &redShift, &redBits);
                getMaskInfo(greenMask, &greenShift, &greenBits);
                getMaskInfo(blueMask, &blueShift, &blueBits);
                redMax = redBits ? ((1 << redBits) - 1) : 1;
                greenMax = greenBits ? ((1 << greenBits) - 1) : 1;
                blueMax = blueBits ? ((1 << blueBits) - 1) : 1;
        }

        while (bytesRead < fileHeader.bfOffBits)
        {
                uint8_t dummy;
                onBMPRead((char *)&dummy, 1);
                bytesRead++;
        }

        // Handle RLE compressed formats
        if (infoHeader.biCompression == BI_RLE8 || infoHeader.biCompression == BI_RLE4)
        {
                // Build line start table for random access
                if (infoHeader.biCompression == BI_RLE8)
                {
                        lineTable = buildLineStartTable(result.height, pixelDataStart);
                }
                else
                {
                        lineTable = buildLineStartTableRLE4(result.height, pixelDataStart);
                }

                if (!lineTable)
                {
                        cleanupAndError("Failed to build line start table", &palette, &rowBuffer, &lineData, &lineTable);
                }

                // Allocate row buffer for RLE decoding
                rowBuffer = (uint8_t *)GetMemory(result.width);
                if (!rowBuffer)
                {
                        cleanupAndError("Failed to allocate row buffer", &palette, &rowBuffer, &lineData, &lineTable);
                }

                // Process lines
                for (int i = 0; i < result.height; i++)
                {
                        // Determine which file line to read and which screen row to report
                        int fileRow;
                        int screenRow;

                        if (topdown)
                        {
                                // topdown=true: read backwards through file
                                fileRow = result.height - 1 - i;
                                screenRow = i; // Report screen rows 0, 1, 2, ...
                        }
                        else
                        {
                                // topdown=false: read sequentially (efficient)
                                fileRow = i;
                                // RLE is bottom-up: file line 0 = image bottom → screen row 479
                                screenRow = result.height - 1 - i;
                        }

                        // Seek to start of this line
                        if (fileRow < lineTable->count && fileRow >= 0)
                        {
                                onBMPSeek(lineTable->positions[fileRow], SEEK_SET);

                                // Decode the line
                                bool success;
                                if (infoHeader.biCompression == BI_RLE8)
                                {
                                        success = decodeRLE8Line(palette, paletteSize, result.width, lineData, rowBuffer);
                                }
                                else
                                {
                                        success = decodeRLE4Line(palette, paletteSize, result.width, lineData, rowBuffer);
                                }

                                if (!success)
                                {
                                        cleanupAndError("Failed to decode RLE line", &palette, &rowBuffer, &lineData, &lineTable);
                                }

                                // Call callback with screen row position
                                if (!linecallback(&result.width, &result.height, lineData, &screenRow))
                                {
                                        result.linesProcessed = i;
                                        FreeMemorySafe((void **)&rowBuffer);
                                        FreeMemorySafe((void **)&lineData);
                                        FreeMemorySafe((void **)&(lineTable->positions));
                                        FreeMemorySafe((void **)&lineTable);
                                        FreeMemorySafe((void **)&palette);
                                        result.success = true;
                                        return result;
                                }
                        }

                        result.linesProcessed = i + 1;
                }

                // Cleanup
                FreeMemorySafe((void **)&rowBuffer);
                FreeMemorySafe((void **)&lineData);
                FreeMemorySafe((void **)&(lineTable->positions));
                FreeMemorySafe((void **)&lineTable);
                FreeMemorySafe((void **)&palette);

                result.success = true;
                return result;
        }

        // Handle uncompressed formats (BI_RGB)
        // Calculate row size with padding (rows are padded to 4-byte boundaries)
        rowSize = ((infoHeader.biBitCount * result.width + 31) / 32) * 4;

        // Allocate row buffer for reading from file
        rowBuffer = (uint8_t *)GetMemory(rowSize);
        if (!rowBuffer)
        {
                cleanupAndError("Failed to allocate row buffer", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Read and decode pixel data
        for (int i = 0; i < result.height; i++)
        {
                // Determine which file line to read and which screen row to report
                int fileRow;
                int screenRow;

                if (topdown)
                {
                        // topdown=true: read backwards through file
                        if (bottomUp)
                        {
                                fileRow = result.height - 1 - i;
                        }
                        else
                        {
                                fileRow = i;
                        }
                        screenRow = i; // Report screen rows 0, 1, 2, ...
                }
                else
                {
                        // topdown=false: read sequentially (efficient)
                        fileRow = i;
                        if (bottomUp)
                        {
                                // File line 0 = image bottom → screen row 479
                                screenRow = result.height - 1 - i;
                        }
                        else
                        {
                                // File line 0 = image top → screen row 479 (upside down)
                                screenRow = result.height - 1 - i;
                        }
                }

                // Seek to the correct line in the file
                long linePosition = pixelDataStart + ((long)fileRow * rowSize);
                onBMPSeek(linePosition, SEEK_SET);

                // Read row from file
                if (onBMPRead((char *)rowBuffer, rowSize) != rowSize)
                {
                        cleanupAndError("Failed to read pixel data", &palette, &rowBuffer, &lineData, &lineTable);
                }

                // Decode based on bit depth into lineData buffer
                switch (infoHeader.biBitCount)
                {
                case 1: // 1-bit monochrome
                        for (col = 0; col < result.width; col++)
                        {
                                int byteIndex = col / 8;
                                int bitIndex = 7 - (col % 8);
                                int bit = (rowBuffer[byteIndex] >> bitIndex) & 1;
                                if (bit < paletteSize)
                                {
                                        lineData[col] = (palette[bit].rgbRed << 16) |
                                                        (palette[bit].rgbGreen << 8) |
                                                        palette[bit].rgbBlue;
                                }
                                else
                                {
                                        lineData[col] = bit ? 0xFFFFFF : 0x000000; // White or black
                                }
                        }
                        break;

                case 4: // 4-bit indexed
                        for (col = 0; col < result.width; col++)
                        {
                                int byteIndex = col / 2;
                                int nibble = (col & 1) ? (rowBuffer[byteIndex] & 0x0F) : (rowBuffer[byteIndex] >> 4);
                                if (nibble < paletteSize)
                                {
                                        lineData[col] = (palette[nibble].rgbRed << 16) |
                                                        (palette[nibble].rgbGreen << 8) |
                                                        palette[nibble].rgbBlue;
                                }
                                else
                                {
                                        lineData[col] = 0; // Black for invalid index
                                }
                        }
                        break;

                case 8: // 8-bit indexed
                        for (col = 0; col < result.width; col++)
                        {
                                uint8_t index = rowBuffer[col];
                                if (index < paletteSize)
                                {
                                        lineData[col] = (palette[index].rgbRed << 16) |
                                                        (palette[index].rgbGreen << 8) |
                                                        palette[index].rgbBlue;
                                }
                                else
                                {
                                        lineData[col] = 0; // Black for invalid index
                                }
                        }
                        break;

                case 16: // 16-bit RGB using bitfield masks
                        for (col = 0; col < result.width; col++)
                        {
                                uint16_t pixel = *(uint16_t *)(rowBuffer + col * 2);
                                uint8_t r = ((pixel & redMask) >> redShift) * 255 / redMax;
                                uint8_t g = ((pixel & greenMask) >> greenShift) * 255 / greenMax;
                                uint8_t b = ((pixel & blueMask) >> blueShift) * 255 / blueMax;
                                lineData[col] = (r << 16) | (g << 8) | b;
                        }
                        break;

                case 24: // 24-bit BGR
                        for (col = 0; col < result.width; col++)
                        {
                                uint8_t b = rowBuffer[col * 3 + 0];
                                uint8_t g = rowBuffer[col * 3 + 1];
                                uint8_t r = rowBuffer[col * 3 + 2];
                                lineData[col] = (r << 16) | (g << 8) | b;
                        }
                        break;
                }

                // Call the callback with screen row position
                if (!linecallback(&result.width, &result.height, lineData, &screenRow))
                {
                        // Callback requested abort - clean up and return successfully
                        result.linesProcessed = i;
                        FreeMemorySafe((void **)&rowBuffer);
                        FreeMemorySafe((void **)&lineData);
                        FreeMemorySafe((void **)&palette);
                        result.success = true;
                        return result;
                }

                result.linesProcessed = i + 1;
        }

        // Cleanup
        FreeMemorySafe((void **)&rowBuffer);
        FreeMemorySafe((void **)&lineData);
        FreeMemorySafe((void **)&palette);

        result.success = true;
        return result;
}
