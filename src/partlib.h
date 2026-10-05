#pragma once
// Finding parts in Fritzing parts libraries and their view SVGs.
// Two layouts are understood:
//   fritzing-parts: <root>/<core|contrib|user>/X.fzp, SVGs in <root>/svg/<folder>/<view>/Y.svg
//   unpacked .fzpz: <dir>/part.X.fzp, SVGs beside it as svg.<view>.Y.svg

#include <QList>
#include <QString>
#include <QStringList>

#include "fzp.h"

namespace partlib {

struct Entry {
	QString path;
	QString moduleId;
	QString title;
	QString family;
	QStringList tags;
};

// The library roots: FRITZING_PARTS (colon-separated) if set, else the
// vendor folders this build was configured with and libraries/fritzing-parts-extra.
QStringList defaultRoots();

// Every .fzp under the roots, with its header fields. obsolete/ folders are
// skipped unless `withObsolete` (old sketches still use their parts).
QList<Entry> index(const QStringList & roots, bool withObsolete = false);

// Entries whose title, moduleId, family, tags or file name contain every
// word of `query` (case-insensitive), best (title) matches first.
QList<Entry> search(const QList<Entry> & entries, const QString & query, int limit);

// A part reference to an .fzp path: an existing file path, a path relative
// to a root, a moduleId, or an exact title. Empty if nothing matches.
QString resolve(const QStringList & roots, const QList<Entry> & entries, const QString & ref);

// The shortest reference that resolve() maps back to `path`: the path
// relative to the first root containing it, else `path` itself.
QString ref(const QStringList & roots, const QString & path);

// The SVG file for one view of a part (e.g. "breadboardView"), or empty.
QString imagePath(const fzp::Part & part, const QString & viewName);

}  // namespace partlib
