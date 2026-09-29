/* Calculator backend. Named z*.c so the linker script puts it in .rominram
 * (the .ac2 part, 260 KB free) instead of the full main region. */
#include <fxcg/display.h>
#include "gfx.h"

void gfx_fill_rect(int x, int y, int w, int h, gfx_color c)
{
  unsigned short *vram = (unsigned short *)GetVRAMAddress();
  int xx, yy, x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > GFX_WIDTH) x1 = GFX_WIDTH;
  if (y1 > GFX_HEIGHT) y1 = GFX_HEIGHT;
  for (yy = y; yy < y1; yy++)
    for (xx = x; xx < x1; xx++)
      vram[yy * GFX_WIDTH + xx] = c;
}

int gfx_text_width(const char *s)
{
  int px = 0, py = 0;
  PrintMini(&px, &py, (unsigned char *)s, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0, 0);
  return px;
}

/* KhiCAS's PrintMini/Bdisp_MMPrint calls measure y from the top of the work
 * area, i.e. absolute screen row minus the 24 px OS status area (old casiostatus
 * used py=174 for absolute row 198; console.cc puts "Edit" at
 * LCD_HEIGHT_PX-STATUS_AREA_PX-17 while its band line is at absolute row 195).
 * gfx_text takes an ABSOLUTE screen row, so convert here. */
#define OS_TEXT_Y_ORIGIN 24

void gfx_text_large(int x, int y, const char *s, gfx_color fg, gfx_color bg)
{
  char one[2];
  one[1] = 0;
  for (; *s; s++, x += GFX_LARGE_W) {
    one[0] = *s;
    PrintCXY(x, y, one, 0x40, -1, fg, bg, 1, 0);
  }
}

void gfx_text(int x, int y, const char *s, gfx_color fg, gfx_color bg)
{
  int px = x, py = y - OS_TEXT_Y_ORIGIN;
  PrintMini(&px, &py, (unsigned char *)s, 0, 0xFFFFFFFF, 0, 0, fg, bg, 1, 0);
}
