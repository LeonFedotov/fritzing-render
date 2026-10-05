#include "fzz.h"

#include <QBuffer>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <private/qzipreader_p.h>

#include "generated.h"
#include "partlib.h"

namespace fzz {

namespace {

// ViewGeometry::WireFlag: wires that belong to the PCB or schematic view, or
// are ratsnest lines; Fritzing saves every view's geometry for every wire.
constexpr int PCBTraceFlag = 4;
constexpr int RatsnestFlag = 16;
constexpr int SchematicTraceFlag = 128;

// Per view: the layers that hold parts (breadboards use their own), the
// notes layer, and the prefix of the layers its items live on.
struct ViewInfo {
	QSet<QString> partLayers;
	QString noteLayer;
	QString layerPrefix;
};

ViewInfo viewInfo(const QString & view) {
	if (view == sketch::SchematicView) return {{"schematic", "schematicText"}, "schematicNote", "schematic"};
	return {{"breadboard", "breadboardbreadboard"}, "breadboardNote", "breadboard"};
}

// Schematic symbols that are Fritzing's own items rather than library parts.
bool isNetLabel(const QString & moduleId) {
	return moduleId == "NetLabelModuleID" || moduleId == "LeftNetLabelModuleID" || moduleId == "v5NetLabelModuleID";
}

QPointF point(const QDomElement & e) {
	return QPointF(e.attribute("x").toDouble(), e.attribute("y").toDouble());
}

// Bezier::fromElement: <bezier><cp0 x y/><cp1 x y/></bezier>, or empty.
QPolygonF bezier(const QDomElement & e) {
	const QDomElement cp0 = e.firstChildElement("cp0");
	const QDomElement cp1 = e.firstChildElement("cp1");
	if (cp0.isNull() || cp1.isNull()) return {};
	return QPolygonF({point(cp0), point(cp1)});
}

QTransform transformOf(const QDomElement & parent) {
	const QDomElement t = parent.firstChildElement("transform");
	if (t.isNull()) return {};
	auto m = [&](const char * name) { return t.attribute(name).toDouble(); };
	return QTransform(m("m11"), m("m12"), m("m13"), m("m21"), m("m22"), m("m23"), m("m31"), m("m32"), m("m33"));
}

// QGraphicsItem::sceneTransform for a top-level item: its transform, then its position.
QTransform placement(const QDomElement & geometry) {
	return transformOf(geometry) * QTransform::fromTranslate(geometry.attribute("x").toDouble(), geometry.attribute("y").toDouble());
}

QString property(const QDomElement & instance, const QString & name) {
	for (QDomElement p = instance.firstChildElement("property"); !p.isNull(); p = p.nextSiblingElement("property")) {
		if (p.attribute("name").compare(name, Qt::CaseInsensitive) == 0) return p.attribute("value");
	}
	return {};
}

// ConnectorItem::saveInstance writes a leg's points relative to the
// connector, whose own position is in part coordinates.
QHash<QString, sketch::Leg> legs(const QDomElement & view) {
	QHash<QString, sketch::Leg> out;
	const QDomElement connectors = view.firstChildElement("connectors");
	for (QDomElement c = connectors.firstChildElement("connector"); !c.isNull(); c = c.nextSiblingElement("connector")) {
		const QDomElement leg = c.firstChildElement("leg");
		if (leg.isNull()) continue;
		const QPointF origin = point(c.firstChildElement("geometry"));
		sketch::Leg l;
		for (QDomElement e = leg.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
			if (e.tagName() == "point") {
				l.points << origin + point(e);
			} else if (e.tagName() == "bezier") {
				// written after each point, for the segment that starts there
				QPolygonF cps = bezier(e);
				if (!cps.isEmpty()) cps.translate(origin);
				l.curves << cps;
			}
		}
		if (l.points.size() < 2) continue;
		while (l.curves.size() >= l.points.size()) l.curves.removeLast();
		while (l.curves.size() < l.points.size() - 1) l.curves << QPolygonF();
		out.insert(c.attribute("connectorId"), l);
	}
	return out;
}

// Whether a wire ends on an item this view doesn't show (in old sketches,
// schematic-only symbols in the breadboard view); Wire::checkVisibility
// hides such wires.
bool attachedToHidden(const QDomElement & view, const QString & layerPrefix) {
	const QDomNodeList connects = view.elementsByTagName("connect");
	for (int i = 0; i < connects.count(); i++) {
		if (!connects.at(i).toElement().attribute("layer").startsWith(layerPrefix)) return true;
	}
	return false;
}

// Whether a wire belongs to the view: breadboard wires, or schematic traces.
bool drawnIn(int flags, const QString & view) {
	if (flags & RatsnestFlag) return false;
	if (view == sketch::SchematicView) return flags & SchematicTraceFlag;
	return !(flags & (PCBTraceFlag | SchematicTraceFlag));
}

// One wire: Wire::sceneCurve / getPaintLine, relative to its position.
// Schematic traces have no shadow (Wire::hasShadow).
sketch::WireSpec wire(const QDomElement & view, const QString & viewName) {
	const QDomElement g = view.firstChildElement("geometry");
	const QPointF pos = point(g);
	sketch::WireSpec w;
	w.fixed = true;
	w.p1 = pos + QPointF(g.attribute("x1").toDouble(), g.attribute("y1").toDouble());
	w.p2 = pos + QPointF(g.attribute("x2").toDouble(), g.attribute("y2").toDouble());
	const QDomElement extras = view.firstChildElement("wireExtras");
	w.color = extras.attribute("color", viewName == sketch::SchematicView ? "#404040" : "blue");
	bool ok = false;
	const double mils = extras.attribute("mils").toDouble(&ok);
	if (ok && mils > 0) w.width = mils * 90 / 1000;
	w.curve = bezier(extras.firstChildElement("bezier"));
	if (!w.curve.isEmpty()) w.curve.translate(pos);
	w.shadow = viewName != sketch::SchematicView;
	return w;
}

// PartLabel::displayTexts: the title, then each displayed property's value.
QString labelText(const QDomElement & instance, const QDomElement & titleGeometry, const QString & fzpPath) {
	QStringList lines;
	QDomElement key = titleGeometry.firstChildElement("displayKey");
	if (key.isNull()) return instance.firstChildElement("title").text().trimmed();
	QDomDocument fzp;
	QFile file(fzpPath);
	if (file.open(QIODevice::ReadOnly)) fzp.setContent(&file);
	for (; !key.isNull(); key = key.nextSiblingElement("displayKey")) {
		const QString name = key.attribute("key");
		QString text = name.isEmpty() ? instance.firstChildElement("title").text().trimmed() : property(instance, name);
		if (text.isEmpty() && !name.isEmpty()) {
			// the part's own value, if the sketch didn't change it
			const QDomNodeList props = fzp.elementsByTagName("property");
			for (int i = 0; i < props.count(); i++) {
				const QDomElement p = props.at(i).toElement();
				if (p.attribute("name").compare(name, Qt::CaseInsensitive) == 0) text = p.text().trimmed();
			}
		}
		if (!text.isEmpty()) lines << text;
	}
	return lines.join('\n');
}

// Where each wire end in the view joins: other traces (by model index) or a
// part's connector ("index:connectorId"). Used for schematic junction dots.
struct WireEnd {
	QPointF at;
	int wireLinks = 0;
	QStringList partConnectors;
};

// SchematicSketchWidget's big dots (Wire::getConnectedColor): a trace end
// that meets two or more other traces, or a part connector that more than
// one trace ends on.
QList<QPointF> junctionDots(const QList<WireEnd> & ends) {
	QHash<QString, int> tracesOnConnector;
	for (const WireEnd & e : ends) {
		for (const QString & c : e.partConnectors) tracesOnConnector[c]++;
	}
	QList<QPointF> dots;
	auto add = [&](QPointF p) {
		for (const QPointF & d : dots) {
			if (QLineF(d, p).length() < 0.01) return;
		}
		dots << p;
	};
	for (const WireEnd & e : ends) {
		bool big = e.wireLinks >= 2;
		for (const QString & c : e.partConnectors) big = big || tracesOnConnector.value(c) > 1;
		if (big) add(e.at);
	}
	return dots;
}

// Writes the parts in an .fzz into `dir`; returns the .fz's contents.
QByteArray unpack(const QByteArray & zip, const QString & dir, QString & error) {
	QBuffer buffer;
	buffer.setData(zip);
	buffer.open(QIODevice::ReadOnly);
	QZipReader reader(&buffer);
	if (reader.status() != QZipReader::NoError || reader.count() == 0) {
		error = "cannot read the .fzz (not a zip archive?)";
		return {};
	}
	QByteArray fz;
	for (const QZipReader::FileInfo & info : reader.fileInfoList()) {
		if (!info.isFile) continue;
		const QString name = QFileInfo(info.filePath).fileName();  // the archive is flat
		const QByteArray data = reader.fileData(info.filePath);
		if (name.endsWith(".fz")) {
			fz = data;
			continue;
		}
		if (!name.endsWith(".fzp") && !name.endsWith(".svg")) continue;
		QFile out(QDir(dir).filePath(name));
		if (!out.open(QIODevice::WriteOnly) || out.write(data) < 0) {
			error = "cannot write " + out.fileName();
			return {};
		}
	}
	if (fz.isEmpty()) error = "the .fzz has no .fz sketch in it";
	return fz;
}

Loaded parse(const QByteArray & fz, const QStringList & roots, const QList<partlib::Entry> & entries, const QString & workDir,
              const QString & viewName) {
	Loaded out;
	out.sketch.view = viewName;
	const ViewInfo info = viewInfo(viewName);
	QDomDocument doc;
	const auto parsed = doc.setContent(fz);
	if (!parsed) {
		out.error = QString("invalid .fz XML at line %1: %2").arg(parsed.errorLine).arg(parsed.errorMessage);
		return out;
	}

	// Which model indexes are wires drawn in this view, for junctions.
	QSet<QString> traces;
	const QDomElement instances = doc.documentElement().firstChildElement("instances");
	for (QDomElement inst = instances.firstChildElement("instance"); !inst.isNull(); inst = inst.nextSiblingElement("instance")) {
		if (inst.attribute("moduleIdRef") != "WireModuleID") continue;
		const QDomElement view = inst.firstChildElement("views").firstChildElement(viewName);
		if (!view.isNull() && drawnIn(view.firstChildElement("geometry").attribute("wireFlags").toInt(), viewName)) traces << inst.attribute("modelIndex");
	}

	QSet<QString> ids;
	QList<WireEnd> ends;
	int notes = 0;
	for (QDomElement inst = instances.firstChildElement("instance"); !inst.isNull(); inst = inst.nextSiblingElement("instance")) {
		const QDomElement view = inst.firstChildElement("views").firstChildElement(viewName);
		if (view.isNull()) continue;
		const QString moduleId = inst.attribute("moduleIdRef");
		const QString layer = view.attribute("layer");
		if (moduleId == "WireModuleID") {
			if (!traces.contains(inst.attribute("modelIndex")) || attachedToHidden(view, info.layerPrefix)) continue;
			const sketch::WireSpec w = wire(view, viewName);
			out.sketch.wires << w;
			const QDomElement connectors = view.firstChildElement("connectors");
			for (QDomElement c = connectors.firstChildElement("connector"); !c.isNull(); c = c.nextSiblingElement("connector")) {
				WireEnd e;
				e.at = c.attribute("connectorId") == "connector0" ? w.p1 : w.p2;
				const QDomElement connects = c.firstChildElement("connects");
				for (QDomElement k = connects.firstChildElement("connect"); !k.isNull(); k = k.nextSiblingElement("connect")) {
					if (traces.contains(k.attribute("modelIndex"))) {
						e.wireLinks++;
					} else {
						e.partConnectors << k.attribute("modelIndex") + ":" + k.attribute("connectorId");
					}
				}
				ends << e;
			}
			continue;
		}
		if (layer == info.noteLayer) {
			notes++;
			continue;
		}
		if (!info.partLayers.contains(layer)) continue;

		QString id = inst.firstChildElement("title").text().trimmed();
		if (id.isEmpty() || ids.contains(id)) id = QString("%1#%2").arg(id.isEmpty() ? moduleId : id, inst.attribute("modelIndex"));
		ids.insert(id);
		QString path;
		if (isNetLabel(moduleId)) {
			path = generated::netLabel(property(inst, "label"), moduleId.startsWith("Left") || property(inst, "direction") == "left", workDir);
		} else if (generated::isSymbol(moduleId)) {
			path = generated::symbol(moduleId, property(inst, "voltage"), workDir, roots);
		} else {
			path = partlib::resolve(roots, entries, moduleId);
			if (path.isEmpty()) path = generated::make(moduleId, workDir);
		}
		if (path.isEmpty()) {
			out.warnings << QString("%1 (%2) left out: not in the parts libraries, and not a part Fritzing generates that this can make")
			                    .arg(id, moduleId);
			continue;
		}
		const QDomElement geometry = view.firstChildElement("geometry");
		sketch::PartSpec p;
		p.id = id;
		p.part = path;
		p.transform = placement(geometry);
		p.pos = p.transform->map(QPointF(0, 0));
		p.z = geometry.attribute("z").toDouble();
		p.legs = legs(view);
		// PartFactory makes these module ids LED items, which recolor their drawing.
		if (moduleId.endsWith("ColorLEDModuleID")) p.color = property(inst, "color");
		const QDomElement title = view.firstChildElement("titleGeometry");
		if (title.attribute("visible") == "true") {
			p.label = labelText(inst, title, path);
			p.labelAt = point(title);
			p.labelSize = title.attribute("fontSize").toDouble() * 90 / 72;  // points to scene units
			p.labelColor = title.attribute("textColor");
		}
		out.sketch.parts << p;
	}
	out.sketch.dots = junctionDots(ends);
	if (notes > 0) out.warnings << QString("%1 note%2 left out: notes are not drawn").arg(notes).arg(notes == 1 ? "" : "s");
	if (out.sketch.parts.isEmpty()) out.error = QString("the sketch has no parts in its %1 that can be drawn").arg(QString(viewName).replace("View", " view"));
	return out;
}

}  // namespace

Format detect(const QByteArray & data) {
	if (data.startsWith("PK\x03\x04")) return Format::Fzz;
	if (data.trimmed().startsWith('<')) return Format::Fz;
	return Format::Json;
}

Loaded load(const QByteArray & data, const QStringList & roots, const QString & workDir, const QString & view) {
	QStringList all = roots;
	QByteArray fz = data;
	if (detect(data) == Format::Fzz) {
		QString error;
		fz = unpack(data, workDir, error);
		if (!error.isEmpty()) return {{}, {}, error};
		all.prepend(workDir);  // the sketch's own copies of its parts come first
	}
	return parse(fz, all, partlib::index(all, true), workDir, view);
}

}  // namespace fzz
