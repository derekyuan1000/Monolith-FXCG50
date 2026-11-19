/* Bare-shell F-key homes (Monolith M2 task 7, docs/KEY_CONFLICTS.md section 2): F1 JUMP, F2 DELETE, F5 CAS.
 * Same modal-loop idiom as skin/znat.cc's math_submenu and skin/zsd_ui.cc's zsd_open: Console_Disp() draws the
 * normal frame (history, status bar, and — via console_disp_status() — the DEFAULT tab-bar labels, which this
 * file's own skin_tabs_draw call then overlays), in_ckgetkey with null menu/shiftmenu/alphamenu (this sub-bar
 * owns the tab row for as long as it runs, same reasoning znat.cc/zsd_ui.cc document for their own calls) waits
 * for one key.
 *
 * F3 (native "opens the matrix editor") and the F4 MATH-sub-bar-first rewrite live directly in console.cc
 * (the fork patch), because each is a one-line reuse of an existing function (input_matrix, znat_math_submenu)
 * with no sub-bar of its own to draw; only F1/F2/F5 need a dedicated modal loop, so only those three live here.
 *
 * Declared with no header of their own: forward-declared directly in console.cc next to khicas_addins_menu and
 * zsd_open, the same convention, since nothing else needs to share a struct or constant with these three.
 * Compiled as zshellbar.o (.rominram, see tools/fork_objs.txt). */
#include "giacPCH.h"
#include <fxcg/display.h>
#include <fxcg/file.h>
#include <fxcg/keyboard.h>
#include <fxcg/misc.h>
#include <string.h>
#include "kdisplay.h"
#include "catalogGUI.hpp"   /* showCatalog; extern int lang */
#include "textGUI.hpp"      /* chk_restart */
#include "console.h"
#include "main.h"
#include "gfx.h"
#include "zskin_status.h"
#include "theme.h"
#include "zskin_tabs.h"

/* shell state (console.cc); same externs skin/zstack.cc already declares for the same purpose. */
extern console_line *Line;
extern location Cursor;
extern int Start_Line, Last_Line;
/* FMENU.py fast-menu legends (console.cc, char[maxfmenusize]): the CAS sub-bar shows their contents, as the old
 * default bottom bar did (string(menu_f1) in get_current_console_menu). */
extern char menu_f1[], menu_f2[], menu_f3[];

extern int g_defer_flip;
/* Console_Disp() already draws the frame including the default tab-bar labels (console_disp_status(), at the
 * end of Console_Disp()); this overlays the sub-bar's own labels on the same tab row and flips the buffer, the
 * same two extra steps skin/znat.cc's redraw() takes after its own Console_Disp() call. */
static void draw_subbar(const char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX], int menucolorbg, const char *msg)
{
  ++g_defer_flip;
  Console_Disp();
  --g_defer_flip;
  skin_tabs_draw(labels, skin_tabs_fg(menucolorbg));
  /* every caller waits in in_ckgetkey next, which redraws the status bar (keeping a zskin_status_msg) and flips */
  if (msg) zskin_status_msg(msg);
}

/* Bounded copy of an FMENU.py legend (up to maxfmenusize-1 = 15 chars) into a tab label slot. */
static void copy_label(char *dst, const char *src)
{
  strncpy(dst, src, SKIN_TAB_LABEL_MAX - 1);
  dst[SKIN_TAB_LABEL_MAX - 1] = 0;
}

static void clear_labels(char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX])
{
  int i;
  for (i = 0; i < SKIN_TABS; i++) labels[i][0] = 0;
}

