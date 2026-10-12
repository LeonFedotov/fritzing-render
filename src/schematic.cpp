#include "schematic.h"

#include <QHash>
#include <QLineF>
#include <QRegularExpression>
#include <QPair>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include "fzp.h"
#include "fzz.h"
#include "generated.h"
#include "render.h"

namespace schematic {

namespace {

using sketch::SchematicView;

constexpr double Grid = 9;          // 0.1 in: parts land on the grid, so their pins do
constexpr double Gap = 36;          // between parts, and between rows
constexpr double RowWidth = 720;    // 8 in, or the widest part
constexpr double LabelSize = 7.5;   // a part label's font size: Fritzing's 6 pt
constexpr double Touch = 2.5;       // pins this close on the breadboard are connected

// Connectors, and wire ends that are points, joined into nets.
struct Nets {
	QHash<QString, QString> parent;

	QString find(const QString & k) {
		QString r = k;
		while (parent.contains(r) && parent[r] != r) r = parent[r];
		return r;
	}
	void join(const QString & a, const QString & b) {
		const QString ra = find(a), rb = find(b);
		if (ra != rb) parent[ra] = rb;
		parent.insert(rb, rb);
	}
};

QString key(const QString & part, const QString & connectorId) {
	return part + '\n' + connectorId;
}

struct Item {
	sketch::PartSpec spec;
	fzp::Part part;
	render::LoadedPart drawing;  // the schematic symbol, when `drawn`
	bool drawn = false;
	// on the breadboard, when it has one
	bool placed = false;
	QTransform toScene;
	QHash<QString, QPointF> at;  // connector id -> scene
};

struct Pin {
	int item;
	QString connector;  // id
	QString name;
	QPointF local;      // the terminal, part coordinates
	bool direct;        // wired or touched, not only joined by its part's bus
};

bool isGround(const QString & name) {
	static const QRegularExpression re("^([adp]?gnd\\d*|vss|0v|ground)$", QRegularExpression::CaseInsensitiveOption);
	return re.match(name.trimmed()).hasMatch();
}

bool isPower(const QString & name) {
	static const QRegularExpression re("^\\+?(\\d+(\\.\\d+)?v\\d*|vcc\\w*|vdd\\w*|vin|vbat|vbus|vusb|vsys|bat\\+?|v\\+)$",
	                                   QRegularExpression::CaseInsensitiveOption);
	return re.match(name.trimmed()).hasMatch();
}

// A pin name that says nothing about the net: "3", "Pin 3".
bool isBare(const QString & name) {
	static const QRegularExpression re("^(pin\\s*)?\\d*$", QRegularExpression::CaseInsensitiveOption);
	return re.match(name.trimmed()).hasMatch();
}

QString netName(const QList<Pin> & pins, const QList<Item> & items) {
	for (const Pin & p : pins) {
		if (isGround(p.name)) return "GND";
	}
	// the name most pins share
	QHash<QString, int> counts;
	for (const Pin & p : pins) {
		if (!isBare(p.name)) counts[p.name.toUpper()]++;
	}
	int best = 1;
	QString shared;
	for (const Pin & p : pins) {
		if (counts.value(p.name.toUpper()) > best) {
			best = counts.value(p.name.toUpper());
			shared = p.name;
		}
	}
	if (!shared.isEmpty()) return shared;
	// else the pin's name on the part with the most pins, a power pin's first
	QList<Pin> byPart = pins;
	std::stable_sort(byPart.begin(), byPart.end(), [&](const Pin & a, const Pin & b) {
		const qsizetype na = items[a.item].part.connectors.size(), nb = items[b.item].part.connectors.size();
		return na != nb ? na > nb : a.direct && !b.direct;  // D13, over the ICSP SCK it's bused to
	});
	for (const Pin & p : byPart) {
		if (isPower(p.name)) return p.name;
	}
	for (const Pin & p : byPart) {
		if (isBare(p.name)) continue;
		// between two-pin parts (an LED and its resistor), "anode" says little: name the part too
		if (items[p.item].part.connectors.size() <= 2) return items[p.item].spec.id + "_" + p.name;
		return p.name;
	}
	return {};
}

enum class Side { Left, Right, Top, Bottom };

Side sideOf(QPointF at, QSizeF size) {
	const double left = at.x(), right = size.width() - at.x(), top = at.y(), bottom = size.height() - at.y();
	const double nearest = std::min({left, right, top, bottom});
	if (nearest == left) return Side::Left;
	if (nearest == right) return Side::Right;
	return nearest == top ? Side::Top : Side::Bottom;
}

double snap(double v) {
	return std::ceil(v / Grid - 1e-9) * Grid;
}

}  // namespace

Result fromBreadboard(const sketch::Sketch & bb, const QStringList & roots, const QList<partlib::Entry> & entries, const QString & workDir) {
	Result result;
	QList<Item> items;
	QHash<QString, int> byId;
	for (const sketch::PartSpec & spec : bb.parts) {
		Item it{spec, {}, {}, false, false, {}, {}};
		render::LoadedPart breadboard;
		if (spec.part.isEmpty()) {
			it.drawing = render::genericPart(spec.generic);
			it.part = it.drawing.part;
			it.drawn = true;
			breadboard = it.drawing;
		} else {
			const QString path = partlib::resolve(roots, entries, spec.part);
			if (path.isEmpty()) {
				result.error = QString("part %1: no part matches \"%2\" (try: fritzing-render search ...)").arg(spec.id, spec.part);
				return result;
			}
			it.part = fzp::read(path);
			if (!it.part.ok) {
				result.error = QString("part %1: %2").arg(spec.id, it.part.error);
				return result;
			}
			if (it.part.views.value(SchematicView).layers.contains("schematic")) {
				it.drawing = render::loadPart(path, SchematicView);
				it.drawn = it.drawing.error.isEmpty();
			}
			it.spec.part = path;
			if (it.part.views.contains(sketch::BreadboardView)) breadboard = render::loadPart(path, sketch::BreadboardView);
			if (!it.drawn && !it.part.family.contains("breadboard", Qt::CaseInsensitive)) {
				result.warnings << QString("%1 (%2) left out: it has no schematic symbol").arg(spec.id, it.part.title);
			}
		}
		if (breadboard.part.ok && breadboard.error.isEmpty()) {
			it.placed = true;
			it.toScene = render::partToScene(spec, breadboard);
			it.at = render::sceneConnectors(spec, breadboard);
		}
		byId.insert(spec.id, items.size());
		items << it;
	}

	// The nets: wires; pins that touch on the breadboard (a wire's end on a
	// pin, a part plugged into a breadboard); the buses inside parts.
	Nets nets;
	QStringList wireEnds;
	struct Node {
		QString key;
		QString part;  // empty for a wire's loose end
		QPointF at;
	};
	QList<Node> nodes;
	for (const Item & it : items) {
		for (auto c = it.at.begin(); c != it.at.end(); ++c) nodes << Node{key(it.spec.id, c.key()), it.spec.id, c.value()};
	}
	auto end = [&](const QString & ref, const QString & part, const std::optional<QPointF> & at, const QString & self, QString & out) -> QString {
		if (at) {
			// a point: on whatever pin it lands on
			out = self;
			if (part.isEmpty()) {
				nodes << Node{self, {}, *at};
			} else if (!byId.contains(part)) {
				return QString("no part with id \"%1\"").arg(part);
			} else if (items[byId[part]].placed) {
				nodes << Node{self, {}, items[byId[part]].toScene.map(*at)};
			}
			return {};
		}
		const int dot = ref.indexOf('.');
		if (dot < 0) return QString("\"%1\" is not <part id>.<connector>").arg(ref);
		const QString id = ref.left(dot), con = ref.mid(dot + 1);
		if (!byId.contains(id)) return QString("no part with id \"%1\"").arg(id);
		const fzp::Part & p = items[byId[id]].part;
		const fzp::Connector * c = fzp::findConnector(p, con);
		if (c == nullptr) return QString("%1 (%2) has no connector \"%3\"").arg(id, p.title, con);
		out = key(id, c->id);
		return {};
	};
	for (int i = 0; i < bb.wires.size(); i++) {
		const sketch::WireSpec & w = bb.wires[i];
		QString a, b;
		QString err;
		if (w.fixed) {  // from a .fz: its two end points
			err = end({}, {}, w.p1, QString("@%1.0").arg(i), a);
			if (err.isEmpty()) err = end({}, {}, w.p2, QString("@%1.1").arg(i), b);
		} else {
			err = end(w.from, w.fromPart, w.fromAt, QString("@%1.0").arg(i), a);
			if (err.isEmpty()) err = end(w.to, w.toPart, w.toAt, QString("@%1.1").arg(i), b);
		}
		if (!err.isEmpty()) {
			result.error = QString("wire %1 -> %2: %3").arg(w.from, w.to, err);
			return result;
		}
		nets.join(a, b);
		wireEnds << a << b;
	}
	QSet<QString> touched;
	// touching: within Touch of each other, on different parts (by a grid of buckets)
	QHash<QPair<int, int>, QList<int>> buckets;
	auto cell = [](QPointF p) { return qMakePair(int(std::floor(p.x() / Touch)), int(std::floor(p.y() / Touch))); };
	for (int i = 0; i < nodes.size(); i++) buckets[cell(nodes[i].at)] << i;
	for (int i = 0; i < nodes.size(); i++) {
		const auto [cx, cy] = cell(nodes[i].at);
		for (int dx = -1; dx <= 1; dx++) {
			for (int dy = -1; dy <= 1; dy++) {
				for (int j : buckets.value(qMakePair(cx + dx, cy + dy))) {
					if (j <= i || (!nodes[i].part.isEmpty() && nodes[i].part == nodes[j].part)) continue;
					if (QLineF(nodes[i].at, nodes[j].at).length() > Touch) continue;
					nets.join(nodes[i].key, nodes[j].key);
					touched << nodes[i].key << nodes[j].key;
				}
			}
		}
	}
	for (const Item & it : items) {
		for (const QStringList & bus : it.part.buses) {
			for (const QString & member : bus.mid(1)) nets.join(key(it.spec.id, bus.first()), key(it.spec.id, member));
		}
	}
	QSet<QString> wired;
	QSet<QString> direct(wireEnds.begin(), wireEnds.end());
	direct.unite(touched);
	for (const QString & e : wireEnds) wired.insert(nets.find(e));

	// The drawn pins on each net that has a wire or joins parts, and the net's name.
	QHash<QString, QList<Pin>> pinsOf;
	QStringList order;
	for (int i = 0; i < items.size(); i++) {
		if (!items[i].drawn) continue;
		for (const render::ConnectorPoint & c : items[i].drawing.connectors) {
			const QString root = nets.find(key(items[i].spec.id, c.id));
			if (!c.found) continue;
			if (!pinsOf.contains(root)) order << root;
			const QString k = key(items[i].spec.id, c.id);
			pinsOf[root] << Pin{i, c.id, c.name, c.local, direct.contains(k)};
		}
	}
	order.removeIf([&](const QString & root) {
		QSet<int> parts;
		for (const Pin & p : pinsOf[root]) parts.insert(p.item);
		return !wired.contains(root) && parts.size() < 2;
	});
	QHash<QString, QString> nameOf;
	QHash<QString, QString> taken;  // name -> net
	int unnamed = 0;
	for (const QString & root : order) {
		QString name = netName(pinsOf[root], items);
		if (name.isEmpty()) name = QString("N%1").arg(++unnamed);
		if (name != "GND") {
			const QString base = name;
			for (int n = 2; taken.contains(name) && taken[name] != root; n++) name = QString("%1_%2").arg(base).arg(n);
		}
		taken.insert(name, root);
		nameOf.insert(root, name);
	}

	// Each part with its labels, at the origin, then laid out in rows.
	QHash<QString, render::LoadedPart> labels;  // by path
	double x = 0, y = 0, rowHeight = 0;
	double rowWidth = RowWidth;
	struct Placed {
		sketch::PartSpec spec;
		QList<sketch::PartSpec> labels;
		QRectF footprint;
	};
	QList<Placed> placed;
	for (int i = 0; i < items.size(); i++) {
		const Item & it = items[i];
		if (!it.drawn) continue;
		Placed pl{it.spec, {}, QRectF(QPointF(0, 0), it.drawing.size)};
		// The labelled pins by side; where pins sit closer than a label is
		// tall (0.1 in), that side's labels shrink to fit between them.
		QList<QPair<const render::ConnectorPoint *, Side>> pins;
		QHash<int, QList<double>> along;  // by side: the pins' positions along it
		for (const render::ConnectorPoint & c : it.drawing.connectors) {
			if (!c.found || !nameOf.contains(nets.find(key(it.spec.id, c.id)))) continue;
			const Side side = sideOf(c.local, it.drawing.size);
			pins << qMakePair(&c, side);
			along[int(side)] << (side == Side::Left || side == Side::Right ? c.local.y() : c.local.x());
		}
		QHash<int, double> scaleOf;
		for (auto a = along.begin(); a != along.end(); ++a) {
			std::sort(a->begin(), a->end());
			double closest = Grid;
			for (int k = 1; k < a->size(); k++) {
				if (a->at(k) - a->at(k - 1) > 0.01) closest = qMin(closest, a->at(k) - a->at(k - 1));
			}
			scaleOf.insert(a.key(), qBound(0.4, closest / (Grid + 0.5), 1.0));
		}
		int n = 0;
		for (const auto & [cp, side] : pins) {
			const render::ConnectorPoint & c = *cp;
			const QString root = nets.find(key(it.spec.id, c.id));
			// a label's tip is its connector; it points at the pin, its body away from the part
			const bool left = side == Side::Right || side == Side::Top;
			const QString path = generated::netLabel(nameOf[root], left, workDir);
			if (path.isEmpty()) {
				result.error = "cannot write net labels into " + workDir;
				return result;
			}
			if (!labels.contains(path)) labels.insert(path, render::loadPart(path, SchematicView));
			const render::LoadedPart & lp = labels[path];
			if (!lp.error.isEmpty() || lp.connectors.isEmpty()) {
				result.error = lp.error;
				return result;
			}
			const QPointF tip = lp.connectors[0].local;
			QTransform t;
			t.translate(c.local.x(), c.local.y());
			if (side == Side::Top || side == Side::Bottom) t.rotate(-90);  // reading upwards
			t.scale(scaleOf.value(int(side)), scaleOf.value(int(side)));
			t.translate(-tip.x(), -tip.y());
			sketch::PartSpec label;
			label.id = QString("net:%1:%2.%3").arg(nameOf[root], it.spec.id).arg(++n);
			label.part = path;
			label.transform = t;
			pl.footprint |= t.mapRect(QRectF(QPointF(0, 0), lp.size));
			pl.labels << label;
		}
		sketch::PartSpec & spec = pl.spec;
		// a label tells parts apart: one alone needs none unless the sketch gives it
		if (spec.label.isEmpty() && bb.parts.size() > 1) spec.label = it.part.title;
		if (!spec.label.isEmpty()) {
			qsizetype longest = 0;
			for (const QString & line : spec.label.split('\n')) longest = qMax(longest, line.size());
			spec.labelAt = pl.footprint.topLeft() - QPointF(0, LabelSize * 1.6);
			spec.labelSize = LabelSize;
			pl.footprint |= QRectF(*spec.labelAt, QSizeF(longest * LabelSize * 0.62, LabelSize * 1.3));
		}
		rowWidth = qMax(rowWidth, pl.footprint.width());
		placed << pl;
	}
	if (placed.isEmpty()) {
		result.error = "the sketch has no parts with a schematic symbol";
		return result;
	}
	for (Placed & pl : placed) {
		if (x > 0 && x + pl.footprint.width() > rowWidth) {
			y += rowHeight + Gap;
			x = 0;
			rowHeight = 0;
		}
		const QPointF origin(snap(x - pl.footprint.left()), snap(y - pl.footprint.top()));
		const QTransform move = QTransform::fromTranslate(origin.x(), origin.y());
		sketch::PartSpec spec = pl.spec;
		spec.rotate = 0;
		spec.legs.clear();
		spec.transform = move;
		spec.pos = origin;
		if (spec.labelAt) spec.labelAt = *spec.labelAt + origin;
		result.sketch.parts << spec;
		for (sketch::PartSpec label : pl.labels) {
			label.transform = *label.transform * move;
			result.sketch.parts << label;
		}
		x = origin.x() + pl.footprint.right() + Gap;
		rowHeight = qMax(rowHeight, origin.y() + pl.footprint.bottom() - y);
	}
	result.sketch.view = SchematicView;
	result.sketch.margin = bb.margin;
	result.sketch.style = bb.style;
	return result;
}

Result fromFritzing(const QByteArray & data, const QStringList & roots, const QString & workDir) {
	const fzz::Loaded schematic = fzz::load(data, roots, workDir, SchematicView);
	if (!schematic.sketch.parts.isEmpty()) return {schematic.sketch, schematic.warnings, schematic.error};
	const fzz::Loaded breadboard = fzz::load(data, roots, workDir, sketch::BreadboardView);
	if (!breadboard.error.isEmpty() || breadboard.sketch.parts.isEmpty()) return {{}, schematic.warnings, schematic.error};
	// its parts come resolved to files, perhaps unpacked into workDir
	Result r = fromBreadboard(breadboard.sketch, roots, {}, workDir);
	r.warnings = breadboard.warnings + r.warnings;
	r.warnings.prepend("the sketch has no schematic positions: laid out from its breadboard");
	return r;
}

}  // namespace schematic
