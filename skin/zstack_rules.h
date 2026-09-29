/* Stacked (2D) results in the Monolith shell: the pure decisions, no OS or Giac includes, so they are unit-tested
 * on the host (tests/harness/zstack_test.c). The Giac/OS glue is zstack.cc.
 *
 * zstack_wanted: cheap string test run on every visible OUTPUT level before anything is parsed. A string is a
 * candidate only if it looks like a plain Giac expression (printable ASCII, no space, none of the characters of
 * strings, lists, matrices, sequences, assignments, tests or messages) and contains a fraction, a power or a root:
 * '/', '^', '**' or "sqrt(" (not preceded by an identifier character: "mysqrt(" does not count). Operator shape:
 * no binary operator first, after '(' or last, none before ')', no two in a row except exactly "**" (a leading
 * '-' is unary). These keep strings that would not parse ("ab/", "1/2/") away from the parser. Limits (they also bound the typesetter recursion, see zstack.cc): at most
 * ZSTACK_MAX_LEN characters, bracket depth ZSTACK_MAX_PAREN, ZSTACK_MAX_OPS operator characters.
 *
 * zstack_layout: vertical placement of the visible levels when at least one is taller than a text row.
 * h[i] is the height of level i (i = 0 is the oldest visible level, the one at Start_Line), cursor the level that
 * must stay visible (Cursor.y), avail the pixel height of the work area. Writes y[i] = top offset in pixels, or
 * ZSTACK_HIDDEN. Rules: everything fits -> top-anchored like the stock grid (y[0] = 0); otherwise the oldest levels
 * are dropped (never the cursor level) and the rest is bottom-anchored so the current line stays at the bottom;
 * if that is still too tall (the cursor is high up in the history), levels below the cursor are dropped as well
 * and the rest is top-anchored from the cursor level. With all heights equal to a text row and n <= avail/row,
 * y[i] = i * row: the stock layout. Returns the number of visible levels. */
#ifndef ZSTACK_RULES_H
#define ZSTACK_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

#define ZSTACK_MAX_LEN 100
#define ZSTACK_MAX_PAREN 8
#define ZSTACK_MAX_OPS 24
#define ZSTACK_HIDDEN (-1)
#define ZSTACK_INPUT_MAX_LEN 240      /* longest past input tried as a 2D template expression */
#define ZSTACK_INPUT_PAD 2            /* pixels above and below a 2D input block */

int zstack_wanted(const char *s);
/* Past INPUT levels (M2 task 1): cheap prefilter run on every visible input level before nin_from_string is tried. True when
 * the string is printable ASCII (no control or wide characters, no newline), has no string literal or comment quote (" ' #),
 * is at most ZSTACK_INPUT_MAX_LEN long and contains one of the serialised template markers ")/(" "^(" "sqrt(" "abs(" "surd("
 * "logb(" "subst(diff(" "integrate(" "sum(" "[[" (nin_to_giac writes exactly these). It says "worth parsing": nin_from_string
 * decides whether there is really a template (e.g. "checksum(1)" passes here and stays text there). */
int zstack_input_wanted(const char *s);
/* Block of a 2D input level from the nin_measure sizes (a above and b below the axis, width w): the height *h is a + b plus
 * ZSTACK_INPUT_PAD on both sides, at least minh (one text row); *axis is the offset of the axis from the block top (the
 * content is centred). Returns 0 (draw as text) when the block is taller than maxh (the work area) or wider than maxw, or a
 * size is negative. */
int zstack_input_block(int a, int b, int w, int minh, int maxh, int maxw, int *h, int *axis);
int zstack_layout(const int *h, int n, int cursor, int avail, int *y);

#ifdef __cplusplus
}
#endif
#endif
