/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "secret_chats/secret_chat_section.h"

#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "history/view/history_view_chat_section.h"
#include "main/main_session.h"
#include "secret_chats/secret_chat_entry.h"
#include "secret_chats/secret_chat_manager.h"
#include "ui/abstract_button.h"
#include "ui/controls/send_button.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QFileDialog>

namespace SecretChats {
namespace {

constexpr auto kComposeHeight = 58;
constexpr auto kPanelHeight = 74;
constexpr auto kBubblePadding = 12;
constexpr auto kBubbleSpacing = 8;
constexpr auto kSidePadding = 18;

[[nodiscard]] QString StateText(State state) {
	switch (state) {
	case State::Requested: return u"ожидает принятия"_q;
	case State::Waiting: return u"ожидание ответа"_q;
	case State::Active: return u"секретный чат"_q;
	case State::Discarded: return u"чат завершён"_q;
	case State::Error: return u"ошибка"_q;
	}
	Unexpected("State in SecretChats::StateText.");
}

[[nodiscard]] QString TtlText(int seconds) {
	return !seconds
		? u"выключен"_q
		: (seconds < 60)
		? u"%1 сек."_q.arg(seconds)
		: (seconds < 3600)
		? u"%1 мин."_q.arg(seconds / 60)
		: (seconds < 86400)
		? u"%1 ч."_q.arg(seconds / 3600)
		: u"%1 дн."_q.arg(seconds / 86400);
}

[[nodiscard]] QString MessageText(const Message &message) {
	auto result = message.text;
	if (result.isEmpty() && message.mediaName.isEmpty()) {
		result = message.service
			? u"Служебное сообщение"_q
			: u"Сообщение без текста"_q;
	}
	return result;
}

[[nodiscard]] QString MetadataText(const Message &message) {
	auto result = message.date
		? QDateTime::fromSecsSinceEpoch(message.date).toString(u"HH:mm"_q)
		: QString();
	if (message.deleted) {
		result += result.isEmpty() ? u"{DELETED}"_q : u"  {DELETED}"_q;
	}
	if (message.ttl > 0) {
		result += u"  TTL %1"_q.arg(TtlText(message.ttl));
	}
	if (!message.mediaName.isEmpty() && !message.archivePath.isEmpty()) {
		result += u"  ✓ на ПК"_q;
	}
	return result;
}

[[nodiscard]] std::optional<ChatInfo> LookupInfo(
		not_null<Manager*> manager,
		int chatId) {
	const auto chats = manager->chats();
	const auto i = ranges::find(chats, chatId, &ChatInfo::id);
	return (i == end(chats))
		? std::nullopt
		: std::make_optional(*i);
}

class HeaderButton final : public Ui::AbstractButton {
public:
	HeaderButton(QWidget *parent) : Ui::AbstractButton(parent) {
		setCursor(style::cur_pointer);
	}

	void setText(const QString &text) {
		_text.setText(st::semiboldTextStyle, text);
		update();
	}

	void setSubtext(const QString &text) {
		_subtext.setText(st::defaultTextStyle, text);
		update();
	}

	void setWidget(not_null<Ui::RpWidget*> widget) {
		_widget = widget;
		_widget->setParent(this);
		_widget->show();
		updateGeometry();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		updateGeometry();
		return st::topBarHeight;
	}

	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		p.fillRect(e->rect(), st::topBarBg);
		const auto textX = _widget ? 64 : 16;
		const auto available = std::max(width() - textX - 8, 0);
		const auto textHeight = st::semiboldFont->height;
		const auto subtextHeight = st::dialogsTextFont->height;
		const auto total = _subtext.isEmpty()
			? textHeight
			: textHeight + subtextHeight;
		const auto y = (height() - total) / 2 - st::lineWidth;
		p.setPen(st::dialogsNameFg);
		_text.draw(p, {
			.position = { textX, y },
			.outerWidth = width(),
			.availableWidth = available,
			.elisionLines = 1,
		});
		if (!_subtext.isEmpty()) {
			p.setPen(st::historyStatusFg);
			_subtext.draw(p, {
				.position = { textX, y + textHeight + 2 * st::lineWidth },
				.outerWidth = width(),
				.availableWidth = available,
				.elisionLines = 1,
			});
		}
	}

