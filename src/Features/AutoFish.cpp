#include "AutoFish.hpp"

#include "../Config.hpp"
#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/patcher.hpp"
#include "../Core/target.hpp"
#include "FishingSpec.hpp"
#include "FeatureManager.hpp"
namespace PF::AutoFish {
namespace {

// ---------------------------------------------------------------------
//  Detour dùng chung. Vì chưa biết chữ ký C# chính xác, ta dùng prototype
//  8 tham số (AArch64: x0..x7) và trả về uintptr_t — x0 sau lời gọi hàm gốc
//  chính là return value. Hàm nào trả void thì x0 là rác (vô hại).
//
//  Khi đã biết chữ ký thật (xem docs/FIND_HOOKS.md), thay bằng detour riêng
//  với prototype chính xác để ép giá trị trả về cho chắc ăn.
// ---------------------------------------------------------------------
using AnyFn = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                            uintptr_t, uintptr_t, uintptr_t, uintptr_t);

AnyFn g_origCheck = nullptr;  // Role::Check  (instant bite)
AnyFn g_origReel = nullptr;   // Role::Reel   (perfect reel)
AnyFn g_origGeneric = nullptr; // Role::Generic (chỉ log)

struct Slot {
    void* target = nullptr;
    const char* label = nullptr;
    bool active = false;
};
Slot g_slots[3];

// --- Role::Check: ép trả true khi bật instant bite (vd: CheckBite) ---
uintptr_t detourCheck(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                      uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    if (g_cfg.instantBite) {
        static uint64_t tick = 0;
        if ((tick++ % 120) == 0) // log thưa thớt để khỏi spam
            PF_LOG("[fish] Check -> forced true (instant bite)");
        return 1; // coi như cá đã cắn
    }
    return g_origCheck ? g_origCheck(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
}

// --- Role::Reel: ép trả "thành công" khi bật perfect reel ---
// success trong game thường là 0 (enum) — ép về 0, giá trị mặc định.
uintptr_t detourReel(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                     uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    uintptr_t ret = g_origReel ? g_origReel(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
    if (g_cfg.perfectReel) {
        static uint64_t tick = 0;
        if ((tick++ % 120) == 0)
            PF_LOG("[fish] Reel -> %lu (perfect reel forced)", (unsigned long)ret);
        return 0; // success
    }
    return ret;
}

// --- Role::Generic: chỉ quan sát ---
uintptr_t detourGeneric(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                        uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    uintptr_t ret = g_origGeneric ? g_origGeneric(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
    static uint64_t tick = 0;
    if ((tick++ % 200) == 0)
        PF_LOG("[fish] generic call self=%p ret=0x%lx", (void*)a0, (unsigned long)ret);
    return ret;
}

void* resolveTarget(const Spec::Method& m) {
    // 1) ưu tiên offset nếu có
    if (m.rva != 0) {
        void* t = rva(m.rva);
        if (t) return t;
    }
    // 2) resolve theo tên
    if (m.klass && m.method) {
        auto ref = Il2Cpp::resolve(m.klass, m.method, m.nsp, m.argc);
        if (ref.fnptr) return ref.fnptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------
//  Feature thật
// ---------------------------------------------------------------------
class AutoFishFeature : public Feature {
public:
    const char* name() const override { return "auto_fish"; }
    const char* title() const override { return "Auto Fishing"; }
    const char* description() const override {
        return "Hook cac ham lien quan ca cua Play Together (Unity IL2CPP)";
    }

    bool install() override {
        if (m_installed) return true;

        int hooked = 0;
        for (const auto& m : Spec::kFishing) {
            void* target = resolveTarget(m);
            if (!target) {
                PF_LOG("[fish] chua tim thay %s::%s (can dien ten/rva trong FishingSpec.hpp)",
                       m.klass, m.method);
                continue;
            }

            int slot = -1;
            void* replacement = nullptr;
            void** origSlot = nullptr;

            switch (m.role) {
                case Spec::Role::Check:
                    if (g_slots[0].active) { PF_LOG("[fish] slot Check đã dùng, bỏ %s", m.method); continue; }
                    slot = 0; replacement = (void*)detourCheck; origSlot = (void**)&g_origCheck; break;
                case Spec::Role::Reel:
                    if (g_slots[1].active) { PF_LOG("[fish] slot Reel đã dùng, bỏ %s", m.method); continue; }
                    slot = 1; replacement = (void*)detourReel; origSlot = (void**)&g_origReel; break;
                default:
                    if (g_slots[2].active) { PF_LOG("[fish] slot Generic đã dùng, bỏ %s", m.method); continue; }
                    slot = 2; replacement = (void*)detourGeneric; origSlot = (void**)&g_origGeneric; break;
            }

            if (PF::hook(m.method, target, replacement, origSlot)) {
                g_slots[slot] = {target, m.method, true};
                ++hooked;
            }
        }

        // Tuỳ chọn: bỏ cooldown qua patch (nếu đã biết RVA)
        if (g_cfg.noCooldown) {
            // TODO: sau khi có offset cooldown, gọi PF::nopRva(...)
            PF_LOG("[fish] noCooldown: chua co offset, chi ghi nhanh");
        }

        m_installed = hooked > 0;
        PF_LOG("[fish] installed=%d hook(s)", hooked);
        return m_installed;
    }

    void uninstall() override {
        for (int i = 0; i < 3; ++i) {
            if (g_slots[i].active && g_slots[i].target) {
                PF::removeHook(g_slots[i].target);
                g_slots[i] = {nullptr, nullptr, false};
            }
        }
        g_origCheck = g_origReel = g_origGeneric = nullptr;
        m_installed = false;
    }

    bool installed() const override { return m_installed; }

private:
    bool m_installed = false;
};

AutoFishFeature g_autoFish;
bool g_registered = false;

} // namespace

void setEnabled(bool on) {
    if (!g_registered) {
        FeatureManager::get().add(&g_autoFish);
        g_registered = true;
    }
    if (on) {
        FeatureManager::get().install("auto_fish");
    } else {
        FeatureManager::get().uninstall("auto_fish");
    }
    g_cfg.autoFish = on;
}

bool enabled() {
    return FeatureManager::get().isInstalled("auto_fish");
}

void install() {
    setEnabled(true);
}

} // namespace PF::AutoFish
