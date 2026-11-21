#!/usr/bin/env bash
# Builds Monolith from pristine KhiCAS sources (Linux or WSL).
#
#   tools/build.sh           first run: fetch sources and toolchain; then apply and build
#   tools/build.sh --clean   start again from a fresh KhiCAS tree
#
# Output: build/monolith.g3a, build/monolith.ac2 and build/release/monolith-<version>.zip
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${MONOLITH_WORK:-$HOME/monolith-work}"
URL="https://www-fourier.univ-grenoble-alpes.fr/~parisse/casio"
VERSION="$(tr -d '[:space:]' < "$REPO/VERSION")"
TOOLCHAIN=/home/parisse          # KhiCAS's Makefile expects the toolchain here
CLEAN=0
[ "${1:-}" = "--clean" ] && CLEAN=1

say() { printf '\n== %s\n' "$*"; }
fetch() { [ -f "$WORK/upstream/$1" ] || curl -fL --retry 3 -o "$WORK/upstream/$1" "$URL/$1"; }
mkdir -p "$WORK/upstream" "$WORK/bin"
export PATH="$TOOLCHAIN/opt/sh3eb-elf/bin:$WORK/bin:$PATH"

for tool in curl make cmake python3 g++; do
  command -v "$tool" >/dev/null || { echo "missing $tool: sudo apt install build-essential cmake libpng-dev curl python3"; exit 1; }
done

# 1. Bernard Parisse's prebuilt sh3eb-elf toolchain (GCC 7.3, Linux amd64)
if ! command -v sh3eb-elf-g++ >/dev/null; then
  say "Installing the sh3eb-elf toolchain in $TOOLCHAIN (asks for sudo once)"
  fetch casiolocal.tgz
  fetch libmpfr.so.4
  sudo mkdir -p "$TOOLCHAIN" && sudo chown "$USER" "$TOOLCHAIN"
  tar xzf "$WORK/upstream/casiolocal.tgz" -C "$TOOLCHAIN"
  sudo cp "$WORK/upstream/libmpfr.so.4" /usr/local/lib/ && sudo ldconfig
fi

# 2. mkg3a, which packs the .g3a (its CMake files predate CMake 3.5)
if ! command -v mkg3a >/dev/null; then
  say "Building mkg3a"
  fetch mkg3a.tgz
  rm -rf "$WORK/mkg3a" && tar xzf "$WORK/upstream/mkg3a.tgz" -C "$WORK"
  sed -i '65s|.*|include_directories ("${CMAKE_CURRENT_BINARY_DIR}")|' "$WORK/mkg3a/src/CMakeLists.txt"
  cmake -S "$WORK/mkg3a/src" -B "$WORK/mkg3a/build" -Wno-dev -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
  cmake --build "$WORK/mkg3a/build" >/dev/null
  cp "$WORK/mkg3a/build/mkg3a" "$WORK/bin/"
fi

# 3. Pristine KhiCAS tree (it ships prebuilt objects, so only changed files recompile)
fetch giacbf.tgz
if [ "$CLEAN" = 1 ] || [ ! -f "$WORK/giacbf/Makefile" ]; then
  say "Unpacking KhiCAS"
  rm -rf "$WORK/giacbf"
  tar xzf "$WORK/upstream/giacbf.tgz" -C "$WORK" giacbf
fi

# 4. Monolith's files and patches
say "Applying Monolith $VERSION"
cp "$REPO"/src/* "$REPO"/assets/monolith-uns.png "$REPO"/assets/monolith-sel.png "$WORK/giacbf/"
for p in "$REPO"/patches/[0-9]*.py; do
  (cd "$WORK/giacbf" && PYTHONPATH="$REPO/patches" python3 "$p")
done

# 5. Build
say "Building"
make -C "$WORK/giacbf" -j"$(nproc)" MONOLITH_VERSION="$VERSION" monolith.g3a monolith.ac2 2>&1 \
  | tee "$WORK/build.log" | grep -E 'error|rom:|ram:|r8c2:|mkg3a' || true
[ -f "$WORK/giacbf/monolith.g3a" ] && [ -f "$WORK/giacbf/monolith.ac2" ] || { echo "build failed: see $WORK/build.log"; exit 1; }

# 6. Release
mkdir -p "$REPO/build/release"
cp "$WORK/giacbf/monolith.g3a" "$WORK/giacbf/monolith.ac2" "$REPO/build/"
python3 - "$REPO" "$VERSION" <<'PY'
import sys, zipfile, os
repo, version = sys.argv[1], sys.argv[2]
out = os.path.join(repo, 'build', 'release', 'monolith-%s.zip' % version)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in ('monolith.g3a', 'monolith.ac2'):
        z.write(os.path.join(repo, 'build', f), f)
    z.writestr('INSTALL.txt', 'Monolith %s for the Casio fx-CG50\n\n'
               'Copy monolith.g3a and monolith.ac2 to the root of the calculator\'s storage memory.\n'
               'Both files must come from the same release.\n' % version)
print('release:', out)
PY
