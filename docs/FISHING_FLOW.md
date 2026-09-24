# Luồng câu cá — đã đọc từ code game (không đoán)

Tài liệu này ghi lại những gì **đã xác minh bằng disassembly** trên
`PLAYTOGETHERVNG.app/Frameworks/UnityFramework.framework/UnityFramework`
(Play Together VNG 2.31.0, binary đã decrypt: `cryptid 0`).

Công cụ dùng để tra: `tools/ida_lite.py` (index `script.json` → RVA + chữ ký C)
và `tools/findxref.c` (quét lệnh `BL` để tìm "ai gọi hàm này").

---

## 1. enum `ActorDefaultControl.eFishingState`

| Giá trị | Tên | Nhóm |
|---|---|---|
0–12 | `None, Casting, Search, SearchResult, Idle, Hit, Fighting, Catch, Fail, Boast, Finish, CastingFail, Miss` | **cá bé (bóng 1-5)** |
13–25 | `BigFish_RaidEnter, RaidSync, Begin, Pumpin, Drag, Tug, Fighting, Catch, Miss, RaidFighting, StunBegin, Stun, StunRecovery` | **cá to / bóng 6-7** |

---

## 2. API `FishingSystem` (RVA + chữ ký thật từ `script.json`)

| Hàm | RVA | Chữ ký |
|---|---|---|
`get_Self` | `0x2CE2AD8` | `FishingSystem* get_Self()` |
`RequestFishingBegin` | `0x2CEB820` | `(Vector3 point, Action<bool,uint,bool,FishingFailType> cb)` |
`RequestCastingResult` | `0x2CEC558` | `(Action<bool,uint> cb)` |
`RequestFishingHit` | `0x2CEAC38` | `(bool isHit, Vector3 fishingPoint, Action<eHitState> cb)` |
`RequestFishingTug` | `0x2CEC804` | `(Action<bool,bool,int,bool> cb)` |
`RequestStunHit` | `0x2CECDC8` | `(Action<bool> cb)` |
`RequestFishingCatch` | `0x2CECFF8` | `(bool success, Action<...> cb)` |
`SetFishHP` | `0x2CE22FC` | `(int HP)` — **không dùng** (theo yêu cầu: không set HP) |
`GetFishShadow` / `ReturnFishShadow` | `0x2CE4F5C` / `0x2CE2EF0` | hệ thống bóng cá |

---

## 3. Ba hàm `Request*` — ai gọi và gọi thế nào

### 3.1 LÔI CÁ = `RequestFishingHit`
* **Chỉ 1 caller**: `ActorDefaultControlPlayer::UpdateFishingState` tại `0x36D9A34`.

```asm
0x36D9994: bl   UnityEngine.Time$$get_realtimeSinceStartup
0x36D999C: ldr  s1, [x21, #44]            ; thời gian từ bảng data
0x36D99A0: fadd s0, s0, s1
0x36D99AC: bl   PT_Encrypt.EncryptFloat$$set_Value   ; ghi mốc thời gian
0x36D99B4: strb #1, [x19, #1496]          ; cờ this+0x5D8
0x36D99B8: strb #1, [x19, #1498]          ; cờ this+0x5DA
0x36D99C0: bl   FishingSystem$$get_Self
0x36D9A18: mov  x0, x22                   ; FishingSystem
0x36D9A1C: mov  w1, #1                    ; isHit = TRUE
0x36D9A20: fmov s0/s1/s2, s8/s9/s10       ; fishingPoint (từ hàm ảo của chính this)
0x36D9A30: mov  x3, #0                    ; hitResultCB = NULL  ← game truyền NULL
0x36D9A34: bl   FishingSystem$$RequestFishingHit
0x36D9A40: bl   ActorControl$$SendToSyncPlayerFishingAction
```

→ `AutoCast` gọi y hệt: `RequestFishingHit(sys, true, <vị trí float>, nullptr)`.

### 3.2 KÉO = `RequestFishingTug` (có cổng điều kiện)
* **Chỉ 1 caller**: cùng `UpdateFishingState`, tại `0x36DB2FC`.

