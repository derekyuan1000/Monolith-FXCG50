/* Natural-input fraction editor in the Monolith shell (milestone M1). See znat.h for the contract.
 * Compiled as znat.o so the linker script places the code in .rominram (.ac2). The nin (about 1.8 KB) and the Giac
 * string buffer come from the shell allocator (console_malloc) when the editor opens and are freed on every exit,
 * so they cost no static RAM. No heap use in the draw path (the original line is kept in a ustl::string once, on
 * entry; a fraction key press takes one temporary nin copy to undo a fraction that would not fit the panel).
 * The model (nin/) is pure C; this file is the glue to the shell: keys in, Edit_Line out. */
#include "giacPCH.h"
#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <fxcg/system.h>
#include <fxcg/misc.h>
#include <string.h>
#include "zskin_status.h"
#include "zskin_tabs.h"
#include "kdisplay.h"
#include "catalogGUI.hpp"
#include "menuGUI.hpp"
#include "console.h"
#include "main.h"
#include "gfx.h"
#include "nin.h"
#include "znat.h"

/* nin_to_giac output bound: at most NIN_MAX_ITEMS (250) items of which at most NIN_MAX_FRACS (32) are templates (any kind);
 * a template is written with its literal text instead of its 1 item (fraction 7 characters, (b)^(e) 5, sqrt 6, abs 5, surd 7,
 * logb 7, sum 8, integrate 15, subst(diff(f,x),x=x0) 18, the second derivative 20; a matrix adds 4 to 25 characters, but its
 * cells take 1 to 16 of the 65 rows, so it never beats the derivatives) plus up to two '*': at most
 * 218 + 32 * 22 = 922. The buffer lives on the heap only while the editor runs. A longer text is refused (NIN_ERR_SIZE). */
#define ZNAT_GIAC_MAX 1024

/* the shell's allocator (console.cc): new char[max(s,256)], 0 when memory is short */
void *console_malloc(unsigned s);
void console_free(void *ptr);

static nin *ed;          /* valid only while znat_edit runs */
static char *giac_buf;   /* ZNAT_GIAC_MAX bytes, same lifetime */

static int char_width(void *ctx, char c) { (void)ctx; (void)c; return GFX_LARGE_W; }

/* Large 18x24 font metrics as in NIN-DEMO (provisional until native-scale captures; see the plan constraints).
 * skin/zstack.cc's nmet (past inputs drawn 2D, M2 task 1) repeats these same seven numbers by value, because met is
 * static here: keep the two in step if this changes. */
static nin_metrics met = {
  char_width, 0,          /* char_w, ctx */
  GFX_LARGE_H,            /* char_h */
  16, 22,                 /* slot_w, slot_h */
  2, 3, 2,                /* bar_h, bar_pad, gap */
  0, 24, GFX_WIDTH, 194,  /* clip rectangle; y0 is set per redraw to the panel top */
  1                       /* large font */
};

#define WORK_TOP 24        /* below the OS status area */
#define PANEL_BOTTOM 194   /* exclusive: the tab bar starts at y=194 */
#define PANEL_MARGIN 4
#define PANEL_MIN_H 30
#define LEFT_X 6
#define RIGHT_LIMIT (GFX_WIDTH - 12)
#define PANEL_MAX_H (PANEL_BOTTOM - WORK_TOP)

/* MATRIX default size when opened from the MATH sub-bar's MAT/VCT entry (M2 task 5 decision, docs/KEY_CONFLICTS.md
 * section 2/3): 2x2, the smallest square size, so it reads as a generic matrix rather than a vector; add/remove a
 * row or column with the keys in matrix_nav() below to reach any other size up to NIN_MAT_MAX x NIN_MAT_MAX. */
#define ZNAT_MATRIX_DEFAULT_ROWS 2
#define ZNAT_MATRIX_DEFAULT_COLS 2

static const char *pending_msg;   /* status message shown until the next key */

static void panel_clear(int top, int h)
{
  gfx_fill_rect(0, top, GFX_WIDTH, h, GFX_WHITE);
  gfx_fill_rect(0, top, GFX_WIDTH, 1, GFX_BLACK);
}

