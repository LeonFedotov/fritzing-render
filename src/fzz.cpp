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
// are ratsnest lines, though Fritzing saves a breadboardView for them too.
constexpr int PCBTraceFlag = 4;
constexpr int RatsnestFlag = 16;
constexpr int SchematicTraceFlag = 128;

// Breadboard-view layers that hold parts (breadboards use their own).
const QSet<QString> PartLayers{"breadboard", "breadboardbreadboard"};

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

// QGraphicsItem::sceneTransform for a top-level item: its transform, then its position.
QTransform placement(const QDomElement & geometry) {
	const QDomElement t = geometry.firstChildElement("transform");
	QTransform transform;
	if (!t.isNull()) {
		auto m = [&](const char * name) { return t.attribute(name).toDouble(); };
		transform = QTransform(m("m11"), m("m12"), m("m13"), m("m21"), m("m22"), m("m23"), m("m31"), m("m32"), m("m33"));
	}
	return transform * QTransform::fromTranslate(geometry.attribute("x").toDouble(), geometry.attribute("y").toDouble());
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
// schematic-only symbols); Wire::checkVisibility hides such wires.
bool attachedToHidden(const QDomElement & view) {
	const QDomNodeList connects = view.elementsByTagName("connect");
	for (int i = 0; i < connects.count(); i++) {
		if (!connects.at(i).toElement().attribute("layer").startsWith("breadboard")) return true;
	}
	return false;
}

// One breadboard wire: Wire::sceneCurve / getPaintLine, relative to its position.
std::optional<sketch::WireSpec> wire(const QDomElement & view) {
	const QDomElement g = view.firstChildElement("geometry");
	const int flags = g.attribute("wireFlags").toInt();
	if (flags & (PCBTraceFlag | RatsnestFlag | SchematicTraceFlag)) return std::nullopt;
	if (attachedToHidden(view)) return std::nullopt;
	const QPointF pos = point(g);
	sketch::WireSpec w;
	w.fixed = true;
	w.p1 = pos + QPointF(g.attribute("x1").toDouble(), g.attribute("y1").toDouble());
	w.p2 = pos + QPointF(g.attribute("x2").toDouble(), g.attribute("y2").toDouble());
	const QDomElement extras = view.firstChildElement("wireExtras");
	w.color = extras.attribute("color", "blue");
	bool ok = false;
	const double mils = extras.attribute("mils").toDouble(&ok);
	if (ok && mils > 0) w.width = mils * 90 / 1000;
	w.curve = bezier(extras.firstChildElement("bezier"));
	if (!w.curve.isEmpty()) w.curve.translate(pos);
	return w;
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

Loaded parse(const QByteArray & fz, const QStringList & roots, const QList<partlib::Entry> & entries, const QString & workDir) {
	Loaded out;
	QDomDocument doc;
	const auto parsed = doc.setContent(fz);
	if (!parsed) {
		out.error = QString("invalid .fz XML at line %1: %2").arg(parsed.errorLine).arg(parsed.errorMessage);
		return out;
	}
	QSet<QString> ids;
	int notes = 0;
	const QDomElement instances = doc.documentElement().firstChildElement("instances");
	for (QDomElement inst = instances.firstChildElement("instance"); !inst.isNull(); inst = inst.nextSiblingElement("instance")) {
		const QDomElement view = inst.firstChildElement("views").firstChildElement("breadboardView");
		if (view.isNull()) continue;
		const QString moduleId = inst.attribute("moduleIdRef");
		const QString layer = view.attribute("layer");
		if (moduleId == "WireModuleID") {
			if (const auto w = wire(view)) out.sketch.wires << *w;
			continue;
		}
		if (layer == "breadboardNote") {
			notes++;
			continue;
		}
		if (!PartLayers.contains(layer)) continue;

		QString id = inst.firstChildElement("title").text().trimmed();
		if (id.isEmpty() || ids.contains(id)) id = QString("%1#%2").arg(id.isEmpty() ? moduleId : id, inst.attribute("modelIndex"));
		ids.insert(id);
		QString path = partlib::resolve(roots, entries, moduleId);
		if (path.isEmpty()) path = generated::make(moduleId, workDir);
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
		if (moduleId.endsWith("ColorLEDModuleID")) {
			for (QDomElement prop = inst.firstChildElement("property"); !prop.isNull(); prop = prop.nextSiblingElement("property")) {
				if (prop.attribute("name") == "color") p.color = prop.attribute("value");
			}
		}
		out.sketch.parts << p;
	}
	if (notes > 0) out.warnings << QString("%1 note%2 left out: notes are not drawn").arg(notes).arg(notes == 1 ? "" : "s");
	if (out.sketch.parts.isEmpty()) out.error = "the sketch has no parts in its breadboard view that can be drawn";
	return out;
}

}  // namespace

Format detect(const QByteArray & data) {
	if (data.startsWith("PK\x03\x04")) return Format::Fzz;
	if (data.trimmed().startsWith('<')) return Format::Fz;
	return Format::Json;
}

Loaded load(const QByteArray & data, const QStringList & roots, const QString & workDir) {
	QStringList all = roots;
	QByteArray fz = data;
	if (detect(data) == Format::Fzz) {
		QString error;
		fz = unpack(data, workDir, error);
		if (!error.isEmpty()) return {{}, {}, error};
		all.prepend(workDir);  // the sketch's own copies of its parts come first
	}
	return parse(fz, all, partlib::index(all, true), workDir);
}

}  // namespace fzz
