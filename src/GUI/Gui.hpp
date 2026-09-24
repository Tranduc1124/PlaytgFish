#pragma once

namespace PF::Gui {

// ---------------------------------------------------------------------
//  HÀM KHỞI ĐỘNG GUI — điểm vào duy nhất cho toàn bộ phần giao diện.
//  main.mm chỉ cần gọi PF::Gui::startup().
// ---------------------------------------------------------------------

// Nạp config, đăng ký offset mặc định, cài gesture + overlay ImGui/Metal.
// Hàm này KHÔNG block; phần chờ game load nằm trong startup().
void startup();

// Dừng GUI và giải phóng tài nguyên ImGui.
void shutdown();

// true khi ImGui đã sẵn sàng vẽ (overlay đã nối được frame present của game).
bool ready();

// Số frame đã vẽ (tiện cho debug)
unsigned long long frameCount();

// Overlay gọi mỗi frame đã vẽ (nội bộ)
void noteFrame();

} // namespace PF::Gui