/* This panel's own F-key legend (M2 task 7 review fix). Since task 7 get_current_console_menu's shell string is the
 * bare shell's JUMP|DEL|MAT|MATH|CAS|File, which does not describe this panel's keys: here F1-F3 insert
 * the menu_f1..f3 fast-menu text (console_menu below), F4 is the MATH sub-bar, F5/F6 do nothing. The shift/alpha
 * legends from get_current_console_menu stay as they are (menu_f7..f18, same keys as the shell). */
extern char menu_f1[], menu_f2[], menu_f3[];
static void editor_menu(ustl::string &menu)
{
  menu = menu_f1;
  menu += "|";
  menu += menu_f2;
  menu += "|";
  menu += menu_f3;
  menu += "|MATH||";
}

/* menu/menucolorbg: this panel's legend (editor_menu), drawn over the shell's own one that Console_Disp's
 * console_disp_status draws, before the single buffer flip so the shell legend is never shown. */
extern int g_suppress_status_flip;
static void redraw(const ustl::string &menu, int menucolorbg)
{
  nin_size sz;
  nin_caret_pos c;
  int h, top, axis, x = LEFT_X;
  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  g_suppress_status_flip = 1;
  Console_Disp();   /* history and the (blanked) current line, tab bar, status icons — flip suppressed */
  g_suppress_status_flip = 0;
  skin_tabs_split(menu.c_str(), labels);
  skin_tabs_draw(labels, skin_tabs_fg(menucolorbg));
  nin_measure(ed, &met, &sz);
  h = sz.a + sz.b + 2 * PANEL_MARGIN;
  if (h < PANEL_MIN_H) h = PANEL_MIN_H;
  if (h > PANEL_MAX_H) h = PANEL_MAX_H;   /* not reached: a fraction that would not fit is refused */
  top = PANEL_BOTTOM - h;
  met.clip_y0 = top;
  axis = top + PANEL_MARGIN + sz.a;
  panel_clear(top, h);
  nin_draw(ed, &met, x, axis, &c);
  if (c.valid && c.x > RIGHT_LIMIT) {   /* scroll left so that the caret stays visible, then draw again */
    x -= c.x - (GFX_WIDTH - 40);
    panel_clear(top, h);
    nin_draw(ed, &met, x, axis, 0);
  }
  if (pending_msg) {
    DefineStatusMessage((char *)pending_msg, 1, 0, 0);
    DisplayStatusArea();
  }
  Bdisp_PutDisp_DD();
}

/* Replace the shell's edit line by s (empty allowed); the shell cursor ends after it, like the stock editors. */
static int put_edit_line(const char *s)
{
  Console_Clear_EditLine();
  if (s && *s) return Console_Input((const unsigned char *)s);
  return CONSOLE_SUCCEEDED;
}

static void insert_text(const char *s)
{
  for (; s && *s; ++s)
    if (nin_insert_char(ed, *s) == NIN_ERR_FULL) {
      pending_msg = lang ? "Editeur plein" : "Editor full";
      return;
    }
}

static const char *serialise_error(int rc)
{
  if (rc == NIN_ERR_EMPTY_SLOT) return lang ? "Boite vide. EXIT: annuler" : "Empty box. EXIT: cancel";
  if (rc == NIN_ERR_SYNTAX) return lang ? "Parentheses. EXIT: annuler" : "Unbalanced ( ). EXIT: cancel";
  return lang ? "Trop long. EXIT: annuler" : "Too long. EXIT: cancel";
}

static int too_tall(void)
{
  nin_size sz;
  nin_measure(ed, &met, &sz);
  return sz.a + sz.b + 2 * PANEL_MARGIN > PANEL_MAX_H;
}

#define ZNAT_TOO_TALL (-100)
#define ZNAT_NO_MEMORY (-101)

/* Inserts the template named by a ZNAT_TPL_* kind (znat.h) at the caret, undone when the tree would then be
 * taller than the panel (its drawing would spill over the status area). ZNAT_TPL_NONE inserts nothing and
 * returns 0 (used only by the entry point today; see znat_edit_template). Returns 0, NIN_ERR_FULL,
 * ZNAT_TOO_TALL or ZNAT_NO_MEMORY; the tree is unchanged unless 0. (M1: fraction only; M2 task 5: every kind.) */
