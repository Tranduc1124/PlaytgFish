#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PF::FishingHooks {

// =====================================================================
//  Hook ĐÚNG CHỮ KÝ, lấy từ dump.cs Play Together 2.31.0
//  (class: ActorDefaultControlPlayer, namespace: rỗng)
//
//  private void ReceiveCastingResult(bool castSuccess, uint difficultyLevel)   RVA 0x36DCFD0
//  private void ReceiveFishingBegin(bool castSuccess, uint difficultyLevel,
//                                   bool isRaid, FishingFailType failType)      RVA 0x36DC44C
//  private void CatchResult(bool success, uint rewardItemId, int size,
//                           FishExtraData options)                              RVA 0x36DD284
//  private void HitResult(FishingSystem.eHitState hitState)                    RVA 0x36DD0C0
//  public  override void FishingBite()                                          RVA 0x36DD3E0
//  public  override void FishLeave()                                            RVA 0x36DD3F4
//
//  FishingSystem.eHitState: None=0, Hit=1, Fail=2
// =====================================================================

enum class HookId {
    ReceiveCastingResult = 0, // ép thành công
    ReceiveFishingBegin,       // ép thành công + failType=0
    CatchResult,               // ép success=true
    HitResult,                 // ép Hit=1
    FishingBite,               // tự gọi để giả vờ cá cắn
    FishLeave,                 // giữ lại (chỉ quan sát)
    Count
};

struct HookState {
    bool installed = false;
    void* target = nullptr;
    uint64_t calls = 0;
    bool lastValue = false; // giá trị bool cuối thấy (để đối chiếu)
};

bool setup();  // resolve theo tên (chính) + fallback theo RVA
bool ready();
void shutdown();

void setEnabled(bool on);
bool enabled();

// Thống kê + cấu hình
const HookState& state(HookId id);
const char* name(HookId id);
void setHookEnabled(HookId id, bool on);
bool hookEnabled(HookId id);

} // namespace PF::FishingHooks
