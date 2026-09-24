#pragma once

#include <csignal>
#include <csetjmp>
#include <cstring>

#include "log.hpp"

// Bọc đoạn code resolve/hook: nếu bên trong gọi hàm của game mà game đó
// lỗi (ABI đổi, con trỏ rác…) thì bắt lại SIGSEGV/SIGBUS, ghi log, rồi bỏ qua
// — thay vì để app bị kill.
//
// CẢNH BÁO: chỉ dùng quanh phần resolve (chạy 1 lần lúc bootstrap).
// Không bọc code gameplay/hook vì state của game có thể đã bị hỏng.
namespace PF {

struct CrashGuard {
    static sigjmp_buf jmp;
    static volatile sig_atomic_t armed;
    static struct sigaction oldSegv;
    static struct sigaction oldBus;

    static void handler(int sig) {
        if (armed) {
            armed = 0;
            siglongjmp(jmp, sig);
        }
        // ngoài vùng guard: để hệ thống xử lý như bình thường
        signal(sig, SIG_DFL);
        raise(sig);
    }

    // Bật guard. Trả về false nghĩa là đã bị crash nhảy ra (xem signal()).
    static bool run(void (*fn)(), const char* label) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        armed = 1;
        sigaction(SIGSEGV, &sa, &oldSegv);
        sigaction(SIGBUS, &sa, &oldBus);

        if (sigsetjmp(jmp, 1) == 0) {
            fn();
            armed = 0;
            sigaction(SIGSEGV, &oldSegv, nullptr);
            sigaction(SIGBUS, &oldBus, nullptr);
            return true;
        }

        sigaction(SIGSEGV, &oldSegv, nullptr);
        sigaction(SIGBUS, &oldBus, nullptr);
        PF_LOG("[guard] %s: bắt được SIGSEGV trong code game, đã bỏ qua", label);
        return false;
    }
};

inline sigjmp_buf CrashGuard::jmp;
inline volatile sig_atomic_t CrashGuard::armed = 0;
inline struct sigaction CrashGuard::oldSegv;
inline struct sigaction CrashGuard::oldBus;

} // namespace PF
