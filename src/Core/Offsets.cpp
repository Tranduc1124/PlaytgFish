#include "Offsets.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../Core/log.hpp"
#include "../Core/paths.hpp"

namespace PF {

OffsetRegistry& OffsetRegistry::get() {
    static OffsetRegistry instance;
    return instance;
}

void OffsetRegistry::registerBuiltin(const std::string& name, const std::string& description) {
    if (has(name)) return;
    OffsetEntry e;
    e.name = name;
    e.value = 0;
    e.description = description;
    e.builtin = true;
    m_entries.push_back(e);
}

OffsetEntry* OffsetRegistry::find(const std::string& name) {
    for (auto& e : m_entries)
        if (e.name == name) return &e;
    return nullptr;
}

bool OffsetRegistry::set(const std::string& name, uintptr_t value, const std::string& description) {
    if (OffsetEntry* e = find(name)) {
        e->value = value;
        if (!description.empty()) e->description = description;
        return true;
    }
    OffsetEntry e;
    e.name = name;
    e.value = value;
    e.description = description;
    e.builtin = false;
    m_entries.push_back(e);
    return true;
}

uintptr_t OffsetRegistry::get(const std::string& name) const {
    for (const auto& e : m_entries)
        if (e.name == name) return e.value;
    return 0;
}

bool OffsetRegistry::has(const std::string& name) const {
    for (const auto& e : m_entries)
        if (e.name == name) return true;
    return false;
}

std::string OffsetRegistry::filePath() const {
    return paths::dataFile("playfish_offsets.txt");
}

// Định dạng:  name = 0xRVA   # description
bool OffsetRegistry::load() {
    const std::string path = filePath();
    std::ifstream in(path);
    if (!in) {
        PF_LOG("[offsets] chưa có file %s, dùng giá trị built-in", path.c_str());
        return false;
    }

    std::string line;
    int loaded = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        std::string rest = line.substr(eq + 1);

        // bỏ comment phía sau
        const size_t hash = rest.find('#');
        if (hash != std::string::npos) rest = rest.substr(0, hash);

        // trim
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.erase(rest.begin());
        while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t' || rest.back() == '\r'))
            rest.pop_back();
        if (name.empty() || rest.empty()) continue;

        const uintptr_t value = static_cast<uintptr_t>(strtoull(rest.c_str(), nullptr, 0));
        set(name, value);
        ++loaded;
    }
    PF_LOG("[offsets] load %d offset từ %s", loaded, path.c_str());
    return true;
}

bool OffsetRegistry::save() const {
    const std::string path = filePath();
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        PF_LOG("[offsets] không ghi được %s", path.c_str());
        return false;
    }
    out << "# PlayFish offsets (RVA từ base UnityFramework)\n";
    for (const auto& e : m_entries) {
        char buf[64];
        snprintf(buf, sizeof(buf), "0x%lx", static_cast<unsigned long>(e.value));
        out << e.name << " = " << buf;
        if (!e.description.empty()) out << "   # " << e.description;
        out << "\n";
    }
    PF_LOG("[offsets] đã lưu %zu offset vào %s", m_entries.size(), path.c_str());
    return true;
}

} // namespace PF
