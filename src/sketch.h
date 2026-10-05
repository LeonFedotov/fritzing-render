#pragma once
// The input format: parts placed on the breadboard view and wires between
// their connectors. Coordinates are Fritzing scene units: 90 per inch.
//
// {
//   "parts": [
//     { "id": "mcu", "part": "core/Arduino Nano3(fix).fzp", "x": 0, "y": 0, "rotate": 0, "label": "SHOW2", "labelBelow": false }
//   ],
//   "wires": [
//     { "from": "mcu.D10", "to": "lcd.CS", "color": "blue", "via": [[120, 40]] }
//   ],
//   "margin": 18
// }
//
// A part missing from the libraries can be drawn as a generic labelled
// block with header pins along its bottom edge, in place of "part":
//   { "id": "relay", "generic": { "title": "BLE Nano", "pins": ["TX", "GND"] }, "x": 0, "y": 0 }
//
// "color" on a part recolors its color_* elements, as Fritzing does for
// LEDs: a Fritzing LED color ("Green (555nm)"), a word (green: Fritzing's
// default shade) or #rrggbb.
//
// "part" is an .fzp path (absolute or relative to a library root), a
// moduleId or an exact title. Connectors are "<part id>.<connector id or name>".
// A wire end can also be a spot on a part, {"part": "a", "at": [x, y]} in
// the part's own coordinates (for a bodge wire to a chip's pin, say), or a
// scene point [x, y].
// "color" is a Fritzing wire color name (blue, red, black, yellow, green,
// grey, white, orange, ochre, cyan, brown, purple, pink) or #rrggbb.
//
// Sketches read from Fritzing's own files (fzz.h) fill in the fields marked
// "from a .fz": a part's full transform and stacking order, its bent legs,
// and wires given by their end points rather than by connectors.

#include <QHash>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QTransform>

#include <optional>

namespace sketch {

struct Generic {
	QString title;
	QStringList pins;
};

// A bendable leg as Fritzing saves it: a polyline in part coordinates, and
// for each segment either nothing (straight) or its two Bézier control points.
struct Leg {
	QPolygonF points;
	QList<QPolygonF> curves;  // curves[i]: from points[i] to points[i + 1]
};

struct PartSpec {
	QString id;
	QString part;
	Generic generic;  // used when `part` is empty
	QPointF pos;
	int rotate = 0;  // degrees, clockwise
	QString label;
	bool labelBelow = false;  // "labelBelow": true puts the label under the part
	QString color;            // recolors the part's color_* elements (Fritzing's LED colors)
	// From a .fz: part coordinates -> scene, in place of pos and rotate; the
	// stacking order (lower first); legs bent by the user, by connector id.
	std::optional<QTransform> transform;
	double z = 0;
	QHash<QString, Leg> legs;
};

struct WireSpec {
	QString from;
	QString to;
	QString color = "blue";
	QList<QPointF> via;
	// Point ends in place of connectors: on part fromPart/toPart (part
	// coordinates), or in the scene when that is empty.
	std::optional<QPointF> fromAt;
	std::optional<QPointF> toAt;
	QString fromPart;
	QString toPart;
	// From a .fz: fixed scene end points in place of from/to/via, the
	// Bézier control points of a curved wire (else empty), and the width.
	bool fixed = false;
	QPointF p1;
	QPointF p2;
	QPolygonF curve;
	double width = 2;  // scene units: Fritzing's default 22.2 mil wire
};

struct Sketch {
	QList<PartSpec> parts;
	QList<WireSpec> wires;
	double margin = 18;
};

struct ParseResult {
	Sketch sketch;
	QString error;  // empty on success
};

ParseResult parse(const QByteArray & json);

}  // namespace sketch
