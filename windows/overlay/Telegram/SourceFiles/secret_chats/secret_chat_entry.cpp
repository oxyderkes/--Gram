/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "secret_chats/secret_chat_entry.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "secret_chats/secret_chat_manager.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/userpic_view.h"
#include "dialogs/ui/dialogs_layout.h"
#include "styles/style_dialogs.h"

#include <QtCore/QMimeDatabase>
#include <QtGui/QImage>

namespace SecretChats {
namespace {

[[nodiscard]] QString StatePreview(State state) {
	switch (state) {
	case State::Requested: return u"Входящий секретный чат"_q;
	case State::Waiting: return u"Ожидание ответа"_q;
	case State::Active: return u"Секретный чат создан"_q;
	case State::Discarded: return u"Секретный чат завершён"_q;
	case State::Error: return u"Ошибка секретного чата"_q;
	}
	Unexpected("State in SecretChats::StatePreview.");
}

[[nodiscard]] QString MessagePreview(const Message &message) {
	auto result = message.text;
	if (!message.mediaName.isEmpty()) {
		const auto attachment = u"📎 %1"_q.arg(message.mediaName);
		result = result.isEmpty() ? attachment : (result + u" · "_q + attachment);
	}
	if (result.isEmpty()) {
		result = message.service ? u"Служебное сообщение"_q : u"Сообщение"_q;
	}
	if (message.deleted) {
		result += u"  {DELETED}"_q;
	}
	return message.outgoing ? (u"Вы: "_q + result) : result;
}

[[nodiscard]] UserId MirrorUserId(int chatId) {
	return UserId(0xFFFF00000000ULL | uint64(uint32(chatId)));
}

[[nodiscard]] uint64 MirrorMediaId(int chatId, uint64 randomId) {
	auto result = randomId
		^ (uint64(uint32(chatId)) << 32)
		^ uint64(0xA6A6C7C700000001ULL);
	return result ? result : 1;
}

[[nodiscard]] bool IsImageName(const QString &name) {
	const auto lower = name.toLower();
	return lower.endsWith(u".jpg"_q)
		|| lower.endsWith(u".jpeg"_q)
		|| lower.endsWith(u".png"_q)
		|| lower.endsWith(u".webp"_q);
}

} // namespace

Entry::Entry(
		not_null<Main::Session*> session,
		not_null<Manager*> manager,
		int chatId)
: Dialogs::Entry(&session->data(), Dialogs::Entry::Type::SecretChat)
, _session(session)
, _manager(manager)
, _chatId(chatId)
, _fallbackDate(base::unixtime::now()) {
	setupHistory();
	refresh();
}

Entry::~Entry() {
	if (inChatList()) {
		owner().removeChatListEntry(this);
	}
}

void Entry::setupHistory() {
	const auto mirror = _session->data().user(MirrorUserId(_chatId));
	mirror->setName(
		u"Секретный чат"_q,
		QString(),
		QString(),
		QString());
	mirror->fullUpdated();
	_history = _session->data().history(mirror);
	_history->setAgramSecretChatId(_chatId);
	_history->clear(History::ClearType::Unload, true);
	_history->markLoadedAtTop();
}

UserData *Entry::user() const {
	return _userId ? _session->data().user(UserId(_userId)).get() : nullptr;
}

uint64 Entry::randomIdForMessage(MsgId id) const {
	const auto i = ranges::find_if(_mirrored, [&](const auto &pair) {
		return pair.second.id == id;
	});
	return (i == end(_mirrored)) ? 0 : i->first;
}

void Entry::syncHistory(const std::vector<Message> &messages) {
	if (!_history) {
		return;
	}
	auto present = base::flat_set<uint64>();
	present.reserve(messages.size());
	for (const auto &message : messages) {
		present.emplace(message.randomId);
	}
	for (auto i = _mirrored.begin(); i != _mirrored.end();) {
		if (present.contains(i->first)) {
			++i;
			continue;
		}
		if (const auto item = _history->owner().message(
				_history->peer->id,
				i->second.id)) {
			item->destroy();
		}
		i = _mirrored.erase(i);
	}

	for (const auto &message : messages) {
		const auto mediaReady = !message.mediaName.isEmpty()
			&& !message.archivePath.isEmpty();
		auto existing = _mirrored.find(message.randomId);
		const auto changed = (existing != end(_mirrored))
			&& (existing->second.text != message.text
				|| existing->second.mediaName != message.mediaName
				|| existing->second.mediaReady != mediaReady
				|| existing->second.service != message.service);
		if (changed) {
			if (const auto item = _history->owner().message(
					_history->peer->id,
					existing->second.id)) {
				item->destroy();
			}
		} else if (existing != end(_mirrored)) {
			if (message.deleted && !existing->second.deleted) {
				if (const auto item = _history->owner().message(
						_history->peer->id,
						existing->second.id)) {
					item->markAgramDeletedOnServer();
				}
				existing->second.deleted = true;
			}
			continue;
		}

		const auto id = (existing != end(_mirrored))
			? existing->second.id
			: _history->owner().nextLocalMessageId();
		auto text = message.text;
		if (!message.mediaName.isEmpty() && !mediaReady) {
			const auto pending = u"📎 %1 · загрузка…"_q.arg(
				message.mediaName);
			text = text.isEmpty() ? pending : (text + u"\n"_q + pending);
		} else if (text.isEmpty() && message.service) {
			text = u"Служебное сообщение"_q;
		}
		auto fields = HistoryItemCommonFields{
			.id = id,
			.flags = (MessageFlag::HistoryEntry
				| MessageFlag::HasFromId
				| MessageFlag::NoForwards
				| (message.outgoing
					? MessageFlag::Outgoing
					: MessageFlag(0))),
			.from = message.outgoing
				? _session->userPeerId()
				: _history->peer->id,
			.date = message.date ? message.date : _fallbackDate,
		};
		auto item = (HistoryItem*)nullptr;
		const auto bytes = mediaReady
			? _manager->mediaBytes(_chatId, message.randomId)
			: QByteArray();
		if (!bytes.isEmpty() && IsImageName(message.mediaName)) {
			auto image = QImage::fromData(bytes);
			if (!image.isNull()) {
				auto thumbs = PreparedPhotoThumbs();
				auto sizes = QVector<MTPPhotoSize>();
				const auto add = [&](char type, QImage prepared, QByteArray raw = {}) {
					sizes.push_back(MTP_photoSize(
						MTP_string(QString(QChar(type))),
						MTP_int(prepared.width()),
						MTP_int(prepared.height()),
						MTP_int(raw.size())));
					thumbs.emplace(type, PreparedPhotoThumb{
						.image = std::move(prepared),
						.bytes = std::move(raw),
					});
				};
				add('a', image.scaled(
					160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
				add('b', image.scaled(
					320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
				add('c', std::move(image), bytes);
				const auto photoId = PhotoId(MirrorMediaId(
					_chatId,
					message.randomId));
				const auto photo = _history->owner().processPhoto(
					MTP_photo(
						MTP_flags(0),
						MTP_long(photoId),
						MTP_long(0),
						MTP_bytes(QByteArray()),
						MTP_int(fields.date),
						MTP_vector<MTPPhotoSize>(sizes),
						MTPVector<MTPVideoSize>(),
						MTP_int(0)),
					thumbs);
				item = _history->addNewLocalMessage(
					std::move(fields),
					photo,
					TextWithEntities{ .text = text });
			}
		}
		if (!item && !bytes.isEmpty()) {
			auto attributes = QVector<MTPDocumentAttribute>();
			attributes.push_back(MTP_documentAttributeFilename(
				MTP_string(message.mediaName)));
			const auto mime = QMimeDatabase().mimeTypeForData(bytes).name();
			const auto document = _history->owner().document(
				DocumentId(MirrorMediaId(_chatId, message.randomId)),
				0,
				QByteArray(),
				fields.date,
				attributes,
				mime,
				InlineImageLocation(),
				ImageWithLocation(),
				ImageWithLocation(),
				false,
				0,
				bytes.size());
			document->setDataAndCache(bytes);
			item = _history->addNewLocalMessage(
				std::move(fields),
				document,
				TextWithEntities{ .text = text });
		}
		if (!item) {
			item = _history->addNewLocalMessage(
				std::move(fields),
				TextWithEntities{ .text = text },
				MTP_messageMediaEmpty());
		}
		if (message.deleted) {
			item->markAgramDeletedOnServer();
		}
		if (mediaReady && !bytes.isEmpty()) {
			item->markAgramSecretMediaLocal();
		}
		_mirrored[message.randomId] = {
			.id = id,
			.text = message.text,
			.mediaName = message.mediaName,
			.mediaReady = mediaReady,
			.service = message.service,
			.deleted = message.deleted,
		};
	}
}

void Entry::refresh() {
	const auto chats = _manager->chats();
	const auto i = ranges::find(chats, _chatId, &ChatInfo::id);
	_exists = (i != end(chats));
	if (!_exists) {
		updateChatListExistence();
		return;
	}
	_userId = i->userId;
	const auto loadedUser = _session->data().userLoaded(UserId(_userId));
	const auto newName = (loadedUser && !loadedUser->name().isEmpty())
		? loadedUser->name()
		: !i->userName.isEmpty()
		? i->userName
		: u"Пользователь %1"_q.arg(_userId);
	if (_name != newName) {
		_name = newName;
		_sortKey = TextUtilities::NameSortKey(_name);
		_nameWords.clear();
		_firstLetters.clear();
		for (const auto &word : TextUtilities::PrepareSearchWords(_name)) {
			_nameWords.insert(word);
			if (!word.isEmpty()) {
				_firstLetters.insert(word.front());
			}
		}
		++_nameVersion;
	}
	if (_history) {
		const auto mirror = _history->peer->asUser();
		mirror->setName(_name, QString(), QString(), QString());
		if (const auto real = this->user()) {
			mirror->setUserpic(
				real->userpicPhotoId(),
				real->userpicLocation(),
				real->userpicHasVideo());
			mirror->updateLastseen(real->lastseen());
		}
	}

	const auto messages = _manager->messages(_chatId);
	syncHistory(messages);
	if (!messages.empty()) {
		const auto &last = messages.back();
		_preview = MessagePreview(last);
		setChatListTimeId(last.date ? last.date : _fallbackDate);
	} else {
		_preview = i->error.isEmpty()
			? StatePreview(i->state)
			: (StatePreview(i->state) + u": "_q + i->error);
		setChatListTimeId(_fallbackDate);
	}
	updateChatListExistence();
	updateChatListEntry();
}

int Entry::fixedOnTopIndex() const {
	return 0;
}

bool Entry::shouldBeInChatList() const {
	return _exists;
}

Dialogs::UnreadState Entry::chatListUnreadState() const {
	return { .known = true };
}

Dialogs::BadgesState Entry::chatListBadgesState() const {
	return {};
}

HistoryItem *Entry::chatListMessage() const {
	return nullptr;
}

bool Entry::chatListMessageKnown() const {
	return true;
}

const QString &Entry::chatListName() const {
	return _name;
}

const QString &Entry::chatListNameSortKey() const {
	return _sortKey;
}

int Entry::chatListNameVersion() const {
	return _nameVersion;
}

const base::flat_set<QString> &Entry::chatListNameWords() const {
	return _nameWords;
}

const base::flat_set<QChar> &Entry::chatListFirstLetters() const {
	return _firstLetters;
}

QString Entry::chatListCustomPreview() const {
	return _preview;
}

void Entry::chatListPreloadData() {
	if (const auto user = _session->data().userLoaded(UserId(_userId))) {
		user->loadUserpic();
	}
}

void Entry::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const {
	const auto x = context.st->padding.left();
	const auto y = context.st->padding.top();
	const auto size = context.st->photoSize;
	if (const auto user = _session->data().userLoaded(UserId(_userId))) {
		user->paintUserpic(p, view, x, y, size);
		return;
	}
	p.setPen(Qt::NoPen);
	p.setBrush(st::dialogsUnreadBg);
	p.drawEllipse(QRect(x, y, size, size));
	p.setFont(st::semiboldFont);
	p.setPen(st::dialogsUnreadFg);
	p.drawText(QRect(x, y, size, size), Qt::AlignCenter, _name.left(1));
}

} // namespace SecretChats
