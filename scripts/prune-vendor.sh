#!/bin/sh
# Shrinks vendor/ for the Docker image: only breadboard and schematic
# drawings are rendered, so PCB and icon SVGs go, except the few that a part
# uses as its breadboard or schematic image. Also drops the git histories
# and the archives fetch-vendor.sh unpacked.
set -e
cd "$(dirname "$0")/../vendor"
rm -rf community-src-tder ./*.fzpz
find . -name .git -prune -exec rm -rf {} +

# Every breadboard or schematic image a part names, e.g. "icon/plain_pcb.svg".
refs=$(mktemp)
find . -name '*.fzp' -exec perl -0777 -ne 'print "$1\n" while /<(?:breadboard|schematic)View\b[^>]*>\s*<layers\s+image="([^"]+)"/g' {} + | sort -u > "$refs"

# fritzing-parts: svg/<folder>/<view>/<file>
find fritzing-parts/svg -mindepth 3 -type f \( -path '*/pcb/*' -o -path '*/icon/*' \) | while read -r f; do
  rel=$(echo "$f" | cut -d/ -f4-)
  grep -qxF "$rel" "$refs" || rm -f "$f"
done
# unpacked .fzpz: svg.<view>.<file> beside the part
for d in adafruit-parts sparkfun-parts seeed-parts mgesteiro-parts dip-ic-parts elegoo-parts mkjanke-parts wemos-shields-parts community-parts; do [ -d "$d" ] && echo "$d"; done |
  xargs -I{} find {} -type f \( -name 'svg.pcb.*' -o -name 'svg.icon.*' \) | while read -r f; do
  rel=$(basename "$f" | sed 's/^svg\.\([a-z]*\)\./\1\//')
  grep -qxF "$rel" "$refs" || rm -f "$f"
done
rm -f "$refs"
