/* Stacked (2D) results in the Monolith shell: Giac/OS glue. See zstack.h. Compiled as zstack.o (.rominram).
 *
 * Only UI code is used: the parser (gen from a string, as the session loader does), taille(), and the 2D typesetter
 * of zdisplay.cc (Equation_compute_size, Equation_total_size, Equation_select, Equation_draw). No Giac algebra is
 * called or changed.
 *
 * Stack bound: the typesetter recurses once per tree level (Equation_compute_size 376 B + Equation_compute_symb_size
 * 468 B per operator, Equation_draw 292 B, measured with -fstack-usage). A tree is typeset only if its nesting is at
 * most ZSTACK_MAX_TREE (checked first by depth_ok, whose recursion stops at that depth), i.e. about 12 operator
 * levels: about 10 KB of stack at most, less than what the stock 2D editor uses on the same results. Before the
 * parse, zstack_wanted() bounds the string (ZSTACK_MAX_LEN characters, ZSTACK_MAX_PAREN brackets, ZSTACK_MAX_OPS
 * operators), which bounds the parser's own recursion and the depth_ok walk.
 *
 * Heap: one cache entry holds a copy of the string and the laid-out tree; entries are replaced only when a new
 * string scrolls into view, so a redraw of an unchanged screen allocates nothing except the typesetter's own
 * temporary strings while drawing. */
#include "giacPCH.h"
#include <fxcg/display.h>
#include <string.h>
#include <stdlib.h>
#include "kdisplay.h"
#include "console.h"
#include "main.h"
#include "zstack.h"
#include "nin.h"

/* shell state (console.cc) */
extern console_line *Line;
extern location Cursor;
extern int Start_Line, Last_Line;
extern int vfontsize;

namespace {
  struct entry {
    char *str;           /* 0: free slot */
    giac::gen *tree;     /* laid-out tree; 0: this string is drawn as text (negative entry) */
    short w, h;          /* block size */
    unsigned stamp;      /* frame of the last use */
    int pc;              /* python_compat when parsed: part of the key (the same text parses differently) */
  };
}

static entry cache[ZSTACK_CACHE];
static unsigned frame;
static signed char visible[LINE_DISP_MAX];   /* cache slot of each visible level of the current frame, -1 none */
static xcas::Equation *origin;               /* Equation_draw takes positions relative to an Equation at (0,0) */
static unsigned char lkind[LINE_DISP_MAX];   /* how each visible level is drawn when ZSTACK_2D: LK_OUT typesetter, LK_IN nin */
static nin *scratch;                         /* the natural-input model used to parse and draw past inputs (heap, first use) */
enum { LK_OUT = 0, LK_IN = 1 };

static int nin_char_w(void *ctx, char c) { (void)ctx; (void)c; return GFX_LARGE_W; }
/* The large-font metrics of the natural editor (skin/znat.cc `met`), same values; the clip is the work area. */
static const nin_metrics nmet = {
  nin_char_w, 0,          /* char_w, ctx */
  GFX_LARGE_H,            /* char_h */
  16, 22,                 /* slot_w, slot_h */
  2, 3, 2,                /* bar_h, bar_pad, gap */
  0, 24, GFX_WIDTH, 194,  /* clip rectangle: the work area between the status area and the tab bar */
  1                       /* large font */
};

static void free_entry(entry &e)
{
  if (e.tree) delete e.tree;
  if (e.str) delete[] e.str;
  e.tree = 0;
  e.str = 0;
}

void zstack_release(void)
{
  for (int i = 0; i < ZSTACK_CACHE; ++i) free_entry(cache[i]);
  for (int i = 0; i < LINE_DISP_MAX; ++i) visible[i] = -1;
  if (scratch) delete scratch;
  scratch = 0;
}

/* Low memory: give back what the current frame does not use (its slots may be in visible[]). */
static void release_unused(void)
{
  for (int i = 0; i < ZSTACK_CACHE; ++i)
    if (cache[i].stamp != frame) free_entry(cache[i]);
}

/* Nesting of g is at most budget. Recursion stops at the budget, so this walk is itself bounded. */
static bool depth_ok(const giac::gen &g, int budget)
{
  if (budget < 0) return false;
  switch (g.type) {
  case giac::_SYMB:
    return depth_ok(g._SYMBptr->feuille, budget - 1);
  case giac::_FRAC:
    return depth_ok(g._FRACptr->num, budget - 1) && depth_ok(g._FRACptr->den, budget - 1);
  case giac::_VECT: {
    const giac::vecteur &v = *g._VECTptr;
    for (unsigned i = 0; i < v.size(); ++i)
      if (!depth_ok(v[i], budget - 1)) return false;
    return true;
  }
  default:
    return true;
  }
}

/* Parse str as the session loader would, silently, leaving no trace for the REPL. A syntax error makes the
 * parser print a message (parser_error -> alert -> logptr -> dConsolePutChar -> Console_Output/Console_NewLine),
 * which would add a history line in the middle of Console_Disp: console output is muted with dconsole_mode = 0
 * during the parse, as main.cc does around its own gen(s,contextptr), and every parser global the parse can set
 * (first_error_line, error_token_name, parser_error, parsed_gen) is restored while still muted (the
 * parser_error setter prints too). */
