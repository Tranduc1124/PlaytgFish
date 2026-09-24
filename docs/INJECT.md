# Chèn PlayFish.dylib vào IPA

Yêu cầu: dự án đã build ra `.theos/obj/PlayFish.dylib`.

## 0. Tool cần có

| Tool | Dùng để | Link |
|---|---|---|
| `insert_dylib` hoặc `optool` | chèn load command vào binary | [Tyilo/insert_dylib](https://github.com/Tyilo/insert_dylib) |
| `zsign` | ký lại IPA | [zhlynn/zsign](https://github.com/zhlynn/zsign) |
| cert + profile | tài khoản ký | Apple Developer / tạo profile tương ứng |

## 1. Chạy script

```bash
./scripts/inject_ipa.sh <input.ipa> [output.ipa] [cert.p12] [profile.mobileprovision]
```

Ví dụ:
```bash
./scripts/inject_ipa.sh PlayTogether.ipa PlayTogether-fish.ipa mycert.p12 myprofile.mobileprovision
```

Script sẽ:
1. giải nén IPA
2. copy `PlayFish.dylib` vào `Payload/<App>.app/Frameworks/`
3. chèn `@executable_path/Frameworks/PlayFish.dylib` vào binary chính
4. xoá `_CodeSignature` cũ
5. ký lại bằng `zsign` (nếu có cert/profile)
6. đóng gói lại IPA

## 2. Làm thủ công (nếu script không chạy được)

```bash
unzip PlayTogether.ipa -d work
cp .theos/obj/PlayFish.dylib work/Payload/PlayTogether.app/Frameworks/

insert_dylib --strip-codesig --all-architectures \
  "@executable_path/Frameworks/PlayFish.dylib" \
  work/Payload/PlayTogether.app/PlayTogether \
  work/Payload/PlayTogether.app/PlayTogether.new

mv work/Payload/PlayTogether.app/PlayTogether.new \
   work/Payload/PlayTogether.app/PlayTogether
chmod 755 work/Payload/PlayTogether.app/PlayTogether
rm -rf work/Payload/PlayTogether.app/_CodeSignature

cd work && zip -qr ../unsigned.ipa Payload
zsign -k mycert.p12 -m myprofile.mobileprovision -o signed.ipa ../unsigned.ipa
```

## 3. Kiểm tra

Sau khi cài lên máy, mở app và xem log console:

```
[PlayFish] ===...
[PlayFish] PlayFish 0.1.0 (...) loaded, pid=...
[PlayFish] UnityFramework base = 0x...
[overlay] hooked -[CAMetalLayer nextDrawable]
[overlay] ImGui ready (Metal backend)
```

Thấy `ImGui ready` nghĩa là overlay đã lên. Bật toggle ở tab Auto Fish.

## 4. Lỗi thường gặp

| Triệu chứng | Nguyên nhân |
|---|---|
| App crash ngay khi mở | sai `CFBundleExecutable`, hoặc dylib sai kiến trúc (phải arm64) |
| `Image not found` khi ký | thiếu entitlement, dùng `zsign -e entitlements.plist` |
| Overlay không hiện | game không dùng Metal (thử lại khi nào backend Metal được chọn), hoặc `drawOverlay` đang tắt |
| `could not build module 'Foundation'` khi build | đổi `TARGET` sang SDK có sẵn (xem Makefile) |
| Không thấy log | dùng `Console.app`/`idevicesyslog` lọc theo tiến trình của app |

## 5. Lưu ý an toàn

- Chỉ dùng trên tài khoản/nội dung thuộc sở hữu của bạn.
- Sửa app có thể khiến app không khởi động hoặc tài khoản bị hạn chế; bạn tự chịu trách nhiệm.
