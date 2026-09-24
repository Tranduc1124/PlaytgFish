#include "FeatureManager.hpp"

#include "../Core/log.hpp"

namespace PF {

FeatureManager& FeatureManager::get() {
    static FeatureManager instance;
    return instance;
}

void FeatureManager::add(Feature* feature) {
    if (!feature) return;
    m_features.push_back(feature);
    PF_LOG("[feature] registered '%s'", feature->name());
}

Feature* FeatureManager::find(const char* name) const {
    if (!name) return nullptr;
    for (Feature* f : m_features)
        if (f && std::string(f->name()) == name) return f;
    return nullptr;
}

bool FeatureManager::install(const char* name) {
    Feature* f = find(name);
    if (!f) {
        PF_LOG("[feature] '%s' not found", name);
        return false;
    }
    if (f->installed()) return true;
    const bool ok = f->install();
    PF_LOG("[feature] %s '%s' -> %s", ok ? "install" : "FAILED", name, ok ? "OK" : "FAIL");
    return ok;
}

void FeatureManager::uninstall(const char* name) {
    Feature* f = find(name);
    if (!f || !f->installed()) return;
    f->uninstall();
    PF_LOG("[feature] uninstall '%s'", name);
}

bool FeatureManager::isInstalled(const char* name) const {
    Feature* f = find(name);
    return f && f->installed();
}

void FeatureManager::installAll() {
    for (Feature* f : m_features)
        if (f) install(f->name());
}

void FeatureManager::uninstallAll() {
    for (Feature* f : m_features)
        if (f) uninstall(f->name());
}

} // namespace PF
