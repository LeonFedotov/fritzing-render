#pragma once
// A schematic for a sketch that only has breadboard positions (a JSON
// sketch): each part's schematic symbol laid out in rows, in the sketch's
// order, and every wired pin given a net label, so pins on the same net
// carry the same name. Nothing is routed.
//
// Nets follow the wires, pins that touch on the breadboard (a wire's end on
// a pin, a part plugged into a breadboard), and the buses inside parts (a
// breadboard's strips, a board's GND pins), through parts that have no
// schematic symbol and so aren't drawn (breadboards). A net is labelled if
// it has a wire or joins two parts. A net is named, in order of preference:
// GND, if any pin on it is a ground; the name most of its pins share;
// a power pin's name (3V3, VIN, ...); the pin's name on the part with the
// most pins (the microcontroller), with the part's id when that has only
// two (led_anode).

#include <QList>
#include <QString>
#include <QStringList>

#include "partlib.h"
#include "sketch.h"

namespace schematic {

struct Result {
	sketch::Sketch sketch;  // its view sketch::SchematicView; parts placed by transform
	QStringList warnings;
	QString error;          // empty on success
};

// Net labels are written into `workDir` as generated parts.
Result fromBreadboard(const sketch::Sketch & breadboard, const QStringList & roots, const QList<partlib::Entry> & entries, const QString & workDir);

// A Fritzing sketch's (.fzz, .fz) schematic view, as fzz::load reads it; for
// one saved without schematic positions (written by hand, say), its
// breadboard laid out as above.
Result fromFritzing(const QByteArray & data, const QStringList & roots, const QString & workDir);

}  // namespace schematic
