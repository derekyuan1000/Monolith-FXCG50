/* gfx: the only drawing API the skin/ widgets may use (CLAUDE.md §5).
 * Backend: zgfx_fxcg.c (calculator, VRAM + OS text).
 * Colours are RGB565. All calls clip to the 384x216 screen. */
#ifndef GFX_GFX_H
#define GFX_GFX_H
#ifdef __cplusplus
extern "C" {
#endif

#define GFX_WIDTH  384
#define GFX_HEIGHT 216
typedef unsigned short gfx_color;
#define GFX_RGB565(r, g, b) ((gfx_color)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define GFX_BLACK 0x0000
#define GFX_WHITE 0xFFFF

void gfx_fill_rect(int x, int y, int w, int h, gfx_color c);
/* y is the top of the text row as an absolute screen row (0 = top of the 216-row screen); the background rectangle is painted with bg */
void gfx_text(int x, int y, const char *s, gfx_color fg, gfx_color bg);
int gfx_text_width(const char *s);
/* The OS's large 18x24 font (the one RUN-MAT uses), fixed advance, drawn character by character. y is the top of the
 * 24-row cell as an absolute screen row (assumed: the same convention KhiCAS's printCentered uses; verify on device). */
#define GFX_LARGE_W 18
#define GFX_LARGE_H 24
void gfx_text_large(int x, int y, const char *s, gfx_color fg, gfx_color bg);

#ifdef __cplusplus
}
#endif
#endif
