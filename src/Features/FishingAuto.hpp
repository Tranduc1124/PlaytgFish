#pragma once

#include <string>
#include <vector>

namespace PF::FishingAuto {

// ---------------------------------------------------------------------
//  Auto-fishing dựa trên field CỐ SẴN CÓ của game (Play Together 2.31):
//    FishingSystem : PT_SystemBase
//      public static FishingSystem get_Self()
//      public bool  AutoFishing   // 0x88  <- cờ auto-cast của game
//      public bool  AutoFishing2  // 0x89  <- cờ auto-lưới cá lớn
//      public bool  IsFishing     // 0x94
//      public bool  FishingMiss   // 0x78
//      public bool  MiniGameUseTimer
//
//  Không cần offset cứng: offset field lấy bằng TÊN lúc runtime
//  (il2cpp_field_get_offset) => đổi version game vẫn chạy.
// ---------------------------------------------------------------------

// Field nào sẽ ép giá trị khi bật
struct FlagBinding {
    std::string name;
    size_t offset = 0;
    bool forced = false; // có đang ép hay không
    bool exists = false;
};

bool setup();   // resolve class + danh sách field (gọi 1 lần)
bool ready();
void shutdown();

// Bật/tắt việc ép field
void setEnabled(bool on);
bool enabled();

// Ép field lên instance hiện tại (gọi mỗi ~0.2s từ render thread)
void tick();

// Danh sách field đã tìm thấy (cho GUI hiển thị)
const std::vector<FlagBinding>& flags();
void setFlagForced(const std::string& name, bool forced);
void applyFlag(const std::string& name, bool value); // ghi 1 field cụ thể (test)

} // namespace PF::FishingAuto
