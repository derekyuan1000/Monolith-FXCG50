#!/bin/bash
# Regenerate patches/ from the commits made on top of the "pristine" tag in the fork tree.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
FORK=${KHICAS_WORK:-$HOME/khicas-work}/fork
cd "$FORK"
rm -f "$REPO"/patches/*.patch
git format-patch -q pristine..HEAD -o "$REPO/patches"
echo "$(ls "$REPO"/patches/*.patch 2>/dev/null | wc -l) patch(es) written to patches/"
