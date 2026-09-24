export THEOS ?= /home/tduck/theos

export ARCHS = arm64
# SDK 16.5 để build được cả khi Theos dùng SDK bị lược bớt (CI macOS dùng SDK Xcode).
# Muốn đổi: make TARGET=iphone:clang:latest:17.0
export TARGET = iphone:clang:16.5:14.0
export FINALPACKAGE = 1

include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = PlayFish

# SDK Theos bị thiếu modulemap hoàn chỉnh -> tắt modules (giống project tipar)
PlayFish_USE_MODULES := 0

# ------------------------------------------------------------------ #
#  Vendored libraries — xem scripts/fetch_libs.sh
# ------------------------------------------------------------------ #
DOBBY_DIR    := libs/Dobby
DOBBY_LIB    := libs/dobby-prebuilt/build/iphoneos/universal/libdobby.a
IMGUI_DIR    := libs/imgui

IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp \
              $(IMGUI_DIR)/imgui_draw.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp \
              $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_metal.mm

# ------------------------------------------------------------------ #
#  Sources
# ------------------------------------------------------------------ #
PlayFish_FILES := src/main.mm \
                  src/Core/Offsets.cpp \
                  src/Core/SettingsStore.cpp \
                  src/Core/paths.mm \
                  src/Features/AutoFish.cpp \
                  src/Features/FeatureManager.cpp \
                  src/Features/Discovery.cpp \
                  src/Features/Overrides.cpp \
                  src/Features/FishingAuto.cpp \
                  src/Features/AutoCast.cpp \
                  src/Features/Esp.cpp \
                  src/Features/Watcher.cpp \
                  src/GUI/Gesture.mm \
                  src/GUI/Touch.mm \
                  src/GUI/Gui.mm \
                  src/GUI/Overlay.mm \
                  src/GUI/Menu.cpp \
                  $(IMGUI_SRCS)

PlayFish_CCFLAGS := -std=c++17 -Wno-deprecated-declarations -Wno-unused-function
PlayFish_CFLAGS   := -Isrc \
                     -I$(DOBBY_DIR)/include \
                     -I$(IMGUI_DIR) \
                     -I$(IMGUI_DIR)/backends

PlayFish_FRAMEWORKS := Foundation UIKit Metal MetalKit QuartzCore CoreGraphics
PlayFish_LDFLAGS    := $(DOBBY_LIB)

# install_name phải khớp đường dẫn insert_dylib ghi vào .app
PlayFish_LDFLAGS += -Wl,-install_name,@executable_path/Frameworks/PlayFish.dylib

# ARC cho toàn bộ target (file .cpp của ImGui là C++ thuần, clang chỉ cảnh báo
# "-fobjc-arc unused"; ta tắt warning đó để -Werror không kích hoạt).
PlayFish_CCFLAGS += -fobjc-arc -Wno-unused-command-line-argument

include $(THEOS_MAKE_PATH)/library.mk
