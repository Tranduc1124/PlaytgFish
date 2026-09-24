#pragma once

// Chuyển địa chỉ trong FILE (như RVA/VA của Il2CppDumper) sang địa chỉ trong RAM.
// Cải tiến từ tphat/Helper/Mem.h (bản đó chỉ cộng slide, chỉ đúng khi file offset
// trùng vmaddr); bản này duyệt segment thật nên đúng với mọi layout Mach-O.

#include <cstdint>
#include <cstring>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>

#include "log.hpp"
#include "target.hpp"

namespace PF {

// image load address (Mach-O header)
inline uintptr_t imageHeader(const char* imageName) {
    return imageBase(imageName);
}

// Chuyển file address (RVA/VA trong file) -> runtime address
inline void* fileToRuntime(const char* imageName, uintptr_t fileAddress) {
    const uintptr_t header = imageBase(imageName);
    if (!header) return nullptr;

    const auto* mach = reinterpret_cast<const mach_header*>(header);
    if (mach->magic != MH_MAGIC_64) return nullptr;

    const auto* load = reinterpret_cast<const load_command*>(mach + 1);
    const uint32_t ncmds = mach->ncmds;

    for (uint32_t i = 0; i < ncmds; ++i) {
        if (load[i].cmd == LC_SEGMENT_64) {
            const auto* seg = reinterpret_cast<const segment_command_64*>(&load[i]);
            const uintptr_t segStart = seg->vmaddr;
            const uintptr_t segEnd = seg->vmaddr + seg->vmsize;
            if (fileAddress >= segStart && fileAddress < segEnd) {
                return reinterpret_cast<void*>(header + (fileAddress - seg->vmaddr +
                                                           (seg->fileoff - seg->fileoff)));
            }
        }
    }

    // fallback: coi như RVA (phần lớn trường hợp)
    return reinterpret_cast<void*>(header + fileAddress);
}

// Ngược lại: runtime address -> file address (để ghi vào bảng offset)
inline uintptr_t runtimeToFile(const char* imageName, const void* runtimeAddress) {
    const uintptr_t header = imageBase(imageName);
    if (!header) return 0;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(runtimeAddress);
    if (addr < header) return 0;
    return addr - header; // giả định RVA (phần lớn trường hợp với __TEXT)
}

} // namespace PF
