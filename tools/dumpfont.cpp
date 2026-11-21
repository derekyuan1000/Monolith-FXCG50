// Generates src/ups_fonts.h: Upsilon's LargeFont/SmallFont glyphs (Source Code Pro,
// rasterised by Upsilon's build, SIL OFL 1.1) as 4-bit alpha tables.
// Needs a built Upsilon tree (simulator, linux); link against its kandinsky objects:
//   U=~/Upsilon; O=$U/output/release/simulator/linux
//   g++ -std=c++11 -I$U/kandinsky/include -I$U/ion/include -I$O -I$U tools/dumpfont.cpp \
//     $O/kandinsky/src/{font,color,point}.o $O/kandinsky/fonts/*FontExtended.o \
//     $O/ion/src/shared/decompress.o $O/ion/src/shared/unicode/utf8_*.o $(find $O -name 'lz4*.o') -o dumpfont
//   ./dumpfont && mv ups_fonts.h src/
#include <kandinsky/font.h>
#include <stdio.h>
#include <stdint.h>
static void dump(const KDFont * f, const char * name, const uint32_t * cps, int n, FILE * out) {
  int w = f->glyphSize().width(), h = f->glyphSize().height();
  fprintf(out, "static const unsigned char %s_data[%d][%d] = {\n", name, n, (w*h+1)/2);
  KDFont::GlyphBuffer buf;
  for (int i = 0; i < n; i++) {
    f->setGlyphGrayscalesForCodePoint(cps[i], &buf);
    fprintf(out, "{");
    // already packed: 2 pixels per byte, first pixel in the high nibble
    for (int p = 0; p < (w*h+1)/2; p++) fprintf(out, "0x%02x,", buf.grayscaleBuffer()[p]);
    fprintf(out, "},\n");
  }
  fprintf(out, "};\nstatic const UpsFont %s = {%d, %d, %s_data[0]};\n", name, w, h, name);
}
int main() {
  // Code points the UI needs: printable ASCII + maths symbols
  static uint32_t cps[200]; int n = 0;
  for (uint32_t c = 0x20; c < 0x7f; c++) cps[n++] = c;
  const uint32_t extra[] = {0xd7 /*×*/, 0xb7 /*·*/, 0x3c0 /*π*/, 0x221a /*√*/, 0x2212 /*−*/, 0x2264, 0x2265, 0x2260, 0x3b8 /*θ*/,
    0x222b /*∫*/, 0x2211 /*∑*/, 0x220f /*∏*/, 0x2248 /*≈*/, 0x221e /*∞*/, 0x2192 /*→*/, 0x2032 /*′*/, 0xb0 /*°*/, 0x3b1, 0x3b2, 0x3bb, 0x3bc, 0x3c3, 0x3c6, 0x3c9, 0x394 /*Δ*/, 0x2026 /*…*/,
    0x212f /*ℯ*/, 0x1d422 /*𝐢*/, 0x25b8 /*▸*/, 0x2023, 0x2190, 0x2191, 0x2193};
  for (uint32_t e : extra) cps[n++] = e;
  FILE * out = fopen("ups_fonts.h", "w");
  fprintf(out, "/* Generated from Upsilon's fonts (kandinsky) by tools/dumpfont.cpp. 4-bit alpha, 2 pixels per byte */\n");
  fprintf(out, "static const unsigned int ups_font_codepoints[%d] = {", n);
  for (int i = 0; i < n; i++) fprintf(out, "0x%x,", cps[i]);
  fprintf(out, "};\n#define UPS_FONT_GLYPHS %d\n", n);
  dump(KDFont::LargeFont, "ups_large", cps, n, out);
  dump(KDFont::SmallFont, "ups_small", cps, n, out);
  fclose(out);
  printf("large %dx%d small %dx%d glyphs %d\n", KDFont::LargeFont->glyphSize().width(), KDFont::LargeFont->glyphSize().height(), KDFont::SmallFont->glyphSize().width(), KDFont::SmallFont->glyphSize().height(), n);
}
