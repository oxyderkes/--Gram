/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "secret_chats/secret_chat_manager.h"

#include "apiwrap.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/utils.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_dh_utils.h"
#include "secret_chat_crypto.h"
#include "secret_chats/secret_chat_entry.h"
#include "secret_scheme.h"
#include "storage/storage_account.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>

#include <deque>

namespace SecretChats {
namespace {

constexpr auto kStorageKey = "agram-secret-chats-v2";
constexpr auto kSaveDeletedKey = "agram-secret-save-deleted-v1";
constexpr auto kMagic = quint32(0x53434741); // AGCS
constexpr auto kVersion = quint32(4);
constexpr auto kCurrentLayer = 144;
constexpr auto kInitialRemoteLayer = 46;
constexpr auto kMaxChats = quint32(1000);
constexpr auto kMaxMessages = quint32(50000);
constexpr auto kMaxOutgoingPackets = quint32(1000);
constexpr auto kRekeyMessageLimit = 100;
constexpr auto kRekeyTimeLimit = 7 * 24 * 60 * 60;
constexpr auto kMediaChunkSize = 512 * 1024;
constexpr auto kDefaultMediaLimit = 64 * 1024 * 1024;

struct EncryptedFileInfo final {
	uint64 id = 0;
	uint64 accessHash = 0;
	int64 size = 0;
	int dcId = 0;
	uint32 fingerprint = 0;
	bool valid = false;
};

struct MediaInfo final {
	int64 size = 0;
	QByteArray key;
	QByteArray iv;
	QString name;
	bool oneView = false;
	bool valid = false;
};

class MediaQueue final {
public:
	using Job = Fn<void(Fn<void()>)>;

	void enqueue(Job job) {
		_jobs.push_back(std::move(job));
		start();
	}

private:
	void start() {
		while (_active < 3 && !_jobs.empty()) {
			auto job = std::move(_jobs.front());
			_jobs.pop_front();
			++_active;
			job([=] {
				--_active;
				start();
			});
		}
	}

	std::deque<Job> _jobs;
	int _active = 0;
};

[[nodiscard]] MediaQueue &GlobalMediaQueue() {
	static auto queue = MediaQueue();
	return queue;
}

[[nodiscard]] EncryptedFileInfo ParseEncryptedFile(
		const MTPEncryptedFile &file) {
	return file.match([](const MTPDencryptedFileEmpty &) {
		return EncryptedFileInfo();
	}, [](const MTPDencryptedFile &data) {
		return EncryptedFileInfo{
			.id = uint64(data.vid().v),
			.accessHash = uint64(data.vaccess_hash().v),
			.size = int64(data.vsize().v),
			.dcId = data.vdc_id().v,
			.fingerprint = uint32(data.vkey_fingerprint().v),
			.valid = true,
		};
	});
}

[[nodiscard]] uint32 FileKeyFingerprint(
		const QByteArray &key,
		const QByteArray &iv) {
	const auto data = key + iv;
	const auto digest = hashMd5(data.constData(), data.size());
	auto first = uint32();
	auto second = uint32();
	memcpy(&first, digest.data(), sizeof(first));
	memcpy(&second, digest.data() + sizeof(first), sizeof(second));
	return first ^ second;
}

[[nodiscard]] MediaInfo ParseMedia(
		const Secret::MTPDecryptedMessageMedia &media) {
	return media.match([](const auto &data) {
		using Type = std::decay_t<decltype(data)>;
		auto result = MediaInfo();
		if constexpr (Secret::MTPDdecryptedMessageMediaPhoto::Is<Type>()) {
			result.size = int64(data.vsize().v);
			result.key = data.vkey().v;
			result.iv = data.viv().v;
			result.name = u"photo.jpg"_q;
			result.valid = true;
		} else if constexpr (
				Secret::MTPDdecryptedMessageMediaVideo::Is<Type>()) {
			result.size = int64(data.vsize().v);
			result.key = data.vkey().v;
			result.iv = data.viv().v;
			result.name = u"video.mp4"_q;
			result.valid = true;
		} else if constexpr (
				Secret::MTPDdecryptedMessageMediaDocument::Is<Type>()) {
			result.size = int64(data.vsize().v);
			result.key = data.vkey().v;
			result.iv = data.viv().v;
			result.name = u"file"_q;
			for (const auto &attribute : data.vattributes().v) {
				if (attribute.type()
						== Secret::mtpc_documentAttributeFilename) {
					result.name = qs(
						attribute.c_documentAttributeFilename().vfile_name());
					break;
				}
			}
			result.valid = true;
		} else if constexpr (
				Secret::MTPDdecryptedMessageMediaDocument46::Is<Type>()) {
			result.size = int64(data.vsize().v);
			result.key = data.vkey().v;
			result.iv = data.viv().v;
			result.name = u"file"_q;
			for (const auto &attribute : data.vattributes().v) {
				if (attribute.type()
						== Secret::mtpc_documentAttributeFilename) {
					result.name = qs(
						attribute.c_documentAttributeFilename().vfile_name());
					break;
				}
			}
			result.valid = true;
		}
		if (result.key.size() != 32 || result.iv.size() != 32
			|| result.size <= 0) {
			result.valid = false;
		}
		return result;
	});
}

template <typename Object>
[[nodiscard]] QByteArray Serialize(const Object &object) {
	auto buffer = mtpBuffer();
	object.write(buffer);
	auto result = QByteArray(
		buffer.size() * int(sizeof(mtpPrime)),
		Qt::Uninitialized);
	if (!result.isEmpty()) {
		memcpy(result.data(), buffer.constData(), result.size());
	}
	return result;
}

[[nodiscard]] std::optional<Secret::MTPDecryptedMessageLayer> ParseLayer(
		const QByteArray &serialized) {
	if (serialized.isEmpty()
		|| (serialized.size() % int(sizeof(mtpPrime))) != 0) {
		return std::nullopt;
	}
	auto buffer = mtpBuffer(serialized.size() / int(sizeof(mtpPrime)));
	memcpy(buffer.data(), serialized.constData(), serialized.size());
	const auto end = buffer.constData() + buffer.size();
	auto from = buffer.constData();
	auto result = Secret::MTPDecryptedMessageLayer();
	if (!result.read(from, end) || from != end) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] bool LayerSerializationSelfTest() {
	const auto randomId = uint64(0x1020304050607080ULL);
	const auto random = QByteArray::fromHex("00112233445566778899aabbccddee");
	const auto layer = Secret::MTPDecryptedMessageLayer(
		Secret::MTP_decryptedMessageLayer(
			MTP_bytes(random),
			MTP_int(kCurrentLayer),
			MTP_int(0),
			MTP_int(1),
			Secret::MTP_decryptedMessageService8(
				MTP_long(randomId),
				MTP_bytes(random),
				Secret::MTP_decryptedMessageActionNotifyLayer(
					MTP_int(kCurrentLayer)))));
	const auto serialized = Serialize(layer);
	if (serialized.size() < int(sizeof(mtpPrime))
		|| qFromLittleEndian<quint32>(
			reinterpret_cast<const uchar*>(serialized.constData()))
			!= Secret::mtpc_decryptedMessageLayer) {
		return false;
	}
	const auto parsed = ParseLayer(serialized);
	if (!parsed) {
		return false;
	}
	const auto &data = parsed->c_decryptedMessageLayer();
	const auto &message = data.vmessage();
	return data.vrandom_bytes().v == random
		&& data.vlayer().v == kCurrentLayer
		&& data.vin_seq_no().v == 0
		&& data.vout_seq_no().v == 1
		&& message.type() == Secret::mtpc_decryptedMessageService8
		&& message.c_decryptedMessageService8().vrandom_id().v == randomId
		&& message.c_decryptedMessageService8().vaction().type()
			== Secret::mtpc_decryptedMessageActionNotifyLayer;
}

[[nodiscard]] uint64 OtherUserId(
		uint64 ownId,
		uint64 adminId,
		uint64 participantId) {
	return (adminId == ownId) ? participantId : adminId;
}

[[nodiscard]] QByteArray KeyBytes(const MTP::AuthKey::Data &key) {
	return QByteArray(
		reinterpret_cast<const char*>(key.data()),
		key.size());
}

[[nodiscard]] bool ReadKey(
		const QByteArray &bytes,
		MTP::AuthKey::Data &key) {
	if (bytes.size() != int(key.size())) {
		return false;
	}
	memcpy(key.data(), bytes.constData(), key.size());
	return true;
}

} // namespace

class Manager::Private final {
public:
	enum class ExchangeStage : uchar {
		None,
		RequestSent,
		AcceptSent,
	};