static bool parse(const char *str, giac::gen &g)
{
  int save_mode = dconsole_mode;
  dconsole_mode = 0;
  int fel = giac::first_error_line(contextptr);
  std::string etn = giac::error_token_name(contextptr);
  std::string perr = giac::parser_error(contextptr);
  giac::gen pgen = giac::parsed_gen(contextptr);
  giac::first_error_line(0, contextptr);
  g = giac::gen(std::string(str), contextptr);
  bool ok = giac::first_error_line(contextptr) == 0 && (g.type == giac::_SYMB || g.type == giac::_FRAC);
  giac::parsed_gen(pgen, contextptr);
  giac::first_error_line(fel, contextptr);
  giac::parser_error(perr, contextptr);
  giac::error_token_name(etn, contextptr);
  dconsole_mode = save_mode;
  return ok;
}

/* Lay out str: font 18, then 16, then 14 when too big (as main.cc's 2D view shrinks). 0 if nothing fits. */
static giac::gen *typeset(const char *str, int *w, int *h)
{
  static const int sizes[3] = { 18, 16, 14 };
  giac::gen g;
  if (!parse(str, g)) return 0;
  if (!depth_ok(g, ZSTACK_MAX_TREE)) return 0;
  if (giac::taille(g, xcas::max_prettyprint_equation) >= xcas::max_prettyprint_equation) return 0;
  for (int k = 0; k < 3; ++k) {
    giac::gen t = xcas::Equation_compute_size(g, giac::attributs(sizes[k], COLOR_WHITE, COLOR_BLACK), LCD_WIDTH_PX, contextptr);
    giac::eqwdata d = xcas::Equation_total_size(t);
    if (d.dx > 0 && d.dx <= ZSTACK_MAX_W && d.dy + ZSTACK_PAD <= ZSTACK_MAX_H) {
      giac::gen *p = new giac::gen(t);
      if (!p) return 0;
      *w = d.dx;
      *h = d.dy + ZSTACK_PAD;
      return p;
    }
  }
  return 0;
}

/* Cache slot for str (filled on a miss), or -1 when str is drawn as text. */
static int lookup(const char *str)
{
  int i, len, victim = 0, pc = giac::python_compat(contextptr);
  if (!zstack_wanted(str)) return -1;
  if (xcas_python_eval != 0) return -1;                 /* MicroPython / JS output: not Giac syntax */
  for (i = 0; i < ZSTACK_CACHE; ++i)
    if (cache[i].str && cache[i].pc == pc && strcmp(cache[i].str, str) == 0) {
      cache[i].stamp = frame;
      return cache[i].tree ? i : -1;
    }
  /* miss: parse and lay out, unless that is unsafe now (not cached, retried later) */
  if (giac::execution_in_progress) return -1;
  if (get_free_memory() < ZSTACK_MIN_FREE) { release_unused(); return -1; }
  for (i = 0; i < ZSTACK_CACHE; ++i)                     /* a free slot, else the least recently used one */
    if (!cache[i].str) { victim = i; break; }
    else if (cache[i].stamp < cache[victim].stamp) victim = i;
  if (cache[victim].str && cache[victim].stamp == frame) return -1;   /* all in use by this frame (cannot happen) */
  free_entry(cache[victim]);
  len = strlen(str);
  char *copy = new char[len + 1];
  if (!copy) return -1;
  memcpy(copy, str, len + 1);
  entry &e = cache[victim];
  int w = 0, h = 0;
  e.tree = typeset(str, &w, &h);
  e.str = copy;
  e.w = (short)w;
  e.h = (short)h;
  e.stamp = frame;
  e.pc = pc;
  return e.tree ? victim : -1;
}

static void draw_slot(int slot, int right_x, int top_y, int selected)
{
  entry &e = cache[slot];
  if (!e.tree) return;
  if (!origin) {
    origin = new xcas::Equation(0, 0, giac::gen(0));
    if (!origin) return;
  }
  giac::eqwdata d = xcas::Equation_total_size(*e.tree);
  /* Equation_draw puts math point (mx, my) at screen (mx - xoff, yoff - my); the box is (d.x, d.y, d.dx, d.dy) */
  int xoff = d.x + d.dx - right_x;
  int yoff = top_y + ZSTACK_PAD_TOP + d.y + d.dy;
  int save_clip = giac::clip_ymin;
  giac::clip_ymin = 24;   /* never draw over the status area */
  if (selected) xcas::Equation_select(*e.tree, true);
  xcas::Equation_draw(*e.tree, xoff, yoff, RAND_MAX, 0, origin);
  if (selected) xcas::Equation_select(*e.tree, false);
  giac::clip_ymin = save_clip;
}

/* Parse a past input into the scratch and size its block: 1, with *h the block height and *axis the offset of the axis from the
 * block top, when it is drawn through nin; 0 when it stays text. The scratch then holds the parsed tree (no caret). */
