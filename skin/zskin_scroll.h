#ifndef ZSKIN_SCROLL_H
#define ZSKIN_SCROLL_H
/* RUN-MAT-style history scrollbar (Monolith M2 task 7, docs/RUNMAT_SPEC.md "Scrollbar" row). Pure geometry
 * (host-tested, no state) plus a small gfx.h draw wrapper, same split as skin/zskin_tabs.c. */
#include "gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 1 when a scrollbar should be drawn at all (the history has more lines than the screen currently shows); 0
 * otherwise (including total_lines <= 0 or visible_lines <= 0). Pure function, no drawing. */
int zskin_scroll_wanted(int total_lines, int visible_lines);

/* Thumb rectangle within a vertical track of height track_h, relative to the track's own top (0 <= *y,
 * *y + *h <= track_h): *y and *h are set and 1 is returned only when zskin_scroll_wanted() is true and
 * track_h > 0; otherwise 0 is returned and *y and *h are left untouched. start_line (0-based index of the topmost visible line)
 * is clamped to a valid range first. Integer-only arithmetic: this target has no FPU (CLAUDE.md hard
 * constraints, -m4a-nofpu). Minimum thumb height 4 px so a long history never shrinks it to nothing. */
int zskin_scroll_thumb(int total_lines, int visible_lines, int start_line, int track_h, int *y, int *h);

/* Draws the scrollbar at theme.h's RM_SCROLLBAR_X..GFX_WIDTH-1, track top track_top (absolute screen y), height
 * track_h (the caller's visible-line pixel height, which varies with the font-size setting). Does nothing when
 * zskin_scroll_thumb() says there is nothing to draw (scrollbar not needed). Colours are PROVISIONAL:
 * docs/RUNMAT_SPEC.md's "Scrollbar" row records the real native shading (black/cyan/blue/frame bands) as not
 * yet sampled from an emulator capture; this draws a plain white track with a solid black thumb as a stand-in,
 * to be replaced once a real capture exists. */
void zskin_scroll_draw(int total_lines, int visible_lines, int start_line, int track_top, int track_h);

#ifdef __cplusplus
}
#endif
#endif
