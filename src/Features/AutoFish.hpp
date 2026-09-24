#pragma once

namespace PF::AutoFish {

// Bật/tắt nhóm tính năng tự câu.
// Khi bật: quét metadata IL2CPP tìm hàm liên quan câu cá, sau đó
//          cài override (ép giá trị trả về) hoặc patch offset tương ứng.
void setEnabled(bool on);

bool enabled();

// Cài lại từ đầu (dùng sau khi game load xong hoặc vừa đổi offset)
void install();

void uninstall();

} // namespace PF::AutoFish
