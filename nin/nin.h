/* nin: natural-input model (RUN-MAT style). Pure C99: no Giac, no OS. Tree of rows and items with a caret,
 * serialisation to the Giac string, parsing back, layout and drawing through gfx.h.
 * Text characters and templates (each a kind with one to four slots; see the NIN_FRAC enum below). Behaviours marked
 * PROVISIONAL are not yet measured on native RUN-MAT (docs/superpowers/specs/2026-09-24-natural-input-design.md, section 5). */
#ifndef NIN_NIN_H
#define NIN_NIN_H
#include "gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Capacities (static memory: 6 bytes per item, 5 per row). Indices are unsigned char, so both must be < 255.
 * NIN_MAX_FRACS is the capacity for templates of ANY kind (the historic name is kept: tests and tools define it): a tree
 * holds at most that many templates in total, so nesting depth, and with it the recursion depth on the device, is bounded.
 * A template owns one to four slots (rows) and the pool has 1 + 2 * NIN_MAX_FRACS rows (root row included), so the ROW pool
 * limits the templates with many slots: with the default 65 rows a tree holds at most 32 one- or two-slot templates but only
 * 21 nested integrals (3 slots) or 16 nested sums (4 slots). An insertion that does not fit returns NIN_ERR_FULL and changes
 * nothing. */
#ifndef NIN_MAX_ITEMS
#define NIN_MAX_ITEMS 250
#endif
#ifndef NIN_MAX_FRACS
#define NIN_MAX_FRACS 32
#endif
#define NIN_MAX_ROWS (1 + 2 * NIN_MAX_FRACS)
#define NIN_NONE 0xFF

/* Template kinds. The value is what nin_item.c holds for a template item (characters are 0x20..0x7E). Slots:
 *   NIN_FRAC  sub[0] numerator, sub[1] denominator
 *   NIN_POW   sub[0] base, sub[1] exponent (superscript)
 *   NIN_SQRT  sub[0] radicand (sub[1] is NIN_NONE)
 *   NIN_ABS   sub[0] argument (sub[1] is NIN_NONE)
 *   NIN_NROOT sub[0] index n, sub[1] radicand x; written surd(x,n) (Giac's REAL n-th root: surd(-8,3) is -2)
 *   NIN_LOGAB sub[0] base b, sub[1] argument x; written logb(x,b)
 *   NIN_DERIV sub[0] f, sub[1] the point x0: d/dx f at x = x0, written subst(diff(f,x),x=x0) (variable x; the
 *             substitution is what makes the value at a point; f must not use an x that holds a value)
 *   NIN_DERIV2 like NIN_DERIV for the second derivative: subst(diff(f,x,2),x=x0)
 *   NIN_INTEGRAL sub[0] integrand f, slot 1 lower limit, slot 2 upper limit; written integrate(f,x,lo,hi) (not int(...):
 *             in TI mode Giac's int with a sequence is a floor). Three slots take three rows, so the row pool holds fewer of them.
 *   NIN_SIGMA sub[0] summand f, slot 1 index variable, slot 2 lower bound, slot 3 upper bound; written sum(f,var,lo,hi)
 *             (drawn with a "var=lo" line under the sign; four rows per template)
 * A template has at most 4 slots; slots 2 and 3 are reached through nin_row.nxt (sub[] names the first two only). */
/* NIN_MATRIX_KIND(rows, cols), rows and cols 1..NIN_MAT_MAX: a matrix (or vector: one column or one row) whose cells are the
 * slots in row-major order, so a matrix of R x C cells owns R*C slot rows out of the row pool (at most 16 of the 65). The
 * dimensions live in the kind byte (0x8A + 4 * (rows - 1) + (cols - 1)), so an item stays 6 bytes. Written [[a,b],[c,d]];
 * a one-row matrix is [[a,b,c]] and a one-column one [[a],[b],[c]] (a bare Giac vector [a,b] is not a template). */
#define NIN_MAT_MAX 4
#define NIN_MATRIX_BASE 0x8A
#define NIN_MATRIX_KIND(rows, cols) (NIN_MATRIX_BASE + ((rows) - 1) * NIN_MAT_MAX + ((cols) - 1))
#define NIN_IS_MATRIX(kind) ((kind) >= NIN_MATRIX_BASE && (kind) < NIN_MATRIX_BASE + NIN_MAT_MAX * NIN_MAT_MAX)
enum { NIN_FRAC = 0x80, NIN_POW = 0x81, NIN_SQRT = 0x82, NIN_ABS = 0x83, NIN_NROOT = 0x84, NIN_LOGAB = 0x85, NIN_DERIV = 0x86, NIN_DERIV2 = 0x87, NIN_INTEGRAL = 0x88, NIN_SIGMA = 0x89 };
#define NIN_IS_TEMPLATE(c) ((c) >= NIN_FRAC)

