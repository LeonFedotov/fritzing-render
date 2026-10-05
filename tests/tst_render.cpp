// Fixture parts (tests/fixtures) have known geometry: a 0.4 x 0.2 in body
// (36 x 18 scene units) with IN at (4.5, 14.4) and OUT's terminal at
// (31.5, 17.775). Tests marked "vendor" use the real parts libraries and
// skip when scripts/fetch-vendor.sh hasn't been run.

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include "fzp.h"
#include "fzz.h"
#include "generated.h"
#include "partlib.h"
#include "render.h"
#include "sketch.h"

namespace {

const QString Fixtures = QStringLiteral(FR_TESTS_DIR) + "/fixtures";
const QStringList FixtureRoots{Fixtures + "/parts", Fixtures + "/fzpz"};

QString vendorPart(const QString & rel) {
	return QStringLiteral(FR_VENDOR_DIR) + "/fritzing-parts/" + rel;
}

// Test JSON is written with single quotes (moc can't parse raw string literals).
QByteArray J(const char * text) {
	return QByteArray(text).replace('\'', '"');
}

QByteArray readFile(const QString & path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// tests/fixtures/sketch.fz, its parts resolved against the fixture libraries.
fzz::Loaded fixtureSketch() {
	return fzz::load(readFile(Fixtures + "/sketch.fz"), FixtureRoots, {});
}

const sketch::PartSpec * partNamed(const sketch::Sketch & s, const QString & id) {
	for (const auto & p : s.parts) {
		if (p.id == id) return &p;
	}
	return nullptr;
}

// The first wire line's coordinates in an SVG (export units).
QList<double> firstLine(const QString & svg, const QString & color) {
	const QRegularExpression re(QString("<line[^>]*stroke='%1' x1='([-\\d.e]+)' y1='([-\\d.e]+)' x2='([-\\d.e]+)' y2='([-\\d.e]+)'").arg(color));
	const auto m = re.match(svg);
	if (!m.hasMatch()) return {};
	return {m.captured(1).toDouble(), m.captured(2).toDouble(), m.captured(3).toDouble(), m.captured(4).toDouble()};
}

}  // namespace

class TestRender : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void fzpReadsHeaderViewsAndConnectors() {
		const fzp::Part p = fzp::read(Fixtures + "/parts/core/testpart.fzp");
		QVERIFY2(p.ok, qPrintable(p.error));
		QCOMPARE(p.title, QString("Test Part"));
		QCOMPARE(p.moduleId, QString("TestPartModuleID"));
		QCOMPARE(p.family, QString("test family"));
		QCOMPARE(p.views.value("breadboardView").image, QString("breadboard/testpart.svg"));
		QCOMPARE(p.connectors.size(), 2);
		QCOMPARE(p.connectors[1].views.value("breadboardView").terminalId, QString("connector1terminal"));
	}

	void fzpFindsConnectorsByIdThenName() {
		const fzp::Part p = fzp::read(Fixtures + "/parts/core/testpart.fzp");
		QCOMPARE(fzp::findConnector(p, "connector1")->name, QString("OUT"));
		QCOMPARE(fzp::findConnector(p, "in")->id, QString("connector0"));
		QVERIFY(fzp::findConnector(p, "nope") == nullptr);
	}

	void fzpReportsMissingFile() {
		const fzp::Part p = fzp::read(Fixtures + "/missing.fzp");
		QVERIFY(!p.ok);
		QVERIFY(p.error.contains("cannot open"));
	}

	void imagePathInBothLayouts() {
		const fzp::Part lib = fzp::read(Fixtures + "/parts/core/testpart.fzp");
		QCOMPARE(QFileInfo(partlib::imagePath(lib, "breadboardView")).fileName(), QString("testpart.svg"));
		const fzp::Part flat = fzp::read(Fixtures + "/fzpz/part.flat.fzp");
		QCOMPARE(QFileInfo(partlib::imagePath(flat, "breadboardView")).fileName(), QString("svg.breadboard.flat.svg"));
		QVERIFY(partlib::imagePath(lib, "pcbView").isEmpty());
	}

	void resolveByPathModuleIdAndTitle() {
		const auto entries = partlib::index(FixtureRoots);
		QCOMPARE(entries.size(), 7);
		QVERIFY(partlib::resolve(FixtureRoots, entries, "core/testpart.fzp").endsWith("testpart.fzp"));
		QVERIFY(partlib::resolve(FixtureRoots, entries, "FlatModuleID").endsWith("part.flat.fzp"));
		QVERIFY(partlib::resolve(FixtureRoots, entries, "test part").endsWith("testpart.fzp"));
		QVERIFY(partlib::resolve(FixtureRoots, entries, "nothing like it").isEmpty());
	}

