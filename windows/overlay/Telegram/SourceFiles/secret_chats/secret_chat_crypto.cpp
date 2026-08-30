/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "secret_chats/secret_chat_crypto.h"

#include "base/openssl_help.h"
#include "base/random.h"

#include <QtCore/QtEndian>

namespace SecretChats::Crypto {
namespace {

constexpr auto kKeyIdSize = 8;
constexpr auto kMessageKeySize = 16;
constexpr auto kLengthSize = 4;
constexpr auto kBlockSize = 16;
constexpr auto kMinPadding = 12;
constexpr auto kMaxPadding = 1024;
constexpr auto kMaxSerializedSize = 16 * 1024 * 1024;

[[nodiscard]] bool ConstantTimeEqual(const void *a, const void *b, int size) {
	auto left = static_cast<const uchar*>(a);
	auto right = static_cast<const uchar*>(b);
	auto different = uchar(0);
	for (auto i = 0; i != size; ++i) {
		different |= left[i] ^ right[i];
	}
	return different == 0;
}

[[nodiscard]] MTP::AuthKeyPtr MakeAuthKey(
		const MTP::AuthKey::Data &key) {
	return std::make_shared<MTP::AuthKey>(key);
}

[[nodiscard]] bytes::vector MessageKeyLarge(
		const MTP::AuthKeyPtr &key,
		const QByteArray &plaintext,
		bool originatorToParticipant) {
	const auto authKey = key->data();
	const auto x = originatorToParticipant ? 0 : 8;
	return openssl::Sha256(
		authKey.subspan(88 + x, 32),
		bytes::make_span(plaintext));
}

[[nodiscard]] int PaddingSize(int serializedSize) {
	const auto withMinimum = kLengthSize + serializedSize + kMinPadding;
	return kMinPadding + ((kBlockSize - (withMinimum % kBlockSize)) % kBlockSize);
}

} // namespace

MTP::AuthKey::Data PrepareAuthKey(bytes::const_span computed) {
	auto result = MTP::AuthKey::Data();
	MTP::AuthKey::FillData(result, computed);
	return result;
}

uint64 KeyFingerprint(const MTP::AuthKey::Data &key) {
	return MTP::AuthKey(key).keyId();
}

QByteArray Encrypt(
		const QByteArray &serialized,
		const MTP::AuthKey::Data &key,
		bool originatorToParticipant) {
	if (serialized.isEmpty() || serialized.size() > kMaxSerializedSize) {
		return {};
	}

	const auto padding = PaddingSize(serialized.size());
	auto plaintext = QByteArray(
		kLengthSize + serialized.size() + padding,
		Qt::Uninitialized);
	qToLittleEndian<quint32>(
		serialized.size(),
		reinterpret_cast<uchar*>(plaintext.data()));
	memcpy(
		plaintext.data() + kLengthSize,
		serialized.constData(),
		serialized.size());
	base::RandomFill(
		plaintext.data() + kLengthSize + serialized.size(),
		padding);

	const auto authKey = MakeAuthKey(key);
	const auto messageKeyLarge = MessageKeyLarge(
		authKey,
		plaintext,
		originatorToParticipant);
	auto messageKey = MTPint128();
	memcpy(&messageKey, messageKeyLarge.data() + 8, kMessageKeySize);

	auto aesKey = MTPint256();
	auto aesIv = MTPint256();
	authKey->prepareAES(
		messageKey,
		aesKey,
		aesIv,
		originatorToParticipant);

	auto result = QByteArray(
		kKeyIdSize + kMessageKeySize + plaintext.size(),
		Qt::Uninitialized);
	qToLittleEndian<quint64>(
		authKey->keyId(),
		reinterpret_cast<uchar*>(result.data()));
	memcpy(result.data() + kKeyIdSize, &messageKey, kMessageKeySize);
	MTP::aesIgeEncryptRaw(
		plaintext.constData(),
		result.data() + kKeyIdSize + kMessageKeySize,
		plaintext.size(),
		&aesKey,
		&aesIv);
	return result;
}

Decrypted Decrypt(
		const QByteArray &encrypted,
		const MTP::AuthKey::Data &key,
		bool originatorToParticipant) {
	const auto header = kKeyIdSize + kMessageKeySize;
	const auto cipherSize = encrypted.size() - header;
	if (cipherSize < kBlockSize || (cipherSize % kBlockSize) != 0) {
		return {};
	}

	const auto authKey = MakeAuthKey(key);
	const auto receivedKeyId = qFromLittleEndian<quint64>(
		reinterpret_cast<const uchar*>(encrypted.constData()));
	if (receivedKeyId != authKey->keyId()) {
		return {};
	}

	auto messageKey = MTPint128();
	memcpy(
		&messageKey,
		encrypted.constData() + kKeyIdSize,
		kMessageKeySize);
	auto aesKey = MTPint256();
	auto aesIv = MTPint256();
	authKey->prepareAES(
		messageKey,
		aesKey,
		aesIv,
		originatorToParticipant);

	auto plaintext = QByteArray(cipherSize, Qt::Uninitialized);
	MTP::aesIgeDecryptRaw(
		encrypted.constData() + header,
		plaintext.data(),
		cipherSize,
		&aesKey,
		&aesIv);

	const auto messageKeyLarge = MessageKeyLarge(
		authKey,
		plaintext,
		originatorToParticipant);
	if (!ConstantTimeEqual(
			&messageKey,
			messageKeyLarge.data() + 8,
			kMessageKeySize)) {
		return {};
	}

	const auto serializedSize = qFromLittleEndian<quint32>(
		reinterpret_cast<const uchar*>(plaintext.constData()));
	if (serializedSize > quint32(kMaxSerializedSize)
		|| serializedSize > quint32(plaintext.size() - kLengthSize)) {
		return {};
	}
	const auto padding = plaintext.size() - kLengthSize - serializedSize;
	if (padding < kMinPadding || padding > kMaxPadding) {
		return {};
	}

	return {
		.bytes = plaintext.mid(kLengthSize, serializedSize),
		.valid = true,
	};
}

bool SelfTest() {
	auto key = MTP::AuthKey::Data();
	for (auto i = size_t(); i != key.size(); ++i) {
		key[i] = std::byte((i * 29 + 17) & 0xFF);
	}
	const auto payload = QByteArray(
		"secret-chat-mtproto2-self-test\0with-binary",
		43);
	for (const auto direction : { false, true }) {
		const auto encrypted = Encrypt(payload, key, direction);
		if (encrypted.isEmpty()) {
			return false;
		}
		const auto decrypted = Decrypt(encrypted, key, direction);
		if (!decrypted.valid || decrypted.bytes != payload) {
			return false;
		}
		if (Decrypt(encrypted, key, !direction).valid) {
			return false;
		}
		auto damaged = encrypted;
		damaged[damaged.size() - 1] = char(damaged.back() ^ 0x01);
		if (Decrypt(damaged, key, direction).valid) {
			return false;
		}
	}
	return true;
}

} // namespace SecretChats::Crypto
