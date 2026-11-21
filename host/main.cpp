/* PC harness for the Calculation screen (src/zupsui.cpp): runs key scripts
 * and writes screenshots (PPM), so the UI can be developed without the calculator.
 *   ./upshost script.txt out_prefix
 * Script lines: "type 1/2+x", "key LEFT", "shot name", "text integrate(\x11,x)".
 * Evaluation is faked with a table of giac answers. */
#include "../src/upsui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned short fb[ups::SCREEN_W * ups::SCREEN_H];

static char hin[64][512], hout[64][512];
static int hn = 0;

struct Canned { const char * in; const char * out; };
static const Canned canned[] = {
  {"factor(x^(2)-1)", "(x-1)*(x+1)"},
  {"((1)/(2))+((1)/(3))", "5/6"},
  {"integrate(((1)/(x^(2)+1)),x)", "atan(x)"},
  {"integrate(((1)/(x^(2)-1)),x)", "1/2*ln(abs(x-1))-1/2*ln(abs(x+1))"},
  {"solve(x^(2)=9,x)", "[-3,3]"},
  {"sqrt(8)", "2*sqrt(2)"},
  {"desolve(y'+y=0,y)", "c_0*exp(-x)"},
  {"[[1,2],[3,4]]^(-1)", "[[-2,1],[3/2,-1/2]]"},
  {"limit(((sin(x))/(x)),x,0)", "1"},
  {"sum(((1)/(k^(2))),k,1,inf)", "pi^2/6"},
  {"taylor(exp(x),x,0,4)", "1+x+x^2/2+x^3/6+x^4/24+x^5*order_size(x)"},
  {"2^(100)", "1267650600228229401496703205376"},
  {"evalf(pi)", "3.14159265359"},
  {"1e-7*3", "3e-07"},
  {0, 0}};

static bool fakeEval(const char * input, char * out, int size) {
  for (int k = 0; canned[k].in; k++)
    if (strcmp(canned[k].in, input) == 0) { snprintf(out, size, "%s", canned[k].out); return true; }
  snprintf(out, size, "%s", input);   // echo
  return true;
}
static int hCount() { return hn; }
static void hGet(int i, const char ** a, const char ** b) { *a = hin[i]; *b = hout[i]; }
static void hAdd(const char * a, const char * b) {
  if (hn == 64) { memmove(hin, hin + 1, sizeof(hin[0]) * 63); memmove(hout, hout + 1, sizeof(hout[0]) * 63); hn--; }
  snprintf(hin[hn], 512, "%s", a); snprintf(hout[hn], 512, "%s", b); hn++;
  printf("  eval: %s  ->  %s\n", a, b);
}
static void hRemove(int i) { for (int k = i; k < hn - 1; k++) { strcpy(hin[k], hin[k + 1]); strcpy(hout[k], hout[k + 1]); } hn--; }
static const char * tLeft() { return "xcas  rad"; }
static const char * tRight() { return "20:14"; }

static const ups::MenuItem calculusMenu[] = {
  {"diff(f,x)", "Derivative", "diff(\x11,x)", 0, 0},
  {"integrate(f,x)", "Antiderivative", "integrate(\x11,x)", 0, 0},
  {"integrate(f,x,a,b)", "Definite integral", "integrate(\x11,x,,)", 0, 0},
  {"limit(f,x,a)", "Limit", "limit(\x11,x,)", 0, 0},
  {"sum(f,k,a,b)", "Sum", "sum(\x11,k,,)", 0, 0},
  {"taylor(f,x,a,n)", "Taylor expansion", "taylor(\x11,x,0,)", 0, 0},
  {"desolve(eq,y)", "Differential equation", "desolve(\x11,y)", 0, 0},
};
static const ups::MenuItem algebraMenu[] = {
  {"factor(p)", "Factorise", "factor(\x11)", 0, 0},
  {"expand(e)", "Expand", "expand(\x11)", 0, 0},
  {"simplify(e)", "Simplify", "simplify(\x11)", 0, 0},
  {"partfrac(f)", "Partial fractions", "partfrac(\x11)", 0, 0},
  {"solve(eq,x)", "Solve an equation", "solve(\x11,x)", 0, 0},
};
static const ups::MenuItem rootMenu[] = {
  {"Algebra", 0, 0, algebraMenu, 5},
  {"Calculus", 0, 0, calculusMenu, 7},
  {"Matrices", 0, 0, 0, 0},
  {"Complex numbers", 0, 0, 0, 0},
  {"Probability", 0, 0, 0, 0},
  {"Catalogue", "All commands", 0, 0, 0},
  {"File and settings", 0, 0, 0, 0},
};

