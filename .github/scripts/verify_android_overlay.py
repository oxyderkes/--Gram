from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
ANDROID = ROOT / "android"
errors: list[str] = []


def read(relative: str) -> str:
    path = ANDROID / relative
    if not path.is_file():
        errors.append(f"missing required overlay file: android/{relative}")
        return ""
    return path.read_text(encoding="utf-8")


core_gradle = read("TMessagesProj/build.gradle")
standalone_gradle = read("TMessagesProj_AppStandalone/build.gradle")
main_manifest = read("TMessagesProj/src/main/AndroidManifest.xml")
standalone_manifest = read("TMessagesProj/config/release/AndroidManifest_standalone.xml")
message_object = read("TMessagesProj/src/main/java/org/telegram/messenger/MessageObject.java")

for label, source in (("core", core_gradle), ("standalone", standalone_gradle)):
    if not re.search(r"minSdkVersion\s+26\b", source):
        errors.append(f"{label} module must keep minSdkVersion 26")

if not re.search(r'buildConfigField\s+"int",\s*"TELEGRAM_APP_ID",\s*configuredApiId\s*\?\s*configuredApiId\s*:\s*"0"', core_gradle):
    errors.append("production API ID must not have a public fallback")
if 'configuredApiHash ? configuredApiHash : ""' not in core_gradle:
    errors.append("production API hash must not have a public fallback")
if "checkAgramApiCredentials" not in core_gradle:
    errors.append("mandatory production credential check is missing")

for label, manifest in (("main", main_manifest), ("standalone", standalone_manifest)):
    if 'android:allowBackup="false"' not in manifest or 'android:fullBackupContent="false"' not in manifest:
        errors.append(f"{label} manifest must disable Android backup")

banned_permissions = (
    "android.permission.READ_PHONE_STATE",
    "android.permission.READ_PHONE_NUMBERS",
    "android.permission.READ_CALL_LOG",
    "android.permission.SEND_SMS",
    "android.permission.SYSTEM_ALERT_WINDOW",
    "android.permission.ACCESS_BACKGROUND_LOCATION",
)
for permission in banned_permissions:
    if permission in main_manifest or permission in standalone_manifest:
        errors.append(f"high-risk permission returned: {permission}")

required_retention_guards = (
    "!DialogObject.isEncryptedDialog(dialogId)",
    "message.ttl == 0",
    "message.ttl_period == 0",
    "!isSecretMedia(message)",
    "!isEphemeral(message)",
)
for guard in required_retention_guards:
    if guard not in message_object:
        errors.append(f"ordinary-message retention safety guard is missing: {guard}")

tracked_text = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for path in ANDROID.rglob("*")
    if path.is_file() and path.suffix.lower() in {".java", ".xml", ".gradle", ".properties", ".md", ".yml", ".yaml"}
)
if re.search(r"(?i)TELEGRAM_API_HASH\s*[=:]\s*[\"']?[0-9a-f]{32}[\"']?", tracked_text):
    errors.append("a concrete Telegram API hash appears in the public overlay")
if re.search(r"(?i)(storePassword|keyPassword)\s*[=:]\s*[\"']?(?!<|\$|System\.getenv)[^\s\"']{6,}", tracked_text):
    errors.append("a signing password may be hard-coded in the public overlay")
if re.search(r"(?im)^RELEASE_(?:STORE|KEY)_PASSWORD\s*=\s*\S+", tracked_text):
    errors.append("a signing password property appears in the public overlay")

if errors:
    print("Agram overlay security verification failed:")
    for error in errors:
        print(f"- {error}")
    sys.exit(1)

print("Agram Android overlay security verification passed.")
