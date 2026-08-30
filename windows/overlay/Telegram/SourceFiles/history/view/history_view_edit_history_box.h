/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

void AgramEditHistoryBox(
	not_null<Ui::GenericBox*> box,
	not_null<HistoryItem*> item);
void ShowAgramEditHistory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace HistoryView
