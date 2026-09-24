#pragma once

namespace PF {

// Toàn bộ setting runtime của dylib. Đổi giá trị ở đây = đổi default,
// người chơi vẫn toggle trong GUI (GUI ghi đè vào đây).
struct Config {
    // --- GUI ---
    bool showMenu      = true;
    bool showLog       = true;

    // --- Auto fishing (bật sau khi điền offset/signature) ---
    bool autoFish      = false; // chế độ tự câu
    bool instantBite   = false; // cá cắn ngay, bỏ chờ nibble
    bool perfectReel   = false; // ép thắng minigame re-el
    bool noCooldown    = false; // bỏ delay giữa 2 lần câu
    bool onlyRare      = false; // chỉ nhận cá hiếm

    // --- Debug ---
    bool drawOverlay   = true; // vẽ GUI trong game
    bool verboseLog    = true;
};

extern Config g_cfg;

} // namespace PF
