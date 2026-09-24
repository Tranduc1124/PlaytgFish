#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PF::Overrides {

// ---------------------------------------------------------------------
//  ÉP giá trị trả về (hoặc chỉ quan sát) cho một hàm đã resolve được.
//  Đây là "logic" dùng được ngay mà không cần biết chữ ký C#:
//    - PassThrough : chỉ đếm lời gọi / xem return value
//    - ForceTrue   : luôn trả 1        (vd: CheckBite)
//    - ForceFalse  : luôn trả 0
//    - ForceValue  : luôn trả giá trị tuỳ chọn
//  Dùng prototype 8 tham số (AArch64 x0..x7). Method > 8 tham số có thể
//  lỗi vì tham số stack không được chuyển tiếp — khi đó dùng offset RVA
//  + detour riêng trong Features/.
// ---------------------------------------------------------------------
enum class Mode {
    PassThrough = 0,
    ForceTrue,
    ForceFalse,
    ForceValue,
};

struct Slot {
    bool active = false;
    void* target = nullptr;
    void* orig = nullptr;
    Mode mode = Mode::PassThrough;
    uintptr_t value = 0;
    std::string label;
    uint64_t calls = 0;
    uintptr_t lastReturn = 0;
    uintptr_t lastArg0 = 0;
};

constexpr size_t kMaxSlots = 8;

bool add(void* target, Mode mode, uintptr_t value, const std::string& label);

void removeAt(size_t index);

void clear();

std::vector<Slot> snapshot();

size_t count();

const char* modeName(Mode m);

} // namespace PF::Overrides
