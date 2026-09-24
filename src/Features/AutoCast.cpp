#include "AutoCast.hpp"

#include <atomic>
#include <ctime>
#include <pthread.h>
#include <unistd.h>

#include "../Core/crashguard.hpp"
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
constexpr uintptr_t kRvaFishingBite = 0x36DD3E0;

void* g_controlClass = nullptr;
void* g_systemClass = nullptr;
void* g_floatClass = nullptr;

// Vector3 của Unity (trả về trong d0-d2)
struct Vec3 {
    float x, y, z;
};

// hàm gọi trực tiếp
void* (*f_self)(void*) = nullptr;                 // FishingSystem.get_Self()
bool (*f_startFishing)(void*) = nullptr;             // StartFishing()
void (*f_onClickFishing)(void*) = nullptr;           // OnClickFishing()
void (*f_requestTug)(void*, void*) = nullptr;        // RequestFishingTug(Action)
void (*f_requestHit)(void*, bool, Vec3, void*) = nullptr; // RequestFishingHit(bool,Vector3,Action)
void (*f_requestStunHit)(void*, void*) = nullptr;    // RequestStunHit(Action)
void (*f_fishCancel)(void*) = nullptr;               // FishingCancel()
bool (*f_shadowReady)(void*) = nullptr;              // get_IsShadowFishReady()
// Điều kiện game dùng để cho phép kéo (đọc code tại 0x2CE13AC):
//   field(this+0x84) == 15  &&  this+0xA8 != null  &&  IsMyActor() ...
// -> ta gọi lại đúng hàm của game thay vì viết lại điều kiện.
bool (*f_isBigFishHit)(void*) = nullptr;

// lấy vị trí float đang câu (để lôi đúng chỗ)
Vec3 (*f_floatPos)(void*) = nullptr;                 // Transform.get_position(floatTransform)
void* (*f_floatTransform)(void*) = nullptr;          // FishingFloatController.get_Transform()
void* g_floatInstance = nullptr;

// lấy instance FishingSystem (gọi mỗi lần cần vì có thể đổi khi vào map)
void* fishingSystem() {
    return f_self ? f_self(nullptr) : nullptr;
}

// trampoline gốc
void* o_updateState = nullptr;
void* o_fishingBite = nullptr;
void* o_floatUpdate = nullptr;

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}
uint64_t g_lastCast = 0, g_lastTug = 0, g_lastBite = 0, g_lastBig = 0, g_lastDrag = 0;

// ---------------------------------------------------------------------
//  Delegate giả cho Action<> của IL2CPP (invoke nằm ở offset 0x10).
//  Callback chỉ GHI NHẬN số liệu, KHÔNG sửa kết quả — server quyết định.
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

// Action<bool,bool,int,bool>: success, isStun, damage, isCritical
void tugResultCallback(bool success, bool isStun, int damage, bool isCritical) {
    g_status.tugs++;
    using Fn = void (*)(bool, bool, int, bool);
    reinterpret_cast<Fn>(g_tugDelegate.invoke)(success, isStun, damage, isCritical);
}

// Action<bool>
void stunHitCallback(bool success) {
    g_status.stuns++;
    using Fn = void (*)(bool);
    reinterpret_cast<Fn>(g_stunDelegate.invoke)(success);
}

// ---------------------------------------------------------------------
//  Detour chỉ ĐỌC trạng thái — không thay đổi kết quả nào
// ---------------------------------------------------------------------
bool detourUpdateFishingState(void* self, int state) {
    g_status.control = self;
    g_status.currentState = state;

    // Nhóm bóng cá: ưu tiên người dùng ép tay, nếu không thì lấy từ state
    if (g_cfg.forceTier == 1)
        g_status.tier = ShadowTier::Small;
    else if (g_cfg.forceTier == 2)
        g_status.tier = ShadowTier::Big;
    else
        g_status.tier = (state >= kBigFishThreshold) ? ShadowTier::Big : ShadowTier::Small;

    g_status.isBigFish = g_status.tier == ShadowTier::Big;
    return reinterpret_cast<bool (*)(void*, int)>(o_updateState)(self, state);
}

void detourFishingBite(void* self) {
    g_status.bites++;
    reinterpret_cast<void (*)(void*)>(o_fishingBite)(self);
}

