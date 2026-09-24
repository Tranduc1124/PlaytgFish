# Tìm đúng hàm câu cá

Mục tiêu: biết **class + method** nào trong `UnityFramework` xử lý việc "cá cắn" và "kết quả câu".

## 1. Chuẩn bị file cần dump

Trong `.app` của Play Together, Unity IL2CPP nằm ở:

```
Payload/PlayTogether.app/Frameworks/UnityFramework
```

Bạn cần 2 file:
- `UnityFramework` (binary, phải **decrypt** nếu bản App Store — dùng `dumpdecrypted`/`frida-ios-dump` trên máy jailbreak, hoặc bản đã dump sẵn)
- `global-metadata.dat` (thường ở `.../Data/Managed/Metadata/global-metadata.dat`)

## 2. Chạy Il2CppDumper

```bash
./Il2CppDumper UnityFramework global-metadata.dat
```

Kết quả quan trọng:
- `dump.cs` — toàn bộ class/method/field của game
- `script.json` — mapping offset cho IDA/Ghidra
- `il2cpp.h` — prototype C

> Lưu ý: `global-metadata.dat` phải **đúng version** với binary, nếu không Il2CppDumper sẽ ra output rác.

## 3. Tìm trong `dump.cs`

```bash
grep -in "fish" dump.cs | head -50
grep -in "bite\|nibble\|reel\|cast\|fishing" dump.cs | head -50
```

Đọc quanh class có tên gợi ý (`FishingManager`, `FishController`, ...) để nhận ra:
- method kiểm tra cá cắn → thường tên `CheckBite`, `IsBite`, `CanCatch`
- method xử lý kết quả → `ReelResult`, `CatchResult`, `GetResult`
- method thả câu → `Cast`, `Throw`, `StartFishing`

## 4. Điền vào `FishingSpec.hpp`

```cpp
inline constexpr Method kFishing[] = {
    {Role::Check,  "FishingManager", "", "CheckBite", 0, 0x0, "kiểm tra cá cắn"},
    {Role::Reel,   "FishingManager", "", "ReelResult", 1, 0x0, "kết quả minigame"},
};
```

- `Role::Check` → dùng cho **Instant bite** (ép trả `true`)
- `Role::Reel` → dùng cho **Perfect reel** (ép trả giá trị thành công)
- `argc`: số tham số của method (xem trong `dump.cs`)

Build lại rồi chèn vào game. Xem log trong tab **Debug** để biết method nào resolve được.

## 5. Nếu resolve theo tên không được

Một số method bị **inline** hoặc không có trong bảng export. Khi đó cần offset:

1. Mở `UnityFramework` bằng Ghidra/IDA, nạp `script.json` để có symbol.
2. Tìm hàm tương ứng, lấy địa chỉ.
3. `rva = địa_chỉ_trong_file - image_base`.
4. Điền `rva` vào `FishingSpec.hpp` (giữ `klass`/`method` để dễ đọc).

## 6. Debug nhanh

Bật **Verbose log** trong tab Debug. Những dòng quan trọng:

```
[il2cpp] init OK
[il2cpp] FishingManager::CheckBite -> 0x... (fn=0x...)
[fish] installed=2 hook(s)
[hook] CheckBite @ 0x... OK
```

Nếu `fn=0x0` → sai tên class/method hoặc sai `argc`.
