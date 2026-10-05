#include "sketch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sketch {

namespace {

QPointF point(const QJsonValue & v) {
	const QJsonArray a = v.toArray();
	return QPointF(a.at(0).toDouble(), a.at(1).toDouble());
}

bool isPoint(const QJsonValue & v) {
	const QJsonArray a = v.toArray();
	return v.isArray() && a.size() == 2 && a.at(0).isDouble() && a.at(1).isDouble();
}

// One wire end: "<part>.<connector>", {"part": id, "at": [x, y]} or [x, y].
// `ref` gets a readable name for messages. Returns an error, or empty.
QString wireEnd(const QJsonValue & v, QString & ref, QString & part, std::optional<QPointF> & at) {
	if (v.isString()) {
		ref = v.toString();
		return ref.isEmpty() ? "every wire needs \"from\" and \"to\"" : QString();
	}
	if (v.isArray()) {
		if (!isPoint(v)) return "a wire end given as a point must be [x, y]";
		at = point(v);
		ref = QString("(%1, %2)").arg(at->x()).arg(at->y());
		return {};
	}
	if (v.isObject()) {
		const QJsonObject o = v.toObject();
		part = o.value("part").toString();
		if (part.isEmpty() || !isPoint(o.value("at"))) return "a wire end on a part needs \"part\" and \"at\": [x, y]";
		at = point(o.value("at"));
		ref = QString("%1@(%2, %3)").arg(part).arg(at->x()).arg(at->y());
		return {};
	}
	return "every wire needs \"from\" and \"to\"";
}

}  // namespace

ParseResult parse(const QByteArray & json) {
	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
	if (err.error != QJsonParseError::NoError) return {{}, "invalid JSON: " + err.errorString()};
	if (!doc.isObject()) return {{}, "the sketch must be a JSON object"};
	const QJsonObject root = doc.object();

	Sketch s;
	s.margin = root.value("margin").toDouble(18);
	for (const QJsonValue & v : root.value("parts").toArray()) {
		const QJsonObject o = v.toObject();
		PartSpec p;
		p.id = o.value("id").toString();
		p.part = o.value("part").toString();
		p.pos = QPointF(o.value("x").toDouble(), o.value("y").toDouble());
		p.rotate = o.value("rotate").toInt();
		p.label = o.value("label").toString();
		p.labelBelow = o.value("labelBelow").toBool();
		p.color = o.value("color").toString();
		const QJsonObject generic = o.value("generic").toObject();
		p.generic.title = generic.value("title").toString();
		for (const QJsonValue & pin : generic.value("pins").toArray()) p.generic.pins << pin.toString();
		if (p.id.isEmpty() || (p.part.isEmpty() && generic.isEmpty())) return {{}, "every part needs an \"id\" and a \"part\" (or \"generic\")"};
		if (p.part.isEmpty() && p.generic.pins.isEmpty()) return {{}, QString("generic part %1 needs pins").arg(p.id)};
		if (p.rotate % 90 != 0) return {{}, QString("part %1: rotate must be a multiple of 90").arg(p.id)};
		s.parts << p;
	}
	for (const QJsonValue & v : root.value("wires").toArray()) {
		const QJsonObject o = v.toObject();
		WireSpec w;
		QString err = wireEnd(o.value("from"), w.from, w.fromPart, w.fromAt);
		if (err.isEmpty()) err = wireEnd(o.value("to"), w.to, w.toPart, w.toAt);
		if (!err.isEmpty()) return {{}, err};
		w.color = o.value("color").toString("blue");
		for (const QJsonValue & p : o.value("via").toArray()) w.via << point(p);
		s.wires << w;
	}
	if (s.parts.isEmpty()) return {{}, "the sketch has no parts"};
	return {s, {}};
}

}  // namespace sketch
