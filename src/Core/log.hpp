#pragma once

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace PF {

// Ring buffer log để hiện trong GUI tab Debug (không spam console).
inline std::vector<std::string>& logBuffer() {
    static std::vector<std::string> buf;
    return buf;
}

inline std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

inline void log(const char* fmt, ...) {
    char body[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    fprintf(stderr, "[PlayFish] %s\n", body);
    fflush(stderr);

    std::lock_guard<std::mutex> lock(logMutex());
    auto& buf = logBuffer();
    buf.emplace_back(body);
    if (buf.size() > 400) buf.erase(buf.begin(), buf.begin() + 200);
}

} // namespace PF

#define PF_LOG(...) ::PF::log(__VA_ARGS__)
