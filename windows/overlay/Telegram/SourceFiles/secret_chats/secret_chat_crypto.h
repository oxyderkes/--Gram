/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_auth_key.h"

namespace SecretChats::Crypto {

struct Decrypted final {
	QByteArray bytes;
	bool valid = false;
};

[[nodiscard]] MTP::AuthKey::Data PrepareAuthKey(
	bytes::const_span computed);
[[nodiscard]] uint64 KeyFingerprint(const MTP::AuthKey::Data &key);
[[nodiscard]] QByteArray Encrypt(
	const QByteArray &serialized,
	const MTP::AuthKey::Data &key,
	bool originatorToParticipant);
[[nodiscard]] Decrypted Decrypt(
	const QByteArray &encrypted,
	const MTP::AuthKey::Data &key,
	bool originatorToParticipant);
[[nodiscard]] bool SelfTest();

} // namespace SecretChats::Crypto
