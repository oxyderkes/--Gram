/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_html_and_pdf.h"

#include "export/data/export_data_types.h"
#include "export/export_settings.h"
#include "export/output/export_output_result.h"
#include "export/output/export_output_stats.h"

#include <algorithm>
#include <climits>
#include <optional>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMargins>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QPageLayout>
#include <QtGui/QPageSize>
#include <QtGui/QPainter>
#include <QtGui/QPdfWriter>
#include <QtGui/QTextDocument>

namespace Export::Output {
namespace {

[[nodiscard]] std::optional<QByteArray> ReadFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	return file.readAll();
}

[[nodiscard]] QString RemoveUserpics(QString html) {
	const auto opening = u"<div class=\"pull_left userpic_wrap\""_q;
	const auto divStart = u"<div"_q;
	const auto divEnd = u"</div>"_q;
	auto offset = 0;
	while (true) {
		const auto start = html.indexOf(
			opening,
			offset,
			Qt::CaseInsensitive);
		if (start < 0) {
			break;
		}
		auto cursor = html.indexOf('>', start);
		if (cursor < 0) {
			break;
		}
		auto depth = 1;
		while (depth > 0) {
			const auto nextStart = html.indexOf(
				divStart,
				cursor + 1,
				Qt::CaseInsensitive);
			const auto nextEnd = html.indexOf(
				divEnd,
				cursor + 1,
				Qt::CaseInsensitive);
			if (nextEnd < 0) {
				return html;
			}
			if (nextStart >= 0 && nextStart < nextEnd) {
				++depth;
				cursor = html.indexOf('>', nextStart);
				if (cursor < 0) {
					return html;
				}
			} else {
				--depth;
				cursor = nextEnd + divEnd.size() - 1;
			}
		}
		html.remove(start, cursor - start + 1);
		offset = start;
	}
	return html;
}

[[nodiscard]] QString ExtractBody(const QByteArray &content) {
	const auto html = QString::fromUtf8(content);
	const auto bodyTag = html.indexOf(u"<body"_q, 0, Qt::CaseInsensitive);
	const auto bodyStart = (bodyTag >= 0) ? html.indexOf('>', bodyTag) : -1;
	const auto bodyEnd = html.lastIndexOf(u"</body>"_q, -1, Qt::CaseInsensitive);
	const auto body = (bodyStart >= 0 && bodyEnd > bodyStart)
		? html.mid(bodyStart + 1, bodyEnd - bodyStart - 1)
		: html;
	return RemoveUserpics(body);
}

[[nodiscard]] QString PdfStyle(const QByteArray &source) {
	auto result = QString::fromUtf8(source);
	result += uR"CSS(
body {
  margin: 0;
  color: #202124;
  background: #ffffff;
  font-size: 13px;
  line-height: 1.45;
}
.page_wrap { background: #ffffff; }
.page_header {
  position: relative;
  width: auto;
  border-bottom: 1px solid #dfe3e6;
  margin-bottom: 20px;
}
.page_header .content,
.page_body {
  width: 100%;
  max-width: none;
  margin: 0;
}
.page_header .content .text {
  padding: 0 0 7px 0;
  font-size: 20px;
}
.page_header .content .agram_header_details {
  margin-top: 0;
  padding: 0 0 14px 0;
  color: #70777b;
  font-size: 11px;
  font-weight: 400;
}
.page_body { padding-top: 0; }
.history { padding: 0; }
.message {
  margin-left: 0;
  margin-right: 0;
  page-break-inside: avoid;
}
.default {
  padding: 9px 0 9px 12px;
  border-left: 3px solid #d8e8f5;
  border-bottom: 1px solid #edf0f2;
}
.default.joined { margin-top: 0; }
.default .userpic_wrap { display: none; }
.default .body { margin-left: 0; }
.default .date {
  display: block;
  text-align: right;
  font-size: 10px;
}
.service { page-break-inside: avoid; }
.pagination { display: none; }
.agram_pdf_segment + .agram_pdf_segment { page-break-before: always; }
)CSS"_q;
	return result;
}

} // namespace

HtmlAndPdfWriter::HtmlAndPdfWriter()
: _html(CreateWriter(Format::Html)) {
}

Format HtmlAndPdfWriter::format() {
	return Format::HtmlAndPdf;
}

Result HtmlAndPdfWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	_ownedSettings = std::make_unique<Settings>(base::duplicate(settings));
	_settingsCopy = _ownedSettings.get();
	_stats = stats;
	return _html->start(settings, environment, stats);
}

Result HtmlAndPdfWriter::writePersonal(const Data::PersonalInfo &data) {
	return _html->writePersonal(data);
}

Result HtmlAndPdfWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	return _html->writeUserpicsStart(data);
}

Result HtmlAndPdfWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	return _html->writeUserpicsSlice(data);
}

Result HtmlAndPdfWriter::writeUserpicsEnd() {
	return _html->writeUserpicsEnd();
}

Result HtmlAndPdfWriter::writeStoriesStart(const Data::StoriesInfo &data) {
	return _html->writeStoriesStart(data);
}

Result HtmlAndPdfWriter::writeStoriesSlice(const Data::StoriesSlice &data) {
	return _html->writeStoriesSlice(data);
}

Result HtmlAndPdfWriter::writeStoriesEnd() {
	return _html->writeStoriesEnd();
}

Result HtmlAndPdfWriter::writeProfileMusicStart(
		const Data::ProfileMusicInfo &data) {
	return _html->writeProfileMusicStart(data);
}

Result HtmlAndPdfWriter::writeProfileMusicSlice(
		const Data::ProfileMusicSlice &data) {
	return _html->writeProfileMusicSlice(data);
}

Result HtmlAndPdfWriter::writeProfileMusicEnd() {
	return _html->writeProfileMusicEnd();
}

