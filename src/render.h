#pragma once
// Breadboard-view rendering, following SketchWidget::renderToSVG: each
// part's view SVG split out and normalized by Fritzing's SvgFileSplitter,
// connector terminals from Fritzing's FSvgRenderer, wires drawn the way
// Wire::makeWireSVG draws them (a shadow line under the wire line).

#include <QHash>
#include <QImage>
#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "fzp.h"
#include "partlib.h"
#include "sketch.h"

namespace render {

// Fritzing scene units per inch, and the export's units per inch.
constexpr double SceneDpi = 90;
constexpr double ExportDpi = 1000;

struct ConnectorPoint {
	QString id;
	QString name;
	QString description;
	QPointF local;  // terminal (or bendable-leg end) in part coordinates, scene units
	bool found = false;
	QString legId;  // bendable ("rubber band") leg: drawn by the renderer, not the part SVG
	QLineF leg;     // body end -> free end, part coordinates
	QString legColor;
	double legWidth = 0;
};

// One part, loaded for the breadboard view.
struct LoadedPart {
	fzp::Part part;
	QString svgPath;
	QString layerId;
	QSizeF size;  // scene units
	QList<ConnectorPoint> connectors;
	QString generatedSvg;  // generic parts: their drawing, in export units
	QString error;
	QString view = sketch::BreadboardView;
};

// A part as drawn in one view (sketch::BreadboardView or SchematicView).
LoadedPart loadPart(const QString & fzpPath, const QString & view = sketch::BreadboardView);

// A labelled block with header pins (0.1 in apart) along its bottom edge,
// for parts the libraries don't have; drawn like Fritzing's mystery part.
LoadedPart genericPart(const sketch::Generic & generic);

struct Result {
	QString svg;
	QSizeF size;            // scene units
	QStringList warnings;
	QString error;          // empty on success
};

Result renderSketch(const sketch::Sketch & sketch, const QStringList & roots, const QList<partlib::Entry> & entries);

// The SVG rasterized at `ppi` pixels per inch, on white unless transparent.
QImage rasterize(const QString & svg, QSizeF sceneSize, double ppi, bool transparent);

}  // namespace render
