# ά‑Gram source overlay

This archive contains the files changed for the ά‑Gram `12.10.1-a-gram.6` APK. It intentionally does not contain Telegram API credentials, a private signing keystore, generated build output, Android SDK, NDK or Gradle caches.

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
3. Copy `local.properties.example` to `local.properties`, set `sdk.dir`, and add private production `TELEGRAM_API_ID` and `TELEGRAM_API_HASH` values obtained from https://my.telegram.org/apps. The build now fails when these values are absent or malformed; there is no public test fallback.
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

- One named local container per Telegram engine account. Existing installations keep their slot and package identity so sessions can migrate in place; no account shares another account's auth state, database, media namespace, account settings or container key.
- Encrypted container metadata with a random container ID and a distinct AES-256-GCM key generated in Android Keystore. Local cards for revoked accounts are encrypted with the same container boundary.
- Offline container creation before Telegram authorization, including an exact preview of the session profile. The selected device profile is locked after successful login.
- Optional per-container PIN and biometric gate on account switching.
- Per-container proxy source of truth and native proxy application; Telegram's legacy proxy screen receives only the selected container's projection.
- Per-container notification privacy: hide identity, show author only, or use full Telegram previews. The secure default is hidden.
- 32 stable Java/native engine slots and removal of the Premium account-count gate. Containers provide isolation and lifecycle management over those bounded engine instances; they do not turn the native engine into an unbounded process pool.
- Staggered lightweight connections for background accounts and lazy initialization of heavy Java controllers on selection.
- Re-entrancy protection during account switching and sparse-slot-safe notification media handling.
- Frozen accounts are labelled in selectors. Every active account continuously refreshes a small local profile snapshot; if the account is blocked or its session is revoked, its name, phone, username and already cached avatar remain visible under an explicit local-profile status instead of disappearing.
- Separate package, account types, MIME types, shortcuts, broadcast actions, app name and launcher icon.
- Standalone direct-push foreground service for use without Google Play Services.
- arm64-only Pixel/GrapheneOS flavor and project-specific signing configuration.
- Telegram API credentials are read from private build settings. Public/test fallback credentials are rejected by the build.
- Minimum Android version is API 26. Sensitive Android backup is disabled, and legacy phone/SMS/call-log, overlay and background-location permissions were removed; self-update permission remains only in the standalone distribution manifest.
- CI checks guard production credential handling, minimum SDK, permissions, backup policy and the retention boundary.
- The login screen shows actionable messages for rejected test API credentials and GrapheneOS network failures instead of silently returning to the phone-number screen.
- Visible branding is ά‑Gram. The supplied blue-on-white SVG geometry is preserved as Android legacy, adaptive and monochrome launcher resources.
- Optional per-account retention of messages deleted on Telegram in regular private chats, groups and channels.
- Retained messages carry a persistent local-only marker, render at 40% opacity and show `{DELETED}` beside the time.
- Secret chats, TTL/self-destruct, view-once, protected and other ephemeral content remain excluded from retention.
