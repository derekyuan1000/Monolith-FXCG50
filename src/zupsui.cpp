/* upsui: Upsilon-style Calculation screen for KhiCAS. See upsui.h. */
#include "upsui.h"
#include <string.h>
#include <stdlib.h>

namespace ups {

struct UpsFont { int w, h; const unsigned char * data; };
#include "ups_fonts.h"

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static unsigned short * s_fb = 0;
static int s_clipX0 = 0, s_clipY0 = 0, s_clipX1 = SCREEN_W, s_clipY1 = SCREEN_H;

static void setClip(int x0, int y0, int x1, int y1) {
  s_clipX0 = x0 < 0 ? 0 : x0; s_clipY0 = y0 < 0 ? 0 : y0;
  s_clipX1 = x1 > SCREEN_W ? SCREEN_W : x1; s_clipY1 = y1 > SCREEN_H ? SCREEN_H : y1;
}
static void resetClip() { setClip(0, 0, SCREEN_W, SCREEN_H); }

static void fillRect(int x, int y, int w, int h, unsigned short c) {
  int x0 = x < s_clipX0 ? s_clipX0 : x, y0 = y < s_clipY0 ? s_clipY0 : y;
  int x1 = x + w > s_clipX1 ? s_clipX1 : x + w, y1 = y + h > s_clipY1 ? s_clipY1 : y + h;
  for (int j = y0; j < y1; j++) {
    unsigned short * p = s_fb + j * SCREEN_W;
    for (int i = x0; i < x1; i++) p[i] = c;
  }
}

static unsigned short blend(unsigned short fg, unsigned short bg, int a /* 0..255, fg weight */) {
  int r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a)) / 255;
  int g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
  int b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
  return (unsigned short)((r << 11) | (g << 5) | b);
}

static inline void blendPixel(int x, int y, unsigned short c, int a) {
  if (a <= 0 || x < s_clipX0 || x >= s_clipX1 || y < s_clipY0 || y >= s_clipY1) return;
  unsigned short * p = s_fb + y * SCREEN_W + x;
  *p = a >= 255 ? c : blend(c, *p, a);
}

static unsigned decodeUtf8(const char ** s) {
  const unsigned char * p = (const unsigned char *)*s;
  unsigned c = *p;
  int n = 0;
  if (c < 0x80) { n = 1; }
  else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 2; }
  else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 3; }
  else { c &= 0x07; n = 4; }
  for (int i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) { n = i; break; }
    c = (c << 6) | (p[i] & 0x3F);
  }
  *s += n;
  return c;
}

static int encodeUtf8(unsigned c, char * out) {
  if (c < 0x80) { out[0] = (char)c; return 1; }
  if (c < 0x800) { out[0] = (char)(0xC0 | (c >> 6)); out[1] = (char)(0x80 | (c & 63)); return 2; }
  if (c < 0x10000) { out[0] = (char)(0xE0 | (c >> 12)); out[1] = (char)(0x80 | ((c >> 6) & 63)); out[2] = (char)(0x80 | (c & 63)); return 3; }
  out[0] = (char)(0xF0 | (c >> 18)); out[1] = (char)(0x80 | ((c >> 12) & 63)); out[2] = (char)(0x80 | ((c >> 6) & 63)); out[3] = (char)(0x80 | (c & 63)); return 4;
}

static int glyphIndex(unsigned cp) {
  if (cp == 0x2212) cp = '-';                 // minus sign: Upsilon's font draws '-'
  if (cp >= 0x20 && cp < 0x7f) return cp - 0x20;
  for (int i = 0x7f - 0x20; i < UPS_FONT_GLYPHS; i++)
    if (ups_font_codepoints[i] == cp) return i;
  return '?' - 0x20;
}

static void drawGlyph(const UpsFont & f, unsigned cp, int x, int y, unsigned short c) {
  const unsigned char * g = f.data + glyphIndex(cp) * ((f.w * f.h + 1) / 2);
  for (int p = 0; p < f.w * f.h; p++) {
    int v = (p & 1) ? (g[p >> 1] & 0xF) : (g[p >> 1] >> 4);
    if (v) blendPixel(x + p % f.w, y + p / f.w, c, v * 17);
  }
}

static int drawText(const UpsFont & f, const char * s, int x, int y, unsigned short c) {
  int x0 = x;
  while (*s) { drawGlyph(f, decodeUtf8(&s), x, y, c); x += f.w; }
  return x - x0;
}

static int textWidth(const UpsFont & f, const char * s) {
  int n = 0;
  while (*s) { decodeUtf8(&s); n++; }
  return n * f.w;
}

/* ------------------------------------------------------------------ */
/* Layout tree                                                         */
/* ------------------------------------------------------------------ */

enum Kind { ROW, CHR, FRAC, SUP, ROOT, ABSV, MAT, VLIST };

struct Node {
  unsigned char kind;
  unsigned char rows;    // MAT
  unsigned short cols;   // MAT
  unsigned cp;           // CHR
  short parent, prev, next, first, last;  // siblings; children list (rows of a container, items of a row)
  short w, h, base;      // size and baseline
  short x, y;            // position relative to the parent box
};

struct Pool {
  enum { N = 900 };
  Node n[N];
  int count;
  int freeList;
  bool full;
  void clear() { count = 0; freeList = -1; full = false; }
  int alloc(int kind) {
    int i;
    if (freeList >= 0) { i = freeList; freeList = n[i].next; }
    else if (count < N) i = count++;
    else { full = true; return -1; }
    Node & d = n[i];
    memset(&d, 0, sizeof(Node));
    d.kind = (unsigned char)kind;
    d.parent = d.prev = d.next = d.first = d.last = -1;
    return i;
  }
  void release(int i) {   // node and its subtree
    if (i < 0) return;
    for (int c = n[i].first; c >= 0;) { int nx = n[c].next; release(c); c = nx; }
    n[i].next = freeList; freeList = i;
  }
};

/* Allocated in init(): KhiCAS has almost no static RAM left, but a large heap */
static Pool * s_editPtr = 0;     // the input field
static Pool * s_scratchPtr = 0;  // layouts of history cells, rebuilt when drawn
#define s_edit (*s_editPtr)
#define s_scratch (*s_scratchPtr)

static void appendChild(Pool & P, int parent, int child) {
  Node & p = P.n[parent];
  Node & c = P.n[child];
  c.parent = (short)parent; c.next = -1; c.prev = p.last;
  if (p.last >= 0) P.n[p.last].next = (short)child; else p.first = (short)child;
  p.last = (short)child;
}

// Insert `item` into row `row` after `after` (-1 = at the start)
static void insertAfter(Pool & P, int row, int after, int item) {
  Node & it = P.n[item];
  it.parent = (short)row;
  it.prev = (short)after;
  if (after < 0) { it.next = P.n[row].first; P.n[row].first = (short)item; }
  else { it.next = P.n[after].next; P.n[after].next = (short)item; }
  if (it.next >= 0) P.n[it.next].prev = (short)item; else P.n[row].last = (short)item;
}

static void unlink(Pool & P, int item) {
  Node & it = P.n[item];
  int row = it.parent;
  if (it.prev >= 0) P.n[it.prev].next = it.next; else P.n[row].first = it.next;
  if (it.next >= 0) P.n[it.next].prev = it.prev; else P.n[row].last = it.prev;
  it.prev = it.next = it.parent = -1;
}

static int child(const Pool & P, int node, int index) {
  int c = P.n[node].first;
  while (c >= 0 && index-- > 0) c = P.n[c].next;
  return c;
}

static int newChr(Pool & P, unsigned cp) { int i = P.alloc(CHR); if (i >= 0) P.n[i].cp = cp; return i; }

static int newContainer(Pool & P, int kind, int nrows) {
  int i = P.alloc(kind);
  if (i < 0) return -1;
  for (int r = 0; r < nrows; r++) { int row = P.alloc(ROW); if (row < 0) return -1; appendChild(P, i, row); }
  return i;
}

static bool isParenOpen(const Pool & P, int i) { return i >= 0 && P.n[i].kind == CHR && P.n[i].cp == '('; }
static bool isParenClose(const Pool & P, int i) { return i >= 0 && P.n[i].kind == CHR && P.n[i].cp == ')'; }

/* ---- metrics, chosen to match Upsilon's spacing ---- */
enum {
  GLYPH_W = 10, GLYPH_H = 18,
  FRAC_MARGIN = 2, FRAC_LINE = 1, FRAC_OVERFLOW = 2, FRAC_HMARGIN = 2,
  INDICE_HEIGHT = 5,
  RADIX_W = 5, RADIX_H = 9, ROOT_HMARGIN = 2, ROOT_WMARGIN = 2, RADIX_LINE = 1,
  PAREN_W = 7, PAREN_VMARGIN = 4,
  EMPTY_W = 10, EMPTY_H = 18,
  ABS_MARGIN = 4,
  MAT_HSPACE = 10, MAT_VSPACE = 4, BRACKET_W = 5
};