	void defaultRootsAreTheVendorLibraries() {
		qunsetenv("FRITZING_PARTS");
		QStringList names;
		for (const QString & root : partlib::defaultRoots()) names << QFileInfo(root).fileName();
		QCOMPARE(names, QStringList({"fritzing-parts", "adafruit-parts", "sparkfun-parts", "community-parts", "parts"}));
	}

	void ownPartsAreInTheLibrary() {
		// parts/ holds parts made here, e.g. the RedBear BLE Nano v1.5 from RedBear's gerbers.
		qunsetenv("FRITZING_PARTS");
		const QStringList roots = partlib::defaultRoots();
		const QString path = partlib::resolve(roots, partlib::index(roots), "RedBearBLENanoV1_5ModuleID");
		QVERIFY2(!path.isEmpty(), "BLE Nano part not found");
		const render::LoadedPart lp = render::loadPart(path);
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		QCOMPARE(lp.connectors.size(), 17);
		QPointF vdd, gnd, swclk;
		for (const auto & c : lp.connectors) {
			QVERIFY(c.found);
			if (c.id == "connector0") vdd = c.local;
			if (c.id == "connector5") gnd = c.local;
			if (c.id == "connector6") swclk = c.local;
		}
		QVERIFY(qAbs(gnd.y() - vdd.y() - 45) < 0.01);    // six pins, 0.1 in apart
		QVERIFY(qAbs(swclk.x() - vdd.x() - 54) < 0.01);  // rows 0.6 in apart
	}

	void ownPartsIncludeTheOdroidShow2() {
		qunsetenv("FRITZING_PARTS");
		const QStringList roots = partlib::defaultRoots();
		const QString path = partlib::resolve(roots, partlib::index(roots), "HardkernelOdroidShow2ModuleID");
		QVERIFY2(!path.isEmpty(), "ODROID-SHOW2 part not found");
		const render::LoadedPart lp = render::loadPart(path);
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		QCOMPARE(lp.connectors.size(), 16);
		QVERIFY(qAbs(lp.size.width() - 83 / 25.4 * 90) < 0.1);  // 83 x 48 mm
		for (const auto & c : lp.connectors) QVERIFY2(c.found, qPrintable(c.name));
		// the I/O header P2: GND (connector0) to P3V45 (connector5), 0.1 in apart
		QVERIFY(qAbs(lp.connectors[5].local.y() - lp.connectors[0].local.y() - 45) < 0.01);
		QVERIFY(fzp::findConnector(lp.part, "INT0") != nullptr);
		QVERIFY(fzp::findConnector(lp.part, "BAT+") != nullptr);
	}

	void refIsRelativeToItsRoot() {
		QCOMPARE(partlib::ref(FixtureRoots, Fixtures + "/parts/core/testpart.fzp"), QString("core/testpart.fzp"));
		QCOMPARE(partlib::ref(FixtureRoots, Fixtures + "/fzpz/part.flat.fzp"), QString("part.flat.fzp"));
		QCOMPARE(partlib::ref(FixtureRoots, "/elsewhere/x.fzp"), QString("/elsewhere/x.fzp"));
		// and it resolves back to the same part
		const auto entries = partlib::index(FixtureRoots);
		QVERIFY(partlib::resolve(FixtureRoots, entries, partlib::ref(FixtureRoots, Fixtures + "/fzpz/part.flat.fzp")).endsWith("part.flat.fzp"));
	}

	void searchNeedsEveryWord() {
		const auto entries = partlib::index(FixtureRoots);
		QCOMPARE(partlib::search(entries, "flat part", 10).size(), 1);
		QCOMPARE(partlib::search(entries, "part", 10).size(), 4);
		QCOMPARE(partlib::search(entries, "part zebra", 10).size(), 0);
	}

	void loadPartGivesSizeAndTerminals() {
		const render::LoadedPart lp = render::loadPart(Fixtures + "/parts/core/testpart.fzp");
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		QCOMPARE(lp.size, QSizeF(36, 18));
		QCOMPARE(lp.connectors.size(), 2);
		QVERIFY(lp.connectors[0].found);
		QCOMPARE(lp.connectors[0].local.x(), 4.5);   // pin centre
		QCOMPARE(lp.connectors[0].local.y(), 14.4);
		QCOMPARE(lp.connectors[1].local.x(), 31.5);  // terminal centre
		QVERIFY(qAbs(lp.connectors[1].local.y() - 17.775) < 1e-9);
	}

