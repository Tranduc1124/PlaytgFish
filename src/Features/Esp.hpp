#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PF::Esp {

// ---------------------------------------------------------------------
//  ESP vẽ khung người chơi khác. Toàn bộ class/method resolve THEO TÊN
//  từ metadata IL2CPP nên không cần offset cứng:
//    ActorSystem.Self                       (static)
//      -> get_OtherActorCharacter()          Dictionary<long, ActorCharacter>
//      -> get_Values() / GetEnumerator() / MoveNext() / get_Current()
//    ActorBase.get_Transform() -> Transform.get_position() -> Vector3
//    UnityEngine.Camera.get_main() -> WorldToScreenPoint(pos)
// ---------------------------------------------------------------------

struct Player {
    uint64_t id = 0;
    float x = 0, y = 0, z = 0;   // world position
    float dist = 0;               // khoảng cách tới camera
    float screenX = 0, screenY = 0;
    bool visible = false;         // trong khung hình
    bool onScreen = false;        // phía trước camera
    std::string name;             // nick lấy từ get_nickName() (nếu có)
};

bool setup();
bool ready();
void shutdown();

bool enabled();
void setEnabled(bool on);

// Bật ESP nếu cấu hình đã lưu yêu cầu bật (dùng lúc bootstrap, trả false nếu chưa bật)
bool setEnabledIfConfigured();

// Làm mới danh sách + tính toạ độ (gọi mỗi frame từ render thread)
void update();

// Vẽ bằng ImGui (gọi trong một window fullscreen, NoBackground)
void draw();

// Thống kê
size_t playerCount();
size_t visibleCount();
const char* lastError();

// Cấu hình (đọc trực tiếp trong draw)
extern bool g_showName;
extern bool g_showDistance;
extern bool g_showBox;
extern bool g_onlyInRange;
extern float g_maxDistance;

} // namespace PF::Esp
