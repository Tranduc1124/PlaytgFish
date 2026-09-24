#include "Menu.hpp"

#include "../Config.hpp"
#include "../Core/hooker.hpp"
#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"
#include "../Core/patcher.hpp"
#include "../Core/target.hpp"
#include "../Core/version.hpp"
#include "../Features/AutoFish.hpp"
#include "../Features/FeatureManager.hpp"

#include "imgui.h"

namespace PF::Menu {
namespace {

// ---------------------------------------------------------------------
//  Tab: Auto Fish — panel bật/tắt tính năng
// ---------------------------------------------------------------------
void tabAutoFish() {
    bool master = g_cfg.autoFish;
    if (ImGui::Checkbox("Auto fishing", &master)) {
        AutoFish::setEnabled(master); // cài/gỡ hook thật sự
    }
    ImGui::SameLine();
    ImGui::TextColored(AutoFish::enabled() ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f)
                                           : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                       AutoFish::enabled() ? "hooks active" : "inactive");

    ImGui::Separator();
    ImGui::TextUnformatted("Tính năng");
    ImGui::Spacing();

    ImGui::BeginDisabled(!g_cfg.autoFish);

    ImGui::Checkbox("Instant bite", &g_cfg.instantBite);
    ImGui::SameLine();
    ImGui::TextDisabled("ep ca can ngay");

    ImGui::Checkbox("Perfect reel", &g_cfg.perfectReel);
    ImGui::SameLine();
    ImGui::TextDisabled("ep thang minigame");

    ImGui::Checkbox("No cooldown", &g_cfg.noCooldown);
    ImGui::SameLine();
    ImGui::TextDisabled("bo delay giua 2 lan");

    ImGui::Checkbox("Only rare fish", &g_cfg.onlyRare);
    ImGui::SameLine();
    ImGui::TextDisabled("chi nhan ca hiem");

    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Bảng định nghĩa hàm");
    ImGui::Spacing();
    ImGui::TextWrapped("Điền tên class/method trong src/Features/FishingSpec.hpp sau khi dump game.");
    ImGui::TextWrapped("Xem docs/FIND_HOOKS.md để biết cách tìm đúng hàm câu cá.");

    ImGui::Spacing();
    if (ImGui::Button("Reinstall hooks"))
        AutoFish::install();
    ImGui::SameLine();
    if (ImGui::Button("Remove all hooks"))
        removeAllHooks();
    ImGui::SameLine();
    if (ImGui::Button("Restore patches"))
        restoreAllPatches();
}

// ---------------------------------------------------------------------
//  Tab: Debug — trạng thái + log
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
    ImGui::Text("Hooks          : %zu / %zu", hookCount(), hookRegistry().size());
    ImGui::Text("Version        : %s (%s)", kVersion, kBuild);

    ImGui::Separator();
    ImGui::TextUnformatted("Features");
    if (ImGui::BeginTable("features", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 100))) {
        ImGui::TableSetupColumn("Feature");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (auto* f : FeatureManager::get().features()) {
            if (!f) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", f->title());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f->description());
            ImGui::TableNextColumn();
            ImGui::TextColored(f->installed() ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f)
                                              : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               f->installed() ? "ON" : "OFF");
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::BeginTable("hooks", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 160))) {
        ImGui::TableSetupColumn("Hook");
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (const auto& h : hookRegistry()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(h.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("0x%lx", reinterpret_cast<uintptr_t>(h.target));
            ImGui::TableNextColumn();
            ImGui::TextColored(h.installed ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               h.installed ? "OK" : "FAIL");
        }
        ImGui::EndTable();
    }

    if (g_cfg.showLog && ImGui::BeginChild("log", ImVec2(0, 180), ImGuiChildFlags_Borders)) {
        std::lock_guard<std::mutex> lock(logMutex());
        for (const auto& line : logBuffer())
            ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    if (g_cfg.showLog)
        ImGui::EndChild();
}

} // namespace

void draw() {
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);

    if (!ImGui::Begin("PlayFish", &g_cfg.showMenu, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Auto Fish")) {
            tabAutoFish();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")) {
            tabDebug();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("PlayFish - Unity IL2CPP (Dobby + ImGui/Metal)");
    ImGui::End();
}

} // namespace PF::Menu
