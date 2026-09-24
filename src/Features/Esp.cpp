#include "Esp.hpp"

#include <cmath>
#include <cstring>

#include "../Config.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/math.hpp"
#include "../Core/monostring.hpp"
#include "imgui.h"

namespace PF::Esp {

// ---- cấu hình (đồng bộ với GUI) ----
bool g_showBox = true;
bool g_showName = true;
bool g_showDistance = true;
bool g_onlyInRange = true;
float g_maxDistance = 60.0f;

namespace {

// Vector3 của Unity trả về trong d0-d2 (3 float)
struct Vec3 {
    float x, y, z;
};

constexpr const char* kActorSystemClass = "ActorSystem";
constexpr const char* kStaticSelf = "Self";
constexpr const char* kGetOther = "get_OtherActorCharacter";
constexpr const char* kGetTransform = "get_Transform";
constexpr const char* kTransformClass = "Transform";
constexpr const char* kGetPosition = "get_position";
constexpr const char* kCameraClass = "Camera";
constexpr const char* kGetMain = "get_main";
constexpr const char* kWorldToScreen = "WorldToScreenPoint";
constexpr const char* kActorBaseClass = "ActorBase";
constexpr const char* kGetNickName = "get_nickName";

void* g_actorSystemClass = nullptr;
void* g_actorBaseClass = nullptr;
void* g_transformClass = nullptr;
void* g_cameraClass = nullptr;

void* (*g_getOther)(void*) = nullptr;
void* (*g_getTransform)(void*) = nullptr;
Vec3 (*g_getPosition)(void*) = nullptr;
// static getter: prototype có tham số MethodInfo* ẩn -> gọi với nullptr
void* (*g_getMain)(void*) = nullptr;
Vec3 (*g_worldToScreen)(void*, Vec3) = nullptr;
const void* (*g_nickFn)(void*) = nullptr; // string get_nickName()

std::vector<Player> g_players;
std::vector<Player> g_visible; // đã project + lọc
char g_error[160] = "";
bool g_enabled = false;
bool g_setupDone = false;

void setError(const char* msg) {
    snprintf(g_error, sizeof(g_error), "%s", msg ? msg : "");
}

Vec3 localCameraPos() {
    if (!g_getMain) return {0, 0, 0};
    void* cam = g_getMain(nullptr); // tham số MethodInfo* ẩn
    if (!cam) return {0, 0, 0};
    // lấy position của camera: dùng lại get_position trên Transform của camera
    void* tr = g_getTransform ? g_getTransform(cam) : nullptr;
    if (!tr || !g_getPosition) return {0, 0, 0};
    return g_getPosition(tr);
}

} // namespace

bool setup() {
    if (g_setupDone) return g_actorSystemClass != nullptr;
    g_setupDone = true;

    if (!Il2Cpp::ready()) {
        setError("il2cpp chua san sang");
        return false;
    }

    g_actorSystemClass = Il2Cpp::findClass(kActorSystemClass, "");
    g_actorBaseClass = Il2Cpp::findClass(kActorBaseClass, "");
    g_transformClass = Il2Cpp::findClass(kTransformClass, "UnityEngine");
    g_cameraClass = Il2Cpp::findClass(kCameraClass, "UnityEngine");

    if (!g_actorSystemClass) {
        setError("khong tim thay class ActorSystem");
        PF_LOG("[esp] %s", g_error);
        return false;
    }
    if (!g_actorBaseClass) {
        setError("khong tim thay class ActorBase");
        return false;
    }
    if (!g_transformClass || !g_cameraClass) {
        setError("khong tim thay Transform/Camera cua UnityEngine");
        return false;
    }

    g_getOther = reinterpret_cast<void* (*)(void*)>(
        Il2Cpp::resolveByClass(g_actorSystemClass, kGetOther, 0).fnptr);
    g_getTransform = reinterpret_cast<void* (*)(void*)>(
        Il2Cpp::resolveByClass(g_actorBaseClass, kGetTransform, 0).fnptr);
    g_getPosition = reinterpret_cast<Vec3 (*)(void*)>(
        Il2Cpp::resolveByClass(g_transformClass, kGetPosition, 0).fnptr);
    g_getMain = reinterpret_cast<void* (*)(void*)>(
        Il2Cpp::resolveByClass(g_cameraClass, kGetMain, 0).fnptr);
    g_worldToScreen = reinterpret_cast<Vec3 (*)(void*, Vec3)>(
        Il2Cpp::resolveByClass(g_cameraClass, kWorldToScreen, 1).fnptr);
    g_nickFn = reinterpret_cast<const void* (*)(void*)>(
        Il2Cpp::resolveByClass(g_actorBaseClass, kGetNickName, 0).fnptr);

    if (!g_getOther || !g_getTransform || !g_getPosition || !g_getMain || !g_worldToScreen) {
        setError("thieu method (get_Other/get_Transform/get_position/get_main/W2SP)");
        PF_LOG("[esp] %s", g_error);
        return false;
    }

    setError("");
    PF_LOG("[esp] setup OK (ActorSystem=%p, %zu players se duoc quet)", g_actorSystemClass, 0);
    return true;
}

bool ready() {
    return g_actorSystemClass != nullptr;
}

void shutdown() {
    g_enabled = false;
    g_setupDone = false;
    g_actorSystemClass = nullptr;
    g_getOther = nullptr;
    g_getTransform = nullptr;
    g_getPosition = nullptr;
    g_getMain = nullptr;
    g_worldToScreen = nullptr;
    g_players.clear();
    g_visible.clear();
}

bool enabled() {
    return g_enabled;
}

void setEnabled(bool on) {
    if (on && !setup())
        return;
    g_enabled = on;
    if (on)
        update();
}

size_t playerCount() {
    return g_players.size();
}

size_t visibleCount() {
    return g_visible.size();
}

const char* lastError() {
    return g_error;
}

void update() {
    if (!g_enabled || !g_actorSystemClass) return;

    // 1) lấy ActorSystem.Self (static field)
    void* system = nullptr;
    if (!Il2Cpp::readStaticField<void*>(g_actorSystemClass, kStaticSelf, system) || !system) {
        // chưa vào map -> chưa có hệ thống
        g_players.clear();
        g_visible.clear();
        return;
    }

    // 2) Dictionary<long, ActorCharacter> -> Values -> enumerator
    void* dict = g_getOther(system);
    if (!dict) {
        g_players.clear();
        g_visible.clear();
        return;
    }

    void* values = Il2Cpp::callNoArgObject(dict, "get_Values");
    if (!values) return;
    void* valuesClass = Il2Cpp::_object_get_class ? Il2Cpp::_object_get_class(values) : nullptr;
    if (!valuesClass) return;

    void* enumerator = Il2Cpp::callNoArgObject(values, "GetEnumerator");
    if (!enumerator) return;
    void* enumClass = Il2Cpp::_object_get_class ? Il2Cpp::_object_get_class(enumerator) : nullptr;
    if (!enumClass) return;

    auto moveNext = reinterpret_cast<bool (*)(void*)>(
        Il2Cpp::resolveByClass(enumClass, "MoveNext", 0).fnptr);
    auto current = reinterpret_cast<uint64_t (*)(void*)>(
        Il2Cpp::resolveByClass(enumClass, "get_Current", 0).fnptr);
    if (!moveNext || !current) {
        setError("khong resolve duoc MoveNext/get_Current");
        return;
    }

    // 3) camera + viewport
    void* cam = g_getMain(nullptr);
    if (!cam) {
        setError("Camera.main == null");
        return;
    }
    const Vec3 camPos = localCameraPos();

    ImVec2 display = ImGui::GetIO().DisplaySize;

    g_players.clear();
    g_visible.clear();

    int guard = 0;
    while (moveNext(enumerator) && guard++ < 256) {
        // Current trả về KeyValuePair<long, ActorCharacter> (struct 16 bytes)
        // nên đọc struct để lấy value
        struct KeyValuePair {
            int64_t key;
            void* value;
        };
        KeyValuePair kv = {};
        // get_Current trả struct 2 field: dùng con trỏ hợp lệ
        using GetCurrentFn = KeyValuePair (*)(void*);
        KeyValuePair pair = reinterpret_cast<GetCurrentFn>(current)(enumerator);
        kv = pair;
        if (!kv.value) continue;

        // transform -> position
        void* tr = g_getTransform(kv.value);
        if (!tr) continue;
        Vec3 p = g_getPosition(tr);

        Player pl;
        pl.id = static_cast<uint64_t>(kv.key);
        pl.x = p.x;
        pl.y = p.y;
        pl.z = p.z;
        float dx = p.x - camPos.x, dy = p.y - camPos.y, dz = p.z - camPos.z;
        pl.dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        // nick: thử get_nickName() (ActorBase có property này trong dump)
        if (g_nickFn) {
            const void* s = g_nickFn(kv.value);
            if (s) pl.name = monoToUtf8(s);
        }

        // chiếu lên màn hình
        Vec3 sp = g_worldToScreen(cam, p);
        pl.onScreen = sp.z > 0.0f;
        pl.screenX = sp.x;
        pl.screenY = sp.y;
        pl.visible = pl.onScreen && sp.x >= 0 && sp.y >= 0 && sp.x <= display.x && sp.y <= display.y;

        g_players.push_back(pl);
    }

    // lọc theo khoảng cách
    for (const auto& p : g_players) {
        if (g_onlyInRange && p.dist > g_maxDistance) continue;
        if (!p.visible) continue;
        g_visible.push_back(p);
    }
}

void draw() {
    if (!g_enabled) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& p : g_visible) {
        if (g_showBox) {
            // khung đơn giản quanh chân nhân vật
            const float h = 24.0f;
            const float w = 12.0f;
            const ImVec2 tl(p.screenX - w * 0.5f, p.screenY - h);
            const ImVec2 br(p.screenX + w * 0.5f, p.screenY);
            dl->AddRect(tl, br, IM_COL32(255, 80, 80, 220), 0.0f, 0, 1.5f);
        }
        if (g_showDistance) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", p.dist);
            dl->AddText(ImVec2(p.screenX + 8.0f, p.screenY - 14.0f), IM_COL32(255, 255, 255, 230), buf);
        }
        if (g_showName) {
            // lấy tên từ ActorCharacter nếu có field Name
            char idbuf[32];
            snprintf(idbuf, sizeof(idbuf), "#%llu", (unsigned long long)p.id);
            dl->AddText(ImVec2(p.screenX - 10.0f, p.screenY - 28.0f), IM_COL32(120, 255, 120, 230), idbuf);
        }
    }
}

} // namespace PF::Esp
