#pragma once

namespace PF::Touch {

// Cài touch input cho ImGui: hook touches* của view game (UnityView) để
// chuyển chạm trên màn hình thành chuột ImGui, đồng thời vẫn gửi tiếp
// event cho game (nên không ảnh hưởng điều khiển).
// Phải gọi trên main thread.
void install();

} // namespace PF::Touch
