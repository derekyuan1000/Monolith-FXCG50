#!/bin/bash
# One-time setup of our KhiCAS fork tree (run in WSL from anywhere).
# Pristine giacbf sources are committed as a baseline (tag "pristine"), then patches/ is applied.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
UP=${KHICAS_WORK:-$HOME/khicas-work}
FORK=$UP/fork
[ -f "$UP/giacbf.tgz" ] || { echo "missing $UP/giacbf.tgz: run tools/build_stock.sh first"; exit 1; }
[ ! -e "$FORK" ] || { echo "fork already exists: $FORK (delete it to recreate)"; exit 1; }
mkdir -p "$FORK"
tar xzf "$UP/giacbf.tgz" -C "$FORK" --strip-components=1
cd "$FORK"
cat > .gitignore <<'EOT'
*.o
*.a
# libmicropy.a is a prebuilt input shipped in the tarball, not a build product
!libmicropy.a
*.elf
*.g3a
*.ac2
*.882
*.bin
*.map
dump*
EOT
git init -q
git config user.name "khicas-fork"
git config user.email "khicas-fork@localhost"
git add -A
git commit -q -m "pristine giacbf.tgz (server date 2026-07-31)"
git tag pristine
if ls "$REPO"/patches/*.patch >/dev/null 2>&1; then
  for p in "$REPO"/patches/*.patch; do
    if ! git am "$p" 2>/dev/null; then
      git am --abort 2>/dev/null || true
      patch -p1 -F2 < "$p"
      subject=$(sed -n '/^Subject:/{s/^Subject: //;s/^\[PATCH[^]]*\] //;p;q}' "$p")
      git add -A
      git commit -q -m "${subject:-(patch $(basename "$p"))}"
      echo "  (applied via patch(1) with fuzz: $(basename "$p"))"
    fi
  done
fi
echo "fork ready: $FORK ($(git rev-list --count pristine..HEAD) patches applied)"
