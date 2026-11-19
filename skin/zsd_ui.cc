/* S<->D result-form pop-up (Monolith M2 task 6): the giac/fxcg-dependent half. See zsd.h for the pure command
 * mapping and the design write-up (which form maps to which command, and why). This file is the UI glue,
 * patterned on skin/znat.cc's own modal sub-menus (math_submenu) and split out from skin/zsd.cc for the same
 * reason skin/zstack.cc is split from skin/zstack_rules.c: this file needs giac/fxcg headers, which would make
 * zsd.cc unbuildable on the host.
 *
 * Declared with no header of its own: `int zsd_open(GIAC_CONTEXT);` is forward-declared directly in console.cc
 * next to `int khicas_addins_menu(GIAC_CONTEXT);`, which the fork already declares the exact same way for the
 * same reason (a one-line hook the rest of the shell has no shared struct or constant to share).
 *
 * Native-list-style modal: a white panel of ZSD_FORM_COUNT rows sits directly above the tab bar, the same
 * bottom-anchored panel skin/znat.cc's editor uses (RM_TAB_TOP_Y from theme.h). The current row is drawn
 * inverted: CLAUDE.md section 2 already documents "current level in reverse colours" as this shell's existing
 * selection convention, reused here because docs/RUNMAT_SPEC.md has no measured OS selection colour yet
 * ("Colours: mostly TBD") -- PROVISIONAL, like the rest of this panel's geometry, pending an emulator/device
 * capture. Row height is theme.h's RM_LINE_PITCH, an already-measured native constant, not a new invented number.
 * UP/DOWN move the highlighted row (wrapping); EXE or OK evaluates the highlighted form; EXIT closes with no
 * change, matching the brief's "EXIT closes" and leaving history untouched either way.
 *
 * "The last result": Giac's own ans() rather than a re-extracted copy of the last OUTPUT line's string (zsd.h
 * explains why). giac::history_out(contextptr).empty() is the same test _ans() itself makes (kprog.cc), used
 * here only to gate opening the pop-up at all (brief item 3: "no OUTPUT level exists yet: show a message and do
 * nothing, no crash").
 *
 * Evaluation path: put the wrapper command into the (empty) edit line, then Console_NewLine(LINE_TYPE_INPUT, 1),
 * exactly the path skin/znat.cc's own EXE branch uses for a freshly typed line (its comment there: "Console_Eval
 * is for re-running an OLD line", which is not what S<->D does -- it always appends a fresh input+output pair,
 * never overwriting or re-running existing history). Compiled as zsd_ui.o (.rominram, see tools/fork_objs.txt). */
#include "giacPCH.h"
#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <fxcg/misc.h>
#include <string.h>
#include "kdisplay.h"
#include "catalogGUI.hpp"   /* extern int lang; (0 english, 1 francais), same include znat.cc needs it from */
#include "console.h"
#include "main.h"
#include "gfx.h"
#include "zflip.h"
#include "theme.h"
#include "zsd.h"

extern int g_suppress_status_flip;
static void zsd_draw(int sel)
{
  int h = ZSD_FORM_COUNT * RM_LINE_PITCH;
  int top = RM_TAB_TOP_Y - h;
  int i;
  const char * const *labels = lang ? zsd_form_label_fr : zsd_form_label;
  g_suppress_status_flip = 1;
  Console_Disp();   /* history and the tab bar behind the panel — flip suppressed */
  g_suppress_status_flip = 0;
  gfx_fill_rect(0, top, GFX_WIDTH, h, GFX_WHITE);
  gfx_fill_rect(0, top, GFX_WIDTH, 1, GFX_BLACK);   /* top border, matches znat.cc's panel_clear */
  for (i = 0; i < ZSD_FORM_COUNT; ++i) {
    int y = top + i * RM_LINE_PITCH;
    gfx_color fg = GFX_BLACK, bg = GFX_WHITE;
    if (i == sel) {
      fg = GFX_WHITE; bg = GFX_BLACK;
      gfx_fill_rect(0, y, GFX_WIDTH, RM_LINE_PITCH, GFX_BLACK);
    }
    gfx_text(6, y + 3, labels[i], fg, bg);
  }
  zflip();
}

int zsd_open(GIAC_CONTEXT)
{
  int sel = 0;
  char cmdbuf[64];
  if (giac::history_out(contextptr).empty()) {
    /* brief item 3: nothing to convert yet. One-frame status message, same idiom as znat.cc's pending_msg. */
    DefineStatusMessage((char *)(lang ? "Aucun resultat" : "No result yet"), 1, 0, 0);
    DisplayStatusArea();
    zflip();
    return CONSOLE_SUCCEEDED;
  }
  for (;;) {
    int key;
    zsd_draw(sel);
    /* menu/shiftmenu/alphamenu null: this pop-up owns the whole screen below the status area, same reasoning
     * skin/znat.cc's math_submenu documents for its own in_ckgetkey call. */
    in_ckgetkey(&key, 1, 0, 0, 0, 12345);
    /* Any key not matched below (including AC/ON) is silently ignored and the loop redraws: the same
     * behaviour skin/znat.cc's math_submenu already has for an unmatched key inside its own modal loop, so
     * this is not a new or surprising interruption path -- EXIT is still the only way out besides picking a
     * row. AC/ON's usual "interrupt a long computation" job does not apply here: nothing is computing while
     * this loop is waiting on a key. */
    if (key == KEY_CTRL_EXIT) return CONSOLE_SUCCEEDED;   /* cancel: nothing changed */
    if (key == KEY_CTRL_UP)   { sel = (sel + ZSD_FORM_COUNT - 1) % ZSD_FORM_COUNT; continue; }
    if (key == KEY_CTRL_DOWN) { sel = (sel + 1) % ZSD_FORM_COUNT; continue; }
    if (key == KEY_CTRL_EXE || key == KEY_CTRL_OK) {
      if (zsd_command_for(sel, "ans()", cmdbuf, sizeof cmdbuf) < 0)
        return CONSOLE_SUCCEEDED;   /* cannot happen: sel is always in range and the buffer is generous */
      Console_Clear_EditLine();
      if (Console_Input((const unsigned char *)cmdbuf) != CONSOLE_SUCCEEDED)
        return CONSOLE_SUCCEEDED;   /* out of memory: leave history untouched, same guard znat.cc's put_edit_line uses */
      /* A fresh line evaluated the way the stock EXE does it (znat.cc's own comment on this same call):
       * Console_NewLine's CONSOLE_NEW_LINE_SET makes Console_GetLine hand the string to run(). */
      return Console_NewLine(LINE_TYPE_INPUT, 1);
    }
  }
}
