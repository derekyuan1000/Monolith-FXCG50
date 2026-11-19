# Flush Profiling, Core-Loop Freeze, then gint + Eigenmath Spike — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make basic typing glitch-free and measurably snappier on the current KhiCAS fork, then decide (with data) whether to rebuild Monolith on gint + Eigenmath.

**Architecture:** Part A works on the existing fork (`~/khicas-work/fork` + `skin/`). It routes every screen flip through one counted function, deletes the redundant flip in the shell's key loop, and puts every overlay behind a `MONO_CORE` build switch so the core loop can be stabilised on its own. Part B is a time-boxed spike in a new `next/` tree (gint/fxSDK + Eigenmath). It only re-implements the core loop and ends at a go/no-go gate. Part A's acceptance checklist is Part B's spec.

**Tech Stack:** sh3eb-elf GCC, KhiCAS/Giac, Casio syscalls (`Bdisp_PutDisp_DD`, `RTC_GetTicks`, `GetKeyWait_OS`); Part B: fxSDK, gint (`dupdate`, `getkey`), Eigenmath C sources.

**Spec:** the user's request of 2026-11-19 (this plan's three numbered goals), plus `README.md`'s feature table for the features at stake.

## Why this order

- Switching to gint does not fix the double flip on its own. The flip is a loop-structure bug, and a straight port would bring it along. Fixing it here first costs about a day and gives a baseline number to compare against.
- Switching to Eigenmath drops Giac. You lose Python/`FMENU.py`, Giac's typesetter (stacked 2D results), the matrix editor, the S⇔D forms and Giac's CAS coverage. That is a product decision, so Part B ends in a gate, not a merge.

## Findings that drive Part A (verified in the fork, 2026-11-19)

One ordinary keypress in the shell runs `Console_GetLine` (`console.cc:3788`):

