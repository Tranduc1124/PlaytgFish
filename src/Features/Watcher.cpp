#include "Watcher.hpp"

#include <ctime>
#include <mutex>

#include "../Core/hooker.hpp"
#include "../Core/log.hpp"

namespace PF::Watcher {
namespace {

std::vector<Entry> g_entries;
std::mutex g_mutex;

// Con trỏ gốc do Dobby trampoline ghi ra, dùng chung cho mọi watcher.
using AnyFn = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                            uintptr_t, uintptr_t, uintptr_t, uintptr_t);
AnyFn g_orig = nullptr;
bool g_watching = false;

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}

// Detour generic: x0 sau khi gọi lại hàm gốc chính là return value.
// (Hàm không có return thì x0 là rác — vẫn an toàn, chỉ là giá trị vô nghĩa.)
uintptr_t watchDispatch(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                        uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    uintptr_t ret = 0;
    if (g_orig) ret = g_orig(a0, a1, a2, a3, a4, a5, a6, a7);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_watching && !g_entries.empty()) {
            Entry& e = g_entries.front();
            e.callCount++;
            e.lastReturn = ret;
            e.lastArg0 = a0;
            e.lastCallMs = nowMs();
        }
    }
    return ret;
}

} // namespace

bool add(const std::string& className, const std::string& methodName, void* target) {
    if (!target) {
        PF_LOG("[watcher] %s::%s khong co native pointer", className.c_str(), methodName.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_watching) {
            PF_LOG("[watcher] da co watcher, chi ho tro 1 watcher cung luc");
            return false;
        }
    }

    if (!PF::hook(("watch:" + className + "::" + methodName).c_str(), target,
                  (void*)watchDispatch, (void**)&g_orig)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Entry e;
        e.className = className;
        e.methodName = methodName;
        e.target = target;
        e.active = true;
        g_entries.push_back(e);
        g_watching = true;
    }

    PF_LOG("[watcher] dang theo %s::%s @ %p", className.c_str(), methodName.c_str(), target);
    return true;
}

void removeAt(size_t index) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (index >= g_entries.size()) return;
    PF_LOG("[watcher] go %s::%s", g_entries[index].className.c_str(), g_entries[index].methodName.c_str());
    g_entries.erase(g_entries.begin() + static_cast<long>(index));
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
    g_watching = false;
    g_orig = nullptr;
}

std::vector<Entry> snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_entries;
}

} // namespace PF::Watcher
