/* Upsilon-style front end for KhiCAS: glue between upsui (zupsui.cpp) and
 * KhiCAS's console. The console keeps the history (so sessions, "run
 * session" and "clear history" keep working) and evaluates through run();
 * upsui draws it the way Upsilon's Calculation app does.
 *
 * File name starts with z: the linker script puts it in the RAM part (.ac2). */
#include "giacPCH.h"
#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <fxcg/system.h>
#include <fxcg/misc.h>
extern "C" {
#include <fxcg/rtc.h>
}
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "kdisplay.h"
#include "console.h"
#include "catalogGUI.hpp"
#include "main.h"
#include "upsui.h"

using namespace giac;

extern console_line * Line;
extern int Start_Line, Last_Line;
extern location Cursor;
extern unsigned char * Edit_Line;
extern bool shiftstate, alphastate, alphalock, alphamaj, oldshiftstate, oldalphastate;
extern short int timeout_delay;
void MENU_save();
void set_menu_timer();
void console_free(void * ptr);
int ups_translate_key(int code);          // main.cc
const char * ups_menu_config();          // console.cc
extern int ups_active, ups_inject[4], ups_inject_n;   // console.cc
extern "C" int mocha_paused;             // mocha.c

/* ---------------- history = console lines ---------------- */

static int s_calcStart[LINE_MAX];
static int s_ncalc = 0;
static char * s_join[2] = {0, 0};
static int s_joinIdx = 0;
enum { JOIN_SIZE = 2048 };

static void reindex() {
  s_ncalc = 0;
  for (int i = 0; i < Last_Line; i++)
    if (Line[i].type == LINE_TYPE_INPUT && Line[i].str) s_calcStart[s_ncalc++] = i;
}

static int hCount() { reindex(); return s_ncalc; }

static void hGet(int i, const char ** in, const char ** out) {
  reindex();
  if (i < 0 || i >= s_ncalc) { *in = ""; *out = ""; return; }
  int a = s_calcStart[i], b = i + 1 < s_ncalc ? s_calcStart[i + 1] : Last_Line;
  *in = (const char *)Line[a].str;
  int n = b - a - 1;
  if (n <= 0) { *out = ""; return; }
  if (n == 1) { *out = (const char *)Line[a + 1].str; return; }
  // prints followed by the result: one line each
  s_joinIdx ^= 1;
  char * j = s_join[s_joinIdx];
  int len = 0;
  for (int k = a + 1; k < b && len < JOIN_SIZE - 2; k++) {
    const char * s = (const char *)Line[k].str;
    if (!s) continue;
    if (len) j[len++] = '\n';
    while (*s && len < JOIN_SIZE - 2) j[len++] = *s++;
  }
  j[len] = 0;
  *out = j;
}

// Keep the console's cursor on its edit line (the last line)
static void syncCursor() {
  Start_Line = Last_Line >= LINE_DISP_MAX ? Last_Line - LINE_DISP_MAX + 1 : 0;
  Cursor.y = Last_Line - Start_Line;
  Cursor.x = 0;
  Line[Last_Line].start_col = 0;
}

static void enterKhicas() {   // before handing the screen to KhiCAS code
  mocha_paused = 0;
  EnableStatusArea(0);
}

static bool hEval(const char * input, char * out, int outSize) {
  enterKhicas();
  syncCursor();
  Console_Clear_EditLine();
  Console_Input((const unsigned char *)input);
  if (Console_NewLine(LINE_TYPE_INPUT, 1) != CONSOLE_NEW_LINE_SET) return false;
  const char * expr = (const char *)Line[Last_Line - 1].str;
  if (strcmp(expr, "=>") == 0) {
    save_session();
    Console_Output((const unsigned char *)"Session saved");
  } else {
    run(expr);
  }
  Console_NewLine(LINE_TYPE_OUTPUT, 1);
  syncCursor();
  if (out && outSize) out[0] = 0;
  return true;
}

static void hAdd(const char *, const char *) {}   // the console already holds it

