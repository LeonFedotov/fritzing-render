#pragma once
// The input format: parts placed on the breadboard view and wires between
// their connectors. Coordinates are Fritzing scene units: 90 per inch.
//
// {
//   "parts": [
//     { "id": "mcu", "part": "core/Arduino Nano3(fix).fzp", "x": 0, "y": 0, "rotate": 0, "label": "SHOW2" }
//   ],
//   "wires": [
//     { "from": "mcu.D10", "to": "lcd.CS", "color": "blue", "via": [[120, 40]] }
//   ],
//   "margin": 18
// }
//
// "part" is an .fzp path (absolute or relative to a library root), a
// moduleId or an exact title. Connectors are "<part id>.<connector id or name>".
// "color" is a Fritzing wire color name (blue, red, black, yellow, green,
// grey, white, orange, ochre, cyan, brown, purple, pink) or #rrggbb.

#include <QList>
#include <QPointF>
#include <QString>

namespace sketch {

struct PartSpec {
	QString id;
	QString part;
	QPointF pos;
	int rotate = 0;  // degrees, clockwise
	QString label;
};

struct WireSpec {
	QString from;
	QString to;
	QString color = "blue";
	QList<QPointF> via;
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
