/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/window_session_controller.h"

#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/boxes/confirm_box.h"

namespace Window {

inline void ConfirmGhostModeAction(
		not_null<SessionController*> controller,
		QString text,
		QString confirmText,
		Fn<void()> confirmed) {
	if (!controller->session().settings().ghostModeEnabled()) {
		confirmed();
		return;
	}
	controller->show(Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = std::move(confirmed),
		.confirmText = std::move(confirmText),
		.title = u"Ghost Mode"_q,
	}));
}

} // namespace Window
