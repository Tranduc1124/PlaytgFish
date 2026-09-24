#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <mach/mach.h>
#include <unistd.h>

#include "log.hpp"
#include "target.hpp"

namespace PF {

// Ghi byte tuỳ ý vào vùng code đang execute.
// Dùng vm_protect (có sẵn trong SDK) thay vì mach_vm_protect.
// Phải page-align vì vm_protect yêu cầu địa chỉ nằm trong 1 trang.
inline bool writeBytes(void* address, const void* bytes, size_t len) {
    if (!address || !bytes || len == 0) return false;

    const vm_size_t pageSize = static_cast<vm_size_t>(getpagesize());
    const vm_address_t start = reinterpret_cast<vm_address_t>(address) & ~(pageSize - 1);
    const vm_address_t end = (reinterpret_cast<vm_address_t>(address) + len + pageSize - 1) & ~(pageSize - 1);
    const vm_size_t range = end - start;

    kern_return_t kr = vm_protect(mach_task_self(), start, range, false,
                                  VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        PF_LOG("[patch] vm_protect failed (%d) @ %p", kr, address);
        return false;
    }
    std::memcpy(address, bytes, len);
    return true;
}

// NOP arm64 = 0xD503201F
inline constexpr uint32_t kArm64Nop = 0xD503201F;

struct PatchRecord {
    void* address = nullptr;
    std::vector<uint8_t> original;
};

inline std::vector<PatchRecord>& patchRecords() {
    static std::vector<PatchRecord> records;
    return records;
}

// Patch có ghi lại byte gốc để restore được
inline bool patch(void* address, const void* bytes, size_t len) {
    if (!address || !bytes || len == 0) return false;
    if (patchRecords().size() < 256) {
        PatchRecord rec;
        rec.address = address;
        const auto* p = reinterpret_cast<const uint8_t*>(address);
        rec.original.assign(p, p + len);
        patchRecords().push_back(std::move(rec));
    }
    if (!writeBytes(address, bytes, len)) return false;
    PF_LOG("[patch] %p <- %zu bytes", address, len);
    return true;
}

inline bool patchRva(uintptr_t offset, const void* bytes, size_t len, const char* image = kGameImage) {
    if (offset == 0) {
        PF_LOG("[patch] rva = 0, chưa điền offset (%s)", image);
        return false;
    }
    return patch(rva(offset, image), bytes, len);
}

inline bool nopRange(void* address, size_t count) {
    if (!address || count == 0) return false;
    std::vector<uint32_t> words(count, kArm64Nop);
    return patch(address, words.data(), words.size() * sizeof(uint32_t));
}

inline bool nopRva(uintptr_t offset, size_t count) {
    if (offset == 0) return false;
    return nopRange(rva(offset), count);
}

inline void restoreAllPatches() {
    for (const auto& rec : patchRecords()) {
        if (rec.address && !rec.original.empty())
            writeBytes(rec.address, rec.original.data(), rec.original.size());
    }
    size_t n = patchRecords().size();
    patchRecords().clear();
    PF_LOG("[patch] restored %zu patch(es)", n);
}

// ---------------------------------------------------------------------
//  Đọc / ghi giá trị (float/int/bool) trong struct của game
// ---------------------------------------------------------------------
template <typename T>
inline T read(void* address) {
    T v{};
    if (address) std::memcpy(&v, address, sizeof(T));
    return v;
}

template <typename T>
inline bool write(void* address, T value) {
    if (!address) return false;
    return writeBytes(address, &value, sizeof(T));
}

} // namespace PF
