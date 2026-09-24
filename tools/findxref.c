/*
 * findxref.c — quét nhanh các lệnh BL/B trong Mach-O arm64 để tìm "ai gọi hàm này".
 *
 * Dùng khi script.json quá lớn / ổ đĩa chậm nên quét bằng Python quá lâu.
 * Cách dùng:
 *     clang -O2 -o findxref findxref.c
 *     ./findxref <binary> <start_hex> <size_hex> <target_rva_hex>
 *
 * In ra mỗi địa chỉ lệnh BL trỏ tới target (mỗi dòng 1 số hex).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <binary> <start_hex> <size_hex> <target_hex>\n", argv[0]);
        return 2;
    }
    const char *bin = argv[1];
    uint64_t start = strtoull(argv[2], NULL, 16);
    uint64_t size = strtoull(argv[3], NULL, 16);
    uint64_t target = strtoull(argv[4], NULL, 16);

    FILE *f = fopen(bin, "rb");
    if (!f) { perror("fopen"); return 1; }
    if (fseek(f, (long)start, SEEK_SET) != 0) { perror("fseek"); return 1; }

    /* đọc theo khối để không tốn RAM */
    enum { CHUNK = 1 << 20 };               /* 1MB */
    static uint8_t buf[CHUNK + 4];          /* +4 để giữ 4 byte dư */
    uint64_t pos = start;
    uint64_t left = size;
    int carry = 0;                          /* byte dư từ khối trước */

    while (left > 0) {
        size_t want = left < CHUNK ? (size_t)left : CHUNK;
        size_t got = fread(buf + carry, 1, want, f);
        if (got == 0) break;
        size_t total = carry + got;
        size_t n = total / 4 * 4;
        for (size_t i = 0; i < n; i += 4) {
            uint32_t w = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) |
                         ((uint32_t)buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
            if ((w & 0xFC000000u) != 0x94000000u) continue; /* BL */
            int32_t imm = (int32_t)(w & 0x03FFFFFFu);
            if (imm & 0x02000000) imm -= 0x04000000;       /* sign extend */
            uint64_t pc = pos + i;
            if ((uint64_t)((int64_t)pc + ((int64_t)imm << 2)) == target)
                printf("0x%llx\n", (unsigned long long)pc);
        }
        carry = total - n;
        memmove(buf, buf + n, carry);
        pos += n;
        left -= got;
    }
    fclose(f);
    return 0;
}
