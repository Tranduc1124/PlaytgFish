#include "SettingsStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../Config.hpp"
#include "../Features/AutoCast.hpp"
#include "../Features/Esp.hpp"
#include "log.hpp"
#include "paths.hpp"

namespace PF::SettingsStore {
namespace {

constexpr const char* kFile = "playfish_config.txt";

void key(std::ofstream& o, const char* k, bool v) { o << k << " = " << (v ? 1 : 0) << "\n"; }
void num(std::ofstream& o, const char* k, int v) { o << k << " = " << v << "\n"; }
void flt(std::ofstream& o, const char* k, float v) { o << k << " = " << v << "\n"; }

bool readLine(std::istream& in, std::string& k, std::string& v) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        k = line.substr(0, eq);
        v = line.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        const size_t s = v.find_first_not_of(" \t\r");
        if (s != std::string::npos) v = v.substr(s);
        return true;
    }
    return false;
}

} // namespace

std::string path() {
    return paths::dataFile(kFile);
}

void save() {
    std::ofstream out(path(), std::ios::trunc);
    if (!out) return;
    auto& a = AutoCast::settings();
    key(out, "auto.enabled", a.enabled);
    key(out, "auto.tier", a.forceTier == 2);  // chỉ lưu dạng ép nhánh to
    num(out, "auto.tierMode", a.forceTier);
    // cá bé (1-5)
    key(out, "small.cast", a.autoCast);
    key(out, "small.bite", a.autoBite);
    key(out, "small.tug", a.autoTug);
    num(out, "small.castMs", a.castIntervalMs);
    num(out, "small.tugMs", a.tugIntervalMs);
    // cá to (6-7)
    key(out, "big.cast", a.bigAutoCast);
    key(out, "big.drag", a.bigAutoDrag);
    key(out, "big.dragUseFloat", a.bigDragUseFloatPos);
    key(out, "big.tug", a.bigAutoTug);
    key(out, "big.stun", a.bigAutoStun);
    num(out, "big.tugMs", a.bigTugIntervalMs);
    // ESP
    key(out, "esp.on", Esp::enabled());
    key(out, "esp.box", Esp::g_showBox);
    key(out, "esp.name", Esp::g_showName);
    key(out, "esp.distance", Esp::g_showDistance);
    key(out, "esp.rangeOnly", Esp::g_onlyInRange);
    flt(out, "esp.maxDistance", Esp::g_maxDistance);
    PF_LOG("[config] đã lưu %s", kFile);
}

void load() {
    std::ifstream in(path());
    if (!in) return;
    auto& a = AutoCast::settings();
    std::string k, v;
    while (readLine(in, k, v)) {
        const bool b = atoi(v.c_str()) != 0;
        const int i = atoi(v.c_str());
        const float f = static_cast<float>(atof(v.c_str()));
        if (k == "auto.enabled") a.enabled = b;
        else if (k == "auto.tierMode") a.forceTier = i;
        else if (k == "small.cast") a.autoCast = b;
        else if (k == "small.bite") a.autoBite = b;
        else if (k == "small.tug") a.autoTug = b;
        else if (k == "small.castMs") a.castIntervalMs = i;
        else if (k == "small.tugMs") a.tugIntervalMs = i;
        else if (k == "big.cast") a.bigAutoCast = b;
        else if (k == "big.drag") a.bigAutoDrag = b;
        else if (k == "big.dragUseFloat") a.bigDragUseFloatPos = b;
        else if (k == "big.tug") a.bigAutoTug = b;
        else if (k == "big.stun") a.bigAutoStun = b;
        else if (k == "big.tugMs") a.bigTugIntervalMs = i;
        else if (k == "esp.on") Esp::setEnabled(b);
        else if (k == "esp.box") Esp::g_showBox = b;
        else if (k == "esp.name") Esp::g_showName = b;
        else if (k == "esp.distance") Esp::g_showDistance = b;
        else if (k == "esp.rangeOnly") Esp::g_onlyInRange = b;
        else if (k == "esp.maxDistance") Esp::g_maxDistance = f;
    }
    PF_LOG("[config] đã nạp %s", kFile);
}

} // namespace PF::SettingsStore
