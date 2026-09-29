/* Pure decisions for stacked results in the shell; see zstack_rules.h. C99, no OS or Giac. */
#include <string.h>
#include "zstack_rules.h"

static int is_op(unsigned char c) { return c == '+' || c == '-' || c == '*' || c == '/' || c == '^'; }
static int is_ident(unsigned char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int zstack_wanted(const char *s)
{
  int len, depth = 0, ops = 0, want = 0;
  unsigned char prev = 0;   /* previous character, 0 at the start */
  if (!s) return 0;
  for (len = 0; s[len]; prev = (unsigned char)s[len], ++len) {
    unsigned char c = (unsigned char)s[len];
    /* operator shape: no binary operator first or right after '(' (a leading '-' is unary), no operator before
     * ')', no two operators in a row except exactly "**" (so "ab/", "1/2/", "1//2", "(*2)" never reach the parser) */
    if (is_op(c)) {
      if (c != '-' && (prev == 0 || prev == '(')) return 0;
      if (is_op(prev) && !(c == '*' && prev == '*' && (len < 2 || s[len - 2] != '*'))) return 0;
    }
    if (c == ')' && is_op(prev)) return 0;
    if (len >= ZSTACK_MAX_LEN) return 0;
    if (c <= 0x20 || c >= 0x7F) return 0;   /* space, control characters, OS wide characters */
    switch (c) {
    case '"': case '\'': case '`': case ':': case ';': case ',': case '=': case '<': case '>': case '!':
    case '[': case ']': case '{': case '}': case '#': case '$': case '%': case '&': case '|': case '?':
    case '@': case '\\': case '~':
      return 0;   /* strings, lists/matrices, sequences, assignments, tests, messages */
    case '(':
      if (++depth > ZSTACK_MAX_PAREN) return 0;
      break;
    case ')':
      if (--depth < 0) return 0;
      break;
    case '+': case '-': case '*': case '/': case '^':
      if (++ops > ZSTACK_MAX_OPS) return 0;
      if (c == '/' || c == '^' || (c == '*' && s[len + 1] == '*')) want = 1;
      break;
    case 's':   /* sqrt( as a function name, not the end of an identifier such as mysqrt( */
      if (strncmp(s + len, "sqrt(", 5) == 0 && !is_ident(prev)) want = 1;
      break;
    default:
      break;
    }
  }
  if (is_op(prev)) return 0;   /* trailing operator */
  return want && depth == 0 && len >= 3;
}

static int has_marker(const char *s, const char *m)
{
  int i, j;
  for (i = 0; s[i]; ++i) {
    for (j = 0; m[j] && s[i + j] == m[j]; ++j) ;
    if (!m[j]) return 1;
  }
  return 0;
}

int zstack_input_wanted(const char *s)
{
  static const char *const markers[] = { ")/(", "^(", "sqrt(", "abs(", "surd(", "logb(", "subst(diff(", "integrate(", "sum(", "[[" };
  int i, len;
  if (!s) return 0;
  for (len = 0; s[len]; ++len) {
    unsigned char c = (unsigned char)s[len];
    if (len >= ZSTACK_INPUT_MAX_LEN) return 0;
    if (c < 0x20 || c > 0x7E || c == '"' || c == '\'' || c == '#') return 0;
  }
  for (i = 0; i < (int)(sizeof markers / sizeof *markers); ++i)
    if (has_marker(s, markers[i])) return 1;
  return 0;
}

int zstack_input_block(int a, int b, int w, int minh, int maxh, int maxw, int *h, int *axis)
{
  int bh;
  if (a < 0 || b < 0 || w < 0 || w > maxw) return 0;
  bh = a + b + 2 * ZSTACK_INPUT_PAD;
  if (bh > maxh) return 0;
  if (bh < minh) bh = minh;
  if (h) *h = bh;
  if (axis) *axis = (bh - (a + b)) / 2 + a;
  return 1;
}

int zstack_layout(const int *h, int n, int cursor, int avail, int *y)
{
  int a = 0, b = n - 1, sum = 0, top = 0, i;
  if (n <= 0) return 0;
  if (cursor < 0) cursor = 0;
  if (cursor > n - 1) cursor = n - 1;
  for (i = 0; i < n; ++i) sum += h[i];
  if (sum > avail) {
    while (sum > avail && a < cursor) sum -= h[a++];   /* drop the oldest levels first */
    if (sum <= avail) top = avail - sum;               /* bottom-anchored: the newest level ends at the bottom */
    else
      while (sum > avail && b > cursor) sum -= h[b--];  /* cursor high up: drop below it, top-anchored */
  }
  for (i = 0; i < n; ++i) {
    if (i < a || i > b) { y[i] = ZSTACK_HIDDEN; continue; }
    y[i] = top;
    top += h[i];
  }
  return b - a + 1;
}
