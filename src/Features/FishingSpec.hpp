#pragma once

#include <cstdint>

// =====================================================================
//  BẢNG ĐỊNH NGHĨA HÀM CẦN HOOK  —  điền sau khi Il2CppDumper
//
//  Ưu tiên resolve theo TÊN (không cần offset, đổi version vẫn chạy).
//  Nếu hàm bị compiler inline / không export thì mới điền `rva`
//  (offset trong RAM = method address - image base của UnityFramework).
//
//  Cách tìm nhanh: grep -i "fish" dump.cs  ->  xem class/method trong
//  class đó rồi điền vào đây.
// =====================================================================

namespace Spec {

enum class Role {
    Check,   // hàm kiểm tra trạng thái (vd: cá đã cắn chưa) -> ép trả true
    Reel,    // hàm xử lý minigame/kết quả -> chỉ quan sát, ép sau khi biết signature
    Generic, // hàm liên quan khác -> chỉ log
};

struct Method {
    Role role;
    const char* klass;    // tên class trong IL2CPP
    const char* nsp;      // namespace ("" nếu global)
    const char* method;   // tên method
    int argc;             // số tham số (dùng cho _get_method_from_name)
    uintptr_t rva;        // 0 = chưa biết, sẽ resolve theo tên
    const char* note;
};

// ---- Bảng chính: sửa ở đây ----
inline constexpr Method kFishing[] = {
    {Role::Check,  "FishingManager", "", "CheckBite",      0, 0x0, "kiểm tra cá cắn"},
    {Role::Reel,   "FishingManager", "", "ReelResult",     1, 0x0, "kết quả minigame"},
    {Role::Generic, "FishingManager", "", "Cast",           0, 0x0, "thả câu"},
};

// ---- Bảng phụ: struct field offset (đọc/ghi trực tiếp) ----
namespace FishData {
// offset của field trong struct, ví dụ rarity/level
inline constexpr uintptr_t kRarity = 0x0;
inline constexpr uintptr_t kLevel = 0x0;
} // namespace FishData

} // namespace Spec
