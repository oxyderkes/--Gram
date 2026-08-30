/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "secret_chats/secret_chat_box.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "secret_chats/secret_chat_manager.h"
#include "secret_chats/secret_chat_section.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtWidgets/QFileDialog>

namespace SecretChats {
namespace {

[[nodiscard]] QString StateText(State state) {
	switch (state) {
	case State::Requested: return u"ожидает принятия"_q;
	case State::Waiting: return u"ожидает ответа"_q;
	case State::Active: return u"активен"_q;
	case State::Discarded: return u"закрыт"_q;
	case State::Error: return u"ошибка"_q;
	}
	Unexpected("State in SecretChats::StateText.");
}

[[nodiscard]] QString UserName(
		not_null<Main::Session*> session,
		uint64 userId) {
	const auto user = session->data().userLoaded(UserId(userId));
	return user ? user->name() : u"Пользователь %1"_q.arg(userId);
}

[[nodiscard]] QString TimeText(int date) {
	return date
		? QDateTime::fromSecsSinceEpoch(date).toString(u"HH:mm"_q)
		: QString();
}

[[nodiscard]] QString TtlText(int seconds) {
	return !seconds
		? u"Выключен"_q
		: (seconds < 60)
		? u"%1 сек."_q.arg(seconds)
		: (seconds < 3600)
		? u"%1 мин."_q.arg(seconds / 60)
		: (seconds < 86400)
		? u"%1 ч."_q.arg(seconds / 3600)
		: u"%1 дн."_q.arg(seconds / 86400);
}

void TtlBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int chatId,
		int current) {
	box->setTitle(rpl::single(u"Таймер самоуничтожения"_q));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		u"Самоуничтожающиеся сообщения удаляются без локального архивирования."_q,
		st::membersAbout));
	for (const auto seconds : { 0, 5, 10, 30, 60, 3600, 86400, 604800 }) {
		auto label = TtlText(seconds);
		if (seconds == current) {
			label += u" · выбрано"_q;
		}
		const auto button = box->addRow(object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(std::move(label)),
			st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			controller->session().secretChats().setTtl(chatId, seconds);
			box->closeBox();
		});
	}
	box->addButton(rpl::single(u"Отмена"_q), [=] { box->closeBox(); });
}

[[nodiscard]] QString MessageText(const Message &message) {
	if (message.service) {
		return message.text.isEmpty()
			? u"Служебное сообщение"_q
			: message.text;
	}
	auto result = message.text;
	if (!message.mediaName.isEmpty()) {
		if (!result.isEmpty()) {
			result += u'\n';
		}
		result += u"Вложение: %1"_q.arg(message.mediaName);
		if (!message.archivePath.isEmpty()) {
			result += u" (сохранено в защищённом архиве)"_q;
		}
	}
	return result.isEmpty() ? u"Сообщение без текста"_q : result;
}

void FillMessages(
		not_null<Ui::VerticalLayout*> content,
		not_null<Manager*> manager,
		int chatId) {
	content->clear();
	const auto list = manager->messages(chatId);
	if (list.empty()) {
		content->add(object_ptr<Ui::FlatLabel>(
			content,
			u"Сообщений пока нет."_q,
			st::membersAbout));
		return;
	}
	for (const auto &message : list) {
		auto row = object_ptr<Ui::VerticalLayout>(content);
		const auto raw = row.data();
		const auto body = raw->add(object_ptr<Ui::FlatLabel>(
			raw,
			(message.outgoing ? u"Вы: "_q : u"Собеседник: "_q)
				+ MessageText(message),
			st::boxLabel));
		body->setSelectable(true);
		body->setOpacity(message.deleted ? 0.4 : 1.);

		auto metadata = TimeText(message.date);
		if (message.deleted) {
			metadata += metadata.isEmpty()
				? u"{DELETED}"_q
				: u"  {DELETED}"_q;
		}
		if (message.ttl > 0) {
			metadata += u"  TTL %1 сек."_q.arg(message.ttl);
		}
		const auto meta = raw->add(object_ptr<Ui::FlatLabel>(
			raw,
			metadata,
			st::membersAbout));
		meta->setTextColorOverride(st::windowSubTextFg->c);
		if (message.deleted) {
			meta->setOpacity(0.4);
		}
		content->add(
			std::move(row),
			style::margins(16, 8, 16, 8),
			message.outgoing ? style::al_right : style::al_left);
	}
}

void SecretChatBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int chatId) {
	const auto session = &controller->session();
	const auto manager = &session->secretChats();
	const auto chats = manager->chats();
	const auto i = ranges::find(chats, chatId, &ChatInfo::id);
	if (i == end(chats)) {
		box->setTitle(rpl::single(u"Секретный чат"_q));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			u"Локальное состояние этого чата не найдено."_q,
			st::boxLabel));
		box->addButton(rpl::single(u"Закрыть"_q), [=] { box->closeBox(); });
		return;
	}
	const auto info = *i;
	const auto title = !info.userName.isEmpty()
		? info.userName
		: UserName(session, info.userId);
	box->setTitle(rpl::single(title));
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(640);

	auto details = u"Состояние: %1"_q.arg(StateText(info.state));
	if (info.keyFingerprint) {
		details += u"\nОтпечаток ключа: %1"_q.arg(
			QString::number(info.keyFingerprint, 16)
				.rightJustified(16, u'0')
				.toUpper());
	}
	details += u"\nЭтот чат и его архив привязаны к данному ПК."_q;
	if (!info.error.isEmpty()) {
		details += u"\nОшибка: %1"_q.arg(info.error);
	}
	const auto about = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		details,
		st::membersAbout));
	about->setSelectable(true);

	const auto messages = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	FillMessages(messages, manager, chatId);
	manager->changes(
	) | rpl::on_next([=] {
		FillMessages(messages, manager, chatId);
		const auto updated = manager->messages(chatId);
		auto maxDate = 0;
		for (const auto &message : updated) {
			if (!message.outgoing) {
				maxDate = std::max(maxDate, message.date);
			}
		}
		if (maxDate) {
			manager->markRead(chatId, maxDate);
		}
	}, messages->lifetime());

	if (info.state == State::Requested) {
		box->addButton(rpl::single(u"Принять"_q), [=] {
			manager->accept(chatId);
		});
		return;
	} else if (info.state != State::Active) {
		box->addButton(rpl::single(u"Закрыть"_q), [=] { box->closeBox(); });
		if (info.state != State::Discarded) {
			box->addLeftButton(rpl::single(u"Завершить чат"_q), [=] {
				manager->discard(chatId, false);
				box->closeBox();
			});
		}
		return;
	}

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(u"Сообщение"_q),
		TextWithTags()),
		style::margins(16, 8, 16, 8));
	field->setMaxHeight(120);
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto send = [=] {
		const auto text = field->getLastText().trimmed();
		if (text.isEmpty()) {
			return;
		}
		manager->sendText(chatId, text);
		field->setText(QString());
	};
	field->submits(
	) | rpl::on_next(send, field->lifetime());
	box->addButton(rpl::single(u"Отправить"_q), send);
	box->addButton(rpl::single(u"Закрыть"_q), [=] { box->closeBox(); });
	box->addLeftButton(rpl::single(u"Таймер"_q), [=] {
		controller->show(Box(TtlBox, controller, chatId, info.ttl));
	});
	box->addLeftButton(rpl::single(u"Файл"_q), [=] {
		const auto path = QFileDialog::getOpenFileName(
			box,
			u"Отправить зашифрованный файл"_q);
		if (!path.isEmpty()) {
			manager->sendFile(chatId, path, field->getLastText().trimmed());
			field->setText(QString());
		}
	});
	box->addLeftButton(rpl::single(u"Завершить чат"_q), [=] {
		manager->discard(chatId, false);
		box->closeBox();
	});
	const auto list = manager->messages(chatId);
	auto maxDate = 0;
	for (const auto &message : list) {
		if (!message.outgoing) {
			maxDate = std::max(maxDate, message.date);
		}
	}
	if (maxDate) {
		manager->markRead(chatId, maxDate);
	}
}

void FillList(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	content->clear();
	const auto session = &controller->session();
	const auto manager = &session->secretChats();
	const auto chats = manager->chats();
	if (chats.empty()) {
		content->add(object_ptr<Ui::FlatLabel>(
			content,
			u"Секретных чатов на этом ПК пока нет. Создать чат можно из меню профиля пользователя."_q,
			st::membersAbout));
		return;
	}
	for (const auto &chat : chats) {
		auto label = u"%1 — %2"_q.arg(
			UserName(session, chat.userId),
			StateText(chat.state));
		if (!chat.error.isEmpty()) {
			label += u" · %1"_q.arg(chat.error);
		}
		const auto button = content->add(object_ptr<Ui::SettingsButton>(
			content,
			rpl::single(std::move(label)),
			st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			ShowChat(controller, chat.id);
		});
	}
}

void SecretChatsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto manager = &controller->session().secretChats();
	box->setTitle(rpl::single(u"Секретные чаты"_q));
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(640);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		u"Экспериментальная реализация. Проверяйте отпечаток ключа с собеседником. Самоуничтожающиеся сообщения и медиа одного просмотра не сохраняются."_q,
		st::membersAbout));

	const auto saveDeleted = box->addRow(
		object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(u"Сохранять удалённые сообщения"_q),
			st::settingsButtonNoIcon));
	saveDeleted->toggleOn(rpl::single(manager->saveDeletedMessages()));
	saveDeleted->toggledValue(
	) | rpl::filter([=](bool value) {
		return value != manager->saveDeletedMessages();
	}) | rpl::on_next([=](bool value) {
		manager->setSaveDeletedMessages(value);
	}, saveDeleted->lifetime());

	const auto content = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	FillList(content, controller);
	manager->changes(
	) | rpl::on_next([=] {
		FillList(content, controller);
	}, content->lifetime());
	box->addButton(rpl::single(u"Закрыть"_q), [=] { box->closeBox(); });
}

} // namespace

void ShowList(not_null<Window::SessionController*> controller) {
	controller->show(Box(SecretChatsBox, controller));
}

void ShowChat(
		not_null<Window::SessionController*> controller,
		int chatId) {
	controller->showSection(
		MakeMemento(&controller->session(), chatId),
		Window::SectionShow(Window::SectionShow::Way::ClearStack));
}

void Start(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	controller->session().secretChats().create(user, [=](int chatId) {
		ShowChat(controller, chatId);
	});
}

} // namespace SecretChats
