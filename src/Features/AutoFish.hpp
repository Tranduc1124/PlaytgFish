#pragma once

namespace PF::AutoFish {

// Cài các hook liên quan tới câu cá.
void install();

// Bật/tắt logic tự câu (được GUI gọi khi đổi setting).
void setEnabled(bool on);

bool enabled();

} // namespace PF::AutoFish
