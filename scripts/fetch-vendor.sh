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
# Community parts the main libraries lack, unpacked like Adafruit's:
# TD-er/fritzing-parts (MIT): MH-Z19 CO2 sensor, NodeMCU, OLEDs, LuaNode32.
# DOIT ESP32 DevKit v1 "improved" (vanepp, Fritzing forum), via a GitHub mirror.
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
fi