static int insert_kind_bounded(int kind)
{
  nin *save;
  int rc;
  if (kind == ZNAT_TPL_NONE) return 0;
  save = (nin *)console_malloc(sizeof(nin));
  if (!save) return ZNAT_NO_MEMORY;
  memcpy(save, ed, sizeof(nin));
  switch (kind) {
    case ZNAT_TPL_FRAC:     rc = nin_insert_frac(ed); break;
    case ZNAT_TPL_POW:      rc = nin_insert_template(ed, NIN_POW); break;
    case ZNAT_TPL_SQUARE:   rc = nin_insert_square(ed); break;
    case ZNAT_TPL_RECIP:    rc = nin_insert_recip(ed); break;
    case ZNAT_TPL_SQRT:     rc = nin_insert_template(ed, NIN_SQRT); break;
    case ZNAT_TPL_ABS:      rc = nin_insert_template(ed, NIN_ABS); break;
    case ZNAT_TPL_NROOT:    rc = nin_insert_template(ed, NIN_NROOT); break;
    case ZNAT_TPL_LOGAB:    rc = nin_insert_template(ed, NIN_LOGAB); break;
    case ZNAT_TPL_DERIV:    rc = nin_insert_template(ed, NIN_DERIV); break;
    case ZNAT_TPL_DERIV2:   rc = nin_insert_template(ed, NIN_DERIV2); break;
    case ZNAT_TPL_INTEGRAL: rc = nin_insert_template(ed, NIN_INTEGRAL); break;
    case ZNAT_TPL_SIGMA:    rc = nin_insert_template(ed, NIN_SIGMA); break;
    case ZNAT_TPL_MATRIX:   rc = nin_insert_matrix(ed, ZNAT_MATRIX_DEFAULT_ROWS, ZNAT_MATRIX_DEFAULT_COLS); break;
    default:                rc = NIN_ERR_CHAR; break;
  }
  if (rc == 0 && too_tall()) { memcpy(ed, save, sizeof(nin)); rc = ZNAT_TOO_TALL; }
  console_free(save);
  return rc;
}

static const char *template_error(int rc)
{
  if (rc == ZNAT_TOO_TALL) return lang ? "Trop haut" : "Too tall";
  if (rc == ZNAT_NO_MEMORY) return lang ? "Memoire" : "Memory";
  return lang ? "Editeur plein" : "Editor full";
}

/* MATH sub-bar (F4, docs/KEY_CONFLICTS.md section 2): native-style tab bar drawn with skin_tabs_draw, 7 entries
 * over two pages (SKIN_TABS is 6, F1-F6). Decision (M2 task 5, least invasive, documented per plan instructions):
 * each entry is its own F-key inside the sub-bar (not arrow+EXE, not a number fast-pick), the same idiom already
 * used for the shell's own tab bar and the CAS sub-bar (docs/KEY_CONFLICTS.md section 2 "Sub-bars use the native
 * look"); F6 is the page-next / page-back "> " / "<" tab, as in the CAS sub-bar. EXIT with no selection returns
 * ZNAT_TPL_NONE (panel unchanged). The n-th root key is wired directly to the physical key in the loop below
 * (KEY_CHAR_POWROOT), not offered here, because KEY_CONFLICTS.md section 2 lists 7 sub-bar entries and none of
 * them is "n-th root"; see the addendum in that file for the reasoning. */
static void math_draw_page(int page, int menucolorbg)
{
  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  int i;
  for (i = 0; i < SKIN_TABS; i++) labels[i][0] = 0;
  if (page == 0) {
    strcpy(labels[0], "MAT/VCT");
    strcpy(labels[1], "logab");
    strcpy(labels[2], "Abs");
    strcpy(labels[3], "d/dx");
    strcpy(labels[4], "d2/dx2");
    strcpy(labels[5], ">");
  } else {
    strcpy(labels[0], "integral");
    strcpy(labels[1], "sigma");
    strcpy(labels[5], "<");
  }
  skin_tabs_draw(labels, skin_tabs_fg(menucolorbg));
}

