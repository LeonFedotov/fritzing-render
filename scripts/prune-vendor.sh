#!/bin/sh
# Shrinks vendor/ for the Docker image: only breadboard drawings are ever
# rendered, so PCB, schematic and icon SVGs go, except the few that a part
# uses as its breadboard image. Also drops the git histories and the
# archives fetch-vendor.sh unpacked.
set -e
cd "$(dirname "$0")/../vendor"
rm -rf adafruit-fritzing community-src-tder ./*.fzpz
find . -name .git -prune -exec rm -rf {} +

# Every breadboard image a part names, e.g. "icon/plain_pcb.svg".
refs=$(mktemp)
find . -name '*.fzp' -exec perl -0777 -ne 'print "$1\n" while /<breadboardView\b[^>]*>\s*<layers\s+image="([^"]+)"/g' {} + | sort -u > "$refs"

# fritzing-parts: svg/<folder>/<view>/<file>
find fritzing-parts/svg -mindepth 3 -type f \( -path '*/pcb/*' -o -path '*/schematic/*' -o -path '*/icon/*' \) | while read -r f; do
  rel=$(echo "$f" | cut -d/ -f4-)
  grep -qxF "$rel" "$refs" || rm -f "$f"
done
# unpacked .fzpz: svg.<view>.<file> beside the part
find adafruit-parts community-parts -type f \( -name 'svg.pcb.*' -o -name 'svg.schematic.*' -o -name 'svg.icon.*' \) | while read -r f; do
  rel=$(basename "$f" | sed 's/^svg\.\([a-z]*\)\./\1\//')
  grep -qxF "$rel" "$refs" || rm -f "$f"
done
rm -f "$refs"
