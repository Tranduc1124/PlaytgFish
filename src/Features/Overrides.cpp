#include "Overrides.hpp"

#include <ctime>
#include <mutex>

#include "../Core/hooker.hpp"
#include "../Core/log.hpp"

namespace PF::Overrides {
namespace {

std::vector<Slot> g_slots;
std::mutex g_mutex;

// Con trỏ gốc: Dobby trampoline ghi ra khi hook thành công
using AnyFn = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                            uintptr_t, uintptr_t, uintptr_t, uintptr_t);

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}

// Mỗi slot có một detour riêng (template index) để giữ trampoline riêng biệt.
template <int N>
uintptr_t dispatch(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                    uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    Slot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (N < static_cast<int>(g_slots.size()) && g_slots[N].active)
            slot = &g_slots[N];
    }
    if (!slot) return 0;

    uintptr_t ret = 0;
    if (slot->orig) {
        auto fn = reinterpret_cast<AnyFn>(slot->orig);
        ret = fn(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        slot->calls++;
        slot->lastReturn = ret;
        slot->lastArg0 = a0;
        (void)nowMs();
    }

    switch (slot->mode) {
        case Mode::ForceTrue: return 1;
        case Mode::ForceFalse: return 0;
        case Mode::ForceValue: return slot->value;
        case Mode::PassThrough:
        default: return ret;
    }
}

} // namespace

const char* modeName(Mode m) {
    switch (m) {
        case Mode::ForceTrue: return "ForceTrue";
        case Mode::ForceFalse: return "ForceFalse";
        case Mode::ForceValue: return "ForceValue";
        case Mode::PassThrough:
        default: return "PassThrough";
    }
}

bool add(void* target, Mode mode, uintptr_t value, const std::string& label) {
    if (!target) return false;

    int idx = -1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 0; i < g_slots.size(); ++i) {
            if (!g_slots[i].active) { idx = static_cast<int>(i); break; }
        }
        if (idx < 0) {
            if (g_slots.size() < kMaxSlots) { g_slots.resize(kMaxSlots); }
            else {
                PF_LOG("[override] het slot (%zu)", kMaxSlots);
                return false;
            }
            idx = static_cast<int>(g_slots.size() - 1);
        }
    }

    Slot s;
    s.active = true;
    s.target = target;
    s.mode = mode;
    s.value = value;
    s.label = label;
    s.orig = nullptr;

    void* tramp = nullptr;
    void* replacement = nullptr;
    switch (idx) {
        case 0: replacement = (void*)dispatch<0>; break;
        case 1: replacement = (void*)dispatch<1>; break;
        case 2: replacement = (void*)dispatch<2>; break;
        case 3: replacement = (void*)dispatch<3>; break;
        case 4: replacement = (void*)dispatch<4>; break;
        case 5: replacement = (void*)dispatch<5>; break;
        case 6: replacement = (void*)dispatch<6>; break;
        case 7: replacement = (void*)dispatch<7>; break;
        default: return false;
    }

    if (!PF::hook(label.c_str(), target, replacement, &tramp))
        return false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        s.orig = tramp;
        g_slots[idx] = s;
    }
    PF_LOG("[override] %s -> %s @ %p", label.c_str(), modeName(mode), target);
    return true;
}

void removeAt(size_t index) {
    void* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (index >= g_slots.size() || !g_slots[index].active) return;
        target = g_slots[index].target;
        g_slots[index] = Slot{};
    }
    if (target) PF::removeHook(target);
}

void clear() {
    std::vector<void*> targets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& s : g_slots)
            if (s.active && s.target) targets.push_back(s.target);
        g_slots.clear();
    }
    for (void* t : targets) PF::removeHook(t);
}

std::vector<Slot> snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slots;
}

size_t count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t n = 0;
    for (const auto& s : g_slots)
        if (s.active) ++n;
    return n;
}

} // namespace PF::Overrides