	struct Exchange final {
		int64 id = 0;
		ExchangeStage stage = ExchangeStage::None;
		bytes::vector randomPower;
		MTP::AuthKey::Data candidate = {};
		bool hasCandidate = false;
	};

	struct OutgoingPacket final {
		uint64 randomId = 0;
		QByteArray data;
	};

	struct PendingPacket final {
		uint64 transportRandomId = 0;
		int date = 0;
		QByteArray serialized;
		EncryptedFileInfo file;
	};

	struct MediaDownload final {
		int chatId = 0;
		uint64 randomId = 0;
		EncryptedFileInfo file;
		MediaInfo media;
		QByteArray encrypted;
		int64 offset = 0;
		Fn<void()> done;
	};

	struct MediaUpload final {
		int chatId = 0;
		uint64 randomId = 0;
		uint64 fileId = 0;
		QString caption;
		QString fileName;
		QString mime;
		QString archivePath;
		QByteArray key;
		QByteArray iv;
		QByteArray encrypted;
		QString md5;
		int64 originalSize = 0;
		int part = 0;
		int parts = 0;
	};

	struct Chat final {
		ChatInfo info;
		int64 accessHash = 0;
		uint64 adminId = 0;
		uint64 participantId = 0;
		QByteArray gA;
		bytes::vector randomPower;
		MTP::AuthKey::Data key = {};
		bool hasKey = false;
		MTP::AuthKey::Data oldKey = {};
		bool hasOldKey = false;
		bool waitForNewKeyMessage = false;
		QByteArray originalKeySha1;
		int keyCreatedAt = 0;
		int keyUseCount = 0;
		Exchange exchange;
		bool rekeyDhPending = false;
		int inSeqNo = 0;
		int outSeqNo = 0;
		int lastRemoteInSeqNo = 0;
		mtpRequestId lastSendRequestId = 0;
		base::flat_map<int, OutgoingPacket> outgoing;
		base::flat_map<int, PendingPacket> pending;
		std::vector<Message> messages;
		bool needsLayerRepairNotify = false;
	};

	Private(Manager *owner, not_null<Main::Session*> session)
	: owner(owner)
	, session(session)
	, ttlTimer([this] { expireMessages(); }) {
		load();
		using UpdateFlag = Data::PeerUpdate::Flag;
		session->changes().peerUpdates(
			UpdateFlag::Name
		) | rpl::on_next([=](const Data::PeerUpdate &update) {
			const auto user = update.peer->asUser();
			if (!user) {
				return;
			}
			auto updated = false;
			for (auto &[id, chat] : chats) {
				if (chat.info.userId == peerToUser(user->id).bare
					&& chat.info.userName != user->name()) {
					chat.info.userName = user->name();
					updated = true;
				}
			}
			if (updated) {
				save(true);
				changed();
			}
		}, lifetime);
		expireMessages();
		ttlTimer.callEach(1000);
	}

	~Private() {
		save(true);
	}

	void create(not_null<UserData*> user, Fn<void(int)> done) {
		const auto input = user->inputUser();
		withDh([=, done = std::move(done)](QByteArray random) mutable {
			const auto generated = MTP::CreateModExp(
				dhG,
				bytes::make_span(dhPrime),
				bytes::make_span(random));
			if (generated.modexp.empty()) {
				return;
			}
			const auto requestRandomId = base::RandomValue<uint32>();
			const auto weak = base::make_weak(owner);
			session->api().request(MTPmessages_RequestEncryption(
				input,
				MTP_int(requestRandomId),
				MTP_bytes(bytes::make_span(generated.modexp))
			)).done([=, power = generated.randomPower, done = std::move(done)](
					const MTPEncryptedChat &result) mutable {
				const auto strong = weak.get();
				if (!strong) {
					return;
				}
				strong->_private->handleChat(result);
				const auto id = result.match([](const auto &data) {
					return data.vid().v;
				});
				if (const auto chat = strong->_private->lookup(id)) {
					chat->info.userName = user->name();
					chat->randomPower = std::move(power);
					strong->_private->save(true);
					strong->_private->changed();
				}
				if (done) {
					done(id);
				}
			}).send();
		});
	}

	void accept(int chatId) {
		const auto chat = lookup(chatId);
		if (!chat
			|| chat->info.state != State::Requested
			|| chat->gA.isEmpty()
			|| accepting.contains(chatId)) {
			return;
		}
		accepting.emplace(chatId);
		withDh([=](QByteArray random) {
			const auto current = lookup(chatId);
			if (!current || current->info.state != State::Requested) {
				accepting.remove(chatId);
				return;
			}
			const auto generated = MTP::CreateModExp(
				dhG,
				bytes::make_span(dhPrime),
				bytes::make_span(random));
			const auto computed = MTP::CreateAuthKey(
				bytes::make_span(current->gA),
				bytes::make_span(generated.randomPower),
				bytes::make_span(dhPrime));
			if (computed.empty()) {
				accepting.remove(chatId);
				fail(*current, u"Небезопасные параметры Diffie–Hellman."_q);
				return;
			}
			current->key = Crypto::PrepareAuthKey(computed);
			current->hasKey = true;
			current->info.keyFingerprint = Crypto::KeyFingerprint(current->key);
			const auto input = MTP_inputEncryptedChat(
				MTP_int(chatId),
				MTP_long(current->accessHash));
			const auto fingerprint = current->info.keyFingerprint;
			save(true);
			const auto weak = base::make_weak(owner);
			session->api().request(MTPmessages_AcceptEncryption(
				input,
				MTP_bytes(bytes::make_span(generated.modexp)),
				MTP_long(fingerprint)
			)).done([=](const MTPEncryptedChat &result) {
				if (const auto strong = weak.get()) {
					strong->_private->accepting.remove(chatId);
					strong->_private->handleChat(result);
				}
			}).fail([=](const MTP::Error &error) {
				if (const auto strong = weak.get()) {
					strong->_private->accepting.remove(chatId);
					if (const auto failed = strong->_private->lookup(chatId)) {
						strong->_private->fail(*failed, error.type());
					}
				}
			}).send();
		});
	}

	void discard(int chatId, bool deleteHistory) {
		const auto chat = lookup(chatId);
		if (!chat) {
			return;
		}
		using Flag = MTPmessages_discardEncryption::Flag;
		session->api().request(MTPmessages_DiscardEncryption(
			MTP_flags(deleteHistory ? Flag::f_delete_history : Flag()),
			MTP_int(chatId)
		)).send();
		chat->info.state = State::Discarded;
		if (deleteHistory) {
			chat->messages.clear();
		}
		save(true);
		changed();
	}

	void sendText(int chatId, const QString &text) {
		const auto chat = lookup(chatId);
		if (!chat || chat->info.state != State::Active || text.isEmpty()) {
			return;
		}
		const auto randomId = base::RandomValue<uint64>();
		const auto message = Secret::MTP_decryptedMessage(
			MTP_flags(0),
			MTP_long(randomId),
			MTP_int(chat->info.ttl),
			MTP_string(text),
			Secret::MTP_decryptedMessageMediaEmpty(),
			MTP_vector<Secret::MTPMessageEntity>({}),
			MTP_string(QString()),
			MTP_long(0),
			MTP_long(0));
		chat->messages.push_back({
			.randomId = randomId,
			.date = base::unixtime::now(),
			.text = text,
			.outgoing = true,
			.ttl = chat->info.ttl,
		});
		sendInner(*chat, randomId, message);
	}

	void sendFile(
			int chatId,
			const QString &path,
			const QString &caption) {
		const auto info = QFileInfo(path);
		if (!info.isFile()
			|| info.size() <= 0
			|| info.size() > kDefaultMediaLimit) {
			return;
		}
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return;
		}
		auto plain = file.readAll();
		if (plain.size() != info.size()) {
			return;
		}
		sendFileBytes(
			chatId,
			std::move(plain),
			info.fileName(),
			QMimeDatabase().mimeTypeForFile(info).name(),
			caption);
	}

