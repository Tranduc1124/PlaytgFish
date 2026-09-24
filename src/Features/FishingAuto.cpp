#include "FishingAuto.hpp"

#include <ctime>

#include "../Config.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"

// =====================================================================
//  LƯU Ý (đã kiểm chứng với dump.cs 2.31.0):
//    [Header("자동낚시 에디터 전용")] public bool AutoFishing;  // 0x88
//    [Header("자동낚시(큰물고기)")]   public bool AutoFishing2; // 0x89
//  Hai field này chỉ xuất hiện ĐÚNG 1 LẦN trong toàn bộ dump (chỗ khai
//  báo) và không có managed code nào đọc tới => đây là cờ editor/debug
//  của nhà phát triển, bật lên không có tác dụng trong bản release.
//
//  Logic câu cá THẬT nằm ở Features/FishingHooks.cpp (hook đúng chữ ký
//  của ActorDefaultControlPlayer.Receive*/CatchResult/HitResult).
//  Module này giữ lại làm công cụ đọc/ghi field theo tên (debug).
// =====================================================================

namespace PF::FishingAuto {
namespace {

// Class và getter singleton (theo tên trong dump.cs)
constexpr const char* kFishingSystemClass = "FishingSystem";
constexpr const char* kSelfGetter = "get_Self";

void* g_class = nullptr;
void* g_selfFn = nullptr; // static FishingSystem get_Self()
std::vector<FlagBinding> g_flags;
bool g_enabled = false;
bool g_setupDone = false;
uint64_t g_lastTick = 0;

// Các field bool của FishingSystem liên quan tới tự động hoá
const char* kWantedFields[] = {
    "AutoFishing",   // auto cast / tự câu
    "AutoFishing2",  // tự lưới cá lớn
    "IsFishing",     // đang câu
    "FishingMiss",   // trượt cá
    "MiniGameUseTimer",
    "RaidResultTest",
    "TestSpecialFishing",
    "blockOtherUpdate",
};

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}

void* selfInstance() {
    if (!g_selfFn) return nullptr;
    // il2cpp sinh prototype có tham số MethodInfo* ẩn ở cuối -> truyền nullptr
    auto fn = reinterpret_cast<void* (*)(void*)>(g_selfFn);
    return fn(nullptr);
}

} // namespace

bool setup() {
    if (g_setupDone) return g_class != nullptr;
    g_setupDone = true;

    if (!Il2Cpp::ready()) {
        PF_LOG("[fishauto] il2cpp chưa sẵn sàng");
        return false;
    }

    g_class = Il2Cpp::findClass(kFishingSystemClass, "");
    if (!g_class) {
        PF_LOG("[fishauto] không tìm thấy class %s", kFishingSystemClass);
        return false;
    }
    PF_LOG("[fishauto] class %s = %p", kFishingSystemClass, g_class);

    // static FishingSystem get_Self()
    auto self = Il2Cpp::resolveByClass(g_class, kSelfGetter, 0);
    g_selfFn = self.fnptr;
    if (!g_selfFn)
        PF_LOG("[fishauto] không tìm thấy %s::%s()", kFishingSystemClass, kSelfGetter);

    // duyệt field của class, lấy offset theo tên
    g_flags.clear();
    for (const auto& f : Il2Cpp::classFields(g_class)) {
        for (const char* want : kWantedFields) {
            if (f.name == want) {
                FlagBinding b;
                b.name = f.name;
                b.offset = f.offset;
                b.exists = f.offset != 0;
                g_flags.push_back(b);
                PF_LOG("[fishauto] field %-20s offset=0x%lx type=%s", f.name.c_str(),
                       (unsigned long)f.offset, f.type.c_str());
                break;
            }
        }
    }
    if (g_flags.empty())
        PF_LOG("[fishauto] không tìm thấy field nào trong danh sách mong muốn");

    return g_class != nullptr;
}

bool ready() {
    return g_class != nullptr;
}

void shutdown() {
    g_enabled = false;
    g_setupDone = false;
    g_class = nullptr;
    g_selfFn = nullptr;
    g_flags.clear();
}

void setEnabled(bool on) {
    if (on && !setup()) {
        PF_LOG("[fishauto] setup thất bại, không bật được");
        return;
    }
    g_enabled = on;
    if (on) tick();
    PF_LOG("[fishauto] %s", on ? "bat" : "tat");
}

bool enabled() {
    return g_enabled;
}

void applyFlag(const std::string& name, bool value) {
    void* self = selfInstance();
    if (!self) return;
    Il2Cpp::writeField<bool>(self, name.c_str(), value);
}

void tick() {
    if (!g_enabled) return;

    const uint64_t t = nowMs();
    if (t - g_lastTick < 200) return; // 5 lần/giây là đủ
    g_lastTick = t;

    void* self = selfInstance();
    if (!self) return; // hệ thống chưa khởi tạo (vào map sau)

    for (const auto& f : g_flags) {
        if (!f.forced || !f.exists) continue;
        // Ghi bằng offset đã cache (khuyến nghị chỉ field instance)
        Il2Cpp::writeFieldAt<bool>(self, f.offset, true);
    }

    if (g_cfg.instantBite) {
        // Nghĩa là cho phép bỏ qua trạng thái "đang chờ" — game đã có cờ riêng,
        // nên chỉ ép khi user bật (mặc định AutoFishing đã đủ)
    }
}

const std::vector<FlagBinding>& flags() {
    return g_flags;
}

void setFlagForced(const std::string& name, bool forced) {
    for (auto& f : g_flags) {
        if (f.name == name) {
            f.forced = forced;
            if (forced) tick();
            return;
        }
    }
}

} // namespace PF::FishingAuto