1. `Console_GetKey` → `in_ckgetkey` (`main.cc:872-876`) redraws the status bar and tabs, then calls **`Bdisp_PutDisp_DD` (flip #1)**, then waits.
2. The key returns. `Console_GetLine` calls `Console_Disp` → `console_disp_status` (`console.cc:3472`) → **`Bdisp_PutDisp_DD` (flip #2)**.
3. The loop goes back to step 1, so **flip #1 runs again** on a frame that differs from #2 only by the status-bar redraw.

So each keypress costs **2 full-screen flips**, and the first one is wasted. Overlays have the same pattern: `znat.cc:121`, `zsd_ui.cc:63`, `zshellbar.cc:52` and `zshellbar.cc:323` each flip and then call `in_ckgetkey(...,1,...)`, which flips again. `g_suppress_status_flip` (patch 0015) works around the same root cause.

There is a second problem: `in_ckgetkey` calls `set_xcas_status()` before its flip, which overwrites any status message an overlay just drew (`znat.cc` `pending_msg`, `zsd_ui.cc:72`). Those messages show for one frame at most.

## Global Constraints

- Patch workflow: edit KhiCAS files in `~/khicas-work/fork`, commit there, run `wsl -d Ubuntu -- bash tools/export_patches.sh`, and commit the regenerated `patches/` here. Monolith-owned files go in `skin/`, `gfx/` and `nin/` in this repo.
- Build: `.\build.ps1` (`-Refork` after patch changes). Outputs: `build/fork/monolith.g3a` and `monolith.ac2`.
- New files are GPL-3.0-or-later, the same as the rest of `skin/`.
- No new user-visible features until Task A5's checklist passes 3 times in a row.
- Testing happens on the real fx-CG50 (OS 3.70 or 3.80.1, **not 3.80.0**). There is no host test harness. "Tests" here are the counters from Task A1 plus the manual checklist in Task A4.

## Review Focus

1. **Output during a long evaluation** (`print` in a loop, progress text). After Task A2 removes the flip from the input path, these must still appear live. Expected: text shows while the evaluation runs, not only when it ends.
2. **Shift / Alpha toggles.** `in_ckgetkey` loops with `continue` and must still flip, so the indicator updates on each press. Expected: one flip per toggle.
3. **Status messages from overlays** ("No result yet", fraction-too-big). Expected: they stay visible until the next key.
4. **Auto power-off and the MENU key while waiting** (`main.cc:883-915`). Expected: after resuming, the screen comes back fully drawn with no blank frame.
5. **The first frame after launch / session restore.** Expected: the shell is fully drawn before the first key wait, even though `Console_Disp` no longer flips.

---

## Part A — Current fork

### Task A1: Single counted flip function + profiling overlay

**Files:**
- Create: `skin/zflip.h`, `skin/zflip.c`
- Modify (fork): `main.cc` (every `Bdisp_PutDisp_DD`, including `sync_screen` at :619), `console.cc` (:932, :954, :3472), `kdisplay.cc:139`
- Modify (repo): `skin/znat.cc`, `skin/zsd_ui.cc`, `skin/zshellbar.cc` (every `Bdisp_PutDisp_DD`)
- Modify: `tools/fork_objs.txt` / `tools/build_fork.sh` so `zflip.o` is built and linked (follow how `zskin_scroll.c` is listed)

**Interfaces:**
- Produces: `void zflip(void);` (the only caller of `Bdisp_PutDisp_DD`), `void zflip_key_begin(void);`, `void zflip_key_end(void);`, `unsigned zflip_last_count(void);`, `int zflip_last_ticks(void);`

- [ ] **Step 1: Write `skin/zflip.h`**

```c
/* Monolith: every screen flip goes through zflip() so it can be counted (MONO_PROF builds). GPL-3.0-or-later */
#ifndef ZFLIP_H
#define ZFLIP_H
#ifdef __cplusplus
extern "C" {
#endif
void zflip(void);            /* Bdisp_PutDisp_DD + count */
void zflip_key_begin(void);  /* a key was just read */
void zflip_key_end(void);    /* about to wait for the next key */
unsigned zflip_last_count(void); /* flips between the last key and the following wait */
int zflip_last_ticks(void);      /* RTC ticks (1/128 s) for the same span */
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Write `skin/zflip.c`**

```c
#include "zflip.h"
#include <fxcg/display.h>
#include <fxcg/rtc.h>

static unsigned flips, last_flips;
static int t0, last_ticks;

void zflip(void) { ++flips; Bdisp_PutDisp_DD(); }
void zflip_key_begin(void) { flips = 0; t0 = RTC_GetTicks(); }
void zflip_key_end(void) { last_flips = flips; last_ticks = RTC_GetTicks() - t0; }
unsigned zflip_last_count(void) { return last_flips; }
int zflip_last_ticks(void) { return last_ticks; }
```

- [ ] **Step 3: Replace every flip.** In the fork run `grep -n "Bdisp_PutDisp_DD" *.c *.cc *.cpp` and change each call to `zflip()` (add `#include "zflip.h"`). Do the same in `skin/`. Afterwards `grep -rn Bdisp_PutDisp_DD` must list only `skin/zflip.c`.

- [ ] **Step 4: Hook the key boundaries in `in_ckgetkey`** (`main.cc`, the `while(1)` loop). Call `zflip_key_end();` just before `GetKeyWait_OS(...)` (after the waitforkey flip) and `zflip_key_begin();` right after it returns with `ret==1`.

- [ ] **Step 5: MONO_PROF readout.** At the end of `set_xcas_status()` (`skin/zskin_status.c`), add:

```c
#ifdef MONO_PROF
  { char b[24]; sprintf(b, "F%u T%d", zflip_last_count(), zflip_last_ticks());
    gfx_text(300, 2, b, GFX_BLACK, GFX_WHITE); }
#endif
```

Add `MONO_PROF=1` → `-DMONO_PROF` to `tools/build_fork.sh` CFLAGS, and a `-Prof` switch to `build.ps1` that passes it through.

- [ ] **Step 6: Build and measure the baseline.** Run `.\build.ps1 -Refork -Prof` and install. In the shell, type `1`, `2`, `+`, `3`, EXE, ←, DEL, F4 (MATH) → EXIT, `a b/c`. Write the F/T value after each key into a table in `docs/perf-baseline.md`. **Expected now:** `F2` for plain typing.

- [ ] **Step 7: Commit** the fork changes, then `export_patches.sh`, then here:

```bash
git add skin/zflip.* skin/zskin_status.c skin/znat.cc skin/zsd_ui.cc skin/zshellbar.cc tools/ build.ps1 patches/ docs/perf-baseline.md
git commit -m "perf: route all screen flips through zflip() and add MONO_PROF flip/tick counter"
```

### Task A2: One flip per keypress (input path presents only in `in_ckgetkey`)

**Files:**
- Modify (fork): `console.cc` (`console_disp_status` :3461-3473, `Console_GetLine` :3788-3795, the `g_suppress_status_flip` definition :67)
- Modify (repo): `skin/znat.cc`, `skin/zsd_ui.cc`, `skin/zshellbar.cc`

**Interfaces:**
- Consumes: `zflip()` from A1
- Produces: `extern int g_defer_flip;` (replaces `g_suppress_status_flip`; a nesting counter, not a bool). Rule: **code that draws and then calls `in_ckgetkey(...,waitforkey=1,...)` must not flip. `in_ckgetkey` presents.**

- [ ] **Step 1: Rename and generalise.** In `console.cc` change `int g_suppress_status_flip = 0;` to `int g_defer_flip = 0;`, and in `console_disp_status` change the line to `if (!g_defer_flip) zflip();`.

- [ ] **Step 2: Defer inside the input loop** (`Console_GetLine`):

```cpp
  do {
      return_val = Console_GetKey();
      ++g_defer_flip; Console_Disp(); --g_defer_flip;  /* in_ckgetkey flips before the next wait */
      if (return_val == CONSOLE_MEM_ERR) return NULL;
  } while (return_val != CONSOLE_NEW_LINE_SET);
```

Do the same at `Console_GetKey`'s `if (key==KEY_CTRL_EXIT) { Console_Disp(); continue; }` (:2473). Do **not** touch the other `Console_Disp()` callers. They are output paths during evaluation (Review Focus 1) and still need to flip.

- [ ] **Step 3: Overlays.** In `znat.cc` `redraw()`, `zsd_ui.cc` `zsd_draw()`, `zshellbar.cc` `draw_subbar()`/`math_submenu()`/the pinned loop: replace `g_suppress_status_flip = 1; Console_Disp(); g_suppress_status_flip = 0;` with `++g_defer_flip; Console_Disp(); --g_defer_flip;`. **Delete** the trailing `zflip()` when the next statement in that loop is `in_ckgetkey(..., 1, ...)`. Keep it where a non-waiting call follows (for example `zsd_open`'s early "No result yet" return).

- [ ] **Step 4: Fix status messages being overwritten (Review Focus 3).** Add `static const char *g_status_msg;` with `void zskin_status_msg(const char *m)` in `skin/zskin_status.c`. `set_xcas_status()` draws it via `DefineStatusMessage(m,1,0,0); DisplayStatusArea();` after the normal bar, and `zflip_key_begin()` clears it (call `zskin_status_msg(0)` there). Replace the direct `DefineStatusMessage`+`DisplayStatusArea` pairs in `znat.cc` and `zsd_ui.cc` with `zskin_status_msg(...)`.

- [ ] **Step 5: First frame (Review Focus 5).** Check that `restore_session`/startup reaches `in_ckgetkey(waitforkey=1)` before any blocking wait. If a path calls `GetKey` directly after `Console_Disp`, add `zflip()` before it.

- [ ] **Step 6: Build `-Prof`, install, and repeat the A1 Step 6 sequence.** Expected: `F1` for every typing key and overlay key. Shift/Alpha toggles show `F1` each (Review Focus 2). Run `for(k:=0;k<200;k++) print(k)`: the numbers must scroll live (Review Focus 1). Leave it idle until auto-off, then resume: the screen must be complete (Review Focus 4). Add the new numbers next to the baseline in `docs/perf-baseline.md`.

- [ ] **Step 7: Commit** the fork, then `export_patches.sh`, then:

```bash
git commit -am "perf: one screen flip per keypress; in_ckgetkey is the only pre-wait present point"
```

### Task A3: `MONO_CORE` build — freeze overlays out of the core loop

**Files:**
- Modify (fork): `console.cc` key dispatch: the `KEY_CHAR_FRAC` handling, `KEY_CTRL_SD/FD` (:2474-2481), bare F1–F5 homes (~:2815-2838), `KEY_CTRL_F9` pinned, the template keys from patch 0011, and the `zstack_*` calls in `Console_Disp` (:3486-3496)
- Modify: `tools/build_fork.sh`, `build.ps1` (`-Core` switch → `-DMONO_CORE`)

**Interfaces:**
- Produces: in a `MONO_CORE` build, the shell is a plain linear 1D line editor. Keys behave as in stock KhiCAS (S⇔D → stock behaviour, F-keys → stock menus, `a b/c` → inserts `/`), and history renders 1D only. Nothing gets deleted. The code is only compiled out.

- [ ] **Step 1:** Wrap each dispatch site listed above in `#ifndef MONO_CORE` … `#endif`, keeping the stock branch in `#else` where the patch replaced one. In `Console_Disp`, under `MONO_CORE`, force `zlm[i]` to the 1D layout so `zstack_console_draw` is never called.
- [ ] **Step 2:** Add `-Core` to `build.ps1` (passes `MONO_CORE=1`, and `build_fork.sh` adds `-DMONO_CORE`).
- [ ] **Step 3:** Build both `.\build.ps1` and `.\build.ps1 -Core -Prof`. Both must link. On the device, the `-Core` build shows plain 1D history, and `a b/c` types `/`.
- [ ] **Step 4: Commit** `"build: MONO_CORE switch compiles out overlays and 2D history for core-loop stabilisation"`.

### Task A4: Core-loop acceptance checklist

**Files:**
- Create: `docs/core-loop-checklist.md`

- [ ] **Step 1:** Write the checklist. Each row is an action plus its expected result. A row fails if you see any flicker, blank frame, stale character, wrong cursor, or a status bar that lags a key.

| # | Action | Expected |
|---|---|---|
| 1 | Type `123456789+` ×5 (past one line) | each char appears on its keypress, line scrolls horizontally, cursor stays visible |
| 2 | ← ×10, type `x`, → to end | `x` inserted at the cursor, no redraw of untouched lines visible |
| 3 | DEL ×5 at the middle and at the start | correct chars removed; DEL at the start does nothing |
| 4 | SHIFT, then SHIFT again; ALPHA, ALPHA; SHIFT+ALPHA (lock) | indicator flips on every press with no lag |
| 5 | EXE on `2+3` | input moves to history, `5` right-aligned, new empty line, `F1` |
| 6 | EXE on an empty line | nothing breaks, no blank frame |
| 7 | EXE on a syntax error `2+` | error shown once, line editable again |
| 8 | 30× EXE `1` (history > screen) then ↑ ×30, ↓ ×30 | scrollbar and selection track, no tearing |
| 9 | Hold a digit key (autorepeat) for 3 s | no dropped/duplicated chars; F stays at 1 |
| 10 | `for(k:=0;k<200;k++) print(k)` | live scrolling output |
| 11 | AC/ON during a long `factor(2^200+1)` | interrupts, shell redrawn fully |
| 12 | MENU → back into add-in | screen fully redrawn, same session |

- [ ] **Step 2:** Run it on the `-Core -Prof` build and write PASS/FAIL plus notes per row under a dated heading.
- [ ] **Step 3: Commit** `"docs: core-loop acceptance checklist and first run"`.

### Task A5: Fix core-loop failures until 3 clean runs

- [ ] **Step 1:** For each FAIL row, follow superpowers:systematic-debugging: reproduce, read the F/T counter, find the root cause, then fix it in the fork (one patch per root cause).
- [ ] **Step 2:** Re-run the full checklist after each fix. Exit when 3 consecutive full runs pass on both the `-Core` build and the default build.
- [ ] **Step 3:** Re-enable overlays one at a time in this order: `zstack` 2D history, `znat`, S⇔D, F-key sub-bars, pinned. Run the checklist plus that overlay's own open → type → EXIT → EXE flow, and require `F1` throughout. Any overlay that can't pass stays behind `MONO_CORE` for now.
- [ ] **Step 4:** Tag `v0.0.3` with `docs/perf-baseline.md` updated (before/after F and T). `.\build.ps1` produces the release zip.

---

## Part B — gint + Eigenmath spike (time-boxed: 5 working days, ends at a gate)

All work goes in `next/` on branch `spike/gint-eigenmath`. Nothing in Part A depends on it.

### Task B1: Toolchain + hello-world on the device
- [ ] Install fxSDK + gint in WSL (follow the current Planète Casio fxSDK install guide; record the exact versions in `next/README.md`).
- [ ] `fxsdk new next/app`, `fxsdk build-cg`. Copy the `.g3a` over and confirm it runs on the fx-CG50 alongside Monolith.
- [ ] Check the licences of gint, fxSDK and Eigenmath for compatibility with distributing a GPL-3.0 add-in, and record the result in `next/README.md`. **Gate:** if any licence is incompatible, stop Part B.
- [ ] Commit.

### Task B2: Eigenmath core builds and evaluates
- [ ] Vendor Eigenmath's C sources under `next/eigenmath/` (pin the upstream commit). Stub its print/output hook to write into a `char` ring buffer.
- [ ] Hard-code `1/3+1/4` and `d(sin(x)^2,x)`, evaluate at startup, `dtext` the results, then `dupdate()` and `getkey()`.
- [ ] Record: `.g3a` size, peak heap (Eigenmath's own stats or a `malloc` high-water wrapper), and evaluation time (gint timer/`clock`). Commit.

### Task B3: The core loop on gint (same spec as A4)
- [ ] Implement only: a line buffer with cursor, Shift/Alpha state, a status bar, a history list, EXE → Eigenmath → append result. Structure it as `for(;;){ draw_all(); dupdate(); key = getkey(); handle(key); }`. That gives **exactly one** `dupdate` per iteration by construction.
- [ ] Run `docs/core-loop-checklist.md` rows 1–9 and 12. Record keypress→frame latency against the A5 numbers in `next/README.md`. Commit.

### Task B4: Go / no-go gate (user decision)
- [ ] Write `next/GATE.md` with: the latency comparison (A5 vs B3), memory headroom, and a row for each README feature (Python/`FMENU.py`, stacked 2D results, natural fraction input, S⇔D forms, matrix editor, exact/decimal, angle sync), marked *free on Eigenmath / needs porting (estimate) / lost*.
- [ ] **Stop and ask the user to decide.** On "go", write a separate full port plan. On "no-go", archive the branch.