Result HtmlAndPdfWriter::writeContactsList(const Data::ContactsList &data) {
	return _html->writeContactsList(data);
}

Result HtmlAndPdfWriter::writeSessionsList(const Data::SessionsList &data) {
	return _html->writeSessionsList(data);
}

Result HtmlAndPdfWriter::writeOtherData(const Data::File &data) {
	return _html->writeOtherData(data);
}

Result HtmlAndPdfWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return _html->writeDialogsStart(data);
}

Result HtmlAndPdfWriter::writeDialogStart(const Data::DialogInfo &data) {
	_dialogTitle = QString::fromUtf8(data.name + ' ' + data.lastName).trimmed();
	return _html->writeDialogStart(data);
}

Result HtmlAndPdfWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	return _html->writeDialogSlice(data);
}

Result HtmlAndPdfWriter::writeDialogEnd() {
	return _html->writeDialogEnd();
}

Result HtmlAndPdfWriter::writeDialogsEnd() {
	return _html->writeDialogsEnd();
}

Result HtmlAndPdfWriter::finish() {
	if (const auto result = _html->finish(); !result) {
		return result;
	}
	return renderPdf();
}

QString HtmlAndPdfWriter::mainFilePath() {
	return _html->mainFilePath();
}

Result HtmlAndPdfWriter::renderPdf() {
	if (!_settingsCopy || !_settingsCopy->onlySinglePeer()) {
		return Result(
			Result::Type::FatalError,
			_settingsCopy ? _settingsCopy->path : QString());
	}
	const auto firstHtml = _html->mainFilePath();
	const auto firstInfo = QFileInfo(firstHtml);
	const auto folder = firstInfo.absoluteDir();
	auto bodies = QStringList();
	for (auto index = 0;; ++index) {
		const auto filename = u"messages"_q
			+ (index ? QString::number(index + 1) : QString())
			+ u".html"_q;
		const auto path = folder.filePath(filename);
		if (!QFileInfo::exists(path)) {
			break;
		}
		const auto content = ReadFile(path);
		if (!content) {
			return Result(Result::Type::Error, path);
		}
		bodies.push_back(ExtractBody(*content));
	}
	if (bodies.isEmpty()) {
		return Result(Result::Type::Error, firstHtml);
	}

	const auto stylePath = folder.filePath(u"css/style.css"_q);
	const auto style = ReadFile(stylePath);
	if (!style) {
		return Result(Result::Type::Error, stylePath);
	}
	auto body = QString();
	for (const auto &part : bodies) {
		body += u"<div class=\"agram_pdf_segment\">"_q
			+ part
			+ u"</div>"_q;
	}
	const auto html = u"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
		"<style>"_q
		+ PdfStyle(*style)
		+ u"</style></head><body>"_q
		+ body
		+ u"</body></html>"_q;

	auto document = QTextDocument();
	document.setDocumentMargin(0.);
	document.setBaseUrl(QUrl::fromLocalFile(folder.absolutePath() + '/'));
	document.setHtml(html);

	const auto pdfPath = folder.filePath(u"messages.pdf"_q);
	auto output = QFile(pdfPath);
	if (!output.open(QIODevice::WriteOnly)) {
		return Result(Result::Type::Error, pdfPath);
	}
	{
		auto writer = QPdfWriter(&output);
		writer.setCreator(u"alpha-Gram"_q);
		writer.setTitle(_dialogTitle.isEmpty()
			? u"Chat export"_q
			: _dialogTitle);
		writer.setResolution(96);
		writer.setPageSize(QPageSize(QPageSize::A4));
		writer.setPageMargins(QMarginsF(), QPageLayout::Point);

		const auto pageWidth = writer.width();
		const auto pageHeight = writer.height();
		const auto horizontalMargin = 48.;
		const auto topMargin = 42.;
		const auto bottomMargin = 48.;
		const auto footerHeight = 20.;
		const auto contentWidth = pageWidth - 2 * horizontalMargin;
		const auto contentHeight = pageHeight
			- topMargin
			- bottomMargin
			- footerHeight;
		document.setPageSize(QSizeF(contentWidth, contentHeight));
		const auto pages = std::max(document.pageCount(), 1);

		auto painter = QPainter();
		if (!painter.begin(&writer)) {
			return Result(Result::Type::Error, pdfPath);
		}
		for (auto page = 0; page != pages; ++page) {
			if (page && !writer.newPage()) {
				painter.end();
				return Result(Result::Type::Error, pdfPath);
			}
			painter.save();
			painter.translate(
				horizontalMargin,
				topMargin - page * contentHeight);
			auto context = QAbstractTextDocumentLayout::PaintContext();
			context.clip = QRectF(
				0,
				page * contentHeight,
				contentWidth,
				contentHeight);
			document.documentLayout()->draw(&painter, context);
			painter.restore();

			painter.save();
			painter.setPen(QColor(u"#6b7280"_q));
			painter.setFont(QFont(u"Arial"_q, 8));
			painter.drawText(
				QRectF(
					0,
					pageHeight - bottomMargin,
					pageWidth,
					footerHeight),
				Qt::AlignHCenter | Qt::AlignVCenter,
				QString::number(page + 1)
					+ u" / "_q
					+ QString::number(pages));
			painter.restore();
		}
		painter.end();
	}
	output.close();
	const auto size = QFileInfo(pdfPath).size();
	if (size <= 0) {
		return Result(Result::Type::Error, pdfPath);
	}
	if (_stats) {
		_stats->incrementFiles();
		_stats->incrementBytes(int(std::min<int64>(size, INT_MAX)));
	}
	return Result::Success();
}

HtmlAndPdfWriter::~HtmlAndPdfWriter() = default;

} // namespace Export::Output
