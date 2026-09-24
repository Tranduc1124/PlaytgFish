#!/usr/bin/env python3
"""
Sinh lại file offset từ dump.cs của Il2CppDumper.

Dùng khi game cập nhật và tên class/method ĐỔI (lúc đó resolve theo tên
không còn tác dụng, phải lấy RVA mới từ dump).

    python3 tools/gen_offsets.py <path/to/dump.cs> [--out src/Core/generated_offsets.hpp]

Output: bảng RVA của các class/method liên quan câu cá + field offset.
Sau đó build lại; các giá trị này được load vào OffsetRegistry lúc chạy
(file playfish_offsets.txt trong Documents) hoặc dùng trực tiếp.
"""
import argparse
import re
import sys
from pathlib import Path

# class -> các method cần lấy RVA (lấy từ dump.cs 2.31.0)
WANT_METHODS = {
    # CHỦ SỞ HỮU các callback kết quả câu cá (đây là điểm hook thật)
    "ActorDefaultControlPlayer": [
        "StartFishing",
        "UpdateFishingState",
        "ReceiveFishingBegin",
        "ReceiveCastingResult",
        "HitResult",
        "CatchResult",
        "FishingBite",
        "FishLeave",
        "CastingFailChat",
    ],
    # Lớp hệ thống (API Request/Receive tương ứng phía trên)
    "FishingSystem": ["get_Self", "ResetFishing", "ReceiveFishingBegin", "ReceiveCastingResult"],
    "ActorSystem": ["get_MyActorCharacter", "get_OtherActorCharacter"],
    "FishingFloatController": ["Update", "UpdatePumpin", "UpdateTug", "UpdateStun"],
    "HeadUpFishGauge": ["Update"],
}

# field boolean hay dùng để ép trong FishingSystem
WANT_FIELDS = {
    "FishingSystem": ["AutoFishing", "AutoFishing2", "IsFishing", "FishingMiss",
                      "MiniGameUseTimer", "TestSpecialFishing"],
    "ActorSystem": ["blockOtherUpdate"],
}

RE_CLASS = re.compile(r"^\s*(?:public|internal|private|protected)?\s*(?:sealed\s+)?class\s+(\w+)")
RE_METHOD = re.compile(r"^\s*(?:public|private|protected|internal).*?\s(\w+)\s*\(")
RE_FIELD = re.compile(r"^\s*(?:public|private|protected|internal)\s+[\w<>\[\],\.\?]+\s+(\w+)\s*;\s*//\s*(0x[0-9a-fA-F]+)")
RE_RVA = re.compile(r"RVA:\s*(0x[0-9a-fA-F]+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", help="đường dẫn tới dump.cs")
    ap.add_argument("--out", default=None, help="file header C++ đầu ra")
    args = ap.parse_args()

    dump_path = Path(args.dump)
    if not dump_path.is_file():
        print(f"!! không tìm thấy {dump_path}", file=sys.stderr)
        return 1

    methods: dict[tuple[str, str], int] = {}
    fields: dict[tuple[str, str], int] = {}
    current: str | None = None
    pending_rva: int | None = None  // chú thích RVA nằm ở DÒNG TRƯỚC method

    with dump_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_CLASS.match(line)
            if m:
                current = m.group(1)
                continue
            if current is None:
                continue

            # RVA nằm ở dòng comment ngay TRƯỚC dòng chữ ký method
            rm = RE_RVA.search(line)
            if rm:
                pending_rva = int(rm.group(1), 16)
                continue

            if current in WANT_METHODS:
                mm = RE_METHOD.match(line)
                if mm and mm.group(1) in WANT_METHODS[current] and pending_rva is not None:
                    methods[(current, mm.group(1))] = pending_rva
                    pending_rva = None

            if current in WANT_FIELDS:
                fm = RE_FIELD.match(line)
                if fm and fm.group(1) in WANT_FIELDS[current]:
                    fields[(current, fm.group(1))] = int(fm.group(2), 16)

    lines = [
        "// TỰ ĐỘNG SINH BẰNG tools/gen_offsets.py - đừng sửa tay.",
        "// Nguồn: dump.cs (Il2CppDumper). Chạy lại script khi cập nhật game.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace Gen {",
        "",
        "struct MethodRva { const char* klass; const char* method; uintptr_t rva; };",
        "struct FieldOff { const char* klass; const char* field; uintptr_t offset; };",
        "",
        "inline constexpr MethodRva kMethods[] = {",
    ]
    for (k, mname), rva in sorted(methods.items()):
        lines.append(f'    {{"{k}", "{mname}", 0x{rva:x}}},')
    lines += ["};", "", "inline constexpr FieldOff kFields[] = {"]
    for (k, fname), off in sorted(fields.items()):
        lines.append(f'    {{"{k}", "{fname}", 0x{off:x}}},')
    lines += ["};", "", "} // namespace Gen", ""]

    text = "\n".join(lines)

    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
        print(f"-> ghi {out} ({len(methods)} method, {len(fields)} field)")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