// FishingFloatController.Update(): ghi nhớ instance float + trạng thái bóng
void detourFloatUpdate(void* self) {
    g_floatInstance = self;
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
    // Chỉ hook 2 hàm cần thiết cho logic tự động + theo dõi.
    // (Các hàm nhận kết quả từ server KHÔNG hook -> không ép gì cả)
    const HookDef defs[] = {
        {"UpdateFishingState", 1, kRvaUpdateFishingState, (void*)detourUpdateFishingState, &o_updateState},
        {"FishingBite", 0, kRvaFishingBite, (void*)detourFishingBite, &o_fishingBite},
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
    PF_LOG("[autocast] hook %zu hàm theo dõi của %s", ok, kControlClass);
    return ok > 0;
}

void resolveAll() {
    g_tugDelegate.invoke = (void*)tugResultCallback;
    g_stunDelegate.invoke = (void*)stunHitCallback;

    if (!g_controlClass) g_controlClass = Il2Cpp::findClass(kControlClass, "");
    if (!g_systemClass) g_systemClass = Il2Cpp::findClass(kSystemClass, "");
    if (!g_floatClass) g_floatClass = Il2Cpp::findClass(kFloatClass, "");
    if (!g_controlClass) return;

    if (g_systemClass) {
        g_status.systemResolved = true;
        f_self = reinterpret_cast<void* (*)(void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "get_Self", 0).fnptr);
        f_requestTug = reinterpret_cast<void (*)(void*, void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "RequestFishingTug", 1).fnptr);
        f_requestHit = reinterpret_cast<void (*)(void*, bool, Vec3, void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "RequestFishingHit", 3).fnptr);
        f_requestStunHit = reinterpret_cast<void (*)(void*, void*)>(
            Il2Cpp::resolveByClass(g_systemClass, "RequestStunHit", 1).fnptr);
    }

    f_startFishing = reinterpret_cast<bool (*)(void*)>(
        Il2Cpp::resolveByClass(g_controlClass, "StartFishing", 0).fnptr);
    f_onClickFishing = reinterpret_cast<void (*)(void*)>(
        Il2Cpp::resolveByClass(g_controlClass, "OnClickFishing", 0).fnptr);
    f_fishCancel = reinterpret_cast<void (*)(void*)>(
        Il2Cpp::resolveByClass(g_controlClass, "FishingCancel", 0).fnptr);

    if (!g_hooksInstalled && installHooks()) {
        g_hooksInstalled = true;
        snprintf(g_status.note, sizeof(g_status.note), "san sang, vao game la chay");
    }

    if (g_floatClass && !o_floatUpdate) {
        auto up = Il2Cpp::resolveByClass(g_floatClass, "Update", 0);
        f_shadowReady = reinterpret_cast<bool (*)(void*)>(
            Il2Cpp::resolveByClass(g_floatClass, "get_IsShadowFishReady", 0).fnptr);
        f_isBigFishHit = reinterpret_cast<bool (*)(void*)>(
            Il2Cpp::resolveByClass(g_floatClass, "IsBigFishHit", 0).fnptr);
        f_floatTransform = reinterpret_cast<void* (*)(void*)>(
            Il2Cpp::resolveByClass(g_floatClass, "get_transform", 0).fnptr);
        if (up.fnptr) {
            // detour cần biết instance float để lấy vị trí khi lôi
            g_floatInstance = nullptr;
            PF::hook("FishingFloatController::Update", up.fnptr,
                     (void*)detourFloatUpdate, &o_floatUpdate);
        }
        // Transform.get_position (UnityEngine)
        void* trClass = Il2Cpp::findClass("Transform", "UnityEngine");
        if (trClass) {
            f_floatPos = reinterpret_cast<Vec3 (*)(void*)>(
                Il2Cpp::resolveByClass(trClass, "get_position", 0).fnptr);
        }
    }
}

void resolveAllEntry() { resolveAll(); }

// Gọi resolveAll() có bẫy lỗi: nếu code game lỗi thì chỉ log, không làm sập app.
void resolveAllSafe() { CrashGuard::run(resolveAllEntry, "resolveAll"); }

void* bootstrapThread(void* /*arg*/) {
    for (int i = 0; i < 600; ++i) { // ~5 phút
        if (!Il2Cpp::ready()) {
            Il2Cpp::init();
        } else if (!Il2Cpp::domainReady()) {
            // Unity mới load xong, il2cpp_init() chưa xong -> TUYỆT ĐỐI chưa
            // được đụng domain, gọi sớm là crash (xem domainReady()).
            if (i % 4 == 0) PF_LOG("[boot] đợi Unity khởi tạo xong domain IL2CPP… (~%ds)", i / 2);
        } else if (!g_hooksInstalled) {
            PF_LOG("[boot] domain IL2CPP sẵn sàng -> bắt đầu resolve + hook");
            resolveAllSafe();
            if (g_hooksInstalled) break;
        } else {
            break;
        }
        usleep(500 * 1000);
    }
    for (int i = 0; i < 20 && !Esp::setEnabledIfConfigured(); ++i)
        usleep(500 * 1000);
    g_bootstrapRunning = false;
    return nullptr;
}

