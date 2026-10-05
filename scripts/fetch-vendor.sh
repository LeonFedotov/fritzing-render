#!/bin/sh
# Fetches what the renderer builds against and renders from, into vendor/:
# svgpp (header-only, used by Fritzing's TextUtils) and the parts libraries.
# Fritzing's own sources are compiled in place from FRITZING_APP (default ../fritzing-app).
# The parts libraries are submodules under libraries/: Adafruit's,
# SparkFun's, Seeed's, mgesteiro's, Adr-hyng's DIP ICs, Elegoo's kit bin,
# mkjanke's ESP32 boards, and fritzing-parts-extra (parts drawn for boards
# no library has).
set -e
cd "$(dirname "$0")/.."
if [ -e .git ]; then git submodule update --init --depth 1; fi
mkdir -p vendor && cd vendor
[ -d svgpp-1.3.1 ] || git clone --depth 1 --branch v1.3.1 https://github.com/svgpp/svgpp.git svgpp-1.3.1
[ -d fritzing-parts ] || git clone --depth 1 --branch develop https://github.com/fritzing/fritzing-parts.git

# Adafruit and SparkFun ship .fzpz archives (fzp + svg.<view>.<name>.svg);
# unpack each into its own folder so the part library reads them like loose parts.
unpack() {  # <folder of .fzpz files> <destination>
  [ -d "$2" ] && return
  mkdir "$2"
  for z in "$1"/*.fzpz; do
    d="$2/$(basename "$z" .fzpz)"
    mkdir -p "$d" && unzip -q -o "$z" -d "$d"
  done
}
unpack ../libraries/adafruit/parts adafruit-parts
unpack ../libraries/sparkfun/products sparkfun-parts

# Libraries that keep their parts in subfolders: every .fzpz under the
# library, each into its own folder, and with "bins" its .fzbz bins too (for
# parts that come only in a bin; elsewhere a bin repeats the .fzpz files).
unpack_tree() {  # <library> <destination> [bins]
  [ -d "$1" ] && [ ! -d "$2" ] || return 0
  mkdir "$2"
  if [ "$3" = bins ]; then pattern='*.fz[pb]z'; else pattern='*.fzpz'; fi
  find "$1" -name "$pattern" -not -path '*/.git/*' | while read -r z; do
    d="$2/$(echo "${z#"$1"/}" | sed 's|/|_|g; s|\.fz[pb]z$||')"
    mkdir -p "$d" && unzip -q -o "$z" -d "$d"
  done
}
unpack_tree ../libraries/seeed seeed-parts bins
unpack_tree ../libraries/mgesteiro mgesteiro-parts
unpack_tree ../libraries/dip-ics dip-ic-parts
unpack_tree ../libraries/mkjanke mkjanke-parts
# Elegoo's bin is loose parts already laid out like an unpacked .fzpz.
if [ -d ../libraries/elegoo/bin ] && [ ! -d elegoo-parts ]; then cp -R ../libraries/elegoo/bin elegoo-parts; fi

# Community parts the main libraries lack, unpacked the same way:
# TD-er/fritzing-parts (MIT): MH-Z19 CO2 sensor, NodeMCU, OLEDs, LuaNode32.
# DOIT ESP32 DevKit v1 "improved" (vanepp, Fritzing forum), via a GitHub mirror.
# Unexpected Maker FeatherS2 (otherguy/FeatherS2-Fritzing, MIT).
if [ ! -d community-parts ]; then
  mkdir community-parts
  git clone -q --depth 1 https://github.com/TD-er/fritzing-parts.git community-src-tder
  for z in community-src-tder/*/*.fzpz; do
    d="community-parts/$(basename "$z" .fzpz)"
    mkdir -p "$d" && unzip -q -o "$z" -d "$d"
  done
  curl -fsSL -o "community-doit-esp32.fzpz" \
    "https://raw.githubusercontent.com/jorgechacblogspot/librerias_fritzing/main/DOIT%20Esp32%20DevKit%20v1%20improved.fzpz"
  mkdir -p "community-parts/DOIT Esp32 DevKit v1 improved"
  unzip -q -o community-doit-esp32.fzpz -d "community-parts/DOIT Esp32 DevKit v1 improved"
  curl -fsSL -o "community-feathers2.fzpz" \
    "https://raw.githubusercontent.com/otherguy/FeatherS2-Fritzing/ee018098aca0ec3f912d9cd7218658e1ac8909fc/Feather%20S2.fzpz"
  mkdir -p "community-parts/Feather S2"
  unzip -q -o community-feathers2.fzpz -d "community-parts/Feather S2"
fi
