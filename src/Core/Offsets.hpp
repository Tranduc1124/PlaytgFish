#pragma once

#include <string>
#include <vector>

namespace PF {

// ---------------------------------------------------------------------
//  Registry offset có TÊN. Thay vì hardcode số trong code, mỗi offset
//  được đăng ký kèm mô tả để:
//    - build/đổi version: sửa 1 chỗ
//    - GUI sửa trực tiếp + lưu ra file (không cần build lại)
//  Giá trị luôn là RVA tính từ base UnityFramework (0 = chưa biết).
// ---------------------------------------------------------------------
struct OffsetEntry {
    std::string name;        // vd "fishing.bite_check"
    uintptr_t value = 0;     // RVA
    std::string description; // ghi chú
    bool builtin = false;    // offset có sẵn trong source
};

class OffsetRegistry {
public:
    static OffsetRegistry& get();

    // Khai báo offset mặc định (gọi 1 lần lúc khởi động)
    void registerBuiltin(const std::string& name, const std::string& description);

    bool set(const std::string& name, uintptr_t value, const std::string& description = "");
    uintptr_t get(const std::string& name) const;
    bool has(const std::string& name) const;

    const std::vector<OffsetEntry>& entries() const { return m_entries; }

    // Đọc/ghi file (đường dẫn tự chọn: Documents của app)
    bool load();
    bool save() const;

    // Đường dẫn file hiện tại (để hiện trong GUI)
    std::string filePath() const;

private:
    OffsetEntry* find(const std::string& name);
    std::vector<OffsetEntry> m_entries;
};

} // namespace PF
