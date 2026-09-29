#include <string.h>
#include "nin.h"

#if NIN_MAX_ITEMS > 254 || NIN_MAX_ROWS > 254
#error "nin: indices are unsigned char"
#endif

/* Highest template kind this file implements (nin.h may list more). */
#define K_LAST NIN_SIGMA

static int is_tpl(const nin *n, int it) { return NIN_IS_TEMPLATE(n->items[it].c); }
static int row_empty(const nin *n, int row) { return n->rows[row].first == NIN_NONE; }
static int is_mat(int kind) { return NIN_IS_MATRIX(kind); }
static int mat_rows(int kind) { return (kind - NIN_MATRIX_BASE) / NIN_MAT_MAX + 1; }
static int mat_cols(int kind) { return (kind - NIN_MATRIX_BASE) % NIN_MAT_MAX + 1; }
static int valid_kind(int kind) { return (kind >= NIN_FRAC && kind <= K_LAST) || is_mat(kind); }
/* number of slots (rows) a template of this kind owns: 1 to 4, or rows * cols for a matrix */
static int nslots(int kind)
{
  if (is_mat(kind)) return mat_rows(kind) * mat_cols(kind);
  switch (kind) {
    case NIN_FRAC: case NIN_POW: case NIN_NROOT: case NIN_LOGAB: case NIN_DERIV: case NIN_DERIV2: return 2;
    case NIN_INTEGRAL: return 3;
    case NIN_SIGMA: return 4;
    default: return 1;
  }
}
static int tpl_slots(const nin *n, int it) { return nslots(n->items[it].c); }
/* Row of slot k of template item it. Slots are chained through nin_row.nxt (sub[0] and sub[1] are the first two). */
static int slot_row(const nin *n, int it, int k)
{
  int r = n->items[it].sub[0];
  while (k-- > 0) r = n->rows[r].nxt;
  return r;
}
/* the template has no content at all (every slot empty) */
static int tpl_empty(const nin *n, int it)
{
  int k;
  for (k = 0; k < tpl_slots(n, it); k++) if (!row_empty(n, slot_row(n, it, k))) return 0;
  return 1;
}

static int alloc_item(nin *n)
{
  int i = n->item_free;
  if (i == NIN_NONE) return -1;
  n->item_free = n->items[i].next;
  return i;
}
static void free_item(nin *n, int i) { n->items[i].next = n->item_free; n->item_free = (unsigned char)i; }

static int alloc_row(nin *n, int owner, int slot)
{
  int r = n->row_free;
  if (r == NIN_NONE) return -1;
  n->row_free = n->rows[r].first;
  n->rows[r].first = n->rows[r].last = NIN_NONE;
  n->rows[r].owner = (unsigned char)owner;
  n->rows[r].slot = (unsigned char)slot;
  n->rows[r].nxt = NIN_NONE;
  return r;
}
static void free_row(nin *n, int r) { n->rows[r].first = n->row_free; n->row_free = (unsigned char)r; }

static void link_after(nin *n, int row, int after, int it)
{
  nin_item *p = &n->items[it];
  nin_row *r = &n->rows[row];
  int nxt = (after == NIN_NONE) ? r->first : n->items[after].next;
  p->row = (unsigned char)row;
  p->prev = (unsigned char)after;
  p->next = (unsigned char)nxt;
  if (after == NIN_NONE) r->first = (unsigned char)it; else n->items[after].next = (unsigned char)it;
  if (nxt == NIN_NONE) r->last = (unsigned char)it; else n->items[nxt].prev = (unsigned char)it;
}

static void unlink_item(nin *n, int it)
{
  nin_item *p = &n->items[it];
  nin_row *r = &n->rows[p->row];
  if (p->prev == NIN_NONE) r->first = p->next; else n->items[p->prev].next = p->next;
  if (p->next == NIN_NONE) r->last = p->prev; else n->items[p->next].prev = p->prev;
}

/* A template item with its empty slot rows (not linked anywhere yet). Returns the item, or -1 and nothing allocated when
 * the template count, the item pool or the row pool is exhausted. */
static int alloc_template(nin *n, int kind)
{
  int it, s, ns = nslots(kind), first = NIN_NONE, prev = NIN_NONE;
  if (n->templates >= NIN_MAX_FRACS) return -1;
  it = alloc_item(n);
  if (it < 0) return -1;
  for (s = 0; s < ns; s++) {
    int r = alloc_row(n, it, s);
    if (r < 0) {                                  /* give back the rows taken so far */
      for (r = first; r != NIN_NONE; ) { int nx = n->rows[r].nxt; free_row(n, r); r = nx; }
      free_item(n, it);
      return -1;
    }
    if (prev == NIN_NONE) first = r; else n->rows[prev].nxt = (unsigned char)r;
    prev = r;
  }
  n->items[it].c = (unsigned char)kind;
  n->items[it].sub[0] = (unsigned char)first;
  n->items[it].sub[1] = (unsigned char)(ns > 1 ? n->rows[first].nxt : NIN_NONE);
  n->templates++;
  return it;
}

void nin_init(nin *n)
{
  int i;
  for (i = 0; i < NIN_MAX_ITEMS; i++) n->items[i].next = (unsigned char)(i + 1 < NIN_MAX_ITEMS ? i + 1 : NIN_NONE);
  for (i = 0; i < NIN_MAX_ROWS; i++) n->rows[i].first = (unsigned char)(i + 1 < NIN_MAX_ROWS ? i + 1 : NIN_NONE);
  n->item_free = 0;
  n->row_free = 0;
  n->templates = 0;
  alloc_row(n, NIN_NONE, 0);          /* row 0 is the root row */
  n->caret_row = 0;
  n->caret_after = NIN_NONE;
}

int nin_is_empty(const nin *n) { return row_empty(n, 0); }

int nin_insert_char(nin *n, char c)
{
  int it;
  if (c < 0x20 || c > 0x7E) return NIN_ERR_CHAR;
  it = alloc_item(n);
  if (it < 0) return NIN_ERR_FULL;
  n->items[it].c = (unsigned char)c;
  n->items[it].sub[0] = n->items[it].sub[1] = NIN_NONE;
  link_after(n, n->caret_row, n->caret_after, it);
  n->caret_after = (unsigned char)it;
  return 0;
}

