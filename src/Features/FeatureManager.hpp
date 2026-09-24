#pragma once

#include <string>
#include <vector>

namespace PF {

// ---------------------------------------------------------------------
//  Mỗi tính năng = 1 đối tượng tự cài/gỡ hook của riêng nó.
//  GUI chỉ bật/tắt qua FeatureManager, không đụng vào hook trực tiếp.
// ---------------------------------------------------------------------
class Feature {
public:
    virtual ~Feature() = default;

    virtual const char* name() const = 0;        // id nội bộ, ví dụ "auto_fish"
    virtual const char* title() const = 0;       // tên hiện trong GUI
    virtual const char* description() const { return ""; }

    virtual bool install() = 0;                  // cài hook/patch
    virtual void uninstall() = 0;                // gỡ hook, khôi phục patch
    virtual bool installed() const = 0;
};

class FeatureManager {
public:
    static FeatureManager& get();

    void add(Feature* feature);

    // Cài/gỡ theo tên. Trả false nếu không tìm thấy feature.
    bool install(const char* name);
    void uninstall(const char* name);
    bool isInstalled(const char* name) const;

    void installAll();
    void uninstallAll();

    Feature* find(const char* name) const;
    const std::vector<Feature*>& features() const { return m_features; }

private:
    std::vector<Feature*> m_features;
};

} // namespace PF
