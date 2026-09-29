#ifndef ZNAT_H
#define ZNAT_H
/* Natural-input (RUN-MAT style) template editor for the Monolith shell (milestone M1, extended M2 task 5).
 * Modal: znat_edit()/znat_edit_template() take over the keyboard until the user presses EXE (evaluate), EXIT (back
 * to the linear editor with the text) or, after an error, EXIT twice (cancel).
 *
 * Return value = what Console_GetKey would return for the key that opened the editor:
 *   CONSOLE_NEW_LINE_SET  the expression was entered (EXE); the caller returns it to Console_GetLine;
 *   CONSOLE_SUCCEEDED     back in the linear editor (Edit_Line holds the serialised text or is unchanged);
 *   ZNAT_NOT_HANDLED      the editor could not open (the current line does not fit the model, or the requested
 *                         template does not fit the panel/pool): the caller must run the stock eqws() editor
 *                         instead. Nothing was changed. */
#define ZNAT_NOT_HANDLED (-1000)

/* Which template (if any) znat_edit_template() inserts as soon as the editor opens, capturing the atom right
 * before the caret the same way the fraction key always has (M2 task 5, docs/KEY_CONFLICTS.md section 2/3).
 * ZNAT_TPL_NONE opens the editor on the current text with no insert at all (kept for a future plain "edit in
 * natural input" key; nothing in the shell uses it yet). ZNAT_TPL_FRAC is what znat_edit() (no argument) has
 * always done, so old callers keep working unchanged. */
enum {
  ZNAT_TPL_NONE = 0,
  ZNAT_TPL_FRAC,       /* a b/c key: capture the number before the caret, same as before */
  ZNAT_TPL_POW,        /* ^ key: empty exponent, base captured */
  ZNAT_TPL_SQUARE,     /* x^2 key: exponent "2" already typed, caret after (nin_insert_square) */
  ZNAT_TPL_RECIP,      /* x^-1 key: exponent "-1" already typed, caret after (nin_insert_recip) */
  ZNAT_TPL_SQRT,       /* sqrt key */
  ZNAT_TPL_ABS,        /* Abs (MATH sub-bar, and the outer Abs key) */
  ZNAT_TPL_NROOT,      /* the n-th root key (MATH sub-bar has no separate entry: see KEY_CONFLICTS section 2) */
  ZNAT_TPL_LOGAB,      /* MATH sub-bar: logab */
  ZNAT_TPL_DERIV,      /* MATH sub-bar: d/dx */
  ZNAT_TPL_DERIV2,     /* MATH sub-bar: d2/dx2 */
  ZNAT_TPL_INTEGRAL,   /* MATH sub-bar: integral */
  ZNAT_TPL_SIGMA,      /* MATH sub-bar: sigma */
  ZNAT_TPL_MATRIX      /* MATH sub-bar: MAT/VCT, default 2x2 (docs/KEY_CONFLICTS.md) */
};

#ifdef __cplusplus
extern "C" {
#endif
int znat_edit(void);                 /* == znat_edit_template(ZNAT_TPL_FRAC); unchanged since M1 */
int znat_edit_template(int tpl_kind);
/* M2 task 7: the MATH sub-bar (znat.cc's static math_submenu), exposed so the bare shell's F4 key can offer it
 * directly (docs/KEY_CONFLICTS.md section 2, "not yet wired on the bare shell line" closed by this entry point).
 * Runs the sub-bar and returns the picked ZNAT_TPL_* (or ZNAT_TPL_NONE on EXIT with nothing picked), same
 * contract as the internal math_submenu the natural editor's own F4 key calls; the caller decides what to do
 * with the result (console.cc calls znat_edit_template on it, same as the editor's own F4 handler does). */
int znat_math_submenu(int menucolorbg);
#ifdef __cplusplus
}
#endif
#endif
