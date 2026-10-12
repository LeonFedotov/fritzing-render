// The web build's entry: the renderer as a module a page calls. The page
// loads the parts a sketch needs into the virtual file system under /parts
// (the same layout as vendor/), then asks for the drawing.
//
// Parts the person adds on the page live in /parts/user-parts, searched
// before the libraries, so their copy of a part wins.
//
//   modules(bytes)              -> JSON array: the parts the sketch names
//                                  (module ids for .fzz/.fz, part refs for JSON)
//   render(bytes, view, theme)  -> JSON {svg, width, height, left, top, warnings, error}
//                                  (sizes in inches; left/top: the scene point
//                                  at the drawing's top-left corner)
//   describe(ref)               -> JSON {title, moduleId, path, width, height,
//                                  connectors: [{id, name, description, x, y, found}]}
//                                  the part's breadboard view, scene units (90/in)
//
// Fritzing's resources (wire colors, templates, symbols, fonts) are
// preloaded at /fritzing-app/resources.

#include <QApplication>
#include <QDirIterator>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <emscripten/bind.h>

#include <string>

#include "fzz.h"
#include "partlib.h"
#include "render.h"
#include "schematic.h"
#include "sketch.h"

namespace {

QByteArray bytesOf(const std::string & s) {
	return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
}

QStringList roots() {
	return QStringList{QStringLiteral("/parts/user-parts")} + partlib::defaultRoots();
}

std::string json(const QJsonDocument & doc) {
	return doc.toJson(QJsonDocument::Compact).toStdString();
}

std::string listModules(const std::string & input) {
	const QByteArray data = bytesOf(input);
	QStringList refs;
	if (fzz::detect(data) == fzz::Format::Json) {
		for (const sketch::PartSpec & p : sketch::parse(data).sketch.parts) {
			if (!p.part.isEmpty() && !refs.contains(p.part)) refs << p.part;
		}
	} else {
		refs = fzz::moduleIds(data);
	}
	return json(QJsonDocument(QJsonArray::fromStringList(refs)));
}

std::string renderBytes(const std::string & input, const std::string & viewName, const std::string & theme) {
	const QByteArray data = bytesOf(input);
	const QString view = viewName == "schematic" ? sketch::SchematicView : sketch::BreadboardView;
	const QStringList roots = ::roots();
	QStringList warnings;
	render::Result r;
	const QTemporaryDir work;
	if (fzz::detect(data) == fzz::Format::Json) {
		const sketch::ParseResult parsed = sketch::parse(data);
		const QList<partlib::Entry> entries = partlib::index(roots);
		sketch::Sketch sk = parsed.sketch;
		if (!parsed.error.isEmpty()) {
			r.error = parsed.error;
		} else if (view == sketch::SchematicView) {
			const schematic::Result s = schematic::fromBreadboard(sk, roots, entries, work.path());
			r.error = s.error;
			warnings = s.warnings;
			sk = s.sketch;
		}
		if (r.error.isEmpty()) {
			sk.style = QString::fromStdString(theme);
			r = render::renderSketch(sk, roots, entries);
		}
	} else {
		schematic::Result s;
		if (view == sketch::SchematicView) {
			s = schematic::fromFritzing(data, roots, work.path());
		} else {
			const fzz::Loaded loaded = fzz::load(data, roots, work.path(), view);
			s = {loaded.sketch, loaded.warnings, loaded.error};
		}
		r.error = s.error;
		warnings = s.warnings;
		if (r.error.isEmpty()) {
			s.sketch.style = QString::fromStdString(theme);
			r = render::renderSketch(s.sketch, roots, {});
		}
	}
	return json(QJsonDocument(QJsonObject{{"svg", r.svg},
	                                      {"width", r.size.width() / render::SceneDpi},
	                                      {"height", r.size.height() / render::SceneDpi},
	                                      {"left", r.origin.x() / render::SceneDpi},
	                                      {"top", r.origin.y() / render::SceneDpi},
	                                      {"warnings", QJsonArray::fromStringList(warnings + r.warnings)},
	                                      {"error", r.error}}));
}

std::string describe(const std::string & ref) {
	const QStringList roots = ::roots();
	const QString path = partlib::resolve(roots, partlib::index(roots), QString::fromStdString(ref));
	if (path.isEmpty()) return json(QJsonDocument(QJsonObject{{"error", QString("no part matches \"%1\"").arg(QString::fromStdString(ref))}}));
	const render::LoadedPart lp = render::loadPart(path);
	if (!lp.error.isEmpty()) return json(QJsonDocument(QJsonObject{{"error", lp.error}}));
	QJsonArray cons;
	for (const auto & c : lp.connectors) {
		cons << QJsonObject{{"id", c.id}, {"name", c.name}, {"description", c.description}, {"x", c.local.x()}, {"y", c.local.y()}, {"found", c.found}};
	}
	return json(QJsonDocument(QJsonObject{{"title", lp.part.title}, {"moduleId", lp.part.moduleId}, {"path", path},
	                                      {"width", lp.size.width()}, {"height", lp.size.height()}, {"connectors", cons}}));
}

}  // namespace

EMSCRIPTEN_BINDINGS(fritzing_render) {
	emscripten::function("modules", &listModules);
	emscripten::function("render", &renderBytes);
	emscripten::function("describe", &describe);
}

int main(int argc, char * argv[]) {
	// Kept for the page's lifetime: the bindings run after main returns.
	static QApplication * app = new QApplication(argc, argv);
	Q_UNUSED(app);
	QDirIterator fonts(QStringLiteral(FR_FRITZING_APP) + "/resources/fonts", {"*.ttf", "*.otf"}, QDir::Files, QDirIterator::Subdirectories);
	while (fonts.hasNext()) QFontDatabase::addApplicationFont(fonts.next());
	return 0;
}