	void sendFileBytes(
			int chatId,
			QByteArray plain,
			QString fileName,
			QString mime,
			const QString &caption) {
		const auto chat = lookup(chatId);
		if (!chat
			|| chat->info.state != State::Active
			|| plain.isEmpty()
			|| plain.size() > kDefaultMediaLimit) {
			return;
		}
		auto upload = std::make_shared<MediaUpload>();
		upload->chatId = chatId;
		upload->randomId = base::RandomValue<uint64>();
		upload->fileId = base::RandomValue<uint64>();
		if (!upload->randomId) {
			upload->randomId = 1;
		}
		if (!upload->fileId) {
			upload->fileId = 1;
		}
		upload->caption = caption;
		upload->fileName = fileName.isEmpty() ? u"file"_q : std::move(fileName);
		upload->mime = mime.isEmpty()
			? QMimeDatabase().mimeTypeForData(plain).name()
			: std::move(mime);
		upload->originalSize = plain.size();
		upload->key = QByteArray(32, Qt::Uninitialized);
		upload->iv = QByteArray(32, Qt::Uninitialized);
		base::RandomFill(upload->key.data(), upload->key.size());
		base::RandomFill(upload->iv.data(), upload->iv.size());
		const auto paddedSize = (plain.size() + 15) & ~15;
		plain.append(QByteArray(paddedSize - plain.size(), '\0'));
		upload->encrypted.resize(plain.size());
		MTP::aesIgeEncryptRaw(
			plain.constData(),
			upload->encrypted.data(),
			upload->encrypted.size(),
			upload->key.constData(),
			upload->iv.constData());
		const auto digest = hashMd5(
			upload->encrypted.constData(),
			upload->encrypted.size());
		upload->md5 = QByteArray(
			reinterpret_cast<const char*>(digest.data()),
			digest.size()).toHex();
		upload->parts = (upload->encrypted.size()
			+ kMediaChunkSize
			- 1) / kMediaChunkSize;
		if (!chat->info.ttl) {
			upload->archivePath = u"secret_%1_%2"_q
				.arg(chatId)
				.arg(QString::number(upload->randomId, 16));
			if (upload->archivePath.isEmpty()
				|| !session->local().writeSecretMedia(
					upload->archivePath,
					plain.left(upload->originalSize))) {
				upload->archivePath.clear();
			}
		}
		chat->messages.push_back({
			.randomId = upload->randomId,
			.date = base::unixtime::now(),
			.text = caption,
			.mediaName = upload->fileName,
			.archivePath = upload->archivePath,
			.outgoing = true,
			.ttl = chat->info.ttl,
		});
		save(true);
		changed();
		uploadNext(std::move(upload));
	}

