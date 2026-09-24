#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dobby.h" // libs/Dobby/include/dobby.h

#include "log.hpp"
#include "target.hpp"

namespace PF {

// ---------------------------------------------------------------------
//  Registry: GUI hiển thị trạng thái từng hook
// ---------------------------------------------------------------------
struct HookEntry {
    std::string name;
    void* target = nullptr;
    bool installed = false;
};

inline std::vector<HookEntry>& hookRegistry() {
    static std::vector<HookEntry> v;
    return v;
}

inline void registerHook(const char* name, void* target, bool ok) {
    hookRegistry().push_back({name, target, ok});
    PF_LOG("[hook] %-36s @ %p  %s", name, target, ok ? "OK" : "FAIL");
}

// ---------------------------------------------------------------------
//  Hook API
//    Dobby nhận con trỏ hàm C++ thuần (không std::function).
//    Detour của bạn nên là hàm tĩnh ở cấp file, ví dụ:
//      static void* orig = nullptr;
//      static bool myFunc(void* self) { return ((Fn)orig)(self); }
//      PF::hook("name", target, (void*)myFunc, &orig);
// ---------------------------------------------------------------------

inline bool hook(const char* name, void* target, void* replacement, void** outOriginal = nullptr) {
    if (!target) {
        registerHook(name, nullptr, false);
        return false;
    }
    void* trampoline = nullptr;
    const int res = DobbyHook(target, replacement, outOriginal ? outOriginal : &trampoline);
    const bool ok = (res == 0);
    registerHook(name, target, ok);
    return ok;
}

// Hook theo RVA (offset lấy từ Il2CppDumper, tính từ base UnityFramework)
inline bool hookRva(const char* name, uintptr_t offset, void* replacement, void** outOriginal = nullptr) {
    if (offset == 0) {
        PF_LOG("[hook] %-36s offset = 0 (chưa điền)", name);
        registerHook(name, nullptr, false);
        return false;
    }
    return hook(name, rva(offset), replacement, outOriginal);
}

// Gỡ một hook cụ thể (DobbyDestroy), đồng bộ registry
inline bool removeHook(void* target) {
    if (!target) return false;
    for (auto& e : hookRegistry()) {
        if (e.target == target && e.installed) {
            DobbyDestroy(target);
            e.installed = false;
            return true;
        }
    }
    return false;
}

// Bỏ toàn bộ hook đã cài
inline void removeAllHooks() {
    size_t n = 0;
    for (auto& e : hookRegistry()) {
        if (e.installed && e.target) {
            DobbyDestroy(e.target);
            e.installed = false;
            ++n;
        }
    }
    PF_LOG("[hook] removed %zu hook(s)", n);
}

inline size_t hookCount() {
    size_t n = 0;
    for (const auto& e : hookRegistry())
        if (e.installed) ++n;
    return n;
}

} // namespace PF