/* F1 JUMP (docs/KEY_CONFLICTS.md section 2: "TOP, BOTTOM, PageUp, PageDown (5 workings)"). TOP/BOTTOM/PageUp/
 * PageDown are one-shot picks (the sub-bar closes back to the shell after each); plain arrow UP/DOWN also scroll
 * without closing the sub-bar, which is read as the note's "5th working" (the 4 labelled picks plus arrow-key
 * scrolling while the JUMP bar is up). EXIT closes with nothing else changed. Every loop below is bounded (by
 * LINE_MAX, the largest the history can ever be, console.h) so a boundary case can never spin forever; each
 * single step is Console_MoveCursor, the same function the physical arrow keys and the existing PageUp/PageDown
 * keys already call, so no new cursor/history bookkeeping is introduced here. */
int zjump_open(void)
{
  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  clear_labels(labels);
  strcpy(labels[0], "TOP");
  strcpy(labels[1], "BOTTOM");
  strcpy(labels[2], "PageUp");
  strcpy(labels[3], "PageDn");
  for (;;) {
    int key, n, cur;
    draw_subbar(labels, 12345, 0);   /* sentinel: white labels, no interpreter colour (a generic utility bar) */
    in_ckgetkey(&key, 1, 0, 0, 0, 12345);
    if (key == KEY_CTRL_EXIT) return CONSOLE_SUCCEEDED;
    if (key == KEY_CTRL_F1) {   /* TOP */
      for (n = 0; n < LINE_MAX && (cur = Start_Line + Cursor.y) > 0; ++n) Console_MoveCursor(CURSOR_UP);
      return CONSOLE_SUCCEEDED;
    }
    if (key == KEY_CTRL_F2) {   /* BOTTOM */
      for (n = 0; n < LINE_MAX && (cur = Start_Line + Cursor.y) < Last_Line; ++n) Console_MoveCursor(CURSOR_DOWN);
      return CONSOLE_SUCCEEDED;
    }
    if (key == KEY_CTRL_F3) {   /* PageUp: same 10-step loop the physical PageUp key already runs */
      for (n = 0; n < 10; ++n) Console_MoveCursor(CURSOR_UP);
      return CONSOLE_SUCCEEDED;
    }
    if (key == KEY_CTRL_F4) {   /* PageDown: same 10-step loop the physical PageDown key already runs */
      for (n = 0; n < 10; ++n) Console_MoveCursor(CURSOR_DOWN);
      return CONSOLE_SUCCEEDED;
    }
    if (key == KEY_CTRL_UP)   { Console_MoveCursor(CURSOR_UP); continue; }
    if (key == KEY_CTRL_DOWN) { Console_MoveCursor(CURSOR_DOWN); continue; }
  }
}

/* F2 DELETE (docs/KEY_CONFLICTS.md section 2: "DEL-LINE, DEL-ALL"). Both reuse an existing, already-proven
 * function rather than new history-array bookkeeping:
 *   DEL-LINE: Console_Backspace() -- its own FIRST branch ("Last_Line>0 && Current_Line<Last_Line") already
 *     deletes the whole history line under the cursor (frees the string, shifts Line[] up, fixes
 *     Last_Line/Start_Line/Cursor.y): this is exactly what the physical DEL key already does when the cursor
 *     sits on a history line, just reachable directly from this sub-bar too. On the live edit line there is no
 *     history line to delete, and Console_Backspace would fall back to a one-character backspace instead, so
 *     DEL-LINE is refused there with a status message (task 7 review fix) and the bar stays open.
 *   DEL-ALL: asks first, like native DEL-ALL (Yes on F1, No on F6; the question goes in the status line, since
 *     this bar has no dialog of its own), then makes the exact same three calls the F6 Applications menu's own
 *     "Clear history" entry makes (console.cc, smallmenu.selection==10): chk_restart(); Console_Init();
 *     Console_Clear_EditLine(); Without the question, F2 F2 wiped the whole history in two presses. */
static int confirm_delete_all(void)
{
  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  const char *q = lang ? "Tout effacer ?" : "Delete all?";
  clear_labels(labels);
  strcpy(labels[0], "Yes");
  strcpy(labels[5], "No");
  for (;;) {
    int key;
    draw_subbar(labels, 12345, q);
    in_ckgetkey(&key, 1, 0, 0, 0, 12345);
    if (key == KEY_CTRL_F1) return 1;
    if (key == KEY_CTRL_F6 || key == KEY_CTRL_EXIT) return 0;
  }
}

