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
		w.from = o.value("from").toString();
		w.to = o.value("to").toString();
		w.color = o.value("color").toString("blue");
		for (const QJsonValue & p : o.value("via").toArray()) w.via << point(p);
		if (w.from.isEmpty() || w.to.isEmpty()) return {{}, "every wire needs \"from\" and \"to\""};
		s.wires << w;
	}
	if (s.parts.isEmpty()) return {{}, "the sketch has no parts"};
	return {s, {}};
}

}  // namespace sketch
