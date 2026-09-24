#include "AutoCast.hpp"

#include <atomic>
#include <ctime>
#include <pthread.h>
#include <unistd.h>

#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/target.hpp"
#include "Esp.hpp"

namespace PF::AutoCast {
namespace {

constexpr const char* kControlClass = "ActorDefaultControlPlayer";
constexpr const char* kSystemClass = "FishingSystem";
constexpr const char* kFloatClass = "FishingFloatController";

Settings g_cfg;
Status g_status;
std::atomic<bool> g_bootstrapRunning{false};
bool g_hooksInstalled = false;

// RVA fallback (dump 2.31.0)
constexpr uintptr_t kRvaUpdateFishingState = 0x36D91BC;
constexpr uintptr_t kRvaCatchResult = 0x36DD284;
constexpr uintptr_t kRvaHitResult = 0x36DD0C0;
constexpr uintptr_t kRvaFishingBite = 0x36DD3E0;
constexpr uintptr_t kRvaReceiveCastingResult = 0x36DCFD0;
constexpr uintptr_t kRvaReceiveFishingBegin = 0x36DC44C;

void* g_controlClass = nullptr;
void* g_systemClass = nullptr;
void* g_floatClass = nullptr;
void* g_selfInstance = nullptr;

// prototype gọi trực tiếp
void* (*f_self)(void*) = nullptr;                    // static get_Self()
bool (*f_startFishing)(void*) = nullptr;             // StartFishing()
void (*f_onClickFishing)(void*) = nullptr;           // OnClickFishing()
void (*f_setFishHp)(void*, int) = nullptr;           // SetFishHP(int)
void (*f_requestTug)(void*, void*) = nullptr;        // RequestFishingTug(Action)
void (*f_requestStunHit)(void*, void*) = nullptr;    // RequestStunHit(Action)
void (*f_fishCancel)(void*) = nullptr;               // FishingCancel()
bool (*f_shadowReady)(void*) = nullptr;              // get_IsShadowFishReady()

// trampoline gốc
void* o_updateState = nullptr;
void* o_fishingBite = nullptr;
void* o_catchResult = nullptr;
void* o_tugResult = nullptr;
void* o_stunHit = nullptr;
void* o_lift = nullptr;
void* o_castingResult = nullptr;
void* o_fishingBegin = nullptr;
void* o_hitResult = nullptr;
void* o_floatUpdate = nullptr;

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}
uint64_t g_lastCast = 0, g_lastTug = 0, g_lastBite = 0, g_lastBig = 0, g_lastHp = 0;

// ---------------------------------------------------------------------
//  Delegate giả cho Action<> (IL2CPP): offset 0x10 là con trỏ hàm invoke
// ---------------------------------------------------------------------
struct FakeDelegate {
    void* klass;
    void* monitor;
    void* invoke;
    void* target;
    void* method;
    void* trampoline;
    void* extra;
    void* interpMethod;
    void* interpInvoke;
    void* methodInfo;
};
FakeDelegate g_tugDelegate{};
FakeDelegate g_stunDelegate{};

// Action<bool,bool,int,bool>
void tugResultCallback(bool success, bool isStun, int damage, bool isCritical) {
    g_status.tugs++;
    bool s = success;
    int dmg = damage;
    if (g_cfg.enabled) {
        if (g_status.isBigFish) {
            if (g_cfg.bigForceSuccess) { s = true; dmg = 9999; isCritical = true; }
        } else if (g_cfg.autoTug && g_cfg.forceTugSuccess) {
            s = true;
            dmg = 9999;
            isCritical = true;
        }
    }
    using Fn = void (*)(bool, bool, int, bool);
    reinterpret_cast<Fn>(g_tugDelegate.invoke)(s, isStun, dmg, isCritical);
}

// Action<bool>
void stunHitCallback(bool success) {
    g_status.stuns++;
    bool s = success;
    if (g_cfg.enabled) {
        if (g_status.isBigFish ? g_cfg.bigForceSuccess : g_cfg.forceStun) s = true;
    }
    using Fn = void (*)(bool);
    reinterpret_cast<Fn>(g_stunDelegate.invoke)(s);
}

// ---------------------------------------------------------------------
//  Detour typed (khớp dump.cs)
// ---------------------------------------------------------------------
bool detourUpdateFishingState(void* self, int state) {
    g_status.control = self;
    g_status.currentState = state;
    g_status.isBigFish = state >= kBigFishThreshold;
    return reinterpret_cast<bool (*)(void*, int)>(o_updateState)(self, state);
}

void detourFishingBite(void* self) {
    g_status.bites++;
    reinterpret_cast<void (*)(void*)>(o_fishingBite)(self);
}

void detourReceiveFishingTug(void* self, bool success, bool isStun, int32_t damage, bool isCritical) {
    if (g_cfg.enabled) {
        if (g_status.isBigFish && g_cfg.bigForceSuccess) {
            success = true;
            damage = 9999;
            isCritical = true;
        } else if (!g_status.isBigFish && g_cfg.autoTug && g_cfg.forceTugSuccess) {
            success = true;
            damage = 9999;
            isCritical = true;
        }
    }
    reinterpret_cast<void (*)(void*, bool, bool, int32_t, bool)>(o_tugResult)(
        self, success, isStun, damage, isCritical);
}

void detourReceiveStunHit(void* self, bool success) {
    if (g_cfg.enabled) {
        if (g_status.isBigFish ? (g_cfg.bigAutoStun && g_cfg.bigForceSuccess) : g_cfg.forceStun)
            success = true;
    }
    reinterpret_cast<void (*)(void*, bool)>(o_stunHit)(self, success);
}

void detourLift(void* self, bool success) {
    if (g_cfg.enabled) {
        if (g_status.isBigFish ? g_cfg.bigForceSuccess : g_cfg.forceLift) success = true;
    }
    reinterpret_cast<void (*)(void*, bool)>(o_lift)(self, success);
}

void detourCatchResult(void* self, bool success, uint32_t rewardItemId, int32_t size, void* options) {
    if (g_cfg.enabled) {
        if (g_status.isBigFish ? g_cfg.bigForceSuccess : g_cfg.forceCatch) {
            success = true;
            g_status.catches++;
        }
    }
    reinterpret_cast<void (*)(void*, bool, uint32_t, int32_t, void*)>(o_catchResult)(
        self, success, rewardItemId, size, options);
}

void detourReceiveCastingResult(void* self, bool castSuccess, uint32_t difficultyLevel) {
    if (g_cfg.enabled && g_cfg.forceCastSuccess) {
        castSuccess = true;
        g_status.serverRejected = false;
    } else if (!castSuccess) {
        g_status.serverRejected = true;
    }
    reinterpret_cast<void (*)(void*, bool, uint32_t)>(o_castingResult)(self, castSuccess, difficultyLevel);
}

void detourReceiveFishingBegin(void* self, bool castSuccess, uint32_t difficultyLevel,
                               bool isRaid, int32_t failType) {
    if (g_cfg.enabled && g_cfg.forceCastSuccess) {
        castSuccess = true;
        failType = 0;
    }
    reinterpret_cast<void (*)(void*, bool, uint32_t, bool, int32_t)>(o_fishingBegin)(
        self, castSuccess, difficultyLevel, isRaid, failType);
}

void detourHitResult(void* self, int32_t hitState) {
    if (g_cfg.enabled) {
        const int st = g_status.currentState;
        const bool inFight = (st == (int)eFishingState::Hit || st == (int)eFishingState::Fighting ||
                              st == (int)eFishingState::BigFish_Fighting ||
                              st == (int)eFishingState::BigFish_Tug);
        if (inFight) hitState = 1; // eHitState.Hit
    }
    reinterpret_cast<void (*)(void*, int32_t)>(o_hitResult)(self, hitState);
}

// FishingFloatController.Update() -> dùng để phát hiện bóng cá sẵn sàng
void detourFloatUpdate(void* self) {
    if (g_cfg.enabled && g_status.isBigFish && f_shadowReady) {
        if (f_shadowReady(self)) g_status.bigPumpin++; // bóng cá đã sẵn sàng
    }
    reinterpret_cast<void (*)(void*)>(o_floatUpdate)(self);
}

struct HookDef {
    const char* method;
    int argc;
    uintptr_t rva;
    void* detour;
    void** orig;
};

bool installHooks() {
    const HookDef defs[] = {
        {"UpdateFishingState", 1, kRvaUpdateFishingState, (void*)detourUpdateFishingState, &o_updateState},
        {"FishingBite", 0, kRvaFishingBite, (void*)detourFishingBite, &o_fishingBite},
        {"ReceiveFishingTug", 4, 0, (void*)detourReceiveFishingTug, &o_tugResult},
        {"ReceiveStunHit", 1, 0, (void*)detourReceiveStunHit, &o_stunHit},
        {"Lift", 1, 0, (void*)detourLift, &o_lift},
        {"CatchResult", 4, kRvaCatchResult, (void*)detourCatchResult, &o_catchResult},
        {"ReceiveCastingResult", 2, kRvaReceiveCastingResult, (void*)detourReceiveCastingResult, &o_castingResult},
        {"ReceiveFishingBegin", 4, kRvaReceiveFishingBegin, (void*)detourReceiveFishingBegin, &o_fishingBegin},
        {"HitResult", 1, kRvaHitResult, (void*)detourHitResult, &o_hitResult},
    };

    size_t ok = 0;
    for (const auto& d : defs) {
        void* target = Il2Cpp::resolveByClass(g_controlClass, d.method, d.argc).fnptr;
        if (!target && d.rva) target = PF::rva(d.rva);
        if (!target) {
            PF_LOG("[autocast] chưa thấy %s", d.method);
            continue;
        }
        if (PF::hook(d.method, target, d.detour, d.orig)) ++ok;
    }
    g_status.controlResolved = ok > 0;
    PF_LOG("[autocast] hook %zu/%zu hàm của %s", ok, sizeof(defs) / sizeof(defs[0]), kControlClass);
    return ok > 0;
}

void resolveAll() {
    g_tugDelegate.invoke = (void*)tugResultCallback;
    g_stunDelegate.invoke = (void*)stunHitCallback;

    if (!g_controlClass) g_controlClass = Il2Cpp::findClass(kControlClass, "");
    if (!g_systemClass) g_systemClass = Il2Cpp::findClass(kSystemClass, "");
    if (!g_floatClass) g_floatClass = Il2Cpp::findClass(kFloatClass, "");

    if (g_systemClass) {
        g_status.systemResolved = true;
        f_self = reinterpret_cast<void* (*)(void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "get_Self", 0).fnptr);
        f_setFishHp = reinterpret_cast<void (*)(void*, int)>(
            Il2Cpp::resolveByClass(g_systemClass, "SetFishHP", 1).fnptr);
        f_requestTug = reinterpret_cast<void (*)(void*, void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "RequestFishingTug", 1).fnptr);
        f_requestStunHit = reinterpret_cast<void (*)(void*, void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "RequestStunHit", 1).fnptr);
    }
    if (!g_controlClass) return;

    f_startFishing = reinterpret_cast<bool (*)(void*)>(
        Il2Cpp::resolveByClass(g_controlClass, "StartFishing", 0).fnptr);
    f_onClickFishing = reinterpret_cast<void (*)(void*)>(
        Il2Cpp::resolveByClass(g_controlClass, "OnClickFishing", 0).fnptr);
    f_fishCancel = reinterpret_cast<void (*)(void*)>(
        Il2Cpp::resolveByClass(g_controlClass, "FishingCancel", 0).fnptr);

    if (!g_hooksInstalled && installHooks()) {
        g_hooksInstalled = true;
        snprintf(g_status.note, sizeof(g_status.note), "hook xong, vao game la chay");
    }

    if (g_floatClass && !o_floatUpdate) {
        auto up = Il2Cpp::resolveByClass(g_floatClass, "Update", 0);
        f_shadowReady = reinterpret_cast<bool (*)(void*)>(
            Il2Cpp::resolveByClass(g_floatClass, "get_IsShadowFishReady", 0).fnptr);
        if (up.fnptr) PF::hook("FishingFloatController::Update", up.fnptr,
                               (void*)detourFloatUpdate, &o_floatUpdate);
    }
}

