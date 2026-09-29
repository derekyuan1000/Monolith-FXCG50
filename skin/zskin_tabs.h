#ifndef ZSKIN_TABS_H
#define ZSKIN_TABS_H
#include "gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SKIN_TABS 6
#define SKIN_TAB_LABEL_MAX 12
#define SKIN_TAB_TEXT_DY 5 /* text top below the tab top: PROVISIONAL until Derek's emulator check (docs/DEVICE_TEST.md) */

/* Split a KhiCAS menu string ("algb | calc |edit+-|...") on '|' into up to
 * SKIN_TABS trimmed labels (extra fields ignored, long labels cut). Returns the
 * number of fields found; unused slots are left empty. */
int skin_tabs_split(const char *menu, char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX]);

/* Label colour for the tab bar from KhiCAS's menucolorbg (the old menu-band
 * colour that showed the active interpreter). Returns GFX_WHITE when the value
 * is <= 0, > 0xFFFF, the "not set" sentinel 12345, or an RGB565 colour too dark
 * to read on black (5-bit r + 6-bit g / 2 + 5-bit b < 24); else the colour itself.
 * Pure function, no drawing. */
gfx_color skin_tabs_fg(int menucolorbg);

/* Draw the native-style tab bar (black tabs, geometry from theme.h) with the
 * labels in colour fg; empty labels leave a blank slot. */
void skin_tabs_draw(const char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX], gfx_color fg);

#ifdef __cplusplus
}
#endif
#endif