/* Antialiased strokes for brackets and the radical sign (drawn, not bitmaps) */
static void strokeSegment(float x0, float y0, float x1, float y1, int bx, int by, int bw, int bh, unsigned short c) {
  float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
  for (int j = 0; j < bh; j++)
    for (int i = 0; i < bw; i++) {
      float px = i + 0.5f, py = j + 0.5f;
      float t = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0;
      if (t < 0) t = 0; else if (t > 1) t = 1;
      float ex = x0 + t * dx - px, ey = y0 + t * dy - py;
      float d2 = ex * ex + ey * ey;
      // about 1.3 px wide, soft edge
      int a = d2 < 0.16f ? 255 : (d2 < 0.81f ? (int)(255 * (0.81f - d2) / 0.65f) : 0);
      if (a) blendPixel(bx + i, by + j, c, a);
    }
}

static void measure(Pool & P, int i, bool showEmpty);

static void measureRow(Pool & P, int row, bool showEmpty) {
  Node & r = P.n[row];
  if (r.first < 0) {
    if (showEmpty) { r.w = EMPTY_W; r.h = EMPTY_H; r.base = EMPTY_H / 2; }
    else { r.w = 0; r.h = GLYPH_H; r.base = GLYPH_H / 2; }
    return;
  }
  // Items other than brackets and superscripts first
  for (int c = r.first; c >= 0; c = P.n[c].next) {
    Node & it = P.n[c];
    if (it.kind == CHR) {
      it.w = GLYPH_W; it.h = GLYPH_H; it.base = GLYPH_H / 2;
      if (it.cp == '(' || it.cp == ')') it.w = PAREN_W;
    } else if (it.kind == SUP) {
      measure(P, it.first, true);
    } else {
      measure(P, c, true);
    }
  }
  /* Left to right: superscripts take the size of what is on their left,
   * brackets the size of what they enclose. */
  short stack[64]; int depth = 0;
  int above[64], below[64];      // extent of the content inside each open bracket
  int curAbove = 0, curBelow = 0;  // extent of the current bracket level
  for (int c = r.first; c >= 0; c = P.n[c].next) {
    Node & it = P.n[c];
    if (it.kind == SUP) {
      int prev = it.prev;
      int bh = GLYPH_H, bb = GLYPH_H / 2;
      if (prev >= 0) { bh = P.n[prev].h; bb = P.n[prev].base; }
      Node & ind = P.n[it.first];
      it.w = ind.w;
      it.h = (short)(bh - INDICE_HEIGHT + ind.h);
      it.base = (short)(ind.h - INDICE_HEIGHT + bb);
      ind.x = 0; ind.y = 0;
    } else if (it.kind == CHR && it.cp == '(') {
      if (depth < 64) { stack[depth] = (short)c; above[depth] = curAbove; below[depth] = curBelow; depth++; }
      curAbove = GLYPH_H / 2; curBelow = GLYPH_H - GLYPH_H / 2;
      continue;
    } else if (it.kind == CHR && it.cp == ')') {
      int a = curAbove, b = curBelow;
      it.h = (short)(a + b + PAREN_VMARGIN); it.base = (short)(a + PAREN_VMARGIN / 2);
      if (depth > 0) {
        depth--;
        Node & o = P.n[stack[depth]];
        o.h = it.h; o.base = it.base;
        curAbove = above[depth]; curBelow = below[depth];
      } else {
        curAbove = GLYPH_H / 2; curBelow = GLYPH_H - GLYPH_H / 2;
      }
    }
    if (it.base > curAbove) curAbove = it.base;
    if (it.h - it.base > curBelow) curBelow = it.h - it.base;
  }
  // Unclosed brackets enclose the rest of the row
  while (depth > 0) {
    depth--;
    Node & o = P.n[stack[depth]];
    o.h = (short)(curAbove + curBelow + PAREN_VMARGIN); o.base = (short)(curAbove + PAREN_VMARGIN / 2);
    int a = curAbove, b = curBelow;
    curAbove = above[depth] > o.base ? above[depth] : o.base;
    curBelow = below[depth] > o.h - o.base ? below[depth] : o.h - o.base;
    (void)a; (void)b;
  }
  int maxAbove = 0, maxBelow = 0, x = 0;
  for (int c = r.first; c >= 0; c = P.n[c].next) {
    Node & it = P.n[c];
    if (it.base > maxAbove) maxAbove = it.base;
    if (it.h - it.base > maxBelow) maxBelow = it.h - it.base;
  }
  for (int c = r.first; c >= 0; c = P.n[c].next) {
    Node & it = P.n[c];
    it.x = (short)x; it.y = (short)(maxAbove - it.base);
    x += it.w;
  }
  r.w = (short)x; r.h = (short)(maxAbove + maxBelow); r.base = (short)maxAbove;
}

static void measure(Pool & P, int i, bool showEmpty) {
  Node & d = P.n[i];
  switch (d.kind) {
  case ROW: measureRow(P, i, showEmpty); break;
  case CHR: d.w = GLYPH_W; d.h = GLYPH_H; d.base = GLYPH_H / 2; break;
  case FRAC: {
    int num = d.first, den = P.n[num].next;
    measureRow(P, num, true); measureRow(P, den, true);
    Node & a = P.n[num]; Node & b = P.n[den];
    int inner = a.w > b.w ? a.w : b.w;
    d.w = (short)(inner + 2 * FRAC_OVERFLOW + 2 * FRAC_HMARGIN);
    d.h = (short)(a.h + 2 * FRAC_MARGIN + FRAC_LINE + b.h);
    d.base = (short)(a.h + FRAC_MARGIN + FRAC_LINE);
    a.x = (short)((d.w - a.w) / 2); a.y = 0;
    b.x = (short)((d.w - b.w) / 2); b.y = (short)(a.h + 2 * FRAC_MARGIN + FRAC_LINE);
    break;
  }
  case ROOT: {
    int rad = d.first, idx = P.n[rad].next;
    measureRow(P, rad, true);
    int iw = RADIX_W, ih = 0;
    if (idx >= 0) {
      measureRow(P, idx, true);
      iw = P.n[idx].w > RADIX_W ? P.n[idx].w : RADIX_W; ih = P.n[idx].h;
    }
    Node & r = P.n[rad];
    int base = r.base + RADIX_LINE + ROOT_HMARGIN;
    if (ih > base) base = ih;
    d.base = (short)base;
    d.w = (short)(iw + 3 * ROOT_WMARGIN + RADIX_LINE + r.w);
    d.h = (short)(base + r.h - r.base);
    r.x = (short)(iw + 2 * ROOT_WMARGIN + RADIX_LINE); r.y = (short)(base - r.base);
    if (idx >= 0) { P.n[idx].x = 0; P.n[idx].y = (short)(base - ih); }
    break;
  }
  case ABSV: {
    int c = d.first; measureRow(P, c, true);
    Node & r = P.n[c];
    d.w = (short)(r.w + 2 * ABS_MARGIN + 2); d.h = (short)(r.h + 2); d.base = (short)(r.base + 1);
    r.x = (short)(ABS_MARGIN + 1); r.y = 1;
    break;
  }
  case MAT: {
    int colw[16], rowa[16], rowb[16];
    int R = d.rows, C = d.cols;
    for (int k = 0; k < 16; k++) { colw[k] = 0; rowa[k] = 0; rowb[k] = 0; }
    int c = d.first;
    for (int y = 0; y < R; y++)
      for (int x = 0; x < C && c >= 0; x++, c = P.n[c].next) {
        measureRow(P, c, true);
        Node & e = P.n[c];
        if (x < 16 && e.w > colw[x]) colw[x] = e.w;
        if (y < 16 && e.base > rowa[y]) rowa[y] = e.base;
        if (y < 16 && e.h - e.base > rowb[y]) rowb[y] = e.h - e.base;
      }
    int w = 0, h = 0;
    for (int x = 0; x < C && x < 16; x++) w += colw[x] + (x ? MAT_HSPACE : 0);
    for (int y = 0; y < R && y < 16; y++) h += rowa[y] + rowb[y] + (y ? MAT_VSPACE : 0);
    d.w = (short)(w + 2 * (BRACKET_W + 3)); d.h = (short)(h + 4); d.base = (short)(d.h / 2);
    c = d.first;
    int yy = 2;
    for (int y = 0; y < R; y++) {
      int xx = BRACKET_W + 3;
      for (int x = 0; x < C && c >= 0; x++, c = P.n[c].next) {
        Node & e = P.n[c];
        int cw = x < 16 ? colw[x] : e.w;
        e.x = (short)(xx + (cw - e.w) / 2); e.y = (short)(yy + (y < 16 ? rowa[y] : e.base) - e.base);
        xx += cw + MAT_HSPACE;
      }
      yy += (y < 16 ? rowa[y] + rowb[y] : 0) + MAT_VSPACE;
    }
    break;
  }
  case SUP: measureRow(P, d.first, true); break;
  case VLIST: {   // lines of text (prints before a result), left aligned
    int w = 0, y = 0;
    for (int c = d.first; c >= 0; c = P.n[c].next) {
      measureRow(P, c, false);
      Node & e = P.n[c];
      e.x = 0; e.y = (short)y;
      y += e.h + 2;
      if (e.w > w) w = e.w;
    }
    d.w = (short)w; d.h = (short)(y > 0 ? y - 2 : 0); d.base = (short)(d.h / 2);
    break;
  }
  }
}