void* bootstrapThread(void* /*arg*/) {
    // Thử liên tục cho tới khi game load xong metadata
    for (int i = 0; i < 600; ++i) { // ~5 phút
        if (!Il2Cpp::ready()) {
            Il2Cpp::init();
        } else if (!g_hooksInstalled) {
            resolveAll();
            if (g_hooksInstalled) break;
        } else {
            break;
        }
        usleep(500 * 1000);
    }
    g_bootstrapRunning = false;

    // ESP cũng do cùng luồng này lo (hàm tự trả về true ngay nếu không cần bật)
    // — tránh hai luồng cùng DobbyHook một địa chỉ.
    for (int i = 0; i < 20 && !Esp::setEnabledIfConfigured(); ++i)
        usleep(500 * 1000);

    return nullptr;
}

} // namespace

Settings& settings() {
    return g_cfg;
}

const Status& status() {
    return g_status;
}

bool ready() {
    return g_hooksInstalled;
}

void shutdown() {
    g_hooksInstalled = false;
    g_controlClass = nullptr;
    g_systemClass = nullptr;
    g_floatClass = nullptr;
    g_status = Status{};
}

void bootstrap() {
    if (g_bootstrapRunning) return;
    g_bootstrapRunning = true;
    pthread_t th;
    if (pthread_create(&th, nullptr, bootstrapThread, nullptr) == 0)
        pthread_detach(th);
}

