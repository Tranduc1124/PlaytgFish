#include "Menu.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "../Config.hpp"
#include "../Core/Offsets.hpp"
#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/patcher.hpp"
#include "../Core/target.hpp"
#include "../Core/version.hpp"
#include "../Features/AutoFish.hpp"
#include "../Features/Discovery.hpp"
#include "../Features/FeatureManager.hpp"
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

} // namespace

void draw() {
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.9f);

    if (!ImGui::Begin("PlayFish", &g_cfg.showMenu, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Auto Fish")) { tabAutoFish(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Find")) { tabFind(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Overrides")) { tabOverrides(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Offsets")) { tabOffsets(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Debug")) { tabDebug(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("PlayFish - Unity IL2CPP (Dobby + ImGui/Metal) | chạm 3 ngon để đóng menu");
    ImGui::End();
}

} // namespace PF::Menu