typedef struct { unsigned char c, next, prev, row, sub[2]; } nin_item;          /* c: 0x20..0x7E char, or a template kind */
typedef struct { unsigned char first, last, owner, slot, nxt; } nin_row;     /* owner: template item or NIN_NONE (root);
                                                                            nxt: row of the owner's next slot (slot rows are chained;
                                                                            an item's sub[0], sub[1] name the first two) */
typedef struct {
  nin_item items[NIN_MAX_ITEMS];
  nin_row rows[NIN_MAX_ROWS];
  unsigned char item_free, row_free;
  unsigned char caret_row, caret_after;     /* caret sits right after item caret_after in caret_row (NIN_NONE: row start) */
  unsigned char templates;                  /* live template items, at most NIN_MAX_FRACS */
} nin;

enum { NIN_LEFT, NIN_RIGHT, NIN_UP, NIN_DOWN };
enum { NIN_ERR_FULL = -1, NIN_ERR_EMPTY_SLOT = -2, NIN_ERR_SIZE = -3, NIN_ERR_CHAR = -4, NIN_ERR_SYNTAX = -5 };
enum { NIN_WARN_LOSSY = 1 };              /* positive return of nin_from_string: the tree is valid, some bytes were replaced */

void nin_init(nin *n);
int nin_insert_char(nin *n, char c);      /* 0 or NIN_ERR_FULL / NIN_ERR_CHAR */
/* The run of digits and '.' right before the caret becomes the numerator (native RUN-MAT, confirmed by Derek
 * 2026-09-24; the scope of "number" is provisional) and the caret goes to the start of the denominator; with no such
 * run the numerator is empty and the caret goes into it. Returns 0 or NIN_ERR_FULL. */
int nin_insert_frac(nin *n);
/* Inserts a template of the given kind (NIN_FRAC, ...) at the caret. Returns 0, NIN_ERR_FULL (nothing changed: the item
 * pool, the row pool or the template count NIN_MAX_FRACS is exhausted) or NIN_ERR_CHAR (unknown kind). nin_insert_frac is
 * nin_insert_template(n, NIN_FRAC). */
int nin_insert_template(nin *n, int kind);
/* The x-squared key: a NIN_POW template with the exponent 2 already typed, the caret right AFTER the template. The base is
 * captured exactly as nin_insert_template(n, NIN_POW) does. Needs two free items; on NIN_ERR_FULL nothing changed. */
int nin_insert_square(nin *n);
/* The x^-1 (reciprocal) key: a NIN_POW template with the exponent -1 already typed, the caret right AFTER the template.
 * Same capture and caret placement as nin_insert_square. Needs three free items; on NIN_ERR_FULL nothing changed. */
int nin_insert_recip(nin *n);
int nin_backspace(nin *n);                /* 1 if something changed, else 0 */
void nin_move(nin *n, int dir);

/* Matrix API. Inserts an empty rows x cols matrix at the caret (nothing is captured; the caret goes into cell 0). Returns 0,
 * NIN_ERR_CHAR (rows or cols outside 1..NIN_MAT_MAX) or NIN_ERR_FULL (the row pool or the template count is exhausted:
 * nothing changed, a matrix must not starve normal typing, so a 4 x 4 one takes 16 of the 65 rows). */
int nin_insert_matrix(nin *n, int rows, int cols);
/* The functions below act on the matrix that holds the caret: the innermost one around it, or the one right before it when
 * the caret stands just behind it. PROVISIONAL: the keys are wired in task 5.
 *   nin_matrix_dims    1 and its size, or 0 when there is no such matrix;
 *   add_row / add_col  a new empty row / column right after the caret's row / column (at the end when the caret is behind
 *                      the matrix);
 *   del_row / del_col  removes the caret's row / column WITH its content (nested templates are freed); the caret goes to
 *                      the end of the nearest cell that is left;
 *   nin_matrix_resize  to rows x cols, growing or shrinking at the bottom / right.
 * Return 0, NIN_ERR_CHAR (no matrix, a size outside 1..NIN_MAT_MAX, or removing the last row / column) or NIN_ERR_FULL (the
 * new empty cells do not fit in the row pool: nothing changed). Cells that stay keep their content and their caret. */
