#!/usr/bin/env python3
"""Liệt kê thân class trong dump.cs (theo tên chính xác), lọc theo keyword.

Dùng:
    python3 tools/dumpclass.py <dump.cs> <ClassName> [keyword]
    python3 tools/dumpclass.py <dump.cs> --owner <MethodName>   # class nào khai báo method
"""
import re
import sys

path = sys.argv[1]

# ---------------------------------------------------------------- owner mode
if sys.argv[2] == "--owner":
    want = sys.argv[3].lower()
    cls_re = re.compile(r"^\s*(?:public|internal|private|protected)?\s*(?:sealed\s+|abstract\s+)?class\s+(\w+)")
    cur, nsp, found = None, "", {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = cls_re.match(line)
            if m:
                cur, nsp = m.group(1), ""
                continue
            if line.startswith("// Namespace:"):
                nsp = line.split(":", 1)[1].strip()
                continue
            if cur and re.search(r"\b" + re.escape(sys.argv[3]) + r"\s*\(", line, re.IGNORECASE):
                found.setdefault(f"{nsp}.{cur}" if nsp else cur, []).append(line.strip()[:140])
    for k, v in found.items():
        print(f"=== {k}")
        for s in v[:6]:
            print("   " + s)
    print(f"[=] {len(found)} class khai báo '{want}'")
    raise SystemExit(0)

cls = sys.argv[2]
kw = sys.argv[3].lower() if len(sys.argv) > 3 else None

# khai báo class gốc: "public class ActorBase : ..." (không có dấu chấm sau tên)
DECL = re.compile(r"^\s*(?:public|internal|private|protected)?\s*(?:sealed\s+|abstract\s+)?class\s+"
                  + re.escape(cls) + r"\s*[:<]")
# thành viên: dòng có dấu ( ... ) { } hoặc kết thúc ; và có tên ở cuối
MEMBER = re.compile(r"^\s*(?:public|private|protected|internal)\s+(?:static\s+|virtual\s+|override\s+|"
                    r"abstract\s+|readonly\s+|const\s+)*[^;()]+?\s+(\w+)\s*(?:\(|;)")

inside = False
hits = 0
with open(path, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        if not inside:
            if DECL.match(line):
                inside = True
                print(f"=== class {cls} : {line.strip()[:110]}")
            continue
        if line.rstrip() == "}":
            break
        m = MEMBER.match(line)
        if not m:
            continue
        name = m.group(1)
        if kw and kw not in name.lower():
            continue
        hits += 1
        print("   " + line.rstrip()[:160])
print(f"[=] {hits} dòng khớp trong {cls}" + (f" (lọc '{kw}')" if kw else ""))