// A bracket is a shallow parabola: closest to the content in the middle, as in Upsilon
static void drawParen(int x, int y, int h, bool left, unsigned short c) {
  for (int j = 0; j < h; j++) {
    int acc[PAREN_W];
    for (int i = 0; i < PAREN_W; i++) acc[i] = 0;
    for (int sub = 0; sub < 4; sub++) {
      float u = ((j + (sub + 0.5f) / 4) / h) * 2 - 1;             // -1 .. 1 down the bracket
      float xc = 1.6f + 3.4f * u * u;                              // ink centre for a left bracket
      if (!left) xc = PAREN_W - xc;
      for (int i = 0; i < PAREN_W; i++) {
        float d = (i + 0.5f) - xc; if (d < 0) d = -d;
        int a = d < 0.35f ? 255 : (d < 1.05f ? (int)(255 * (1.05f - d) / 0.7f) : 0);
        acc[i] += a;
      }
    }
    for (int i = 0; i < PAREN_W; i++) if (acc[i]) blendPixel(x + i, y + j, c, acc[i] / 4);
  }
}

static void drawNode(Pool & P, int i, int x, int y, unsigned short fg, bool showEmpty);

static void drawRow(Pool & P, int row, int x, int y, unsigned short fg, bool showEmpty) {
  Node & r = P.n[row];
  if (r.first < 0) {
    if (showEmpty) fillRect(x + 1, y + 3, EMPTY_W - 2, EMPTY_H - 6, C_SURFACE1);
    return;
  }
  for (int c = r.first; c >= 0; c = P.n[c].next) drawNode(P, c, x + P.n[c].x, y + P.n[c].y, fg, showEmpty);
}

static void drawNode(Pool & P, int i, int x, int y, unsigned short fg, bool showEmpty) {
  Node & d = P.n[i];
  if (x > s_clipX1 || x + d.w < s_clipX0 || y > s_clipY1 || y + d.h < s_clipY0) return;
  switch (d.kind) {
  case ROW: drawRow(P, i, x, y, fg, showEmpty); break;
  case CHR:
    if (d.cp == '(' || d.cp == ')') drawParen(x, y, d.h, d.cp == '(', fg);
    else drawGlyph(ups_large, d.cp, x, y, fg);
    break;
  case FRAC: {
    int num = d.first, den = P.n[num].next;
    drawRow(P, num, x + P.n[num].x, y + P.n[num].y, fg, true);
    drawRow(P, den, x + P.n[den].x, y + P.n[den].y, fg, true);
    fillRect(x + FRAC_HMARGIN, y + P.n[num].h + FRAC_MARGIN, d.w - 2 * FRAC_HMARGIN, FRAC_LINE, fg);
    break;
  }
  case SUP: drawRow(P, d.first, x, y, fg, true); break;
  case ROOT: {
    int rad = d.first, idx = P.n[rad].next;
    Node & r = P.n[rad];
    int iw = idx >= 0 ? (P.n[idx].w > RADIX_W ? P.n[idx].w : RADIX_W) : RADIX_W;
    drawRow(P, rad, x + r.x, y + r.y, fg, true);
    if (idx >= 0) drawRow(P, idx, x + P.n[idx].x, y + P.n[idx].y, fg, true);
    int top = d.base - r.base - RADIX_LINE - ROOT_HMARGIN;
    // the radical's tick: from its top left down to the foot of the vertical stroke
    strokeSegment(0.6f, 0.8f, RADIX_W - 0.5f, RADIX_H - 0.4f, x + iw + ROOT_WMARGIN - RADIX_W, y + d.base + r.h - r.base - RADIX_H, RADIX_W, RADIX_H, fg);
    fillRect(x + iw + ROOT_WMARGIN, y + top, RADIX_LINE, r.h + ROOT_HMARGIN + RADIX_LINE, fg);
    fillRect(x + iw + ROOT_WMARGIN, y + top, r.w + 2 * ROOT_WMARGIN + 1, RADIX_LINE, fg);
    break;
  }
  case ABSV: {
    Node & r = P.n[d.first];
    drawRow(P, d.first, x + r.x, y + r.y, fg, true);
    fillRect(x + 1, y, 1, d.h, fg);
    fillRect(x + d.w - 2, y, 1, d.h, fg);
    break;
  }
  case VLIST:
    for (int c = d.first; c >= 0; c = P.n[c].next) drawRow(P, c, x + P.n[c].x, y + P.n[c].y, fg, false);
    break;
  case MAT: {
    for (int c = d.first; c >= 0; c = P.n[c].next) drawRow(P, c, x + P.n[c].x, y + P.n[c].y, fg, true);
    // square brackets
    fillRect(x + 2, y, 1, d.h, fg); fillRect(x + 2, y, BRACKET_W, 1, fg); fillRect(x + 2, y + d.h - 1, BRACKET_W, 1, fg);
    fillRect(x + d.w - 3, y, 1, d.h, fg); fillRect(x + d.w - 2 - BRACKET_W, y, BRACKET_W, 1, fg); fillRect(x + d.w - 2 - BRACKET_W, y + d.h - 1, BRACKET_W, 1, fg);
    break;
  }
  }
}

// Absolute position of a node's box inside the layout rooted at `root`
static void absolutePos(const Pool & P, int i, int root, int * ax, int * ay) {
  int x = 0, y = 0;
  while (i >= 0 && i != root) { x += P.n[i].x; y += P.n[i].y; i = P.n[i].parent; }
  *ax = x; *ay = y;
}

/* ------------------------------------------------------------------ */
/* Serialiser: layout -> giac syntax                                   */
/* ------------------------------------------------------------------ */

struct Out {
  char * buf; int size, len;
  void put(const char * s) { while (*s) putc(*s++); }
  void putc(char c) { if (len < size - 1) buf[len++] = c; buf[len] = 0; }
  void putcp(unsigned cp) { char t[5]; int n = encodeUtf8(cp, t); for (int k = 0; k < n; k++) putc(t[k]); }
};

static bool isDigitCp(unsigned c) { return (c >= '0' && c <= '9') || c == '.'; }
static bool isLetterCp(unsigned c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == 0x3c0 || c == 0x212f || c == 0x3b8 || (c >= 0x3b1 && c <= 0x3c9) || c == 0x394 || c == 0x221e; }

static void serializeRow(const Pool & P, int row, Out & o);
static bool s_python = false;   // serialise with Python syntax

static void serializeCp(unsigned cp, Out & o) {
  switch (cp) {
  case 0xd7: case 0xb7: o.putc('*'); break;
  case 0x2212: o.putc('-'); break;
  case 0xf7: o.putc('/'); break;
  case 0x3c0: o.put("pi"); break;
  case 0x212f: o.put(s_python ? "e" : "e"); break;
  case 0x1d422: o.putc('i'); break;
  case 0x2264: o.put("<="); break;
  case 0x2265: o.put(">="); break;
  case 0x2260: o.put("!="); break;
  case 0x3b8: o.put("theta"); break;
  case 0x221e: o.put("inf"); break;
  case 0x2192: o.put("=>"); break;
  case 0x1d07: o.putc('e'); break;   // small E of x10^
  default: o.putcp(cp);
  }
}

// What an item "ends with" / "starts with", to insert '*' where giac needs one
enum { T_OP, T_NUM, T_LETTER, T_CLOSE, T_OPEN, T_BOX };
static int startType(const Pool & P, int i) {
  const Node & d = P.n[i];
  if (d.kind != CHR) return d.kind == SUP ? T_OP : T_BOX;
  if (isDigitCp(d.cp)) return T_NUM;
  if (isLetterCp(d.cp)) return T_LETTER;
  if (d.cp == '(') return T_OPEN;
  return T_OP;
}
static int endType(const Pool & P, int i) {
  const Node & d = P.n[i];
  if (d.kind == SUP) return T_CLOSE;
  if (d.kind != CHR) return T_BOX;
  if (isDigitCp(d.cp)) return T_NUM;
  if (isLetterCp(d.cp)) return T_LETTER;
  if (d.cp == ')' || d.cp == ']') return T_CLOSE;
  return T_OP;
}
static bool needStar(int prevEnd, int nextStart) {
  if (prevEnd == T_OP) return false;
  if (s_python && prevEnd == T_NUM && nextStart == T_LETTER) return true;   // 2x is not Python
  if (nextStart == T_BOX) return true;
  if (nextStart == T_OPEN) return prevEnd != T_LETTER;   // f( is a call, 2( is a product
  if (nextStart == T_NUM) return prevEnd == T_CLOSE || prevEnd == T_BOX;
  if (nextStart == T_LETTER) return prevEnd == T_CLOSE || prevEnd == T_BOX;
  return false;
}