	void sketchParseRejectsBadInput() {
		QVERIFY(sketch::parse("not json").error.startsWith("invalid JSON"));
		QVERIFY(!sketch::parse(J("{'parts':[{'part':'x'}]}")).error.isEmpty());
		QVERIFY(sketch::parse(J("{'parts':[{'id':'a','part':'x','rotate':45}]}")).error.contains("multiple of 90"));
		QVERIFY(sketch::parse(J("{'parts':[]}")).error.contains("no parts"));
		const auto ok = sketch::parse(J("{'parts':[{'id':'a','part':'x','x':5}],'wires':[{'from':'a.1','to':'a.2','via':[[1,2]]}]}"));
		QVERIFY2(ok.error.isEmpty(), qPrintable(ok.error));
		QCOMPARE(ok.sketch.parts[0].pos, QPointF(5, 0));
		QCOMPARE(ok.sketch.wires[0].via[0], QPointF(1, 2));
		QCOMPARE(ok.sketch.wires[0].color, QString("blue"));
	}

	void sketchParseAcceptsGenericParts() {
		const auto ok = sketch::parse(J("{'parts':[{'id':'g','generic':{'title':'BLE Nano','pins':['TX','GND']}}]}"));
		QVERIFY2(ok.error.isEmpty(), qPrintable(ok.error));
		QCOMPARE(ok.sketch.parts[0].generic.title, QString("BLE Nano"));
		QCOMPARE(ok.sketch.parts[0].generic.pins, QStringList({"TX", "GND"}));
		QVERIFY(sketch::parse(J("{'parts':[{'id':'g','generic':{'title':'x','pins':[]}}]}")).error.contains("pins"));
	}

	void wireEndsCanBePointsOnAPartOrInTheScene() {
		// {"part": id, "at": [x, y]}: a spot on a part (part coordinates, so it
		// follows the part's placement and rotation); [x, y]: a scene point.
		const auto parsed = sketch::parse(J("{'margin':0,'parts':[{'id':'a','part':'core/testpart.fzp','x':100,'rotate':90}],"
		                                    "'wires':[{'from':{'part':'a','at':[4.5,14.4]},'to':[0,0],'color':'#123456'}]}"));
		QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		// (4.5, 14.4) turned about (18, 9) -> (12.6, -4.5), at x 100 -> (112.6, -4.5);
		// the scene starts at the turned body's top (-9) and the point (0, 0) less 3
		const QList<double> l = firstLine(r.svg, "#123456");
		QVERIFY(qAbs(l[0] - (112.6 + 3) * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[1] - (-4.5 + 9) * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[2] - 3.0 * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[3] - 9.0 * 1000 / 90) < 0.01);
	}

