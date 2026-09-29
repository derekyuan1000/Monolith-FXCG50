/* Stacked (2D) results in the Monolith shell (milestone M1): OUTPUT levels that are fractions, powers or roots are
 * drawn with Giac's own 2D typesetter (zdisplay.cc: Equation_compute_size / Equation_draw), right-aligned at
 * ZSTACK_RIGHT_X, instead of one line of text. History levels stay plain strings (sessions, Giac and the key
 * handling are unchanged); the string is parsed again only to draw it, and the laid-out tree is kept in a small
 * cache of ZSTACK_CACHE entries (the visible levels). Anything that cannot be stacked safely falls back to the
 * stock text drawing: zstack_wanted() false, parse failure, a list/matrix/sequence, a tree deeper than
 * ZSTACK_MAX_TREE or larger than max_prettyprint_equation, wider than ZSTACK_MAX_W or taller than ZSTACK_MAX_H even
 * at font size 14, free memory under ZSTACK_MIN_FREE, a computation in progress, or a Python/JS interpreter.
 *
 * Only a Giac result is a candidate: an OUTPUT level directly between two INPUT levels (program print() output
 * stays text). The cache key is the string plus python_compat. The parse is muted (dconsole_mode = 0) and leaves
 * the parser globals as they were. zstack_release() is called by do_run before each evaluation, so no cached tree
 * sits on the heap during a computation.
 *
 * Past INPUT levels (M2 task 1): an input level whose string nin_from_string (nin/, the natural-input model) turns into at
 * least one template (fraction, power, root, abs, nth root, log base, derivatives, integral, sum, matrix) is drawn through
 * nin_draw, left-aligned at x = 2, with the large-font metrics of the natural editor (skin/znat.cc), instead of one line
 * of text. Prefilter zstack_input_wanted() (pure, host-tested); a level is at least one text row high and at most the
 * work area, a taller or wider one stays text, as does the level being edited (the last line and a non-read-only cursor
 * line), Python/JS input, a string with a byte outside 0x20..0x7E (nin would replace it) and anything nin cannot hold.
 * The nin scratch (about 1.8 KB) is allocated on the first use and freed by zstack_release(); the strings are parsed again
 * for every frame (nothing is cached, nothing is allocated per frame). A cursor on a read-only 2D input is shown by a frame.
 *
 * Compiled as zstack.o (.rominram). Pure decisions (string test, vertical layout) are in zstack_rules.c. */
#ifndef ZSTACK_H
#define ZSTACK_H
#include "zstack_rules.h"
#include "theme.h"

#define ZSTACK_RIGHT_X RM_RESULT_RIGHT_X  /* right edge of stacked results; must match console.cc's text-result right edge */
#define ZSTACK_MAX_W 372        /* widest stacked result (left edge >= 2) */
#define ZSTACK_MAX_H 90         /* tallest stacked block, pad included (5 text rows) */
#define ZSTACK_PAD_TOP 1        /* block = pad + typesetter box + 4: the typesetter's glyphs sit 2 px below its box */
#define ZSTACK_PAD 5
#define ZSTACK_CACHE 10         /* > LINE_DISP_MAX (9): one frame never evicts an entry it uses */
#define ZSTACK_MAX_TREE 24      /* gen nesting (a binary operator counts 2: symbolic + argument vector) */
#define ZSTACK_MIN_FREE (64 * 1024)

/* zstack_console_layout mode[] values */
enum { ZSTACK_TEXT = 0, ZSTACK_2D = 1, ZSTACK_OFF = 2 };

/* 1 and *w, *h (block size in pixels) when str is drawn stacked; 0 when it must be drawn as text. */
int zstack_measure(const char *str, int *w, int *h);
/* Draw str stacked, right edge right_x, block top top_y (absolute screen y). Nothing if zstack_measure says 0. */
void zstack_draw(const char *str, int right_x, int top_y, int selected);

/* For Console_Disp. Fills, for the visible levels i = 0 .. LINE_DISP_MAX-1 (level Start_Line + i):
 * y[i] = pixel offset below the status area (print_y units) and mode[i]. Returns 0 when no visible level is
 * stacked: then y[i] == i * vfontsize and mode[i] == ZSTACK_TEXT for every i, i.e. the stock layout, and
 * Console_Disp draws exactly what it drew before. */
int zstack_console_layout(int *y, unsigned char *mode);
/* Draw visible level i (mode ZSTACK_2D) at offset y; selected: the cursor is on it (drawn inverted). */
void zstack_console_draw(int i, int y, int selected);
/* Free the cache (low memory). */
void zstack_release(void);

#endif
