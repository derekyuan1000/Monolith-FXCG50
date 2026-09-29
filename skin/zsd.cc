/* S<->D result-form pop-up (Monolith M2 task 6): the pure, host-testable half. See zsd.h for the design
 * write-up (which form maps to which command, and why). This file has no giac/fxcg/ustl dependency at all — it
 * is plain string handling, on purpose, so it can be compiled and run on the host exactly like skin/zstack_rules.c
 * (the pure half of stacked results). The modal pop-up itself (drawing, in_ckgetkey, giac::history_out, the
 * KEY_CTRL_SD fork hook) is skin/zsd_ui.cc, a separate file for the same reason zstack.cc is separate from
 * zstack_rules.c: that file needs giac/fxcg headers, which would make this one unbuildable on the host.
 * Compiled as zsd.o (.rominram, see tools/fork_objs.txt). */
#include <string.h>
#include "zsd.h"

const char * const zsd_form_label[ZSD_FORM_COUNT] = {
  "Exact",
  "Decimal",
  "Fraction",
  "Mixed fraction",
  "Root/pi form",
  "Scientific",
};

const char * const zsd_form_label_fr[ZSD_FORM_COUNT] = {
  "Exact",
  "Decimal",
  "Fraction",
  "Fraction mixte",
  "Forme racine/pi",
  "Scientifique",
};

int zsd_command_for(int form, const char *expr_str, char *buf, unsigned size)
{
  const char *pre, *post;
  unsigned need;
  if (!buf || size == 0 || !expr_str || !expr_str[0]) return -1;
  buf[0] = 0;
  switch (form) {
    case ZSD_EXACT:    pre = "exact(";           post = ")";       break;
    case ZSD_DECIMAL:  pre = "evalf(";           post = ")";       break;
    case ZSD_FRACTION: pre = "exact(";           post = ")";       break;   /* documented in zsd.h: same as Exact */
    case ZSD_MIXED:    pre = "propfrac(exact(";  post = "))";      break;
    case ZSD_ROOTPI:   pre = "simplify(exact(";  post = "))";      break;
    case ZSD_SCI:      pre = "format(evalf(";    post = "),\"e3\")"; break;   /* scientific, NOT engineering: see zsd.h */
    default: return -1;
  }
  need = (unsigned)strlen(pre) + (unsigned)strlen(expr_str) + (unsigned)strlen(post);
  if (need >= size) return -1;   /* buf already zeroed above: never a partial/truncated command */
  strcpy(buf, pre);
  strcat(buf, expr_str);
  strcat(buf, post);
  return (int)need;
}