int zdelete_open(void)
{
  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  const char *msg = 0;
  clear_labels(labels);
  strcpy(labels[0], "DEL-LINE");
  strcpy(labels[1], "DEL-ALL");
  for (;;) {
    int key;
    draw_subbar(labels, 12345, msg);
    in_ckgetkey(&key, 1, 0, 0, 0, 12345);
    msg = 0;
    if (key == KEY_CTRL_EXIT) return CONSOLE_SUCCEEDED;
    if (key == KEY_CTRL_F1) {
      if (Start_Line + Cursor.y >= Last_Line) {   /* the live edit line: nothing to delete as a line */
        msg = lang ? "Choisir une ligne" : "Select a line";
        continue;
      }
      return Console_Backspace();
    }
    if (key == KEY_CTRL_F2) {
      if (!confirm_delete_all()) return CONSOLE_SUCCEEDED;
      chk_restart();
      Console_Init();
      Console_Clear_EditLine();
      return CONSOLE_SUCCEEDED;
    }
  }
}

/* F5 CAS (docs/KEY_CONFLICTS.md section 2: "the pull up KhiCAS functions button ... exactly today's KhiCAS
 * bottom bar: menu_f1 | menu_f2 | menu_f3 | CMDS | A<>a | (was F1-F3's default before this task, now moved down
 * a level since F1-F3 have native meanings at the top level). menu_f1/f2/f3 are Console_FMenu(KEY_CTRL_F1/F2/
 * F3) -- already fully self-contained (draws its own popup, waits for a pick, returns the resulting string via
 * Console_Input), so this sub-bar is a thin picker in front of code that already exists. CMDS is the same
 * catalog call the old bare-shell F4 key used to make directly. A<>a is handle_f5() (case toggle), same as the
 * old F5 key. shift-/alpha- pressed while this sub-bar is showing arrive as KEY_CTRL_F7..KEY_CTRL_F20 (the
 * keyboard/OS layer reports the modified code directly, independent of which sub-bar is open), forwarded
 * straight to Console_FMenu(key) exactly like the top-level shell already does, so FMENU.py's menu_f7..menu_f18
 * keep working unchanged. The sixth slot is EXIT rather than the brief's decorative "▷": there is nothing to
 * page between here (shift/alpha already reach the extra items directly, without a second page), so labelling
 * it "▷" would promise a page-flip this bar does not have; documented as a deliberate, honest simplification in
 * docs/KEY_CONFLICTS.md rather than left unstated. Deferred (KEY_CONFLICTS.md): the shift/alpha LEGENDS are not
 * swapped in while this bar is up (in_ckgetkey gets null menus so casiostatus cannot overdraw the bar); the keys
 * themselves work. */
int zcas_open(int menucolorbg)
{
  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  clear_labels(labels);
  copy_label(labels[0], menu_f1);   /* the legends' contents, not their names (task 7 review fix) */
  copy_label(labels[1], menu_f2);
  copy_label(labels[2], menu_f3);
  strcpy(labels[3], "CMDS");
  strcpy(labels[4], "A<>a");
  strcpy(labels[5], "EXIT");
  for (;;) {
    int key;
    draw_subbar(labels, menucolorbg, 0);   /* real interpreter colour: this bar hosts the CAS/interpreter commands */
    in_ckgetkey(&key, 1, 0, 0, 0, 12345);
    if (key == KEY_CTRL_EXIT || key == KEY_CTRL_F6) return CONSOLE_SUCCEEDED;
    if (key == KEY_CTRL_F1) return Console_FMenu(KEY_CTRL_F1);
    if (key == KEY_CTRL_F2) return Console_FMenu(KEY_CTRL_F2);
    if (key == KEY_CTRL_F3) return Console_FMenu(KEY_CTRL_F3);
    if (key == KEY_CTRL_F4) {
      char buf[512];
      if (!showCatalog(buf, 0, 0)) buf[0] = 0;
      return Console_Input((const unsigned char *)buf);
    }
    if (key == KEY_CTRL_F5) { handle_f5(); continue; }   /* case toggle only; stays open, same as the old F5 key */
    if (key >= KEY_CTRL_F7 && key <= KEY_CTRL_F20) return Console_FMenu(key);
  }
}

