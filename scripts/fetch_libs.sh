#!/usr/bin/env bash
# Tải đúng những gì cần để build PlayFish (không clone full repo, không git).
#
#   libs/imgui   ~1.5 MB  : core ImGui + backend Metal (12 file)
#   libs/Dobby    ~200 KB : header doby.h + libdobby.a prebuilt cho iOS
#
# Dùng cho build local lẫn GitHub Actions.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBS="$ROOT/libs"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

IMGUI_TAG="v1.92.9b"   # Dear ImGui release mới nhất (2026-07-31)
DOBBY_TAG="latest"    # tag chính thức của Dobby (prebuilt arm64 + arm64e)

IMGUI_TARBALL="https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/${IMGUI_TAG}"
DOBBY_TARBALL="https://github.com/jmpews/Dobby/releases/download/${DOBBY_TAG}/dobby-iphoneos-all.tar.gz"

# Chỉ những file thật sự được Makefile compile / include
IMGUI_FILES=(
  "imgui.h" "imgui.cpp"
  "imgui_draw.cpp" "imgui_tables.cpp" "imgui_widgets.cpp"
  "imgui_internal.h" "imconfig.h"
  "imstb_rectpack.h" "imstb_textedit.h" "imstb_truetype.h"
  "backends/imgui_impl_metal.h" "backends/imgui_impl_metal.mm"
  "LICENSE.txt"
)

mkdir -p "$LIBS/imgui/backends" "$LIBS/Dobby/include" "$LIBS/dobby-prebuilt"

# ---------------------------------------------------------------- ImGui
if [ -f "$LIBS/imgui/imgui.cpp" ] && [ -f "$LIBS/imgui/backends/imgui_impl_metal.mm" ]; then
  echo ">> Dear ImGui đã có, bỏ qua"
else
  echo ">> tải Dear ImGui $IMGUI_TAG"
  curl -fsSL "$IMGUI_TARBALL" -o "$TMP/imgui.tar.gz"
  tar xzf "$TMP/imgui.tar.gz" -C "$TMP"
  SRC="$(find "$TMP" -maxdepth 1 -type d -name 'imgui-*' | head -1)"
  for f in "${IMGUI_FILES[@]}"; do
    cp "$SRC/$f" "$LIBS/imgui/$f"
  done
fi

# ---------------------------------------------------------------- Dobby
if [ -f "$LIBS/dobby-prebuilt/build/iphoneos/universal/libdobby.a" ] &&
   [ -f "$LIBS/Dobby/include/dobby.h" ]; then
  echo ">> Dobby đã có, bỏ qua"
else
  echo ">> tải Dobby $DOBBY_TAG (header + libdobby.a prebuilt iOS)"
  curl -fsSL "$DOBBY_TARBALL" -o "$TMP/dobby.tar.gz"
  tar xzf "$TMP/dobby.tar.gz" -C "$TMP"
  # chỉ lấy header public + static lib universal (chứa arm64 + arm64e)
  mkdir -p "$LIBS/dobby-prebuilt/build/iphoneos/universal"
  cp "$TMP/build/iphoneos/dobby.h" "$LIBS/Dobby/include/dobby.h"
  cp "$TMP/build/iphoneos/universal/libdobby.a" \
     "$LIBS/dobby-prebuilt/build/iphoneos/universal/libdobby.a"
fi

echo ">> kết quả:"
du -sh "$LIBS/imgui" "$LIBS/Dobby" "$LIBS/dobby-prebuilt" 2>/dev/null