	void uploadNext(const std::shared_ptr<MediaUpload> &upload) {
		if (upload->part >= upload->parts) {
			completeUpload(upload);
			return;
		}
		const auto offset = upload->part * kMediaChunkSize;
		const auto bytes = upload->encrypted.mid(offset, kMediaChunkSize);
		const auto weak = base::make_weak(owner);
		session->api().request(MTPupload_SaveFilePart(
			MTP_long(upload->fileId),
			MTP_int(upload->part),
			MTP_bytes(bytes)
		)).done([=](const MTPBool &result) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			if (!mtpIsTrue(result)) {
				strong->_private->markUploadFailed(upload);
				return;
			}
			++upload->part;
			strong->_private->uploadNext(upload);
		}).fail([=](const MTP::Error &) {
			if (const auto strong = weak.get()) {
				strong->_private->markUploadFailed(upload);
			}
		}).toDC(MTP::uploadDcId(0)).send();
	}

	void markUploadFailed(const std::shared_ptr<MediaUpload> &upload) {
		const auto chat = lookup(upload->chatId);
		if (!chat) {
			return;
		}
		const auto i = ranges::find(
			chat->messages,
			upload->randomId,
			&Message::randomId);
		if (i != end(chat->messages)) {
			i->service = true;
			i->text = u"Не удалось загрузить файл."_q;
		}
		save(true);
		changed();
	}

	void completeUpload(const std::shared_ptr<MediaUpload> &upload) {
		const auto chat = lookup(upload->chatId);
		if (!chat || chat->info.state != State::Active) {
			return;
		}
		auto attributes = QVector<Secret::MTPDocumentAttribute>();
		attributes.push_back(Secret::MTP_documentAttributeFilename(
			MTP_string(upload->fileName)));
		const auto media = Secret::MTP_decryptedMessageMediaDocument(
			MTP_bytes(QByteArray()),
			MTP_int(0),
			MTP_int(0),
			MTP_string(upload->mime),
			MTP_long(upload->originalSize),
			MTP_bytes(upload->key),
			MTP_bytes(upload->iv),
			MTP_vector<Secret::MTPDocumentAttribute>(attributes),
			MTP_string(upload->caption));
		const auto message = Secret::MTP_decryptedMessage(
			MTP_flags(Secret::MTPDdecryptedMessage::Flag::f_media),
			MTP_long(upload->randomId),
			MTP_int(chat->info.ttl),
			MTP_string(upload->caption),
			media,
			MTP_vector<Secret::MTPMessageEntity>({}),
			MTP_string(QString()),
			MTP_long(0),
			MTP_long(0));
		const auto data = prepareInner(*chat, upload->randomId, message);
		if (data.isEmpty()) {
			markUploadFailed(upload);
			return;
		}
		const auto fingerprint = FileKeyFingerprint(upload->key, upload->iv);
		const auto file = MTP_inputEncryptedFileUploaded(
			MTP_long(upload->fileId),
			MTP_int(upload->parts),
			MTP_string(upload->md5),
			MTP_int(fingerprint));
		auto request = session->api().request(MTPmessages_SendEncryptedFile(
			MTP_flags(0),
			input(*chat),
			MTP_long(upload->randomId),
			MTP_bytes(data),
			file));
		chat->lastSendRequestId = chat->lastSendRequestId
			? request.afterRequest(chat->lastSendRequestId).send()
			: request.send();
	}

	void deleteMessages(int chatId, const std::vector<uint64> &randomIds) {
		const auto chat = lookup(chatId);
		if (!chat || chat->info.state != State::Active || randomIds.empty()) {
			return;
		}
		auto ids = QVector<MTPlong>();
		ids.reserve(randomIds.size());
		for (const auto id : randomIds) {
			ids.push_back(MTP_long(id));
		}
		applyDelete(*chat, randomIds);
		sendService(*chat, Secret::MTP_decryptedMessageActionDeleteMessages(
			MTP_vector<MTPlong>(ids)));
	}

	void setTtl(int chatId, int seconds) {
		const auto chat = lookup(chatId);
		if (!chat || chat->info.state != State::Active) {
			return;
		}
		chat->info.ttl = std::clamp(seconds, 0, 7 * 24 * 60 * 60);
		sendService(*chat, Secret::MTP_decryptedMessageActionSetMessageTTL(
			MTP_int(chat->info.ttl)));
	}

	void markRead(int chatId, int maxDate) {
		const auto chat = lookup(chatId);
		if (!chat || chat->info.state != State::Active) {
			return;
		}
		session->api().request(MTPmessages_ReadEncryptedHistory(
			input(*chat),
			MTP_int(maxDate)
		)).send();
		auto ids = QVector<MTPlong>();
		const auto now = base::unixtime::now();
		for (auto &message : chat->messages) {
			if (!message.outgoing
				&& !message.service
				&& message.ttl > 0
				&& !message.destroyAt
				&& message.date <= maxDate) {
				message.destroyAt = now + message.ttl;
				ids.push_back(MTP_long(message.randomId));
			}
		}
		if (!ids.empty()) {
			sendService(*chat, Secret::MTP_decryptedMessageActionReadMessages(
				MTP_vector<MTPlong>(ids)));
			save(true);
			changed();
		}
	}

	void handleChat(const MTPEncryptedChat &update) {
		auto requested = 0;
		update.match([&](const auto &data) {
			using Type = std::decay_t<decltype(data)>;
			if constexpr (MTPDencryptedChatEmpty::Is<Type>()) {
				return;
			} else if constexpr (MTPDencryptedChatDiscarded::Is<Type>()) {
				const auto id = data.vid().v;
				if (const auto chat = lookup(id)) {
					chat->info.state = State::Discarded;
					if (data.is_history_deleted()) {
						chat->messages.clear();
					}
				}
			} else {
				const auto id = data.vid().v;
				auto &chat = ensure(id);
				chat.accessHash = data.vaccess_hash().v;
				chat.adminId = data.vadmin_id().v;
				chat.participantId = data.vparticipant_id().v;
				chat.info.userId = OtherUserId(
					session->userId().bare,
					chat.adminId,
					chat.participantId);
				if (const auto user = session->data().userLoaded(
						UserId(chat.info.userId))) {
					chat.info.userName = user->name();
				}
				if constexpr (MTPDencryptedChatWaiting::Is<Type>()) {
					chat.info.state = State::Waiting;
				} else if constexpr (MTPDencryptedChatRequested::Is<Type>()) {
					chat.info.state = State::Requested;
					chat.gA = data.vg_a().v;
					requested = id;
				} else if constexpr (MTPDencryptedChat::Is<Type>()) {
					activate(chat, data);
				}
			}
		});
		save(true);
		changed();
		if (requested) {
			accept(requested);
		}
	}

	void handleMessage(const MTPEncryptedMessage &update, int qts) {
		update.match([&](const auto &data) {
			auto file = EncryptedFileInfo();
			if constexpr (MTPDencryptedMessage::Is<decltype(data)>()) {
				file = ParseEncryptedFile(data.vfile());
			}
			const auto chat = lookup(data.vchat_id().v);
			if (!chat || chat->info.state != State::Active || !chat->hasKey) {
				return;
			}
			const auto originatorToParticipant = chat->adminId != session->userId().bare;
			auto decrypted = Crypto::Decrypt(
				data.vbytes().v,
				chat->key,
				originatorToParticipant);
			auto usedNewKey = decrypted.valid;
			if (!decrypted.valid && chat->hasOldKey) {
				decrypted = Crypto::Decrypt(
					data.vbytes().v,
					chat->oldKey,
					originatorToParticipant);
				usedNewKey = false;
			}
			auto implicitCommit = false;
			if (!decrypted.valid && chat->exchange.hasCandidate) {
				decrypted = Crypto::Decrypt(
					data.vbytes().v,
					chat->exchange.candidate,
					originatorToParticipant);
				if (decrypted.valid) {
					installNewKey(*chat, chat->exchange.candidate, false);
					clearExchange(*chat);
					usedNewKey = true;
					implicitCommit = true;
				}
			}
			if (!decrypted.valid) {
				fail(*chat, u"Не удалось проверить зашифрованное сообщение."_q);
				return;
			}
			processPacket(
				*chat,
				data.vrandom_id().v,
				data.vdate().v,
				decrypted.bytes,
				file);
			if (usedNewKey) {
				++chat->keyUseCount;
			}
			if (usedNewKey && chat->waitForNewKeyMessage) {
				chat->waitForNewKeyMessage = false;
				dropOldKeyIfSafe(*chat);
			}
			if (implicitCommit) {
				sendService(*chat, Secret::MTP_decryptedMessageActionNoop());
				dropOldKeyIfSafe(*chat);
			}
			maybeRekey(*chat);
		});
		save(true);
		if (qts > 0) {
			session->api().request(MTPmessages_ReceivedQueue(
				MTP_int(qts)
			)).send();
		}
		changed();
	}

	void handleRead(int chatId, int maxDate, int date) {
		const auto chat = lookup(chatId);
		if (!chat || chat->info.state != State::Active) {
			return;
		}
		const auto startedAt = std::max(date, base::unixtime::now());
		auto changedAny = false;
		for (auto &message : chat->messages) {
			if (message.outgoing
				&& !message.service
				&& message.ttl > 0
				&& !message.destroyAt
				&& message.date <= maxDate) {
				message.destroyAt = startedAt + message.ttl;
				changedAny = true;
			}
		}
		if (changedAny) {
			save(true);
			changed();
		}
	}

	std::vector<ChatInfo> chatsList() const {
		auto result = std::vector<ChatInfo>();
		result.reserve(chats.size());
		for (const auto &[id, chat] : chats) {
			result.push_back(chat.info);
		}
		return result;
	}

	std::vector<Message> messageList(int chatId) const {
		const auto i = chats.find(chatId);
		return (i != end(chats)) ? i->second.messages : std::vector<Message>();
	}

	QByteArray mediaBytes(int chatId, uint64 randomId) const {
		const auto chat = chats.find(chatId);
		if (chat == end(chats)) {
			return {};
		}
		const auto message = ranges::find(
			chat->second.messages,
			randomId,
			&Message::randomId);
		return (message == end(chat->second.messages)
			|| message->archivePath.isEmpty())
			? QByteArray()
			: session->local().readSecretMedia(message->archivePath);
	}

	void withDh(Fn<void(QByteArray)> done) {
		const auto weak = base::make_weak(owner);
		session->api().request(MTPmessages_GetDhConfig(
			MTP_int(dhVersion),
			MTP_int(MTP::ModExpFirst::kRandomPowerSize)
		)).done([=](const MTPmessages_DhConfig &result) mutable {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			const auto random = result.match([&](
					const MTPDmessages_dhConfig &data) {
				auto prime = bytes::make_vector(data.vp().v);
				if (!MTP::IsPrimeAndGood(prime, data.vg().v)) {
					return QByteArray();
				}
				strong->_private->dhG = data.vg().v;
				strong->_private->dhPrime = std::move(prime);
				strong->_private->dhVersion = data.vversion().v;
				return data.vrandom().v;
			}, [&](const MTPDmessages_dhConfigNotModified &data) {
				return (strong->_private->dhG
					&& !strong->_private->dhPrime.empty())
					? data.vrandom().v
					: QByteArray();
			});
			if (random.size() != MTP::ModExpFirst::kRandomPowerSize) {
				return;
			}
			strong->_private->save(true);
			done(random);
		}).send();
	}

	void activate(Chat &chat, const MTPDencryptedChat &data) {
		if (!chat.hasKey) {
			if (chat.randomPower.empty() || dhPrime.empty()) {
				fail(chat, u"Потеряно локальное состояние обмена ключами."_q);
				return;
			}
			const auto computed = MTP::CreateAuthKey(
				bytes::make_span(data.vg_a_or_b().v),
				bytes::make_span(chat.randomPower),
				bytes::make_span(dhPrime));
			if (computed.empty()) {
				fail(chat, u"Небезопасный открытый ключ собеседника."_q);
				return;
			}
			chat.key = Crypto::PrepareAuthKey(computed);
			chat.hasKey = true;
			chat.info.keyFingerprint = Crypto::KeyFingerprint(chat.key);
		}
		if (chat.info.keyFingerprint != uint64(data.vkey_fingerprint().v)) {
			fail(chat, u"Отпечаток ключа не совпал."_q);
			discard(chat.info.id, true);
			return;
		}
		if (!chat.keyCreatedAt) {
			chat.keyCreatedAt = base::unixtime::now();
			const auto hash = openssl::Sha1(bytes::make_span(chat.key));
			chat.originalKeySha1 = QByteArray(
				reinterpret_cast<const char*>(hash.data()),
				hash.size());
		}
		const auto wasActive = chat.info.state == State::Active;
		chat.info.state = State::Active;
		chat.randomPower.clear();
		chat.gA.clear();
		if (!wasActive) {
			sendLayerNotify(chat);
		}
	}

	void processPacket(
			Chat &chat,
			uint64 transportRandomId,
			int date,
			const QByteArray &serialized,
			EncryptedFileInfo file) {
		const auto parsed = ParseLayer(serialized);
		if (!parsed) {
			fail(chat, u"Некорректный TL-контейнер секретного чата."_q);
			return;
		}
		const auto &layer = parsed->c_decryptedMessageLayer();
		if (layer.vlayer().v > kCurrentLayer || layer.vlayer().v < 46) {
			fail(chat, u"Неподдерживаемый слой Secret Chats."_q);
			return;
		}
		chat.info.remoteLayer = std::max(chat.info.remoteLayer, layer.vlayer().v);

		const auto remoteIsAdmin = chat.adminId != session->userId().bare;
		const auto expectedInParity = remoteIsAdmin ? 0 : 1;
		const auto expectedOutParity = remoteIsAdmin ? 1 : 0;
		const auto encodedIn = layer.vin_seq_no().v;
		const auto encodedOut = layer.vout_seq_no().v;
		if (encodedIn < 0 || encodedOut < 0
			|| (encodedIn & 1) != expectedInParity
			|| (encodedOut & 1) != expectedOutParity) {
			fail(chat, u"Нарушена чётность seq_no."_q);
			discard(chat.info.id, false);
			return;
		}
		const auto remoteIn = encodedIn / 2;
		const auto remoteOut = encodedOut / 2;
		if (remoteIn < chat.lastRemoteInSeqNo || remoteIn > chat.outSeqNo) {
			fail(chat, u"Некорректное подтверждение seq_no."_q);
			discard(chat.info.id, false);
			return;
		}
		chat.lastRemoteInSeqNo = remoteIn;
		for (auto i = chat.outgoing.begin(); i != chat.outgoing.end();) {
			i = (i->first < remoteIn) ? chat.outgoing.erase(i) : std::next(i);
		}

		const auto &inner = layer.vmessage();
		const auto resend = (inner.type() == Secret::mtpc_decryptedMessageService)
			&& (inner.c_decryptedMessageService().vaction().type()
				== Secret::mtpc_decryptedMessageActionResend);
		if (resend) {
			applyMessage(chat, date, inner);
		}
		if (remoteOut < chat.inSeqNo) {
			return;
		} else if (remoteOut > chat.inSeqNo) {
			chat.pending.emplace(remoteOut, PendingPacket{
				.transportRandomId = transportRandomId,
				.date = date,
				.serialized = serialized,
				.file = file,
			});
			if (chat.pending.size() == 1) {
				sendService(chat, Secret::MTP_decryptedMessageActionResend(
					MTP_int(2 * chat.inSeqNo + expectedOutParity),
					MTP_int(encodedOut - 2)));
			}
			return;
		}

		if (!resend) {
			applyMessage(chat, date, inner, file);
		}
		++chat.inSeqNo;
		while (true) {
			const auto i = chat.pending.find(chat.inSeqNo);
			if (i == end(chat.pending)) {
				break;
			}
			const auto packet = std::move(i->second);
			chat.pending.erase(i);
			processPacket(
				chat,
				packet.transportRandomId,
				packet.date,
				packet.serialized,
				packet.file);
		}
	}

	void applyMessage(
			Chat &chat,
			int date,
			const Secret::MTPDecryptedMessage &message,
			EncryptedFileInfo file = {}) {
		const auto append = [&](uint64 randomId, int ttl, const QString &text,
				const Secret::MTPDecryptedMessageMedia *value) {
			auto item = Message{
				.randomId = randomId,
				.date = date,
				.text = text,
				.outgoing = false,
				.ttl = ttl,
			};
			auto media = MediaInfo();
			if (value) {
				media = ParseMedia(*value);
				item.mediaName = media.name;
				media.oneView = item.ttl > 0;
				item.oneView = media.oneView;
			}
			chat.messages.push_back(item);
			if (media.valid && file.valid && !item.ttl && !item.oneView) {
				queueMediaDownload(chat, item.randomId, file, std::move(media));
			}
		};
		if (message.type() == Secret::mtpc_decryptedMessage) {
			const auto &data = message.c_decryptedMessage();
			const auto media = data.vmedia();
			append(
				uint64(data.vrandom_id().v),
				data.vttl().v,
				qs(data.vmessage()),
				media ? &*media : nullptr);
			return;
		} else if (message.type() == Secret::mtpc_decryptedMessage46) {
			const auto &data = message.c_decryptedMessage46();
			const auto media = data.vmedia();
			append(
				uint64(data.vrandom_id().v),
				data.vttl().v,
				qs(data.vmessage()),
				media ? &*media : nullptr);
			return;
		} else if (message.type() == Secret::mtpc_decryptedMessage23) {
			const auto &data = message.c_decryptedMessage23();
			append(
				uint64(data.vrandom_id().v),
				data.vttl().v,
				qs(data.vmessage()),
				&data.vmedia());
			return;
		} else if (message.type() == Secret::mtpc_decryptedMessage8) {
			const auto &data = message.c_decryptedMessage8();
			append(
				uint64(data.vrandom_id().v),
				0,
				qs(data.vmessage()),
				&data.vmedia());
			return;
		}
		auto action = Secret::MTPDecryptedMessageAction();
		if (message.type() == Secret::mtpc_decryptedMessageService) {
			action = message.c_decryptedMessageService().vaction();
		} else if (message.type()
				== Secret::mtpc_decryptedMessageService8) {
			action = message.c_decryptedMessageService8().vaction();
		} else {
			return;
		}
		switch (action.type()) {
		case Secret::mtpc_decryptedMessageActionNotifyLayer: {
			chat.info.remoteLayer = std::max(
				chat.info.remoteLayer,
				action.c_decryptedMessageActionNotifyLayer().vlayer().v);
		} break;
		case Secret::mtpc_decryptedMessageActionSetMessageTTL: {
			chat.info.ttl = std::max(
				action.c_decryptedMessageActionSetMessageTTL().vttl_seconds().v,
				0);
		} break;
		case Secret::mtpc_decryptedMessageActionDeleteMessages: {
			const auto &ids = action.c_decryptedMessageActionDeleteMessages(
			).vrandom_ids().v;
			auto values = std::vector<uint64>();
			values.reserve(ids.size());
			for (const auto &id : ids) {
				values.push_back(id.v);
			}
			applyDelete(chat, values);
		} break;
		case Secret::mtpc_decryptedMessageActionReadMessages: {
			const auto &ids = action.c_decryptedMessageActionReadMessages(
			).vrandom_ids().v;
			const auto now = base::unixtime::now();
			for (const auto &id : ids) {
				const auto i = ranges::find(
					chat.messages,
					uint64(id.v),
					&Message::randomId);
				if (i != end(chat.messages)
					&& i->outgoing
					&& i->ttl > 0
					&& !i->destroyAt) {
					i->destroyAt = now + i->ttl;
				}
			}
		} break;
		case Secret::mtpc_decryptedMessageActionFlushHistory: {
			for (auto &item : chat.messages) {
				if (item.ttl || item.oneView || !saveDeleted) {
					item.randomId = 0;
				} else {
					item.deleted = true;
					item.opacity = .4f;
				}
			}
			chat.messages.erase(
				std::remove_if(
					chat.messages.begin(),
					chat.messages.end(),
					[](const Message &item) { return !item.randomId; }),
				chat.messages.end());
		} break;
		case Secret::mtpc_decryptedMessageActionResend: {
			const auto &range = action.c_decryptedMessageActionResend();
			resend(chat, range.vstart_seq_no().v, range.vend_seq_no().v);
		} break;
		case Secret::mtpc_decryptedMessageActionRequestKey: {
			const auto &request = action.c_decryptedMessageActionRequestKey();
			handleRequestKey(chat, request.vexchange_id().v, request.vg_a().v);
		} break;
		case Secret::mtpc_decryptedMessageActionAcceptKey: {
			const auto &accepted = action.c_decryptedMessageActionAcceptKey();
			handleAcceptKey(
				chat,
				accepted.vexchange_id().v,
				accepted.vg_b().v,
				accepted.vkey_fingerprint().v);
		} break;
		case Secret::mtpc_decryptedMessageActionCommitKey: {
			const auto &commit = action.c_decryptedMessageActionCommitKey();
			handleCommitKey(
				chat,
				commit.vexchange_id().v,
				commit.vkey_fingerprint().v);
		} break;
		case Secret::mtpc_decryptedMessageActionAbortKey: {
			const auto id = action.c_decryptedMessageActionAbortKey(
			).vexchange_id().v;
			if (chat.exchange.id == id) {
				clearExchange(chat);
			}
		} break;
		default: break;
		}
	}

	void handleRequestKey(Chat &chat, int64 exchangeId, const QByteArray &gA) {
		if (dhPrime.empty() || !dhG) {
			sendService(chat, Secret::MTP_decryptedMessageActionAbortKey(
				MTP_long(exchangeId)));
			return;
		}
		if (chat.exchange.stage != ExchangeStage::None) {
			if (chat.exchange.id == exchangeId) {
				clearExchange(chat);
				return;
			}
			if (chat.exchange.stage != ExchangeStage::RequestSent
				|| chat.exchange.id > exchangeId) {
				return;
			}
			clearExchange(chat);
		}
		auto random = bytes::vector(MTP::ModExpFirst::kRandomPowerSize);
		bytes::set_random(random);
		const auto generated = MTP::CreateModExp(
			dhG,
			bytes::make_span(dhPrime),
			bytes::make_span(random));
		const auto computed = MTP::CreateAuthKey(
			bytes::make_span(gA),
			bytes::make_span(generated.randomPower),
			bytes::make_span(dhPrime));
		if (computed.empty()) {
			sendService(chat, Secret::MTP_decryptedMessageActionAbortKey(
				MTP_long(exchangeId)));
			return;
		}
		chat.exchange.id = exchangeId;
		chat.exchange.stage = ExchangeStage::AcceptSent;
		chat.exchange.candidate = Crypto::PrepareAuthKey(computed);
		chat.exchange.hasCandidate = true;
		const auto fingerprint = Crypto::KeyFingerprint(
			chat.exchange.candidate);
		sendService(chat, Secret::MTP_decryptedMessageActionAcceptKey(
			MTP_long(exchangeId),
			MTP_bytes(bytes::make_span(generated.modexp)),
			MTP_long(fingerprint)));
	}

	void handleAcceptKey(
			Chat &chat,
			int64 exchangeId,
			const QByteArray &gB,
			int64 fingerprint) {
		if (chat.exchange.stage != ExchangeStage::RequestSent
			|| chat.exchange.id != exchangeId
			|| chat.exchange.randomPower.empty()) {
			return;
		}
		const auto computed = MTP::CreateAuthKey(
			bytes::make_span(gB),
			bytes::make_span(chat.exchange.randomPower),
			bytes::make_span(dhPrime));
		if (computed.empty()) {
			sendService(chat, Secret::MTP_decryptedMessageActionAbortKey(
				MTP_long(exchangeId)));
			clearExchange(chat);
			return;
		}
		const auto candidate = Crypto::PrepareAuthKey(computed);
		const auto expected = Crypto::KeyFingerprint(candidate);
		if (expected != uint64(fingerprint)) {
			sendService(chat, Secret::MTP_decryptedMessageActionAbortKey(
				MTP_long(exchangeId)));
			clearExchange(chat);
			return;
		}
		sendService(chat, Secret::MTP_decryptedMessageActionCommitKey(
			MTP_long(exchangeId),
			MTP_long(expected)));
		installNewKey(chat, candidate, true);
		clearExchange(chat);
	}

	void handleCommitKey(Chat &chat, int64 exchangeId, int64 fingerprint) {
		if (chat.exchange.stage != ExchangeStage::AcceptSent
			|| chat.exchange.id != exchangeId
			|| !chat.exchange.hasCandidate) {
			return;
		}
		const auto expected = Crypto::KeyFingerprint(chat.exchange.candidate);
		if (expected != uint64(fingerprint)) {
			sendService(chat, Secret::MTP_decryptedMessageActionAbortKey(
				MTP_long(exchangeId)));
			clearExchange(chat);
			return;
		}
		const auto candidate = chat.exchange.candidate;
		installNewKey(chat, candidate, false);
		clearExchange(chat);
		sendService(chat, Secret::MTP_decryptedMessageActionNoop());
		dropOldKeyIfSafe(chat);
	}

	void applyDelete(Chat &chat, const std::vector<uint64> &randomIds) {
		const auto contains = [&](uint64 value) {
			return std::find(begin(randomIds), end(randomIds), value)
				!= end(randomIds);
		};
		chat.messages.erase(
			std::remove_if(
				chat.messages.begin(),
				chat.messages.end(),
				[&](Message &item) {
					if (!contains(item.randomId)) {
						return false;
					} else if (saveDeleted && !item.ttl && !item.oneView) {
						item.deleted = true;
						item.opacity = .4f;
						return false;
					}
					return true;
				}),
			chat.messages.end());
	}

	void resend(Chat &chat, int start, int endSeq) {
		const auto ownIsAdmin = chat.adminId == session->userId().bare;
		const auto parity = ownIsAdmin ? 1 : 0;
		if (start < 0 || endSeq < start
			|| (start & 1) != parity || (endSeq & 1) != parity) {
			discard(chat.info.id, false);
			return;
		}
		for (auto encoded = start; encoded <= endSeq; encoded += 2) {
			const auto raw = encoded / 2;
			const auto i = chat.outgoing.find(raw);
			if (i == end(chat.outgoing)) {
				discard(chat.info.id, false);
				return;
			}
			sendPacket(chat, i->second.randomId, i->second.data, false);
		}
	}

	void queueMediaDownload(
			Chat &chat,
			uint64 randomId,
			EncryptedFileInfo file,
			MediaInfo media) {
		if (file.size <= 0 || file.size > kDefaultMediaLimit
			|| media.size <= 0 || media.size > kDefaultMediaLimit
			|| file.fingerprint != FileKeyFingerprint(media.key, media.iv)) {
			return;
		}
		const auto chatId = chat.info.id;
		const auto weak = base::make_weak(owner);
		GlobalMediaQueue().enqueue([=, media = std::move(media)](
				Fn<void()> done) mutable {
			const auto strong = weak.get();
			if (!strong) {
				done();
				return;
			}
			auto download = std::make_shared<MediaDownload>();
			download->chatId = chatId;
			download->randomId = randomId;
			download->file = file;
			download->media = std::move(media);
			download->encrypted.reserve(file.size);
			download->done = std::move(done);
			strong->_private->downloadNext(std::move(download));
		});
	}

	void downloadNext(std::shared_ptr<MediaDownload> download) {
		const auto weak = base::make_weak(owner);
		const auto left = download->file.size - download->offset;
		const auto limit = int(std::min<int64>(left, kMediaChunkSize));
		session->api().request(MTPupload_GetFile(
			MTP_flags(0),
			MTP_inputEncryptedFileLocation(
				MTP_long(download->file.id),
				MTP_long(download->file.accessHash)),
			MTP_long(download->offset),
			MTP_int(limit)
		)).done([=](const MTPupload_File &result) {
			const auto strong = weak.get();
			if (!strong) {
				finishDownload(download);
				return;
			}
			auto part = QByteArray();
			const auto good = result.match([&](const MTPDupload_file &data) {
				part = data.vbytes().v;
				return true;
			}, [](const auto &) {
				return false;
			});
			if (!good || part.isEmpty()
				|| part.size() > download->file.size - download->offset) {
				strong->_private->finishDownload(download);
				return;
			}
			download->encrypted.append(part);
			download->offset += part.size();
			if (download->offset >= download->file.size) {
				strong->_private->completeDownload(download);
			} else {
				strong->_private->downloadNext(download);
			}
		}).fail([=](const MTP::Error &) {
			if (const auto strong = weak.get()) {
				strong->_private->finishDownload(download);
			} else {
				finishDownload(download);
			}
		}).toDC(MTP::downloadDcId(download->file.dcId, 0)).send();
	}

	void completeDownload(const std::shared_ptr<MediaDownload> &download) {
		if (download->encrypted.isEmpty()
			|| (download->encrypted.size() % 16) != 0
			|| download->media.size > download->encrypted.size()) {
			finishDownload(download);
			return;
		}
		auto decrypted = QByteArray(
			download->encrypted.size(),
			Qt::Uninitialized);
		MTP::aesIgeDecryptRaw(
			download->encrypted.constData(),
			decrypted.data(),
			download->encrypted.size(),
			download->media.key.constData(),
			download->media.iv.constData());
		decrypted.truncate(download->media.size);
		const auto chat = lookup(download->chatId);
		if (!chat) {
			finishDownload(download);
			return;
		}
		const auto i = std::find_if(
			chat->messages.begin(),
			chat->messages.end(),
			[&](const Message &item) {
				return item.randomId == download->randomId;
			});
		if (i == chat->messages.end() || i->ttl || i->oneView) {
			finishDownload(download);
			return;
		}
		const auto archive = u"secret_%1_%2"_q
			.arg(download->chatId)
			.arg(QString::number(download->randomId, 16));
		if (session->local().writeSecretMedia(archive, decrypted)) {
			i->archivePath = archive;
			i->mediaName = download->media.name;
			save(true);
			changed();
		}
		finishDownload(download);
	}

	static void finishDownload(
			const std::shared_ptr<MediaDownload> &download) {
		if (download->done) {
			auto done = std::move(download->done);
			done();
		}
	}

	void maybeRekey(Chat &chat) {
		if (!chat.hasKey
			|| chat.exchange.stage != ExchangeStage::None
			|| chat.rekeyDhPending) {
			return;
		}
		const auto now = base::unixtime::now();
		const auto expired = chat.keyUseCount > 0
			&& chat.keyCreatedAt > 0
			&& (now - chat.keyCreatedAt) >= kRekeyTimeLimit;
		if (chat.keyUseCount <= kRekeyMessageLimit && !expired) {
			return;
		}
		if (dhG && !dhPrime.empty()) {
			startRekey(chat);
			return;
		}
		chat.rekeyDhPending = true;
		const auto chatId = chat.info.id;
		withDh([=](QByteArray) {
			if (const auto current = lookup(chatId)) {
				current->rekeyDhPending = false;
				startRekey(*current);
			}
		});
	}

	void startRekey(Chat &chat) {
		if (chat.exchange.stage != ExchangeStage::None
			|| !dhG || dhPrime.empty()) {
			return;
		}
		auto random = bytes::vector(MTP::ModExpFirst::kRandomPowerSize);
		bytes::set_random(random);
		const auto generated = MTP::CreateModExp(
			dhG,
			bytes::make_span(dhPrime),
			bytes::make_span(random));
		if (generated.modexp.empty()) {
			return;
		}
		auto exchangeId = int64(base::RandomValue<uint64>());
		if (!exchangeId) {
			exchangeId = 1;
		}
		chat.exchange.id = exchangeId;
		chat.exchange.stage = ExchangeStage::RequestSent;
		chat.exchange.randomPower = generated.randomPower;
		sendService(chat, Secret::MTP_decryptedMessageActionRequestKey(
			MTP_long(exchangeId),
			MTP_bytes(bytes::make_span(generated.modexp))));
	}

	void installNewKey(
			Chat &chat,
			const MTP::AuthKey::Data &key,
			bool initiator) {
		chat.oldKey = chat.key;
		chat.hasOldKey = true;
		chat.key = key;
		chat.hasKey = true;
		chat.info.keyFingerprint = Crypto::KeyFingerprint(chat.key);
		chat.keyCreatedAt = base::unixtime::now();
		chat.keyUseCount = 0;
		chat.waitForNewKeyMessage = initiator;
		save(true);
	}

	void clearExchange(Chat &chat) {
		chat.exchange = Exchange();
		save(true);
	}

	void dropOldKeyIfSafe(Chat &chat) {
		if (!chat.hasOldKey
			|| chat.waitForNewKeyMessage
			|| !chat.pending.empty()) {
			return;
		}
		chat.oldKey = MTP::AuthKey::Data();
		chat.hasOldKey = false;
		save(true);
	}

	void sendService(
			Chat &chat,
			const Secret::MTPDecryptedMessageAction &action) {
		const auto randomId = base::RandomValue<uint64>();
		sendInner(
			chat,
			randomId,
			Secret::MTP_decryptedMessageService(MTP_long(randomId), action));
	}

	void sendLayerNotify(Chat &chat) {
		const auto randomId = base::RandomValue<uint64>();
		auto random = QByteArray(15, Qt::Uninitialized);
		base::RandomFill(random.data(), random.size());
		sendInner(
			chat,
			randomId,
			Secret::MTP_decryptedMessageService8(
				MTP_long(randomId),
				MTP_bytes(random),
				Secret::MTP_decryptedMessageActionNotifyLayer(
					MTP_int(kCurrentLayer))));
	}

	void sendInner(
			Chat &chat,
			uint64 randomId,
			const Secret::MTPDecryptedMessage &message) {
		const auto encrypted = prepareInner(chat, randomId, message);
		if (!encrypted.isEmpty()) {
			sendPacket(chat, randomId, encrypted, true);
		}
	}

	[[nodiscard]] QByteArray prepareInner(
			Chat &chat,
			uint64 randomId,
			const Secret::MTPDecryptedMessage &message) {
		const auto ownIsAdmin = chat.adminId == session->userId().bare;
		const auto encodedIn = 2 * chat.inSeqNo + (ownIsAdmin ? 0 : 1);
		const auto encodedOut = 2 * chat.outSeqNo + (ownIsAdmin ? 1 : 0);
		auto random = QByteArray(15, Qt::Uninitialized);
		base::RandomFill(random.data(), random.size());
		// The layer constructor itself is boxed on the wire.
		const auto layer = Secret::MTPDecryptedMessageLayer(
			Secret::MTP_decryptedMessageLayer(
				MTP_bytes(random),
				MTP_int(kCurrentLayer),
				MTP_int(encodedIn),
				MTP_int(encodedOut),
				message));
		const auto encrypted = Crypto::Encrypt(
			Serialize(layer),
			chat.key,
			ownIsAdmin);
		if (encrypted.isEmpty()) {
			fail(chat, u"Не удалось зашифровать сообщение."_q);
			return QByteArray();
		}
		const auto rawOut = chat.outSeqNo++;
		chat.outgoing.emplace(rawOut, OutgoingPacket{
			.randomId = randomId,
			.data = encrypted,
		});
		++chat.keyUseCount;
		save(true);
		maybeRekey(chat);
		changed();
		return encrypted;
	}

	void sendPacket(
			Chat &chat,
			uint64 randomId,
			const QByteArray &data,
			bool chain) {
		auto request = session->api().request(MTPmessages_SendEncrypted(
			MTP_flags(0),
			input(chat),
			MTP_long(randomId),
			MTP_bytes(data)));
		chat.lastSendRequestId = (chain && chat.lastSendRequestId)
			? request.afterRequest(chat.lastSendRequestId).send()
			: request.send();
	}

	[[nodiscard]] MTPInputEncryptedChat input(const Chat &chat) const {
		return MTP_inputEncryptedChat(
			MTP_int(chat.info.id),
			MTP_long(chat.accessHash));
	}

	Chat &ensure(int id) {
		const auto [i, added] = chats.try_emplace(id);
		if (added) {
			i->second.info.id = id;
			i->second.info.remoteLayer = kInitialRemoteLayer;
		}
		return i->second;
	}

	Chat *lookup(int id) {
		const auto i = chats.find(id);
		return (i == end(chats)) ? nullptr : &i->second;
	}

	void fail(Chat &chat, const QString &error) {
		chat.info.state = State::Error;
		chat.info.error = error;
		save(true);
		changed();
	}

	void changed() {
		if (uiReady) {
			syncEntries();
		}
		changes.fire({});
	}

	void finishUiIntegration() {
		uiReady = true;
		syncEntries();
	}

	void prepareForSessionClear() {
		if (sessionClearing) {
			return;
		}
		sessionClearing = true;
		uiReady = false;
		ttlTimer.cancel();
		lifetime.destroy();

		// Entry::~Entry() removes itself from Data::Session chat lists and
		// therefore must run while the mirrored histories are still alive.
		entries.clear();
	}

	void acceptPending() {
		auto ids = std::vector<int>();
		for (const auto &[id, chat] : chats) {
			if (chat.info.state == State::Requested && !chat.gA.isEmpty()) {
				ids.push_back(id);
			}
		}
		for (const auto id : ids) {
			accept(id);
		}
	}

	void syncEntries() {
		auto nameUpdated = false;
		for (auto &[id, chat] : chats) {
			if (const auto user = session->data().userLoaded(
					UserId(chat.info.userId))) {
				if (chat.info.userName != user->name()) {
					chat.info.userName = user->name();
					nameUpdated = true;
				}
			}
			auto i = entries.find(id);
			if (i == end(entries)) {
				i = entries.emplace(
					id,
					std::make_unique<Entry>(session, owner, id)).first;
			}
			i->second->refresh();
		}
		if (nameUpdated) {
			save(true);
		}
	}

	Entry *entry(int chatId) const {
		const auto i = entries.find(chatId);
		return (i == end(entries)) ? nullptr : i->second.get();
	}

	void expireMessages() {
		const auto now = base::unixtime::now();
		auto removed = false;
		for (auto &[id, chat] : chats) {
			const auto was = chat.messages.size();
			chat.messages.erase(
				std::remove_if(
					chat.messages.begin(),
					chat.messages.end(),
					[&](const Message &message) {
						return message.destroyAt > 0
							&& message.destroyAt <= now;
					}),
				chat.messages.end());
			removed |= (was != chat.messages.size());
		}
		if (removed) {
			save(true);
			changed();
		}
	}

	void save(bool flush) {
		auto result = QByteArray();
		auto stream = QDataStream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << kMagic << kVersion << dhVersion << dhG;
		stream << QByteArray(
			reinterpret_cast<const char*>(dhPrime.data()),
			dhPrime.size());
		stream << quint32(chats.size());
		for (const auto &[id, chat] : chats) {
			stream << qint32(id)
				<< quint64(chat.info.userId)
				<< chat.info.userName
				<< quint8(chat.info.state)
				<< qint32(chat.info.ttl)
				<< qint32(chat.info.remoteLayer)
				<< quint64(chat.info.keyFingerprint)
				<< chat.info.error
				<< qint64(chat.accessHash)
				<< quint64(chat.adminId)
				<< quint64(chat.participantId)
				<< chat.gA
				<< QByteArray(
					reinterpret_cast<const char*>(chat.randomPower.data()),
					chat.randomPower.size())
				<< chat.hasKey
				<< (chat.hasKey ? KeyBytes(chat.key) : QByteArray())
				<< chat.hasOldKey
				<< (chat.hasOldKey ? KeyBytes(chat.oldKey) : QByteArray())
				<< chat.waitForNewKeyMessage
				<< chat.originalKeySha1
				<< qint32(chat.keyCreatedAt)
				<< qint32(chat.keyUseCount)
				<< qint64(chat.exchange.id)
				<< quint8(chat.exchange.stage)
				<< QByteArray(
					reinterpret_cast<const char*>(
						chat.exchange.randomPower.data()),
					chat.exchange.randomPower.size())
				<< chat.exchange.hasCandidate
				<< (chat.exchange.hasCandidate
					? KeyBytes(chat.exchange.candidate)
					: QByteArray())
				<< qint32(chat.inSeqNo)
				<< qint32(chat.outSeqNo)
				<< qint32(chat.lastRemoteInSeqNo);
			stream << quint32(chat.outgoing.size());
			for (const auto &[seq, packet] : chat.outgoing) {
				stream << qint32(seq) << quint64(packet.randomId) << packet.data;
			}
			stream << quint32(chat.messages.size());
			for (const auto &message : chat.messages) {
				stream << quint64(message.randomId)
					<< qint32(message.date)
					<< message.text
					<< message.mediaName
					<< message.archivePath
					<< message.outgoing
					<< message.service
					<< message.deleted
					<< message.oneView
					<< qint32(message.ttl)
					<< qint32(message.destroyAt)
					<< message.opacity;
			}
		}
		if (stream.status() != QDataStream::Ok) {
			return;
		}
		session->local().writePref<QByteArray>(kStorageKey, result);
		if (flush) {
			session->local().writePrefsNow();
		}
	}

	void load() {
		saveDeleted = session->local().readPref<bool>(kSaveDeletedKey, true);
		const auto stored = session->local().readPref<QByteArray>(kStorageKey);
		if (stored.isEmpty()) {
			return;
		}
		auto copy = stored;
		auto buffer = QBuffer(&copy);
		buffer.open(QIODevice::ReadOnly);
		auto stream = QDataStream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
		auto magic = quint32();
		auto version = quint32();
		stream >> magic >> version >> dhVersion >> dhG;
		auto prime = QByteArray();
		stream >> prime;
		if (magic != kMagic
			|| version < 2
			|| version > kVersion
			|| prime.size() > 256) {
			return;
		}
		dhPrime = bytes::make_vector(prime);
		auto count = quint32();
		stream >> count;
		if (count > kMaxChats) {
			return;
		}
		for (auto index = quint32(); index != count; ++index) {
			auto id = qint32();
			auto state = quint8();
			auto ttl = qint32();
			auto layer = qint32();
			auto accessHash = qint64();
			auto inSeq = qint32();
			auto outSeq = qint32();
			auto remoteIn = qint32();
			auto key = QByteArray();
			auto oldKey = QByteArray();
			auto exchangePower = QByteArray();
			auto candidate = QByteArray();
			auto exchangeStage = quint8();
			auto power = QByteArray();
			auto chat = Chat();
			stream >> id
				>> chat.info.userId;
			if (version >= 4) {
				stream >> chat.info.userName;
			}
			stream >> state
				>> ttl
				>> layer
				>> chat.info.keyFingerprint
				>> chat.info.error
				>> accessHash
				>> chat.adminId
				>> chat.participantId
				>> chat.gA
				>> power
				>> chat.hasKey
				>> key
				>> chat.hasOldKey
				>> oldKey
				>> chat.waitForNewKeyMessage
				>> chat.originalKeySha1
				>> chat.keyCreatedAt
				>> chat.keyUseCount
				>> chat.exchange.id
				>> exchangeStage
				>> exchangePower
				>> chat.exchange.hasCandidate
				>> candidate
				>> inSeq
				>> outSeq
				>> remoteIn;
			if (state > quint8(State::Error)
				|| exchangeStage > quint8(ExchangeStage::AcceptSent)
				|| inSeq < 0 || outSeq < 0 || remoteIn < 0
				|| chat.keyUseCount < 0
				|| power.size() > MTP::ModExpFirst::kRandomPowerSize
				|| exchangePower.size() > MTP::ModExpFirst::kRandomPowerSize
				|| (chat.hasKey && !ReadKey(key, chat.key))
				|| (chat.hasOldKey && !ReadKey(oldKey, chat.oldKey))
				|| (chat.exchange.hasCandidate
					&& !ReadKey(candidate, chat.exchange.candidate))) {
				return;
			}
			chat.info.id = id;
			chat.info.state = State(state);
			chat.info.ttl = ttl;
			chat.info.remoteLayer = layer;
			chat.accessHash = accessHash;
			chat.inSeqNo = inSeq;
			chat.outSeqNo = outSeq;
			chat.lastRemoteInSeqNo = remoteIn;
			chat.randomPower = bytes::make_vector(power);
			chat.exchange.stage = ExchangeStage(exchangeStage);
			chat.exchange.randomPower = bytes::make_vector(exchangePower);
			auto packets = quint32();
			stream >> packets;
			if (packets > kMaxOutgoingPackets) {
				return;
			}
			for (auto packet = quint32(); packet != packets; ++packet) {
				auto seq = qint32();
				auto item = OutgoingPacket();
				stream >> seq >> item.randomId >> item.data;
				chat.outgoing.emplace(seq, std::move(item));
			}
			auto messages = quint32();
			stream >> messages;
			if (messages > kMaxMessages) {
				return;
			}
			chat.messages.reserve(messages);
			for (auto message = quint32(); message != messages; ++message) {
				auto item = Message();
				stream >> item.randomId
					>> item.date
					>> item.text
					>> item.mediaName
					>> item.archivePath
					>> item.outgoing
					>> item.service
					>> item.deleted
					>> item.oneView
					>> item.ttl;
				if (version >= 3) {
					stream >> item.destroyAt;
				}
				stream >> item.opacity;
				chat.messages.push_back(std::move(item));
			}
			if (version == 3
				&& State(state) == State::Error
				&& chat.info.error
					== u"Некорректный TL-контейнер секретного чата."_q
				&& chat.hasKey) {
				state = quint8(State::Active);
				chat.info.error.clear();
				chat.inSeqNo = 0;
				chat.outSeqNo = 0;
				chat.lastRemoteInSeqNo = 0;
				chat.outgoing.clear();
				chat.pending.clear();
				chat.needsLayerRepairNotify = true;
			}
			chat.info.state = State(state);
			chats.emplace(id, std::move(chat));
		}
		if (stream.status() != QDataStream::Ok) {
			chats.clear();
		}
	}

	void finishLayerRepair() {
		for (auto &[id, chat] : chats) {
			if (!chat.needsLayerRepairNotify
				|| chat.info.state != State::Active
				|| !chat.hasKey) {
				continue;
			}
			chat.needsLayerRepairNotify = false;
			sendLayerNotify(chat);
		}
	}

	Manager *owner = nullptr;
	const not_null<Main::Session*> session;
	base::flat_map<int, Chat> chats;
	base::flat_map<int, std::unique_ptr<Entry>> entries;
	base::flat_set<int> accepting;
	int dhVersion = 0;
	int dhG = 0;
	bytes::vector dhPrime;
	bool saveDeleted = true;
	rpl::event_stream<> changes;
	bool uiReady = false;
	bool sessionClearing = false;
	base::Timer ttlTimer;
	rpl::lifetime lifetime;
};

