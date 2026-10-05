#pragma once
// Parts Fritzing makes as it runs rather than keeps in a library, written
// out as a part file and drawing so they load like any other part. So far:
// single-row pin headers (generic_{female,male,rounded_female}_pin_header_N_100mil
// and their double_row variants, which Fritzing's breadboard view also
// draws as one row), from Fritzing's own templates.

#include <QString>

namespace generated {

// Writes the part for `moduleId` into `dir`; returns its .fzp path, or empty
// if it isn't a part this knows how to make.
QString make(const QString & moduleId, const QString & dir);

}  // namespace generated