static void hRemove(int i) {
  reindex();
  if (i < 0 || i >= s_ncalc) return;
  int a = s_calcStart[i], b = i + 1 < s_ncalc ? s_calcStart[i + 1] : Last_Line;
  int n = b - a;
  for (int k = a; k < b; k++) { if (Line[k].str && Line[k].str != Edit_Line) console_free(Line[k].str); }
  for (int k = a; k + n <= Last_Line; k++) Line[k] = Line[k + n];
  for (int k = Last_Line - n + 1; k <= Last_Line; k++) { Line[k].str = 0; Line[k].type = LINE_TYPE_INPUT; Line[k].readonly = 0; Line[k].disp_len = 0; Line[k].start_col = 0; }
  Last_Line -= n;
  console_changed = 1;
  syncCursor();
}

/* ---------------- title bar ---------------- */

static const char * tLeft() {
  static char buf[40];
  const char * mode = xcas_python_eval == 1 ? "PYTHON" : (xcas_python_eval == -1 ? "JS" : "XCAS");
  const char * angle = angle_radian(contextptr) ? "rad" : "deg";
  const char * key = "";
  if (shiftstate) key = "  shift";
  else if (alphastate) key = alphalock ? (alphamaj ? "  ALPHA" : "  alpha") : (alphamaj ? "  A" : "  a");
  sprintf(buf, "%s  %s%s", mode, angle, key);
  return buf;
}

static const char * tRight() {
  static char buf[8];
  unsigned h, m, s, ms;
  RTC_GetTime(&h, &m, &s, &ms);
  sprintf(buf, "%02u:%02u", h, m);
  return buf;
}

static bool pythonMode() { return xcas_python_eval == 1; }

/* ---------------- toolbox from KhiCAS's F-key menus ---------------- */

struct Help { const char * name; const char * args; const char * text; };
static const Help kHelp[] = {
  {"simplify", "expr", "Simplify"}, {"factor", "expr", "Factorise"}, {"partfrac", "f", "Partial fractions"},
  {"tcollect", "expr", "Collect trig terms"}, {"texpand", "expr", "Expand trig and exp"}, {"sum", "f,k,a,b", "Sum"},
  {"product", "f,k,a,b", "Product"}, {"diff", "f,x", "Derivative"}, {"integrate", "f,x,a,b", "Integral"},
  {"limit", "f,x,a", "Limit"}, {"series", "f,x=a,n", "Series expansion"}, {"solve", "eq,x", "Solve"},
  {"desolve", "eq,y", "Differential equation"}, {"rsolve", "eq,u(n),u(0)=a", "Recurrence"},
  {"plot", "f,x=a..b", "Graph of a function"}, {"plotseq", "f,x=a,n", "Cobweb diagram"}, {"plotlist", "list", "Plot a list"},
  {"plotparam", "[x(t),y(t)],t", "Parametric curve"}, {"plotpolar", "r(t),t", "Polar curve"}, {"plotfield", "f,[t,y]", "Slope field"},
  {"histogram", "list", "Histogram"}, {"barplot", "list", "Bar chart"}, {"irem", "a,b", "Integer remainder"},
  {"ifactor", "n", "Prime factors"}, {"gcd", "a,b", "Highest common factor"}, {"lcm", "a,b", "Lowest common multiple"},
  {"isprime", "n", "Primality test"}, {"nextprime", "n", "Next prime"}, {"powmod", "a,n,p", "Modular power"},
  {"iegcd", "a,b", "Bezout coefficients"}, {"exact", "x", "Exact value"}, {"approx", "x", "Decimal value"},
  {"floor", "x", "Floor"}, {"ceil", "x", "Ceiling"}, {"round", "x", "Round"}, {"sign", "x", "Sign"},
  {"max", "a,b", "Maximum"}, {"min", "a,b", "Minimum"}, {"abs", "z", "Modulus"}, {"arg", "z", "Argument"},
  {"re", "z", "Real part"}, {"im", "z", "Imaginary part"}, {"conj", "z", "Conjugate"}, {"csolve", "eq,z", "Complex solutions"},
  {"cfactor", "expr", "Factorise over C"}, {"cpartfrac", "f", "Partial fractions over C"},
  {"matrix", "n,m,f", "Matrix"}, {"det", "M", "Determinant"}, {"matpow", "M,n", "Matrix power"}, {"ranm", "n,m", "Random matrix"},
  {"tran", "M", "Transpose"}, {"rref", "M", "Row echelon form"}, {"egvl", "M", "Eigenvalues"}, {"egv", "M", "Eigenvectors"},
  {"makelist", "f,k,a,b", "Make a list"}, {"range", "n", "0..n-1"}, {"seq", "f,k,a,b", "Sequence"}, {"size", "list", "Length"},
  {"append", "list,x", "Add to a list"}, {"ranv", "n", "Random vector"}, {"sort", "list", "Sort"}, {"apply", "f,list", "Apply to a list"},
  {"proot", "p", "Numeric roots"}, {"pcoeff", "roots", "Polynomial from roots"}, {"quo", "a,b,x", "Quotient"}, {"rem", "a,b,x", "Remainder"},
  {"egcd", "a,b,x", "Bezout for polynomials"}, {"resultant", "a,b,x", "Resultant"}, {"comb", "n,k", "nCr"}, {"binomial", "n,p,k", "Binomial"},
  {"rand", "", "Random number"}, {"normald", "m,s,x", "Normal density"}, {"point", "x,y", "Point"}, {"line", "A,B", "Line"},
  {"triangle", "A,B,C", "Triangle"}, {"circle", "C,r", "Circle"}, {"plane", "A,B,C", "Plane"}, {"sphere", "C,r", "Sphere"},
  {"gauss", "q,vars", "Gauss reduction"}, {"lu", "M", "LU decomposition"}, {"qr", "M", "QR decomposition"}, {"jordan", "M", "Jordan form"},
  {"cholesky", "M", "Cholesky"}, {"gramschmidt", "basis", "Gram-Schmidt"}, {"debug", "f(x)", "Step through"},
  {0, 0, 0}};

