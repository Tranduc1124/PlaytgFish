#pragma once

#include <string>

namespace PF::SettingsStore {

// Lưu/đọc cấu hình (bật/tắt tính năng) vào Documents để vào game là có sẵn,
// không phải bật lại mỗi lần. Gọi tự động khi GUI đóng menu.
void save();
void load();
std::string path();

} // namespace PF::SettingsStore
