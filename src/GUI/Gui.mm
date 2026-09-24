#import "Gui.hpp"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "../Config.hpp"
#include "../Core/Offsets.hpp"
#include "../Core/SettingsStore.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/paths.hpp"
#include "../Core/target.hpp"
#include "../Features/AutoCast.hpp"
#include "../Features/Esp.hpp"
#include "Gesture.hpp"
#include "Overlay.hpp"
#include "Touch.hpp"

namespace PF::Gui {
namespace {

bool g_started = false;
unsigned long long g_frames = 0;

// Offset mặc định của các tính năng (0 = chưa biết, điền qua GUI hoặc file)
void registerBuiltinOffsets() {
    auto& reg = OffsetRegistry::get();
    reg.registerBuiltin("fishing.bite_check", "RVA ham kiem tra ca can (ep true)");
    reg.registerBuiltin("fishing.reel_result", "RVA ham ket qua minigame (ep 0 = success)");
    reg.registerBuiltin("fishing.cast_cooldown", "RVA ham tinh cooldown (nop de bo delay)");
    reg.registerBuiltin("fishing.catch_request", "RVA ham gui yeu cauau bat ca len server");
}

} // namespace

void startup() {
    if (g_started) {
        PF_LOG("[gui] đã khởi động rồi");
        return;
    }
    g_started = true;
    PF_LOG("[gui] startup");

    registerBuiltinOffsets();
    OffsetRegistry::get().load();     // đọc file nếu người dùng đã sửa offset
    SettingsStore::load();             // nạp cấu hình đã lưu (vào game là chạy)

    // Tự hook nền: thử liên tục tới khi game load xong metadata.
    // Người dùng không cần bấm nút nào — vào game là tự chạy.
    PF::AutoCast::bootstrap();
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        for (int i = 0; i < 300; ++i) {
            if (Il2Cpp::ready()) {
                if (PF::Esp::setEnabledIfConfigured()) break;
            }
            [NSThread sleepForTimeInterval:0.5];
        }
    });

    // Gesture + touch phải cài trên main thread (UIKit)
    dispatch_async(dispatch_get_main_queue(), ^{
        PF::Gesture::install();
        PF::Touch::install();
        PF::Overlay::install();
    });

    // Nhắc nhở nếu 20s sau vẫn chưa vẽ được frame nào
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(20 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{
                       if (!ready())
                           PF_LOG("[gui] CẢNH BÁO: 20s rồi mà ImGui chưa vẽ được. "
                                  "Kiểm tra game có dùng Metal không (tab Debug).");
                   });
}

void shutdown() {
    if (!g_started) return;
    g_started = false;
    dispatch_async(dispatch_get_main_queue(), ^{
        PF::Overlay::shutdown();
    });
    PF_LOG("[gui] shutdown");
}

bool ready() {
    return PF::Overlay::ready();
}

unsigned long long frameCount() {
    return g_frames;
}

} // namespace PF::Gui

// Cho Overlay đếm số frame đã vẽ (biến nằm trong namespace Gui)
namespace PF::Gui {
void noteFrame() { ++g_frames; }
} // namespace PF::Gui