static const char * categoryName(const char * shortName) {
  static const char * const names[][2] = {
    {"algb", "Algebra"}, {"calc", "Calculus"}, {"plot", "Graphs"}, {"arit", "Arithmetic"}, {"real", "Real numbers"},
    {"cplx", "Complex numbers"}, {"draw", "Drawing"}, {"prog", "Programming"}, {"lin2", "Matrix decompositions"},
    {"logo", "Turtle"}, {"color", "Colours"}, {"misc", "Probability and misc"}, {"geo", "Geometry"}, {"lin", "Matrices"},
    {"list", "Lists"}, {"poly", "Polynomials"}, {"math", "Maths"}, {"cmath", "Complex maths"}, {"numpy", "numpy"},
    {"linalg", "Linear algebra"}, {"char", "Characters"}, {"fill", "Filled shapes"}, {"rand", "Random"}, {0, 0}};
  for (int i = 0; names[i][0]; i++) if (strcmp(names[i][0], shortName) == 0) return names[i][1];
  return shortName;
}

enum { MAX_CAT = 20, MAX_ITEMS = 8 };
static ups::MenuItem * s_cats = 0;      // top level: categories then extras
static ups::MenuItem * s_items = 0;     // MAX_CAT * MAX_ITEMS
static char * s_strings = 0;            // labels and insert texts
static int s_stringLen = 0;
enum { STRINGS = 8192 };
static int s_ncat = 0, s_nroot = 0;
static int s_fkeyCat[19];               // F1..F18 -> category index
static int s_builtFor = -2;

static const char * keep(const char * s, int len = -1) {
  if (len < 0) len = strlen(s);
  if (s_stringLen + len + 1 > STRINGS) return "";
  char * d = s_strings + s_stringLen;
  memcpy(d, s, len); d[len] = 0;
  s_stringLen += len + 1;
  return d;
}

