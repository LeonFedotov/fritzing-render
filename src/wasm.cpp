// The web build's entry: the renderer as a module a page calls. The page
// loads the parts a sketch needs into the virtual file system under /parts
// (the same layout as vendor/), then asks for the drawing.
//
//   modules(bytes)              -> JSON array: the parts the sketch names
//                                  (module ids for .fzz/.fz, part refs for JSON)
//   render(bytes, view, theme)  -> JSON {svg, width, height, warnings, error}
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
#include "sketch.h"

namespace {

QByteArray bytesOf(const std::string & s) {
	return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
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
	const QStringList roots = partlib::defaultRoots();
	QStringList warnings;
	render::Result r;
	if (fzz::detect(data) == fzz::Format::Json) {
		if (view != sketch::BreadboardView) {
			r.error = "JSON sketches have breadboard positions only; the schematic view needs an .fzz or .fz";
		} else {
			const sketch::ParseResult parsed = sketch::parse(data);
			if (!parsed.error.isEmpty()) r.error = parsed.error;
			else r = render::renderSketch(parsed.sketch, roots, partlib::index(roots));
		}
	} else {
		const QTemporaryDir work;
		const fzz::Loaded loaded = fzz::load(data, roots, work.path(), view);
		if (!loaded.error.isEmpty()) {
			r.error = loaded.error;
		} else {
			warnings = loaded.warnings;
			sketch::Sketch sk = loaded.sketch;
			sk.style = QString::fromStdString(theme);
			r = render::renderSketch(sk, roots, {});
		}
	}
	return json(QJsonDocument(QJsonObject{{"svg", r.svg},
	                                      {"width", r.size.width() / render::SceneDpi},
	                                      {"height", r.size.height() / render::SceneDpi},
	                                      {"warnings", QJsonArray::fromStringList(warnings + r.warnings)},
	                                      {"error", r.error}}));
}

}  // namespace

EMSCRIPTEN_BINDINGS(fritzing_render) {
	emscripten::function("modules", &listModules);
	emscripten::function("render", &renderBytes);
}

int main(int argc, char * argv[]) {
	// Kept for the page's lifetime: the bindings run after main returns.
	static QApplication * app = new QApplication(argc, argv);
	Q_UNUSED(app);
	QDirIterator fonts(QStringLiteral(FR_FRITZING_APP) + "/resources/fonts", {"*.ttf", "*.otf"}, QDir::Files, QDirIterator::Subdirectories);
	while (fonts.hasNext()) QFontDatabase::addApplicationFont(fonts.next());
	return 0;
}
