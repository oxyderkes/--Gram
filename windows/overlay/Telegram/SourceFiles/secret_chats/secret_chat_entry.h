/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"

namespace Main {
class Session;
} // namespace Main

class History;
class UserData;

namespace SecretChats {

class Manager;
struct Message;

class Entry final : public Dialogs::Entry {
public:
	Entry(
		not_null<Main::Session*> session,
		not_null<Manager*> manager,
		int chatId);
	~Entry();

	[[nodiscard]] int chatId() const {
		return _chatId;
	}
	[[nodiscard]] History *history() const {
		return _history;
	}
	[[nodiscard]] UserData *user() const;
	[[nodiscard]] uint64 randomIdForMessage(MsgId id) const;
	void refresh();

	[[nodiscard]] int fixedOnTopIndex() const override;
	[[nodiscard]] bool shouldBeInChatList() const override;
	[[nodiscard]] Dialogs::UnreadState chatListUnreadState() const override;
	[[nodiscard]] Dialogs::BadgesState chatListBadgesState() const override;
	[[nodiscard]] HistoryItem *chatListMessage() const override;
	[[nodiscard]] bool chatListMessageKnown() const override;
	[[nodiscard]] const QString &chatListName() const override;
	[[nodiscard]] const QString &chatListNameSortKey() const override;
	[[nodiscard]] int chatListNameVersion() const override;
	[[nodiscard]] const base::flat_set<QString> &chatListNameWords() const override;
	[[nodiscard]] const base::flat_set<QChar> &chatListFirstLetters() const override;
	[[nodiscard]] QString chatListCustomPreview() const;

	void chatListPreloadData() override;
	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const override;

private:
	struct MirroredMessage final {
		MsgId id = 0;
		QString text;
		QString mediaName;
		bool mediaReady = false;
		bool service = false;
		bool deleted = false;
	};

	void setupHistory();
	void syncHistory(const std::vector<Message> &messages);

	const not_null<Main::Session*> _session;
	const not_null<Manager*> _manager;
	const int _chatId;
	History *_history = nullptr;
	base::flat_map<uint64, MirroredMessage> _mirrored;
	uint64 _userId = 0;
	bool _exists = true;
	QString _name;
	QString _sortKey;
	QString _preview;
	base::flat_set<QString> _nameWords;
	base::flat_set<QChar> _firstLetters;
	int _nameVersion = 1;
	TimeId _fallbackDate = 0;
};

} // namespace SecretChats
