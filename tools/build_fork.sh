#!/bin/bash
# Build the fork: tracked sources from the fork tree + overlay from gfx/ and skin/.
# Option --makefile-only: stop after generating the build tree's Makefile (to test fork_objs.txt).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
UP=${KHICAS_WORK:-$HOME/khicas-work}
FORK=$UP/fork
OUT=$UP/fork-build
# identity (CLAUDE.md 3.4: distinct name, icon, version)
APP_NAME=Monolith
APP_INTERNAL=MONOLITH
APP_VERSION=0.0.2
STEM=monolith
export PATH="$HOME/opt/sh3eb-elf/bin:$PATH"
[ -d "$FORK/.git" ] || { echo "run tools/fork_setup.sh first"; exit 1; }
mkdir -p "$OUT"

# 1. tracked sources (objects already in $OUT are kept, so make stays incremental)
(cd "$FORK" && git ls-files -z | xargs -0 cp -P --parents -u --preserve=timestamps -t "$OUT")

# 2. overlay: our C files and headers, flat (the Makefile uses -I.); host-only files are skipped
for f in "$REPO"/gfx/*.c "$REPO"/gfx/*.h "$REPO"/skin/*.c "$REPO"/skin/*.cc "$REPO"/skin/*.h; do
  [ -e "$f" ] || continue
  case "$(basename "$f")" in gfx_host.*) continue ;; esac
  cp -u --preserve=timestamps "$f" "$OUT/"
done

# The natural-input model is overlaid as znin.c (the z prefix puts its code in .rominram, see prizm.ld) plus nin.h.
cp -u --preserve=timestamps "$REPO/nin/nin.c" "$OUT/znin.c"
cp -u --preserve=timestamps "$REPO/nin/nin.h" "$OUT/nin.h"

# 3. adapt the Makefile exactly as tools/build_stock.sh does, plus our extra objects.
# Regenerated from the fork working tree on every run (like the other files, so an
# uncommitted Makefile edit is picked up), which keeps the seds below idempotent.
cd "$OUT"
cp -f "$FORK/Makefile" Makefile
sed -i "s#/home/parisse#$HOME#g" Makefile
sed -i '/\/bin\/cp/d' Makefile
cp -f "$REPO/assets/monolith_uns.png" "$REPO/assets/monolith_sel.png" "$OUT/"
sed -i "s/-n basic:Khicas[0-9]* -n internal:KHICAS[0-9]* -V [0-9.]* -i uns:khicasio.png -i sel:khicasio1.png/-n basic:$APP_NAME -n internal:$APP_INTERNAL -V $APP_VERSION -i uns:monolith_uns.png -i sel:monolith_sel.png/" Makefile
n=$(grep -c 'basic:Monolith' Makefile || true)
[ "$n" -ge 1 ] || { echo "identity sed did not match the upstream Makefile"; exit 1; }
echo "identity sed applied to $n mkg3a line(s)"
FORK_OBJS=${FORK_OBJS:-$REPO/tools/fork_objs.txt}
EXTRA=$( { tr -d '\r' < "$FORK_OBJS" | grep -v '^#' || true; } | tr '\n' ' ')
sed -i "s/^GUI_OBJS = \(.*\)$/GUI_OBJS = \1 $EXTRA/" Makefile
# Optional build switches (build.ps1 -Prof / -Core): MONO_PROF=1 shows flips/ticks per key in the status bar,
# MONO_CORE=1 compiles out the overlays and 2D history so the plain shell loop can be tested on its own.
DEFS=""
[ "${MONO_PROF:-0}" = 1 ] && DEFS="$DEFS -DMONO_PROF"
[ "${MONO_CORE:-0}" = 1 ] && DEFS="$DEFS -DMONO_CORE"
if [ -n "$DEFS" ]; then
  sed -i "s/^CFLAGS = /CFLAGS = $DEFS /; s/^CXXFLAGS = /CXXFLAGS = $DEFS /; s/^CXXFLAGS1 = /CXXFLAGS1 = $DEFS /" Makefile
fi
# make does not track flags: when they change, drop the objects the switches affect so they are rebuilt.
if [ "$(cat .monolith_defs 2>/dev/null)" != "$DEFS" ]; then
  rm -f console.o main.o $EXTRA
  echo "$DEFS" > .monolith_defs
fi
echo "build switches:${DEFS:- none}"
[ "${1:-}" != "--makefile-only" ] || { echo "Makefile generated: $OUT/Makefile"; exit 0; }

# 4. build
( time make khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882 ) 2>&1 | tee build.log

# 5. report and copy back
echo "=== memory report (device link)"
grep -A5 'Memory region' build.log | head -6 || echo "(nothing relinked: outputs were up to date)"
ls -l khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882
sha256sum khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882
mkdir -p "$REPO/build/fork"
rm -f "$REPO"/build/fork/khicas50.* "$REPO"/build/fork/emucas50.g3a   # stale names must not be installed by mistake
cp -f khicas50.g3a "$REPO/build/fork/${STEM}.g3a"
cp -f khicas50.ac2 "$REPO/build/fork/${STEM}.ac2"
cp -f emucas50.g3a "$REPO/build/fork/${STEM}_emu.g3a"
cp -f khicas50.882 "$REPO/build/fork/${STEM}.882"
cp -f build.log "$REPO/build/fork/"
echo "=== copied to $REPO/build/fork/"
