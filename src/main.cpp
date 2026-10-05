// fritzing-render: Fritzing breadboard diagrams to SVG/PNG without the app.
//
//   fritzing-render render <sketch.json|sketch.fzz|sketch.fz|-> [-o out.svg] [--png out.png] [--ppi 300] [--transparent]
//                          [--view breadboard|schematic] [--theme fritzing|modern]
//   fritzing-render search <words...> [--limit 20] [--json]
//   fritzing-render part <fzp path | moduleId | title> [--json]
//
// Library roots: FRITZING_PARTS (colon-separated), else the build's vendor/.

#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

#include "fzz.h"
#include "partlib.h"
#include "render.h"
#include "sketch.h"

namespace {

QTextStream & out() {
	static QTextStream s(stdout);
	return s;
}

QTextStream & err() {
	static QTextStream s(stderr);
	return s;
}

int usage() {
	err() << "usage:\n"
	         "  fritzing-render render <sketch.json|sketch.fzz|sketch.fz|-> [-o out.svg] [--png out.png] [--ppi 300] [--transparent]\n"
	         "                         [--view breadboard|schematic]   (schematic: .fzz/.fz only)\n"
	         "                         [--theme fritzing|modern]       (the schematic's look)\n"
	         "  fritzing-render search <words...> [--limit 20] [--json]\n"
	         "  fritzing-render part <fzp path | moduleId | title> [--json]\n";
	return 2;
}

QString option(QStringList & args, const QString & name, const QString & fallback = {}) {
	const int i = args.indexOf(name);
	if (i < 0 || i + 1 >= args.size()) return fallback;
	const QString v = args.at(i + 1);
	args.remove(i, 2);
	return v;
}

bool flag(QStringList & args, const QString & name) {
	return args.removeAll(name) > 0;
}

QByteArray readInput(const QString & path) {
	QFile f;
	if (path == "-") {
		if (!f.open(stdin, QIODevice::ReadOnly)) return {};
	} else {
		f.setFileName(path);
		if (!f.open(QIODevice::ReadOnly)) return {};
	}
	return f.readAll();
}

QJsonObject entryJson(const partlib::Entry & e, const QStringList & roots) {
	return {{"title", e.title}, {"ref", partlib::ref(roots, e.path)}, {"moduleId", e.moduleId}, {"family", e.family}, {"path", e.path},
	        {"tags", QJsonArray::fromStringList(e.tags)}};
}

int cmdSearch(QStringList args) {
	const int limit = option(args, "--limit", "20").toInt();
	const bool json = flag(args, "--json");
	if (args.isEmpty()) return usage();
	const QStringList roots = partlib::defaultRoots();
	const auto hits = partlib::search(partlib::index(roots), args.join(' '), limit);
	if (json) {
		QJsonArray a;
		for (const auto & e : hits) a << entryJson(e, roots);
		out() << QJsonDocument(a).toJson();
		return 0;
	}
	for (const auto & e : hits) out() << e.title << "\n    " << partlib::ref(roots, e.path) << "\n";
	return hits.isEmpty() ? 1 : 0;
}

int cmdPart(QStringList args) {
	const bool json = flag(args, "--json");
	if (args.size() != 1) return usage();
	const QStringList roots = partlib::defaultRoots();
	const QString path = partlib::resolve(roots, partlib::index(roots), args.first());
	if (path.isEmpty()) {
		err() << "no part matches \"" << args.first() << "\"\n";
		return 1;
	}
	const render::LoadedPart lp = render::loadPart(path);
	if (!lp.error.isEmpty()) {
		err() << lp.error << "\n";
		return 1;
	}
	if (json) {
		QJsonArray cons;
		for (const auto & c : lp.connectors) {
			cons << QJsonObject{{"id", c.id}, {"name", c.name}, {"description", c.description},
			                    {"x", c.local.x()}, {"y", c.local.y()}, {"found", c.found}};
		}
		out() << QJsonDocument(QJsonObject{{"title", lp.part.title}, {"ref", partlib::ref(roots, path)}, {"moduleId", lp.part.moduleId}, {"path", path},
		                                   {"svg", lp.svgPath}, {"width", lp.size.width()}, {"height", lp.size.height()},
		                                   {"connectors", cons}})
		             .toJson();
		return 0;
	}
	out() << lp.part.title << "\n  " << path << "\n  breadboard " << lp.svgPath << "\n  size "
	      << lp.size.width() << " x " << lp.size.height() << " (90/in)\n";
	for (const auto & c : lp.connectors) {
		out() << QString("  %1  %2  (%3, %4)%5\n")
		             .arg(c.id, -12)
		             .arg(c.name, -14)
		             .arg(c.local.x(), 0, 'f', 1)
		             .arg(c.local.y(), 0, 'f', 1)
		             .arg(c.found ? "" : "  [no breadboard geometry]");
	}
	return 0;
}

int cmdRender(QStringList args) {
	const QString svgOut = option(args, "-o");
	const QString pngOut = option(args, "--png");
	const double ppi = option(args, "--ppi", "300").toDouble();
	const bool transparent = flag(args, "--transparent");
	const QString viewArg = option(args, "--view", "breadboard");
	const QString style = option(args, "--theme", "fritzing");
	if (args.size() != 1 || (viewArg != "breadboard" && viewArg != "schematic") || (style != "fritzing" && style != "modern")) return usage();
	const QString view = viewArg == "schematic" ? sketch::SchematicView : sketch::BreadboardView;
	const QByteArray input = readInput(args.first());
	if (input.isEmpty()) {
		err() << "cannot read " << args.first() << "\n";
		return 1;
	}
	const QStringList roots = partlib::defaultRoots();
	QStringList warnings;
	render::Result r;
	if (fzz::detect(input) == fzz::Format::Json) {
		if (view != sketch::BreadboardView) {
			err() << "JSON sketches have breadboard positions only; the schematic view needs an .fzz or .fz "
			         "(see https://github.com/LeonFedotov/fritzing-render/issues/1)\n";
			return 1;
		}
		const sketch::ParseResult parsed = sketch::parse(input);
		if (!parsed.error.isEmpty()) {
			err() << parsed.error << "\n";
			return 1;
		}
		r = render::renderSketch(parsed.sketch, roots, partlib::index(roots));
	} else {
		// A Fritzing sketch: its parts come resolved to files, some perhaps unpacked here.
		const QTemporaryDir work;
		const fzz::Loaded loaded = fzz::load(input, roots, work.path(), view);
		if (!loaded.error.isEmpty()) {
			err() << loaded.error << "\n";
			return 1;
		}
		warnings = loaded.warnings;
		sketch::Sketch sk = loaded.sketch;
		sk.style = style;
		r = render::renderSketch(sk, roots, {});
	}
	if (!r.error.isEmpty()) {
		err() << r.error << "\n";
		return 1;
	}
	for (const QString & w : warnings + r.warnings) err() << "warning: " << w << "\n";
	if (svgOut.isEmpty() && pngOut.isEmpty()) {
		out() << r.svg;
		return 0;
	}
	if (!svgOut.isEmpty()) {
		QFile f(svgOut);
		if (!f.open(QIODevice::WriteOnly) || f.write(r.svg.toUtf8()) < 0) {
			err() << "cannot write " << svgOut << "\n";
			return 1;
		}
	}
	if (!pngOut.isEmpty() && !render::rasterize(r.svg, r.size, ppi, transparent).save(pngOut)) {
		err() << "cannot write " << pngOut << "\n";
		return 1;
	}
	return 0;
}

}  // namespace

