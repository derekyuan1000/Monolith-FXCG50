/* upsui: an Upsilon-style Calculation screen for KhiCAS (fx-CG50).
 *
 * Self-contained (no giac, no STL, no OS calls) so it can be tested on a PC:
 * the host draws into a 384x216 RGB565 framebuffer and feeds key events.
 * KhiCAS provides evaluation and history storage through ups::Host.
 *
 * Pieces:
 *  - Upsilon's own fonts (LargeFont 10x18, SmallFont 7x14, antialiased)
 *  - a layout tree like Poincare's (rows, fractions, powers, roots, |x|, matrices)
 *  - a 2D editor with a cursor that moves through fractions, powers, roots
 *  - a parser turning giac's text output into layouts, and a serialiser
 *    turning the edited layout into giac syntax
 *  - the Calculation view (title bar, history cells, input field) and an
 *    Upsilon-style toolbox, which replaces KhiCAS's F-key bar on OPTN
 */
#ifndef UPSUI_H
#define UPSUI_H

namespace ups {

enum { SCREEN_W = 384, SCREEN_H = 216 };

/* Catppuccin Mocha, as in Upsilon's theme (RGB565) */
enum Colour {
  C_BASE = 0x18E5, C_MANTLE = 0x18C4, C_CRUST = 0x1083,
  C_SURFACE0 = 0x3188, C_SURFACE1 = 0x422B, C_SURFACE2 = 0x5ACE,
  C_OVERLAY0 = 0x6B90, C_SUBTEXT0 = 0xA575, C_TEXT = 0xCEBE,
  C_MAUVE = 0xCD3E, C_RED = 0xF455, C_PEACH = 0xFD90, C_GREEN = 0xA714
};

enum KeyCode {
  K_NONE = 0,
  K_CHAR,      // insert code point ev.cp
  K_TEXT,      // insert ev.text (UTF-8); \x11 marks where the cursor goes
  K_LEFT, K_RIGHT, K_UP, K_DOWN,
  K_DEL, K_AC, K_EXE, K_BACK,
  K_FRAC,      // fraction template (numerator collapses the term on the left)
  K_POW,       // superscript box
  K_SQUARE,    // ^2
  K_SQRT,      // square root template
  K_NROOT,     // n-th root template
  K_EXP,       // e^box
  K_TENPOW,    // x10^box
  K_ABS,       // |box|
  K_TOOLBOX    // OPTN
};

struct Event {
  int key;
  unsigned cp;
  const char * text;
};

/* What KhiCAS provides */
struct Host {
  // Evaluate giac input; write the result (giac syntax) in out. Return false on error (out holds the message).
  bool (*evaluate)(const char * input, char * out, int outSize);
  int (*historyCount)();
  // Input and output texts of calculation i (0 = oldest)
  void (*historyGet)(int i, const char ** input, const char ** output);
  void (*historyAdd)(const char * input, const char * output);
  void (*historyRemove)(int i);
  // Title bar: left text (e.g. "xcas rad"), right text (e.g. clock); may be null
  const char * (*titleLeft)();
  const char * (*titleRight)();
  // Python syntax for the input (** and explicit *); may be null
  bool (*pythonMode)();
  // Toolbox entries whose insert text starts with \x01 run an action (catalogue, settings...).
  // Return true and fill out with text to insert, or false to insert nothing. May be null.
  bool (*action)(const char * id, char * out, int outSize);
};

/* Toolbox tree: a leaf inserts `insert`; a node opens `children` */
struct MenuItem {
  const char * label;     // e.g. "integrate(f,x)"
  const char * help;      // e.g. "Antiderivative" (may be null)
  const char * insert;    // text inserted, \x11 = cursor; null for a submenu
  const MenuItem * children;
  int count;
};

void init(unsigned short * framebuffer, const Host * host, const MenuItem * toolboxRoot, int toolboxCount);
void setFramebuffer(unsigned short * framebuffer);
void setToolbox(const MenuItem * toolboxRoot, int toolboxCount);   // e.g. after switching to Python
// Returns true if the event was used. EXE evaluates through Host.
bool handle(const Event & e);
void draw();

/* Pieces exposed for tests */
int serializeInput(char * out, int size);             // current input as giac text
void setInputFromText(const char * text);              // parse giac text into the input field
int layoutRoundTrip(const char * text, char * out, int size); // parse then serialise
bool toolboxOpen();
// Open the toolbox, optionally inside top-level entry `index` (-1 = top level)
void openToolbox(int index);
// Insert text in the input field as menus do (\x11 = cursor)
void insertInInput(const char * text);
bool inputEmpty();

}

#endif
