#include "render.h"

#include <QColor>
#include <QDomDocument>
#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QTransform>

#include "connectors/svgidlayer.h"
#include "fsvgrenderer.h"
#include "svg/svgfilesplitter.h"
#include "utils/textutils.h"
#include "viewlayer.h"

namespace render {

namespace {

const QString BreadboardView = "breadboardView";

struct WireColor {
	QString wire;
	QString shadow;
};

// Fritzing's breadboard wire palette (resources/ratsnestcolors.xml).
QHash<QString, WireColor> loadWireColors() {
	QHash<QString, WireColor> colors;
	QFile file(QStringLiteral(FR_FRITZING_APP) + "/resources/ratsnestcolors.xml");
	QDomDocument doc;
	if (!file.open(QIODevice::ReadOnly) || !doc.setContent(&file)) return colors;
	for (QDomElement view = doc.documentElement().firstChildElement("view"); !view.isNull(); view = view.nextSiblingElement("view")) {
		if (view.attribute("name") != BreadboardView) continue;
		for (QDomElement c = view.firstChildElement("color"); !c.isNull(); c = c.nextSiblingElement("color")) {
			colors.insert(c.attribute("name").toLower(), WireColor{c.attribute("wire"), c.attribute("shadow")});
		}
	}
	return colors;
}

WireColor wireColor(const QString & spec, const QHash<QString, WireColor> & palette) {
	if (palette.contains(spec.toLower())) return palette.value(spec.toLower());
	const QColor c(spec);
	if (!c.isValid()) return palette.value("blue", WireColor{"#418dd9", "#1b5bb3"});
	return WireColor{c.name(), c.darker(150).name()};
}

QString pickLayer(const fzp::View & view) {
	for (const QString & l : view.layers) {
		if (l == "breadboard") return l;
	}
	return view.layers.value(0);
}

struct Placed {
	sketch::PartSpec spec;
	LoadedPart loaded;
	QTransform toScene;  // part coordinates -> scene
	QRectF sceneRect;
};

QTransform placement(const sketch::PartSpec & spec, QSizeF size) {
	QTransform t;
	t.translate(spec.pos.x(), spec.pos.y());
	t.translate(size.width() / 2, size.height() / 2);
	t.rotate(spec.rotate);
	t.translate(-size.width() / 2, -size.height() / 2);
	return t;
}

QString partSvg(const LoadedPart & lp, QString & error) {
	if (!lp.generatedSvg.isEmpty()) return lp.generatedSvg;
	SvgFileSplitter splitter;
	if (!splitter.split(lp.svgPath, lp.layerId)) {
		error = QString("cannot split layer %1 out of %2").arg(lp.layerId, lp.svgPath);
		return {};
	}
	for (const ConnectorPoint & c : lp.connectors) {
		if (!c.legId.isEmpty() && !c.leg.isNull()) splitter.gReplace(c.legId);
	}
	double factor = 1;
	if (!splitter.normalize(ExportDpi, lp.layerId, false, factor)) {
		error = QString("cannot normalize %1").arg(lp.svgPath);
		return {};
	}
	QString svg = splitter.elementString(lp.layerId);
	TextUtils::fixMuch(svg, false);
	return svg;
}

double toExport(double scene) { return scene * ExportDpi / SceneDpi; }

QString labelSvg(const QString & text, QPointF scenePos) {
	return QString("<text x='%1' y='%2' font-family='Droid Sans, Helvetica, Arial, sans-serif' font-size='%3' fill='#333333'>%4</text>")
	    .arg(toExport(scenePos.x()))
	    .arg(toExport(scenePos.y()))
	    .arg(toExport(9))
	    .arg(text.toHtmlEscaped());
}

// ConnectorItem::makeLegSvg for a leg that hasn't been bent: one straight
// path, in part coordinates (export units), so the part's rotation applies.
QString legsSvg(const LoadedPart & lp) {
	QString out;
	for (const ConnectorPoint & c : lp.connectors) {
		if (c.legId.isEmpty() || c.leg.isNull()) continue;
		out += QString("<path d='M%1,%2 L%3,%4' fill='none' stroke='%5' stroke-width='%6' stroke-linecap='round'/>")
		           .arg(toExport(c.leg.p1().x()))
		           .arg(toExport(c.leg.p1().y()))
		           .arg(toExport(c.leg.p2().x()))
		           .arg(toExport(c.leg.p2().y()))
		           .arg(c.legColor.isEmpty() ? "#8c8c8c" : c.legColor)
		           .arg(toExport(c.legWidth));
	}
	return out;
}

QString connectorList(const LoadedPart & lp) {
	QStringList names;
	for (const ConnectorPoint & c : lp.connectors) names << (c.name.isEmpty() ? c.id : c.name);
	return names.join(", ");
}

}  // namespace

LoadedPart loadPart(const QString & fzpPath) {
	LoadedPart lp;
	lp.part = fzp::read(fzpPath);
	if (!lp.part.ok) {
		lp.error = lp.part.error;
		return lp;
	}
	if (!lp.part.views.contains(BreadboardView)) {
		lp.error = lp.part.title + " has no breadboard view";
		return lp;
	}
	lp.layerId = pickLayer(lp.part.views.value(BreadboardView));
	lp.svgPath = partlib::imagePath(lp.part, BreadboardView);
	if (lp.svgPath.isEmpty()) {
		lp.error = QString("breadboard SVG %1 of %2 not found").arg(lp.part.views.value(BreadboardView).image, fzpPath);
		return lp;
	}

	LoadInfo info(lp.svgPath);
	for (const fzp::Connector & c : lp.part.connectors) {
		const fzp::ConnectorView v = c.views.value(BreadboardView);
		if (v.svgId.isEmpty()) continue;
		info.connectorIDs << v.svgId;
		if (!v.terminalId.isEmpty()) info.terminalIDs << v.terminalId;
		if (!v.legId.isEmpty()) info.legIDs << v.legId;
	}
	FSvgRenderer renderer;
	if (renderer.loadSvg(info).isEmpty()) {
		lp.error = "cannot load " + lp.svgPath;
		return lp;
	}
	lp.size = renderer.defaultSizeF();

	for (const fzp::Connector & c : lp.part.connectors) {
		ConnectorPoint cp{c.id, c.name, c.description, {}, false};
		const fzp::ConnectorView v = c.views.value(BreadboardView);
		if (!v.svgId.isEmpty()) {
			SvgIdLayer layer(ViewLayer::BreadboardView);
			layer.m_svgId = v.svgId;
			layer.m_terminalId = v.terminalId;
			layer.m_legId = v.legId;
			if (renderer.setUpConnector(&layer, false, ViewLayer::NewTop)) {
				cp.legId = v.legId;
				cp.leg = layer.m_legLine;
				cp.legColor = layer.m_legColor;
				cp.legWidth = layer.m_legStrokeWidth;
				// ConnectorItem::adjustedTerminalPoint: a bendable leg ends the connector.
				cp.local = (!v.legId.isEmpty() && !layer.m_legLine.isNull())
				               ? layer.m_legLine.p2()
				               : layer.rect(ViewLayer::NewTop).topLeft() + layer.point(ViewLayer::NewTop);
				cp.found = true;
			}
		}
		lp.connectors << cp;
	}
	return lp;
}

LoadedPart genericPart(const sketch::Generic & g) {
	constexpr double Pitch = 9;      // 0.1 in
	constexpr double TitleSize = 7;  // font size, scene units
	constexpr double PinSize = 5.5;
	const int n = static_cast<int>(g.pins.size());
	const double width = qMax(Pitch * (n - 1) + 2 * Pitch, g.title.size() * TitleSize * 0.56 + 14);
	int longest = 0;
	for (const QString & p : g.pins) longest = qMax(longest, static_cast<int>(p.size()));
	const double height = 16 + longest * PinSize * 0.62 + 10;
	const double firstPin = (width - Pitch * (n - 1)) / 2;

	LoadedPart lp;
	lp.part.ok = true;
	lp.part.title = g.title;
	lp.part.moduleId = "generic";
	lp.size = QSizeF(width, height);
	QString svg = QString("<rect x='0' y='0' width='%1' height='%2' rx='%3' fill='#2b2b2b' stroke='#111111' stroke-width='%4'/>")
	                  .arg(toExport(width)).arg(toExport(height)).arg(toExport(2)).arg(toExport(0.6));
	svg += QString("<text x='%1' y='%2' font-family='Droid Sans, Helvetica, Arial, sans-serif' font-size='%3' fill='#f0f0f0' text-anchor='middle'>%4</text>")
	           .arg(toExport(width / 2)).arg(toExport(11)).arg(toExport(TitleSize)).arg(g.title.toHtmlEscaped());
	for (int i = 0; i < n; i++) {
		const double x = firstPin + i * Pitch;
		const QString id = QString("connector%1").arg(i);
		lp.part.connectors << fzp::Connector{id, g.pins[i], {}, {}};
		lp.connectors << ConnectorPoint{id, g.pins[i], {}, QPointF(x, height), true, {}, {}, {}, 0};
		svg += QString("<rect x='%1' y='%2' width='%3' height='%3' fill='#d9b45a' stroke='#8a6d1f' stroke-width='%4'/>")
		           .arg(toExport(x - 2.25)).arg(toExport(height - 4.5)).arg(toExport(4.5)).arg(toExport(0.4));
		svg += QString("<text transform='translate(%1,%2) rotate(-90)' font-family='Droid Sans Mono, Menlo, monospace' font-size='%3' fill='#f0f0f0'>%4</text>")
		           .arg(toExport(x + PinSize * 0.35)).arg(toExport(height - 7)).arg(toExport(PinSize)).arg(g.pins[i].toHtmlEscaped());
	}
	lp.generatedSvg = svg;
	return lp;
}

Result renderSketch(const sketch::Sketch & sk, const QStringList & roots, const QList<partlib::Entry> & entries) {
	Result result;
	QHash<QString, Placed> placed;
	QStringList order;
	for (const sketch::PartSpec & spec : sk.parts) {
		const QString path = spec.part.isEmpty() ? QString() : partlib::resolve(roots, entries, spec.part);
		if (!spec.part.isEmpty() && path.isEmpty()) {
			result.error = QString("part %1: no part matches \"%2\" (try: fritzing-render search ...)").arg(spec.id, spec.part);
			return result;
		}
		Placed p{spec, spec.part.isEmpty() ? genericPart(spec.generic) : loadPart(path), {}, {}};
		if (!p.loaded.error.isEmpty()) {
			result.error = QString("part %1: %2").arg(spec.id, p.loaded.error);
			return result;
		}
		p.toScene = placement(spec, p.loaded.size);
		p.sceneRect = p.toScene.mapRect(QRectF(QPointF(0, 0), p.loaded.size));
		for (const ConnectorPoint & cp : p.loaded.connectors) {
			if (!cp.leg.isNull()) p.sceneRect |= p.toScene.mapRect(QRectF(cp.leg.p1(), cp.leg.p2()).normalized());
		}
		placed.insert(spec.id, p);
		order << spec.id;
	}

	auto endpoint = [&](const QString & ref, QPointF & out) -> QString {
		const int dot = ref.indexOf('.');
		if (dot < 0) return QString("\"%1\" is not <part id>.<connector>").arg(ref);
		const QString id = ref.left(dot);
		const QString con = ref.mid(dot + 1);
		if (!placed.contains(id)) return QString("no part with id \"%1\"").arg(id);
		const Placed & p = placed[id];
		const fzp::Connector * c = fzp::findConnector(p.loaded.part, con);
		if (c == nullptr) return QString("%1 (%2) has no connector \"%3\"; it has: %4").arg(id, p.loaded.part.title, con, connectorList(p.loaded));
		for (const ConnectorPoint & cp : p.loaded.connectors) {
			if (cp.id != c->id) continue;
			if (!cp.found) return QString("%1.%2 has no breadboard geometry").arg(id, con);
			out = p.toScene.map(cp.local);
			return {};
		}
		return QString("%1.%2 not found").arg(id, con);
	};

	struct Line {
		QList<QPointF> points;
		WireColor color;
	};
	const QHash<QString, WireColor> palette = loadWireColors();
	QList<Line> lines;
	for (const sketch::WireSpec & w : sk.wires) {
		QPointF a, b;
		QString err = endpoint(w.from, a);
		if (err.isEmpty()) err = endpoint(w.to, b);
		if (!err.isEmpty()) {
			result.error = QString("wire %1 -> %2: %3").arg(w.from, w.to, err);
			return result;
		}
		Line line{{a}, wireColor(w.color, palette)};
		line.points << w.via << b;
		lines << line;
	}

	// Everything drawn, in scene units, plus the margin.
	QRectF bounds;
	for (const QString & id : order) {
		const Placed & p = placed[id];
		bounds |= p.sceneRect;
		if (!p.spec.label.isEmpty()) bounds |= QRectF(p.sceneRect.left(), p.sceneRect.top() - 14, p.spec.label.size() * 6, 14);
	}
	for (const Line & l : lines) {
		for (const QPointF & pt : l.points) bounds |= QRectF(pt - QPointF(3, 3), QSizeF(6, 6));
	}
	bounds.adjust(-sk.margin, -sk.margin, sk.margin, sk.margin);
	const QPointF offset = bounds.topLeft();

	QString out = TextUtils::makeSVGHeader(SceneDpi, ExportDpi, bounds.width(), bounds.height());
	for (const QString & id : order) {
		const Placed & p = placed[id];
		QString err;
		const QString svg = partSvg(p.loaded, err);
		if (!err.isEmpty()) {
			result.error = QString("part %1: %2").arg(id, err);
			return result;
		}
		const QPointF at = p.spec.pos - offset;
		out += QString("<g id='%1' transform='translate(%2,%3) rotate(%4,%5,%6)'>%7</g>\n")
		           .arg(id.toHtmlEscaped())
		           .arg(toExport(at.x()))
		           .arg(toExport(at.y()))
		           .arg(p.spec.rotate)
		           .arg(toExport(p.loaded.size.width() / 2))
		           .arg(toExport(p.loaded.size.height() / 2))
		           .arg(svg + legsSvg(p.loaded));
		if (!p.spec.label.isEmpty()) out += labelSvg(p.spec.label, p.sceneRect.topLeft() - offset + QPointF(0, -4)) + "\n";
	}

	// Wire::makeWireSVG: a 4-unit shadow under a 2-unit line (the default
	// breadboard wire, 22.2 mil). Shadows first, so joints stay clean.
	const QVector<qreal> noDash;
	out += "<g id='wires'>\n";
	for (const Line & l : lines) {
		for (int i = 1; i < l.points.size(); i++) {
			out += TextUtils::makeLineSVG(l.points[i - 1] - offset, l.points[i] - offset, 4, l.color.shadow, ExportDpi, SceneDpi, false, false, noDash);
		}
	}
	for (const Line & l : lines) {
		for (int i = 1; i < l.points.size(); i++) {
			out += TextUtils::makeLineSVG(l.points[i - 1] - offset, l.points[i] - offset, 2, l.color.wire, ExportDpi, SceneDpi, false, false, noDash);
		}
	}
	out += "</g>\n</svg>\n";

	result.svg = out;
	result.size = bounds.size();
	return result;
}

QImage rasterize(const QString & svg, QSizeF sceneSize, double ppi, bool transparent) {
	const QSize px(qRound(sceneSize.width() / SceneDpi * ppi), qRound(sceneSize.height() / SceneDpi * ppi));
	QImage image(px, QImage::Format_ARGB32_Premultiplied);
	image.fill(transparent ? Qt::transparent : Qt::white);
	QSvgRenderer renderer(svg.toUtf8());
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter);
	painter.end();
	return image;
}

}  // namespace render
