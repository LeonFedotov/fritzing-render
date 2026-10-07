#include "partlib.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <algorithm>

namespace partlib {

namespace {

// Header fields only; stops at <views>, so indexing stays fast.
Entry readEntry(const QString & path) {
	Entry e;
	e.path = path;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return e;
	QXmlStreamReader xml(&file);
	bool inFamily = false;
	while (!xml.atEnd()) {
		xml.readNext();
		if (!xml.isStartElement()) continue;
		const auto name = xml.name();
		if (name == QLatin1String("module")) {
			e.moduleId = xml.attributes().value("moduleId").toString();
		} else if (name == QLatin1String("title")) {
			e.title = xml.readElementText().trimmed();
		} else if (name == QLatin1String("tag")) {
			e.tags << xml.readElementText().trimmed();
		} else if (name == QLatin1String("property")) {
			inFamily = xml.attributes().value("name").compare(QLatin1String("family"), Qt::CaseInsensitive) == 0;
			const QString text = xml.readElementText().trimmed();
			if (inFamily) e.family = text;
		} else if (name == QLatin1String("views") || name == QLatin1String("connectors")) {
			break;
		}
	}
	return e;
}

QString haystack(const Entry & e) {
	return QStringList{e.title, e.moduleId, e.family, e.tags.join(' '), QFileInfo(e.path).completeBaseName()}
	    .join(' ')
	    .toLower();
}

}  // namespace

QStringList defaultRoots() {
	const QString env = qEnvironmentVariable("FRITZING_PARTS");
	if (!env.isEmpty()) return env.split(':', Qt::SkipEmptyParts);
	const QString vendor = QStringLiteral(FR_VENDOR_DIR);
	return {vendor + "/fritzing-parts", vendor + "/adafruit-parts", vendor + "/sparkfun-parts", vendor + "/seeed-parts",
	        vendor + "/mgesteiro-parts", vendor + "/dip-ic-parts", vendor + "/elegoo-parts", vendor + "/mkjanke-parts",
	        vendor + "/community-parts",
	        QStringLiteral(FR_PARTS_DIR)};
}

// .fzp files under `dir`, not descending into svg/ (thousands of drawings,
// no part files), hidden folders, or obsolete/ unless asked to.
void collect(const QDir & dir, int depth, bool withObsolete, QList<Entry> & out) {
	for (const QFileInfo & f : dir.entryInfoList({"*.fzp"}, QDir::Files)) out << readEntry(f.absoluteFilePath());
	if (depth == 0) return;
	for (const QFileInfo & d : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QString name = d.fileName();
		if (name == "svg" || (name == "obsolete" && !withObsolete) || name.startsWith('.')) continue;
		collect(QDir(d.absoluteFilePath()), depth - 1, withObsolete, out);
	}
}

QList<Entry> index(const QStringList & roots, bool withObsolete) {
	QList<Entry> out;
	for (const QString & root : roots) collect(QDir(root), 3, withObsolete, out);
	return out;
}

QList<Entry> search(const QList<Entry> & entries, const QString & query, int limit) {
	const QStringList words = query.toLower().split(' ', Qt::SkipEmptyParts);
	QList<QPair<int, Entry>> scored;
	for (const Entry & e : entries) {
		const QString hay = haystack(e);
		const bool all = std::all_of(words.begin(), words.end(), [&](const QString & w) { return hay.contains(w); });
		if (!all) continue;
		const QString title = e.title.toLower();
		const int titleHits = static_cast<int>(std::count_if(words.begin(), words.end(), [&](const QString & w) { return title.contains(w); }));
		const int core = e.path.contains("/core/") ? 1 : 0;
		scored << qMakePair(titleHits * 10 + core * 2 - static_cast<int>(title.size() / 40), e);
	}
	std::stable_sort(scored.begin(), scored.end(), [](const auto & a, const auto & b) { return a.first > b.first; });
	QList<Entry> out;
	for (int i = 0; i < scored.size() && i < limit; i++) out << scored[i].second;
	return out;
}

QString resolve(const QStringList & roots, const QList<Entry> & entries, const QString & ref) {
	if (QFileInfo::exists(ref) && ref.endsWith(".fzp")) return QFileInfo(ref).absoluteFilePath();
	for (const QString & root : roots) {
		const QString p = QDir(root).filePath(ref);
		if (QFileInfo::exists(p)) return QFileInfo(p).absoluteFilePath();
	}
	for (const Entry & e : entries) {
		if (e.moduleId == ref) return e.path;
	}
	for (const Entry & e : entries) {
		if (e.title.compare(ref, Qt::CaseInsensitive) == 0) return e.path;
	}
	return {};
}

QString ref(const QStringList & roots, const QString & path) {
	const QString abs = QFileInfo(path).absoluteFilePath();
	for (const QString & root : roots) {
		const QString r = QFileInfo(root).absoluteFilePath() + "/";
		if (abs.startsWith(r)) return abs.mid(r.size());
	}
	return path;
}

QString imagePath(const fzp::Part & part, const QString & viewName) {
	const QString image = part.views.value(viewName).image;
	if (image.isEmpty()) return {};
	const QFileInfo fzpInfo(part.path);
	const QDir dir = fzpInfo.dir();

	// fritzing-parts: <root>/<folder>/X.fzp -> <root>/svg/<folder>/<image>
	QDir root = dir;
	const QString folder = dir.dirName();
	if (root.cdUp()) {
		const QString p = root.filePath("svg/" + folder + "/" + image);
		if (QFileInfo::exists(p)) return p;
		for (const QString & f : {"core", "contrib", "user", "obsolete"}) {
			const QString q = root.filePath(QString("svg/%1/%2").arg(f, image));
			if (QFileInfo::exists(q)) return q;
		}
	}
	// unpacked .fzpz: "breadboard/Y.svg" -> svg.breadboard.Y.svg beside the fzp
	const QString flat = "svg." + QString(image).replace('/', '.');
	if (QFileInfo::exists(dir.filePath(flat))) return dir.filePath(flat);
	if (QFileInfo::exists(dir.filePath(image))) return dir.filePath(image);
	return {};
}

QList<Manifest> manifest(const QStringList & roots) {
	QList<Manifest> out;
	for (const QString & root : roots) {
		const QDir dir(root);
		for (const Entry & e : index({root}, true)) {
			const fzp::Part part = fzp::read(e.path);
			if (!part.ok) continue;
			auto rel = [&](const QString & path) { return path.isEmpty() ? QString() : dir.relativeFilePath(path); };
			out << Manifest{root, rel(e.path), rel(imagePath(part, "breadboardView")), rel(imagePath(part, "schematicView")), e};
		}
	}
	return out;
}

}  // namespace partlib
