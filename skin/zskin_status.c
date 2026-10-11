#define ZSKIN_STATUS_C
#include <fxcg/display.h>
#include <string.h>
#include "zskin_status.h"
#include "gfx.h"

#define ICON_FLAGS    (SAF_BATTERY | SAF_ALPHA_SHIFT | SAF_SETUP_ANGLE)
#define TEXT_FLAGS    (SAF_BATTERY | SAF_TEXT | SAF_GLYPH | SAF_ALPHA_SHIFT)
/* RUNMAT_FLAGS keeps SAF_BATTERY (battery icon on right) but drops
 * SAF_ALPHA_SHIFT to reduce autonomous OS refreshes.  The wait loop in
 * in_ckgetkey now calls set_xcas_status() before every Bdisp_PutDisp_DD
 * so boxes are redrawn each flip regardless. */
#define RUNMAT_FLAGS  (SAF_TEXT | SAF_GLYPH)

static int mode = 0; /* 0 unknown, 1 icons, 2 text, 3 runmat */

void zskin_status_icons(void)
{
  if (mode != 1) {
    DefineStatusAreaFlags(3, ICON_FLAGS, 0, 0);
    mode = 1;
  }
}

void zskin_status_message(char *msg, short p2, char color, char p4)
{
  if (mode != 2) {
    DefineStatusAreaFlags(3, TEXT_FLAGS, 0, 0);
    mode = 2;
  }
  DefineStatusMessage(msg, p2, color, p4);
}

void zskin_status_runmat(int is_radian)
{
  /* RUN-MAT style status bar:  [Math] [Deg/Rad] [Norm1] [d/c] [Real]
   *
   * Strategy: let the OS render the text string via DefineStatusMessage /
   * DisplayStatusArea, then overlay 1px box borders with gfx_fill_rect.
   * gfx_fill_rect writes directly to VRAM at absolute y coords so it works
   * in the 24px status bar area.
   *
   * X0 = left pixel where DefineStatusMessage starts its text string.
   * PAD = padding pixels between the text and the box border.
   * Tune X0 if boxes are still offset: increase to shift boxes right,
   * decrease (or set 0) to shift left. */
  static char buf[48];
  const char *angle = is_radian ? "Rad" : "Deg";
  const char *labels[5] = {"Math", angle, "Norm1", "d/c", "Real"};
  /* gap strings between adjacent boxes: narrowed Norm1-d/c gap */
  const char *seps[4]   = {"  ", "  ", " ", "  "};
  int i, x, lw, bx, bw;
  const int PAD = 1;   /* box padding each side */
  const int BY  = 2;   /* top of box, absolute y in status bar */
  const int BH  = 19;  /* box height */
  /* The OS draws the message from the left edge, so the text starts with LEAD: the first box's left border
   * (X0 - PAD) then lands on screen instead of at x = -1, where it was clipped. */
  static const char LEAD[] = " ";
  const int X0  = gfx_text_width(LEAD);

  /* Build text string — separators must match seps[] widths above */
  strncpy(buf, LEAD, sizeof(buf) - 1);
  strncat(buf, "Math  ", sizeof(buf) - 1 - strlen(buf));
  strncat(buf, angle, sizeof(buf) - 1 - strlen(buf));
  strncat(buf, "  Norm1 d/c  Real", sizeof(buf) - 1 - strlen(buf));
  buf[sizeof(buf) - 1] = 0;

  /* 1. Set RUNMAT mode (once) and render text into VRAM via OS */
  if (mode != 3) {
    DefineStatusAreaFlags(3, RUNMAT_FLAGS, 0, 0);
    mode = 3;
  }
  DefineStatusMessage(buf, 1, 0, 0);
  DisplayStatusArea();

  /* 2. Draw box borders on top of the OS-rendered text.
   *    gfx_fill_rect clips x<0 so the first box edge is safe at bx=-PAD. */
  x = X0;
  for (i = 0; i < 5; i++) {
    lw = gfx_text_width(labels[i]);
    bx = x - PAD;
    bw = lw + 2 * PAD;
    gfx_fill_rect(bx,      BY,        bw, 1,  GFX_BLACK);
    gfx_fill_rect(bx,      BY + BH-1, bw, 1,  GFX_BLACK);
    gfx_fill_rect(bx,      BY,        1,  BH, GFX_BLACK);
    gfx_fill_rect(bx+bw-1, BY,        1,  BH, GFX_BLACK);
    if (i < 4)
      x += lw + gfx_text_width(seps[i]);
  }
}
