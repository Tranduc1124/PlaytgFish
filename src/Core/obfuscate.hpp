#pragma once

// Giấu chuỗi literal khỏi binary (dựa trên ý tưởng oxorany trong tphat).
// Mục đích: trong `strings PlayFish.dylib` không thấy "ReceiveCastingResult",
// "ActorDefaultControlPlayer" ... (giảm khả năng bị phát hiện qua signature).
//
// Cách dùng:
//   auto s = OBF("FishingSystem");     // -> const char* giải mã lúc chạy
//   PF::il2cpp::findClass(s, "");

#include <cstdint>
#include <cstring>
#include <string>

namespace PF::obf {

constexpr uint32_t kKey = 0x9E3779B9u; // hằng số compile-time

// XOR chuỗi lúc compile: biến thành mảng byte mã hoá
class ObfString {
public:
    template <size_t N>
    constexpr ObfString(const char (&text)[N]) : m_len(N - 1) {
        for (size_t i = 0; i < N; ++i)
            m_buf[i] = static_cast<char>(text[i] ^ static_cast<char>((kKey >> ((i % 4) * 8)) & 0xFF));
    }

    // Giải mã (cache 1 lần)
    const char* c_str() const {
        if (!m_ready) {
            for (size_t i = 0; i < m_len; ++i)
                m_buf[i] = static_cast<char>(m_buf[i] ^ static_cast<char>((kKey >> ((i % 4) * 8)) & 0xFF));
            m_buf[m_len] = '\0';
            m_ready = true;
        }
        return m_buf;
    }

    operator const char*() const { return c_str(); }

    std::string str() const { return std::string(c_str(), m_len); }

private:
    mutable char m_buf[256] = {};
    mutable size_t m_len = 0;
    mutable bool m_ready = false;
};

} // namespace PF::obf

#define OBF(text) (::PF::obf::ObfString(text).c_str())
