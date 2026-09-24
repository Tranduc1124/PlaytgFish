#!/usr/bin/env python3
"""
ida_lite.py — đọc logic IL2CPP kiểu IDA, không đoán.

Nguồn dữ liệu: script.json của Il2CppDumper (mỗi method có Address + Name +
Signature C thật) và chính binary UnityFramework (đã decrypt).

Cách dùng:
    # 1) xây index (chạy 1 lần, script.json 222MB)
    python3 tools/ida_lite.py index <script.json> <index.txt>

    # 2) tìm hàm
    python3 tools/ida_lite.py find <index.txt> "FishingSystem$$RequestFishingTug"

    # 3) đọc code của hàm (disassemble + chú thích tên hàm được gọi)
    python3 tools/ida_lite.py dis <index.txt> <UnityFramework> "FishingSystem$$SetFishHP" 40

    # 4) liệt kê mọi hàm trong 1 class
    python3 tools/ida_lite.py cls <index.txt> "FishingFloatController"
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys

# --------------------------------------------------------------- index
def build_index(script_json: str, out_txt: str) -> None:
    """Đọc script.json (có thể 200MB+) bằng cách bơm từng entry ScriptMethod."""
    size = os.path.getsize(script_json) // (1 << 20)
    print(f"[*] đọc {script_json} (~{size}MB)", file=sys.stderr)
    n = 0
    tmp = out_txt + ".tmp"
    pat_addr = re.compile(r'"Address"\s*:\s*(-?\d+)')
    pat_name = re.compile(r'"Name"\s*:\s*"((?:[^"\\]|\\.)*)"')
    pat_sig = re.compile(r'"Signature"\s*:\s*"((?:[^"\\]|\\.)*)"')

    with open(script_json, "r", encoding="utf-8", errors="replace") as f, \
         open(tmp, "w", encoding="utf-8") as out:
        in_methods = False
        cur_addr = None
        cur_name = None
        cur_sig = ""
        depth = 0
        for line in f:
            if not in_methods:
                if '"ScriptMethod"' in line:
                    in_methods = True
                    cur_addr, cur_name, cur_sig = None, None, ""
                    depth = line.count("{") - line.count("}")
                continue

            if cur_addr is None:
                m = pat_addr.search(line)
                if m:
                    cur_addr = int(m.group(1))
            if cur_name is None:
                m = pat_name.search(line)
                if m:
                    cur_name = m.group(1)
            if not cur_sig:
                m = pat_sig.search(line)
                if m:
                    cur_sig = m.group(1)

            depth += line.count("{") - line.count("}")
            if depth <= 0 and cur_addr is not None and cur_name is not None:
                out.write(f"{cur_addr:x}\t{cur_name}\t{cur_sig}\n")
                n += 1
                cur_addr, cur_name, cur_sig = None, None, ""
                depth = 0
            if ']' in line and depth <= 0:
                in_methods = False
    os.replace(tmp, out_txt)
    print(f"[+] ghi {n} method vào {out_txt}", file=sys.stderr)


def load_index(path: str):
    idx = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 2:
                continue
            try:
                a = int(parts[0], 16)
            except ValueError:
                continue
            idx[a] = (parts[1], parts[2] if len(parts) > 2 else "")
    return idx


# --------------------------------------------------------------- find
def cmd_find(idx_path: str, pattern: str) -> None:
    idx = load_index(idx_path)
    pat = pattern.lower()
    hits = [(a, n, s) for a, (n, s) in idx.items() if pat in n.lower()]
    hits.sort()
    for a, n, s in hits[:400]:
        print(f"0x{a:x}  {n}")
        if s:
            print(f"          {s}")
    print(f"[=] {len(hits)} kết quả", file=sys.stderr)


def cmd_cls(idx_path: str, cls_name: str) -> None:
    idx = load_index(idx_path)
    pat = cls_name.lower()
    hits = sorted((a, n, s) for a, (n, s) in idx.items() if n.lower().startswith(pat))
    for a, n, s in hits:
        print(f"0x{a:x}  {n}")
    print(f"[=] {len(hits)} method", file=sys.stderr)


# --------------------------------------------------------------- disasm
BRANCH = re.compile(r"\b(bl|b|adrp|adr)\b\s+(0x[0-9a-f]+|\#?0x[0-9a-f]+)")
ADDR_RE = re.compile(r"^\s*([0-9a-f]+):\s")

def _decode_branches(data: bytes, base: int):
    """Trả về dict {địa chỉ lệnh: đích branch} cho lệnh BL/B của ARM64."""
    out = {}
    for i in range(0, len(data) - 3, 4):
        w = int.from_bytes(data[i:i + 4], "little")
        op = (w >> 26) & 0x3F
        if op in (0x25, 0x05):  # BL (100101) hoặc B (000101)
            imm = w & 0x03FFFFFF
            if imm & 0x02000000:
                imm -= 0x04000000
            out[base + i] = base + i + (imm << 2)
    return out


def cmd_dis(idx_path: str, binary: str, pattern: str, count: int, exact: bool = False) -> None:
    idx = load_index(idx_path)
    if exact:
        hits = [(a, n, s) for a, (n, s) in idx.items() if n == pattern]
    else:
        pat = pattern.lower()
        hits = [(a, n, s) for a, (n, s) in idx.items() if pat in n.lower()]
    hits = sorted(hits)
    if not hits:
        print("không tìm thấy hàm", file=sys.stderr)
        return
    addr, name, sig = hits[0]
    print(f"// {name}\n// {sig}\n// RVA 0x{addr:x}   ({len(hits)} hàm khớp)\n", file=sys.stderr)

    # __TEXT: vmaddr 0 / fileoff 0  =>  RVA == file offset
    nbytes = max(64, count * 4)
    tmp = "/tmp/pf_dis.bin"
    try:
        subprocess.run(["dd", f"if={binary}", "of=" + tmp, "bs=1",
                        f"skip={addr}", f"count={nbytes}", "status=none"],
                       check=True, timeout=180)
        with open(tmp, "rb") as f:
            data = f.read()
    except Exception as e:  # noqa
        print(f"đọc binary lỗi: {e}", file=sys.stderr)
        return

    # llvm-mc giải mã, nhận byte dạng "0xAA 0xBB ..."
    hexbytes = " ".join(f"0x{b:02x}" for b in data)
    try:
        asm = subprocess.run(
            ["llvm-mc", "-disassemble", "--triple=aarch64-apple-ios"],
            input=hexbytes, capture_output=True, text=True, errors="replace",
            timeout=180).stdout.splitlines()
    except Exception as e:  # noqa
        print(f"disassemble lỗi: {e}", file=sys.stderr)
        return

    branches = _decode_branches(data, addr)
    for i, ins in enumerate(asm):
        pc = addr + i * 4
        note = ""
        if pc in branches:
            t = branches[pc]
            nm = idx.get(t) or idx.get(t & ~0x3)
            note = f"   ; <{nm[0]}>" if nm else f"   ; -> 0x{t:x}"
        print(f"0x{pc:x}: {ins.strip()}{note}")


def _disasm_at(binary: str, addr: int, count: int, idx: dict) -> None:
    """Giải mã `count` lệnh ARM64 tại RVA `addr` và chú thích tên hàm được gọi."""
    nbytes = max(64, count * 4)
    tmp = "/tmp/pf_dis.bin"
    try:
        subprocess.run(["dd", f"if={binary}", "of=" + tmp, "bs=1",
                        f"skip={addr}", f"count={nbytes}", "status=none"],
                       check=True, timeout=180)
        with open(tmp, "rb") as f:
            data = f.read()
    except Exception as e:  # noqa
        print(f"đọc binary lỗi: {e}", file=sys.stderr)
        return

    hexbytes = " ".join(f"0x{b:02x}" for b in data)
    try:
        asm = subprocess.run(
            ["llvm-mc", "-disassemble", "--triple=aarch64-apple-ios"],
            input=hexbytes, capture_output=True, text=True, errors="replace",
            timeout=180).stdout.splitlines()
    except Exception as e:  # noqa
        print(f"disassemble lỗi: {e}", file=sys.stderr)
        return

    branches = _decode_branches(data, addr)
    for i, ins in enumerate(asm):
        pc = addr + i * 4
        note = ""
        if pc in branches:
            t = branches[pc]
            hit = idx.get(t) or idx.get(t & ~0x3)
            note = f"   ; <{hit[0]}>" if hit else f"   ; -> 0x{t:x}"
        print(f"0x{pc:x}: {ins.strip()}{note}")


def cmd_disat(idx_path: str, binary: str, hexaddr: str, count: int) -> None:
    """Disassemble theo RVA (tránh mọi vấn đề quoting với tên hàm chứa $$)."""
    idx = load_index(idx_path)
    addr = int(hexaddr, 16)
    nm, sig = idx.get(addr, ("<unknown>", ""))
    print(f"// {nm}\n// {sig}\n// RVA 0x{addr:x}\n", file=sys.stderr)
    _disasm_at(binary, addr, count, idx)


def cmd_xrefs(idx_path: str, binary: str, hexaddr: str) -> None:
    """Quét toàn bộ __text + __il2cpp tìm lệnh BL trỏ tới RVA mục tiêu (xref)."""
    idx = load_index(idx_path)
    target = int(hexaddr, 16)
    # vùng mã: __text 0x4000+0x29cc160, __il2cpp 0x29d0160+0x7c33828
    regions = [(0x4000, 0x29CC160), (0x29D0160, 0x7C33828)]
    tmp = "/tmp/pf_scan.bin"
    hits = []
    for start, size in regions:
        try:
            subprocess.run(["dd", f"if={binary}", "of=" + tmp, "bs=1",
                            f"skip={start}", f"count={size}", "status=none"],
                           check=True, timeout=900)
        except Exception as e:  # noqa
            print(f"đọc vùng 0x{start:x} lỗi: {e}", file=sys.stderr)
            continue
        with open(tmp, "rb") as f:
            data = f.read()
        # quét nhanh bằng struct.iter_unpack (lặp ở tầng C)
        pos = 0
        for (w,) in struct.iter_unpack("<I", data):
            if w & 0xFC000000 == 0x94000000:  # BL
                imm = w & 0x03FFFFFF
                if imm & 0x02000000:
                    imm -= 0x04000000
                pc = start + pos
                if pc + (imm << 2) == target:
                    hits.append(pc)
            pos += 4
    hits.sort()
    print(f"// {len(hits)} lời gọi (BL) tới 0x{target:x}")
    for pc in hits:
        nm, sig = idx.get(pc, ("<unknown>", ""))
        print(f"0x{pc:x}  được gọi từ: {nm}")
        if sig:
            print(f"        {sig[:150]}")


def cmd_owner(idx_path: str, hexaddr: str) -> None:
    """Tra cụ thể: địa chỉ X nằm trong hàm nào (hàm có address gần nhất & <= X)."""
    idx = load_index(idx_path)
    addr = int(hexaddr, 16)
    addrs = sorted(idx)
    lo = None
    for a in addrs:
        if a <= addr:
            lo = a
        else:
            break
    if lo is None:
        print("trước địa chỉ đầu tiên trong index", file=sys.stderr)
        return
    hi = next((a for a in addrs if a > lo), None)
    nm, sig = idx[lo]
    print(f"0x{addr:x} nằm trong: {nm}")
    print(f"  bắt đầu 0x{lo:x}" + (f", hàm kế tiếp 0x{hi:x} (dài ~0x{hi-lo:x} byte)" if hi else ""))
    if sig:
        print(f"  {sig}")


# --------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("index")
    p.add_argument("script_json")
    p.add_argument("out")

    p = sub.add_parser("find")
    p.add_argument("index")
    p.add_argument("pattern")

    p = sub.add_parser("cls")
    p.add_argument("index")
    p.add_argument("class_name")

    p = sub.add_parser("dis")
    p.add_argument("index")
    p.add_argument("binary")
    p.add_argument("pattern")
    p.add_argument("count", nargs="?", type=int, default=30)
    p.add_argument("--exact", action="store_true")

    p = sub.add_parser("disat")
    p.add_argument("index")
    p.add_argument("binary")
    p.add_argument("hexaddr")
    p.add_argument("count", nargs="?", type=int, default=40)

    p = sub.add_parser("xrefs")
    p.add_argument("index")
    p.add_argument("binary")
    p.add_argument("hexaddr")

    p = sub.add_parser("owner")
    p.add_argument("index")
    p.add_argument("hexaddr")

    a = ap.parse_args()
    if a.cmd == "index":
        build_index(a.script_json, a.out)
    elif a.cmd == "find":
        cmd_find(a.index, a.pattern)
    elif a.cmd == "cls":
        cmd_cls(a.index, a.class_name)
    elif a.cmd == "dis":
        cmd_dis(a.index, a.binary, a.pattern, a.count, a.exact)
    elif a.cmd == "disat":
        cmd_disat(a.index, a.binary, a.hexaddr, a.count)
    elif a.cmd == "xrefs":
        cmd_xrefs(a.index, a.binary, a.hexaddr)
    elif a.cmd == "owner":
        cmd_owner(a.index, a.hexaddr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