static void serializeItem(const Pool & P, int i, Out & o) {
  const Node & d = P.n[i];
  switch (d.kind) {
  case CHR: serializeCp(d.cp, o); break;
  case FRAC: {
    int num = d.first, den = P.n[num].next;
    o.put("(("); serializeRow(P, num, o); o.put(")/("); serializeRow(P, den, o); o.put("))");
    break;
  }
  case SUP: o.put(s_python ? "**(" : "^("); serializeRow(P, d.first, o); o.putc(')'); break;
  case ROOT: {
    int rad = d.first, idx = P.n[rad].next;
    if (idx >= 0 && s_python) { o.put("(("); serializeRow(P, rad, o); o.put(")**(1/("); serializeRow(P, idx, o); o.put(")))"); }
    else if (idx >= 0) { o.put("surd("); serializeRow(P, rad, o); o.putc(','); serializeRow(P, idx, o); o.putc(')'); }
    else { o.put("sqrt("); serializeRow(P, rad, o); o.putc(')'); }
    break;
  }
  case ABSV: o.put("abs("); serializeRow(P, d.first, o); o.putc(')'); break;
  case MAT: {
    int c = d.first;
    o.putc('[');
    for (int y = 0; y < d.rows; y++) {
      if (y) o.putc(',');
      o.putc('[');
      for (int x = 0; x < d.cols && c >= 0; x++, c = P.n[c].next) { if (x) o.putc(','); serializeRow(P, c, o); }
      o.putc(']');
    }
    o.putc(']');
    break;
  }
  }
}

static void serializeRow(const Pool & P, int row, Out & o) {
  int prev = -1;
  for (int c = P.n[row].first; c >= 0; c = P.n[c].next) {
    if (prev >= 0 && needStar(endType(P, prev), startType(P, c))) o.putc('*');
    serializeItem(P, c, o);
    prev = c;
  }
}

/* ------------------------------------------------------------------ */
/* Parser: giac text -> layout                                         */
/* ------------------------------------------------------------------ */

struct Ast {
  enum Type { NUM, SYM, BIN, NEG, CALL, LIST, POSTFIX, STR };
  unsigned char type;
  char op;              // BIN: + - * / ^ = < > l(<=) g(>=) n(!=) e(==) ,
  char bracket;         // LIST: '[' or '('
  const char * s; int len;   // NUM, SYM, CALL name, STR
  short a, b;           // BIN children; NEG/POSTFIX child a
  short args[24]; short nargs;  // CALL, LIST
};

struct Parser {
  enum { N = 400 };
  Ast t[N]; int count;
  const char * p;
  bool ok;
  int alloc(int type) { if (count >= N) { ok = false; return 0; } Ast & a = t[count]; memset(&a, 0, sizeof(Ast)); a.type = (unsigned char)type; return count++; }
  void skip() { while (*p == ' ') p++; }

  int primary() {
    skip();
    const char * s = p;
    if ((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) {
      while ((*p >= '0' && *p <= '9') || *p == '.') p++;
      if ((*p == 'e' || *p == 'E') && ((p[1] >= '0' && p[1] <= '9') || ((p[1] == '-' || p[1] == '+') && p[2] >= '0' && p[2] <= '9'))) {
        p += 2; while (*p >= '0' && *p <= '9') p++;
      }
      int n = alloc(Ast::NUM); t[n].s = s; t[n].len = (int)(p - s); return n;
    }
    if (*p == '"') {
      p++; while (*p && *p != '"') p++;
      int n = alloc(Ast::STR); t[n].s = s + 1; t[n].len = (int)(p - s - 1); if (*p) p++; return n;
    }
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' || (unsigned char)*p >= 0x80) {
      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || (unsigned char)*p >= 0x80) p++;
      int len = (int)(p - s);
      skip();
      if (*p == '(') {
        p++;
        int n = alloc(Ast::CALL); t[n].s = s; t[n].len = len;
        args(n, ')');
        return n;
      }
      int n = alloc(Ast::SYM); t[n].s = s; t[n].len = len; return n;
    }
    if (*p == '(' || *p == '[') {
      char open = *p, close = open == '(' ? ')' : ']';
      p++;
      int n = alloc(Ast::LIST); t[n].bracket = open;
      args(n, close);
      if (open == '(' && t[n].nargs == 1) return t[n].args[0];   // plain grouping
      return n;
    }
    ok = false;
    return alloc(Ast::SYM);
  }

  void args(int n, char close) {
    skip();
    if (*p == close) { p++; return; }
    for (;;) {
      int e = expr(1);
      if (t[n].nargs < 24) t[n].args[t[n].nargs++] = (short)e; else ok = false;
      skip();
      if (*p == ',') { p++; continue; }
      if (*p == close) { p++; return; }
      ok = false; return;
    }
  }

  int unary() {
    skip();
    if (*p == '-') { p++; int n = alloc(Ast::NEG); t[n].a = (short)power(); return n; }
    if (*p == '+') { p++; return power(); }
    return power();
  }

  int postfix() {
    int e = primary();
    for (;;) {
      skip();
      if (*p == '!' && p[1] != '=') { p++; int n = alloc(Ast::POSTFIX); t[n].a = (short)e; e = n; continue; }
      return e;
    }
  }

  int power() {
    int b = postfix();
    skip();
    if (*p == '^') {
      p++;
      int n = alloc(Ast::BIN); t[n].op = '^'; t[n].a = (short)b; t[n].b = (short)unary();   // right associative, -2 allowed
      return n;
    }
    return b;
  }

  static int precedence(char op) {
    switch (op) {
    case '=': case '<': case '>': case 'l': case 'g': case 'n': case 'e': return 1;
    case '+': case '-': return 2;
    case '*': case '/': return 3;
    }
    return 0;
  }

  char peekOp(int * len) {
    skip();
    *len = 1;
    switch (*p) {
    case '+': case '-': case '*': case '/': return *p;
    case '=': if (p[1] == '=') { *len = 2; return 'e'; } return '=';
    case '<': if (p[1] == '=') { *len = 2; return 'l'; } return '<';
    case '>': if (p[1] == '=') { *len = 2; return 'g'; } return '>';
    case '!': if (p[1] == '=') { *len = 2; return 'n'; } return 0;
    }
    return 0;
  }

  int expr(int minPrec) {
    int left = unary();
    for (;;) {
      int len; char op = peekOp(&len);
      int pr = precedence(op);
      if (!op || pr < minPrec) return left;
      p += len;
      int right = expr(pr + 1);
      int n = alloc(Ast::BIN); t[n].op = op; t[n].a = (short)left; t[n].b = (short)right;
      left = n;
    }
  }
};

static Parser * s_parserPtr = 0;
#define s_parser (*s_parserPtr)

static void emit(Pool & P, Parser & T, int n, int row);

static void emitText(Pool & P, int row, const char * s, int len) {
  const char * e = s + len;
  while (s < e) { unsigned cp = decodeUtf8(&s); int c = newChr(P, cp); if (c < 0) return; appendChild(P, row, c); }
}
static void emitStr(Pool & P, int row, const char * s) { emitText(P, row, s, (int)strlen(s)); }
static void emitCp(Pool & P, int row, unsigned cp) { int c = newChr(P, cp); if (c >= 0) appendChild(P, row, c); }

static bool nameIs(const Ast & a, const char * name) { int l = (int)strlen(name); return a.len == l && strncmp(a.s, name, l) == 0; }

static int precOf(const Parser & T, int n) {
  const Ast & a = T.t[n];
  if (a.type == Ast::BIN) return a.op == '^' ? 5 : Parser::precedence(a.op);
  if (a.type == Ast::NEG) return 2;   // -x needs brackets after * or ^
  return 9;
}

static void emitWrapped(Pool & P, Parser & T, int n, int row, bool wrap) {
  if (wrap) emitCp(P, row, '(');
  emit(P, T, n, row);
  if (wrap) emitCp(P, row, ')');
}

static int emitContainer(Pool & P, int row, int kind, int nrows) {
  int c = newContainer(P, kind, nrows);
  if (c >= 0) appendChild(P, row, c);
  return c;
}

// First item a subtree would display, for the "show x or not" rule of products
static bool startsWithDigit(const Parser & T, int n) {
  const Ast & a = T.t[n];
  if (a.type == Ast::NUM) return true;
  if (a.type == Ast::BIN && a.op != '/') return startsWithDigit(T, a.a);
  if (a.type == Ast::POSTFIX) return startsWithDigit(T, a.a);
  return false;
}

static void emitNumber(Pool & P, int row, const char * s, int len) {
  // 1.5e-07 -> 1.5x10^-7, as Upsilon shows it
  int e = -1;
  for (int k = 0; k < len; k++) if (s[k] == 'e' || s[k] == 'E') { e = k; break; }
  if (e < 0) { emitText(P, row, s, len); return; }
  emitText(P, row, s, e);
  emitCp(P, row, 0xd7); emitStr(P, row, "10");
  int sup = emitContainer(P, row, SUP, 1);
  if (sup < 0) return;
  const char * x = s + e + 1; int xl = len - e - 1;
  if (*x == '+') { x++; xl--; }
  bool neg = *x == '-'; if (neg) { x++; xl--; }
  while (xl > 1 && *x == '0') { x++; xl--; }
  if (neg) emitCp(P, P.n[sup].first, '-');
  emitText(P, P.n[sup].first, x, xl);
}