void tick() {
    if (!g_cfg.enabled) return;

    // nếu chưa hook xong thì thử lại (game vừa vào map)
    if (!g_hooksInstalled) {
        if (Il2Cpp::ready()) resolveAll();
        return;
    }

    if (f_self) g_selfInstance = f_self(nullptr);
    void* control = g_status.control;
    if (!control) return;

    const uint64_t t = nowMs();
    const int st = g_status.currentState;
    const bool big = st >= kBigFishThreshold;
    g_status.isBigFish = big;

    // ================= NHÁNH B: CÁ LỚN / BÓNG CÁ =================
    if (big) {
        // RaidEnter/RaidSync/Begin: chuẩn bị, ép HP về 0 để kết thúc nhanh
        if (st == (int)eFishingState::BigFish_RaidEnter ||
            st == (int)eFishingState::BigFish_RaidSync ||
            st == (int)eFishingState::BigFish_Begin ||
            st == (int)eFishingState::BigFish_Pumpin ||
            st == (int)eFishingState::BigFish_Drag ||
            st == (int)eFishingState::BigFish_Tug ||
            st == (int)eFishingState::BigFish_Fighting) {

            if (g_cfg.bigZeroHp && f_setFishHp && g_selfInstance && t - g_lastHp > 200) {
                g_lastHp = t;
                f_setFishHp(g_selfInstance, 0);
                g_status.bigTug++;
            }

            if (st == (int)eFishingState::BigFish_Pumpin) g_status.bigPumpin++;
            if (st == (int)eFishingState::BigFish_Drag) g_status.bigDrag++;

            // tug / stun liên tục
            const bool doTug = (st == (int)eFishingState::BigFish_Tug) ||
                               (st == (int)eFishingState::BigFish_Fighting) ||
                               (st == (int)eFishingState::BigFish_Drag);
            if (doTug && g_cfg.bigAutoTug && f_requestTug && g_selfInstance &&
                t - g_lastBig >= (uint64_t)g_cfg.bigTugIntervalMs) {
                g_lastBig = t;
                f_requestTug(g_selfInstance, &g_tugDelegate);
            }
            if ((st == (int)eFishingState::BigFish_Stun ||
                 st == (int)eFishingState::BigFish_StunBegin) &&
                g_cfg.bigAutoStun && f_requestStunHit && g_selfInstance &&
                t - g_lastBig >= (uint64_t)g_cfg.bigTugIntervalMs) {
                g_lastBig = t;
                f_requestStunHit(g_selfInstance, &g_stunDelegate);
            }
        }

        // kết thúc -> quăng lại
        if ((st == (int)eFishingState::BigFish_Catch ||
             st == (int)eFishingState::BigFish_Miss ||
             st == (int)eFishingState::BigFish_StunRecovery) &&
            g_cfg.bigAutoCast && t - g_lastCast >= (uint64_t)g_cfg.castIntervalMs) {
            g_lastCast = t;
            g_status.casts++;
            if (f_startFishing) f_startFishing(control);
        }
        return;
    }

    // ================= NHÁNH A: CÁ THƯỜNG =================
    if (g_cfg.autoCast && (st == (int)eFishingState::None || st == (int)eFishingState::Idle ||
                           st == (int)eFishingState::Finish || st == (int)eFishingState::Fail ||
                           st == (int)eFishingState::CastingFail ||
                           st == (int)eFishingState::Miss)) {
        if (t - g_lastCast >= (uint64_t)g_cfg.castIntervalMs) {
            g_lastCast = t;
            g_status.casts++;
            if (f_startFishing) f_startFishing(control);
            else if (f_onClickFishing) f_onClickFishing(control);
        }
    }

    if (g_cfg.autoBite && (st == (int)eFishingState::Search ||
                           st == (int)eFishingState::SearchResult) &&
        t - g_lastBite >= (uint64_t)g_cfg.castIntervalMs) {
        g_lastBite = t;
        auto bite = reinterpret_cast<void (*)(void*)>(o_fishingBite);
        if (bite) bite(control);
    }

    if (g_cfg.autoTug && (st == (int)eFishingState::Fighting ||
                          st == (int)eFishingState::Hit) &&
        t - g_lastTug >= (uint64_t)g_cfg.tugIntervalMs) {
        g_lastTug = t;
        if (f_requestTug && g_selfInstance) f_requestTug(g_selfInstance, &g_tugDelegate);
    }
}

} // namespace PF::AutoCast