// true nếu state nằm trong nhóm "đang làm việc" (cần tương tác)
bool isWorkingState(int st) {
    return st == (int)eFishingState::Casting || st == (int)eFishingState::Search ||
           st == (int)eFishingState::SearchResult || st == (int)eFishingState::Hit ||
           st == (int)eFishingState::Fighting || st == (int)eFishingState::Catch ||
           st == (int)eFishingState::BigFish_Pumpin || st == (int)eFishingState::BigFish_Drag ||
           st == (int)eFishingState::BigFish_Tug || st == (int)eFishingState::BigFish_Fighting ||
           st == (int)eFishingState::BigFish_Stun || st == (int)eFishingState::BigFish_StunBegin;
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
    if (!g_hooksInstalled) {
        if (Il2Cpp::domainReady()) resolveAllSafe();
        return;
    }
    if (!g_controlClass) return;
    void* control = g_status.control;
    if (!control) return; // chưa vào trạng thái câu nào

    const uint64_t t = nowMs();
    const int st = g_status.currentState;
    const bool big = g_status.tier == ShadowTier::Big;

    // ================= NHÁNH B: BÓNG 6-7 (cá to / quái) =================
    if (big) {
        // --- BƯỚC 1: LÔI CÁ (bắt buộc với bóng 6-7) ---
        // Đã đọc code thật của game tại UpdateFishingState (0x36d9a18):
        //     mov  x0, x22            ; FishingSystem.get_Self()
        //     mov  w1, #1             ; isHit = true
        //     fmov s0/s1/s2, ...      ; fishingPoint
        //     mov  x3, #0             ; hitResultCB = NULL   <-- game truyền NULL
        //     bl   FishingSystem::RequestFishingHit
        // Ta gọi y hệt, kể cả việc truyền NULL cho callback.
        const bool needDrag = st == (int)eFishingState::BigFish_Pumpin ||
                              st == (int)eFishingState::BigFish_Drag ||
                              st == (int)eFishingState::BigFish_RaidFighting;
        if (needDrag && g_cfg.bigAutoDrag && f_requestHit) {
            void* sys = fishingSystem();
            if (sys && t - g_lastDrag >= (uint64_t)g_cfg.bigTugIntervalMs) {
                g_lastDrag = t;
                g_status.bigDrag++;
                Vec3 pt{0, 0, 0};
                if (g_cfg.bigDragUseFloatPos && g_floatInstance &&
                    f_floatTransform && f_floatPos) {
                    void* tr = f_floatTransform(g_floatInstance);
                    if (tr) pt = f_floatPos(tr);
                }
                f_requestHit(sys, true, pt, nullptr); // callback = NULL như game
            }
        }

        // --- BƯỚC 2: KÉO (tug) ---
        // Game chỉ cho kéo khi FishingFloatController::IsBigFishHit() trả true
        // (đọc code tại 0x36DB2B0: bl IsBigFishHit; cbz w0 -> bỏ qua RequestFishingTug).
        // Ta dùng đúng cổng đó thay vì đoán theo state.
        const bool inFight = st == (int)eFishingState::BigFish_Tug ||
                             st == (int)eFishingState::BigFish_Fighting;
        if (inFight && g_cfg.bigAutoTug && f_requestTug) {
            const bool gate = f_isBigFishHit ? (g_floatInstance && f_isBigFishHit(g_floatInstance)) : true;
            void* sys = gate ? fishingSystem() : nullptr;
            if (sys && t - g_lastBig >= (uint64_t)g_cfg.bigTugIntervalMs) {
                g_lastBig = t;
                g_status.bigTug++;
                f_requestTug(sys, &g_tugDelegate);
            }
        }
        if ((st == (int)eFishingState::BigFish_Stun ||
             st == (int)eFishingState::BigFish_StunBegin) && g_cfg.bigAutoStun) {
            void* sys = fishingSystem();
            if (sys && f_requestStunHit && t - g_lastBig >= (uint64_t)g_cfg.bigTugIntervalMs) {
                g_lastBig = t;
                f_requestStunHit(sys, &g_stunDelegate);
            }
        }
        // xong màn cá lớn -> quăng câu tiếp
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

    // ================= NHÁNH A: BÓNG 1-5 (cá bé) =================
    if (g_cfg.autoCast &&
        (st == (int)eFishingState::None || st == (int)eFishingState::Idle ||
         st == (int)eFishingState::Finish || st == (int)eFishingState::Fail ||
         st == (int)eFishingState::CastingFail || st == (int)eFishingState::Miss)) {
        if (t - g_lastCast >= (uint64_t)g_cfg.castIntervalMs) {
            g_lastCast = t;
            g_status.casts++;
            if (f_startFishing) f_startFishing(control);
            else if (f_onClickFishing) f_onClickFishing(control);
        }
    }

    if (g_cfg.autoBite &&
        (st == (int)eFishingState::Search || st == (int)eFishingState::SearchResult) &&
        t - g_lastBite >= (uint64_t)g_cfg.castIntervalMs) {
        g_lastBite = t;
        auto bite = reinterpret_cast<void (*)(void*)>(o_fishingBite);
        if (bite) bite(control);
    }

    if (g_cfg.autoTug &&
        (st == (int)eFishingState::Fighting || st == (int)eFishingState::Hit)) {
        void* sys = fishingSystem();
        if (sys && f_requestTug && t - g_lastTug >= (uint64_t)g_cfg.tugIntervalMs) {
            g_lastTug = t;
            f_requestTug(sys, &g_tugDelegate);
        }
    }
}

} // namespace PF::AutoCast
