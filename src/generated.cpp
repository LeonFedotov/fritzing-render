#include "generated.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

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

QString pinHeaderFzp(const QString & moduleId, const QString & title, const QString & image, int pins, const QString & type) {
	QString connectors;
	for (int i = 0; i < pins; i++) {
		connectors += QString("  <connector id='connector%1' name='Pin %2' type='%3'>\n"
		                      "   <description>pin %2</description>\n"
		                      "   <views><breadboardView><p layer='breadboard' svgId='connector%1pin' terminalId='connector%1terminal'/></breadboardView></views>\n"
		                      "  </connector>\n")
		                  .arg(i)
		                  .arg(i + 1)
		                  .arg(type);
	}
	return QString("<?xml version='1.0' encoding='UTF-8'?>\n"
	               "<module moduleId='%1'>\n"
	               " <title>%2</title>\n"
	               " <properties><property name='family'>generic pin header</property></properties>\n"
	               " <views><breadboardView><layers image='breadboard/%3'><layer layerId='breadboard'/></layers></breadboardView></views>\n"
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
	if (!write(QDir(dir).filePath("svg.breadboard." + image), svg) || !write(fzpPath, pinHeaderFzp(moduleId, title, image, pins, form == "male" ? "male" : "female"))) return {};
	return fzpPath;
}

}  // namespace generated
