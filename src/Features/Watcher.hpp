#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PF::Watcher {

// Một "watcher" = hook generic lên 1 method IL2CPP để quan sát lời gọi.
// Dùng để tìm đúng hàm câu cá trong game (xem return value + arguments).
struct Entry {
    std::string className;
    std::string methodName;
    void* target = nullptr;
    bool active = false;
    uint64_t callCount = 0;
    uintptr_t lastReturn = 0;
    uintptr_t lastArg0 = 0;
    uint64_t lastCallMs = 0;
};

// Cài watcher cho method đã resolve (dùng Il2Cpp::resolve)
bool add(const std::string& className, const std::string& methodName, void* target);

void removeAt(size_t index);

void clear();

// Bản sao an toàn cho GUI (copy dưới lock)
std::vector<Entry> snapshot();

} // namespace PF::Watcher
