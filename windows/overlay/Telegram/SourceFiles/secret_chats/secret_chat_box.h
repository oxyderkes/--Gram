/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class UserData;

namespace Window {
class SessionController;
} // namespace Window

namespace SecretChats {

void ShowList(not_null<Window::SessionController*> controller);
void ShowChat(not_null<Window::SessionController*> controller, int chatId);
void Start(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

} // namespace SecretChats
