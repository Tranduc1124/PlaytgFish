#!/usr/bin/env python3
"""
inject_dylib.py — chèn LC_LOAD_DYLIB vào Mach-O (arm64 iOS) bằng Python thuần.

Vì sao không dùng insert_dylib/optool?
  -> Cả hai đều là tool macOS. Máy bạn chạy Windows/WSL nên không dùng được.
  Script này làm đúng việc đó bằng cấu trúc load command chuẩn của Mach-O:
  dùng khoảng trống giữa cuối load commands và vùng dữ liệu đầu tiên
  (segment đầu thường bắt đầu ở 0x1000/0x4000 nên luôn còn dư chỗ).

Cách dùng:
    python3 tools/inject_dylib.py <binary> "@executable_path/Frameworks/X.dylib"
    python3 tools/inject_dylib.py <binary> <path> --remove          # gỡ bản đang có
    python3 tools/inject_dylib.py <binary> --list                  # xem đang load gì

Yêu cầu: binary arm64 (MH_MAGIC_64), không cần thư viện ngoài.
"""
import argparse
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
LC_LOAD_DYLIB = 0x0C
LC_ID_DYLIB = 0x0D
LC_CODE_SIGNATURE = 0x1D
LC_SEGMENT_64 = 0x19


class MachO:
    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            self.data = bytearray(f.read())
        # mach_header_64: magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved
        (magic, _, _, _, self.ncmds, self.sizeofcmds, _, _) = struct.unpack_from(
            "<IiiIIIII", self.data, 0)
        if magic != MH_MAGIC_64:
            raise SystemExit(f"!! không phải Mach-O 64-bit: magic=0x{magic:x}")
        self.lc_end = 32 + self.sizeofcmds

    # ---- duyệt load commands ----
    def commands(self):
        off = 32
        for i in range(self.ncmds):
            cmd, cmdsize = struct.unpack_from("<II", self.data, off)
            yield i, off, cmd, cmdsize
            off += cmdsize

    def list_dylibs(self):
        out = []
        for _, off, cmd, cmdsize in self.commands():
            if cmd in (LC_LOAD_DYLIB, LC_ID_DYLIB):
                nameoff = struct.unpack_from("<I", self.data, off + 8)[0]
                end = self.data.index(b"\0", off + nameoff)
                out.append((self.data[off + nameoff:end].decode(errors="replace"), cmd))
        return out

    def first_data_offset(self) -> int:
        """Offset file nhỏ nhất mà dữ liệu THỰC SỰ bắt đầu (để biết chỗ trống).

        Không được dùng fileoff của segment: __TEXT luôn có fileoff=0 vì nó phủ
        luôn mach header (và vmaddr lại khác 0), nên cách đó ra số âm.
        Chỉ dùng offset của section đầu tiên có dữ liệu thật (thường __text ở 0x4000).
        """
        zerofill = (0x1, 0xC, 0x12)  # S_ZEROFILL, S_GB_ZEROFILL, S_THREAD_LOCAL_ZEROFILL
        best = len(self.data)
        for _, off, cmd, _ in self.commands():
            if cmd != LC_SEGMENT_64:
                continue
            nsects = struct.unpack_from("<I", self.data, off + 64)[0]
            for i in range(nsects):
                b = off + 72 + i * 80
                if b + 80 > len(self.data):
                    break
                size = struct.unpack_from("<Q", self.data, b + 40)[0]
                offset = struct.unpack_from("<I", self.data, b + 48)[0]
                flags = struct.unpack_from("<I", self.data, b + 64)[0]
                if size == 0 or offset == 0 or (flags & 0xFF) in zerofill:
                    continue
                best = min(best, offset)
        return best

    def remove_load_dylib(self, target: str) -> bool:
        """Xoá LC_LOAD_DYLIB trùng target (giữ nguyên thứ tự, zero-fill)."""
        removed = 0
        for _, off, cmd, cmdsize in list(self.commands()):
            if cmd != LC_LOAD_DYLIB:
                continue
            nameoff = struct.unpack_from("<I", self.data, off + 8)[0]
            end = self.data.index(b"\0", off + nameoff)
            name = self.data[off + nameoff:end].decode(errors="replace")
            if name == target:
                self.data[off:off + cmdsize] = b"\0" * cmdsize
                removed += 1
        if removed:
            # gộp các command rỗng: đặt lại ncmds/sizeofcmds
            newcmds = []
            off = 32
            for _ in range(self.ncmds):
                cmd, cmdsize = struct.unpack_from("<II", self.data, off)
                if cmdsize and any(self.data[off:off + cmdsize]):
                    newcmds.append((off, cmdsize))
                off += cmdsize
            # dồn các command còn lại về đầu vùng load commands
            packed = bytearray()
            for off, cmdsize in newcmds:
                packed += self.data[off:off + cmdsize]
            self.data[32:32 + self.sizeofcmds] = packed + b"\0" * (self.sizeofcmds - len(packed))
            self.ncmds = len(newcmds)
            self.sizeofcmds = len(packed)
            struct.pack_into("<II", self.data, 16, self.ncmds, self.sizeofcmds)
            with open(self.path, "wb") as f:
                f.write(self.data)
        return removed > 0

    def add_load_dylib(self, dylib_path: str) -> None:
        for name, cmd in self.list_dylibs():
            if name == dylib_path and cmd == LC_LOAD_DYLIB:
                print(f"[=] đã có sẵn: {dylib_path}")
                return

        path_bytes = dylib_path.encode() + b"\0"
        cmdsize = (24 + len(path_bytes) + 15) & ~15  # canh 16 byte
        first_data = self.first_data_offset()
        room = first_data - self.lc_end
        if room < cmdsize:
            raise SystemExit(
                f"!! không đủ chỗ trống để chèn (cần {cmdsize}, còn {room}).\n"
                f"   Binary quá chặt -> phải dùng insert_dylib trên macOS.")

        buf = bytearray(cmdsize)
        struct.pack_into("<II", buf, 0, LC_LOAD_DYLIB, cmdsize)
        struct.pack_into("<I", buf, 8, 24)              # dylib.name offset
        struct.pack_into("<III", buf, 12, 0, 0x10000, 0x10000)  # timestamp, cur, compat
        buf[24:24 + len(path_bytes)] = path_bytes

        self.data[self.lc_end:self.lc_end + cmdsize] = buf
        self.ncmds += 1
        self.sizeofcmds += cmdsize
        struct.pack_into("<II", self.data, 16, self.ncmds, self.sizeofcmds)
        with open(self.path, "wb") as f:
            f.write(self.data)
        print(f"[+] đã chèn LC_LOAD_DYLIB -> {dylib_path}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("dylib", nargs="?", help='vd "@executable_path/Frameworks/PlayFish.dylib"')
    ap.add_argument("--remove", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    m = MachO(a.binary)
    if a.list or not a.dylib:
        print(f"binary : {a.binary}")
        print(f"ncmds  : {m.ncmds}, sizeofcmds={m.sizeofcmds}, chỗ trống={m.first_data_offset() - m.lc_end}")
        for name, cmd in m.list_dylibs():
            kind = "ID_DYLIB" if cmd == LC_ID_DYLIB else "LOAD"
            print(f"  [{kind}] {name}")
        return 0

    if a.remove:
        ok = m.remove_load_dylib(a.dylib)
        print(("[+] đã gỡ " if ok else "[-] không tìm thấy ") + a.dylib)
        return 0 if ok else 1

    m.add_load_dylib(a.dylib)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