int nin_matrix_dims(const nin *n, int *rows, int *cols);
int nin_matrix_add_row(nin *n);
int nin_matrix_add_col(nin *n);
int nin_matrix_del_row(nin *n);
int nin_matrix_del_col(nin *n);
int nin_matrix_resize(nin *n, int rows, int cols);
int nin_is_empty(const nin *n);           /* nothing typed at all */

/* Giac string; returns its length, or a negative error:
 *   NIN_ERR_EMPTY_SLOT  an empty box remains;
 *   NIN_ERR_SYNTAX      a template slot holds unbalanced parentheses (typed "(" or ")" characters; nested templates
 *                       are always balanced), or a slot of surd/logb/diff/integrate/sum holds a depth-0 comma (it would
 *                       add an argument: logb(8,2,7) is another function), or a stray "]" / ")" or an unclosed "[" / "(" stands
 *                       in such a slot or in a matrix cell (parentheses and brackets counted apart; two slots must not
 *                       combine into another structure). Other kinds check parentheses only. The root row may hold
 *                       anything the user typed, so it is never checked. Both errors mean "cannot be evaluated";
 *   NIN_ERR_SIZE        the output does not fit (the buffer holds the truncated, terminated text).
 * buf is always terminated when size > 0. */
int nin_to_giac(const nin *n, char *buf, int size);
/* Rebuild the tree from a string. The caret goes to the end. Returns:
 *   0                   the tree was built;
 *   NIN_WARN_LOSSY (1)  the tree was built and is valid, but at least one byte outside 0x20..0x7E was replaced by '?';
 *   NIN_ERR_FULL        the string needs more items or templates than the pool holds: the tree is left EMPTY.
 *                       The caller (the shell integration) must keep its original string and edit it linearly then.
 * Only the exact forms that nin_to_giac writes are recognised as templates: "((A)/(B))", "(A)^(B)", "sqrt(A)", "abs(A)",
 * "surd(X,N)", "logb(X,B)", "subst(diff(F,x),x=X0)", "subst(diff(F,x,2),x=X0)", "integrate(F,x,LO,HI)", "sum(F,V,LO,HI)".
 * The spec's "(A)/(B)", "A/B" and "A^B" shorthands are NOT parsed, they stay plain text. In the comma forms the arguments are
 * split at depth-0 commas (brackets count) and a slot with a depth-0 comma is not accepted. A candidate is accepted only if
 * all slots are non-empty and it is not glued to an operand on either side (nin_to_giac would have inserted a '*' there); otherwise
 * it stays plain text, so text the user wrote such as sin((1)/(2)) is never changed.
 * Round trip guarantee: a string produced by nin_to_giac re-serialises to the identical string after nin_from_string,
 * as long as the tree is not within 2 items of NIN_MAX_ITEMS (an implicit '*' in the string costs one item, so a tree
 * near capacity may not survive a serialise/parse round trip: callers fall back to the original string). */
int nin_from_string(nin *n, const char *s);

/* Layout. Sizes are measured from the axis (the fraction bar centre; half the character height for text). */
typedef struct { int w, a, b; } nin_size;
typedef struct {
  int (*char_w)(void *ctx, char c); void *ctx;
  int char_h;            /* text row height */
  int slot_w, slot_h;    /* empty box */
  int bar_h, bar_pad, gap;
  int clip_x0, clip_y0, clip_x1, clip_y1;   /* characters whose box does not touch this rectangle (x1/y1 exclusive) are not drawn */
  int large;             /* 0: draw text with gfx_text (small OS font); nonzero: gfx_text_large (18x24 font). Last field: initialisers must give it explicitly (-Wextra) */
} nin_metrics;
typedef struct { int valid, x, y, h; } nin_caret_pos;

void nin_measure(const nin *n, const nin_metrics *m, nin_size *out);
/* Draws the whole expression with its axis at axis_y and its left edge at x, and the caret (2 px bar).
 * Text is skipped when its box lies wholly outside the clip rectangle of m (the OS text call is not trusted to clip);
 * a partly visible character is still drawn. Boxes, bars and the caret go through gfx_fill_rect, which clips itself. */
void nin_draw(const nin *n, const nin_metrics *m, int x, int axis_y, nin_caret_pos *caret);

#ifdef __cplusplus
}
#endif
#endif
