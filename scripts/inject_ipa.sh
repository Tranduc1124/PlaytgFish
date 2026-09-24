#!/usr/bin/env bash
# Chèn PlayFish.dylib vào IPA rồi ký lại (sideload, không cần jailbreak).
#
#   ./scripts/inject_ipa.sh <input.ipa> [output.ipa] [cert.p12] [profile.mobileprovision]
#
# Yêu cầu (cài trong WSL hoặc dùng bản Windows):
#   - insert_dylib   : https://github.com/Tyilo/insert_dylib (bản macOS)
#                     hoặc dùng `optool insert -c <dylib> -p <path> -t <binary>`
#   - zsign          : https://github.com/zhlynn/zsign
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IPA_IN="${1:-}"
IPA_OUT="${2:-${IPA_IN%.ipa}-patched.ipa}"
CERT="${3:-}"
PROFILE="${4:-}"

if [ -z "$IPA_IN" ] || [ ! -f "$IPA_IN" ]; then
    echo "usage: $0 <input.ipa> [output.ipa] [cert.p12] [profile.mobileprovision]"
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

DYLIB="$ROOT/.theos/obj/PlayFish.dylib"
if [ ! -f "$DYLIB" ]; then
    echo "!! chưa build dylib. Chạy: make   (trong $ROOT)"
    exit 1
fi

echo ">> giải nén IPA"
unzip -q "$IPA_IN" -d "$WORK"

APP_DIR="$(find "$WORK/Payload" -maxdepth 1 -name '*.app' | head -1)"
if [ -z "$APP_DIR" ]; then
    echo "!! không tìm thấy .app trong Payload/"
    exit 1
fi
echo ">> app: $(basename "$APP_DIR")"

BIN_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP_DIR/Info.plist" 2>/dev/null \
            || plutil -extract CFBundleExecutable raw "$APP_DIR/Info.plist")"
BIN="$APP_DIR/$BIN_NAME"
echo ">> binary: $BIN_NAME"

# ------------------------------------------------------------- chèn dylib
mkdir -p "$APP_DIR/Frameworks"
cp "$DYLIB" "$APP_DIR/Frameworks/PlayFish.dylib"
chmod 755 "$APP_DIR/Frameworks/PlayFish.dylib"

if command -v insert_dylib >/dev/null 2>&1; then
    echo ">> insert_dylib @executable_path/Frameworks/PlayFish.dylib"
    insert_dylib --strip-codesig --all-architectures \
        "@executable_path/Frameworks/PlayFish.dylib" \
        "$BIN" "$BIN.new"
    mv "$BIN.new" "$BIN"
elif command -v optool >/dev/null 2>&1; then
    echo ">> optool insert"
    optool insert -c "$APP_DIR/Frameworks/PlayFish.dylib" \
        -p "@executable_path/Frameworks/PlayFish.dylib" -t "$BIN"
else
    echo "!! chưa có insert_dylib hoặc optool — cài 1 trong 2 tool rồi chạy lại"
    exit 1
fi

chmod 755 "$BIN"
rm -rf "$APP_DIR/_CodeSignature"

# ------------------------------------------------------------- ký lại
if [ -n "$CERT" ] && [ -n "$PROFILE" ]; then
    if command -v zsign >/dev/null 2>&1; then
        echo ">> zsign ký lại"
        (cd "$WORK" && zip -qr "$IPA_OUT" Payload)
        zsign -k "$CERT" -m "$PROFILE" -o "$IPA_OUT" "$IPA_OUT"
    else
        echo "!! chưa có zsign — IPA chưa được ký, hãy ký tay hoặc cài zsign"
        (cd "$WORK" && zip -qr "$IPA_OUT" Payload)
    fi
else
    echo ">> không có cert/profile, đóng gói IPA (bạn tự ký sau)"
    (cd "$WORK" && zip -qr "$IPA_OUT" Payload)
fi

echo ">> xong: $IPA_OUT"
