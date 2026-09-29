# Monolith

Monolith is a **work-in-progress** fork of [KhiCAS](https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac.html) — Bernard Parisse's Giac/Xcas computer algebra system for the Casio fx-CG50 — that reskins the UI to look and behave like the calculator's native **RUN-MAT** screen while keeping the full Giac CAS core.

## What Monolith adds over stock KhiCAS

| Feature | Notes |
|---|---|
| Own folder | Everything Monolith needs or creates lives in `\@MONOLITH\` in Storage Memory: the RAM part `monolith.ac2`, the session `monolith.xw` and `FMENU.py`.  Stock KhiCAS files are never touched, so both add-ins can be installed together |
| RUN-MAT status bar | Boxed Math / Deg / Norm1 / d/c / Real labels, redrawn on every key |
| Natural fraction input | `a b/c` key opens a 2D fraction editor in the shell; submits as `((n)/(d))` |
| Stacked 2D results | Exact outputs drawn as real fractions/powers/roots via Giac's typesetter, right-aligned |
| 2D past inputs | History INPUT lines that contain a template form are redrawn in 2D |
| More templates | power, x², x⁻¹, √, \|·\|, ⁿ√, logₐb, d/dx, d²/dx², ∫, Σ, matrix 1–4×4 — all from natural keys or the F4 MATH sub-bar |
| S⇔D pop-up | Presents six result forms (Exact, Decimal, Fraction, Mixed, Root/π, Scientific) instead of the add-ins menu |
| Shell keys | F1 = JUMP, F2 = DELETE, F3 = matrix editor, F4 = MATH; Shift+F1 = CAS menu, Shift+F2 = File, Shift+F3 = Pinned commands; F5/F6 are free for `FMENU.py` |
| Flicker fix | Overlays suppress the mid-draw screen flip so no blank frame appears before the panel |
| Decimal default | Starts in approximate (decimal) mode; exact mode is still reachable via SETUP or `approx_mode(0)` |
| Straight to the shell | No QR splash page and no XCAS/Python startup prompt.  Overclock and Erase session are in the SETUP menu |

## Install on the fx-CG50

1. Connect the calculator by USB (or use an SD card) and open its Storage Memory.
2. Copy `monolith.g3a` to the root.
3. Create a folder named `@MONOLITH` in the root and copy `monolith.ac2` into it.

The add-in will not start if `@MONOLITH\monolith.ac2` is missing.  The two files are attached to each [release](../../releases).

Casio OS 3.80.0 is reported to crash KhiCAS; use 3.80.1 or 3.70.  The fx-CG Manager PLUS emulator build has not been tested with the `@MONOLITH` folder layout.

## Build

**Requirements:** Linux or WSL2 (the `sh3eb-elf` toolchain ships as Linux amd64 only), `git`, `python3`, `cmake`, `make`, `build-essential`.  Network access to `www-fourier.univ-grenoble-alpes.fr` is needed to download the pristine sources (~420 MB total).

```sh
# First time: fetch sources and set up the toolchain (takes ~5 minutes)
wsl -d Ubuntu -- bash tools/build_stock.sh

# Apply Monolith patches and build
wsl -d Ubuntu -- bash tools/fork_setup.sh
wsl -d Ubuntu -- bash tools/build_fork.sh
```

`monolith.g3a` and `monolith.ac2` appear in `build/fork/`.  To change the KhiCAS side, commit in `~/khicas-work/fork` and run `tools/export_patches.sh` to refresh `patches/`.

## Licence

Monolith is licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE).  It is built from Giac (GPL-3.0-or-later) and KhiCAS (GPL); new files in `gfx/`, `skin/` and `nin/` are GPL-3.0-or-later.

## Credits

- KhiCAS / Giac CAS: [Bernard Parisse](https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac.html) et al., GPL-3.0-or-later
- Interface adapted from Eigenmath for Casio by Gabriel Maia, Mike Smith, Nemhardy, LePhenixNoir
- MicroPython 1.12: Damien George et al., MIT
- Nayuki QR code generator: Project Nayuki, MIT
- Monolith UI skin and natural-input model: GPL-3.0-or-later

## Repository layout

| Path | Contents |
|---|---|
| `patches/` | Changes to KhiCAS sources, applied on top of pristine `giacbf.tgz` by `tools/fork_setup.sh` |
| `gfx/`, `skin/`, `nin/` | New Monolith source files overlaid into the fork by `tools/build_fork.sh` |
| `assets/` | Add-in menu icons |
| `tools/` | Build scripts; `export_patches.sh` regenerates `patches/` after editing the fork |
