# PlayFish

Dylib inject cho **Play Together** (Unity IL2CPP), chạy trên iOS **không cần jailbreak**.

- **Hook**: [Dobby](https://github.com/jmpews/Dobby) (inline hook arm64, static link)
- **GUI**: [Dear ImGui](https://github.com/ocornut/imgui) `v1.92.9b` + backend Metal, vẽ overlay trong game qua `CAMetalLayer`
- **Build**: [Theos](https://theos.dev) target `library` → `PlayFish.dylib`
- **CI**: GitHub Actions (macOS) tự tải thư viện và build

---

## 1. Cấu trúc project

```
PlayFish/
├── Makefile                     # Theos: arm64 dylib, tự gom Dobby + ImGui
├── .github/workflows/build.yml  # CI build trên macOS
├── scripts/
│   ├── fetch_libs.sh            # tải đúng file cần (imgui ~3MB, dobby ~200KB)
│   └── inject_ipa.sh            # chèn dylib + ký lại IPA
├── docs/
│   ├── FIND_HOOKS.md            # cách tìm đúng hàm câu cá (Il2CppDumper)
│   └── INJECT.md                # quy trình chèn vào IPA từng bước
└── src/
    ├── main.mm                  # entry: chờ UnityFramework, init il2cpp + overlay
    ├── Config.hpp               # toàn bộ setting runtime
    ├── Core/
    │   ├── target.hpp           # base address của UnityFramework, RVA helper, bảng offset
    │   ├── hooker.hpp           # wrapper Dobby + registry hook
    │   ├── patcher.hpp          # ghi byte/nop trong RAM (vm_protect) + restore
    │   ├── il2cpp.hpp           # resolve class/method theo TÊN (không cần offset)
    │   ├── log.hpp              # log + ring buffer cho GUI
    │   └── version.hpp
    ├── GUI/
    │   ├── Overlay.hpp/.mm      # hook CAMetalLayer/MTLCommandBuffer, vẽ ImGui mỗi frame
    │   └── Menu.hpp/.cpp        # panel bật/tắt tính năng + tab Debug
    └── Features/
        ├── FeatureManager.hpp/.cpp  # registry: GUI chỉ bật/tắt qua đây
        ├── AutoFish.hpp/.cpp        # tính năng câu cá (hook theo spec)
        ├── FishingSpec.hpp          # BẢNG ĐỊNH NGHĨA hàm cần hook ← điền ở đây
        └── Watcher.hpp/.cpp         # hook generic để quan sát lời gọi
```

## 2. Build

### Local (WSL / Linux / macOS)
```bash
./scripts/fetch_libs.sh     # tải ImGui + Dobby (nhỏ gọn, không clone full repo)
make                        # ra .theos/obj/PlayFish.dylib
```

Build với SDK khác:
```bash
make TARGET=iphone:clang:latest:17.0
```

### GitHub Actions
Push lên repo → workflow `build.yml` chạy trên `macos-14`, build và upload artifact `PlayFish-dylib`.

## 3. Cài vào game

```bash
./scripts/inject_ipa.sh PlayTogether.ipa out.ipa cert.p12 profile.mobileprovision
```

Chi tiết + xử lý lỗi: [`docs/INJECT.md`](docs/INJECT.md).

## 4. Bật tính năng câu cá

1. Dump game (xem [`docs/FIND_HOOKS.md`](docs/FIND_HOOKS.md)) để lấy tên class/method.
2. Điền vào `src/Features/FishingSpec.hpp`.
3. Build lại, chèn vào IPA.
4. Mở GUI trong game (overlay) → tab **Auto Fish** → bật tính năng.

GUI chỉ làm một việc: **bật/tắt tính năng** và hiện trạng thái. Toàn bộ logic nằm ở `src/Features/`.

## 5. Ghi chú

- Mọi offset tính từ **base của `UnityFramework`**, không phải base app.
- Ưu tiên **resolve theo tên** (`Il2Cpp::resolve`) để game update không phải build lại.
- Đây là công cụ để học RE/automation trên chính tài khoản của bạn; trách nhiệm thuộc về người dùng.
