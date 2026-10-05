#pragma once
// Parts Fritzing makes as it runs rather than keeps in a library, written
// out as a part file and drawing so they load like any other part:
// single-row pin headers (generic_{female,male,rounded_female}_pin_header_N_100mil
// and their double_row variants, which Fritzing's breadboard view also
// draws as one row) from Fritzing's own templates, schematic net labels,
// and Fritzing's own schematic symbols (ground, power).

#include <QString>
#include <QStringList>

namespace generated {

// Writes the part for `moduleId` into `dir`; returns its .fzp path, or empty
// if it isn't a part this knows how to make.
QString make(const QString & moduleId, const QString & dir);

// A schematic net label (NetLabel::makeSvg, current style): `text` in a box
// as wide as it needs, pointed on the side of its one connector, the left
// if `left`. Returns its .fzp path in `dir`.
QString netLabel(const QString & text, bool left, const QString & dir);

// Fritzing's own schematic symbols, kept in its resources/parts: ground,
// power, power label, DC power.
bool isSymbol(const QString & moduleId);

// Writes such a symbol into `dir`, its drawing found in Fritzing's
// resources or the library `roots`, and a power symbol's label set to
// `voltage` (SymbolPaletteItem::replaceTextElement). Returns its .fzp path,
// or empty if its drawing can't be found.
QString symbol(const QString & moduleId, const QString & voltage, const QString & dir, const QStringList & roots);

}  // namespace generated
