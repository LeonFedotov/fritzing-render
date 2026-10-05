#include "fzp.h"

#include <QDomDocument>
#include <QFile>

namespace fzp {

namespace {

QString childText(const QDomElement & parent, const QString & tag) {
	return parent.firstChildElement(tag).text().trimmed();
}

QHash<QString, View> readViews(const QDomElement & root) {
	QHash<QString, View> views;
	const QDomElement viewsEl = root.firstChildElement("views");
	for (QDomElement v = viewsEl.firstChildElement(); !v.isNull(); v = v.nextSiblingElement()) {
		const QDomElement layers = v.firstChildElement("layers");
		View view;
		view.image = layers.attribute("image");
		for (QDomElement l = layers.firstChildElement("layer"); !l.isNull(); l = l.nextSiblingElement("layer")) {
			view.layers << l.attribute("layerId");
		}
		views.insert(v.tagName(), view);
	}
	return views;
}

QList<Connector> readConnectors(const QDomElement & root) {
	QList<Connector> out;
	const QDomElement list = root.firstChildElement("connectors");
	for (QDomElement c = list.firstChildElement("connector"); !c.isNull(); c = c.nextSiblingElement("connector")) {
		Connector con;
		con.id = c.attribute("id");
		con.name = c.attribute("name");
		con.description = childText(c, "description");
		const QDomElement views = c.firstChildElement("views");
		for (QDomElement v = views.firstChildElement(); !v.isNull(); v = v.nextSiblingElement()) {
			const QDomElement p = v.firstChildElement("p");
			if (p.isNull()) continue;
			con.views.insert(v.tagName(), ConnectorView{p.attribute("layer"), p.attribute("svgId"),
			                                            p.attribute("terminalId"), p.attribute("legId")});
		}
		out << con;
	}
	return out;
}

}  // namespace

Part read(const QString & path) {
	Part part;
	part.path = path;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		part.error = "cannot open " + path;
		return part;
	}
	QDomDocument doc;
	const QDomDocument::ParseResult parsed = doc.setContent(&file);
	if (!parsed) {
		part.error = QString("%1: %2 (line %3)").arg(path, parsed.errorMessage).arg(parsed.errorLine);
		return part;
	}
	const QDomElement root = doc.documentElement();
	part.moduleId = root.attribute("moduleId");
	part.title = childText(root, "title");
	const QDomElement props = root.firstChildElement("properties");
	for (QDomElement p = props.firstChildElement("property"); !p.isNull(); p = p.nextSiblingElement("property")) {
		if (p.attribute("name").compare("family", Qt::CaseInsensitive) == 0) part.family = p.text().trimmed();
	}
	const QDomElement tags = root.firstChildElement("tags");
	for (QDomElement t = tags.firstChildElement("tag"); !t.isNull(); t = t.nextSiblingElement("tag")) {
		part.tags << t.text().trimmed();
	}
	part.views = readViews(root);
	part.connectors = readConnectors(root);
	part.ok = true;
	return part;
}

const Connector * findConnector(const Part & part, const QString & ref) {
	for (const Connector & c : part.connectors) {
		if (c.id.compare(ref, Qt::CaseInsensitive) == 0) return &c;
	}
	for (const Connector & c : part.connectors) {
		if (c.name.compare(ref, Qt::CaseInsensitive) == 0) return &c;
	}
	return nullptr;
}

}  // namespace fzp
