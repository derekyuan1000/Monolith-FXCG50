#!/bin/bash
# Zip the built add-in in the layout the calculator expects (README "Install on the fx-CG50"):
#   monolith.g3a               -> Storage Memory root
#   @MONOLITH/monolith.ac2     -> the @MONOLITH folder
# Run after tools/build_fork.sh. Writes build/release/monolith-<version>.zip (git-ignored).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
SRC=$REPO/build/fork
VERSION=$(sed -n 's/^APP_VERSION=//p' "$REPO/tools/build_fork.sh")
[ -n "$VERSION" ] || { echo "APP_VERSION not found in tools/build_fork.sh"; exit 1; }
for f in monolith.g3a monolith.ac2; do
  [ -f "$SRC/$f" ] || { echo "missing $SRC/$f: run tools/build_fork.sh first"; exit 1; }
done
OUT=$REPO/build/release
mkdir -p "$OUT"
ZIP=$OUT/monolith-$VERSION.zip
python3 - "$SRC" "$ZIP" <<'EOF'
import sys, zipfile
src, dest = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(f"{src}/monolith.g3a", "monolith.g3a")
    z.write(f"{src}/monolith.ac2", "@MONOLITH/monolith.ac2")
EOF
echo "=== $ZIP"
python3 -c "import sys,zipfile; [print(f'  {i.file_size:>9}  {i.filename}') for i in zipfile.ZipFile(sys.argv[1]).infolist()]" "$ZIP"