Manager::Manager(not_null<Main::Session*> session)
: _private(std::make_unique<Private>(this, session)) {
	Assert(Crypto::SelfTest());
	Assert(LayerSerializationSelfTest());
	_private->finishLayerRepair();
}

void Manager::start() {
	if (_private->uiReady) {
		return;
	}
	_private->finishUiIntegration();
	_private->acceptPending();
}

void Manager::prepareForSessionClear() {
	_private->prepareForSessionClear();
}

Manager::~Manager() = default;

void Manager::create(not_null<UserData*> user, Fn<void(int)> done) {
	_private->create(user, std::move(done));
}

void Manager::accept(int chatId) {
	_private->accept(chatId);
}

void Manager::discard(int chatId, bool deleteHistory) {
	_private->discard(chatId, deleteHistory);
}

void Manager::sendText(int chatId, const QString &text) {
	_private->sendText(chatId, text);
}

void Manager::sendFile(
		int chatId,
		const QString &path,
		const QString &caption) {
	_private->sendFile(chatId, path, caption);
}

void Manager::sendFileBytes(
		int chatId,
		QByteArray bytes,
		QString fileName,
		QString mime,
		const QString &caption) {
	_private->sendFileBytes(
		chatId,
		std::move(bytes),
		std::move(fileName),
		std::move(mime),
		caption);
}