static void emit(Pool & P, Parser & T, int n, int row) {
  if (P.full) return;
  Ast & a = T.t[n];
  switch (a.type) {
  case Ast::NUM: emitNumber(P, row, a.s, a.len); break;
  case Ast::STR: emitCp(P, row, '"'); emitText(P, row, a.s, a.len); emitCp(P, row, '"'); break;
  case Ast::SYM:
    if (nameIs(a, "pi")) emitCp(P, row, 0x3c0);
    else if (nameIs(a, "infinity") || nameIs(a, "inf")) emitCp(P, row, 0x221e);
    else if (nameIs(a, "e")) emitCp(P, row, 0x212f);
    else if (nameIs(a, "theta")) emitCp(P, row, 0x3b8);
    else emitText(P, row, a.s, a.len);
    break;
  case Ast::NEG:
    emitCp(P, row, '-');
    emitWrapped(P, T, a.a, row, precOf(T, a.a) <= 2);
    break;
  case Ast::POSTFIX:
    emitWrapped(P, T, a.a, row, precOf(T, a.a) < 9);
    emitCp(P, row, '!');
    break;
  case Ast::BIN: {
    int pr = precOf(T, n);
    if (a.op == '/') {
      int numerator = a.a;
      if (T.t[numerator].type == Ast::NEG) { emitCp(P, row, '-'); numerator = T.t[numerator].a; }   // -1/2: minus outside
      int f = emitContainer(P, row, FRAC, 2);
      if (f < 0) return;
      emit(P, T, numerator, P.n[f].first);
      emit(P, T, a.b, P.n[P.n[f].first].next);
      break;
    }
    if (a.op == '^') {
      const Ast & b = T.t[a.a];
      bool atom = (b.type == Ast::NUM && b.s[0] != '-') || b.type == Ast::SYM || b.type == Ast::CALL || b.type == Ast::LIST;
      // sqrt(x)^2 and friends look wrong without brackets, as in Upsilon
      if (b.type == Ast::CALL && (nameIs(b, "sqrt") || nameIs(b, "abs"))) atom = true;
      emitWrapped(P, T, a.a, row, !atom);
      int s = emitContainer(P, row, SUP, 1);
      if (s >= 0) emit(P, T, a.b, P.n[s].first);
      break;
    }
    if (a.op == '*') {
      emitWrapped(P, T, a.a, row, precOf(T, a.a) < pr);
      bool wrapRight = precOf(T, a.b) < pr || T.t[a.b].type == Ast::NEG;
      // Upsilon writes 2x, 3(x+1), xy but 2x3: a sign only before a digit
      if (!wrapRight && startsWithDigit(T, a.b)) emitCp(P, row, 0xd7);
      emitWrapped(P, T, a.b, row, wrapRight);
      break;
    }
    emitWrapped(P, T, a.a, row, precOf(T, a.a) < pr);
    switch (a.op) {
    case 'l': emitCp(P, row, 0x2264); break;
    case 'g': emitCp(P, row, 0x2265); break;
    case 'n': emitCp(P, row, 0x2260); break;
    case 'e': emitStr(P, row, "=="); break;
    default: emitCp(P, row, (unsigned char)a.op);
    }
    // a-(b+c), a-(-b): brackets on the right of - when needed
    bool wrapRight = precOf(T, a.b) < pr || (precOf(T, a.b) == pr && (a.op == '-')) || (T.t[a.b].type == Ast::NEG && a.op != '=');
    emitWrapped(P, T, a.b, row, wrapRight);
    break;
  }
  case Ast::CALL: {
    if (nameIs(a, "sqrt") && a.nargs == 1) {
      int r = emitContainer(P, row, ROOT, 1);
      if (r >= 0) emit(P, T, a.args[0], P.n[r].first);
      break;
    }
    if ((nameIs(a, "surd") || nameIs(a, "root")) && a.nargs == 2) {
      int r = emitContainer(P, row, ROOT, 2);
      if (r < 0) break;
      bool rootFirst = nameIs(a, "root");   // root(n,x), surd(x,n)
      emit(P, T, a.args[rootFirst ? 1 : 0], P.n[r].first);
      emit(P, T, a.args[rootFirst ? 0 : 1], P.n[P.n[r].first].next);
      break;
    }
    if (nameIs(a, "abs") && a.nargs == 1) {
      int r = emitContainer(P, row, ABSV, 1);
      if (r >= 0) emit(P, T, a.args[0], P.n[r].first);
      break;
    }
    if (nameIs(a, "exp") && a.nargs == 1) {
      emitCp(P, row, 0x212f);
      int s = emitContainer(P, row, SUP, 1);
      if (s >= 0) emit(P, T, a.args[0], P.n[s].first);
      break;
    }
    emitText(P, row, a.s, a.len);
    emitCp(P, row, '(');
    for (int k = 0; k < a.nargs; k++) { if (k) emitCp(P, row, ','); emit(P, T, a.args[k], row); }
    emitCp(P, row, ')');
    break;
  }
  case Ast::LIST: {
    // [[a,b],[c,d]] -> matrix
    bool matrix = a.bracket == '[' && a.nargs > 0 && a.nargs <= 16;
    int cols = -1;
    for (int k = 0; matrix && k < a.nargs; k++) {
      const Ast & r = T.t[a.args[k]];
      if (r.type != Ast::LIST || r.bracket != '[' || r.nargs == 0 || r.nargs > 16) matrix = false;
      else if (cols < 0) cols = r.nargs;
      else if (cols != r.nargs) matrix = false;
    }
    if (matrix) {
      int m = P.alloc(MAT);
      if (m < 0) break;
      P.n[m].rows = (unsigned char)a.nargs; P.n[m].cols = (unsigned short)cols;
      appendChild(P, row, m);
      for (int k = 0; k < a.nargs; k++)
        for (int j = 0; j < cols; j++) {
          int cell = P.alloc(ROW); if (cell < 0) return;
          appendChild(P, m, cell);
          emit(P, T, T.t[a.args[k]].args[j], cell);
        }
      break;
    }
    emitCp(P, row, a.bracket == '[' ? '[' : '(');
    for (int k = 0; k < a.nargs; k++) { if (k) emitCp(P, row, ','); emit(P, T, a.args[k], row); }
    emitCp(P, row, a.bracket == '[' ? ']' : ')');
    break;
  }
  }
}

// Parse one line of giac text into `row`; falls back to the raw text
static void parseInto(Pool & P, int row, const char * text, int len) {
  char buf[1024];
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, text, len); buf[len] = 0;
  Parser & T = s_parser;
  T.count = 0; T.ok = true; T.p = buf;
  int top = T.expr(1);
  // top-level sequences "a,b" (giac prints solutions that way sometimes)
  int items[24]; int nitems = 0;
  items[nitems++] = top;
  T.skip();
  while (T.ok && *T.p == ',' && nitems < 24) { T.p++; items[nitems++] = T.expr(1); T.skip(); }
  if (!T.ok || *T.p != 0) { emitText(P, row, buf, len); return; }
  for (int k = 0; k < nitems; k++) {
    if (k) emitCp(P, row, ',');
    emit(P, T, items[k], row);
  }
}

// Build the layout of giac text into a new row of P. Several lines (prints
// before a result) become a column of rows.
static int buildLayout(Pool & P, const char * text) {
  int row = P.alloc(ROW);
  if (row < 0) return -1;
  const char * nl = strchr(text, '\n');
  if (!nl) {
    parseInto(P, row, text, (int)strlen(text));
  } else {
    int v = P.alloc(VLIST);
    if (v < 0) return row;
    appendChild(P, row, v);
    const char * line = text;
    for (;;) {
      const char * end = strchr(line, '\n');
      int len = end ? (int)(end - line) : (int)strlen(line);
      if (len > 0 || end) {
        int r = P.alloc(ROW);
        if (r < 0) break;
        appendChild(P, v, r);
        parseInto(P, r, line, len);
      }
      if (!end) break;
      line = end + 1;
    }
  }
  if (P.full) {   // too big for the pool: show the text
    P.clear();
    row = P.alloc(ROW);
    int n = 0; const char * s = text;
    while (*s && n < Pool::N - 2) { unsigned cp = decodeUtf8(&s); if (cp == '\n') cp = ' '; int c = newChr(P, cp); if (c < 0) break; appendChild(P, row, c); n++; }
  }
  return row;
}

/* ------------------------------------------------------------------ */
/* Editor                                                              */
/* ------------------------------------------------------------------ */

static int s_root = -1;        // root row of the input
static int s_curRow = -1;      // row holding the cursor
static int s_curAfter = -1;    // cursor is after this item (-1 = at the start of the row)

static void editorClear() {
  s_edit.clear();
  s_root = s_edit.alloc(ROW);
  s_curRow = s_root; s_curAfter = -1;
}

static bool isOperatorItem(const Pool & P, int i) {
  if (P.n[i].kind != CHR) return false;
  switch (P.n[i].cp) {
  case '+': case '-': case 0x2212: case '*': case 0xd7: case 0xb7: case '=': case ',': case '<': case '>':
  case 0x2264: case 0x2265: case 0x2260: case 0x2192: case ';': case ':':
    return true;
  }
  return false;
}

static void insertItem(int item) {
  insertAfter(s_edit, s_curRow, s_curAfter, item);
  s_curAfter = item;
}

static void insertCp(unsigned cp) {
  int c = newChr(s_edit, cp);
  if (c >= 0) insertItem(c);
}

// Put the cursor at the start (or end) of a row
static void cursorInto(int row, bool atEnd) { s_curRow = row; s_curAfter = atEnd ? s_edit.n[row].last : -1; }

