// Fixture parts (tests/fixtures) have known geometry: a 0.4 x 0.2 in body
// (36 x 18 scene units) with IN at (4.5, 14.4) and OUT's terminal at
// (31.5, 17.775). Tests marked "vendor" use the real parts libraries and
// skip when scripts/fetch-vendor.sh hasn't been run.

#include <QFileInfo>
#include <QRegularExpression>
#include <QtTest>

#include "fzp.h"
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
		QCOMPARE(entries.size(), 4);
		QVERIFY(partlib::resolve(FixtureRoots, entries, "core/testpart.fzp").endsWith("testpart.fzp"));
		QVERIFY(partlib::resolve(FixtureRoots, entries, "FlatModuleID").endsWith("part.flat.fzp"));
		QVERIFY(partlib::resolve(FixtureRoots, entries, "test part").endsWith("testpart.fzp"));
		QVERIFY(partlib::resolve(FixtureRoots, entries, "nothing like it").isEmpty());
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
		QCOMPARE(partlib::search(entries, "part", 10).size(), 3);
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
};

QTEST_MAIN(TestRender)
#include "tst_render.moc"