/* Shift+F3: pinned items (docs/KEY_CONFLICTS.md).
 * File: \\fls0\monolith_pinned.txt — one command per line, plain text.
 * F1/EXE: insert selected pin into the edit line.
 * F2: save the current edit line as a new pin.
 * F3: delete the selected pin.
 * UP/DOWN: navigate the list.
 * EXIT/F6: close without action. */

#define ZPINNED_FILE  "\\\\fls0\\monolith_pinned.txt"
#define ZPINNED_MAX   20
#define ZPINNED_LEN   64
#define ZPINNED_ROWS  RM_WORK_LINES   /* 7, from theme.h */

/* Highlight colours for the selected row (arbitrary RUN-MAT-style blue). */
#define ZPINNED_SEL_BG  GFX_RGB565(0, 80, 160)
#define ZPINNED_SEL_FG  GFX_WHITE

static int zpinned_load(char out[ZPINNED_MAX][ZPINNED_LEN])
{
  unsigned short fname[MAX_FILENAME_SIZE + 1];
  const char path[] = ZPINNED_FILE;
  Bfile_StrToName_ncpy(fname, (const unsigned char *)path, (int)(sizeof path));
  int fd = Bfile_OpenFile_OS(fname, READ);
  if (fd < 0) return 0;
  int fsz = Bfile_GetFileSize_OS(fd);
  if (fsz <= 0 || fsz > ZPINNED_MAX * ZPINNED_LEN) { Bfile_CloseFile_OS(fd); return 0; }
  char buf[ZPINNED_MAX * ZPINNED_LEN + 1];
  Bfile_ReadFile_OS(fd, buf, fsz, -1);
  Bfile_CloseFile_OS(fd);
  buf[fsz] = '\0';
  int n = 0;
  char *p = buf, *end = buf + fsz;
  while (p < end && n < ZPINNED_MAX) {
    char *nl = (char *)memchr(p, '\n', (int)(end - p));
    int len = nl ? (int)(nl - p) : (int)(end - p);
    if (len > 0 && p[len - 1] == '\r') len--;
    if (len > 0) {
      int copy = len < ZPINNED_LEN - 1 ? len : ZPINNED_LEN - 1;
      memcpy(out[n], p, copy);
      out[n][copy] = '\0';
      n++;
    }
    p = nl ? nl + 1 : end;
  }
  return n;
}

static void zpinned_save(char lines[ZPINNED_MAX][ZPINNED_LEN], int n)
{
  unsigned short fname[MAX_FILENAME_SIZE + 1];
  const char path[] = ZPINNED_FILE;
  Bfile_StrToName_ncpy(fname, (const unsigned char *)path, (int)(sizeof path));
  Bfile_DeleteEntry(fname);
  if (n == 0) return;
  /* build flat content: lines separated by '\n' */
  char buf[ZPINNED_MAX * (ZPINNED_LEN + 1) + 1];
  int pos = 0, i;
  for (i = 0; i < n; i++) {
    int len = (int)strlen(lines[i]);
    memcpy(buf + pos, lines[i], len);
    pos += len;
    buf[pos++] = '\n';
  }
  int sz = pos;
  Bfile_CreateEntry_OS(fname, CREATEMODE_FILE, &sz);
  int fd = Bfile_OpenFile_OS(fname, WRITE);
  if (fd < 0) return;
  Bfile_WriteFile_OS(fd, buf, sz);
  Bfile_CloseFile_OS(fd);
}

