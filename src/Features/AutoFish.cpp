#include "AutoFish.hpp"

#include <cstring>
#include <vector>

#include "../Config.hpp"
#include "../Core/Offsets.hpp"
#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/patcher.hpp"
#include "../Core/target.hpp"
#include "Discovery.hpp"
#include "FeatureManager.hpp"
#include "Overrides.hpp"

namespace PF::AutoFish {
namespace {

bool g_enabled = false;
std::vector<void*> g_overrideTargets; // các target đã override (để gỡ khi tắt)
bool g_cooldownPatched = false;

// Tìm ứng viên đầu tiên có native pointer, ưu tiên điểm cao
const Discovery::Candidate* bestCandidate(const std::vector<Discovery::Candidate>& list,
                                          const char* role) {
    for (const auto& c : list) {
        if (role && c.role && strcmp(c.role, role) == 0)
            return &c;
    }
    return list.empty() ? nullptr : &list.front();
}

// Cài override theo offset đã biết (nếu có), ngược lại dùng discovery
bool installBiteOverride() {
    // 1) offset thủ công trong registry
    const uintptr_t rva = OffsetRegistry::get().get("fishing.bite_check");
    if (rva) {
        void* target = PF::rva(rva);
        if (target && Overrides::add(target, Overrides::Mode::ForceTrue, 1, "bite_check(rva)")) {
            g_overrideTargets.push_back(target);
            return true;
        }
    }
    // 2) tự tìm qua metadata
    auto list = Discovery::scan(Discovery::kClassKeywords, 400);
    const auto* c = bestCandidate(list, "bite");
    if (!c || !c->fnptr) return false;
    if (Overrides::add(c->fnptr, Overrides::Mode::ForceTrue, 1,
                       c->klass + "::" + c->method)) {
        g_overrideTargets.push_back(c->fnptr);
        return true;
    }
    return false;
}

bool installReelOverride() {
    const uintptr_t rva = OffsetRegistry::get().get("fishing.reel_result");
    if (rva) {
        void* target = PF::rva(rva);
        if (target && Overrides::add(target, Overrides::Mode::ForceFalse, 0, "reel_result(rva)")) {
            g_overrideTargets.push_back(target);
            return true;
        }
    }
    auto list = Discovery::scan(Discovery::kClassKeywords, 400);
    const auto* c = bestCandidate(list, "result");
    if (!c || !c->fnptr) return false;
    if (Overrides::add(c->fnptr, Overrides::Mode::ForceFalse, 0,
                       c->klass + "::" + c->method)) {
        g_overrideTargets.push_back(c->fnptr);
        return true;
    }
    return false;
}

void installCooldownPatch() {
    const uintptr_t rva = OffsetRegistry::get().get("fishing.cast_cooldown");
    if (!rva) {
        PF_LOG("[fish] noCooldown: chưa có offset fishing.cast_cooldown");
        return;
    }
    if (PF::nopRva(rva, 1)) {
        g_cooldownPatched = true;
        PF_LOG("[fish] noCooldown: đã nop cooldown @ rva 0x%lx", (unsigned long)rva);
    }
}

void uninstallCooldownPatch() {
    if (g_cooldownPatched) {
        restoreAllPatches();
        g_cooldownPatched = false;
    }
}

} // namespace

void setEnabled(bool on) {
    if (on == g_enabled) return;
    g_enabled = on;
    if (on) {
        install();
    } else {
        uninstall();
    }
}

bool enabled() {
    return g_enabled;
}

void install() {
    PF_LOG("[fish] cài đặt...");

    bool ok = false;
    if (g_cfg.instantBite)
        ok = installBiteOverride() || ok;
    if (g_cfg.perfectReel)
        ok = installReelOverride() || ok;
    if (g_cfg.noCooldown)
        installCooldownPatch();

    PF_LOG("[fish] hoàn tất (override=%zu, cooldown_patch=%s)",
           g_overrideTargets.size(), g_cooldownPatched ? "có" : "không");
    (void)ok;
}

void uninstall() {
    for (void* t : g_overrideTargets) {
        PF::removeHook(t);
    }
    g_overrideTargets.clear();
    uninstallCooldownPatch();
    PF_LOG("[fish] đã gỡ toàn bộ hook/patch của auto-fish");
}

} // namespace PF::AutoFish
