# Core-loop acceptance checklist

Run on the fx-CG50 (OS 3.70 or 3.80.1, not 3.80.0) with a `-Prof` build, first `.\build.ps1 -Core -Prof`
(no overlays, 1D history), then `.\build.ps1 -Prof` (everything on). Install the `.g3a` **and** the `.ac2` from
the same build.

A row **fails** on any of: flicker, a blank frame, a stale or missing character, the cursor in the wrong place,
or a status bar / Shift-Alpha indicator that lags a key. Note the `F` value (screen flips for that key) when it
is not 1.

| # | Action | Expected |
|---|---|---|
| 1 | Type `123456789+` five times (runs past one line) | each character appears on its own keypress; the line scrolls horizontally; the cursor stays visible; F1 |
| 2 | ← ×10, type `x`, then → to the end | `x` inserted at the cursor; untouched history does not visibly redraw; F1 |
| 3 | DEL ×5 in the middle of the line, then at the start | the right characters go; DEL at the start does nothing |
| 4 | SHIFT, SHIFT; ALPHA, ALPHA; SHIFT then ALPHA (lock), ALPHA | the indicator changes on every press, no lag; F1 each |
| 5 | EXE on `2+3` | input moves to history, `5` right-aligned, a new empty line; F1 |
| 6 | EXE on an empty line | nothing breaks, no blank frame |
| 7 | EXE on `2+` (syntax error) | the error shows once; the line is editable again |
| 8 | EXE `1` thirty times (history taller than the screen), then ↑ ×30, ↓ ×30 | selection and scrollbar track the cursor, no tearing |
| 9 | Hold a digit key for 3 s (autorepeat) | no dropped or doubled characters; F stays 1 |
| 10 | `for(k:=0;k<200;k++) print(k)` | the numbers scroll **while** it runs, not only at the end |
| 11 | `factor(2^200+1)`, then AC/ON while it runs | the input is already in history while computing; AC/ON interrupts; the shell redraws fully |
| 12 | MENU → home screen → back into Monolith | the screen is fully drawn on return, same session |
| 13 | Leave it idle until auto power-off, then turn it on | the shell comes back fully drawn |

Full build only (`-Prof` without `-Core`):

| # | Action | Expected |
|---|---|---|
| 14 | `a b/c`, type `1`, ↓, `2`, EXE | 2D fraction editor; each key F1; result `1/2` (or `0.5`) |
| 15 | S⇔D with an empty history | "No result yet" stays in the status bar until the next key |
| 16 | `1/3` EXE, then S⇔D, ↓, EXE | pop-up moves with each key (F1); the chosen form is evaluated |
| 17 | F4 MATH → EXIT; F1 JUMP → EXIT; F2 DELETE → DEL-ALL → No | each sub-bar opens and closes with F1, no flash of the shell's own tabs |
| 18 | Shift+F3 (pinned) → ↑/↓ → EXIT | list moves with F1 |

## Runs

<!-- Copy this block per run: date, build (-Core -Prof / -Prof), then PASS/FAIL + note per row. -->
