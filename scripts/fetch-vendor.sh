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