	void onStateChanged(State was, StateChangeSource source) override {
		if (isDown() && !(was & StateFlag::Down)) {
			clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		}
	}

private:
	void updateGeometry() {
		if (_widget) {
			_widget->moveToLeft(8, (st::topBarHeight - _widget->height()) / 2);
		}
	}

	Ui::Text::String _text;
	Ui::Text::String _subtext;
	Ui::RpWidget *_widget = nullptr;
};

class MessagesWidget final : public Ui::RpWidget {
public:
	MessagesWidget(
		QWidget *parent,
		not_null<Manager*> manager,
		int chatId)
	: RpWidget(parent)
	, _manager(manager)
	, _chatId(chatId) {
		setAttribute(Qt::WA_OpaquePaintEvent, false);
		refresh();
	}

	void refresh() {
		_messages = _manager->messages(_chatId);
		resizeToWidth(width());
		update();
	}

	void setQuery(QString query) {
		query = query.trimmed();
		if (_query != query) {
			_query = std::move(query);
			resizeToWidth(width());
			update();
		}
	}

	void setMinimumContentHeight(int height) {
		if (_minimumHeight != height) {
			_minimumHeight = height;
			resizeToWidth(width());
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		prepareLayout(newWidth);
		return std::max(_minimumHeight, _contentHeight);
	}

	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		p.setClipRect(e->rect());
		p.setRenderHint(QPainter::Antialiasing);
		for (const auto &bubble : _bubbles) {
			p.save();
			if (bubble.deleted) {
				p.setOpacity(0.4);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(bubble.outgoing ? st::msgOutBg : st::msgInBg);
			p.drawRoundedRect(bubble.outer, 10, 10);
			p.setOpacity(bubble.deleted ? 0.4 : 1.);
			if (!bubble.preview.isNull()) {
				p.save();
				auto clip = QPainterPath();
				clip.addRoundedRect(bubble.media, 8, 8);
				p.setClipPath(clip, Qt::IntersectClip);
				p.drawImage(bubble.media, bubble.preview);
				p.restore();
			} else if (!bubble.mediaName.isEmpty()) {
				const auto icon = QRect(
					bubble.media.x(),
					bubble.media.y() + 5,
					44,
					44);
				p.setPen(Qt::NoPen);
				p.setBrush(bubble.outgoing
					? st::msgFileOutBg
					: st::msgFileInBg);
				p.drawEllipse(icon);
				const auto &glyph = bubble.outgoing
					? st::historyFileOutDocument
					: st::historyFileInDocument;
				glyph.paintInCenter(p, icon);
				p.setFont(st::semiboldFont);
				p.setPen(bubble.outgoing
					? st::historyTextOutFg
					: st::historyTextInFg);
				p.drawText(
					bubble.media.adjusted(56, 4, 0, -27),
					Qt::AlignLeft | Qt::AlignVCenter,
					st::semiboldFont->elided(
						bubble.mediaName,
						bubble.media.width() - 56));
				p.setFont(st::dialogsDateFont);
				p.setPen(bubble.outgoing ? st::msgOutDateFg : st::msgInDateFg);
				p.drawText(
					bubble.media.adjusted(56, 27, 0, -4),
					Qt::AlignLeft | Qt::AlignVCenter,
					bubble.archivePath.isEmpty()
						? u"Загрузка…"_q
						: u"Открыть файл"_q);
			}
			if (!bubble.text.isEmpty()) {
				p.setFont(st::normalFont);
				p.setPen(bubble.outgoing
					? st::historyTextOutFg
					: st::historyTextInFg);
				p.drawText(
					bubble.body,
					Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
					bubble.text);
			}
			p.setFont(st::dialogsDateFont);
			p.setPen(bubble.outgoing
				? st::msgOutDateFg
				: st::msgInDateFg);
			p.drawText(
				bubble.meta,
				Qt::AlignRight | Qt::AlignVCenter,
				bubble.metadata);
			p.restore();
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		for (const auto &bubble : _bubbles) {
			if (!bubble.archivePath.isEmpty() && bubble.media.contains(e->pos())) {
				const auto bytes = _manager->mediaBytes(_chatId, bubble.randomId);
				if (bytes.isEmpty() || !_temporaryDir.isValid()) {
					break;
				}
				auto name = QFileInfo(bubble.mediaName).fileName();
				if (name.isEmpty()) {
					name = u"media.bin"_q;
				}
				const auto path = _temporaryDir.filePath(
					u"%1_%2"_q.arg(
						QString::number(bubble.randomId, 16),
						name));
				auto file = QFile(path);
				if ((file.exists() || (file.open(QIODevice::WriteOnly)
					&& file.write(bytes) == bytes.size()))) {
					file.close();
					QDesktopServices::openUrl(QUrl::fromLocalFile(path));
				}
				e->accept();
				return;
			}
		}
		RpWidget::mousePressEvent(e);
	}

private:
	struct Bubble final {
		QRect outer;
		QRect body;
		QRect media;
		QRect meta;
		QString text;
		QString metadata;
		QString mediaName;
		QString archivePath;
		QImage preview;
		uint64 randomId = 0;
		bool outgoing = false;
		bool deleted = false;
	};

	[[nodiscard]] static bool IsImageName(const QString &name) {
		const auto suffix = QFileInfo(name).suffix().toLower();
		return suffix == u"jpg"_q
			|| suffix == u"jpeg"_q
			|| suffix == u"png"_q
			|| suffix == u"webp"_q
			|| suffix == u"gif"_q
			|| suffix == u"bmp"_q;
	}

	void prepareLayout(int newWidth) {
		_bubbles.clear();
		const auto available = std::max(newWidth - 2 * kSidePadding, 160);
		const auto maxBubbleWidth = std::clamp(
			int(newWidth * 0.72),
			180,
			560);
		const auto maxTextWidth = std::max(
			maxBubbleWidth - 2 * kBubblePadding,
			100);
		const auto bodyMetrics = QFontMetrics(st::normalFont->f);
		const auto metaHeight = st::dialogsDateFont->height;
		auto y = 22;
		for (const auto &message : _messages) {
			if (!_query.isEmpty()
				&& !message.text.contains(_query, Qt::CaseInsensitive)
				&& !message.mediaName.contains(_query, Qt::CaseInsensitive)) {
				continue;
			}
			const auto text = MessageText(message);
			const auto metadata = MetadataText(message);
			auto preview = QImage();
			if (!message.archivePath.isEmpty()
				&& IsImageName(message.mediaName)) {
				preview.loadFromData(_manager->mediaBytes(
					_chatId,
					message.randomId));
			}
			auto mediaSize = QSize();
			if (!preview.isNull()) {
				mediaSize = preview.size();
				mediaSize.scale(
					std::min(maxTextWidth, 440),
					360,
					Qt::KeepAspectRatio);
			} else if (!message.mediaName.isEmpty()) {
				mediaSize = QSize(std::min(maxTextWidth, 340), 54);
			}
			const auto natural = std::max(
				text.isEmpty()
					? 0
					: bodyMetrics.horizontalAdvance(text.section(u'\n', 0, 0)),
				st::dialogsDateFont->width(metadata));
			const auto minimumTextWidth = std::min(
				message.mediaName.isEmpty() ? 120 : 220,
				maxTextWidth);
			const auto textWidth = std::clamp(
				std::max(natural, mediaSize.width()),
				minimumTextWidth,
				maxTextWidth);
			const auto bodyHeight = text.isEmpty()
				? 0
				: std::max(
					bodyMetrics.boundingRect(
						QRect(0, 0, textWidth, 100000),
						Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
						text).height(),
					st::normalFont->height);
			const auto bubbleWidth = std::min(
				available,
				std::max({
					textWidth,
					mediaSize.width(),
					st::dialogsDateFont->width(metadata) })
					+ 2 * kBubblePadding);
			const auto mediaHeight = mediaSize.height();
			const auto mediaSkip = (mediaHeight && bodyHeight) ? 7 : 0;
			const auto bubbleHeight = kBubblePadding
				+ mediaHeight
				+ mediaSkip
				+ bodyHeight
				+ 3
				+ metaHeight
				+ kBubblePadding;
			const auto x = message.outgoing
				? (newWidth - kSidePadding - bubbleWidth)
				: kSidePadding;
			const auto outer = QRect(x, y, bubbleWidth, bubbleHeight);
			_bubbles.push_back({
				.outer = outer,
				.body = QRect(
					x + kBubblePadding,
					y + kBubblePadding + mediaHeight + mediaSkip,
					bubbleWidth - 2 * kBubblePadding,
					bodyHeight),
				.media = QRect(
					x + kBubblePadding,
					y + kBubblePadding,
					mediaSize.width(),
					mediaHeight),
				.meta = QRect(
					x + kBubblePadding,
					y + kBubblePadding + mediaHeight + mediaSkip + bodyHeight + 3,
					bubbleWidth - 2 * kBubblePadding,
					metaHeight),
				.text = text,
				.metadata = metadata,
				.mediaName = message.mediaName,
				.archivePath = message.archivePath,
				.preview = std::move(preview),
				.randomId = message.randomId,
				.outgoing = message.outgoing,
				.deleted = message.deleted,
			});
			y += bubbleHeight + kBubbleSpacing;
		}
		if (_messages.empty()) {
			y += 40;
		}
		_contentHeight = y + 16;
	}

	const not_null<Manager*> _manager;
	const int _chatId;
	std::vector<Message> _messages;
	std::vector<Bubble> _bubbles;
	QString _query;
	QTemporaryDir _temporaryDir;
	int _minimumHeight = 0;
	int _contentHeight = 0;
};

class Memento final : public Window::SectionMemento {
public:
	Memento(not_null<Main::Session*> session, int chatId)
	: _session(session)
	, _chatId(chatId) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	[[nodiscard]] not_null<Main::Session*> session() const {
		return _session;
	}
	[[nodiscard]] int chatId() const {
		return _chatId;
	}

private:
	const not_null<Main::Session*> _session;
	const int _chatId;
};

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

void SettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int chatId) {
	const auto manager = &controller->session().secretChats();
	const auto info = LookupInfo(manager, chatId);
	box->setTitle(rpl::single(u"Параметры секретного чата"_q));
	if (!info) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			u"Локальное состояние чата не найдено."_q,
			st::boxLabel));
		box->addButton(rpl::single(u"Закрыть"_q), [=] { box->closeBox(); });
		return;
	}
	auto details = u"Состояние: %1\nЭтот секретный чат и его архив привязаны к данному ПК."_q.arg(
		StateText(info->state));
	if (info->keyFingerprint) {
		details += u"\nОтпечаток ключа: %1"_q.arg(
			QString::number(info->keyFingerprint, 16)
				.rightJustified(16, u'0')
				.toUpper());
	}
	if (!info->error.isEmpty()) {
		details += u"\nОшибка: %1"_q.arg(info->error);
	}
	const auto about = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		details,
		st::membersAbout));
	about->setSelectable(true);

	const auto ttl = box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(u"Таймер: %1"_q.arg(TtlText(info->ttl))),
		st::settingsButtonNoIcon));
	ttl->setClickedCallback([=] {
		controller->show(Box(TtlBox, controller, chatId, info->ttl));
	});

	const auto saveDeleted = box->addRow(object_ptr<Ui::SettingsButton>(
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

	if (info->state != State::Discarded) {
		box->addLeftButton(rpl::single(u"Завершить чат"_q), [=] {
			manager->discard(chatId, false);
			box->closeBox();
		});
	}
	box->addButton(rpl::single(u"Готово"_q), [=] { box->closeBox(); });
}

class Widget final : public Window::SectionWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		int chatId)
	: Window::SectionWidget(parent, controller, peerValue(controller, chatId))
	, _chatId(chatId)
	, _manager(&controller->session().secretChats())
	, _topBar(this)
	, _search(this, st::topBarSearch)
	, _call(this, st::topBarCall)
	, _info(this, st::topBarInfo)
	, _menu(this, st::topBarMenuToggle)
	, _searchField(
		this,
		st::defaultMultiSelectSearchField,
		rpl::single(u"Поиск в чате"_q))
	, _searchClose(this, st::topBarCloseChoose)
	, _shadow(this)
	, _scroll(this, st::historyScroll)
	, _field(
		this,
		st::historyComposeField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(u"Сообщение"_q),
		TextWithTags())
	, _attach(this, st::historyAttach)
	, _send(this, st::historySend)
	, _status(this, rpl::single(QString()), st::membersAbout) {
		_topBar->setClickedCallback([=] {
			showProfile();
		});
		_search->setClickedCallback([=] { setSearchShown(true); });
		_searchClose->setClickedCallback([=] { setSearchShown(false); });
		_info->setClickedCallback([=] { showProfile(); });
		_call->setClickedCallback([=] { startCall(false); });
		_menu->setClickedCallback([=] { showMenu(); });
		_searchField->changes(
		) | rpl::on_next([=] {
			if (_messages) {
				_messages->setQuery(_searchField->getLastText());
			}
		}, _searchField->lifetime());
		_searchField->submits(
		) | rpl::on_next([=] {
			if (_messages) {
				_messages->setQuery(_searchField->getLastText());
			}
		}, _searchField->lifetime());
		_searchField->hide();
		_searchClose->hide();
		_field->setMaxHeight(st::historyComposeFieldMaxHeight);
		const auto send = [=] {
			const auto text = _field->getLastText().trimmed();
			if (!text.isEmpty()) {
				_manager->sendText(_chatId, text);
				_field->setText(QString());
			}
		};
		_field->submits(
		) | rpl::on_next(send, _field->lifetime());
		_send->setClickedCallback(send);
		_attach->setClickedCallback([=] {
			const auto path = QFileDialog::getOpenFileName(
				this,
				u"Отправить зашифрованный файл"_q);
			if (!path.isEmpty()) {
				_manager->sendFile(
					_chatId,
					path,
					_field->getLastText().trimmed());
				_field->setText(QString());
			}
		});
		_messages = _scroll->setOwnedWidget(object_ptr<MessagesWidget>(
			this,
			_manager,
			_chatId));
		_scroll->show();
		_topBar->show();
		_search->show();
		_call->show();
		_info->show();
		_menu->show();
		_shadow->show();

		_manager->changes(
		) | rpl::on_next([=] {
			refresh();
		}, lifetime());
		using UpdateFlag = Data::PeerUpdate::Flag;
		session().changes().peerUpdates(
			UpdateFlag::Name | UpdateFlag::OnlineStatus
		) | rpl::on_next([=](const Data::PeerUpdate &update) {
			if (const auto user = currentUser(); update.peer == user) {
				refresh();
			}
		}, lifetime());
		refresh();
	}

	Dialogs::RowDescriptor activeChat() const override {
		if (const auto entry = _manager->entry(_chatId)) {
			return { entry, FullMsgId() };
		}
		return {};
	}

	bool floatPlayerHandleWheelEvent(QEvent *e) override {
		return _scroll->viewportEvent(e);
	}

	QRect floatPlayerAvailableRect() override {
		return mapToGlobal(_scroll->geometry());
	}

	bool hasTopBarShadow() const override {
		return true;
	}

	bool showInternal(
			not_null<Window::SectionMemento*> memento,
			const Window::SectionShow &params) override {
		const auto secret = dynamic_cast<Memento*>(memento.get());
		return secret
			&& secret->session() == &session()
			&& secret->chatId() == _chatId;
	}

	bool sameTypeAs(not_null<Window::SectionMemento*> memento) override {
		return dynamic_cast<Memento*>(memento.get()) != nullptr;
	}

	std::shared_ptr<Window::SectionMemento> createMemento() override {
		return std::make_shared<Memento>(&session(), _chatId);
	}

	void setInternalState(const QRect &geometry) {
		setGeometry(geometry);
		Ui::SendPendingMoveResizeEvents(this);
	}

