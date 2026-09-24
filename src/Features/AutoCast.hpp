#pragma once

#include <cstdint>
#include <string>

namespace PF::AutoCast {

// =====================================================================
//  TỰ ĐỘNG CÂU — 2 NHÁNH LOGIC (dump.cs Play Together 2.31.0)
//
//  enum ActorDefaultControl.eFishingState
//   --- nhánh A: cá thường ---
//   None=0 Casting=1 Search=2 SearchResult=3 Idle=4 Hit=5
//   Fighting=6 Catch=7 Fail=8 Boast=9 Finish=10 CastingFail=11 Miss=12
//   --- nhánh B: CÁ LỚN / BÓNG CÁ (>=13) ---
//   BigFish_RaidEnter=13  RaidSync=14  Begin=15
//   Pumpin=16   Drag=17    Tug=18       Fighting=19
//   Catch=20    Miss=21    RaidFighting=22
//   StunBegin=23 Stun=24  StunRecovery=25
//
//  class ActorDefaultControlPlayer (callback từ server)
//    bool  StartFishing()                      RVA 0x36D8180
//    bool  UpdateFishingState(eFishingState)   RVA 0x36D91BC
//    void  OnClickFishing()                    RVA 0x36D7A3C
//    void  ReceiveCastingResult(bool,uint)     RVA 0x36DCFD0
//    void  ReceiveFishingBegin(bool,uint,bool,FishingFailType)  RVA 0x36DC44C
//    void  HitResult(FishingSystem.eHitState)  RVA 0x36DD0C0   (None=0 Hit=1 Fail=2)
//    void  ReceiveFishingTug(bool,bool,int,bool)
//    void  ReceiveStunHit(bool)
//    void  CatchResult(bool,uint,int,FishExtraData)  RVA 0x36DD284
//    void  Lift(bool)
//    void  FishingBite() / FishLeave() / FishingCancel()
//    void  PumpinFail() / DashBroken() / FishingStunHit()
//
//  class FishingSystem : PT_SystemBase
//    static FishingSystem get_Self()
//    void SetFishHP(int)                        <- hạ HP cá (kể cả cá lớn)
//    void RequestFishingTug(Action<bool,bool,int,bool>)
//    void RequestStunHit(Action<bool>)
//    void RequestFishingCatch(bool, Action<...>)
//
//  class FishingFloatController
//    bool get_IsShadowFishReady()               <- trạng thái bóng cá sẵn sàng
//    bool get_IsHit() / get_IsDragAlert()
//    bool UpdatePumpin() / UpdateTug() / UpdateStun()
//
//  class FishShadowController  (bóng cá)
//    Transform RootObj (0x70)  float Weight (0x80)  float BigFishOffset (0x7C)
// =====================================================================

enum class eFishingState : int {
    None = 0, Casting = 1, Search = 2, SearchResult = 3, Idle = 4, Hit = 5,
    Fighting = 6, Catch = 7, Fail = 8, Boast = 9, Finish = 10, CastingFail = 11,
    Miss = 12,
    BigFish_RaidEnter = 13, BigFish_RaidSync = 14, BigFish_Begin = 15,
    BigFish_Pumpin = 16, BigFish_Drag = 17, BigFish_Tug = 18, BigFish_Fighting = 19,
    BigFish_Catch = 20, BigFish_Miss = 21, BigFish_RaidFighting = 22,
    BigFish_StunBegin = 23, BigFish_Stun = 24, BigFish_StunRecovery = 25,
};

// Ngưỡng phân nhánh: >= 13 là cá lớn (bóng cá)
inline constexpr int kBigFishThreshold = 13;

// Nhóm cỡ cá (Fishlist.FishSizeGroup trong bảng data):
//   1..5 = cá bé  -> dùng nhánh A (state 0-12)
//   6..7 = cá to / quái -> dùng nhánh B (state >=13, BigFish_*)
// Người dùng có thể ép tay nhánh khi game không báo đúng state.
inline constexpr int kSmallShadowMin = 1;
inline constexpr int kSmallShadowMax = 5;
inline constexpr int kBigShadowMin = 6;
inline constexpr int kBigShadowMax = 7;

enum class ShadowTier {
    Unknown = 0,
    Small = 1, // bóng 1-5
    Big = 2,   // bóng 6-7
};

struct Settings {
    bool enabled = false;

    // --- nhánh A: cá thường (bóng 1-5) ---
    bool autoCast = true;         // tự quăng câu
    bool autoBite = true;         // tự kích hoạt cắn
    bool autoTug = true;          // tự gửi yêu cầu kéo (tug)
    int tugIntervalMs = 120;

    // --- nhánh B: cá lớn / bóng 6-7 ---
    bool bigAutoCast = true;      // tự quăng câu sau khi xong màn cá lớn
    bool bigAutoDrag = true;      // tự LÔI (RequestFishingHit) — bước bắt buộc của bóng 6-7
    bool bigAutoTug = true;       // tự tug minigame cá lớn
    int bigTugIntervalMs = 150;
    bool bigAutoStun = true;      // tự gửi yêu cầu stun

    // Lôi theo vị trí float đang câu (lấy từ FishingFloatController.get_Transform)
    // thay vì điểm (0,0,0).
    bool bigDragUseFloatPos = true;

    // --- CHỈ tự động thao tác, KHÔNG ép kết quả ---
    // (game/server tự quyết định thành công hay thất bại)

    // --- chọn nhánh thủ công (0 = tự nhận từ state) ---
    // 1 = ép nhánh cá bé (1-5), 2 = ép nhánh cá to (6-7)
    int forceTier = 0;

    int castIntervalMs = 1500;
};

Settings& settings();

// ---------------------------------------------------------------------
//  Bootstrap tự động: thử resolve liên tục tới khi thành công
//  (được gọi nền từ Gui::startup — người dùng không cần bấm gì)
// ---------------------------------------------------------------------
void bootstrap();          // bắt đầu vòng thử nền
bool ready();              // đã hook xong
void shutdown();

void tick();               // chạy logic (mỗi frame, render thread)

// ---------------------------------------------------------------------
//  Trạng thái (GUI hiển thị)
// ---------------------------------------------------------------------
struct Status {
    bool controlResolved = false;
    bool systemResolved = false;
    void* control = nullptr;
    int currentState = 0;
    bool isBigFish = false;
    ShadowTier tier = ShadowTier::Unknown; // nhóm bóng: 1-5 bé / 6-7 to
    uint64_t casts = 0, bites = 0, tugs = 0, stuns = 0;
    uint64_t bigDrag = 0, bigTug = 0;
    char note[96] = "";
};

const Status& status();

} // namespace PF::AutoCast