static void zpinned_draw_list(char lines[ZPINNED_MAX][ZPINNED_LEN], int n, int sel, int scroll)
{
  /* white background over the work area */
  gfx_fill_rect(0, RM_WORK_TOP_Y, GFX_WIDTH, RM_WORK_BOTTOM_Y - RM_WORK_TOP_Y + 1, GFX_WHITE);
  int i;
  for (i = 0; i < ZPINNED_ROWS && (scroll + i) < n; i++) {
    int y = RM_WORK_TOP_Y + i * RM_LINE_PITCH;
    int idx = scroll + i;
    if (idx == sel) {
      gfx_fill_rect(0, y, GFX_WIDTH - 10, RM_LINE_PITCH, ZPINNED_SEL_BG);
      gfx_text(4, y + RM_INK_TOP, lines[idx], ZPINNED_SEL_FG, ZPINNED_SEL_BG);
    } else {
      gfx_text(4, y + RM_INK_TOP, lines[idx], GFX_BLACK, GFX_WHITE);
    }
  }
  if (n == 0) {
    const char *msg = lang ? "Aucun favori.  F2: ajouter la ligne courante."
                           : "No pins.  F2: add current line.";
    gfx_text(4, RM_WORK_TOP_Y + RM_INK_TOP, msg, GFX_BLACK, GFX_WHITE);
  }
}

int zpinned_open(void)
{
  char lines[ZPINNED_MAX][ZPINNED_LEN];
  int n = zpinned_load(lines);
  int sel = 0, scroll = 0;

  char labels[SKIN_TABS][SKIN_TAB_LABEL_MAX];
  clear_labels(labels);
  strcpy(labels[0], "INSERT");
  strcpy(labels[1], "ADD");
  strcpy(labels[2], "DEL");
  strcpy(labels[5], "EXIT");

  for (;;) {
    int key;
    ++g_defer_flip;
    Console_Disp();
    --g_defer_flip;
    zpinned_draw_list(lines, n, sel, scroll);
    skin_tabs_draw(labels, skin_tabs_fg(12345));   /* in_ckgetkey below flips */

    in_ckgetkey(&key, 1, 0, 0, 0, 12345);

    if (key == KEY_CTRL_EXIT || key == KEY_CTRL_F6)
      return CONSOLE_SUCCEEDED;
    if (key == KEY_CTRL_UP) {
      if (sel > 0) { sel--; if (sel < scroll) scroll--; }
      continue;
    }
    if (key == KEY_CTRL_DOWN) {
      if (n > 0 && sel < n - 1) { sel++; if (sel >= scroll + ZPINNED_ROWS) scroll++; }
      continue;
    }
    if ((key == KEY_CTRL_EXE || key == KEY_CTRL_F1) && n > 0)
      return Console_Input((const unsigned char *)lines[sel]);
    if (key == KEY_CTRL_F2) {
      /* add current edit line as a new pin */
      if (n < ZPINNED_MAX) {
        const char *cur = (const char *)Line[Last_Line].str;
        if (cur && cur[0]) {
          strncpy(lines[n], cur, ZPINNED_LEN - 1);
          lines[n][ZPINNED_LEN - 1] = '\0';
          n++;
          sel = n - 1;
          if (sel >= scroll + ZPINNED_ROWS) scroll = sel - ZPINNED_ROWS + 1;
          zpinned_save(lines, n);
        }
      }
      continue;
    }
    if (key == KEY_CTRL_F3 && n > 0) {
      /* delete selected pin, shift remainder up */
      int i;
      for (i = sel; i < n - 1; i++) memcpy(lines[i], lines[i + 1], ZPINNED_LEN);
      n--;
      if (sel >= n && sel > 0) sel--;
      if (scroll > 0 && n < scroll + ZPINNED_ROWS) scroll = n > ZPINNED_ROWS ? n - ZPINNED_ROWS : 0;
      zpinned_save(lines, n);
      continue;
    }
  }
}
