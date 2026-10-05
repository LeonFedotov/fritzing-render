#!/bin/sh
# Fetches what the renderer builds against and renders from, into vendor/:
# svgpp (header-only, used by Fritzing's TextUtils) and the parts libraries.
# Fritzing's own sources are compiled in place from FRITZING_APP (default ../fritzing-app).
set -e
cd "$(dirname "$0")/.."
mkdir -p vendor && cd vendor
[ -d svgpp-1.3.1 ] || git clone --depth 1 --branch v1.3.1 https://github.com/svgpp/svgpp.git svgpp-1.3.1
[ -d fritzing-parts ] || git clone --depth 1 --branch develop https://github.com/fritzing/fritzing-parts.git
[ -d adafruit-fritzing ] || git clone --depth 1 https://github.com/adafruit/Fritzing-Library.git adafruit-fritzing
# Adafruit ships .fzpz archives (fzp + svg.<view>.<name>.svg); unpack each
# into its own folder so the part library can read them like loose parts.
if [ ! -d adafruit-parts ]; then
  mkdir adafruit-parts
  for z in adafruit-fritzing/parts/*.fzpz; do
    d="adafruit-parts/$(basename "$z" .fzpz)"
    mkdir -p "$d" && unzip -q -o "$z" -d "$d"
  done
fi