/* Runs the MATH sub-bar until a template is picked (returned as a ZNAT_TPL_*) or EXIT is pressed (ZNAT_TPL_NONE).
 * The panel above the tab row is left exactly as redraw() last drew it; only the tab row changes. */
static int math_submenu(int menucolorbg)
{
  int page = 0;
  for (;;) {
    int key;
    math_draw_page(page, menucolorbg);
    Bdisp_PutDisp_DD();
    /* menu/shiftmenu/alphamenu null: casiostatus (main.cc) only redraws the tab bar when all three are non-null,
     * so this sub-bar (drawn above by math_draw_page/skin_tabs_draw) is never blanked from underneath it; shift
     * and alpha state are still tracked by in_ckgetkey the same as everywhere else. */
    in_ckgetkey(&key, 1, 0, 0, 0, 12345);
    if (key == KEY_CTRL_EXIT) return ZNAT_TPL_NONE;
    if (page == 0) {
      if (key == KEY_CTRL_F1) return ZNAT_TPL_MATRIX;
      if (key == KEY_CTRL_F2) return ZNAT_TPL_LOGAB;
      if (key == KEY_CTRL_F3) return ZNAT_TPL_ABS;
      if (key == KEY_CTRL_F4) return ZNAT_TPL_DERIV;
      if (key == KEY_CTRL_F5) return ZNAT_TPL_DERIV2;
      if (key == KEY_CTRL_F6) { page = 1; continue; }
    } else {
      if (key == KEY_CTRL_F1) return ZNAT_TPL_INTEGRAL;
      if (key == KEY_CTRL_F2) return ZNAT_TPL_SIGMA;
      if (key == KEY_CTRL_F6) { page = 0; continue; }
    }
  }
}

/* M2 task 7: public entry point so console.cc's bare-shell F4 key can offer this same sub-bar without opening
 * the natural editor first (znat.h). math_submenu itself stays static/internal to this file; this is a plain
 * forwarding call, not a new implementation, so the sub-bar's own key handling (above) is exercised exactly the
 * same way it already is from inside the editor's own F4 branch. */
int znat_math_submenu(int menucolorbg) { return math_submenu(menucolorbg); }

/* Matrix row/column add/remove while the caret sits inside a matrix (M2 task 5, PROVISIONAL, documented in
 * docs/KEY_CONFLICTS.md section 2/3): none of these four keys does anything else in this panel (checked against
 * the key list in this file), so the choice cannot collide. Decision: RIGHT/DOWN-ish keys grow, LEFT/UP-ish keys
 * shrink. shift-RIGHT/shift-LEFT (the only shift-arrow keys the keyboard exposes: no shift-UP/shift-DOWN
 * constants exist in libfxcg's keyboard.h) add/remove a COLUMN; PAGEDOWN/PAGEUP (unused inside this panel today)
 * add/remove a ROW. Deleting the matrix whole is unchanged: DEL on an empty matrix / the whole-template DEL rule
 * already in nin/ (nin_backspace) still applies; these keys only resize an existing matrix. When the caret is not
 * inside a matrix the keys fall through and do nothing (they had no meaning here before this task either). */
static int matrix_nav(int key)
{
  int mr, mc, rc;
  if (key != KEY_SHIFT_RIGHT && key != KEY_SHIFT_LEFT && key != KEY_CTRL_PAGEDOWN && key != KEY_CTRL_PAGEUP)
    return 0;
  if (!nin_matrix_dims(ed, &mr, &mc)) return 1;   /* not in a matrix: key handled (as a no-op), nothing to insert */
  if (key == KEY_SHIFT_RIGHT) rc = nin_matrix_add_col(ed);
  else if (key == KEY_SHIFT_LEFT) rc = nin_matrix_del_col(ed);
  else if (key == KEY_CTRL_PAGEDOWN) rc = nin_matrix_add_row(ed);
  else rc = nin_matrix_del_row(ed);
  if (rc == NIN_ERR_FULL) pending_msg = lang ? "Editeur plein" : "Editor full";
  else if (rc == NIN_ERR_CHAR) pending_msg = lang ? "Taille limite" : "Size limit";
  return 1;
}