static int is_number_char(const nin *n, int it)
{
  int c = n->items[it].c;
  return (c >= '0' && c <= '9') || c == '.';
}
static int is_name_char(const nin *n, int it)
{
  int c = n->items[it].c;
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
static int is_digit_item(const nin *n, int it) { return n->items[it].c >= '0' && n->items[it].c <= '9'; }

/* The run of digits and '.' ending at the caret (its first item), or NIN_NONE. */
static int number_run_first(const nin *n)
{
  int first = NIN_NONE, cur;
  for (cur = n->caret_after; cur != NIN_NONE && !is_tpl(n, cur) && is_number_char(n, cur); cur = n->items[cur].prev)
    first = cur;
  return first;
}

/* The atom right before the caret, as the base of a power (its first item), or NIN_NONE. PROVISIONAL scope, in order:
 *   a template item (the whole thing);
 *   a number: the run of digits and '.' (but digits that end an identifier belong to it: "x2" is one name);
 *   a group "(...)" ending at the caret, with the function name right before its '(' when there is one ("sin(x)");
 *   an identifier: the run of letters, digits and '_' ending at the caret, minus its leading digits ("3x" gives "x").
 * Anything else (an operator, an unmatched ')', nothing) captures nothing. */
static int pow_atom_first(const nin *n)
{
  int last = n->caret_after, cur, first, name, depth;
  if (last == NIN_NONE) return NIN_NONE;
  if (is_tpl(n, last)) return last;
  if (is_number_char(n, last)) {
    int point = 0;
    first = number_run_first(n);
    /* "x2", "a1": digits that end an identifier belong to its name (an "x2" base must not become x*(2)^e). A run with a
     * '.' is a number ("x2.5" gives 2.5). */
    for (cur = last; cur != n->items[first].prev; cur = n->items[cur].prev) point |= n->items[cur].c == '.';
    cur = n->items[first].prev;
    if (point || cur == NIN_NONE || is_tpl(n, cur) || !is_name_char(n, cur) || is_digit_item(n, cur)) return first;
    /* falls through to the identifier branch below */
  }
  if (n->items[last].c == ')') {
    depth = 0;
    for (cur = last; cur != NIN_NONE; cur = n->items[cur].prev) {
      if (is_tpl(n, cur)) continue;                   /* a template is balanced in itself */
      if (n->items[cur].c == ')') depth++;
      else if (n->items[cur].c == '(' && --depth == 0) break;
    }
    if (cur == NIN_NONE) return NIN_NONE;             /* unmatched ')' */
    first = cur;
    name = NIN_NONE;
    for (cur = n->items[first].prev; cur != NIN_NONE && !is_tpl(n, cur) && is_name_char(n, cur); cur = n->items[cur].prev)
      name = cur;
    while (name != NIN_NONE && is_digit_item(n, name)) name = n->items[name].next;   /* leading digits are a coefficient */
    return (name != NIN_NONE && name != first) ? name : first;
  }
  if (is_name_char(n, last)) {
    for (cur = last, first = NIN_NONE; cur != NIN_NONE && !is_tpl(n, cur) && is_name_char(n, cur); cur = n->items[cur].prev) first = cur;
    while (first != last && is_digit_item(n, first)) first = n->items[first].next;
    return first;
  }
  return NIN_NONE;
}

/* First item of what the template captures as its first slot (the atom ending at the caret), or NIN_NONE when nothing. */
static int capture_first(const nin *n, int kind)
{
  /* native RUN-MAT (Derek, 2026-09-24): the number typed just before the fraction key becomes the numerator.
   * PROVISIONAL scope: the run of digits and '.' ending at the caret. */
  if (kind == NIN_FRAC) return number_run_first(n);
  if (kind == NIN_POW) return pow_atom_first(n);
  return NIN_NONE;                               /* every other kind: nothing is captured, the first slot starts empty */
}

int nin_insert_template(nin *n, int kind)
{
  int it, first, cur, slot0;
  if (!valid_kind(kind)) return NIN_ERR_CHAR;
  it = alloc_template(n, kind);
  if (it < 0) return NIN_ERR_FULL;
  slot0 = n->items[it].sub[0];
  first = capture_first(n, kind);
  link_after(n, n->caret_row, n->caret_after, it);
  if (first == NIN_NONE) {
    n->caret_row = (unsigned char)slot0;
    n->caret_after = NIN_NONE;
    return 0;
  }
  for (cur = first; cur != it; ) {              /* the run is followed by the new template item */
    int nx = n->items[cur].next;
    unlink_item(n, cur);
    link_after(n, slot0, n->rows[slot0].last, cur);
    cur = nx;
  }
  n->caret_row = (unsigned char)slot_row(n, it, 1);   /* the captured first slot is done: on to the second one */
  n->caret_after = NIN_NONE;
  return 0;
}

int nin_insert_frac(nin *n) { return nin_insert_template(n, NIN_FRAC); }

int nin_insert_matrix(nin *n, int rows, int cols)
{
  if (rows < 1 || rows > NIN_MAT_MAX || cols < 1 || cols > NIN_MAT_MAX) return NIN_ERR_CHAR;
  return nin_insert_template(n, NIN_MATRIX_KIND(rows, cols));
}

int nin_insert_square(nin *n)
{
  int free_items = 0, i, rc, it;
  for (i = n->item_free; i != NIN_NONE && free_items < 2; i = n->items[i].next) free_items++;
  if (free_items < 2) return NIN_ERR_FULL;
  rc = nin_insert_template(n, NIN_POW);
  if (rc < 0) return rc;
  it = n->rows[n->caret_row].owner;                       /* the caret is in a slot of the new template */
  n->caret_row = n->items[it].sub[1];
  n->caret_after = NIN_NONE;
  nin_insert_char(n, '2');                                /* cannot fail: one free item is left */
  n->caret_row = n->items[it].row;
  n->caret_after = (unsigned char)it;
  return 0;
}

/* The x^-1 (reciprocal) key: a NIN_POW template with the exponent -1 already typed, caret right AFTER the
 * template. The base is captured exactly as nin_insert_template(n, NIN_POW) does. Needs three free items
 * (the template item, '-' and '1'); on NIN_ERR_FULL nothing changed. (M2 task 5.) */
int nin_insert_recip(nin *n)
{
  int free_items = 0, i, rc, it;
  for (i = n->item_free; i != NIN_NONE && free_items < 3; i = n->items[i].next) free_items++;
  if (free_items < 3) return NIN_ERR_FULL;
  rc = nin_insert_template(n, NIN_POW);
  if (rc < 0) return rc;
  it = n->rows[n->caret_row].owner;                       /* the caret is in a slot of the new template */
  n->caret_row = n->items[it].sub[1];
  n->caret_after = NIN_NONE;
  nin_insert_char(n, '-');                                /* cannot fail: two free items are left */
  nin_insert_char(n, '1');
  n->caret_row = n->items[it].row;
  n->caret_after = (unsigned char)it;
  return 0;
}

static void remove_template(nin *n, int it)
{
  int r = n->items[it].sub[0];
  while (r != NIN_NONE) { int nx = n->rows[r].nxt; free_row(n, r); r = nx; }
  unlink_item(n, it);
  free_item(n, it);
  n->templates--;
}

/* A power with a filled base and an empty exponent ("x^" typed, nothing after): DEL takes the exponent slot away and gives
 * the base back to the row, so a mistaken power key loses nothing. */
static int pow_open(const nin *n, int it)
{
  return n->items[it].c == NIN_POW && row_empty(n, n->items[it].sub[1]) && !row_empty(n, n->items[it].sub[0]);
}
static void dissolve_pow(nin *n, int it)
{
  int base = n->items[it].sub[0], row = n->items[it].row, after = n->items[it].prev, cur, nx;
  for (cur = n->rows[base].first; cur != NIN_NONE; cur = nx) {
    nx = n->items[cur].next;
    unlink_item(n, cur);
    link_after(n, row, after, cur);
    after = cur;
  }
  n->caret_row = (unsigned char)row;
  n->caret_after = (unsigned char)after;
  remove_template(n, it);
}

int nin_backspace(nin *n)
{
  int it = n->caret_after;
  if (it != NIN_NONE) {
    if (is_tpl(n, it)) {
      /* PROVISIONAL: an empty template is removed; a filled one is entered (last slot end), nothing is destroyed */
      if (tpl_empty(n, it)) {
        n->caret_after = n->items[it].prev;
        remove_template(n, it);
        return 1;
      }
      if (pow_open(n, it)) { dissolve_pow(n, it); return 1; }
      n->caret_row = (unsigned char)slot_row(n, it, tpl_slots(n, it) - 1);
      n->caret_after = n->rows[n->caret_row].last;
      return 0;
    }
    n->caret_after = n->items[it].prev;
    unlink_item(n, it);
    free_item(n, it);
    return 1;
  }
  if (n->rows[n->caret_row].owner != NIN_NONE) {           /* at the start of a template slot */
    int tp = n->rows[n->caret_row].owner;
    if (tpl_empty(n, tp)) {
      n->caret_row = n->items[tp].row;
      n->caret_after = n->items[tp].prev;
      remove_template(n, tp);
      return 1;
    }
    if (n->rows[n->caret_row].slot == 1 && pow_open(n, tp)) { dissolve_pow(n, tp); return 1; }
    /* PROVISIONAL: at the start of the FIRST slot of a filled template the caret steps out to the left of the template
     * (nothing is deleted; the next DEL then acts on what precedes it). In a later slot nothing happens. */
    if (n->rows[n->caret_row].slot == 0) {
      n->caret_row = n->items[tp].row;
      n->caret_after = n->items[tp].prev;
    }
  }
  return 0;
}

/* ---- matrix editing (the keys come in M2 task 5) ---- */
/* the matrix that holds the caret: the innermost one around it, or the one right before it; NIN_NONE when there is none */
static int caret_matrix(const nin *n)
{
  int row = n->caret_row, owner;
  if (n->caret_after != NIN_NONE && is_tpl(n, n->caret_after) && is_mat(n->items[n->caret_after].c)) return n->caret_after;
  for (;;) {
    owner = n->rows[row].owner;
    if (owner == NIN_NONE) return NIN_NONE;
    if (is_mat(n->items[owner].c)) return owner;
    row = n->items[owner].row;
  }
}
/* index of the cell of matrix mi that holds the caret, or -1 when the caret is not inside it (behind it) */
static int caret_cell(const nin *n, int mi)
{
  int row = n->caret_row, owner;
  for (;;) {
    owner = n->rows[row].owner;
    if (owner == NIN_NONE) return -1;
    if (owner == mi) return n->rows[row].slot;
    row = n->items[owner].row;
  }
}
/* frees everything a row holds, nested templates and their rows included (the row itself is the caller's) */
static void discard_row(nin *n, int row)
{
  int it = n->rows[row].first;
  while (it != NIN_NONE) {
    int nx = n->items[it].next;
    if (is_tpl(n, it)) {
      int r = n->items[it].sub[0];
      while (r != NIN_NONE) { int rn = n->rows[r].nxt; discard_row(n, r); free_row(n, r); r = rn; }
      n->templates--;
    }
    free_item(n, it);
    it = nx;
  }
  n->rows[row].first = n->rows[row].last = NIN_NONE;
}

/* Rebuilds matrix mi as nr x nc: new cell (r, c) takes the old cell (rmap[r], cmap[c]), or is a new empty cell when either is
 * -1. Old cells that are not taken are discarded with their content. NIN_ERR_FULL (nothing changed) when the new cells do
 * not fit in the row pool (the rows of discarded cells are reused). */
static int mat_rebuild(nin *n, int mi, int nr, int nc, const signed char *rmap, const signed char *cmap)
{
  int kind = n->items[mi].c, rows = mat_rows(kind), cols = mat_cols(kind), r, c, k, need = 0, drop = 0, avail = 0, f;
  int ck = caret_cell(n, mi), lost = 0;
  unsigned char old[NIN_MAT_MAX * NIN_MAT_MAX], nw[NIN_MAT_MAX * NIN_MAT_MAX];
  char used[NIN_MAT_MAX * NIN_MAT_MAX];
  memset(used, 0, sizeof used);
  for (k = 0; k < rows * cols; k++) old[k] = (unsigned char)slot_row(n, mi, k);
  for (r = 0; r < nr; r++)
    for (c = 0; c < nc; c++) {
      if (rmap[r] < 0 || cmap[c] < 0) need++;
      else used[rmap[r] * cols + cmap[c]] = 1;
    }
  for (k = 0; k < rows * cols; k++) if (!used[k]) drop++;
  for (f = n->row_free; f != NIN_NONE; f = n->rows[f].first) avail++;
  if (need > avail + drop) return NIN_ERR_FULL;
  if (ck >= 0 && !used[ck]) lost = 1;
  for (k = 0; k < rows * cols; k++)
    if (!used[k]) { discard_row(n, old[k]); free_row(n, old[k]); }
  for (r = 0; r < nr; r++)
    for (c = 0; c < nc; c++)
      nw[r * nc + c] = (unsigned char)((rmap[r] < 0 || cmap[c] < 0) ? alloc_row(n, mi, r * nc + c) : old[rmap[r] * cols + cmap[c]]);
  for (k = 0; k < nr * nc; k++) {
    n->rows[nw[k]].owner = (unsigned char)mi;
    n->rows[nw[k]].slot = (unsigned char)k;
    n->rows[nw[k]].nxt = (unsigned char)(k + 1 < nr * nc ? nw[k + 1] : NIN_NONE);
  }
  n->items[mi].c = (unsigned char)NIN_MATRIX_KIND(nr, nc);
  n->items[mi].sub[0] = nw[0];
  n->items[mi].sub[1] = (unsigned char)(nr * nc > 1 ? nw[1] : NIN_NONE);
  if (lost) {                                    /* the caret's cell went: the nearest cell that is left, at its end */
    int r0 = ck / cols, c0 = ck % cols;
    r = r0 < nr ? r0 : nr - 1;
    c = c0 < nc ? c0 : nc - 1;
    n->caret_row = nw[r * nc + c];
    n->caret_after = n->rows[n->caret_row].last;
  }
  return 0;
}

int nin_matrix_dims(const nin *n, int *rows, int *cols)
{
  int mi = caret_matrix(n);
  if (mi == NIN_NONE) return 0;
  *rows = mat_rows(n->items[mi].c);
  *cols = mat_cols(n->items[mi].c);
  return 1;
}

/* op: 0 add row, 1 add column, 2 delete row, 3 delete column; the row / column is the caret's (the last one when the caret
 * is behind the matrix) */
static int mat_op(nin *n, int op)
{
  int mi = caret_matrix(n), kind, rows, cols, ck, rc, cc, r, c, nr, nc;
  signed char rmap[NIN_MAT_MAX], cmap[NIN_MAT_MAX];
  if (mi == NIN_NONE) return NIN_ERR_CHAR;
  kind = n->items[mi].c; rows = mat_rows(kind); cols = mat_cols(kind);
  ck = caret_cell(n, mi);
  rc = ck >= 0 ? ck / cols : rows - 1;
  cc = ck >= 0 ? ck % cols : cols - 1;
  nr = rows + (op == 0) - (op == 2);
  nc = cols + (op == 1) - (op == 3);
  if (nr < 1 || nr > NIN_MAT_MAX || nc < 1 || nc > NIN_MAT_MAX) return NIN_ERR_CHAR;
  for (r = 0; r < nr; r++) rmap[r] = (signed char)(op == 0 ? (r <= rc ? r : r == rc + 1 ? -1 : r - 1) : op == 2 ? (r < rc ? r : r + 1) : r);
  for (c = 0; c < nc; c++) cmap[c] = (signed char)(op == 1 ? (c <= cc ? c : c == cc + 1 ? -1 : c - 1) : op == 3 ? (c < cc ? c : c + 1) : c);
  return mat_rebuild(n, mi, nr, nc, rmap, cmap);
}
int nin_matrix_add_row(nin *n) { return mat_op(n, 0); }
int nin_matrix_add_col(nin *n) { return mat_op(n, 1); }
int nin_matrix_del_row(nin *n) { return mat_op(n, 2); }
int nin_matrix_del_col(nin *n) { return mat_op(n, 3); }

int nin_matrix_resize(nin *n, int rows, int cols)
{
  int mi = caret_matrix(n), kind, r, c;
  signed char rmap[NIN_MAT_MAX], cmap[NIN_MAT_MAX];
  if (mi == NIN_NONE || rows < 1 || rows > NIN_MAT_MAX || cols < 1 || cols > NIN_MAT_MAX) return NIN_ERR_CHAR;
  kind = n->items[mi].c;
  if (rows == mat_rows(kind) && cols == mat_cols(kind)) return 0;
  for (r = 0; r < rows; r++) rmap[r] = (signed char)(r < mat_rows(kind) ? r : -1);
  for (c = 0; c < cols; c++) cmap[c] = (signed char)(c < mat_cols(kind) ? c : -1);
  return mat_rebuild(n, mi, rows, cols, rmap, cmap);
}

/* Vertical level of a slot on screen (smaller = higher). A kind whose slots all lie on one level has no vertical
 * movement (UP/DOWN pass to an enclosing template). PROVISIONAL, like all movement. */
static int vlevel(int kind, int slot)
{
  switch (kind) {
    case NIN_FRAC: return slot;              /* numerator above the denominator */
    case NIN_POW: return 1 - slot;           /* the exponent above the base */
    case NIN_NROOT: return slot;             /* the index above the radicand */
    case NIN_LOGAB: return 1 - slot;         /* the argument above the (subscript) base */
    case NIN_INTEGRAL: return slot == 0 ? 1 : slot == 1 ? 2 : 0;   /* upper limit, integrand, lower limit */
    case NIN_SIGMA: return slot == 0 ? 1 : slot == 3 ? 0 : 2;      /* upper bound, summand, "var=lo" line */
    default: return 0;
  }
}

/* Slot that UP / DOWN jump to from slot cur of a template: the nearest slot one level up / down, cur itself when there is
 * none (the caret then goes to the end of its own slot), or -1 when the kind has a single level. */
static int vtarget(int kind, int cur, int dir)
{
  int k, ns = nslots(kind), lc, mn, mx, want;
  if (is_mat(kind)) {                            /* same column, one row up / down; at the edge the enclosing template decides */
    int cols = mat_cols(kind), r = cur / cols;
    if (dir == NIN_UP) return r > 0 ? cur - cols : -1;
    return r < mat_rows(kind) - 1 ? cur + cols : -1;
  }
  lc = mn = mx = vlevel(kind, cur);
  for (k = 0; k < ns; k++) {
    int l = vlevel(kind, k);
    if (l < mn) mn = l;
    if (l > mx) mx = l;
  }
  if (mn == mx) return -1;
  want = lc + (dir == NIN_UP ? -1 : 1);
  if (want < mn || want > mx) return cur;
  for (k = 0; k < ns; k++) if (vlevel(kind, k) == want) return k;
  return cur;
}

/* PROVISIONAL traversal (natural reading order, to be checked against native captures):
 * RIGHT enters a template at the start of its first slot, walks its slots in order (start of the next slot when one is
 * left) and leaves after the template; LEFT is the reverse (enters at the end of the last slot);
 * UP/DOWN jump to the end of the slot one level above / below (see vtarget); a template without levels passes them on to
 * the nearest enclosing template that has some. */
void nin_move(nin *n, int dir)
{
  int row = n->caret_row, after = n->caret_after, it, owner = n->rows[row].owner;
  if (dir == NIN_LEFT) {
    if (after != NIN_NONE) {
      if (is_tpl(n, after)) {
        n->caret_row = (unsigned char)slot_row(n, after, tpl_slots(n, after) - 1);
        n->caret_after = n->rows[n->caret_row].last;
      }
      else n->caret_after = n->items[after].prev;
    }
    else if (owner != NIN_NONE) {
      if (n->rows[row].slot > 0) { n->caret_row = (unsigned char)slot_row(n, owner, n->rows[row].slot - 1); n->caret_after = n->rows[n->caret_row].last; }
      else { n->caret_row = n->items[owner].row; n->caret_after = n->items[owner].prev; }
    }
  }
  else if (dir == NIN_RIGHT) {
    it = (after == NIN_NONE) ? n->rows[row].first : n->items[after].next;
    if (it != NIN_NONE) {
      if (is_tpl(n, it)) { n->caret_row = n->items[it].sub[0]; n->caret_after = NIN_NONE; }
      else n->caret_after = (unsigned char)it;
    }
    else if (owner != NIN_NONE) {
      if (n->rows[row].slot + 1 < tpl_slots(n, owner)) { n->caret_row = (unsigned char)slot_row(n, owner, n->rows[row].slot + 1); n->caret_after = NIN_NONE; }
      else { n->caret_row = n->items[owner].row; n->caret_after = (unsigned char)owner; }
    }
  }
  else if (dir == NIN_UP || dir == NIN_DOWN) {   /* any other value is ignored */
    int slot = n->rows[row].slot;
    /* the innermost enclosing template that has vertical levels takes the jump; slot is the slot it is being visited from */
    while (owner != NIN_NONE) {
      int s = vtarget(n->items[owner].c, slot, dir), target;
      if (s >= 0) {
        target = slot_row(n, owner, s);
        n->caret_row = (unsigned char)target;
        n->caret_after = n->rows[target].last;
        break;
      }
      row = n->items[owner].row;
      owner = n->rows[row].owner;
      slot = n->rows[row].slot;
    }
  }
}

/* ---- serialisation ---- */
typedef struct { char *buf; int size, len, err; } out_t;

static void put(out_t *o, char c)
{
  if (o->err) return;
  if (o->len + 1 >= o->size) { o->err = NIN_ERR_SIZE; return; }
  o->buf[o->len++] = c;
}
static int operand_end(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ')' || c == ']' || c == '.' || c == '_'; }
static int operand_start(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '(' || c == '[' || c == '.' || c == '_'; }

/* What nin_to_giac writes for a template, as a format: characters above 4 are literal, the bytes 1..4 stand for the text
 * of slot 0..3. The same table drives parsing (match_fmt), so writer and parser cannot drift apart. */
static const char *tpl_fmt(int kind)
{
  switch (kind) {
    case NIN_POW: return "(\001)^(\002)";
    case NIN_SQRT: return "sqrt(\001)";
    case NIN_ABS: return "abs(\001)";
    case NIN_NROOT: return "surd(\002,\001)";              /* the radicand is slot 1, written first */
    case NIN_LOGAB: return "logb(\002,\001)";              /* the argument is slot 1, written first */
    case NIN_DERIV: return "subst(diff(\001,x),x=\002)";
    case NIN_DERIV2: return "subst(diff(\001,x,2),x=\002)";
    case NIN_INTEGRAL: return "integrate(\001,x,\002,\003)";
    case NIN_SIGMA: return "sum(\001,\002,\003,\004)";
    default: return "((\001)/(\002))";
  }
}

static int fmt_has_comma(int kind)
{
  int hc = 0;
  const char *f;
  if (is_mat(kind)) return 1;                     /* a comma at depth 0 in a cell would add a cell */
  f = tpl_fmt(kind);
  while (*f) hc |= *f++ == ',';
  return hc;
}

/* Template slots (rows with an owner) must hold balanced typed parentheses, otherwise the text written around them would
 * change the meaning ("1)+(2" over 3 would read as 1+2/3). Nested templates write balanced text and are checked in their
 * own rows. In the slots of the comma-format kinds (surd, logb, diff, integrate, sum) and in matrix cells brackets must be
 * balanced too, parentheses and brackets counted apart, and no comma may stand at depth 0: otherwise two slots could
 * combine into another structure ("1]" and "[2" in two cells would read as [[1],[2]]). Other kinds do not check brackets.
 * The root row may hold anything the user typed. */
static void write_row(const nin *n, int row, out_t *o);
/* [[a,b],[c,d]] from the cells in row-major order */
static void write_matrix(const nin *n, int it, out_t *o)
{
  int kind = n->items[it].c, r, c, rows = mat_rows(kind), cols = mat_cols(kind);
  put(o, '[');
  for (r = 0; r < rows; r++) {
    put(o, '[');
    for (c = 0; c < cols; c++) {
      write_row(n, slot_row(n, it, r * cols + c), o);
      if (c < cols - 1) put(o, ',');
    }
    put(o, ']');
    if (r < rows - 1) put(o, ',');
  }
  put(o, ']');
}

static void write_row(const nin *n, int row, out_t *o)
{
  int it, last = 0;                      /* 0 nothing/operator, 'o' operand end, 'f' template */
  int depth = 0, slot = n->rows[row].owner != NIN_NONE;
  /* a slot of a comma-format template (surd, logb, diff, integrate, sum) must not hold a depth-0 comma: it would add an
   * argument, e.g. logb(8,2,7) is Giac's 3-argument logb (a silent different meaning). Same rule as match_fmt. */
  int cc = slot && fmt_has_comma(n->items[n->rows[row].owner].c), db = 0;     /* db: open brackets in the slot */
  for (it = n->rows[row].first; it != NIN_NONE && !o->err; it = n->items[it].next) {
    if (is_tpl(n, it)) {
      int kind = n->items[it].c, k;
      const char *f;
      for (k = 0; k < nslots(kind); k++)
        if (row_empty(n, slot_row(n, it, k))) { o->err = NIN_ERR_EMPTY_SLOT; return; }
      if (last == 'o' || last == 'f') put(o, '*');
      if (is_mat(kind)) write_matrix(n, it, o);
      else for (f = tpl_fmt(kind); *f; f++) {
        if ((unsigned char)*f > 4) put(o, *f);
        else write_row(n, slot_row(n, it, *f - 1), o);
      }
      last = 'f';
    }
    else {
      char c = (char)n->items[it].c;
      if (last == 'f' && operand_start(c)) put(o, '*');
      put(o, c);
      last = operand_end(c) ? 'o' : 0;
      if (slot) {
        if (c == '(') depth++;
        else if (c == ')' && --depth < 0 && !o->err) { o->err = NIN_ERR_SYNTAX; return; }
      }
      if (cc && !o->err) {                            /* depth is the parenthesis count kept above (it never goes below 0 here) */
        if (c == '[') db++;
        else if (c == ']' && --db < 0) { o->err = NIN_ERR_SYNTAX; return; }
        else if (c == ',' && depth == 0 && db == 0) { o->err = NIN_ERR_SYNTAX; return; }
      }
    }
  }
  if (slot && (depth != 0 || db != 0) && !o->err) o->err = NIN_ERR_SYNTAX;
}

int nin_to_giac(const nin *n, char *buf, int size)
{
  out_t o;
  o.buf = buf; o.size = size; o.len = 0; o.err = 0;
  if (size <= 0) return NIN_ERR_SIZE;
  write_row(n, 0, &o);
  if (o.err == NIN_ERR_SIZE) o.len = size - 1;
  buf[o.len] = 0;
  return o.err ? o.err : o.len;
}

/* ---- parsing ---- */
/* s[i..len) against a template format: literals must match exactly, a slot ends at the first occurrence of the literal
 * that follows it at nesting depth 0 (parentheses count; in a format with commas brackets count too, so "[1,2]" is one
 * argument, and a slot may not hold a depth-0 comma). Bounds of slot k (start inclusive, end exclusive) go to b[2k], b[2k+1]. Returns the index of the last
 * character of the match, or -1. */
static int match_fmt(const char *s, int i, int len, const char *fmt, int b[8])
{
  int p = i, hc = 0;                          /* hc: a format with commas, its arguments are split at depth-0 commas */
  const char *f = fmt;
  while (*f) hc |= *f++ == ',';               /* (no strchr: the demo link has only libfxcg's memcmp/memset, libc's string.o clashes) */
  f = fmt;
  while (*f) {
    int k, ll = 0, dp = 0, db = 0;                            /* open parentheses / brackets in the slot so far */
    const char *lit;
    if ((unsigned char)*f > 4) {
      if (p >= len || s[p] != *f) return -1;
      p++; f++;
      continue;
    }
    k = *f - 1;
    f++;
    lit = f;
    while ((unsigned char)lit[ll] > 4) ll++;                 /* every slot is followed by a literal in the formats */
    b[2 * k] = p;
    for (;; p++) {
      char c;
      if (p >= len) return -1;
      if (dp == 0 && db == 0 && p + ll <= len && memcmp(s + p, lit, (size_t)ll) == 0) break;
      c = s[p];
      if (hc && dp == 0 && db == 0 && c == ',') return -1;   /* one comma too many: not something nin_to_giac writes */
      /* the slot must be balanced (nin_to_giac refuses an unbalanced one); brackets only count in a format with commas,
       * where they keep a "[1,2]" together. Parentheses and brackets are counted apart: a ']' never closes a '('. */
      if (c == '(') dp++;
      else if (c == ')') { if (--dp < 0) return -1; }
      else if (hc && c == '[') db++;
      else if (hc && c == ']') { if (--db < 0) return -1; }
    }
    b[2 * k + 1] = p;
  }
  return p - 1;
}

/* A matrix "[[a,b],[c,d]]" at s[i] (i < len): every row "[" cells separated by "," "]", rows separated by ",", all rows with the
 * same number (1..NIN_MAT_MAX) of non-empty cells, at most NIN_MAT_MAX rows; a cell ends at the first depth-0 ',' or ']'
 * (parentheses and brackets count). Returns the index of the last ']', or -1; *rows and *cols get the size. With want >= 0
 * the bounds [*c0, *c1) of cell number want (row-major) are returned as well. */
static int match_matrix(const char *s, int i, int len, int *rows, int *cols, int want, int *c0, int *c1)
{
  int p = i + 1, r = 0, cc = -1, idx = 0;
  if (i + 1 >= len || s[i] != '[' || s[i + 1] != '[') return -1;
  for (;;) {                                      /* p is at the '[' that opens a row */
    int c = 0;
    if (p >= len || s[p] != '[') return -1;
    p++;
    for (;;) {                                    /* one cell */
      int start = p, dp = 0, db = 0;
      for (;; p++) {
        char ch;
        if (p >= len) return -1;
        ch = s[p];
        if (dp == 0 && db == 0 && (ch == ',' || ch == ']')) break;
        if (ch == '(') dp++;
        else if (ch == ')') { if (--dp < 0) return -1; }
        else if (ch == '[') db++;
        else if (ch == ']') { if (--db < 0) return -1; }
      }
      if (p == start || ++c > NIN_MAT_MAX) return -1;             /* empty cell, or too many columns */
      if (idx == want) { *c0 = start; *c1 = p; }
      idx++;
      if (s[p] == ']') break;
      p++;                                        /* past the ',' */
    }
    p++;                                          /* past the row's ']' */
    if (++r > NIN_MAT_MAX) return -1;
    if (cc < 0) cc = c; else if (cc != c) return -1;
    if (p >= len) return -1;
    if (s[p] == ']') break;
    if (s[p] != ',') return -1;
    p++;
  }
  *rows = r; *cols = cc;
  return p;
}

/* A template candidate at s[i] (i < to): its kind, the end index e of its last character, and the bounds of its slots in
 * b[2k..2k+1]. Accepted only if nin_to_giac could have written exactly these characters here: every slot non-empty, and
 * no operand glued to either side (nin_to_giac would have put a '*' there). Anything else, e.g. sin((1)/(2)), stays plain
 * text and is never altered. Returns e, or -1. */
static int find_template(const char *s, int i, int from, int to, int *kind, int b[8])
{
  int kd;
  for (kd = NIN_FRAC; kd <= K_LAST; kd++) {
    int e = match_fmt(s, i, to, tpl_fmt(kd), b), k, ok = e >= 0;
    for (k = 0; ok && k < nslots(kd); k++) if (b[2 * k] == b[2 * k + 1]) ok = 0;
    if (ok && ((i > from && operand_end(s[i - 1])) || (e + 1 < to && operand_start(s[e + 1])))) ok = 0;
    if (ok) { *kind = kd; return e; }
  }
  if (s[i] == '[') {                              /* a matrix: no slot bounds here, parse_row rescans for its cells */
    int r, c, e = match_matrix(s, i, to, &r, &c, -1, &b[0], &b[1]);
    if (e >= 0 && !((i > from && operand_end(s[i - 1])) || (e + 1 < to && operand_start(s[e + 1])))) { *kind = NIN_MATRIX_KIND(r, c); return e; }
  }
  return -1;
}

/* Returns 0, or NIN_ERR_FULL. *lossy is set when a byte outside 0x20..0x7E was replaced. */
static int parse_row(nin *n, int row, const char *s, int from, int to, int *lossy)
{
  int i = from;
  while (i < to) {
    int b[8], kind, e, it;
    e = find_template(s, i, from, to, &kind, b);
    if (e >= 0) {
      int k;
      it = alloc_template(n, kind);
      if (it < 0) return NIN_ERR_FULL;
      link_after(n, row, n->rows[row].last, it);
      if (is_mat(kind)) {
        for (k = 0; k < nslots(kind); k++) {
          int r, c, c0 = 0, c1 = 0;
          match_matrix(s, i, to, &r, &c, k, &c0, &c1);
          if (parse_row(n, slot_row(n, it, k), s, c0, c1, lossy) < 0) return NIN_ERR_FULL;
        }
      }
      else for (k = 0; k < nslots(kind); k++)
        if (parse_row(n, slot_row(n, it, k), s, b[2 * k], b[2 * k + 1], lossy) < 0) return NIN_ERR_FULL;
      i = e + 1;
    }
    else {
      it = alloc_item(n);
      if (it < 0) return NIN_ERR_FULL;
      if (s[i] < 0x20 || s[i] > 0x7E) *lossy = 1;
      n->items[it].c = (unsigned char)((s[i] >= 0x20 && s[i] <= 0x7E) ? s[i] : '?');
      n->items[it].sub[0] = n->items[it].sub[1] = NIN_NONE;
      link_after(n, row, n->rows[row].last, it);
      i++;
    }
  }
  return 0;
}

int nin_from_string(nin *n, const char *s)
{
  int lossy = 0;
  nin_init(n);
  if (parse_row(n, 0, s, 0, (int)strlen(s), &lossy) < 0) { nin_init(n); return NIN_ERR_FULL; }
  n->caret_row = 0;
  n->caret_after = n->rows[0].last;
  return lossy ? NIN_WARN_LOSSY : 0;
}

/* ---- layout and drawing ---- */
static int imax(int a, int b) { return a > b ? a : b; }

/* Compact sizes for the recursive frames: a slot size and, for a template, where each slot goes. */
typedef struct { short w, a, b; } ssz;
typedef struct { short dx[4], dy[4]; ssz z; } tplay;   /* slot k: left edge x + dx[k], axis y + dy[k]; z: the whole template */

/* How far the exponent axis lies above the base axis: half a text row, or more when the base is taller so that the
 * exponent top meets the base top (PROVISIONAL: same font size as the base, no native measurement yet). */
static int pow_raise(const nin_metrics *m, const ssz *base, const ssz *ex) { return imax(m->char_h / 2, base->a - ex->a); }

static int thick(const nin_metrics *m) { return m->bar_h > 0 ? m->bar_h : 1; }
/* Text that a template draws itself (never part of the model): width of a string in the current font. */
static int lit_w(const nin_metrics *m, const char *s)
{
  int w = 0;
  while (*s) w += m->char_w(m->ctx, *s++);
  return w;
}
/* Width of the radical sign (hook and rise), a third of a text row, at least 4 px. */
static int rad_w(const nin_metrics *m) { return imax(m->char_h / 3, 4); }

/* Width of the integral sign, a third of a text row, at least 5 px; hook length a quarter of a row, at least 2 px. */
static int int_w(const nin_metrics *m) { return imax(m->char_h / 3, 5); }
static int int_hook(const nin_metrics *m) { return imax(m->char_h / 4, 2); }

/* Width of the sigma sign: two thirds of a text row, at least 8 px. */
static int sig_w(const nin_metrics *m) { return imax(m->char_h * 2 / 3, 8); }

static const char *deriv_label(int kind) { return kind == NIN_DERIV2 ? "d2/dx2(" : "d/dx("; }

/* Size of a template and the position of its slots from the sizes s[] of the slots (leaf: no recursion, so the recursive
 * frames stay small). */
static void tpl_layout(const nin_metrics *m, int kind, const ssz *s, tplay *o)
{
  int t = thick(m);
  memset(o, 0, sizeof *o);
  switch (kind) {
    case NIN_POW: {
      int raise = pow_raise(m, &s[0], &s[1]);
      o->dx[1] = s[0].w; o->dy[1] = (short)-raise;
      o->z.w = (short)(s[0].w + s[1].w);
      o->z.a = (short)imax(s[0].a, raise + s[1].a);
      o->z.b = (short)imax(s[0].b, s[1].b - raise);
      break;
    }
    case NIN_SQRT:                            /* sign, radicand, then a right overhang of the over-bar */
      o->dx[0] = (short)rad_w(m);
      o->z.w = (short)(rad_w(m) + s[0].w + m->bar_pad);
      o->z.a = (short)(s[0].a + m->gap + t);
      o->z.b = s[0].b;
      break;
    case NIN_NROOT: {                         /* index, radical sign, radicand; the index bottom meets the sign's hook */
      int a0 = s[1].a + m->gap + t, h0 = a0 + s[1].b, up = a0 - h0 / 2;
      o->dx[0] = 0; o->dy[0] = (short)-(up + s[0].b);
      o->dx[1] = (short)(s[0].w + rad_w(m));
      o->z.w = (short)(s[0].w + rad_w(m) + s[1].w + m->bar_pad);
      o->z.a = (short)imax(a0, up + s[0].a + s[0].b);
      o->z.b = s[1].b;
      break;
    }
    case NIN_LOGAB: {                         /* "log", the base lowered by half a text row, "(", the argument, ")" */
      int lw = lit_w(m, "log"), drop = m->char_h / 2, ha = m->char_h / 2;
      o->dx[0] = (short)lw; o->dy[0] = (short)drop;
      o->dx[1] = (short)(lw + s[0].w + lit_w(m, "("));
      o->z.w = (short)(o->dx[1] + s[1].w + lit_w(m, ")"));
      o->z.a = (short)imax(imax(ha, s[1].a), s[0].a - drop);
      o->z.b = (short)imax(imax(m->char_h - ha, s[1].b), drop + s[0].b);
      break;
    }
    case NIN_DERIV: case NIN_DERIV2: {        /* label, f, ")|x=", x0 on one line (PROVISIONAL: native stacks d over dx) */
      int l1 = lit_w(m, deriv_label(kind)), ha = m->char_h / 2;
      o->dx[0] = (short)l1;
      o->dx[1] = (short)(l1 + s[0].w + lit_w(m, ")|x="));
      o->z.w = (short)(o->dx[1] + s[1].w);
      o->z.a = (short)imax(ha, imax(s[0].a, s[1].a));
      o->z.b = (short)imax(m->char_h - ha, imax(s[0].b, s[1].b));
      break;
    }
    case NIN_INTEGRAL: {                      /* sign, then the upper limit above / the integrand / the lower limit below, then "dx" */
      int sw = int_w(m), cw = imax(imax(s[0].w + lit_w(m, "dx"), s[1].w), s[2].w);
      int fa = imax(s[0].a, m->char_h / 2), fb = imax(s[0].b, m->char_h - m->char_h / 2);   /* the "dx" label makes the row a text row */
      o->dx[0] = o->dx[1] = o->dx[2] = (short)sw;
      o->dy[1] = (short)(fb + s[1].a);
      o->dy[2] = (short)-(fa + s[2].b);
      o->z.w = (short)(sw + cw);
      o->z.a = (short)(fa + s[2].a + s[2].b);
      o->z.b = (short)(fb + s[1].a + s[1].b);
      break;
    }
    case NIN_SIGMA: {                         /* sign in a column with the upper bound above and "var=lo" below; the summand on the right */
      int ha = m->char_h / 2, sa = imax(s[0].a, ha), sb = imax(s[0].b, m->char_h - ha), eqw = lit_w(m, "=");
      int loww = s[1].w + eqw + s[2].w, colw = imax(imax(sig_w(m), s[3].w), loww);
      int lra = imax(imax(s[1].a, s[2].a), ha), lrb = imax(imax(s[1].b, s[2].b), m->char_h - ha);
      o->dx[0] = (short)colw;
      o->dx[1] = (short)((colw - loww) / 2);
      o->dx[2] = (short)(o->dx[1] + s[1].w + eqw);
      o->dx[3] = (short)((colw - s[3].w) / 2);
      o->dy[1] = o->dy[2] = (short)(sb + lra);
      o->dy[3] = (short)-(sa + s[3].b);
      o->z.w = (short)(colw + s[0].w);
      o->z.a = (short)(sa + s[3].a + s[3].b);
      o->z.b = (short)(sb + lra + lrb);
      break;
    }
    case NIN_ABS:                             /* a bar, a gap, the argument, a gap, a bar */
      o->dx[0] = (short)(t + m->gap);
      o->z.w = (short)(s[0].w + 2 * (t + m->gap));
      o->z.a = s[0].a;
      o->z.b = s[0].b;
      break;
    default: {                                /* NIN_FRAC */
      int up = m->bar_h / 2, dn = m->bar_h - up;
      o->z.w = (short)(imax(s[0].w, s[1].w) + 2 * m->bar_pad);
      o->z.a = (short)(up + m->gap + s[0].a + s[0].b);
      o->z.b = (short)(dn + m->gap + s[1].a + s[1].b);
      o->dx[0] = (short)((o->z.w - s[0].w) / 2); o->dy[0] = (short)-(up + m->gap + s[0].b);
      o->dx[1] = (short)((o->z.w - s[1].w) / 2); o->dy[1] = (short)(dn + m->gap + s[1].a);
      break;
    }
  }
}

/* The radical sign from filled rectangles only (no fonts): a tick, a short down stroke, a flat joint, the long vertical
 * rise, and the over-bar across the radicand. The sign spans [axis - a, axis + b) and is w wide from x. */
static void draw_radical(const nin_metrics *m, int x, int axis, int a, int b, int w)
{
  int t = thick(m), rw = rad_w(m), y0 = axis - a, y1 = axis + b, ytick = y0 + (a + b) / 2;
  gfx_fill_rect(x + rw - t, y0, w - rw + t, t, GFX_BLACK);           /* over-bar */
  gfx_fill_rect(x + rw - t, y0, t, y1 - y0, GFX_BLACK);              /* rise */
  gfx_fill_rect(x, ytick, rw / 2 + t, t, GFX_BLACK);                 /* tick */
  gfx_fill_rect(x + rw / 2, ytick, t, y1 - ytick, GFX_BLACK);        /* short down stroke */
  gfx_fill_rect(x + rw / 2, y1 - t, rw - rw / 2, t, GFX_BLACK);      /* joint */
}

static void draw_char(const nin_metrics *m, int x, int axis, char c, int w);
/* Text of a template, drawn like typed characters; returns its width. */
static int draw_lit(const nin_metrics *m, int x, int axis, const char *s)
{
  int x0 = x;
  for (; *s; s++) {
    int w = m->char_w(m->ctx, *s);
    draw_char(m, x, axis, *s, w);
    x += w;
  }
  return x - x0;
}

/* The integral sign from filled rectangles only: a stem with a short hook to the right at the top and to the left at the
 * bottom. It spans [axis - a, axis + b) and is int_w wide from x. */
static void draw_integral_sign(const nin_metrics *m, int x, int axis, int a, int b)
{
  int t = thick(m), sw = int_w(m), hk = int_hook(m), xs = x + (sw - t) / 2, y0 = axis - a, y1 = axis + b;
  gfx_fill_rect(xs, y0, t, y1 - y0, GFX_BLACK);                     /* stem */
  gfx_fill_rect(xs, y0, x + sw - xs, t, GFX_BLACK);                 /* top hook */
  gfx_fill_rect(x + sw - t, y0, t, hk, GFX_BLACK);
  gfx_fill_rect(x, y1 - t, xs - x + t, t, GFX_BLACK);               /* bottom hook */
  gfx_fill_rect(x, y1 - hk, t, hk, GFX_BLACK);
}

/* The sigma sign from filled rectangles only: a top and a bottom bar and two strokes meeting at a point on the right
 * (a ">" shape, stroke width thick). It spans [axis - a, axis + b) and is w wide from x. PROVISIONAL shape. */
static void draw_sigma_sign(const nin_metrics *m, int x, int axis, int a, int b, int w)
{
  int t = thick(m), y0 = axis - a, h = a + b, mid = h / 2, half = mid - t, span = w * 2 / 3, r;
  gfx_fill_rect(x, y0, w, t, GFX_BLACK);
  gfx_fill_rect(x, y0 + h - t, w, t, GFX_BLACK);
  if (half < 1) half = 1;
  for (r = t; r < h - t; r++) {
    int d = r < mid ? r - t : h - 1 - t - r;
    if (d < 0) continue;
    gfx_fill_rect(x + d * span / half, y0 + r, t, 1, GFX_BLACK);
  }
}

/* What a template draws besides its slots (leaf). Goes after the slots: the OS text call paints a background box that may
 * be taller than the glyph and would hide bars and signs. */
static void tpl_decor(const nin_metrics *m, int kind, int x, int axis, const ssz *s, const tplay *l)
{
  int t = thick(m);
  switch (kind) {
    case NIN_SQRT: draw_radical(m, x, axis, l->z.a, l->z.b, l->z.w); break;
    case NIN_SIGMA: {
      int ha = m->char_h / 2, sa = imax(s[0].a, ha), sb = imax(s[0].b, m->char_h - ha), sw = sig_w(m);
      draw_sigma_sign(m, x + (l->dx[0] - sw) / 2, axis, sa, sb, sw);
      draw_lit(m, x + l->dx[1] + s[1].w, axis + l->dy[1], "=");
      break;
    }
    case NIN_INTEGRAL:
      draw_integral_sign(m, x, axis, l->z.a, l->z.b);
      draw_lit(m, x + l->dx[0] + s[0].w, axis, "dx");
      break;
    case NIN_DERIV: case NIN_DERIV2:
      draw_lit(m, x, axis, deriv_label(kind));
      draw_lit(m, x + l->dx[1] - lit_w(m, ")|x="), axis, ")|x=");
      break;
    case NIN_LOGAB:
      draw_lit(m, x, axis, "log");
      draw_lit(m, x + l->dx[1] - lit_w(m, "("), axis, "(");
      draw_lit(m, x + l->z.w - lit_w(m, ")"), axis, ")");
      break;
    case NIN_NROOT: draw_radical(m, x + s[0].w, axis, s[1].a + m->gap + t, s[1].b, l->z.w - s[0].w); break;
    case NIN_ABS:
      gfx_fill_rect(x, axis - l->z.a, t, l->z.a + l->z.b, GFX_BLACK);
      gfx_fill_rect(x + l->z.w - t, axis - l->z.a, t, l->z.a + l->z.b, GFX_BLACK);
      break;
    case NIN_FRAC: gfx_fill_rect(x, axis - m->bar_h / 2, l->z.w, m->bar_h, GFX_BLACK); break;
    default: break;                            /* NIN_POW: nothing */
  }
}

/* ---- matrix grid: column widths and row heights come from the tallest / widest cell (PROVISIONAL sizes) ---- */
typedef struct { short cw[NIN_MAT_MAX], ra[NIN_MAT_MAX], rb[NIN_MAT_MAX]; } matgrid;
static void measure_row(const nin *n, const nin_metrics *m, int row, ssz *out);

static void mat_grid(const nin *n, const nin_metrics *m, int it, matgrid *g)
{
  int kind = n->items[it].c, cols = mat_cols(kind), k, ns = nslots(kind);
  ssz z;
  memset(g, 0, sizeof *g);
  for (k = 0; k < ns; k++) {
    int c = k % cols, r = k / cols;
    measure_row(n, m, slot_row(n, it, k), &z);
    if (z.w > g->cw[c]) g->cw[c] = z.w;
    if (z.a > g->ra[r]) g->ra[r] = z.a;
    if (z.b > g->rb[r]) g->rb[r] = z.b;
  }
}
/* brackets: a bar of thick(m) with 2 px serifs (a sixth of a text row at most), plus a gap to the cells */
static int mat_hook(const nin_metrics *m) { return imax(2, m->char_h / 6); }
static int mat_bw(const nin_metrics *m) { return imax(thick(m), mat_hook(m)) + m->gap; }
static void mat_size(const nin_metrics *m, int kind, const matgrid *g, ssz *o)
{
  int r, c, rows = mat_rows(kind), cols = mat_cols(kind), w = 2 * mat_bw(m) + (cols - 1) * 2 * m->gap, h = (rows - 1) * m->gap;
  for (c = 0; c < cols; c++) w += g->cw[c];
  for (r = 0; r < rows; r++) h += g->ra[r] + g->rb[r];
  o->w = (short)w; o->a = (short)(h / 2); o->b = (short)(h - h / 2);
}

/* Sizes go through ssz (shorts) inside the recursion: it keeps the frames of nested templates small. */
static void measure_row(const nin *n, const nin_metrics *m, int row, ssz *out)
{
  int it, sw = 0, sa = 0, sb = 0;
  if (row_empty(n, row)) {
    out->w = (short)m->slot_w; out->a = (short)(m->slot_h / 2); out->b = (short)(m->slot_h - m->slot_h / 2);
    return;
  }
  for (it = n->rows[row].first; it != NIN_NONE; it = n->items[it].next) {
    int tw, ta, tb;
    if (is_tpl(n, it) && is_mat(n->items[it].c)) {
      matgrid g;
      ssz z;
      mat_grid(n, m, it, &g);
      mat_size(m, n->items[it].c, &g, &z);
      tw = z.w; ta = z.a; tb = z.b;
    }
    else if (is_tpl(n, it)) {
      ssz sl[4];
      tplay lay;
      int k, ns = tpl_slots(n, it);
      memset(sl, 0, sizeof sl);
      for (k = 0; k < ns; k++) measure_row(n, m, slot_row(n, it, k), &sl[k]);
      tpl_layout(m, n->items[it].c, sl, &lay);
      tw = lay.z.w; ta = lay.z.a; tb = lay.z.b;
    }
    else {
      tw = m->char_w(m->ctx, (char)n->items[it].c);
      ta = m->char_h / 2;
      tb = m->char_h - ta;
    }
    sw += tw; sa = imax(sa, ta); sb = imax(sb, tb);
  }
  out->w = (short)sw; out->a = (short)sa; out->b = (short)sb;
}

void nin_measure(const nin *n, const nin_metrics *m, nin_size *out)
{
  ssz z;
  measure_row(n, m, 0, &z);
  out->w = z.w; out->a = z.a; out->b = z.b;
}

static void set_caret(nin_caret_pos *c, int x, int y, int h) { c->valid = 1; c->x = x; c->y = y; c->h = h; }

static void draw_char(const nin_metrics *m, int x, int axis, char c, int w)
{
  char s[2];
  int top = axis - m->char_h / 2;
  s[0] = c; s[1] = 0;
  /* the OS text call is not trusted to clip: skip characters wholly outside the clip rectangle */
  if (x + w > m->clip_x0 && x < m->clip_x1 && top + m->char_h > m->clip_y0 && top < m->clip_y1) {
    if (m->large) gfx_text_large(x, top, s, GFX_BLACK, GFX_WHITE);
    else gfx_text(x, top, s, GFX_BLACK, GFX_WHITE);
  }
}

static void draw_row(const nin *n, const nin_metrics *m, int row, int x, int axis, nin_caret_pos *caret);

/* Draws a matrix and returns its width. The brackets go after the cells, like every bar. */
static int draw_matrix(const nin *n, const nin_metrics *m, int it, int x, int axis, nin_caret_pos *caret)
{
  matgrid g;
  ssz z;
  int kind = n->items[it].c, rows = mat_rows(kind), cols = mat_cols(kind), r, c, cx, ry, bw = mat_bw(m), t = thick(m), hk = mat_hook(m), y0, h;
  mat_grid(n, m, it, &g);
  mat_size(m, kind, &g, &z);
  y0 = axis - z.a; h = z.a + z.b;
  ry = y0;
  for (r = 0; r < rows; r++) {
    cx = x + bw;
    for (c = 0; c < cols; c++) {
      ssz cs;
      int cell = slot_row(n, it, r * cols + c);
      measure_row(n, m, cell, &cs);
      draw_row(n, m, cell, cx + (g.cw[c] - cs.w) / 2, ry + g.ra[r], caret);
      cx += g.cw[c] + 2 * m->gap;
    }
    ry += g.ra[r] + g.rb[r] + m->gap;
  }
  gfx_fill_rect(x, y0, t, h, GFX_BLACK);                              /* left bracket */
  gfx_fill_rect(x, y0, hk, t, GFX_BLACK);
  gfx_fill_rect(x, y0 + h - t, hk, t, GFX_BLACK);
  gfx_fill_rect(x + z.w - t, y0, t, h, GFX_BLACK);                    /* right bracket */
  gfx_fill_rect(x + z.w - hk, y0, hk, t, GFX_BLACK);
  gfx_fill_rect(x + z.w - hk, y0 + h - t, hk, t, GFX_BLACK);
  return z.w;
}

static void draw_row(const nin *n, const nin_metrics *m, int row, int x, int axis, nin_caret_pos *caret)
{
  int it;
  if (row_empty(n, row)) {
    int top = axis - m->slot_h / 2, r = x + m->slot_w - 1, bot = top + m->slot_h - 1;
    gfx_fill_rect(x, top, m->slot_w, 1, GFX_BLACK);
    gfx_fill_rect(x, bot, m->slot_w, 1, GFX_BLACK);
    gfx_fill_rect(x, top, 1, m->slot_h, GFX_BLACK);
    gfx_fill_rect(r, top, 1, m->slot_h, GFX_BLACK);
    /* inset inside the box, 1 px clear of its border, so it cannot be mistaken for the border */
    if (row == n->caret_row) set_caret(caret, x + 2, top + 2, m->slot_h - 4);
    return;
  }
  if (row == n->caret_row && n->caret_after == NIN_NONE) set_caret(caret, x, axis - m->char_h / 2, m->char_h);
  for (it = n->rows[row].first; it != NIN_NONE; it = n->items[it].next) {
    int w;
    if (is_tpl(n, it) && is_mat(n->items[it].c)) w = draw_matrix(n, m, it, x, axis, caret);
    else if (is_tpl(n, it)) {
      ssz sl[4];
      tplay lay;
      int k, ns = tpl_slots(n, it), kind = n->items[it].c;
      memset(sl, 0, sizeof sl);
      for (k = 0; k < ns; k++) measure_row(n, m, slot_row(n, it, k), &sl[k]);
      tpl_layout(m, kind, sl, &lay);
      w = lay.z.w;
      for (k = 0; k < ns; k++) draw_row(n, m, slot_row(n, it, k), x + lay.dx[k], axis + lay.dy[k], caret);
      tpl_decor(m, kind, x, axis, sl, &lay);
    }
    else {
      w = m->char_w(m->ctx, (char)n->items[it].c);
      draw_char(m, x, axis, (char)n->items[it].c, w);
    }
    x += w;
    if (row == n->caret_row && n->caret_after == it) set_caret(caret, x, axis - m->char_h / 2, m->char_h);
  }
}

void nin_draw(const nin *n, const nin_metrics *m, int x, int axis_y, nin_caret_pos *caret)
{
  nin_caret_pos local;
  if (!caret) caret = &local;
  caret->valid = 0;
  draw_row(n, m, 0, x, axis_y, caret);
  if (caret->valid) gfx_fill_rect(caret->x, caret->y, 3, caret->h, GFX_BLACK);
}