static void insertFraction() {
  Pool & P = s_edit;
  int f = newContainer(P, FRAC, 2);
  if (f < 0) return;
  int num = P.n[f].first, den = P.n[num].next;
  // Collapse the term on the left of the cursor into the numerator (Poincare does the same)
  int start = s_curAfter, depth = 0;
  while (start >= 0) {
    if (isParenClose(P, start)) depth++;
    else if (isParenOpen(P, start)) { if (depth == 0) break; depth--; }
    else if (depth == 0 && isOperatorItem(P, start)) break;
    start = P.n[start].prev;
  }
  // items after `start` up to the cursor go into the numerator
  int first = start < 0 ? P.n[s_curRow].first : P.n[start].next;
  bool any = s_curAfter >= 0 && first >= 0 && (start != s_curAfter);
  if (any) {
    int c = first;
    for (;;) {
      int nx = P.n[c].next;
      bool lastOne = c == s_curAfter;
      unlink(P, c);
      appendChild(P, num, c);
      if (lastOne) break;
      c = nx;
    }
  }
  insertAfter(P, s_curRow, start, f);
  s_curAfter = f;
  cursorInto(any ? den : num, false);
}

static void insertSup(const char * prefill) {
  int s = newContainer(s_edit, SUP, 1);
  if (s < 0) return;
  insertItem(s);
  int row = s_edit.n[s].first;
  if (prefill) {
    s_curRow = row; s_curAfter = -1;
    while (*prefill) insertCp(decodeUtf8(&prefill));
    s_curRow = s_edit.n[s].parent; s_curAfter = s;   // cursor after the power
  } else {
    cursorInto(row, false);
  }
}

static void insertRoot(bool withIndex) {
  int r = newContainer(s_edit, ROOT, withIndex ? 2 : 1);
  if (r < 0) return;
  insertItem(r);
  int rad = s_edit.n[r].first;
  cursorInto(withIndex ? s_edit.n[rad].next : rad, false);   // n-th root: index first, as in Upsilon
}

static void insertAbs() {
  int a = newContainer(s_edit, ABSV, 1);
  if (a < 0) return;
  insertItem(a);
  cursorInto(s_edit.n[a].first, false);
}

/* Text from menus: "integrate(\x11,x)" puts the cursor at \x11.
 * Plain giac text (e.g. a history entry) is parsed into a layout first. */
static void insertText(const char * text) {
  bool hasMarker = strchr(text, '\x11') != 0;
  if (!hasMarker) {
    // structured insertion: parse into the scratch pool and copy items over
    s_scratch.clear();
    int row = buildLayout(s_scratch, text);
    if (row < 0) return;
    struct Copier {
      static int copy(Pool & S, int i) {
        int d = s_edit.alloc(S.n[i].kind);
        if (d < 0) return -1;
        s_edit.n[d].cp = S.n[i].cp; s_edit.n[d].rows = S.n[i].rows; s_edit.n[d].cols = S.n[i].cols;
        for (int c = S.n[i].first; c >= 0; c = S.n[c].next) { int k = copy(S, c); if (k < 0) return -1; appendChild(s_edit, d, k); }
        return d;
      }
    };
    for (int c = s_scratch.n[row].first; c >= 0; c = s_scratch.n[c].next) {
      int k = Copier::copy(s_scratch, c);
      if (k < 0) return;
      insertItem(k);
    }
    return;
  }
  int markRow = -1, markAfter = -2;
  const char * s = text;
  while (*s) {
    if (*s == '\x11') { markRow = s_curRow; markAfter = s_curAfter; s++; continue; }
    insertCp(decodeUtf8(&s));
  }
  if (markAfter != -2) { s_curRow = markRow; s_curAfter = markAfter; }
}

// Leave the container holding the cursor row, placing the cursor before/after it
static bool exitContainer(bool after) {
  int container = s_edit.n[s_curRow].parent;
  if (container < 0) return false;
  int row = s_edit.n[container].parent;
  s_curRow = row;
  s_curAfter = after ? container : s_edit.n[container].prev;
  return true;
}

static bool moveRight() {
  Pool & P = s_edit;
  int next = s_curAfter < 0 ? P.n[s_curRow].first : P.n[s_curAfter].next;
  if (next >= 0) {
    Node & d = P.n[next];
    if (d.kind == CHR) { s_curAfter = next; return true; }
    int target = d.first;
    if (d.kind == ROOT && P.n[target].next >= 0) target = P.n[target].next;   // index first
    cursorInto(target, false);
    return true;
  }
  // End of row: next sibling row in the same container (matrix cell, root index -> radicand), else leave
  int container = P.n[s_curRow].parent;
  if (container < 0) return false;
  Node & c = P.n[container];
  if (c.kind == ROOT && P.n[c.first].next == s_curRow) { cursorInto(c.first, false); return true; }
  if (c.kind == MAT && P.n[s_curRow].next >= 0) { cursorInto(P.n[s_curRow].next, false); return true; }
  return exitContainer(true);
}

static bool moveLeft() {
  Pool & P = s_edit;
  if (s_curAfter >= 0) {
    Node & d = P.n[s_curAfter];
    if (d.kind == CHR) { s_curAfter = d.prev; return true; }
    int target = d.last;      // fraction -> denominator, power -> indice, root -> radicand
    if (d.kind == ROOT) target = d.first;
    cursorInto(target, true);
    return true;
  }
  int container = P.n[s_curRow].parent;
  if (container < 0) return false;
  Node & c = P.n[container];
  if (c.kind == ROOT && c.first == s_curRow && P.n[c.first].next >= 0) { cursorInto(P.n[c.first].next, true); return true; }
  if (c.kind == MAT && P.n[s_curRow].prev >= 0) { cursorInto(P.n[s_curRow].prev, true); return true; }
  return exitContainer(false);
}

// Cursor x inside the input layout, used to keep the column when moving up/down
static int cursorX() {
  int ax, ay;
  absolutePos(s_edit, s_curRow, s_root, &ax, &ay);
  if (s_curAfter < 0) return ax;
  return ax + s_edit.n[s_curAfter].x + s_edit.n[s_curAfter].w;
}

static void cursorNearX(int row, int x) {
  int ax, ay;
  absolutePos(s_edit, row, s_root, &ax, &ay);
  s_curRow = row; s_curAfter = -1;
  int best = x - ax; if (best < 0) best = -best;
  for (int c = s_edit.n[row].first; c >= 0; c = s_edit.n[c].next) {
    int bx = ax + s_edit.n[c].x + s_edit.n[c].w;
    int d = bx - x; if (d < 0) d = -d;
    if (d < best) { best = d; s_curAfter = c; }
  }
}

static bool moveVertical(bool up) {
  Pool & P = s_edit;
  measure(P, s_root, false);   // positions are needed to keep the column
  int x = cursorX();
  // Into a power next to the cursor
  if (up) {
    int next = s_curAfter < 0 ? P.n[s_curRow].first : P.n[s_curAfter].next;
    if (s_curAfter >= 0 && P.n[s_curAfter].kind == SUP) { cursorInto(P.n[s_curAfter].first, true); return true; }
    if (next >= 0 && P.n[next].kind == SUP) { cursorInto(P.n[next].first, false); return true; }
  }
  // Walk up the containers to find one that has a row in that direction
  int row = s_curRow;
  while (row >= 0) {
    int container = P.n[row].parent;
    if (container < 0) return false;
    Node & c = P.n[container];
    if (c.kind == FRAC) {
      int num = c.first, den = P.n[num].next;
      if (up && row == den) { cursorNearX(num, x); return true; }
      if (!up && row == num) { cursorNearX(den, x); return true; }
    } else if (c.kind == SUP && !up) {
      int r = c.parent; s_curRow = r; s_curAfter = container; return true;
    } else if (c.kind == ROOT && up && P.n[c.first].next >= 0 && row == c.first) {
      cursorNearX(P.n[c.first].next, x); return true;
    } else if (c.kind == ROOT && !up && P.n[c.first].next == row) {
      cursorNearX(c.first, x); return true;
    } else if (c.kind == MAT) {
      int idx = 0; for (int k = c.first; k >= 0 && k != row; k = P.n[k].next) idx++;
      int target = up ? idx - c.cols : idx + c.cols;
      if (target >= 0 && target < c.rows * c.cols) { cursorNearX(child(P, container, target), x); return true; }
    }
    row = c.parent;
  }
  return false;
}

// Splice the items of `row` into the cursor row after `after`; returns the last item inserted
static int spliceRow(int row, int after) {
  Pool & P = s_edit;
  int last = after;
  for (int c = P.n[row].first; c >= 0;) {
    int nx = P.n[c].next;
    unlink(P, c);
    insertAfter(P, s_curRow, last, c);
    last = c;
    c = nx;
  }
  return last;
}

static void backspace() {
  Pool & P = s_edit;
  if (s_curAfter >= 0) {
    Node & d = P.n[s_curAfter];
    if (d.kind == CHR) {
      int prev = d.prev;
      unlink(P, s_curAfter); P.release(s_curAfter);
      s_curAfter = prev;
      return;
    }
    // A fraction/power/root on the left: go inside first, as Upsilon does
    moveLeft();
    return;
  }
  // At the start of a row inside a container: remove the container, keep the content
  int container = P.n[s_curRow].parent;
  if (container < 0) return;
  Node & c = P.n[container];
  if (c.kind == MAT) { moveLeft(); return; }
  int parentRow = c.parent;
  int before = c.prev;
  int first = c.first, second = P.n[first].next;
  int kind = c.kind;
  if (kind == ROOT && s_curRow == second) { moveLeft(); return; }   // in the index: just move
  int keepSecond = kind == FRAC ? second : -1;                       // roots lose their index
  unlink(P, container);
  s_curRow = parentRow;
  int mid = spliceRow(first, before);
  if (keepSecond >= 0) spliceRow(keepSecond, mid);
  // fraction: cursor between numerator and denominator content; others: where the box was
  s_curAfter = kind == FRAC ? mid : before;
  P.release(container);
}

