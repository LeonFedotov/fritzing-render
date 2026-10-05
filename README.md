# fritzing-render

Renders Fritzing breadboard diagrams to SVG and PNG without the Fritzing app,
using Fritzing's own SVG code and its parts libraries.

**Status: work in progress.**

## How it works

Fritzing exports a view by taking each part's SVG for that view, scaling it to
the export DPI, placing it, and appending the wires
(`SketchWidget::renderToSVG` in fritzing-app). This project compiles the
self-contained part of that pipeline straight from a
[fritzing-app](https://github.com/fritzing/fritzing-app) checkout (14 source
files: `SvgFileSplitter`, `FSvgRenderer`, `TextUtils`, `GraphicsUtils`, the SVG
path parser and helpers) and replaces the parts that pull in the whole
application (the part model, wires, the sketch canvas) with a small
reimplementation.

Front ends: a CLI, and an MCP server so an agent can render diagrams and look
at the images.

## Requirements

- Qt 6 (`brew install qt`), Boost headers, CMake
- A fritzing-app checkout next to this repo (or set `FRITZING_APP`)
- `scripts/fetch-vendor.sh` for svgpp and the parts libraries

## Licence

GPL-3.0-or-later, as it compiles Fritzing's GPL sources. The parts libraries
(fetched, not included) are CC-BY-SA 3.0 (Fritzing) and their own licences
(Adafruit).