void Manager::deleteMessages(
		int chatId,
		const std::vector<uint64> &randomIds) {
	_private->deleteMessages(chatId, randomIds);
}

void Manager::setTtl(int chatId, int seconds) {
	_private->setTtl(chatId, seconds);
}

void Manager::markRead(int chatId, int maxDate) {
	_private->markRead(chatId, maxDate);
}

void Manager::handleChat(const MTPEncryptedChat &chat) {
	_private->handleChat(chat);
}

void Manager::handleMessage(const MTPEncryptedMessage &message, int qts) {
	_private->handleMessage(message, qts);
}

void Manager::handleRead(int chatId, int maxDate, int date) {
	_private->handleRead(chatId, maxDate, date);
}

std::vector<ChatInfo> Manager::chats() const {
	return _private->chatsList();
}

std::vector<Message> Manager::messages(int chatId) const {
	return _private->messageList(chatId);
}

QByteArray Manager::mediaBytes(int chatId, uint64 randomId) const {
	return _private->mediaBytes(chatId, randomId);
}

Entry *Manager::entry(int chatId) const {
	return _private->entry(chatId);
}

bool Manager::saveDeletedMessages() const {
	return _private->saveDeleted;
}

void Manager::setSaveDeletedMessages(bool value) {
	if (_private->saveDeleted == value) {
		return;
	}
	_private->saveDeleted = value;
	_private->session->local().writePref<bool>(kSaveDeletedKey, value);
	_private->session->local().writePrefsNow();
	_private->changed();
}

rpl::producer<> Manager::changes() const {
	return _private->changes.events();
}

} // namespace SecretChats
