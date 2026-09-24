#include "FishingHooks.hpp"

#include "../Config.hpp"
#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"

namespace PF::FishingHooks {
namespace {

constexpr const char* kOwnerClass = "ActorDefaultControlPlayer"; // namespace rỗng

// RVA fallback lấy từ dump.cs 2.31.0 (chỉ dùng khi không resolve được tên)
constexpr uintptr_t kRvaReceiveCastingResult = 0x36DCFD0;
constexpr uintptr_t kRvaReceiveFishingBegin = 0x36DC44C;
constexpr uintptr_t kRvaCatchResult = 0x36DD284;
constexpr uintptr_t kRvaHitResult = 0x36DD0C0;
constexpr uintptr_t kRvaFishingBite = 0x36DD3E0;
constexpr uintptr_t kRvaFishLeave = 0x36DD3F4;

// enum FishingSystem.eHitState (None=0, Hit=1, Fail=2)
constexpr int kHitStateHit = 1;
[[maybe_unused]] constexpr int kHitStateNone = 0;
[[maybe_unused]] constexpr int kHitStateFail = 2;

HookState g_state[static_cast<size_t>(HookId::Count)];
bool g_enabled[static_cast<size_t>(HookId::Count)] = {true, true, true, true, false, false};
bool g_ready = false;
bool g_master = false;
void* g_ownerClass = nullptr;
void* g_biteFn = nullptr; // để tự gọi FishingBite khi bật auto-bite

// ---------------------------------------------------------------------
//  Detour typed — khớp chữ ký C# trong dump.cs
//  self = ActorDefaultControlPlayer*
// ---------------------------------------------------------------------

// private void ReceiveCastingResult(bool castSuccess, uint difficultyLevel)
void detourReceiveCastingResult(void* self, bool castSuccess, uint32_t difficultyLevel) {
    auto& st = g_state[static_cast<size_t>(HookId::ReceiveCastingResult)];
    st.calls++;
    st.lastValue = castSuccess;
    if (g_master && g_enabled[static_cast<size_t>(HookId::ReceiveCastingResult)])
        castSuccess = true; // ép thành công
    reinterpret_cast<void (*)(void*, bool, uint32_t)>(st.target)(self, castSuccess, difficultyLevel);
}

// private void ReceiveFishingBegin(bool castSuccess, uint difficultyLevel,
//                                  bool isRaid, FishingSystem.FishingFailType failType)
void detourReceiveFishingBegin(void* self, bool castSuccess, uint32_t difficultyLevel,
                               bool isRaid, int32_t failType) {
    auto& st = g_state[static_cast<size_t>(HookId::ReceiveFishingBegin)];
    st.calls++;
    st.lastValue = castSuccess;
    if (g_master && g_enabled[static_cast<size_t>(HookId::ReceiveFishingBegin)]) {
        castSuccess = true;
        failType = 0; // không lỗi
    }
    reinterpret_cast<void (*)(void*, bool, uint32_t, bool, int32_t)>(st.target)(
        self, castSuccess, difficultyLevel, isRaid, failType);
}

// private void CatchResult(bool success, uint rewardItemId, int size, FishExtraData options)
// FishExtraData là struct class-reference -> truyền nguyên con trỏ
void detourCatchResult(void* self, bool success, uint32_t rewardItemId, int32_t size, void* options) {
    auto& st = g_state[static_cast<size_t>(HookId::CatchResult)];
    st.calls++;
    st.lastValue = success;
    if (g_master && g_enabled[static_cast<size_t>(HookId::CatchResult)])
        success = true; // ép bắt được
    reinterpret_cast<void (*)(void*, bool, uint32_t, int32_t, void*)>(st.target)(
        self, success, rewardItemId, size, options);
}

// private void HitResult(FishingSystem.eHitState hitState)   None=0 Hit=1 Fail=2
void detourHitResult(void* self, int32_t hitState) {
    auto& st = g_state[static_cast<size_t>(HookId::HitResult)];
    st.calls++;
    st.lastValue = (hitState == kHitStateHit);
    if (g_master && g_enabled[static_cast<size_t>(HookId::HitResult)])
        hitState = kHitStateHit; // luôn trúng
    reinterpret_cast<void (*)(void*, int32_t)>(st.target)(self, hitState);
}

// public override void FishingBite()  -> dùng để tự kích hoạt cắn câu
void detourFishingBite(void* self) {
    auto& st = g_state[static_cast<size_t>(HookId::FishingBite)];
    st.calls++;
    reinterpret_cast<void (*)(void*)>(st.target)(self);
}

// public override void FishLeave()  -> chỉ quan sát
void detourFishLeave(void* self) {
    auto& st = g_state[static_cast<size_t>(HookId::FishLeave)];
    st.calls++;
    reinterpret_cast<void (*)(void*)>(st.target)(self);
}

// Bảng mô tả hook: tên method trong dump + RVA fallback + detour typed
struct Def {
    HookId id;
    const char* method;
    int argc;
    uintptr_t rva;
    void* detour;
};

const Def kDefs[] = {
    {HookId::ReceiveCastingResult, "ReceiveCastingResult", 2, kRvaReceiveCastingResult,
     (void*)detourReceiveCastingResult},
    {HookId::ReceiveFishingBegin, "ReceiveFishingBegin", 4, kRvaReceiveFishingBegin,
     (void*)detourReceiveFishingBegin},
    {HookId::CatchResult, "CatchResult", 4, kRvaCatchResult, (void*)detourCatchResult},
    {HookId::HitResult, "HitResult", 1, kRvaHitResult, (void*)detourHitResult},
    {HookId::FishingBite, "FishingBite", 0, kRvaFishingBite, (void*)detourFishingBite},
    {HookId::FishLeave, "FishLeave", 0, kRvaFishLeave, (void*)detourFishLeave},
};

} // namespace

bool setup() {
    if (g_ready) return true;
    if (!Il2Cpp::ready()) {
        PF_LOG("[fishhooks] il2cpp chưa sẵn sàng");
        return false;
    }

    g_ownerClass = Il2Cpp::findClass(kOwnerClass, "");
    if (!g_ownerClass) {
        PF_LOG("[fishhooks] không tìm thấy class %s", kOwnerClass);
        return false;
    }
    PF_LOG("[fishhooks] class %s = %p", kOwnerClass, g_ownerClass);

    for (const Def& d : kDefs) {
        HookState& st = g_state[static_cast<size_t>(d.id)];

        // 1) ưu tiên resolve theo tên (tự đổi theo version)
        void* target = Il2Cpp::resolveByClass(g_ownerClass, d.method, d.argc).fnptr;

        // 2) fallback: RVA từ dump.cs
        if (!target && d.rva)
            target = PF::rva(d.rva);

        if (!target) {
            PF_LOG("[fishhooks] %s: không tìm thấy (tên lẫn RVA)", d.method);
            continue;
        }

        if (PF::hook(d.method, target, d.detour, &st.target)) {
            st.installed = true;
            if (d.id == HookId::FishingBite) g_biteFn = target;
        }
    }

    g_ready = true;
    size_t ok = 0;
    for (const auto& s : g_state)
        if (s.installed) ++ok;
    PF_LOG("[fishhooks] cài %zu/%zu hook", ok, static_cast<size_t>(HookId::Count));
    return ok > 0;
}

bool ready() {
    return g_ready;
}

void shutdown() {
    for (auto& s : g_state) {
        if (s.installed && s.target) {
            PF::removeHook(s.target);
            s = HookState{};
        }
    }
    g_ready = false;
    g_biteFn = nullptr;
}

void setEnabled(bool on) {
    if (on && !setup()) return;
    g_master = on;
    PF_LOG("[fishhooks] %s", on ? "bat" : "tat");
}

bool enabled() {
    return g_master;
}

const HookState& state(HookId id) {
    return g_state[static_cast<size_t>(id)];
}

const char* name(HookId id) {
    for (const Def& d : kDefs)
        if (d.id == id) return d.method;
    return "?";
}

void setHookEnabled(HookId id, bool on) {
    g_enabled[static_cast<size_t>(id)] = on;
}

bool hookEnabled(HookId id) {
    return g_enabled[static_cast<size_t>(id)];
}

} // namespace PF::FishingHooks
