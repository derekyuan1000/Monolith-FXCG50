/* S<->D result-form pop-up (Monolith M2 task 6). See skin/zsd.cc and docs/KEY_CONFLICTS.md section 3 (S<->D row)
 * for the design write-up. This header holds only the pure, host-testable part: the enum of forms and the
 * choice-to-command mapping. The modal pop-up itself (zsd_open, gfx drawing, giac::history_out check) lives in
 * zsd.cc and is declared where it is used (console.cc), the same way khicas_addins_menu is, because it needs
 * giac/fxcg headers this file must not pull in (tests build this header alone on the host, no giac, no fxcg).
 *
 * Decision (documented, not asked, per the plan's standing instruction): the six forms below. Each maps to a
 * wrapper around the last result. "The last result" is Giac's own ans() (history_out's top), not a re-parsed copy
 * of the last OUTPUT line's string: zstack.cc already has to carefully filter "which OUTPUT line is a genuine Giac
 * result vs. program print() text" (zstack_console_layout's k<1/Last_Line/neighbour-type checks); duplicating that
 * filter here to recover a string would be extra code for no gain when Giac already exposes the same value through
 * ans(), which is exactly what the existing Ans key already types (console.cc keytostring: "ans()").
 *
 *   ZSD_EXACT    exact(ans())                 exact/symbolic form
 *   ZSD_DECIMAL  evalf(ans())                 decimal approximation
 *   ZSD_FRACTION exact(ans())                 SAME command as Exact: grep of static_extern.h / static_help.h in
 *                                              the fork tree found at_exact and at_evalf but no at_fraction or
 *                                              "fraction(" builtin distinct from exact(); exact() already produces
 *                                              the a/b form for a rational. Kept as its own row anyway (matching
 *                                              the brief's requested list of forms one-for-one) rather than
 *                                              silently dropping it, since a real S<->D-style list plausibly
 *                                              separates "the exact value" from "as a fraction" even when, for a
 *                                              plain scalar on this build, they resolve to the same text.
 *   ZSD_MIXED    propfrac(exact(ans()))       a + b/c form: Giac's propfrac (static_help.h "propFrac": "Simplifies
 *                                              and writes the fraction A/B as Q+R/B"); exact() first so a decimal
 *                                              last result is turned into a fraction before propfrac sees it.
 *   ZSD_ROOTPI   simplify(exact(ans()))       keeps sqrt/pi symbolic. Documented limitation: exact() cannot invert
 *                                              an evalf'd float back into a radical from scratch (recognising e.g.
 *                                              1.414213562 as sqrt(2) is a much harder, open-ended problem); this
 *                                              form only re-simplifies an expression that is already exact, which
 *                                              covers the common case (a result that already contains sqrt/pi but
 *                                              in an unreduced shape).
 *   ZSD_SCI      format(evalf(ans()),"e3")    scientific notation, 3 significant digits after the point.
 *                                              RENAMED from "Engineering"/ZSD_ENG (fix round 1, 2026-09-27):
 *                                              static_help.h's own doc string for format()'s "e" mode ("e =
 *                                              ingenieur"/"engineer") is misleading relative to what the code
 *                                              actually does. Traced in the fork: format(gen,string,ctx)
 *                                              (zti89.cc) sets format_double(contextptr)="e3" and calls
 *                                              print_DOUBLE_ (kgen.cc print_DOUBLE_, around line 11780), which
 *                                              with a non-empty forme takes the early branch (around line
 *                                              11803-11812): plain sprintfdouble(s,"%.3e",d), i.e. ordinary
 *                                              scientific notation (exponent NOT forced to a multiple of 3).
 *                                              The real engineering-notation branch (ndigits = 3*(ndigits/3),
 *                                              around line 11829-11838) is gated on scientific_format(contextptr)
 *                                              == 2, reachable only when forme is EMPTY, driven by the separate
 *                                              scientific_format(2) command (kprog.cc), never through the
 *                                              two-arg format() call. Reaching the real branch means mutating
 *                                              scientific_format for the duration of one evaluation and
 *                                              restoring it after, which is a global (not per-call) context
 *                                              flag: doing that safely around a single Console_NewLine
 *                                              evaluation (not a direct C++ call this file controls end to end)
 *                                              risks leaving the flag changed if anything goes wrong mid-
 *                                              evaluation, silently altering how every OTHER decimal result
 *                                              displays for the rest of the session. Not attempted: this row is
 *                                              honestly relabelled "Scientific" instead, matching what the
 *                                              command actually produces, rather than chasing true engineering
 *                                              notation. If a future task wants real engineering notation, the
 *                                              safe route is a dedicated Giac call that takes the mode as an
 *                                              argument (none was found in this fork), not context mutation.
 */
#ifndef ZSD_H
#define ZSD_H

enum {
  ZSD_EXACT = 0,
  ZSD_DECIMAL,
  ZSD_FRACTION,
  ZSD_MIXED,
  ZSD_ROOTPI,
  ZSD_SCI,      /* was ZSD_ENG ("Engineering"): renamed, see the design note above */
  ZSD_FORM_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif

/* Row labels, ZSD_FORM_COUNT entries, English and French (docs: status messages keep French variants), same
 * order as the enum. Both arrays are fully populated (no NULL entries). `const char * const`, not `const char *`:
 * the pointers themselves never change, so the array belongs in rom (.rodata) rather than static ram (.data) --
 * static ram is this target's tightest budget (docs/BUDGET.md). */
extern const char * const zsd_form_label[ZSD_FORM_COUNT];
extern const char * const zsd_form_label_fr[ZSD_FORM_COUNT];

/* Writes the wrapper command for form applied to expr_str (e.g. "ans()") into buf (size bytes, NUL-terminated).
 * Returns strlen(buf) (>= 0) on success; -1 when form is out of range, expr_str is NULL/empty, or buf is too
 * small to hold the result (buf[0] is set to 0 first when size >= 1, so a truncation is never mistaken for a
 * usable string). Pure: no Giac call, no I/O; host-testable. */
int zsd_command_for(int form, const char *expr_str, char *buf, unsigned size);

#ifdef __cplusplus
}
#endif
#endif