```asm
0x36DB2B0: mov  x1, #0
0x36DB2B4: bl   FishingFloatController$$IsBigFishHit    ; ← CỔNG điều kiện
0x36DB2B8: cbz  w0, #344                              ; false -> KHÔNG kéo
0x36DB2E8: bl   System.Action<bool,bool,int,bool>::.ctor
0x36DB2F0: mov  x0, x20                               ; FishingSystem
0x36DB2F4: mov  x1, x21                               ; callback delegate
0x36DB2F8: mov  x2, #0                                ; MethodInfo* = NULL
0x36DB2FC: bl   FishingSystem$$RequestFishingTug
```

`IsBigFishHit()` (RVA `0x2CE13AC`) là điều kiện nhiều thành phần:
```asm
0x2CE13E0: ldr  w8, [x19, #132]   ; this+0x84
0x2CE13E4: cmp  w8, #15          ; == BigFish_Begin
0x2CE13E8: b.ne <false>
0x2CE13EC: ldr  x0, [x19, #168]  ; this+0xA8 phải khác null
0x2CE13FC: bl   ActorControl$$get_IsMyActor
0x2CE1410: bl   Singleton<object>::get_I
```
→ `AutoCast` **gọi lại `IsBigFishHit()`** thay vì viết lại điều kiện.

### 3.3 STUN = `RequestStunHit`
* **Chỉ 1 caller**: `ActorDefaultControlPlayer::FishStunRecovery` tại `0x36DEE88`.
  (Hàm này là `override`, không có caller trực tiếp — được gọi qua vtable.)

```asm
0x36DEE38: bl   ActorDefaultControl$$FishStunRecovery    ; gọi hàm base trước
0x36DEE40: bl   Singleton<object>::get_I
0x36DEE58: ldr  x20, [x0, #200]                          ; FishingSystem
0x36DEE74: bl   System.Action<bool>::.ctor                ; tạo callback
0x36DEE88: bl   FishingSystem$$RequestStunHit             ; (cb, MethodInfo=NULL)
0x36DEEB8: b    FishingSystem$$set_IsFishStun             ; tail call
```

---

## 4. Chuỗi logic đúng cho bóng 6-7 (đã đối chiếu code)

```
BigFish_Pumpin / BigFish_Drag
   └─ RequestFishingHit(true, <điểm float>, NULL)      ← LÔI (không callback)
BigFish_Tug / BigFish_Fighting
   └─ nếu IsBigFishHit() == true
        └─ RequestFishingTug(<callback>, NULL)          ← KÉO
BigFish_Stun / BigFish_StunBegin
   └─ RequestStunHit(<callback>, NULL)                  ← STUN
BigFish_Catch / BigFish_Miss / BigFish_StunRecovery
   └─ StartFishing()                                    ← quăng câu tiếp
```

Cá bé (bóng 1-5): `StartFishing()` → `FishingBite()` → `RequestFishingTug()`.

---

## 5. Điều KHÔNG làm (có chủ đích)

* **Không** `SetFishHP(0)` — bỏ theo yêu cầu.
* **Không** ép `Receive*/CatchResult/HitResult/Lift` — các hàm này là **kết quả từ
  server**; sửa chúng chỉ đổi phía client và dễ lệch trạng thái.
* **Không** gửi `RequestFishingHit` ngoài state `BigFish_*`, và chỉ gửi `RequestFishingTug`
  khi `IsBigFishHit()` cho phép — y hệt điều kiện của game.

## 6. Cách tái lập các bước trên

```bash
# 1) index 520k hàm từ script.json
python3 tools/ida_lite.py index script.json ~/pf_index.txt

# 2) tra hàm
python3 tools/ida_lite.py find ~/pf_index.txt RequestFishingHit

# 3) ai gọi hàm (build findxref vì Python quá chậm)
clang -O2 -o findxref tools/findxref.c
./findxref <UnityFramework> 4000 29cc160 2ceac38

# 4) đọc code
python3 tools/ida_lite.py disat ~/pf_index.txt <UnityFramework> 36d9a18 18
python3 tools/ida_lite.py owner ~/pf_index.txt 36d9a34
```
