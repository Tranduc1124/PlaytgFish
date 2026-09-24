#pragma once

namespace PF::Gesture {

// Cài gesture mở/đóng menu trong game mà không cần PC:
//   - chạm 3 ngón          : toggle menu
//   - chạm 2 ngón 2 lần    : toggle menu (dự phòng)
// Phải gọi trên main thread.
void install();

} // namespace PF::Gesture
