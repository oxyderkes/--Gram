/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace SecretChats {

[[nodiscard]] std::shared_ptr<Window::SectionMemento> MakeMemento(
	not_null<Main::Session*> session,
	int chatId);
void ShowSettings(
	not_null<Window::SessionController*> controller,
	int chatId);
void ShowTtlSettings(
	not_null<Window::SessionController*> controller,
	int chatId);

} // namespace SecretChats