	void wireEndPointsAreChecked() {
		QVERIFY(sketch::parse(J("{'parts':[{'id':'a','part':'x'}],'wires':[{'from':{'part':'a'},'to':'a.IN'}]}")).error.contains("at"));
		QVERIFY(sketch::parse(J("{'parts':[{'id':'a','part':'x'}],'wires':[{'from':[1],'to':'a.IN'}]}")).error.contains("[x, y]"));
		const auto parsed = sketch::parse(J("{'parts':[{'id':'a','part':'core/testpart.fzp'}],'wires':[{'from':{'part':'b','at':[0,0]},'to':'a.IN'}]}"));
		QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
		QVERIFY(render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots)).error.contains("no part with id \"b\""));
	}

	void genericPartHasPinsAlongTheBottomAtHeaderPitch() {
		const render::LoadedPart lp = render::genericPart({"BLE Nano relay", {"TX", "GND", "VIN"}});
		QVERIFY(lp.error.isEmpty());
		QCOMPARE(lp.connectors.size(), 3);
		QCOMPARE(lp.connectors[0].name, QString("TX"));
		QCOMPARE(lp.connectors[1].local.x() - lp.connectors[0].local.x(), 9.0);
		QCOMPARE(lp.connectors[0].local.y(), lp.size.height());
		QVERIFY(lp.connectors[0].found);
		QVERIFY(fzp::findConnector(lp.part, "gnd") != nullptr);
	}

	void genericPartRendersWithItsTitleAndWires() {
		const auto parsed = sketch::parse(J("{'margin':0, 'parts':[{'id':'g','generic':{'title':'BLE Nano relay','pins':['TX','GND']}},{'id':'b','part':'core/testpart.fzp','x':100}], 'wires':[{'from':'g.TX','to':'b.IN','color':'#123456'}]}"));
		QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QVERIFY(r.svg.contains("BLE Nano relay"));
		const render::LoadedPart lp = render::genericPart({"BLE Nano relay", {"TX", "GND"}});
		const QList<double> l = firstLine(r.svg, "#123456");
		QVERIFY(qAbs(l[0] - lp.connectors[0].local.x() * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[1] - lp.connectors[0].local.y() * 1000 / 90) < 0.01);
	}

	void wireRunsBetweenTerminals() {
		const auto parsed = sketch::parse(J("{'margin':0, 'parts':[{'id':'a','part':'core/testpart.fzp'},{'id':'b','part':'core/testpart.fzp','x':100}], 'wires':[{'from':'a.IN','to':'b.OUT','color':'#123456'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		const QList<double> l = firstLine(r.svg, "#123456");
		QCOMPARE(l.size(), 4);
		// (4.5, 14.4) -> export units, origin at the bounds. TextUtils::makeLineSVG
		// writes 6 significant digits, so allow 0.01 export units.
		QVERIFY(qAbs(l[0] - 4.5 * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[1] - 14.4 * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[2] - 131.5 * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[3] - 17.775 * 1000 / 90) < 0.01);
	}

	void rotationMovesTerminalsAboutTheCentre() {
		// rotate 90 (clockwise) about (18, 9): IN (4.5, 14.4) -> (12.6, -4.5);
		// the rotated body spans (9, -9)..(27, 27) and b sits inside y -9..27, so
		// the origin moves to (9, -9).
		const auto parsed = sketch::parse(J("{'margin':0, 'parts':[{'id':'a','part':'core/testpart.fzp','rotate':90},{'id':'b','part':'core/testpart.fzp','x':100}], 'wires':[{'from':'a.IN','to':'b.IN','color':'#123456'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		const QList<double> l = firstLine(r.svg, "#123456");
		QVERIFY(qAbs(l[0] - (12.6 - 9) * 1000 / 90) < 0.01);
		QVERIFY(qAbs(l[1] - (-4.5 + 9) * 1000 / 90) < 0.01);
	}

	void labelsSitAboveOrBelow() {
		auto labelY = [](const char * json) {
			const auto parsed = sketch::parse(QByteArray(json).replace('\'', '"'));
			const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
			const auto m = QRegularExpression("<text x='[-\\d.e]+' y='([-\\d.e]+)'").match(r.svg);
			return m.captured(1).toDouble() * 90 / 1000;  // scene units from the bounds' top
		};
		// margin 0: above, the label's top sets the bounds; below, the part's top does.
		QVERIFY(qAbs(labelY("{'margin':0,'parts':[{'id':'a','part':'core/testpart.fzp','label':'A'}]}") - 10) < 0.01);
		QVERIFY(qAbs(labelY("{'margin':0,'parts':[{'id':'a','part':'core/testpart.fzp','label':'A','labelBelow':true}]}") - (18 + 11)) < 0.01);
	}

	void namedColorsUseFritzingsPalette() {
		const auto parsed = sketch::parse(J("{'parts':[{'id':'a','part':'core/testpart.fzp'}], 'wires':[{'from':'a.IN','to':'a.OUT','color':'red'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY(r.svg.contains("stroke='#cc1414'"));  // wire
		QVERIFY(r.svg.contains("stroke='#8c0000'"));  // shadow
	}

	void unknownConnectorListsTheRealOnes() {
		const auto parsed = sketch::parse(J("{'parts':[{'id':'a','part':'core/testpart.fzp'}], 'wires':[{'from':'a.IN','to':'a.VCC'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY(r.error.contains("no connector \"VCC\""));
		QVERIFY(r.error.contains("IN, OUT"));
	}

	void drawingWithoutALayerGroupIsDrawnWhole() {
		// Fritzing's own export drops such parts (the layer split fails); the
		// app shows them, so render the whole drawing as the layer.
		const auto parsed = sketch::parse(J("{'margin':0,'parts':[{'id':'a','part':'core/nolayer.fzp'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QVERIFY(r.svg.contains("#abcdef"));
		QCOMPARE(QColor(render::rasterize(r.svg, r.size, 180, false).pixel(36, 5)), QColor("#abcdef"));
	}

	void singleLayerViewIsDrawnWhole() {
		// The app loads a one-layer view's whole drawing (ItemBase::setUpImage),
		// so elements outside the layer's group show too.
		const auto parsed = sketch::parse(J("{'margin':0,'parts':[{'id':'a','part':'core/outside.fzp'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QCOMPARE(QColor(render::rasterize(r.svg, r.size, 180, false).pixel(36, 5)), QColor("#123456"));
	}

	void partColorRecolorsColorElementsLikeFritzingsLed() {
		auto render = [](const char * color) {
			const auto parsed = sketch::parse(J(QString("{'margin':0,'parts':[{'id':'a','part':'core/led.fzp','color':'%1'}]}").arg(color).toUtf8().constData()));
			return render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		};
		const auto byWord = render("green");  // Fritzing's own green: Green (555nm)
		QVERIFY2(byWord.error.isEmpty(), qPrintable(byWord.error));
		QCOMPARE(QColor(render::rasterize(byWord.svg, byWord.size, 180, false).pixel(36, 5)), QColor("#00b33b"));
		QCOMPARE(QColor(render::rasterize(render("Yellow (595nm)").svg, byWord.size, 180, false).pixel(36, 5)), QColor("#fadf47"));
		QCOMPARE(QColor(render::rasterize(render("#123abc").svg, byWord.size, 180, false).pixel(36, 5)), QColor("#123abc"));
		QVERIFY(render("plaid").error.contains("plaid"));
		// elements without a color_ id keep theirs
		QCOMPARE(QColor(render::rasterize(byWord.svg, byWord.size, 180, false).pixel(36, 30)), QColor("#cccccc"));
	}

	void unknownPartIsAnError() {
		const auto parsed = sketch::parse(J("{'parts':[{'id':'a','part':'core/nothing.fzp'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		QVERIFY(r.error.contains("no part matches"));
	}

	void rasterizeHasTheSketchSizeAtThePpi() {
		const auto parsed = sketch::parse(J("{'margin':0,'parts':[{'id':'a','part':'core/testpart.fzp'}]}"));
		const render::Result r = render::renderSketch(parsed.sketch, FixtureRoots, partlib::index(FixtureRoots));
		const QImage img = render::rasterize(r.svg, r.size, 180, false);
		QCOMPARE(img.size(), QSize(72, 36));  // 0.4 x 0.2 in at 180 ppi
		QCOMPARE(QColor(img.pixel(36, 5)), QColor("#cccccc"));
	}

	void vendorNanoPinPitch() {
		const QString path = vendorPart("core/Arduino Nano3(fix).fzp");
		if (!QFileInfo::exists(path)) QSKIP("vendor parts not fetched");
		const render::LoadedPart lp = render::loadPart(path);
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		const fzp::Connector * d2 = fzp::findConnector(lp.part, "D2");
		const fzp::Connector * d3 = fzp::findConnector(lp.part, "D3");
		QPointF p2, p3;
		for (const auto & c : lp.connectors) {
			if (c.id == d2->id) p2 = c.local;
			if (c.id == d3->id) p3 = c.local;
		}
		QCOMPARE(p3.y() - p2.y(), 9.0);  // 0.1 in at 90 units per inch
	}

	void vendorLedEndsAtItsLeg() {
		const QString path = vendorPart("core/LED-generic-5mm_6852162_005.fzp");
		if (!QFileInfo::exists(path)) QSKIP("vendor parts not fetched");
		const render::LoadedPart lp = render::loadPart(path);
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		for (const auto & c : lp.connectors) {
			QVERIFY(!c.legId.isEmpty());
			QCOMPARE(c.local, c.leg.p2());
			QVERIFY(c.local.y() > lp.size.height());  // the leg reaches past the body
		}
	}

	void fzPlacesPartsWithTheirTransforms() {
		const fzz::Loaded l = fixtureSketch();
		QVERIFY2(l.error.isEmpty(), qPrintable(l.error));
		QCOMPARE(l.sketch.parts.size(), 2);
		const sketch::PartSpec * u1 = partNamed(l.sketch, "U1");
		QVERIFY(u1 != nullptr && u1->transform.has_value());
		QVERIFY(u1->part.endsWith("testpart.fzp"));
		// IN (4.5, 14.4), turned about (18, 9) -> (12.6, -4.5), then placed at (100, 50)
		const QPointF in = u1->transform->map(QPointF(4.5, 14.4));
		QVERIFY(qAbs(in.x() - 112.6) < 1e-9 && qAbs(in.y() - 45.5) < 1e-9);
		QCOMPARE(u1->z, 2.5);
	}

	void fzKeepsLedColorsAndBentLegs() {
		const fzz::Loaded l = fixtureSketch();
		const sketch::PartSpec * led = partNamed(l.sketch, "LED1");
		QVERIFY(led != nullptr);
		QCOMPARE(led->color, QString("Yellow (595nm)"));
		const sketch::Leg leg = led->legs.value("connector0");
		// leg points are relative to the connector at (4.5, 18)
		QCOMPARE(leg.points, QPolygonF({QPointF(4.5, 18), QPointF(4.5, 27), QPointF(13.5, 36)}));
		QCOMPARE(leg.curves.size(), 2);
		QVERIFY(leg.curves[0].isEmpty());
		QCOMPARE(leg.curves[1], QPolygonF({QPointF(4.5, 31.5), QPointF(9, 36)}));
	}

	void fzReadsBreadboardWiresButNotTraces() {
		const fzz::Loaded l = fixtureSketch();
		QCOMPARE(l.sketch.wires.size(), 2);
		const sketch::WireSpec & straight = l.sketch.wires[0];
		QVERIFY(straight.fixed);
		QCOMPARE(straight.p1, QPointF(10, 20));
		QCOMPARE(straight.p2, QPointF(40, 20));
		QCOMPARE(straight.color, QString("#cc1414"));
		QVERIFY(qAbs(straight.width - 2) < 1e-3);  // 22.2 mil
		const sketch::WireSpec & curved = l.sketch.wires[1];
		QCOMPARE(curved.curve, QPolygonF({QPointF(10, 50), QPointF(30, 50)}));
		QVERIFY(qAbs(curved.width - 3) < 1e-3);
	}

	void fzWarnsAboutWhatItLeavesOut() {
		const QString warnings = fixtureSketch().warnings.join('\n');
		QVERIFY2(warnings.contains("no_such_module"), qPrintable(warnings));
		QVERIFY(warnings.contains("X1"));
		QVERIFY(warnings.contains("note"));
		QVERIFY(!warnings.contains("NetLabel"));  // schematic-only: not part of this view
	}

	void fzGeneratesFritzingsPinHeaders() {
		QTemporaryDir dir;
		const fzz::Loaded l = fzz::load(readFile(Fixtures + "/sketch.fz"), FixtureRoots, dir.path());
		QVERIFY2(!l.warnings.join(' ').contains("J1"), qPrintable(l.warnings.join('\n')));
		const sketch::PartSpec * j1 = partNamed(l.sketch, "J1");
		QVERIFY(j1 != nullptr);
		const render::LoadedPart lp = render::loadPart(j1->part);
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		QCOMPARE(lp.size, QSizeF(27, 9));  // three pins, 0.1 in each
		QCOMPARE(lp.connectors.size(), 3);
		QVERIFY(qAbs(lp.connectors[1].local.x() - lp.connectors[0].local.x() - 9) < 1e-6);
		const render::Result r = render::renderSketch(l.sketch, FixtureRoots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
	}

	void generatedPinHeadersHaveASchematic() {
		QTemporaryDir dir;
		const render::LoadedPart lp = render::loadPart(generated::make("generic_female_pin_header_3_100mil", dir.path()), sketch::SchematicView);
		QVERIFY2(lp.error.isEmpty(), qPrintable(lp.error));
		QCOMPARE(lp.connectors.size(), 3);
		QVERIFY(qAbs(lp.size.height() - 27) < 0.01);  // 0.1 in a pin
		QVERIFY(qAbs(lp.connectors[1].local.y() - lp.connectors[0].local.y() - 9) < 0.01);
		QVERIFY(lp.connectors[0].local.x() < 1);       // pins end on the left
	}

	void fzRendersTransformsCurvesAndLegs() {
		fzz::Loaded l = fixtureSketch();
		l.sketch.margin = 0;
		const render::Result r = render::renderSketch(l.sketch, FixtureRoots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QVERIFY(r.svg.contains("stroke='#cc1414'"));
		QVERIFY(r.svg.contains("stroke='#8c0000'"));  // red's shadow, found from the wire's hex color
		QVERIFY(r.svg.contains(QRegularExpression("<path [^>]*stroke='#418dd9'[^>]*d='M[-\\d.e]+,[-\\d.e]+C")));
		QVERIFY(r.svg.contains(QRegularExpression("d='M[-\\d.e]+,[-\\d.e]+ L[-\\d.e]+,[-\\d.e]+ C")));  // the bent leg
		// LED1 (z 2.4) is drawn before U1 (z 2.5)
		QVERIFY(r.svg.indexOf("id='LED1'") < r.svg.indexOf("id='U1'"));
		// from the curved wire's start (0, less its 3-unit allowance) to U1's turned body (x 109..127)
		QCOMPARE(r.size.width(), 130.0);
	}

	void fzzUnpacksTheSketchAndItsBundledParts() {
		QTemporaryDir dir;
		const fzz::Loaded l = fzz::load(readFile(Fixtures + "/bundled.fzz"), FixtureRoots, dir.path());
		QVERIFY2(l.error.isEmpty(), qPrintable(l.error));
		QCOMPARE(l.sketch.parts.size(), 1);
		QVERIFY(l.sketch.parts[0].part.startsWith(dir.path()));
		const render::Result r = render::renderSketch(l.sketch, FixtureRoots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
	}

	void fzzErrors() {
		QTemporaryDir dir;
		QVERIFY(fzz::load("PK\x03\x04garbage", FixtureRoots, dir.path()).error.contains("cannot read"));
		QVERIFY(fzz::load("<module><instances/></module>", FixtureRoots, dir.path()).error.contains("no parts"));
		QVERIFY(fzz::load("<not-closed", FixtureRoots, dir.path()).error.contains("invalid"));
	}

	void fzzIsRecognisedByItsBytes() {
		QCOMPARE(fzz::detect(readFile(Fixtures + "/bundled.fzz")), fzz::Format::Fzz);
		QCOMPARE(fzz::detect(readFile(Fixtures + "/sketch.fz")), fzz::Format::Fz);
		QCOMPARE(fzz::detect("  {\"parts\": []}"), fzz::Format::Json);
	}

	void vendorFritzingExampleSketchRenders() {
		const QString path = QStringLiteral(FR_FRITZING_APP) + "/sketches/core/AnalogInputPot.fzz";
		if (!QFileInfo::exists(path) || !QFileInfo::exists(vendorPart("core"))) QSKIP("vendor parts or fritzing-app sketches missing");
		QTemporaryDir dir;
		const QStringList roots = partlib::defaultRoots();
		const fzz::Loaded l = fzz::load(readFile(path), roots, dir.path());
		QVERIFY2(l.error.isEmpty(), qPrintable(l.error));
		QVERIFY2(!l.warnings.join(' ').contains("not in the parts"), qPrintable(l.warnings.join('\n')));
		QCOMPARE(l.sketch.parts.size(), 5);  // Arduino, potentiometer, LED, two half breadboards
		const render::Result r = render::renderSketch(l.sketch, roots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
	}
	void schematicReadsTheSchematicView() {
		QTemporaryDir dir;
		const fzz::Loaded l = fzz::load(readFile(Fixtures + "/schematic.fz"), FixtureRoots, dir.path(), sketch::SchematicView);
		QVERIFY2(l.error.isEmpty(), qPrintable(l.error));
		QCOMPARE(l.sketch.view, sketch::SchematicView);
		QStringList ids;
		for (const auto & p : l.sketch.parts) ids << p.id;
		QVERIFY2(ids.contains("R1") && ids.contains("Net1") && ids.contains("VCC1"), qPrintable(ids.join(',')));
		QVERIFY(!ids.contains("BB"));  // breadboard-only
		QCOMPARE(partNamed(l.sketch, "R1")->transform->map(QPointF(0, 0)), QPointF(100, 50));
		QCOMPARE(l.sketch.wires.size(), 5);  // the traces, not the breadboard wire
		for (const auto & w : l.sketch.wires) {
			QVERIFY(!w.shadow);
			QVERIFY(qAbs(w.width - 0.875) < 1e-3);  // 9.72 mil
		}
	}

	void schematicDotsMarkJunctions() {
		QTemporaryDir dir;
		const fzz::Loaded l = fzz::load(readFile(Fixtures + "/schematic.fz"), FixtureRoots, dir.path(), sketch::SchematicView);
		// three traces meet at (60, 54.5); two leave R1's pin at (136, 54.5)
		QCOMPARE(l.sketch.dots.size(), 2);
		QVERIFY(l.sketch.dots.contains(QPointF(60, 54.5)));
		QVERIFY(l.sketch.dots.contains(QPointF(136, 54.5)));
	}

	void schematicLabelsShowTitleAndProperties() {
		QTemporaryDir dir;
		const fzz::Loaded l = fzz::load(readFile(Fixtures + "/schematic.fz"), FixtureRoots, dir.path(), sketch::SchematicView);
		const sketch::PartSpec * r1 = partNamed(l.sketch, "R1");
		QCOMPARE(r1->label, QString("R1\n220Ω"));
		QCOMPARE(*r1->labelAt, QPointF(102, 30));
		QCOMPARE(r1->labelSize, 6 * 90 / 72.0);  // points to scene units
		QVERIFY(partNamed(l.sketch, "Net1")->label.isEmpty());  // no titleGeometry: hidden
	}

	void schematicRendersSymbolsTracesAndDots() {
		QTemporaryDir dir;
		const fzz::Loaded l = fzz::load(readFile(Fixtures + "/schematic.fz"), FixtureRoots, dir.path(), sketch::SchematicView);
		const render::Result r = render::renderSketch(l.sketch, FixtureRoots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QVERIFY(r.svg.contains(">SDA<"));    // the net label's text
		QVERIFY(r.svg.contains(">3.3V<"));   // the power label's voltage
		QVERIFY(r.svg.contains(">220Ω<"));   // R1's label, second line
		QCOMPARE(r.svg.count("<circle"), 2);  // junction dots
		QCOMPARE(r.svg.count("stroke='#404040'"), 5);  // one line per trace: no shadows
		QVERIFY(r.svg.contains("#000000"));  // the drawing: R1's box
	}

	void netLabelPointsAtItsWire() {
		QTemporaryDir dir;
		const QString right = generated::netLabel("SDA", false, dir.path());
		const QString left = generated::netLabel("SDA", true, dir.path());
		QVERIFY(!right.isEmpty() && !left.isEmpty() && right != left);
		const render::LoadedPart r = render::loadPart(right, sketch::SchematicView);
		const render::LoadedPart lp = render::loadPart(left, sketch::SchematicView);
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QCOMPARE(r.connectors.size(), 1);
		QVERIFY(qAbs(r.connectors[0].local.x() - r.size.width()) < 0.5);  // tip on the right
		QVERIFY(qAbs(lp.connectors[0].local.x()) < 0.5);                  // tip on the left
		QVERIFY(qAbs(r.size.height() - 9) < 0.01);                        // 0.1 in tall
		QVERIFY(render::loadPart(generated::netLabel("A much longer net name", false, dir.path()), sketch::SchematicView).size.width() > r.size.width());
	}

	void vendorFritzingExampleSchematicRenders() {
		const QString path = QStringLiteral(FR_FRITZING_APP) + "/sketches/core/AnalogInputPot.fzz";
		if (!QFileInfo::exists(path) || !QFileInfo::exists(vendorPart("core"))) QSKIP("vendor parts or fritzing-app sketches missing");
		QTemporaryDir dir;
		const QStringList roots = partlib::defaultRoots();
		const fzz::Loaded l = fzz::load(readFile(path), roots, dir.path(), sketch::SchematicView);
		QVERIFY2(l.error.isEmpty(), qPrintable(l.error));
		QVERIFY2(!l.warnings.join(' ').contains("not in the parts"), qPrintable(l.warnings.join('\n')));
		const render::Result r = render::renderSketch(l.sketch, roots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
	}
	void modernThemeRestylesTheSchematic() {
		QTemporaryDir dir;
		fzz::Loaded l = fzz::load(readFile(Fixtures + "/schematic.fz"), FixtureRoots, dir.path(), sketch::SchematicView);
		const render::Result plain = render::renderSketch(l.sketch, FixtureRoots, {});
		l.sketch.style = "modern";
		const render::Result r = render::renderSketch(l.sketch, FixtureRoots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QVERIFY(!plain.svg.contains("#2563eb"));                  // Fritzing's look is the default
		QCOMPARE(r.svg.count("stroke='#2563eb'"), 5);             // the traces, in one blue
		QVERIFY(!r.svg.contains("stroke='#404040'"));             // not in their own colors
		QVERIFY(r.svg.contains("fill='#e0e7ff'"));                // R1's badge
		QVERIFY(r.svg.contains("font-family=\"Noto Sans"));      // the parts' text in the theme's face
		QVERIFY(r.svg.count("<circle") > plain.svg.count("<circle") + 20);  // the dot grid
		QVERIFY(r.svg.contains(QRegularExpression("<polygon[^>]*fill=\"#2563eb\"")));  // the net label, a filled tag
		QVERIFY(r.svg.contains(QRegularExpression("<text[^>]*fill=\"#ffffff\"[^>]*>SDA<")));  // with white text
		QVERIFY(r.svg.contains(QRegularExpression("<text[^>]*fill=\"#dc2626\"[^>]*>3.3V<")));  // the power label in red
	}

	void themeLeavesTheBreadboardAlone() {
		fzz::Loaded l = fzz::load(readFile(Fixtures + "/sketch.fz"), FixtureRoots, {});
		const QString plain = render::renderSketch(l.sketch, FixtureRoots, {}).svg;
		l.sketch.style = "modern";
		QCOMPARE(render::renderSketch(l.sketch, FixtureRoots, {}).svg, plain);
	}

	void vendorModernThemeFillsComponentBodies() {
		const QString path = QStringLiteral(FR_FRITZING_APP) + "/sketches/core/AnalogInputPot.fzz";
		if (!QFileInfo::exists(path) || !QFileInfo::exists(vendorPart("core"))) QSKIP("vendor parts or fritzing-app sketches missing");
		QTemporaryDir dir;
		const QStringList roots = partlib::defaultRoots();
		fzz::Loaded l = fzz::load(readFile(path), roots, dir.path(), sketch::SchematicView);
		l.sketch.style = "modern";
		const render::Result r = render::renderSketch(l.sketch, roots, {});
		QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
		QVERIFY(r.svg.contains(QRegularExpression("<rect[^>]*fill=\"#f1f5f9\"")));  // the Arduino's body, filled
		QVERIFY(r.svg.contains("rx=\"25\""));                                      // with rounded corners
	}
};

QTEST_MAIN(TestRender)
#include "tst_render.moc"
