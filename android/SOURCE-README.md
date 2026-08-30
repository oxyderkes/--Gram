# ά‑Gram source overlay

This archive contains the files changed for the ά‑Gram `12.10.1-a-gram.5` APK. It intentionally does not contain the user's Telegram API credentials, private signing keystore, generated build output, Android SDK, NDK or Gradle caches.

## Base source

- Repository: https://github.com/DrKLO/Telegram
- Commit: `62b56a07ca7e30e39f7fd00a6728d6bbd716ca1c`
- Upstream version: `12.10.1 (7038)`
- License: GNU GPL v2 or later; see `LICENSE`.

The overlay preserves paths relative to the repository root. Copy it over a checkout of the exact commit.

The Android application ID and signing identity intentionally retain their pre-rebrand Manygram values so ά‑Gram can be installed as an in-place update without deleting local app data.

## Reconstruct and build

1. Clone the upstream repository with submodules and check out the commit above.
2. Copy every file from this overlay over the repository root.
3. Copy `local.properties.example` to `local.properties`, set `sdk.dir`, and add your own `TELEGRAM_API_ID` and `TELEGRAM_API_HASH` from https://my.telegram.org/apps. Keep the hash private.
4. Install JDK 17, Android SDK 36, Build Tools 36.0.0, NDK `27.2.12479018` and CMake 3.22.1.
5. Generate a signing key and provide its passwords only through environment variables or a private user-level Gradle configuration:

   ```powershell
   $env:AGRAM_RELEASE_STORE_PASSWORD = "<strong-private-password>"
   $env:AGRAM_RELEASE_KEY_PASSWORD = "<strong-private-password>"
   $env:AGRAM_RELEASE_KEY_ALIAS = "manygram"
   keytool -genkeypair -storetype PKCS12 -keystore TMessagesProj/config/manygram.keystore -storepass $env:AGRAM_RELEASE_STORE_PASSWORD -keypass $env:AGRAM_RELEASE_KEY_PASSWORD -alias $env:AGRAM_RELEASE_KEY_ALIAS -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=ά-Gram Personal Build, O=ά-Gram"
   ```

   Never commit the keystore or either password. Back up the release key securely: Android updates must be signed with the same key.

6. Build the arm64 standalone APK:

   ```powershell
   .\gradlew.bat :TMessagesProj_AppStandalone:assembleAfatStandalone --no-daemon
   ```

The resulting APK is under `TMessagesProj_AppStandalone/build/outputs/apk/afat/standalone/app.apk`.

## Implementation summary

- 32 Java/native account slots and removal of the Premium account-count gate.
- Staggered lightweight connections for background accounts and lazy initialization of heavy Java controllers on selection.
- Re-entrancy protection during account switching and sparse-slot-safe notification media handling.
- Frozen accounts are labelled in selectors. Every active account continuously refreshes a small local profile snapshot; if the account is blocked or its session is revoked, its name, phone, username and already cached avatar remain visible under an explicit local-profile status instead of disappearing.
- Separate package, account types, MIME types, shortcuts, broadcast actions, app name and launcher icon.
- Standalone direct-push foreground service for use without Google Play Services.
- arm64-only Pixel/GrapheneOS flavor and project-specific signing configuration.
- Telegram API credentials are read from `local.properties`, with upstream sample credentials retained only as a test fallback.
- The login screen shows actionable messages for rejected test API credentials and GrapheneOS network failures instead of silently returning to the phone-number screen.
- Visible branding is ά‑Gram. The supplied blue-on-white SVG geometry is preserved as Android legacy, adaptive and monochrome launcher resources.
- Optional per-account retention of messages deleted on Telegram in regular private chats, groups and channels.
- Retained messages carry a persistent local-only marker and show “Deleted on server” beside the time.
- Secret chats, self-destructing media, chat auto-delete and ephemeral messages remain excluded from retention.