static void shot(const char * prefix, const char * name) {
  ups::draw();
  char path[256]; snprintf(path, sizeof(path), "%s%s.ppm", prefix, name);
  FILE * f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", ups::SCREEN_W, ups::SCREEN_H);
  for (int i = 0; i < ups::SCREEN_W * ups::SCREEN_H; i++) {
    unsigned short c = fb[i];
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  char buf[512]; ups::serializeInput(buf, sizeof(buf));
  printf("shot %s  input=[%s]\n", name, buf);
}

static int keyByName(const char * n) {
  struct { const char * n; int k; } t[] = {
    {"LEFT", ups::K_LEFT}, {"RIGHT", ups::K_RIGHT}, {"UP", ups::K_UP}, {"DOWN", ups::K_DOWN},
    {"DEL", ups::K_DEL}, {"AC", ups::K_AC}, {"EXE", ups::K_EXE}, {"BACK", ups::K_BACK},
    {"FRAC", ups::K_FRAC}, {"POW", ups::K_POW}, {"SQUARE", ups::K_SQUARE}, {"SQRT", ups::K_SQRT},
    {"NROOT", ups::K_NROOT}, {"EXP", ups::K_EXP}, {"TENPOW", ups::K_TENPOW}, {"ABS", ups::K_ABS},
    {"OPTN", ups::K_TOOLBOX}, {0, 0}};
  for (int i = 0; t[i].n; i++) if (strcmp(t[i].n, n) == 0) return t[i].k;
  fprintf(stderr, "unknown key %s\n", n); exit(1);
}

int main(int argc, char ** argv) {
  ups::Host host = {fakeEval, hCount, hGet, hAdd, hRemove, tLeft, tRight, 0, 0};
  ups::init(fb, &host, rootMenu, 7);
  if (argc < 3) return 1;
  FILE * s = fopen(argv[1], "r");
  char line[1024];
  while (fgets(line, sizeof(line), s)) {
    line[strcspn(line, "\n")] = 0;
    if (!line[0] || line[0] == '#') continue;
    char * arg = strchr(line, ' ');
    if (arg) *arg++ = 0; else arg = (char *)"";
    if (!strcmp(line, "type")) {
      // each UTF-8 character becomes a K_CHAR; ^ and / become boxes, as the keys do
      const char * p = arg;
      while (*p) {
        const unsigned char * u = (const unsigned char *)p;
        unsigned cp; int n = 1;
        if (u[0] < 0x80) cp = u[0];
        else if ((u[0] & 0xE0) == 0xC0) { cp = ((u[0] & 31) << 6) | (u[1] & 63); n = 2; }
        else { cp = ((u[0] & 15) << 12) | ((u[1] & 63) << 6) | (u[2] & 63); n = 3; }
        p += n;
        ups::Event e = {ups::K_CHAR, cp, 0};
        ups::handle(e);
      }
    } else if (!strcmp(line, "text")) {
      static char buf[512]; int j = 0;
      for (const char * p = arg; *p; p++) { if (p[0] == '\\' && p[1] == 'x' && p[2] == '1' && p[3] == '1') { buf[j++] = '\x11'; p += 3; } else buf[j++] = *p; }
      buf[j] = 0;
      ups::Event e = {ups::K_TEXT, 0, buf}; ups::handle(e);
    } else if (!strcmp(line, "key")) {
      char * tok = strtok(arg, " ");
      while (tok) { ups::Event e = {keyByName(tok), 0, 0}; ups::handle(e); tok = strtok(0, " "); }
    } else if (!strcmp(line, "shot")) {
      shot(argv[2], arg);
    } else if (!strcmp(line, "roundtrip")) {
      char buf[1024]; ups::layoutRoundTrip(arg, buf, sizeof(buf));
      printf("roundtrip %s -> %s\n", arg, buf);
    } else if (!strcmp(line, "expect")) {
      char buf[1024]; ups::serializeInput(buf, sizeof(buf));
      if (strcmp(buf, arg)) { printf("FAIL expected [%s] got [%s]\n", arg, buf); }
      else printf("ok   [%s]\n", buf);
    } else {
      fprintf(stderr, "bad line %s\n", line); return 1;
    }
  }
  return 0;
}