static void buildToolbox() {
  if (s_builtFor == xcas_python_eval) return;
  s_builtFor = xcas_python_eval;
  if (!s_cats) {   // malloc, not new: no C++ exception support in KhiCAS's link
    s_cats = (ups::MenuItem *)malloc(sizeof(ups::MenuItem) * (MAX_CAT + 8));
    s_items = (ups::MenuItem *)malloc(sizeof(ups::MenuItem) * MAX_CAT * MAX_ITEMS);
    s_strings = (char *)malloc(STRINGS);
  }
  s_stringLen = 0; s_ncat = 0;
  for (int k = 0; k < 19; k++) s_fkeyCat[k] = -1;
  const char * cfg = ups_menu_config();
  int cur = -1;
  char line[64];
  while (cfg && *cfg) {
    int n = 0;
    while (*cfg && *cfg != '\n' && *cfg != '\r' && n < 63) line[n++] = *cfg++;
    line[n] = 0;
    while (*cfg == '\n' || *cfg == '\r') cfg++;
    if (line[0] == 'F' && n >= 2 && line[1] >= '1' && line[1] <= '0' + 18) {
      const char * name = n > 3 ? line + 3 : "";
      cur = -1;
      if (!*name || !strcmp(name, "menu") || !strcmp(name, "A<>a") || !strcmp(name, "none")) continue;
      // F3 plot and F: plot are the same menu: keep one entry
      for (int k = 0; k < s_ncat; k++)
        if (strcmp(s_cats[k].label, categoryName(name)) == 0) { s_fkeyCat[line[1] - '0'] = k; cur = -2; }
      if (cur == -2) continue;
      if (s_ncat >= MAX_CAT) continue;
      cur = s_ncat++;
      s_fkeyCat[line[1] - '0'] = cur;
      ups::MenuItem & c = s_cats[cur];
      c.label = keep(categoryName(name)); c.help = 0; c.insert = 0;
      c.children = s_items + cur * MAX_ITEMS; c.count = 0;
      continue;
    }
    if (cur < 0 || !line[0] || !strcmp(line, "reserved") || s_cats[cur].count >= MAX_ITEMS) continue;
    ups::MenuItem & it = s_items[cur * MAX_ITEMS + s_cats[cur].count++];
    it.children = 0; it.count = 0; it.help = 0;
    if (!strcmp(line, "char table")) { it.label = keep("Character table"); it.insert = "\x01" "chartab"; continue; }
    if (n > 1 && line[n - 1] == '(') {
      char name[48]; int l = n - 1 < 47 ? n - 1 : 47;
      memcpy(name, line, l); name[l] = 0;
      const char * args = "";
      for (int h = 0; kHelp[h].name; h++) if (!strcmp(kHelp[h].name, name)) { args = kHelp[h].args; it.help = kHelp[h].text; break; }
      char label[96]; sprintf(label, "%s(%s)", name, args);
      it.label = keep(label);
      char ins[64]; sprintf(ins, "%s(\x11)", name);
      it.insert = keep(ins);
    } else {
      it.label = keep(line);
      it.insert = keep(line);
    }
  }
  // Everything KhiCAS's F-key bar and F4/F6 gave access to
  static const ups::MenuItem extras[] = {
    {"Catalogue", "All commands", "\x01" "catalog", 0, 0},
    {"Variables", "Stored values and functions", "\x01" "vars", 0, 0},
    {"Character table", 0, "\x01" "chartab", 0, 0},
    {"File and sessions", "Save, load, scripts, editor", "\x01" "file", 0, 0},
    {"Settings", "Angle, mode, shortcuts", "\x01" "setup", 0, 0},
    {"Python / Xcas", "Switch interpreter", "\x01" "mode", 0, 0},
  };
  s_nroot = s_ncat;
  for (unsigned k = 0; k < sizeof(extras) / sizeof(extras[0]); k++) s_cats[s_nroot++] = extras[k];
  ups::setToolbox(s_cats, s_nroot);
}

/* Run a key through KhiCAS's own console handler (F5, F6, SETUP, VARS...):
 * the key is injected, a harmless LEFT makes the handler return if it loops,
 * and whatever it typed in the console's edit line is moved to our input. */
static void bridge(int key) {
  enterKhicas();
  syncCursor();
  Console_Clear_EditLine();
  ups_inject_n = 0;
  ups_inject[ups_inject_n++] = KEY_CTRL_LEFT;
  ups_inject[ups_inject_n++] = key;
  Console_GetKey();
  ups_inject_n = 0;
  syncCursor();
  if (Edit_Line && Edit_Line[0]) {
    ups::insertInInput((const char *)Edit_Line);
    Console_Clear_EditLine();
  }
  buildToolbox();   // the interpreter may have changed
}

