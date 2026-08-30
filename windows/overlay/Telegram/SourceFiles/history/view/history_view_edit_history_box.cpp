/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_edit_history_box.h"

#include "base/unixtime.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace HistoryView {
namespace {

void AppendVersion(
		TextWithEntities &result,
		QString title,
		QString text) {
	if (!result.empty()) {
		result.append(u"\n\n"_q);
	}
	result.append(tr::bold(std::move(title))).append(u"\n"_q);
	result.append(text.isEmpty()
		? tr::lng_agram_edit_history_empty(tr::now)
		: std::move(text));
}

} // namespace

void AgramEditHistoryBox(
		not_null<Ui::GenericBox*> box,
		not_null<HistoryItem*> item) {
	box->setTitle(tr::lng_agram_edit_history_title());
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });

	auto content = TextWithEntities();
	const auto versions = item->history()->owner().agramEditHistory(
		item->fullId());
	for (auto index = size_t(); index != versions.size(); ++index) {
		const auto &version = versions[index];
		auto title = (index == 0)
			? tr::lng_agram_edit_history_original(tr::now)
			: tr::lng_agram_edit_history_version(tr::now)
				+ u" "_q
				+ QString::number(index + 1);
		if (version.changedAt) {
			title += u"  ·  "_q
				+ langDateTime(base::unixtime::parse(version.changedAt));
		}
		AppendVersion(content, std::move(title), version.text);
	}
	AppendVersion(
		content,
		tr::lng_agram_edit_history_current(tr::now),
		item->originalText().text);

	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(std::move(content)),
		st::boxLabel));
	label->setSelectable(true);
}

void ShowAgramEditHistory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	controller->show(Box(AgramEditHistoryBox, item));
}

} // namespace HistoryView