int main(int argc, char * argv[]) {
	// No window system needed: Qt's offscreen platform still gives fonts and painting.
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
	// Trust part drawings (QtSvg::AssumeTrustedSource) in Fritzing's own SVG
	// loading too, so deeply nested ones keep their connectors.
	if (qEnvironmentVariableIsEmpty("QT_SVG_DEFAULT_OPTIONS")) qputenv("QT_SVG_DEFAULT_OPTIONS", "2");
	// Font fallbacks, and part drawings Qt's SVG renderer finds fault with, are not the user's to fix.
	if (qEnvironmentVariableIsEmpty("QT_LOGGING_RULES")) qputenv("QT_LOGGING_RULES", "qt.qpa.fonts=false;qt.svg=false");
	QApplication app(argc, argv);
	// Fritzing's fonts (Droid Sans, Noto Sans, OCR-A, ...), which part drawings and labels name.
	QDirIterator fonts(QStringLiteral(FR_FRITZING_APP) + "/resources/fonts", {"*.ttf", "*.otf"}, QDir::Files, QDirIterator::Subdirectories);
	while (fonts.hasNext()) QFontDatabase::addApplicationFont(fonts.next());
	QStringList args = app.arguments().mid(1);
	if (args.isEmpty()) return usage();
	const QString cmd = args.takeFirst();
	if (cmd == "render") return cmdRender(args);
	if (cmd == "search") return cmdSearch(args);
	if (cmd == "part") return cmdPart(args);
	return usage();
}
