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

void writeKey(std::ofstream& out, const char* key, bool value) {
    out << key << " = " << (value ? 1 : 0) << "\n";
}
void writeInt(std::ofstream& out, const char* key, int value) {
    out << key << " = " << value << "\n";
}
void writeFloat(std::ofstream& out, const char* key, float value) {
    out << key << " = " << value << "\n";
}

bool readLine(std::istream& in, std::string& key, std::string& val) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        key = line.substr(0, eq);
        val = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        size_t s = val.find_first_not_of(" \t\r");
        if (s != std::string::npos) val = val.substr(s);
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
    writeKey(out, "autofish.enabled", a.enabled);
    writeKey(out, "autofish.cast", a.autoCast);
    writeKey(out, "autofish.castSuccess", a.forceCastSuccess);
    writeKey(out, "autofish.bite", a.autoBite);
    writeKey(out, "autofish.tug", a.autoTug);
    writeKey(out, "autofish.tugSuccess", a.forceTugSuccess);
    writeKey(out, "autofish.stun", a.forceStun);
    writeKey(out, "autofish.catch", a.forceCatch);
    writeKey(out, "autofish.lift", a.forceLift);
    writeInt(out, "autofish.castMs", a.castIntervalMs);
    writeInt(out, "autofish.tugMs", a.tugIntervalMs);
    // nhánh cá lớn / bóng cá
    writeKey(out, "bigfish.cast", a.bigAutoCast);
    writeKey(out, "bigfish.pumpin", a.bigAutoPumpin);
    writeKey(out, "bigfish.drag", a.bigAutoDrag);
    writeKey(out, "bigfish.tug", a.bigAutoTug);
    writeKey(out, "bigfish.stun", a.bigAutoStun);
    writeKey(out, "bigfish.success", a.bigForceSuccess);
    writeKey(out, "bigfish.zeroHp", a.bigZeroHp);
    writeInt(out, "bigfish.tugMs", a.bigTugIntervalMs);
    // ESP
    writeKey(out, "esp.enabled", Esp::enabled());
    writeKey(out, "esp.box", Esp::g_showBox);
    writeKey(out, "esp.name", Esp::g_showName);
    writeKey(out, "esp.distance", Esp::g_showDistance);
    writeKey(out, "esp.rangeOnly", Esp::g_onlyInRange);
    writeFloat(out, "esp.maxDistance", Esp::g_maxDistance);
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
        if (k == "autofish.enabled") a.enabled = b;
        else if (k == "autofish.cast") a.autoCast = b;
        else if (k == "autofish.castSuccess") a.forceCastSuccess = b;
        else if (k == "autofish.bite") a.autoBite = b;
        else if (k == "autofish.tug") a.autoTug = b;
        else if (k == "autofish.tugSuccess") a.forceTugSuccess = b;
        else if (k == "autofish.stun") a.forceStun = b;
        else if (k == "autofish.catch") a.forceCatch = b;
        else if (k == "autofish.lift") a.forceLift = b;
        else if (k == "autofish.castMs") a.castIntervalMs = i;
        else if (k == "autofish.tugMs") a.tugIntervalMs = i;
        else if (k == "bigfish.cast") a.bigAutoCast = b;
        else if (k == "bigfish.pumpin") a.bigAutoPumpin = b;
        else if (k == "bigfish.drag") a.bigAutoDrag = b;
        else if (k == "bigfish.tug") a.bigAutoTug = b;
        else if (k == "bigfish.stun") a.bigAutoStun = b;
        else if (k == "bigfish.success") a.bigForceSuccess = b;
        else if (k == "bigfish.zeroHp") a.bigZeroHp = b;
        else if (k == "bigfish.tugMs") a.bigTugIntervalMs = i;
        else if (k == "esp.enabled") Esp::setEnabled(b);
        else if (k == "esp.box") Esp::g_showBox = b;
        else if (k == "esp.name") Esp::g_showName = b;
        else if (k == "esp.distance") Esp::g_showDistance = b;
        else if (k == "esp.rangeOnly") Esp::g_onlyInRange = b;
        else if (k == "esp.maxDistance") Esp::g_maxDistance = f;
    }
    PF_LOG("[config] đã nạp %s", kFile);
}

} // namespace PF::SettingsStore
