#include "gfx.h"
#include "theme.h"
#include "zskin_tabs.h"

int skin_tabs_split(const char *menu, char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX])
{
  int slot = 0, n = 0;
  const char *p;
  for (n = 0; n < SKIN_TABS; n++)
    labels[n][0] = 0;
  n = 0;
  if (!menu || !*menu)
    return 0;
  for (p = menu;; p++) {
    if (*p == '|' || *p == 0) {
      while (n > 0 && labels[slot][n - 1] == ' ')
        n--;
      labels[slot][n] = 0;
      slot++;
      n = 0;
      if (*p == 0 || slot == SKIN_TABS)
        break;
    }
    else if (*p == ' ' && n == 0) {
      /* skip leading spaces */
    }
    else if (n < SKIN_TAB_LABEL_MAX - 1) {
      labels[slot][n++] = *p;
    }
  }
  return slot;
}

gfx_color skin_tabs_fg(int menucolorbg)
{
  int r, g, b;
  if (menucolorbg <= 0 || menucolorbg > 0xFFFF || menucolorbg == 12345)
    return GFX_WHITE;
  r = (menucolorbg >> 11) & 31;
  g = (menucolorbg >> 5) & 63;
  b = menucolorbg & 31;
  if (r + g / 2 + b < 24)
    return GFX_WHITE;
  return (gfx_color)menucolorbg;
}

void skin_tabs_draw(const char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX], gfx_color fg)
{
  int i;
  gfx_fill_rect(0, RM_TAB_TOP_Y, GFX_WIDTH, RM_TAB_HEIGHT, GFX_WHITE);
  for (i = 0; i < SKIN_TABS; i++) {
    char clip[SKIN_TAB_LABEL_MAX];
    int len, w, x;
    if (!labels[i][0])
      continue;
    /* Clip label to fit within the tab slot (RM_TAB_WIDTH - 4 px margin). */
    for (len = 0; labels[i][len]; len++) clip[len] = labels[i][len];
    clip[len] = 0;
    while (len > 0 && (w = gfx_text_width(clip)) > RM_TAB_WIDTH - 4)
      clip[--len] = 0;
    w = gfx_text_width(clip);
    x = RM_TAB_X0 + i * RM_TAB_PITCH;
    /* Black tab from 1px below the tab top down to the last screen row: the white gaps between tabs and the
     * 1px row above them separate the tabs. Stopping 1px short of the bottom left white lines at each tab's
     * bottom-left and bottom-right corners. */
    gfx_fill_rect(x + 1, RM_TAB_TOP_Y + 1, RM_TAB_WIDTH - 2, GFX_HEIGHT - (RM_TAB_TOP_Y + 1), GFX_BLACK);
    gfx_text(x + (RM_TAB_WIDTH - w) / 2, RM_TAB_TOP_Y + SKIN_TAB_TEXT_DY, clip, fg, GFX_BLACK);
  }
}
