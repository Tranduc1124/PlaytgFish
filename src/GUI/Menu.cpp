#include "Menu.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "../Config.hpp"
#include "../Core/Offsets.hpp"
#include "../Core/SettingsStore.hpp"
#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/patcher.hpp"
#include "../Core/target.hpp"
#include "../Core/version.hpp"
#include "../Features/AutoFish.hpp"
#include "../Features/Discovery.hpp"
#include "../Features/Esp.hpp"
#include "../Features/FeatureManager.hpp"
#include "../Features/AutoCast.hpp"
#include "../Features/FishingAuto.hpp"
#include "../Features/Overrides.hpp"
#include "Gui.hpp"

#include "imgui.h"

namespace PF::Menu {
namespace {

// state cho tab Find
static char s_keyword[128] = "";
static std::vector<Discovery::Candidate> s_results;
static bool s_scanned = false;
static int s_selectedResult = -1;

void tabAutoFish() {
    bool master = g_cfg.autoFish;
    if (ImGui::Checkbox("Auto fishing", &master)) {
        g_cfg.autoFish = master;
        AutoFish::setEnabled(master);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(ép cá cắn / thắng minigame / bỏ cooldown)");

    ImGui::Separator();
    ImGui::TextUnformatted("Tính năng");
    ImGui::Spacing();

    ImGui::BeginDisabled(!g_cfg.autoFish);
    ImGui::Checkbox("Instant bite", &g_cfg.instantBite);
    ImGui::SameLine();
    ImGui::TextDisabled("ép CheckBite trả true");

    ImGui::Checkbox("Perfect reel", &g_cfg.perfectReel);
    ImGui::SameLine();
    ImGui::TextDisabled("ép kết quả = success");

    ImGui::Checkbox("No cooldown", &g_cfg.noCooldown);
    ImGui::SameLine();
    ImGui::TextDisabled("nop hàm cooldown");

    ImGui::Checkbox("Only rare fish", &g_cfg.onlyRare);
    ImGui::SameLine();
    ImGui::TextDisabled("(cần offset field rarity)");

    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::Button("Re-apply")) {
        if (g_cfg.autoFish) {
            AutoFish::uninstall();
            AutoFish::install();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove all hooks")) {
        removeAllHooks();
        Overrides::clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore patches")) {
        restoreAllPatches();
    }
}

// ---------------------------------------------------------------------
//  Tab Find: quét metadata IL2CPP tự tìm hàm liên quan câu cá
// ---------------------------------------------------------------------
void tabFind() {
    ImGui::TextWrapped("Tự quét metadata của UnityFramework để tìm class/method "
                       "liên quan câu cá — không cần biết trước tên.");
    ImGui::Spacing();

    ImGui::TextUnformatted("Từ khoá");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("##kw", s_keyword, sizeof(s_keyword));
    ImGui::SameLine();
    if (ImGui::Button("Quét")) {
        s_results = Discovery::scan(strlen(s_keyword) ? s_keyword : Discovery::kClassKeywords, 300);
        s_scanned = true;
        s_selectedResult = s_results.empty() ? -1 : 0;
    }

    if (!s_scanned) return;

    ImGui::Text("Tìm thấy %zu method", s_results.size());
    ImGui::Separator();

    if (ImGui::BeginTable("candidates", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 300))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Score");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Method");
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Native");
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(s_results.size()); ++i) {
            const auto& c = s_results[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableNextColumn();
            ImGui::Text("%d", c.score);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.klass.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.method.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.role);
            ImGui::TableNextColumn();
            ImGui::Text("0x%lx", reinterpret_cast<uintptr_t>(c.fnptr));
            if (ImGui::IsItemClicked())
                s_selectedResult = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (s_selectedResult >= 0 && s_selectedResult < static_cast<int>(s_results.size())) {
        const auto& c = s_results[s_selectedResult];
        ImGui::Separator();
        ImGui::Text("Đã chọn: %s::%s", c.klass.c_str(), c.method.c_str());
        ImGui::Text("Role: %s   Native: 0x%lx", c.role, reinterpret_cast<uintptr_t>(c.fnptr));

        static int mode = 0; // 0 pass, 1 true, 2 false, 3 value
        const char* modes[] = {"Chỉ quan sát", "Force True", "Force False", "Force Value"};
        ImGui::Combo("Chế độ", &mode, modes, IM_ARRAYSIZE(modes));

        static int value = 1;
        if (mode == 3) {
            ImGui::InputInt("Giá trị", &value);
        }
        if (ImGui::Button("Cài override cho hàm này")) {
            Overrides::Mode m = Overrides::Mode::PassThrough;
            uintptr_t v = 0;
            switch (mode) {
                case 1: m = Overrides::Mode::ForceTrue; v = 1; break;
                case 2: m = Overrides::Mode::ForceFalse; v = 0; break;
                case 3: m = Overrides::Mode::ForceValue; v = (uintptr_t)value; break;
                default: break;
            }
            Overrides::add(c.fnptr, m, v, c.klass + "::" + c.method);
        }
    }
}

// ---------------------------------------------------------------------
//  Tab Overrides: xem các hook override đang chạy
// ---------------------------------------------------------------------
void tabOverrides() {
    ImGui::Text("Số override đang chạy: %zu / %zu", Overrides::count(), Overrides::kMaxSlots);
    ImGui::Separator();

    if (ImGui::BeginTable("ovr", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 300))) {
        ImGui::TableSetupColumn("Label");
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Mode");
        ImGui::TableSetupColumn("Calls");
        ImGui::TableSetupColumn("Last ret");
        ImGui::TableSetupColumn("Arg0");
        ImGui::TableSetupColumn(""); // nút gỡ
        ImGui::TableHeadersRow();

        auto slots = Overrides::snapshot();
        for (size_t i = 0; i < slots.size(); ++i) {
            auto& s = slots[i];
            if (!s.active) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.label.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("0x%lx", reinterpret_cast<uintptr_t>(s.target));
            ImGui::TableNextColumn();
            ImGui::Text("%s", Overrides::modeName(s.mode));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(s.calls));
            ImGui::TableNextColumn();
            ImGui::Text("0x%lx", static_cast<unsigned long>(s.lastReturn));
            ImGui::TableNextColumn();
            ImGui::Text("0x%lx", static_cast<unsigned long>(s.lastArg0));
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Gỡ"))
                Overrides::removeAt(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------
//  Tab Offsets: sửa RVA trong game mà không cần build lại
// ---------------------------------------------------------------------
void tabOffsets() {
    auto& reg = OffsetRegistry::get();
    ImGui::TextWrapped("Offset ở đây là RVA tính từ base UnityFramework. Sửa xong bấm "
                       "Save, sau đó vào Auto Fish bấm Re-apply.");
    ImGui::Spacing();

    if (ImGui::Button("Save")) reg.save();
    ImGui::SameLine();
    if (ImGui::Button("Load")) reg.load();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", reg.filePath().c_str());

    ImGui::Separator();
    if (ImGui::BeginTable("offsets", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 240))) {
        ImGui::TableSetupColumn("Tên");
        ImGui::TableSetupColumn("Giá trị (hex)");
        ImGui::TableSetupColumn("Mô tả");
        ImGui::TableHeadersRow();

        static char valueBuf[8][32] = {};
        const auto& entries = reg.entries();
        for (size_t i = 0; i < entries.size() && i < 8; ++i) {
            const auto& e = entries[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            if (valueBuf[i][0] == '\0') {
                snprintf(valueBuf[i], sizeof(valueBuf[i]), "0x%lx", (unsigned long)e.value);
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##v", valueBuf[i], sizeof(valueBuf[i]))) {
                reg.set(e.name, strtoull(valueBuf[i], nullptr, 0));
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.description.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------
//  Tab Debug
// ---------------------------------------------------------------------
void tabDebug() {
    ImGui::Checkbox("Draw overlay", &g_cfg.drawOverlay);
    ImGui::SameLine();
    ImGui::Checkbox("Verbose log", &g_cfg.verboseLog);
    ImGui::SameLine();
    ImGui::Checkbox("Show log", &g_cfg.showLog);

    ImGui::Separator();
    ImGui::Text("UnityFramework : 0x%lx", imageBase(kGameImage));
    ImGui::Text("App executable : 0x%lx", imageBase(kAppImage));
    ImGui::Text("il2cpp resolver: %s", Il2Cpp::ready() ? "OK" : "chưa sẵn sàng");
    ImGui::Text("ImGui frames    : %llu", Gui::frameCount());
    ImGui::Text("ImGui ready     : %s", Gui::ready() ? "OK" : "chưa");
    ImGui::Text("Hooks           : %zu / %zu", hookCount(), hookRegistry().size());
    ImGui::Text("Version         : %s (%s)", kVersion, kBuild);

    ImGui::Separator();
    if (ImGui::BeginChild("log", ImVec2(0, 220), ImGuiChildFlags_Borders)) {
        std::lock_guard<std::mutex> lock(logMutex());
        for (const auto& line : logBuffer())
            ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------
//  Tab Fish: ép field của FishingSystem (offset resolve theo tên)
// ---------------------------------------------------------------------
void tabFishFields() {
    bool on = FishingAuto::enabled();
    if (ImGui::Checkbox("Bật đọc/ghi field FishingSystem", &on)) {
        FishingAuto::setEnabled(on);
    }
    ImGui::TextDisabled("(công cụ debug — xem/ghi field theo tên)");

    ImGui::Spacing();
    auto& flags = FishingAuto::flags();
    if (flags.empty()) {
        ImGui::TextWrapped("Chưa tìm thấy field nào. Bấm 'Resolve FishingSystem' "
                           "(cần vào game đã load xong).");
        return;
    }

    ImGui::TextUnformatted("Field tìm thấy trong FishingSystem");
    ImGui::Separator();
    for (const auto& f : flags) {
        ImGui::PushID(f.name.c_str());
        bool forced = f.forced;
        if (ImGui::Checkbox(f.name.c_str(), &forced))
            FishingAuto::setFlagForced(f.name, forced);
        ImGui::SameLine();
        ImGui::TextDisabled("offset 0x%lx", (unsigned long)f.offset);
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------
//  Tab ESP: khung người chơi khác
// ---------------------------------------------------------------------
void tabEsp() {
    bool on = Esp::enabled();
    if (ImGui::Checkbox("Bật ESP", &on)) {
        Esp::setEnabled(on);
    }
    ImGui::SameLine();
    if (Esp::lastError()[0])
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Esp::lastError());

    ImGui::BeginDisabled(!on);
    ImGui::Checkbox("Khung (box)", &Esp::g_showBox);
    ImGui::SameLine();
    ImGui::Checkbox("Tên/ID", &Esp::g_showName);
    ImGui::SameLine();
    ImGui::Checkbox("Khoảng cách", &Esp::g_showDistance);
    ImGui::Checkbox("Chỉ trong tầm", &Esp::g_onlyInRange);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("max (m)", &Esp::g_maxDistance, 5.0f, 200.0f);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Người chơi khác: %zu   Hiển thị: %zu", Esp::playerCount(), Esp::visibleCount());
}

// ---------------------------------------------------------------------
//  Tab Auto Cast: quăng câu -> cắn -> kéo -> thu (tất cả trong 1 chỗ)
// ---------------------------------------------------------------------
void tabAutoCast() {
    auto& s = AutoCast::settings();

    ImGui::Checkbox("BẬT TỰ ĐỘNG CÂU", &s.enabled);
    ImGui::SameLine();
    if (!AutoCast::ready())
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "đang tự hook…");
    else
        ImGui::TextDisabled("đã hook xong");

    ImGui::Separator();
    ImGui::TextUnformatted("Nhóm bóng cá");
    ImGui::TextDisabled("1-5 = cá bé (nhánh thường) | 6-7 = cá to/quái (nhánh BigFish)");
    ImGui::RadioButton("Tự nhận từ game", &s.forceTier, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Ép nhánh cá bé (1-5)", &s.forceTier, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Ép nhánh cá to (6-7)", &s.forceTier, 2);

    ImGui::Separator();
    ImGui::TextUnformatted("Cá bé / bóng 1-5");
    ImGui::Checkbox("Tự quăng câu", &s.autoCast);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt("nhịp ms", &s.castIntervalMs, 500, 5000);
    ImGui::Checkbox("Tự kích hoạt cắn (bite)", &s.autoBite);
    ImGui::Checkbox("Tự gửi yêu cầu kéo (tug)", &s.autoTug);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt("kéo ms", &s.tugIntervalMs, 60, 600);

    ImGui::Separator();
    ImGui::TextUnformatted("Cá to / bóng 6-7 (state >=13)");
    ImGui::TextDisabled("Bắt buộc: LÔI (hit) -> KÉO (tug) -> stun");
    ImGui::Checkbox("Tự lôi cá (RequestFishingHit)", &s.bigAutoDrag);
    ImGui::SameLine();
    ImGui::Checkbox("Lôi theo vị trí float", &s.bigDragUseFloatPos);
    ImGui::Checkbox("Tự quăng câu sau khi xong", &s.bigAutoCast);
    ImGui::Checkbox("Tự tug minigame", &s.bigAutoTug);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt("kéo ms##big", &s.bigTugIntervalMs, 60, 800);
    ImGui::Checkbox("Tự stun", &s.bigAutoStun);

    ImGui::Separator();
    ImGui::TextDisabled("Chỉ tự động thao tác — không ép kết quả, không set HP (server/game tự quyết).");

    ImGui::Separator();
    const auto& st = AutoCast::status();
    const char* tierName = st.tier == AutoCast::ShadowTier::Big ? "CÁ TO (6-7)"
                         : st.tier == AutoCast::ShadowTier::Small ? "CÁ BÉ (1-5)" : "?";
    ImGui::Text("Trạng thái: %d | %s", st.currentState, tierName);
    ImGui::Text("Quăng %llu | Cắn %llu | Kéo %llu | Stun %llu",
                (unsigned long long)st.casts, (unsigned long long)st.bites,
                (unsigned long long)st.tugs, (unsigned long long)st.stuns);
    ImGui::Text("Cá to — Lôi %llu | Kéo %llu",
                (unsigned long long)st.bigDrag, (unsigned long long)st.bigTug);
}

} // namespace

// Vẽ ESP ở một cửa sổ fullscreen trong suốt (độc lập menu)
static void drawEspOverlay() {
    if (!Esp::enabled()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##esp_overlay", nullptr, flags)) {
        Esp::draw();
    }
    ImGui::End();
}

void draw() {
    // Cập nhật dữ liệu trước khi vẽ (render thread)
    AutoCast::tick();
    FishingAuto::tick();
    Esp::update();
    drawEspOverlay();

    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.9f);

    if (!ImGui::Begin("PlayFish", &g_cfg.showMenu, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Auto Cast")) { tabAutoCast(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Fish")) { tabFishFields(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Auto Fish")) { tabAutoFish(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("ESP")) { tabEsp(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Find")) { tabFind(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Overrides")) { tabOverrides(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Offsets")) { tabOffsets(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Debug")) { tabDebug(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("PlayFish - Unity IL2CPP (Dobby + ImGui/Metal) | chạm 3 ngon để mở menu");
    const bool closing = !g_cfg.showMenu;
    ImGui::End();

    // tự lưu cấu hình khi người dùng đóng menu
    if (closing) SettingsStore::save();
}

} // namespace PF::Menu