protected:
	void resizeEvent(QResizeEvent *e) override {
		layoutControls();
		SectionWidget::resizeEvent(e);
	}

	void paintEvent(QPaintEvent *e) override {
		SectionWidget::paintEvent(e);
		Painter p(this);
		p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
		p.fillRect(
			QRect(0, height() - bottomHeight(), width(), bottomHeight()),
			st::historyComposeAreaBg);
	}

	void doSetInnerFocus() override {
		if (_active) {
			_field->setFocusFast();
		} else {
			setFocus();
		}
	}

	void showFinishedHook() override {
		doSetInnerFocus();
		markRead();
	}

private:
	[[nodiscard]] UserData *currentUser() const {
		const auto info = LookupInfo(_manager, _chatId);
		return info
			? session().data().userLoaded(UserId(info->userId))
			: nullptr;
	}

	void showProfile() {
		if (const auto user = currentUser()) {
			controller()->showPeerInfo(user);
		}
	}

	void startCall(bool video) {
		if (const auto user = currentUser()) {
			Core::App().calls().startOutgoingCall(user, { .video = video });
		}
	}

	void setSearchShown(bool shown) {
		if (_searchShown == shown) {
			return;
		}
		_searchShown = shown;
		_topBar->setVisible(!shown);
		_search->setVisible(!shown);
		_call->setVisible(!shown);
		_info->setVisible(!shown);
		_menu->setVisible(!shown);
		_searchField->setVisible(shown);
		_searchClose->setVisible(shown);
		if (shown) {
			_searchField->setFocusFast();
		} else {
			_searchField->setText(QString());
			if (_messages) {
				_messages->setQuery(QString());
			}
			doSetInnerFocus();
		}
		layoutControls();
		update();
	}

	void showMenu() {
		_popup = base::make_unique_q<Ui::PopupMenu>(this, st::defaultPopupMenu);
		_popup->addAction(u"Профиль"_q, [=] { showProfile(); });
		_popup->addAction(u"Поиск"_q, [=] { setSearchShown(true); });
		_popup->addAction(u"Аудиозвонок"_q, [=] { startCall(false); });
		_popup->addAction(u"Видеозвонок"_q, [=] { startCall(true); });
		_popup->addSeparator();
		if (const auto info = LookupInfo(_manager, _chatId)) {
			_popup->addAction(u"Таймер самоуничтожения"_q, [=] {
				controller()->show(Box(
					TtlBox,
					controller(),
					_chatId,
					info->ttl));
			});
		}
		_popup->addAction(u"Параметры секретного чата"_q, [=] {
			controller()->show(Box(SettingsBox, controller(), _chatId));
		});
		if (const auto info = LookupInfo(_manager, _chatId);
			info && info->state != State::Discarded) {
			_popup->addSeparator();
			_popup->addAction(u"Завершить секретный чат"_q, [=] {
				_manager->discard(_chatId, false);
			});
		}
		_popup->popup(_menu->mapToGlobal(QPoint(
			_menu->width(),
			_menu->height())));
	}

	void layoutControls() {
		const auto top = st::topBarHeight;
		const auto bottom = bottomHeight();
		if (_searchShown) {
			_searchClose->moveToRight(0, 0);
			const auto fieldLeft = 16;
			const auto fieldWidth = std::max(
				width() - fieldLeft - _searchClose->width() - 8,
				0);
			_searchField->setGeometryToLeft(
				fieldLeft,
				st::historyAdminLogSearchTop,
				fieldWidth,
				_searchField->height());
		} else {
			auto right = 0;
			_menu->moveToRight(right, 0);
			right += _menu->width();
			_info->moveToRight(right, 0);
			right += _info->width();
			_call->moveToRight(right, 0);
			right += _call->width();
			_search->moveToRight(right, 0);
			right += _search->width() + st::topBarCallSkip;
			_topBar->moveToLeft(0, 0);
			_topBar->resizeToWidth(std::max(width() - right, 0));
		}
		_shadow->setGeometry(0, top, width(), st::lineWidth);
		_scroll->setGeometry(0, top, width(), std::max(height() - top - bottom, 0));
		if (_messages) {
			_messages->setMinimumContentHeight(_scroll->height());
			_messages->resizeToWidth(_scroll->width());
		}

		const auto controlsTop = height() - bottom;
		if (_active) {
			_attach->moveToLeft(0, controlsTop + 6);
			_send->moveToRight(0, controlsTop + 6);
			const auto left = _attach->width();
			const auto right = _send->width();
			_field->setGeometryToLeft(
				left,
				controlsTop + 6,
				std::max(width() - left - right, 0),
				st::historyComposeField.heightMin);
		} else {
			_status->setGeometryToLeft(
				20,
				controlsTop + 16,
				std::max(width() - 40, 0),
				bottom - 20);
		}
	}
	static rpl::producer<PeerData*> peerValue(
			not_null<Window::SessionController*> controller,
			int chatId) {
		const auto info = LookupInfo(
			&controller->session().secretChats(),
			chatId);
		const auto peer = info
			? controller->session().data().userLoaded(UserId(info->userId))
			: nullptr;
		return rpl::single<PeerData*>(peer);
	}

	int bottomHeight() const {
		return _active ? kComposeHeight : kPanelHeight;
	}

	void refresh() {
		const auto info = LookupInfo(_manager, _chatId);
		if (!info) {
			_topBar->setText(u"Секретный чат"_q);
			_topBar->setSubtext(u"локальное состояние не найдено"_q);
			_active = false;
			_status->setText(u"Локальное состояние чата не найдено."_q);
		} else {
			const auto user = session().data().userLoaded(UserId(info->userId));
			_topBar->setText((user && !user->name().isEmpty())
				? user->name()
				: !info->userName.isEmpty()
				? info->userName
				: u"Пользователь %1"_q.arg(info->userId));
			_topBar->setSubtext((user && info->state == State::Active)
				? Data::OnlineText(user, base::unixtime::now())
				: StateText(info->state));
			if (user && !_userpicInstalled) {
				_userpicInstalled = true;
				_topBar->setWidget(Ui::CreateChild<Ui::UserpicButton>(
					_topBar.get(),
					user,
					st::topBarInfoButton));
			}
			_active = (info->state == State::Active);
			const auto status = (info->state == State::Waiting)
				? u"Запрос отправлен. Ожидаем, пока собеседник примет секретный чат."_q
				: (info->state == State::Requested)
				? u"Устанавливаем защищённое соединение…"_q
				: (info->state == State::Discarded)
				? u"Секретный чат завершён. Локальная история сохранена на этом ПК."_q
				: (info->state == State::Error)
				? u"Ошибка: %1"_q.arg(info->error)
				: QString();
			_status->setText(status);
		}
		_field->setVisible(_active);
		_attach->setVisible(_active);
		_send->setVisible(_active);
		_status->setVisible(!_active);
		_messages->refresh();
		markRead();
		layoutControls();
		crl::on_main(this, [=] {
			_scroll->scrollToY(_scroll->scrollTopMax());
		});
		update();
	}

	void markRead() {
		if (!_active) {
			return;
		}
		auto maxDate = 0;
		for (const auto &message : _manager->messages(_chatId)) {
			if (!message.outgoing) {
				maxDate = std::max(maxDate, message.date);
			}
		}
		if (maxDate) {
			_manager->markRead(_chatId, maxDate);
		}
	}

	const int _chatId;
	const not_null<Manager*> _manager;
	object_ptr<HeaderButton> _topBar;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::IconButton> _call;
	object_ptr<Ui::IconButton> _info;
	object_ptr<Ui::IconButton> _menu;
	object_ptr<Ui::InputField> _searchField;
	object_ptr<Ui::IconButton> _searchClose;
	object_ptr<Ui::PlainShadow> _shadow;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<MessagesWidget> _messages;
	object_ptr<Ui::InputField> _field;
	object_ptr<Ui::IconButton> _attach;
	object_ptr<Ui::SendButton> _send;
	object_ptr<Ui::FlatLabel> _status;
	base::unique_qptr<Ui::PopupMenu> _popup;
	bool _active = false;
	bool _searchShown = false;
	bool _userpicInstalled = false;
};

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third || &controller->session() != _session) {
		return nullptr;
	}
	auto result = object_ptr<Widget>(parent, controller, _chatId);
	result->setInternalState(geometry);
	return result;
}

} // namespace

std::shared_ptr<Window::SectionMemento> MakeMemento(
		not_null<Main::Session*> session,
		int chatId) {
	if (const auto entry = session->secretChats().entry(chatId)) {
		if (const auto history = entry->history()) {
			return std::make_shared<HistoryView::ChatMemento>(
				HistoryView::ChatViewId{ .history = history });
		}
	}
	return std::make_shared<Memento>(session, chatId);
}

void ShowSettings(
		not_null<Window::SessionController*> controller,
		int chatId) {
	controller->show(Box(SettingsBox, controller, chatId));
}

void ShowTtlSettings(
		not_null<Window::SessionController*> controller,
		int chatId) {
	const auto info = LookupInfo(&controller->session().secretChats(), chatId);
	if (info) {
		controller->show(Box(TtlBox, controller, chatId, info->ttl));
	}
}

} // namespace SecretChats