static bool hAction(const char * id, char * out, int size) {
  out[0] = 0;
  enterKhicas();
  if (!strcmp(id, "catalog")) {
    char buf[512];
    if (showCatalog(buf, 0, 0)) { strncpy(out, buf, size - 1); out[size - 1] = 0; }
  } else if (!strcmp(id, "vars")) {
    const char * s = keytostring(KEY_CTRL_VARS, 0);
    if (s) { strncpy(out, s, size - 1); out[size - 1] = 0; }
  } else if (!strcmp(id, "chartab")) {
    int k = chartab();
    if (k > 0) { out[0] = (char)k; out[1] = 0; }
  } else if (!strcmp(id, "file")) {
    bridge(KEY_CTRL_F6);
  } else if (!strcmp(id, "setup")) {
    bridge(KEY_CTRL_SETUP);
  } else if (!strcmp(id, "mode")) {
    hEval(xcas_python_eval == 1 ? "xcas" : "python", 0, 0);
    buildToolbox();
  }
  return out[0] != 0;
}

/* ---------------- keys ---------------- */

// KhiCAS's in_ckgetkey, redrawing the screen whenever SHIFT/ALPHA change
static void drawScreen();
static int readKey() {
  int col, row;
  unsigned short keycode = 0;
  int keyflag = GetSetupSetting((unsigned int)0x14);
  shiftstate = alphastate = alphalock = false; alphamaj = true;
  if ((keyflag & 4) | (keyflag & 8)) alphastate = true;
  if (keyflag & 0x80) alphalock = true;
  if (keyflag == 0x88) alphamaj = false;
  if (keyflag & 1) shiftstate = true;
  for (;;) {
    drawScreen();
    SetSetupSetting(0x14, 0);
    int ret = GetKeyWait_OS(&col, &row, 2, timeout_delay, 1, &keycode);
    int key = 0;
    if (ret == 0) continue;
    if ((!shiftstate && col == 4 && row == 9) || ret == 2) {   // MENU, or the auto power-off timeout
      MENU_save(); set_menu_timer();
      enterKhicas();
      GetKey(&key);
      return 0;
    }
    int code = col == 1 ? 35 : (10 - row) * 6 + (7 - col);
    if (code == 6) { shiftstate = !shiftstate; continue; }
    if (code == 12) {
      if (shiftstate) { alphastate = alphalock = true; shiftstate = false; }
      else if (alphastate) { alphastate = alphalock = false; }
      else alphastate = true;
      continue;
    }
    if (code < 0 || code >= 54) continue;
    if (shiftstate && code == 35) {   // SHIFT AC/ON: off
      shiftstate = false;
      enterKhicas();
      set_menu_timer();
      GetKey(&key);
      return 0;
    }
    if (alphastate) code += alphamaj ? 2 * 9 * 6 : 3 * 9 * 6;
    else if (shiftstate) code += 9 * 6;
    key = ups_translate_key(code);
    if (alphalock && key == KEY_CTRL_UNDO) key = KEY_CTRL_DEL;
    if (alphastate && !alphalock && key >= KEY_CTRL_F1 && key <= KEY_CTRL_F6) key += KEY_CTRL_F13 - KEY_CTRL_F1;
    oldshiftstate = shiftstate; shiftstate = false;
    oldalphastate = alphastate;
    if (!alphalock) alphastate = false;
    return key;
  }
}

static void send(int k, unsigned cp = 0, const char * text = 0) {
  ups::Event e; e.key = k; e.cp = cp; e.text = text;
  ups::handle(e);
}

static void insertString(const char * s) {
  if (!s || !*s) return;
  int l = strlen(s);
  const unsigned char * u = (const unsigned char *)s;
  if (l == 1 && u[0] < 0x80) { send(ups::K_CHAR, u[0]); return; }
  if (s[l - 1] == '(' && l < 60) {   // functions: sin( -> sin(|)
    char buf[64]; memcpy(buf, s, l); buf[l] = '\x11'; buf[l + 1] = ')'; buf[l + 2] = 0;
    send(ups::K_TEXT, 0, buf);
    return;
  }
  send(ups::K_TEXT, 0, s);
}

static int fkeyIndex(int key) {   // F1..F18 -> 1..18
  if (key >= KEY_CTRL_F1 && key <= KEY_CTRL_F6) return key - KEY_CTRL_F1 + 1;
  if (key >= KEY_CTRL_F7 && key <= KEY_CTRL_F20) return key - KEY_CTRL_F7 + 7;
  return 0;
}

