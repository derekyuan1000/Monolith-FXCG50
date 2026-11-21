/* Catppuccin Mocha skin for KhiCAS (fx-CG50), applied at link time.
 *
 * KhiCAS draws with hard-coded colours (white background, black text) in
 * hundreds of places. Instead of patching every call, the screen is recoloured
 * just before it reaches the LCD: the linker routes Bdisp_PutDisp_DD, GetKey
 * and GetKeyWait_OS (which refresh the LCD from VRAM) through the wrappers
 * below (-Wl,--wrap=...), and every VRAM pixel whose colour is one of
 * KhiCAS's palette colours is replaced by its Mocha counterpart. Mocha colours
 * are never sources, so recolouring an already recoloured screen changes
 * nothing, and colours that are not in the tables (3D shading, images) are
 * left as they are.
 *
 * The screen has three bands with their own table, like Upsilon:
 *   rows   0- 23  status bar  -> mauve bar with dark text (Upsilon's title bar)
 *   rows  24-197  content     -> base background, text-coloured text
 *   rows 198-215  F-key menu  -> menu colours turned into dark tints
 *
 * Inverted text (the selected history line) is drawn as Upsilon's selection:
 * surface1 background with normal text, instead of a solid text-coloured bar.
 */
#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <string.h>
#include "mocha_tables.h"

#define TOP_ROWS 24
#define MENU_FIRST_ROW 198

static void remap_rows(unsigned short * p, unsigned short * end, const unsigned int * table) {
  for (; p < end; ++p) {
    unsigned int c = *p;
    unsigned int h = ((c * MOCHA_HASH_K) >> 10) & (MOCHA_HASH_SIZE - 1);
    for (;;) {
      unsigned int e = table[h];
      if (e == 0xFFFFFFFFu) break;            // empty slot: colour not remapped
      if ((e >> 16) == c) { *p = (unsigned short)e; break; }
      h = (h + 1) & (MOCHA_HASH_SIZE - 1);
    }
  }
}

/* Set while a QR code is on screen: phone scanners need it dark on light */
int mocha_paused = 0;

void mocha_remap_vram(void) {
  if (mocha_paused) return;
  unsigned short * vram = (unsigned short *)GetVRAMAddress();
  remap_rows(vram, vram + TOP_ROWS * LCD_WIDTH_PX, mocha_top);
  remap_rows(vram + TOP_ROWS * LCD_WIDTH_PX, vram + MENU_FIRST_ROW * LCD_WIDTH_PX, mocha_body);
  remap_rows(vram + MENU_FIRST_ROW * LCD_WIDTH_PX, vram + LCD_HEIGHT_PX * LCD_WIDTH_PX, mocha_bottom);
}

/* Screen refreshes */
void __real_Bdisp_PutDisp_DD(void);
void __wrap_Bdisp_PutDisp_DD(void) {
  mocha_remap_vram();
  __real_Bdisp_PutDisp_DD();
}

int __real_GetKey(int * key);
int __wrap_GetKey(int * key) {
  mocha_remap_vram();
  return __real_GetKey(key);
}

int __real_GetKeyWait_OS(int * column, int * row, int type_of_waiting, int timeout_period, int menu, unsigned short * keycode);
int __wrap_GetKeyWait_OS(int * column, int * row, int type_of_waiting, int timeout_period, int menu, unsigned short * keycode) {
  if (type_of_waiting != KEYWAIT_HALTOFF_TIMEROFF) {
    // Only before a real wait: polling loops call this with no wait, many times per second
    mocha_remap_vram();
  }
  return __real_GetKeyWait_OS(column, row, type_of_waiting, timeout_period, menu, keycode);
}

/* Selection style */
void __real_PrintMini(int * x, int * y, unsigned char * s, int mode_flags, unsigned int xlimit, int P6, int P7, int color, int back_color, int writeflag, int P11);
void __wrap_PrintMini(int * x, int * y, unsigned char * s, int mode_flags, unsigned int xlimit, int P6, int P7, int color, int back_color, int writeflag, int P11) {
  if ((mode_flags & 4) && color == COLOR_BLACK && back_color == COLOR_WHITE) {
    // Inverted black on white: draw it the way Upsilon highlights a selected cell
    __real_PrintMini(x, y, s, mode_flags & ~4, xlimit, P6, P7, MOCHA_TEXT, MOCHA_SURFACE1, writeflag, P11);
    return;
  }
  __real_PrintMini(x, y, s, mode_flags, xlimit, P6, P7, color, back_color, writeflag, P11);
}

void __real_PrintXY(int x, int y, char * string, int mode, int color);
void __wrap_PrintXY(int x, int y, char * string, int mode, int color) {
  if (mode == TEXT_MODE_INVERT && color == TEXT_COLOR_BLACK && x >= 1 && y >= 1 && y <= 8) {
    // Same for the OS font (menus): surface1 cell, then the text on top of it.
    // PrintXY skips the first two characters; cells are 18x24 below the status bar.
    int len = (int)strlen(string) - 2;
    if (len > 0) {
      int px = (x - 1) * 18, py = TOP_ROWS + (y - 1) * 24, w = len * 18;
      if (px + w > LCD_WIDTH_PX) w = LCD_WIDTH_PX - px;
      unsigned short * vram = (unsigned short *)GetVRAMAddress() + py * LCD_WIDTH_PX + px;
      for (int r = 0; r < 24 && w > 0; ++r, vram += LCD_WIDTH_PX) {
        for (int i = 0; i < w; ++i) vram[i] = MOCHA_SURFACE1;
      }
      __real_PrintXY(x, y, string, TEXT_MODE_TRANSPARENT_BACKGROUND, TEXT_COLOR_BLACK);
      return;
    }
  }
  __real_PrintXY(x, y, string, mode, color);
}
