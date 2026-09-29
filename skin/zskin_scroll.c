#include "gfx.h"
#include "theme.h"
#include "zskin_scroll.h"

int zskin_scroll_wanted(int total_lines, int visible_lines)
{
  return total_lines > 0 && visible_lines > 0 && total_lines > visible_lines;
}

int zskin_scroll_thumb(int total_lines, int visible_lines, int start_line, int track_h, int *y, int *h)
{
  int th, ty, max_start;
  const int min_h = 4;
  if (!zskin_scroll_wanted(total_lines, visible_lines) || track_h <= 0) return 0;
  if (start_line < 0) start_line = 0;
  max_start = total_lines - visible_lines;
  if (start_line > max_start) start_line = max_start;
  th = track_h * visible_lines / total_lines;
  if (th < min_h) th = min_h;
  if (th > track_h) th = track_h;
  /* spread the thumb's free travel over the valid offsets, so the last offset puts it flush with the bottom
   * (start*track/total fell short by rounding, e.g. 10 lines, 7 visible, 168 px: bottom at 167) */
  ty = (track_h - th) * start_line / max_start;
  *y = ty;
  *h = th;
  return 1;
}

void zskin_scroll_draw(int total_lines, int visible_lines, int start_line, int track_top, int track_h)
{
  int y, h;
  if (!zskin_scroll_thumb(total_lines, visible_lines, start_line, track_h, &y, &h)) return;
  gfx_fill_rect(RM_SCROLLBAR_X, track_top, GFX_WIDTH - RM_SCROLLBAR_X, track_h, GFX_WHITE);
  gfx_fill_rect(RM_SCROLLBAR_X, track_top + y, GFX_WIDTH - RM_SCROLLBAR_X, h, GFX_BLACK);
}