static void dispatch(int key) {
  using namespace ups;
  if (key == 0) return;
  if (toolboxOpen()) {   // the toolbox only takes navigation keys
    switch (key) {
    case KEY_CTRL_UP: send(K_UP); break;
    case KEY_CTRL_DOWN: send(K_DOWN); break;
    case KEY_CTRL_LEFT: send(K_LEFT); break;
    case KEY_CTRL_RIGHT: send(K_RIGHT); break;
    case KEY_CTRL_EXE: send(K_EXE); break;
    case KEY_CTRL_EXIT: case KEY_CTRL_DEL: send(K_BACK); break;
    case KEY_CTRL_OPTN: send(K_TOOLBOX); break;
    }
    return;
  }
  switch (key) {
  case KEY_CTRL_EXE: send(K_EXE); return;
  case KEY_CTRL_DEL: send(K_DEL); return;
  case KEY_CTRL_AC: send(K_AC); return;
  case KEY_CTRL_EXIT: send(K_BACK); return;
  case KEY_CTRL_LEFT: send(K_LEFT); return;
  case KEY_CTRL_RIGHT: send(K_RIGHT); return;
  case KEY_CTRL_UP: send(K_UP); return;
  case KEY_CTRL_DOWN: send(K_DOWN); return;
  case KEY_CTRL_OPTN: send(K_TOOLBOX); return;
  case KEY_CHAR_FRAC: case KEY_CHAR_DIV: send(xcas_python_eval == 1 ? K_CHAR : K_FRAC, '/'); return;
  case KEY_CHAR_POW: send(K_POW); return;
  case KEY_CHAR_SQUARE: send(K_SQUARE); return;
  case KEY_CHAR_ROOT: send(K_SQRT); return;
  case KEY_CHAR_POWROOT: send(K_NROOT); return;
  case KEY_CHAR_CUBEROOT: send(K_NROOT); send(K_CHAR, '3'); send(K_RIGHT); return;
  case KEY_CHAR_EXPN: send(K_EXP); return;
  case KEY_CHAR_EXP: send(K_TENPOW); return;
  case KEY_CHAR_VALR: send(K_ABS); return;
  case KEY_CHAR_RECIP: send(K_POW); send(K_CHAR, '-'); send(K_CHAR, '1'); send(K_RIGHT); return;
  case KEY_CHAR_MULT: send(K_CHAR, 0xd7); return;
  case KEY_CHAR_PLUS: send(K_CHAR, '+'); return;
  case KEY_CHAR_MINUS: case KEY_CHAR_PMINUS: send(K_CHAR, '-'); return;
  case KEY_CHAR_PI: send(K_CHAR, 0x3c0); return;
  case KEY_CHAR_STORE: send(K_CHAR, 0x2192); return;
  case KEY_CTRL_F4: { char buf[256]; if (hAction("catalog", buf, sizeof(buf))) insertString(buf); return; }
  case KEY_CTRL_F5: case KEY_CTRL_F6: bridge(key); return;
  }
  int f = fkeyIndex(key);
  if (f > 0 && f <= 18) {
    if (s_fkeyCat[f] >= 0) openToolbox(s_fkeyCat[f]);
    return;
  }
  enterKhicas();
  const char * s = keytostring(key, GetSetupSetting((unsigned int)0x14));
  if (s) { insertString(s); return; }
  bridge(key);
}

/* ---------------- screen ---------------- */

static ups::Host s_host = {hEval, hCount, hGet, hAdd, hRemove, tLeft, tRight, pythonMode, hAction};

static void drawScreen() {
  EnableStatusArea(3);        // our own title bar replaces the OS status bar
  mocha_paused = 1;           // already drawn in Mocha colours
  ups::setFramebuffer((unsigned short *)GetVRAMAddress());
  ups::draw();
  Bdisp_PutDisp_DD();
}

void ups_main() {
  if (!s_join[0]) { s_join[0] = (char *)malloc(JOIN_SIZE); s_join[1] = (char *)malloc(JOIN_SIZE); }
  ups_active = 1;
  s_builtFor = -2;
  buildToolbox();
  ups::init((unsigned short *)GetVRAMAddress(), &s_host, s_cats, s_nroot);
  syncCursor();
  for (;;) {
    int key = readKey();
    dispatch(key);
  }
}
