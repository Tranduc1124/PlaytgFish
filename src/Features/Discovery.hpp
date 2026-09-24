#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PF::Discovery {

// ---------------------------------------------------------------------
//  Quét metadata IL2CPP lúc runtime để tự tìm class/method liên quan
//  đến câu cá — không cần biết trước tên class, cũng không cần offset.
//  Kết quả dùng để điền FishingSpec hoặc tạo override trong GUI.
// ---------------------------------------------------------------------
struct Candidate {
    std::string nsp;
    std::string klass;
    std::string method;
    std::string image;
    void* fnptr = nullptr; // địa chỉ native (để hook)
    int score = 0;         // điểm khớp từ khoá
    const char* role = ""; // gợi ý: "bite" / "reel" / "cast" / "other"
};

// Từ khoá mặc định (đã bỏ namespace hệ thống)
inline constexpr const char* kClassKeywords = "fish,fishing,bite,nibble,reel,cast,rod,bait,hook,catch,aquarium";

// Quét và trả về danh sách ứng viên, sắp theo score giảm dần
std::vector<Candidate> scan(const char* keywords = kClassKeywords, size_t limit = 200);

// Chỉ lấy method của 1 class (dùng khi xem chi tiết trong GUI)
std::vector<Candidate> methodsOf(void* klass, const char* className, const char* nsp, size_t limit = 120);

} // namespace PF::Discovery
