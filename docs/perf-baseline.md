# Screen flips per key (MONO_PROF builds)

Build with `.\build.ps1 -Prof`; the status bar shows `F<flips> T<ticks>` for the last key
(ticks are RTC ticks, 1/128 s ≈ 7.8 ms, so T is coarse: read it as a trend over several keys).

| Key sequence | before (0.0.2 + counter) F / T | after one-flip (A2) F / T |
|---|---|---|
| `1` | | |
| `2` | | |
| `+` | | |
| `3` | | |
| EXE | | |
| ← | | |
| DEL | | |
| SHIFT (toggle) | | |
| F4 MATH → EXIT | | |
| `a b/c` (fraction editor), type `1`, EXIT | | |

Expected from reading the code: before = F2 for plain typing; after = F1.
