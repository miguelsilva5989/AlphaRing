#include "Splitscreen.h"

#include "mcc/settings/Settings.h"

#include "common.h"

#include "global/Global.h"

#include <offset_mcc.h>

#include "../CGameManager.h"

namespace MCC::Splitscreen {
    DefDetourFunction(__int64, __fastcall, get_index_by_xuid, void* a1, __int64 xuid) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        if (!p_setting->b_override)
            return ppOriginal_get_index_by_xuid(a1, xuid);

        return CGameManager::get_index(xuid);
    }

    // todo:: let other players have the ability to pause the game

    bool Initialize() {
        bool result;

        // fix: changing team freeze the game
        result = AlphaRing::Hook::Detour({
            {0x38A09C/*0x2D01DC*/, 0x374164/*0x2BD620*/, get_index_by_xuid, (void**)&ppOriginal_get_index_by_xuid},
        });

        assertm(result, "MCC:Splitscreen: failed to hook");

        return true;
    }
}

#include "imgui.h"
#include "mcc/mcc.h"
#include "input/Input.h"
#include "log/Log.h"
#include "render/d3d11/D3d11.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace MCC::Splitscreen {
    void RealContext();

    static int s_binding_player = -1;
    static bool s_binding_waiting_for_release = false;
    static char s_binding_error[96] = {};
    static int s_selected_player = 0;
    static bool s_profile_save_pending = false;

    static constexpr int kControllerCount = 4;
    static constexpr int kUnassignedController = 4;
    static constexpr ImVec4 kSuccessColor {0.32f, 0.79f, 0.58f, 1.0f};
    static constexpr ImVec4 kWarningColor {0.95f, 0.68f, 0.30f, 1.0f};
    static constexpr ImVec4 kErrorColor {0.91f, 0.35f, 0.33f, 1.0f};

    static bool ControllerConnected(int controller) {
        XINPUT_STATE state {};
        return controller >= 0 && controller < kControllerCount &&
               AlphaRing::Input::GetXInputGetState(controller, &state);
    }

    static bool ControllerHasBindingInput(int controller) {
        XINPUT_STATE state {};
        if (!AlphaRing::Input::GetXInputGetState(controller, &state))
            return false;

        return state.Gamepad.wButtons != 0 ||
               state.Gamepad.bLeftTrigger > 30 ||
               state.Gamepad.bRightTrigger > 30;
    }

    static int DetectActiveController() {
        for (int i = 0; i < kControllerCount; ++i) {
            if (ControllerHasBindingInput(i))
                return i;
        }
        return -1;
    }

    static int ControllerOwner(int controller, int except_player = -1) {
        const auto* settings = AlphaRing::Global::MCC::Splitscreen();
        for (int player = 0; player < settings->player_count; ++player) {
            if (player == except_player || (player == 0 && settings->b_player0_use_km))
                continue;

            const auto* profile = CGameManager::get_profile(player);
            if (profile && profile->controller_index == controller)
                return player;
        }
        return -1;
    }

    static void AssignKeyboard(int player) {
        if (player == 0)
            AlphaRing::Global::MCC::Splitscreen()->b_player0_use_km = true;
    }

    static void AssignController(int player, int controller) {
        auto* profile = CGameManager::get_profile(player);
        if (!profile)
            return;

        profile->controller_index = controller;
        if (player == 0)
            AlphaRing::Global::MCC::Splitscreen()->b_player0_use_km = false;
    }

    static const char* CurrentInputLabel(int player, char* buffer, size_t size) {
        const auto* settings = AlphaRing::Global::MCC::Splitscreen();
        const auto* profile = CGameManager::get_profile(player);
        if (player == 0 && settings->b_player0_use_km)
            return "Keyboard + mouse";
        if (!profile || profile->controller_index < 0 || profile->controller_index >= kControllerCount)
            return "Not assigned";

        std::snprintf(
                buffer,
                size,
                "Controller %d%s",
                profile->controller_index + 1,
                ControllerConnected(profile->controller_index) ? " - connected" : " - disconnected"
        );
        return buffer;
    }

    static bool DrawInputSelector(int player) {
        bool changed = false;
        char preview_buffer[80];
        char option_buffer[80];
        const char* preview = CurrentInputLabel(player, preview_buffer, sizeof(preview_buffer));

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##input", preview)) {
            const auto* settings = AlphaRing::Global::MCC::Splitscreen();
            const auto* profile = CGameManager::get_profile(player);

            if (player == 0) {
                const bool selected = settings->b_player0_use_km;
                if (ImGui::Selectable("Keyboard + mouse", selected)) {
                    AssignKeyboard(player);
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::Separator();
            }

            for (int controller = 0; controller < kControllerCount; ++controller) {
                const int owner = ControllerOwner(controller, player);
                if (owner >= 0) {
                    std::snprintf(
                            option_buffer,
                            sizeof(option_buffer),
                            "Controller %d - Player %d",
                            controller + 1,
                            owner + 1
                    );
                } else {
                    std::snprintf(
                            option_buffer,
                            sizeof(option_buffer),
                            "Controller %d - %s",
                            controller + 1,
                            ControllerConnected(controller) ? "connected" : "disconnected"
                    );
                }

                const bool selected = !settings->b_player0_use_km && profile &&
                                      profile->controller_index == controller;
                ImGui::BeginDisabled(owner >= 0);
                if (ImGui::Selectable(option_buffer, selected)) {
                    AssignController(player, controller);
                    changed = true;
                }
                ImGui::EndDisabled();
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::Separator();
            const bool unassigned = !settings->b_player0_use_km &&
                                    (!profile || profile->controller_index < 0 ||
                                     profile->controller_index >= kControllerCount);
            if (ImGui::Selectable("Not assigned", unassigned)) {
                AssignController(player, kUnassignedController);
                changed = true;
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    static void BeginControllerBinding(int player) {
        s_binding_player = player;
        s_binding_waiting_for_release = true;
        s_binding_error[0] = '\0';
    }

    static bool UpdateControllerBinding() {
        if (s_binding_player < 0)
            return false;

        if (s_binding_waiting_for_release) {
            if (DetectActiveController() < 0)
                s_binding_waiting_for_release = false;
            return false;
        }

        const int controller = DetectActiveController();
        if (controller < 0)
            return false;

        const int owner = ControllerOwner(controller, s_binding_player);
        if (owner >= 0) {
            std::snprintf(
                    s_binding_error,
                    sizeof(s_binding_error),
                    "Controller %d is assigned to Player %d",
                    controller + 1,
                    owner + 1
            );
            s_binding_waiting_for_release = true;
            return false;
        }

        AssignController(s_binding_player, controller);
        s_selected_player = s_binding_player;
        s_profile_save_pending = true;
        s_binding_player = -1;
        s_binding_error[0] = '\0';
        return true;
    }

    static bool DrawPlayerCount(int* count) {
        bool changed = false;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Players");
        ImGui::SameLine();
        for (int value = 1; value <= 4; ++value) {
            ImGui::PushID(value);
            if (*count == value) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(std::to_string(value).c_str(), ImVec2(38.0f, 0.0f))) {
                *count = value;
                changed = true;
            }
            if (*count == value)
                ImGui::PopStyleColor(2);
            ImGui::PopID();
            if (value != 4)
                ImGui::SameLine(0.0f, 4.0f);
        }
        return changed;
    }

    static bool CopyCurrentMccProfile(int player) {
        LOG_INFO("Copy current MCC profile for player {}", player);
        __int64 xuid;
        auto* manager = GameManager();
        auto* engine = GameEngine();
        auto* destination = CGameManager::get_profile(player);
        if (!destination || !MCC::IsInGame() || !manager || !(xuid = CGameManager::get_xuid(0)))
            return false;

        auto* source_profile = manager->ppOriginal.get_player_profile(manager, xuid);
        auto* source_mapping = manager->ppOriginal.retrive_gamepad_mapping(manager, xuid);
        if (!source_profile || !source_mapping) {
            LOG_ERROR("Copy current MCC profile: source profile or mapping unavailable");
            return false;
        }

        std::memcpy(&destination->profile, source_profile, sizeof(CUserProfile));
        std::memcpy(&destination->mapping, source_mapping, sizeof(CGamepadMapping));
        if (engine)
            engine->load_setting();
        return true;
    }

    void ImGuiContext() {
        static bool show_splitscreen = true;

        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Split Screen", nullptr, &show_splitscreen);
            ImGui::EndMainMenuBar();
        }

        if (show_splitscreen) {
            ImGui::SetNextWindowSize(ImVec2(780.0f, 720.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 480.0f), ImVec2(1200.0f, 1100.0f));
            if (ImGui::Begin("Split Screen Setup", &show_splitscreen, ImGuiWindowFlags_NoCollapse))
                RealContext();
            ImGui::End();
        }
    }

    static bool DrawPlayerRow(int index) {
        bool dirty = false;
        char buffer[1024];
        auto* profile = CGameManager::get_profile(index);
        if (!profile)
            return false;

        ImGui::PushID(index);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        char player_label[24];
        std::snprintf(player_label, sizeof(player_label), "Player %d", index + 1);
        if (ImGui::Selectable(player_label, s_selected_player == index))
            s_selected_player = index;

        ImGui::TableSetColumnIndex(1);
        String::convert(buffer, profile->name, sizeof(buffer));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##name", buffer, sizeof(buffer))) {
            String::convert(profile->name, buffer, sizeof(buffer));
            dirty = true;
            s_profile_save_pending = true;
        }

        ImGui::TableSetColumnIndex(2);
        const bool input_changed = DrawInputSelector(index);
        dirty |= input_changed;
        s_profile_save_pending |= input_changed;

        ImGui::TableSetColumnIndex(3);
        if (s_binding_player == index) {
            ImGui::TextColored(kWarningColor, s_binding_waiting_for_release ? "Release buttons" : "Listening...");
        } else {
            ImGui::BeginDisabled(s_binding_player >= 0);
            if (ImGui::Button("Detect", ImVec2(-1.0f, 0.0f)))
                BeginControllerBinding(index);
            ImGui::EndDisabled();
        }
        ImGui::PopID();

        return dirty;
    }

    static bool DrawPlayersPage() {
        bool dirty = UpdateControllerBinding();
        auto* settings = AlphaRing::Global::MCC::Splitscreen();
        s_selected_player = std::clamp(s_selected_player, 0, settings->player_count - 1);

        if (s_binding_player >= 0) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.12f, 0.06f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.50f, 0.34f, 0.12f, 1.0f));
            ImGui::BeginChild("ControllerBinding", ImVec2(0.0f, 72.0f), true);
            ImGui::TextColored(kWarningColor, "Player %d controller detection", s_binding_player + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) {
                s_binding_player = -1;
                s_binding_error[0] = '\0';
            }
            ImGui::TextDisabled(s_binding_waiting_for_release ? "Release all controller buttons" : "Press a button on the controller");
            if (s_binding_error[0])
                ImGui::TextColored(kErrorColor, "%s", s_binding_error);
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }

        const ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("PlayersTable", 4, table_flags)) {
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            ImGui::TableHeadersRow();
            for (int player = 0; player < settings->player_count; ++player)
                dirty |= DrawPlayerRow(player);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Player settings");
        auto* profile = CGameManager::get_profile(s_selected_player);
        if (!profile)
            return dirty;

        ImGui::Text("Player %d", s_selected_player + 1);
        ImGui::SameLine();
        char input_buffer[80];
        ImGui::TextDisabled("%s", CurrentInputLabel(s_selected_player, input_buffer, sizeof(input_buffer)));

        ImGui::BeginDisabled(!MCC::IsInGame());
        if (ImGui::Button("Copy current MCC settings"))
            CopyCurrentMccProfile(s_selected_player);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Save player profiles")) {
            MCC::Settings::Profile::CaptureFromRuntime();
            MCC::Settings::Profile::Save();
        }

        const bool profile_disabled = (!s_selected_player && !settings->b_override_profile) ||
                                      (s_selected_player && settings->b_use_player0_profile);
        ImGui::BeginDisabled(profile_disabled);
        if (ImGui::CollapsingHeader("Controller mapping")) {
            ImGui::Indent();
            profile->mapping.ImGuiContext(profile->controller_index);
            ImGui::Unindent();
        }
        if (ImGui::CollapsingHeader("Profile options")) {
            ImGui::Indent();
            profile->profile.ImGuiContext();
            ImGui::Unindent();
        }
        ImGui::EndDisabled();

        if (profile_disabled)
            ImGui::TextDisabled("Custom profile options are disabled by the shared-profile settings.");
        return dirty;
    }

    static void DrawMonitorRect(const char* label, AlphaRing::Render::D3d11::RectI& rect) {
        ImGui::PushID(label);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt2("##position", &rect.x);
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt2("##size", &rect.w);
        ImGui::PopID();
    }

    static void DrawDisplaysPage() {
        auto& monitor = AlphaRing::Render::D3d11::MonitorSplit();

        ImGui::SeparatorText("Output windows");
        ImGui::Checkbox("Auto-detect monitors", &monitor.auto_detect_monitors);
        ImGui::Checkbox("Show only during split gameplay", &monitor.auto_hide_when_not_split);
        ImGui::Checkbox("Match primary monitor aspect", &monitor.match_primary_aspect);

        ImGui::Spacing();
        ImGui::SeparatorText("Native player resolution");
        bool native_requested = monitor.native_ce_render;
        ImGui::BeginDisabled(AlphaRing::Render::D3d11::NativeRenderActive());
        if (ImGui::Checkbox("Enable for Halo CE", &native_requested))
            AlphaRing::Render::D3d11::SetNativeRenderEnabled(native_requested);
        ImGui::EndDisabled();

        ImGui::TextDisabled("Compatibility");
        ImGui::SameLine();
        ImGui::TextColored(kWarningColor, "Halo CE / Classic graphics");
        ImGui::TextDisabled("Renderer");
        ImGui::SameLine();
        ImGui::TextUnformatted(AlphaRing::Render::D3d11::NativeRenderStatus());

        ImGui::Spacing();
        ImGui::SeparatorText("Monitor layout");
        if (ImGui::BeginTable("MonitorLayout", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::BeginDisabled(monitor.auto_detect_monitors);
            DrawMonitorRect("Player 1", monitor.player[0]);
            DrawMonitorRect("Player 2", monitor.player[1]);
            ImGui::EndDisabled();
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (!monitor.mirror_windows_enabled) {
            if (ImGui::Button("Start output windows", ImVec2(190.0f, 0.0f)))
                AlphaRing::Render::D3d11::StartMirrorSplitWindows();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.17f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.22f, 0.20f, 1.0f));
            if (ImGui::Button("Stop output windows", ImVec2(190.0f, 0.0f)))
                AlphaRing::Render::D3d11::StopMirrorSplitWindows();
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::TextColored(kSuccessColor, "Active");
        }
    }

    static bool DrawAdvancedPage() {
        bool dirty = false;
        auto* settings = AlphaRing::Global::MCC::Splitscreen();
        ImGui::SeparatorText("Profiles");
        dirty |= ImGui::Checkbox("Use Player 1 profile for other players", &settings->b_use_player0_profile);
        dirty |= ImGui::Checkbox("Override Player 1 profile", &settings->b_override_profile);
        return dirty;
    }

    void RealContext() {
        bool dirty = false;
        auto* settings = AlphaRing::Global::MCC::Splitscreen();
        settings->player_count = std::clamp(settings->player_count, 1, 4);
        if (s_binding_player >= settings->player_count)
            s_binding_player = -1;

        ImGui::BeginGroup();
        dirty |= ImGui::Checkbox("Enable split screen", &settings->b_override);
        ImGui::SameLine();
        ImGui::TextColored(settings->b_override ? kSuccessColor : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                           settings->b_override ? "Enabled" : "Disabled");
        ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 235.0f);
        dirty |= DrawPlayerCount(&settings->player_count);

        ImGui::Spacing();
        if (ImGui::BeginTabBar("SplitSetupTabs")) {
            if (ImGui::BeginTabItem("Players")) {
                ImGui::Spacing();
                dirty |= DrawPlayersPage();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Displays (Experimental)")) {
                ImGui::Spacing();
                DrawDisplaysPage();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced")) {
                ImGui::Spacing();
                dirty |= DrawAdvancedPage();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (dirty) {
            MCC::Settings::Splitscreen::CaptureFromRuntime();
            MCC::Settings::Splitscreen::Save();
        }
        if (s_profile_save_pending && !ImGui::IsAnyItemActive()) {
            MCC::Settings::Profile::CaptureFromRuntime();
            MCC::Settings::Profile::Save();
            s_profile_save_pending = false;
        }
    }
}