static bool editorEmpty() { return s_edit.n[s_root].first < 0; }

/* ------------------------------------------------------------------ */
/* Calculation view                                                    */
/* ------------------------------------------------------------------ */

enum {
  TITLE_H = 18,
  CELL_MARGIN = 5,            // HistoryViewCell::k_margin
  CELL_HMARGIN = 10,          // input/output horizontal margins
  FIELD_MIN_H = 37, FIELD_MARGIN = 5,
  FIELD_MAX_H = 129,          // 0.6 * screen height
  TOOLBOX_HEADER_H = 20
};

static const Host * s_host = 0;
static const MenuItem * s_menuRoot = 0;
static int s_menuRootCount = 0;

static int s_selected = -1;      // selected calculation, -1 = editing
static bool s_selectedOutput = true;
static int s_fieldScroll = 0;    // horizontal scroll of the input field

// Toolbox state
static bool s_toolbox = false;
static const MenuItem * s_menuStack[8];
static int s_menuCount[8];
static int s_menuSel[8];
static int s_menuDepth = 0;

static char * s_evalBuf = 0;    // heap: KhiCAS has little static RAM
static char * s_inputBuf = 0;
enum { EVAL_BUF = 2048, INPUT_BUF = 1024 };

void setFramebuffer(unsigned short * fb) { s_fb = fb; }
void setToolbox(const MenuItem * root, int count) { s_menuRoot = root; s_menuRootCount = count; s_toolbox = false; }

void init(unsigned short * fb, const Host * host, const MenuItem * root, int count) {
  if (!s_editPtr) {
    // malloc, not new: KhiCAS links without C++ exception support
    s_editPtr = (Pool *)malloc(sizeof(Pool)); s_scratchPtr = (Pool *)malloc(sizeof(Pool));
    s_parserPtr = (Parser *)malloc(sizeof(Parser));
    s_evalBuf = (char *)malloc(EVAL_BUF); s_inputBuf = (char *)malloc(INPUT_BUF);
  }
  s_fb = fb; s_host = host; s_menuRoot = root; s_menuRootCount = count;
  editorClear();
  s_selected = -1; s_toolbox = false;
}

static void toolboxSelect();
bool toolboxOpen() { return s_toolbox; }

void openToolbox(int index) {
  if (!s_menuRoot || s_menuRootCount == 0) return;
  s_toolbox = true; s_menuDepth = 0; s_selected = -1;
  s_menuStack[0] = s_menuRoot; s_menuCount[0] = s_menuRootCount; s_menuSel[0] = 0;
  if (index >= 0 && index < s_menuRootCount) {
    s_menuSel[0] = index;
    if (s_menuRoot[index].children) toolboxSelect();
  }
}

void insertInInput(const char * text) { s_selected = -1; insertText(text); }
bool inputEmpty() { return editorEmpty(); }

int serializeInput(char * out, int size) {
  Out o; o.buf = out; o.size = size; o.len = 0; out[0] = 0;
  s_python = s_host && s_host->pythonMode && s_host->pythonMode();
  serializeRow(s_edit, s_root, o);
  return o.len;
}

void setInputFromText(const char * text) {
  editorClear();
  insertText(text);
}

int layoutRoundTrip(const char * text, char * out, int size) {
  s_scratch.clear();
  int row = buildLayout(s_scratch, text);
  Out o; o.buf = out; o.size = size; o.len = 0; out[0] = 0;
  if (row >= 0) serializeRow(s_scratch, row, o);
  return o.len;
}

static void evaluateInput() {
  if (editorEmpty()) {
    // EXE on an empty field repeats the last calculation, as in Upsilon
    int n = s_host->historyCount();
    if (n == 0) return;
    const char * in, * out;
    s_host->historyGet(n - 1, &in, &out);
    strncpy(s_inputBuf, in, INPUT_BUF - 1); s_inputBuf[INPUT_BUF - 1] = 0;
  } else {
    serializeInput(s_inputBuf, INPUT_BUF);
  }
  s_evalBuf[0] = 0;
  s_host->evaluate(s_inputBuf, s_evalBuf, EVAL_BUF);
  s_host->historyAdd(s_inputBuf, s_evalBuf);
  editorClear();
  s_fieldScroll = 0;
}

static void toolboxSelect() {
  const MenuItem & it = s_menuStack[s_menuDepth][s_menuSel[s_menuDepth]];
  if (it.children && s_menuDepth < 7) {
    s_menuDepth++;
    s_menuStack[s_menuDepth] = it.children; s_menuCount[s_menuDepth] = it.count; s_menuSel[s_menuDepth] = 0;
    return;
  }
  if (it.insert) {
    s_toolbox = false;
    s_selected = -1;
    if (it.insert[0] == '\x01') {
      static char buf[256];
      buf[0] = 0;
      if (s_host->action && s_host->action(it.insert + 1, buf, sizeof(buf)) && buf[0]) insertText(buf);
    } else {
      insertText(it.insert);
    }
  }
}

static bool handleToolbox(const Event & e) {
  int n = s_menuCount[s_menuDepth];
  switch (e.key) {
  case K_UP: s_menuSel[s_menuDepth] = (s_menuSel[s_menuDepth] + n - 1) % n; return true;
  case K_DOWN: s_menuSel[s_menuDepth] = (s_menuSel[s_menuDepth] + 1) % n; return true;
  case K_RIGHT: case K_EXE: toolboxSelect(); return true;
  case K_LEFT: case K_BACK:
    if (s_menuDepth > 0) s_menuDepth--; else s_toolbox = false;
    return true;
  case K_TOOLBOX: s_toolbox = false; return true;
  }
  return true;   // modal
}

bool handle(const Event & e) {
  if (s_toolbox) return handleToolbox(e);
  if (e.key == K_TOOLBOX) {
    if (!s_menuRoot || s_menuRootCount == 0) return false;
    openToolbox(-1);
    return true;
  }
  if (s_selected >= 0) {
    int n = s_host->historyCount();
    switch (e.key) {
    case K_UP:
      if (s_selectedOutput) s_selectedOutput = false;
      else if (s_selected > 0) { s_selected--; s_selectedOutput = true; }
      return true;
    case K_DOWN:
      if (!s_selectedOutput) s_selectedOutput = true;
      else if (s_selected < n - 1) { s_selected++; s_selectedOutput = false; }
      else s_selected = -1;
      return true;
    case K_EXE: {
      const char * in, * out;
      s_host->historyGet(s_selected, &in, &out);
      s_selected = -1;
      insertText(s_selectedOutput ? out : in);
      return true;
    }
    case K_BACK: s_selected = -1; return true;
    case K_DEL:
      s_host->historyRemove(s_selected);
      n = s_host->historyCount();
      if (s_selected >= n) s_selected = n - 1;
      return true;
    case K_LEFT: case K_RIGHT: return true;
    default:
      // typing while a calculation is selected goes back to the input field
      s_selected = -1;
      break;
    }
  }
  switch (e.key) {
  case K_CHAR:
    if (e.cp == '/') insertFraction();
    else if (e.cp == '^') insertSup(0);
    else insertCp(e.cp);
    return true;
  case K_TEXT: insertText(e.text); return true;
  case K_FRAC: insertFraction(); return true;
  case K_POW: insertSup(0); return true;
  case K_SQUARE: insertSup("2"); return true;
  case K_SQRT: insertRoot(false); return true;
  case K_NROOT: insertRoot(true); return true;
  case K_EXP: insertCp(0x212f); insertSup(0); return true;
  case K_TENPOW: insertCp(0xd7); insertCp('1'); insertCp('0'); insertSup(0); return true;
  case K_ABS: insertAbs(); return true;
  case K_LEFT: moveLeft(); return true;
  case K_RIGHT: moveRight(); return true;
  case K_UP:
    if (moveVertical(true)) return true;
    if (s_host->historyCount() > 0) { s_selected = s_host->historyCount() - 1; s_selectedOutput = true; }
    return true;
  case K_DOWN: moveVertical(false); return true;
  case K_DEL: backspace(); return true;
  case K_AC: editorClear(); s_fieldScroll = 0; return true;
  case K_EXE: evaluateInput(); return true;
  case K_BACK: return false;   // let KhiCAS handle EXIT on an idle screen
  }
  return false;
}

/* ---- drawing the screen ---- */

static void drawTitleBar() {
  fillRect(0, 0, SCREEN_W, TITLE_H, C_MAUVE);
  const UpsFont & f = ups_small;
  int y = (TITLE_H - f.h) / 2;
  const char * title = "MONOLITH";
  drawText(f, title, (SCREEN_W - textWidth(f, title)) / 2, y, C_CRUST);
  if (s_host->titleLeft) { const char * l = s_host->titleLeft(); if (l) drawText(f, l, 5, y, C_CRUST); }
  if (s_host->titleRight) { const char * r = s_host->titleRight(); if (r) drawText(f, r, SCREEN_W - 5 - textWidth(f, r), y, C_CRUST); }
}