/* The editor proper; ed and giac_buf are allocated by znat_edit_template. tpl_kind is the template (if any,
 * ZNAT_TPL_NONE for none) to insert as soon as the line is parsed, capturing the atom before the caret exactly
 * as the fraction key always has (M2 task 5). */
static int edit(int tpl_kind)
{
  ustl::string orig((const char *)Console_GetEditLine());
  bool exit_pending = false;
  int rc = nin_from_string(ed, orig.c_str());
  if (rc == NIN_ERR_FULL || rc == NIN_WARN_LOSSY) return ZNAT_NOT_HANDLED;
  /* NIN_WARN_LOSSY: at least one byte outside 0x20..0x7E was replaced; showing '?' instead of what the user typed is more confusing than falling back to plain text */
  if (insert_kind_bounded(tpl_kind) != 0) return ZNAT_NOT_HANDLED;   /* full, too tall or no memory: stock editor */
  /* The editor shows the whole line, so the linear copy is blanked while editing (Console_Disp would draw it twice).
   * Every exit below restores or replaces it. */
  Console_Clear_EditLine();
  pending_msg = 0;
  for (;;) {
    int key, keyflag;
    ustl::string menu, shiftmenu, alphamenu;
    int menucolorbg = 12345;
    keyflag = GetSetupSetting(0x14);
    get_current_console_menu(menu, shiftmenu, alphamenu, menucolorbg, 0);
    editor_menu(menu);
    redraw(menu, menucolorbg);
    /* Best-effort Edit_Line save before in_ckgetkey: if the OS intercepts MENU before our handler sees it,
     * the session-save mechanism will have the current formula instead of the blank left by Console_Clear_EditLine.
     * This runs every key-wait iteration; the serialized text is hidden by the panel drawn on top (acceptable). */
    { int _rc = nin_to_giac(ed, giac_buf, ZNAT_GIAC_MAX);
      if (_rc >= 0 && giac_buf[0] != '\0') put_edit_line(giac_buf);
      else put_edit_line(orig.c_str()); }
    in_ckgetkey(&key, 1, menu.c_str(), shiftmenu.c_str(), alphamenu.c_str(), menucolorbg);
    pending_msg = 0;
    if (key != KEY_CHAR_FRAC) translate_fkey(key);

    if (key == KEY_CTRL_EXIT) {
      if (exit_pending) return put_edit_line(orig.c_str());   /* second EXIT after an error: cancel, keep the old text */
      rc = nin_to_giac(ed, giac_buf, ZNAT_GIAC_MAX);
      if (rc < 0) { pending_msg = serialise_error(rc); exit_pending = true; continue; }
      return put_edit_line(giac_buf);
    }
    exit_pending = false;
    /* MENU during editing: serialize the current state into Edit_Line so the stock session-save has the
     * formula, then return ZNAT_NOT_HANDLED so the normal MENU/session-save flow runs. If serialization
     * fails (empty slots, unbalanced parens, too long), restore the original text instead. */
#ifdef KEY_CTRL_MENU
    if (key == KEY_CTRL_MENU) {
      rc = nin_to_giac(ed, giac_buf, ZNAT_GIAC_MAX);
      if (rc >= 0) put_edit_line(giac_buf);
      else put_edit_line(orig.c_str());
      return ZNAT_NOT_HANDLED;
    }
#endif
    if (key == KEY_CTRL_EXE) {
      rc = nin_to_giac(ed, giac_buf, ZNAT_GIAC_MAX);
      if (rc < 0) { pending_msg = serialise_error(rc); exit_pending = true; continue; }
      if (rc == 0) return put_edit_line("");   /* nothing typed: same as EXIT */
      /* A fresh line is evaluated the way the stock EXE does it: text into the edit line, then Console_NewLine, whose
       * CONSOLE_NEW_LINE_SET makes Console_GetLine hand the string to run(). (Console_Eval is for re-running an OLD line.) */
      if (put_edit_line(giac_buf) != CONSOLE_SUCCEEDED) { pending_msg = lang ? "Memoire" : "Memory"; continue; }
      return Console_NewLine(LINE_TYPE_INPUT, 1);
    }
    /* Template keys (M2 task 5): the same keys that open this editor from the shell (Console_GetKey) also work
     * once inside it, so a template can be nested inside another one's slot. Each inserts and, on failure (pool
     * full, no memory, or the tree would grow taller than the panel), leaves the tree unchanged and shows a
     * message, exactly like the fraction key always has. */
    if (key == KEY_CHAR_FRAC) {
      rc = insert_kind_bounded(ZNAT_TPL_FRAC);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (key == KEY_CHAR_POW) {
      rc = insert_kind_bounded(ZNAT_TPL_POW);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (key == KEY_CHAR_SQUARE) {
      rc = insert_kind_bounded(ZNAT_TPL_SQUARE);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (key == KEY_CHAR_RECIP) {
      rc = insert_kind_bounded(ZNAT_TPL_RECIP);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (key == KEY_CHAR_ROOT) {
      rc = insert_kind_bounded(ZNAT_TPL_SQRT);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (key == KEY_CHAR_VALR) {
      rc = insert_kind_bounded(ZNAT_TPL_ABS);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (key == KEY_CHAR_POWROOT) {
      rc = insert_kind_bounded(ZNAT_TPL_NROOT);
      if (rc != 0) pending_msg = template_error(rc);
    }
    else if (matrix_nav(key)) {
      /* handled: a row/column was added or removed, or the key is a matrix-nav key with the caret outside any
       * matrix (a documented no-op); matrix_nav already set pending_msg on a size-limit or pool-full error */
    }
    else if (key == KEY_CTRL_LEFT) nin_move(ed, NIN_LEFT);
    else if (key == KEY_CTRL_RIGHT) nin_move(ed, NIN_RIGHT);
    else if (key == KEY_CTRL_UP) nin_move(ed, NIN_UP);
    else if (key == KEY_CTRL_DOWN) nin_move(ed, NIN_DOWN);
    else if (key == KEY_CTRL_DEL) nin_backspace(ed);
    else if (key == KEY_CTRL_AC) nin_init(ed);
    else if (key == KEY_CTRL_SETUP) menu_setup();
    else if (key == KEY_CTRL_INS) {
      int ch = chartab();
      if (ch >= 0) { char one[2]; one[0] = (char)ch; one[1] = 0; insert_text(one); }
    }
    else if (key == KEY_CTRL_F4) {
      /* MATH sub-bar (docs/KEY_CONFLICTS.md section 2), replacing the plain catalog that used to sit on F4 here;
       * the catalog itself moved to shift-CATALOG below, same redirection as the outer shell. */
      int mk = math_submenu(menucolorbg);
      if (mk != ZNAT_TPL_NONE) {
        rc = insert_kind_bounded(mk);
        if (rc != 0) pending_msg = template_error(rc);
      }
    }
    else if (key == KEY_CTRL_CATALOG) {
      char buf[512];
      if (showCatalog(buf, 1, 0)) insert_text(buf);   /* preselect=1: the full catalog, as shift-CATALOG gives outside */
    }
    else if (key == KEY_CTRL_F5 || key == KEY_CTRL_F6 || key == KEY_CTRL_SD || key == KEY_CTRL_FD) {
      /* A<>a, the session menu and the add-ins menu are not offered inside the fraction editor */
    }
    else if ((key >= KEY_CTRL_F1 && key <= KEY_CTRL_F6) || (key >= KEY_CTRL_F7 && key <= KEY_CTRL_F20)) {
      insert_text(console_menu(key, 0));   /* the same fast menus as Console_FMenu, text only */
    }
    else {
      insert_text(keytostring(key, keyflag));   /* digits, letters, operators, sin( ... : same table as the shell; 0 if unhandled */
    }
  }
}

int znat_edit_template(int tpl_kind)
{
  int rc;
  ed = (nin *)console_malloc(sizeof(nin));
  giac_buf = (char *)console_malloc(ZNAT_GIAC_MAX);
  if (!ed || !giac_buf) rc = ZNAT_NOT_HANDLED;   /* nothing touched yet: the stock editor runs instead */
  else rc = edit(tpl_kind);
  if (giac_buf) console_free(giac_buf);
  if (ed) console_free(ed);
  ed = 0;
  giac_buf = 0;
  return rc;
}

int znat_edit(void) { return znat_edit_template(ZNAT_TPL_FRAC); }
