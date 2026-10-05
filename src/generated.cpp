#include "generated.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFontMetricsF>
#include <QRegularExpression>

#include <cmath>

#include "utils/textutils.h"

namespace generated {

namespace {

constexpr int MaxPins = 64;

bool write(const QString & path, const QString & text) {
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(text.toUtf8()) >= 0;
}

// PinHeader::makeBreadboardSvg: 0.1 in per pin, each pin drawn from the
// template for its form, female headers on a dark strip.
QString pinHeaderSvg(const QString & form, int pins) {
	const QString file = QStringLiteral(FR_FRITZING_APP) + QString("/resources/templates/generic_%1_pin_header_bread_template.txt").arg(form);
	if (!QFile::exists(file)) return {};
	constexpr double UnitPoints = 1000;  // 0.1 in, in the template's units
	QString svg = QString("<?xml version='1.0' encoding='utf-8'?>\n"
	                      "<svg version='1.2' baseProfile='tiny' xmlns='http://www.w3.org/2000/svg' x='0in' y='0in' width='%1in' height='0.1in' viewBox='0 0 %2 1000'>\n"
	                      "<g id='breadboard'>\n")
	                  .arg(0.1 * pins)
	                  .arg(UnitPoints * pins);
	if (form != "male") svg += QString("<rect fill='#404040' width='%1' height='1000'/>\n").arg(UnitPoints * pins);
	svg += TextUtils::incrementTemplate(file, pins, UnitPoints, TextUtils::standardMultiplyPinFunction, TextUtils::standardCopyPinFunction, nullptr);
	return svg + "</g>\n</svg>\n";
}

// PinHeader::makeSchematicSvg, 0.1 in grid: a line and arrow per pin, numbered.
QString stdIncCopyPinFunction(int pin, const QString & argString, void *) {
	return argString.arg(pin).arg(pin + 1);
}

QString pinHeaderSchematicSvg(const QString & form, int pins) {
	const QString file = QStringLiteral(FR_FRITZING_APP) +
	                     QString("/resources/templates/generic_%1_10thin_pin_header_schem_template.txt").arg(form == "male" ? "male" : "female");
	if (!QFile::exists(file)) return {};
	constexpr double UnitPoints = 7.2;  // 0.1 in, in points
	QString svg = QString("<?xml version='1.0' encoding='utf-8'?>\n"
	                      "<svg version='1.2' baseProfile='tiny' xmlns='http://www.w3.org/2000/svg' x='0in' y='0in' width='0.2in' height='%1in' viewBox='0 0 14.4 %2'>\n"
	                      "<g id='schematic'>\n")
	                  .arg(0.1 * pins)
	                  .arg(UnitPoints * pins);
	svg += TextUtils::incrementTemplate(file, pins, UnitPoints, TextUtils::standardMultiplyPinFunction, stdIncCopyPinFunction, nullptr);
	return svg + "</g>\n</svg>\n";
}

QString pinHeaderFzp(const QString & moduleId, const QString & title, const QString & image, int pins, const QString & type) {
	QString connectors;
	for (int i = 0; i < pins; i++) {
		connectors += QString("  <connector id='connector%1' name='Pin %2' type='%3'>\n"
		                      "   <description>pin %2</description>\n"
		                      "   <views><breadboardView><p layer='breadboard' svgId='connector%1pin' terminalId='connector%1terminal'/></breadboardView>\n"
		                      "    <schematicView><p layer='schematic' svgId='connector%1pin' terminalId='connector%1terminal'/></schematicView></views>\n"
		                      "  </connector>\n")
		                  .arg(i)
		                  .arg(i + 1)
		                  .arg(type);
	}
	return QString("<?xml version='1.0' encoding='UTF-8'?>\n"
	               "<module moduleId='%1'>\n"
	               " <title>%2</title>\n"
	               " <properties><property name='family'>generic pin header</property></properties>\n"
	               " <views><breadboardView><layers image='breadboard/%3'><layer layerId='breadboard'/></layers></breadboardView>\n"
	               "  <schematicView><layers image='schematic/%3'><layer layerId='schematic'/></layers></schematicView></views>\n"
	               " <connectors>\n%4 </connectors>\n"
	               "</module>\n")
	    .arg(moduleId.toHtmlEscaped(), title.toHtmlEscaped(), image.toHtmlEscaped(), connectors);
}

}  // namespace

QString make(const QString & moduleId, const QString & dir) {
	// e.g. generic_female_pin_header_6_100mil, generic_double_row_female_pin_header_6_100mil
	static const QRegularExpression header("^generic_(?:double_row_)?(female|male|rounded_female)_pin_header_(\\d+)_100mil$");
	const auto m = header.match(moduleId);
	if (!m.hasMatch() || dir.isEmpty()) return {};
	const QString form = m.captured(1);
	const int pins = m.captured(2).toInt();
	if (pins < 1 || pins > MaxPins) return {};
	const QString svg = pinHeaderSvg(form, pins);
	if (svg.isEmpty()) return {};
	// the unpacked-.fzpz layout: part.X.fzp beside svg.breadboard.Y.svg
	const QString image = moduleId + ".svg";
	const QString fzpPath = QDir(dir).filePath("part." + moduleId + ".fzp");
	const QString title = QString("Generic %1 pin header, %2 pins").arg(QString(form).replace('_', ' ')).arg(pins);
	const QString schematic = pinHeaderSchematicSvg(form, pins);
	if (schematic.isEmpty() || !write(QDir(dir).filePath("svg.schematic." + image), schematic)) return {};
	if (!write(QDir(dir).filePath("svg.breadboard." + image), svg) || !write(fzpPath, pinHeaderFzp(moduleId, title, image, pins, form == "male" ? "male" : "female"))) return {};
	return fzpPath;
}

QString netLabel(const QString & text, bool left, const QString & dir) {
	if (dir.isEmpty()) return {};
	// NetLabel::makeSvg (not "legacy"), in its units: 1000 per inch
	constexpr double FontSize = 200.0 / 3, Height = 100, Arrow = Height / 2, Stroke = 10.0 / 3, Padding = 50.0 / 3, Baseline = 76;
	QFont font("Noto Sans");
	font.setPixelSize(qRound(FontSize));
	const double textWidth = QFontMetricsF(font).horizontalAdvance(text);
	const double width = std::ceil((textWidth - Padding) / 50) * 50 + Arrow + Padding * 2;
	const double hs = Stroke / 2;
	const QString points = left ? QString("%1,%2 %3,%4 %5,%4 %5,%6 %3,%6").arg(hs).arg(Height / 2).arg(Arrow).arg(hs).arg(width - hs).arg(Height - hs)
	                            : QString("%1,%2 %3,%4 %5,%4 %5,%6 %3,%6").arg(width - hs).arg(Height / 2).arg(width - Arrow).arg(hs).arg(hs).arg(Height - hs);
	const double tip = left ? 0 : width;
	const double pinX = left ? 0 : width - Arrow - 0.1;
	const QString svg = QString("<?xml version='1.0' encoding='UTF-8'?>\n"
	                            "<svg xmlns='http://www.w3.org/2000/svg' width='%1in' height='%2in' viewBox='0 0 %3 %4'>\n"
	                            "<g id='schematic'>\n"
	                            "<rect id='connector0pin' x='%5' y='0' width='%6' height='%4' fill='none' stroke='none' stroke-width='0'/>\n"
	                            "<rect id='connector0terminal' x='%7' y='%8' width='0.1' height='0.1' fill='none' stroke='none' stroke-width='0'/>\n"
	                            "<polygon fill='white' stroke='#000000' stroke-width='%9' points='%10'/>\n"
	                            "<text x='%11' y='%12' fill='#000000' font-family='Noto Sans' font-weight='400' text-anchor='start' font-size='%13'>%14</text>\n"
	                            "</g>\n</svg>\n")
	                        .arg(width / 1000).arg(Height / 1000).arg(width).arg(Height)
	                        .arg(pinX).arg(Arrow).arg(tip).arg(Height / 2)
	                        .arg(Stroke).arg(points)
	                        .arg(Padding + (left ? Arrow : 0)).arg(Baseline).arg(FontSize).arg(text.toHtmlEscaped());
	const QString stem = QString("netlabel_%1_%2").arg(left ? "left" : "right",
	                                                   QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
	const QString fzp = QString("<?xml version='1.0' encoding='UTF-8'?>\n"
	                            "<module moduleId='%1'>\n <title>Net label %2</title>\n"
	                            " <properties><property name='family'>net label</property></properties>\n"
	                            " <views><schematicView><layers image='schematic/%1.svg'><layer layerId='schematic'/></layers></schematicView></views>\n"
	                            " <connectors><connector id='connector0' name='%2' type='male'><description>net %2</description>\n"
	                            "  <views><schematicView><p layer='schematic' svgId='connector0pin' terminalId='connector0terminal'/></schematicView></views>\n"
	                            " </connector></connectors>\n</module>\n")
	                        .arg(stem, text.toHtmlEscaped());
	const QString fzpPath = QDir(dir).filePath("part." + stem + ".fzp");
	if (!write(QDir(dir).filePath("svg.schematic." + stem + ".svg"), svg) || !write(fzpPath, fzp)) return {};
	return fzpPath;
}

bool isSymbol(const QString & moduleId) {
	static const QStringList ids{"GroundModuleID", "JustPowerModuleID", "PowerLabelModuleID", "PowerModuleID"};
	return ids.contains(moduleId);
}

QString symbol(const QString & moduleId, const QString & voltage, const QString & dir, const QStringList & roots) {
	if (dir.isEmpty() || !isSymbol(moduleId)) return {};
	// the symbol's part file, among Fritzing's own
	const QString resources = QStringLiteral(FR_FRITZING_APP) + "/resources/parts";
	QDomDocument fzp;
	for (const QFileInfo & f : QDir(resources + "/core").entryInfoList({"*.fzp"}, QDir::Files)) {
		QFile file(f.absoluteFilePath());
		QDomDocument doc;
		if (file.open(QIODevice::ReadOnly) && doc.setContent(&file) && doc.documentElement().attribute("moduleId") == moduleId) {
			fzp = doc;
			break;
		}
	}
	if (fzp.isNull()) return {};
	QDomElement layers = fzp.documentElement().firstChildElement("views").firstChildElement("schematicView").firstChildElement("layers");
	const QString image = layers.attribute("image");
	QString svgPath;
	QStringList candidates{resources + "/svg/core/" + image};
	for (const QString & root : roots) candidates << root + "/svg/core/" + image << root + "/svg/obsolete/" + image;
	for (const QString & c : candidates) {
		if (QFileInfo::exists(c)) {
			svgPath = c;
			break;
		}
	}
	QFile file(svgPath);
	if (svgPath.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
	QString svg = QString::fromUtf8(file.readAll());
	bool ok = false;
	const double v = voltage.toDouble(&ok);
	if (ok && moduleId != "GroundModuleID") svg = TextUtils::replaceTextElement(svg, "label", QString::number(std::trunc(v * 1000) / 1000) + "V");
	const QString stem = QString(moduleId).remove("ModuleID") + (ok ? "_" + QString::number(v).replace('.', '_') + "V" : QString());
	layers.setAttribute("image", "schematic/" + stem + ".svg");
	const QString fzpPath = QDir(dir).filePath("part." + stem + ".fzp");
	if (!write(QDir(dir).filePath("svg.schematic." + stem + ".svg"), svg) || !write(fzpPath, fzp.toString())) return {};
	return fzpPath;
}

}  // namespace generated
