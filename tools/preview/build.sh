#!/bin/sh
# Build and run the host-side UI preview (macOS / Linux).
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-preview}
mkdir -p "$OUT"
c++ -std=c++17 -O2 -Wall -Wno-unused-function -Isrc \
  src/gfx.cpp src/ui.cpp src/settings.cpp src/fonts.cpp tools/preview/preview.cpp -o "$OUT/preview_bin"
"$OUT/preview_bin" "$OUT"
if command -v sips >/dev/null; then
  for f in "$OUT"/*.bmp; do sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null; done
fi
