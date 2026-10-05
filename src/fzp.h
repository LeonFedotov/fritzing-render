#pragma once
// A Fritzing part definition (.fzp): what the renderer needs from it.
// Replaces Fritzing's ModelPartShared, which pulls in the whole application.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace fzp {

// One connector's geometry ids in one view, from <p layer svgId terminalId legId>.
struct ConnectorView {
	QString layer;
	QString svgId;
	QString terminalId;
	QString legId;
};

struct Connector {
	QString id;
	QString name;
	QString description;
	QHash<QString, ConnectorView> views;  // keyed by view name, e.g. "breadboardView"
};

struct View {
	QString image;        // e.g. "breadboard/foo.svg", relative to the parts svg folder
	QStringList layers;   // layerIds, e.g. "breadboard", "breadboardbreadboard"
};

struct Part {
	QString path;         // the .fzp file
	QString moduleId;
	QString title;
	QString family;
	QStringList tags;
	QHash<QString, View> views;
	QList<Connector> connectors;
	bool ok = false;
	QString error;
};

Part read(const QString & path);

// The connector whose id or name matches `ref` (ids first, then names,
// case-insensitive), or nullptr.
const Connector * findConnector(const Part & part, const QString & ref);

}  // namespace fzp
