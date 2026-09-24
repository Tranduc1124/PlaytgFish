#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include <mach-o/dyld.h>
#include <unistd.h>

namespace PF {

// Unity IL2CPP: code game nằm trong dylib này (Payload/*.app/Frameworks/UnityFramework)
inline constexpr const char* kGameImage = "UnityFramework";
// App executable — lấy từ CFBundleExecutable trong Info.plist của IPA bản VNG
inline constexpr const char* kAppImage = "PLAYTOGETHERVNG";

// Load address của image (Mach-O header == image base)
inline uintptr_t imageBase(const char* imageName) {
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char* path = _dyld_get_image_name(i);
        if (!path) continue;
        const char* slash = strrchr(path, '/');
        std::string name = slash ? (slash + 1) : path;
        if (name == imageName)
            return reinterpret_cast<uintptr_t>(_dyld_get_image_header(i));
    }
    return 0;
}

// Chờ image load xong (game load UnityFramework sau vài giây)
inline bool waitForImage(const char* name, int timeoutMs = 60000) {
    for (int waited = 0; waited < timeoutMs; waited += 100) {
        if (imageBase(name) != 0) return true;
        usleep(100 * 1000);
    }
    return false;
}

// base + RVA
inline void* rva(uintptr_t offset, const char* image = kGameImage) {
    const uintptr_t base = imageBase(image);
    if (!base || !offset) return nullptr;
    return reinterpret_cast<void*>(base + offset);
}

} // namespace PF

// =====================================================================
//  OFFSETS — điền sau khi chạy Il2CppDumper trên UnityFramework
//  Đây là RVA (offset từ base UnityFramework trong RAM), KHÔNG phải
//  RVA trong file. Chuyển đổi: xem README mục "Offsets workflow".
// =====================================================================
namespace Offsets {

namespace Fishing {
constexpr uintptr_t kBiteCheck     = 0x0; // TODO: hàm check cá cắn
constexpr uintptr_t kBiteSuccess   = 0x0; // TODO: hàm xử lý cá cắn
constexpr uintptr_t kReelResult    = 0x0; // TODO: hàm kết quả minigame re-el
constexpr uintptr_t kCastCooldown  = 0x0; // TODO: hàm tính cooldown câu
} // namespace Fishing

namespace FishData {
constexpr uintptr_t kRarityOffset = 0x0; // TODO: field offset trong struct Fish
} // namespace FishData

} // namespace Offsets
