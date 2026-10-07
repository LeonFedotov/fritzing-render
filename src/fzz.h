#pragma once
// Fritzing's own sketch files: an .fzz (a zip of the .fz sketch and any
// parts it bundles) or a bare .fz. One view is read, breadboard or
// schematic: its parts, placed by their saved transforms (with LED colors
// and bent legs on the breadboard, labels and junction dots in the
// schematic), and its wires. Items of other views are not part of it;
// notes and parts no library has are left out with a warning.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "sketch.h"

namespace fzz {

enum class Format { Json, Fz, Fzz };

// What a sketch file holds, from its first bytes.
Format detect(const QByteArray & data);

struct Loaded {
	sketch::Sketch sketch;  // parts refer to their .fzp files by absolute path
	QStringList warnings;   // what was left out, and why
	QString error;          // empty on success
};

// An .fzz or .fz file's contents, with parts resolved by moduleId against
// `roots` (obsolete parts included, as old sketches use them) and, for an
// .fzz, the parts bundled in it, unpacked into `workDir`. Parts Fritzing
// generates (generated.h: pin headers, net labels, power and ground
// symbols) are written to `workDir` too; with no workDir they are left out.
Loaded load(const QByteArray & data, const QStringList & roots, const QString & workDir,
            const QString & view = sketch::BreadboardView);

// The module ids an .fzz or .fz uses, each once, wires left out: the parts
// a loader must have before rendering it. Empty for anything else.
QStringList moduleIds(const QByteArray & data);

}  // namespace fzz