static int input_block(const char *str, int *h, int *axis)
{
  nin_size sz;
  if (!zstack_input_wanted(str) || xcas_python_eval != 0) return 0;
  if (!scratch) {
    if (get_free_memory() < ZSTACK_MIN_FREE) return 0;
    scratch = new nin;
    if (!scratch) return 0;
  }
  /* 0: built; a warning (a byte was replaced) or NIN_ERR_FULL (too big for the pool) leave the level as text */
  if (nin_from_string(scratch, str) != 0 || scratch->templates == 0) return 0;
  scratch->caret_row = NIN_NONE;                       /* no row matches: nin_draw draws no caret */
  nin_measure(scratch, &nmet, &sz);
  return zstack_input_block(sz.a, sz.b, sz.w, vfontsize, LINE_DISP_MAX * vfontsize, LCD_WIDTH_PX - 8, h, axis);
}

/* Draw the past input at block top y (offset below the status area); selected: a frame around the block. */
static void draw_input(const char *str, int y, int selected)
{
  int h = 0, axis = 0, w;
  nin_size sz;
  if (!input_block(str, &h, &axis)) return;
  nin_draw(scratch, &nmet, 2, 24 + y + axis, 0);
  if (selected) {
    nin_measure(scratch, &nmet, &sz);
    w = sz.w + 4;
    gfx_fill_rect(0, 24 + y, w, 1, GFX_BLACK);
    gfx_fill_rect(0, 24 + y + h - 1, w, 1, GFX_BLACK);
    gfx_fill_rect(0, 24 + y, 1, h, GFX_BLACK);
    gfx_fill_rect(w - 1, 24 + y, 1, h, GFX_BLACK);
  }
}

int zstack_measure(const char *str, int *w, int *h)
{
  int slot = lookup(str);
  if (slot < 0) return 0;
  *w = cache[slot].w;
  *h = cache[slot].h;
  return 1;
}

void zstack_draw(const char *str, int right_x, int top_y, int selected)
{
  int slot = lookup(str);
  if (slot >= 0) draw_slot(slot, right_x, top_y, selected);
}

int zstack_console_layout(int *y, unsigned char *mode)
{
  int h[LINE_DISP_MAX], n = 0, i, any = 0;
  ++frame;
  for (i = 0; i < LINE_DISP_MAX; ++i) {
    y[i] = i * vfontsize;
    mode[i] = ZSTACK_TEXT;
    visible[i] = -1;
    lkind[i] = LK_OUT;
  }
  for (i = 0; i < LINE_DISP_MAX && i + Start_Line <= Last_Line; ++i, ++n) {
    console_line &l = Line[i + Start_Line];
    h[i] = vfontsize;
    if (l.type == LINE_TYPE_INPUT) {
      /* a past input written with templates is drawn through nin. Never the line being edited: the last line, and
       * the cursor line unless it is read-only (then it is drawn with a frame). */
      int ih, iaxis;
      if (!l.str || i + Start_Line >= Last_Line || (i == Cursor.y && !l.readonly)) continue;
      if (!input_block((const char *)l.str, &ih, &iaxis)) continue;
      lkind[i] = LK_IN;
      mode[i] = ZSTACK_2D;
      h[i] = ih;
      any = 1;
      continue;
    }
    /* OUTPUT levels only; on the cursor only when read-only (then drawn selected, like the stock PrintRev) */
    if (l.type != LINE_TYPE_OUTPUT || !l.str || (i == Cursor.y && !l.readonly)) continue;
    /* a Giac result: the only output line between two input lines. Lines printed by a program (print(), dates,
     * 'mysqrt(2)') come in runs, or before the program's result, and stay text. */
    int k = i + Start_Line;
    if (k < 1 || k >= Last_Line || Line[k - 1].type != LINE_TYPE_INPUT || Line[k + 1].type != LINE_TYPE_INPUT) continue;
    int slot = lookup((const char *)l.str);
    if (slot < 0) continue;
    visible[i] = (signed char)slot;
    mode[i] = ZSTACK_2D;
    h[i] = cache[slot].h;
    any = 1;
  }
  if (!any) return 0;   /* stock layout, set above */
  int yy[LINE_DISP_MAX];
  zstack_layout(h, n, Cursor.y, LINE_DISP_MAX * vfontsize, yy);
  for (i = 0; i < n; ++i) {
    if (yy[i] == ZSTACK_HIDDEN) mode[i] = ZSTACK_OFF;
    else y[i] = yy[i];
  }
  return 1;
}

void zstack_console_draw(int i, int y, int selected)
{
  if (i < 0 || i >= LINE_DISP_MAX) return;
  if (lkind[i] == LK_IN) {                 /* (Console_Disp does not call this for a hidden level) */
    draw_input((const char *)Line[i + Start_Line].str, y, selected);
    return;
  }
  if (visible[i] < 0) return;
  draw_slot(visible[i], ZSTACK_RIGHT_X, 24 + y, selected);
}
