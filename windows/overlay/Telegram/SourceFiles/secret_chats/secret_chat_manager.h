/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "rpl/event_stream.h"

class UserData;

namespace Main {
class Session;
} // namespace Main

namespace SecretChats {

class Entry;

enum class State : uchar {
	Requested,
	Waiting,
	Active,
	Discarded,
	Error,
};

struct ChatInfo final {
	int id = 0;
	uint64 userId = 0;
	QString userName;
	State state = State::Waiting;
	int ttl = 0;
	int remoteLayer = 46;
	uint64 keyFingerprint = 0;
	QString error;
};

struct Message final {
	uint64 randomId = 0;
	int date = 0;
	QString text;
	QString mediaName;
	QString archivePath;
	bool outgoing = false;
	bool service = false;
	bool deleted = false;
	bool oneView = false;
	int ttl = 0;
	int destroyAt = 0;
	float opacity = 1.;
};

class Manager final : public base::has_weak_ptr {
public:
	explicit Manager(not_null<Main::Session*> session);
	~Manager();
	void start();
	void prepareForSessionClear();

	void create(not_null<UserData*> user, Fn<void(int)> done = {});
	void accept(int chatId);
	void discard(int chatId, bool deleteHistory = false);
	void sendText(int chatId, const QString &text);
	void sendFile(
		int chatId,
		const QString &path,
		const QString &caption = QString());
	void sendFileBytes(
		int chatId,
		QByteArray bytes,
		QString fileName,
		QString mime,
		const QString &caption = QString());
	void deleteMessages(int chatId, const std::vector<uint64> &randomIds);
	void setTtl(int chatId, int seconds);
	void markRead(int chatId, int maxDate);

	void handleChat(const MTPEncryptedChat &chat);
	void handleMessage(const MTPEncryptedMessage &message, int qts);
	void handleRead(int chatId, int maxDate, int date);

	[[nodiscard]] std::vector<ChatInfo> chats() const;
	[[nodiscard]] std::vector<Message> messages(int chatId) const;
	[[nodiscard]] QByteArray mediaBytes(int chatId, uint64 randomId) const;
	[[nodiscard]] Entry *entry(int chatId) const;
	[[nodiscard]] bool saveDeletedMessages() const;
	void setSaveDeletedMessages(bool value);
	[[nodiscard]] rpl::producer<> changes() const;

private:
	class Private;
	const std::unique_ptr<Private> _private;
};

} // namespace SecretChats