struct CellGeom { int h; bool single; int inY, outY; };

static CellGeom cellGeometry(const Node & in, const Node & out) {
  CellGeom g;
  int avail = SCREEN_W - 2 * CELL_HMARGIN;
  g.single = in.w + out.w + 2 * CELL_HMARGIN <= avail;
  if (g.single) {
    int diff = out.base - in.base;
    g.inY = CELL_MARGIN + (diff > 0 ? diff : 0);
    g.outY = CELL_MARGIN + (diff < 0 ? -diff : 0);
    int bottom = g.inY + in.h > g.outY + out.h ? g.inY + in.h : g.outY + out.h;
    g.h = bottom + CELL_MARGIN;
  } else {
    g.inY = CELL_MARGIN;
    g.outY = CELL_MARGIN + in.h + CELL_MARGIN;
    g.h = g.outY + out.h + CELL_MARGIN;
  }
  return g;
}

// Draw calculation i with its bottom at y; returns its height
static int drawCell(int i, int bottom, int top) {
  const char * inText, * outText;
  s_host->historyGet(i, &inText, &outText);
  s_scratch.clear();
  int in = buildLayout(s_scratch, inText);
  int out = buildLayout(s_scratch, outText);
  if (in < 0 || out < 0) return 0;
  measure(s_scratch, in, false); measure(s_scratch, out, false);
  Node & IN = s_scratch.n[in]; Node & OUT = s_scratch.n[out];
  CellGeom g = cellGeometry(IN, OUT);
  int y = bottom - g.h;
  if (y < bottom && bottom > top) {
    setClip(0, top, SCREEN_W, bottom);
    unsigned short bg = (i & 1) ? C_MANTLE : C_BASE;
    fillRect(0, y, SCREEN_W, g.h, bg);
    int inX = CELL_HMARGIN;
    int outX = SCREEN_W - CELL_HMARGIN - OUT.w;
    if (outX < CELL_HMARGIN) outX = CELL_HMARGIN;
    if (s_selected == i) {
      if (s_selectedOutput) fillRect(outX - 4, y + g.outY - 2, OUT.w + 8, OUT.h + 4, C_SURFACE1);
      else fillRect(inX - 4, y + g.inY - 2, IN.w + 8, IN.h + 4, C_SURFACE1);
    }
    setClip(0, top > y ? top : y, SCREEN_W - CELL_HMARGIN / 2, bottom);
    drawNode(s_scratch, in, inX, y + g.inY, C_TEXT, false);
    drawNode(s_scratch, out, outX, y + g.outY, C_TEXT, false);
    resetClip();
  }
  return g.h;
}

static int drawInputField() {
  Pool & P = s_edit;
  measure(P, s_root, false);
  Node & r = P.n[s_root];
  int h = r.h + 2 * FIELD_MARGIN;
  if (h < FIELD_MIN_H) h = FIELD_MIN_H;
  if (h > FIELD_MAX_H) h = FIELD_MAX_H;
  int top = SCREEN_H - h;
  fillRect(0, top, SCREEN_W, h, C_BASE);
  fillRect(0, top, SCREEN_W, 1, C_SURFACE0);   // ExpressionInputBorder
  // keep the cursor visible
  int cx = cursorX();
  int viewW = SCREEN_W - 2 * FIELD_MARGIN - 2;
  if (cx - s_fieldScroll > viewW) s_fieldScroll = cx - viewW;
  if (cx - s_fieldScroll < 0) s_fieldScroll = cx;
  if (r.w <= viewW) s_fieldScroll = 0;
  int x0 = FIELD_MARGIN - s_fieldScroll;
  int y0 = top + 1 + (h - 1 - r.h) / 2;
  setClip(0, top + 1, SCREEN_W, SCREEN_H);
  drawNode(P, s_root, x0, y0, C_TEXT, false);
  // cursor
  if (s_selected < 0 && !s_toolbox) {
    int ax, ay;
    absolutePos(P, s_curRow, s_root, &ax, &ay);
    Node & row = P.n[s_curRow];
    int ch = row.h, cy = ay;
    if (s_curAfter >= 0) {
      Node & it = P.n[s_curAfter];
      cx = ax + it.x + it.w;
      // cursor as tall as the neighbouring item
      ch = it.h; cy = ay + it.y;
    } else {
      cx = ax;
      int nx = row.first;
      if (nx >= 0) { ch = P.n[nx].h; cy = ay + P.n[nx].y; }
    }
    if (ch < GLYPH_H) { cy -= (GLYPH_H - ch) / 2; ch = GLYPH_H; }
    fillRect(x0 + cx, y0 + cy, 1, ch, C_TEXT);
  }
  resetClip();
  return top;
}

static void drawToolbox() {
  int top = TITLE_H;
  const MenuItem * items = s_menuStack[s_menuDepth];
  int n = s_menuCount[s_menuDepth], sel = s_menuSel[s_menuDepth];
  // header: name of the submenu, as Upsilon's toolbox
  fillRect(0, top, SCREEN_W, TOOLBOX_HEADER_H, C_SURFACE1);
  const char * title = s_menuDepth == 0 ? "Toolbox" : s_menuStack[s_menuDepth - 1][s_menuSel[s_menuDepth - 1]].label;
  drawText(ups_small, title, (SCREEN_W - textWidth(ups_small, title)) / 2, top + (TOOLBOX_HEADER_H - ups_small.h) / 2, C_TEXT);
  top += TOOLBOX_HEADER_H;
  fillRect(0, top, SCREEN_W, SCREEN_H - top, C_MANTLE);
  // rows: label in the large font, help in the small font below
  const int rowH = 40, singleH = 30;
  int visibleTop = top;
  // scroll so the selection is visible
  int ySel = 0;
  for (int k = 0; k < sel; k++) ySel += items[k].help ? rowH : singleH;
  int selH = items[sel].help ? rowH : singleH;
  int scroll = 0, area = SCREEN_H - visibleTop;
  if (ySel + selH > area) scroll = ySel + selH - area;
  int y = visibleTop - scroll;
  setClip(0, visibleTop, SCREEN_W, SCREEN_H);
  for (int k = 0; k < n; k++) {
    int h = items[k].help ? rowH : singleH;
    if (y + h > visibleTop && y < SCREEN_H) {
      fillRect(0, y, SCREEN_W, h - 1, k == sel ? C_SURFACE1 : C_BASE);
      fillRect(0, y + h - 1, SCREEN_W, 1, C_MANTLE);
      if (items[k].help) {
        drawText(ups_large, items[k].label, 10, y + 3, C_TEXT);
        drawText(ups_small, items[k].help, 10, y + 3 + GLYPH_H + 2, C_OVERLAY0);
      } else {
        drawText(ups_large, items[k].label, 10, y + (h - GLYPH_H) / 2, C_TEXT);
      }
      if (items[k].children) {   // chevron
        int cx = SCREEN_W - 16, cy = y + h / 2;
        for (int d = 0; d < 4; d++) { fillRect(cx + d, cy - 4 + d, 1, 1, C_TEXT); fillRect(cx + d, cy + 4 - d, 1, 1, C_TEXT); }
      }
    }
    y += h;
  }
  resetClip();
}

void draw() {
  resetClip();
  drawTitleBar();
  if (s_toolbox) { drawToolbox(); return; }
  int fieldTop = drawInputField();
  // history, newest at the bottom
  int n = s_host->historyCount();
  int areaTop = TITLE_H;
  fillRect(0, areaTop, SCREEN_W, fieldTop - areaTop, C_BASE);
  // scroll so that the selected calculation is visible
  int bottom = fieldTop;
  if (s_selected >= 0) {
    // heights of the calculations below the selection
    int below = 0;
    for (int i = n - 1; i > s_selected; i--) {
      const char * a, * b; s_host->historyGet(i, &a, &b);
      s_scratch.clear();
      int in = buildLayout(s_scratch, a), out = buildLayout(s_scratch, b);
      if (in < 0 || out < 0) break;
      measure(s_scratch, in, false); measure(s_scratch, out, false);
      below += cellGeometry(s_scratch.n[in], s_scratch.n[out]).h;
    }
    const char * a, * b; s_host->historyGet(s_selected, &a, &b);
    s_scratch.clear();
    int in = buildLayout(s_scratch, a), out = buildLayout(s_scratch, b);
    int selH = 0;
    if (in >= 0 && out >= 0) { measure(s_scratch, in, false); measure(s_scratch, out, false); selH = cellGeometry(s_scratch.n[in], s_scratch.n[out]).h; }
    int need = below + selH - (fieldTop - areaTop);
    if (need > 0) bottom += need;
    if (selH > fieldTop - areaTop) bottom -= selH - (fieldTop - areaTop);   // tall cell: show its top
  }
  for (int i = n - 1; i >= 0 && bottom > areaTop; i--) {
    int h = drawCell(i, bottom, areaTop);
    if (h <= 0) break;
    bottom -= h;
  }
  resetClip();
}

}
