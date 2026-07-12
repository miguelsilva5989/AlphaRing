#include "Splitscreen.h"

#include "mcc/settings/Settings.h"
#include "mcc/settings/ConfigValidation.h"

#include "common.h"

#include "global/Global.h"
#include "hook/BuildManifest.h"

#include <offset_mcc.h>

#include "../CGameManager.h"

namespace MCC::Splitscreen {
    DefDetourFunction(__int64, __fastcall, get_index_by_xuid, void* a1, __int64 xuid) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        if (!p_setting->b_override)
            return ppOriginal_get_index_by_xuid(a1, xuid);

        const int index = CGameManager::get_index(xuid);
        return index >= 0 ? index : ppOriginal_get_index_by_xuid(a1, xuid);
    }

    // todo:: let other players have the ability to pause the game

    bool Initialize() {
        const auto& offsets = AlphaRing::Compatibility::CurrentBuild().mcc;
        // fix: changing team freeze the game
        const bool result = AlphaRing::Hook::Detour({
            {offsets.index_by_xuid, 0, get_index_by_xuid, (void**)&ppOriginal_get_index_by_xuid},
        });

        if (!result)
            LOG_ERROR("MCC Splitscreen: failed to install the XUID hook");
        return result;
    }
}

#include "imgui.h"
#include "mcc/mcc.h"
#include "input/Input.h"
#include "log/Log.h"
#include "render/d3d11/D3d11.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace MCC::Splitscreen {
    void RealContext();

    static int s_binding_player = -1;
    static bool s_binding_waiting_for_release = false;
    static char s_binding_error[96] = {};
    static int s_selected_player = 0;

    static constexpr int kControllerCount = 4;
    static constexpr int kUnassignedController = 4;
    static constexpr ImVec4 kSuccessColor {0.32f, 0.79f, 0.58f, 1.0f};
    static constexpr ImVec4 kWarningColor {0.95f, 0.68f, 0.30f, 1.0f};
    static constexpr ImVec4 kErrorColor {0.91f, 0.35f, 0.33f, 1.0f};

    struct SetupDraft {
        bool initialized = false;
        bool dirty = false;
        bool split_enabled = false;
        int player_count = 1;
        bool player_one_uses_keyboard = true;
        bool override_profile = false;
        bool use_player_one_profile = true;
        std::array<CGameManager::Profile_t, 4> profiles {};
        AlphaRing::Render::D3d11::MonitorSplitConfig monitor {};
    };

    static SetupDraft s_setup;

    static void RefreshSetupDraft() {
        const auto* settings = AlphaRing::Global::MCC::Splitscreen();
        s_setup.split_enabled = settings->b_override.load(std::memory_order_acquire);
        s_setup.player_count = MCC::Settings::NormalizePlayerCount(
                settings->player_count.load(std::memory_order_acquire));
        s_setup.player_one_uses_keyboard = settings->b_player0_use_km.load(std::memory_order_acquire);
        s_setup.override_profile = settings->b_override_profile.load(std::memory_order_acquire);
        s_setup.use_player_one_profile = settings->b_use_player0_profile.load(std::memory_order_acquire);
        for (int player = 0; player < 4; ++player) {
            if (const auto* profile = CGameManager::get_profile(player))
                s_setup.profiles[static_cast<size_t>(player)] = *profile;
        }
        s_setup.monitor = AlphaRing::Render::D3d11::MonitorSplit();
        s_setup.initialized = true;
        s_setup.dirty = false;
    }

    static void NormalizeSetupDraft() {
        s_setup.player_count = MCC::Settings::NormalizePlayerCount(s_setup.player_count);
        std::array<int, MCC::Settings::kMaximumPlayers> assignments {};
        for (int player = 0; player < 4; ++player)
            assignments[static_cast<size_t>(player)] = s_setup.profiles[static_cast<size_t>(player)].controller_index;
        MCC::Settings::NormalizeControllerAssignments(s_setup.player_one_uses_keyboard, assignments);
        for (int player = 0; player < 4; ++player)
            s_setup.profiles[static_cast<size_t>(player)].controller_index = assignments[static_cast<size_t>(player)];
    }

    static bool ApplySetupDraft() {
        NormalizeSetupDraft();
        auto* settings = AlphaRing::Global::MCC::Splitscreen();
        settings->b_override.store(s_setup.split_enabled, std::memory_order_release);
        settings->player_count.store(s_setup.player_count, std::memory_order_release);
        settings->b_player0_use_km.store(s_setup.player_one_uses_keyboard, std::memory_order_release);
        settings->b_override_profile.store(s_setup.override_profile, std::memory_order_release);
        settings->b_use_player0_profile.store(s_setup.use_player_one_profile, std::memory_order_release);

        for (int player = 0; player < 4; ++player) {
            if (auto* profile = CGameManager::get_profile(player))
                *profile = s_setup.profiles[static_cast<size_t>(player)];
        }

        auto& monitor = AlphaRing::Render::D3d11::MonitorSplit();
        const bool outputs_active = monitor.mirror_windows_enabled;
        const bool current_native = monitor.native_ce_render;
        const bool requested_native = AlphaRing::Render::D3d11::NativeRenderActive()
                ? current_native
                : s_setup.monitor.native_ce_render;
        monitor = s_setup.monitor;
        monitor.mirror_windows_enabled = outputs_active;
        monitor.native_ce_render = current_native;
        if (requested_native != current_native)
            AlphaRing::Render::D3d11::SetNativeRenderEnabled(requested_native);

        MCC::Settings::Splitscreen::CaptureFromRuntime();
        const bool split_saved = MCC::Settings::Splitscreen::Save();
        MCC::Settings::Profile::CaptureFromRuntime();
        const bool profiles_saved = MCC::Settings::Profile::Save();
        s_setup.monitor = monitor;
        s_setup.dirty = !(split_saved && profiles_saved);
        return !s_setup.dirty;
    }

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
        for (int player = 0; player < s_setup.player_count; ++player) {
            if (player == except_player || (player == 0 && s_setup.player_one_uses_keyboard))
                continue;

            const auto& profile = s_setup.profiles[static_cast<size_t>(player)];
            if (profile.controller_index == controller)
                return player;
        }
        return -1;
    }

    static void AssignKeyboard(int player) {
        if (player == 0)
            s_setup.player_one_uses_keyboard = true;
    }

    static void AssignController(int player, int controller) {
        if (player < 0 || player >= 4)
            return;

        s_setup.profiles[static_cast<size_t>(player)].controller_index = controller;
        if (player == 0)
            s_setup.player_one_uses_keyboard = false;
    }

    static const char* CurrentInputLabel(int player, char* buffer, size_t size) {
        if (player < 0 || player >= 4)
            return "Not assigned";
        const auto& profile = s_setup.profiles[static_cast<size_t>(player)];
        if (player == 0 && s_setup.player_one_uses_keyboard)
            return "Keyboard + mouse";
        if (profile.controller_index < 0 || profile.controller_index >= kControllerCount)
            return "Not assigned";

        std::snprintf(
                buffer,
                size,
                "Controller %d%s",
                profile.controller_index + 1,
                ControllerConnected(profile.controller_index) ? " - connected" : " - disconnected"
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
            const auto& profile = s_setup.profiles[static_cast<size_t>(player)];

            if (player == 0) {
                const bool selected = s_setup.player_one_uses_keyboard;
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

                const bool selected = !s_setup.player_one_uses_keyboard &&
                                      profile.controller_index == controller;
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
            const bool unassigned = !s_setup.player_one_uses_keyboard &&
                                    (profile.controller_index < 0 ||
                                     profile.controller_index >= kControllerCount);
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
        s_setup.dirty = true;
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
            const bool selected = *count == value;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.64f, 0.44f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.75f, 0.53f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.54f, 0.37f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.58f, 1.0f, 0.79f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.30f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.40f, 0.53f, 1.0f));
            }
            if (ImGui::Button(std::to_string(value).c_str(), ImVec2(38.0f, 0.0f))) {
                *count = value;
                changed = true;
            }
            if (selected) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(5);
            } else {
                ImGui::PopStyleColor(3);
            }
            ImGui::PopID();
            if (value != 4)
                ImGui::SameLine(0.0f, 4.0f);
        }
        return changed;
    }

    static bool CopyCurrentMccProfile(CGameManager::Profile_t& destination, int player) {
        LOG_INFO("Copy current MCC profile for player {}", player);
        __int64 xuid;
        auto* manager = GameManager();
        if (!MCC::IsInGame() || !manager || !(xuid = CGameManager::get_xuid(0)))
            return false;

        auto* source_profile = manager->ppOriginal.get_player_profile(manager, xuid);
        auto* source_mapping = manager->ppOriginal.retrive_gamepad_mapping(manager, xuid);
        if (!source_profile || !source_mapping) {
            LOG_ERROR("Copy current MCC profile: source profile or mapping unavailable");
            return false;
        }

        std::memcpy(&destination.profile, source_profile, sizeof(CUserProfile));
        std::memcpy(&destination.mapping, source_mapping, sizeof(CGamepadMapping));
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
        if (index < 0 || index >= 4)
            return false;
        auto& profile = s_setup.profiles[static_cast<size_t>(index)];

        ImGui::PushID(index);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        char player_label[24];
        std::snprintf(player_label, sizeof(player_label), "Player %d", index + 1);
        if (ImGui::Selectable(player_label, s_selected_player == index))
            s_selected_player = index;

        ImGui::TableSetColumnIndex(1);
        String::convert(buffer, profile.name, sizeof(buffer));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##name", buffer, sizeof(buffer))) {
            String::convert(profile.name, buffer, sizeof(buffer));
            dirty = true;
        }

        ImGui::TableSetColumnIndex(2);
        const bool input_changed = DrawInputSelector(index);
        dirty |= input_changed;

        ImGui::TableSetColumnIndex(3);
        if (s_binding_player == index) {
            ImGui::TextColored(
                    kWarningColor,
                    "%s",
                    s_binding_waiting_for_release ? "Release buttons" : "Listening..."
            );
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
        s_selected_player = std::clamp(s_selected_player, 0, s_setup.player_count - 1);

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
            ImGui::TextDisabled(
                    "%s",
                    s_binding_waiting_for_release ? "Release all controller buttons" : "Press a button on the controller"
            );
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
            for (int player = 0; player < s_setup.player_count; ++player)
                dirty |= DrawPlayerRow(player);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Player settings");
        auto& profile = s_setup.profiles[static_cast<size_t>(s_selected_player)];

        ImGui::Text("Player %d", s_selected_player + 1);
        ImGui::SameLine();
        char input_buffer[80];
        ImGui::TextDisabled("%s", CurrentInputLabel(s_selected_player, input_buffer, sizeof(input_buffer)));

        ImGui::BeginDisabled(!MCC::IsInGame());
        if (ImGui::Button("Copy current MCC settings")) {
            if (CopyCurrentMccProfile(profile, s_selected_player))
                dirty = true;
        }
        ImGui::EndDisabled();

        const bool profile_disabled = (!s_selected_player && !s_setup.override_profile) ||
                                      (s_selected_player && s_setup.use_player_one_profile);
        const CGamepadMapping mapping_before = profile.mapping;
        const CUserProfile profile_before = profile.profile;
        ImGui::BeginDisabled(profile_disabled);
        if (ImGui::CollapsingHeader("Controller mapping")) {
            ImGui::Indent();
            profile.mapping.ImGuiContext(profile.controller_index);
            ImGui::Unindent();
        }
        if (ImGui::CollapsingHeader("Profile options")) {
            ImGui::Indent();
            profile.profile.ImGuiContext();
            ImGui::Unindent();
        }
        ImGui::EndDisabled();

        if (std::memcmp(&mapping_before, &profile.mapping, sizeof(mapping_before)) != 0 ||
            std::memcmp(&profile_before, &profile.profile, sizeof(profile_before)) != 0)
            dirty = true;

        if (profile_disabled)
            ImGui::TextDisabled("Custom profile options are disabled by the shared-profile settings.");
        return dirty;
    }

    static bool DrawMonitorRect(const char* label, AlphaRing::Render::D3d11::RectI& rect) {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::InputInt2("##position", &rect.x);
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::InputInt2("##size", &rect.w);
        ImGui::PopID();
        return changed;
    }

    static bool DrawDisplaysPage() {
        bool dirty = false;
        auto& monitor = s_setup.monitor;
        auto& active_monitor = AlphaRing::Render::D3d11::MonitorSplit();

        ImGui::SeparatorText("Output windows");
        dirty |= ImGui::Checkbox("Auto-detect monitors", &monitor.auto_detect_monitors);
        dirty |= ImGui::Checkbox("Show only during split gameplay", &monitor.auto_hide_when_not_split);
        dirty |= ImGui::Checkbox("Match primary monitor aspect", &monitor.match_primary_aspect);

        ImGui::Spacing();
        ImGui::SeparatorText("Native player resolution");
        bool native_requested = monitor.native_ce_render;
        ImGui::BeginDisabled(AlphaRing::Render::D3d11::NativeRenderActive());
        if (ImGui::Checkbox("Enable for Halo CE", &native_requested)) {
            monitor.native_ce_render = native_requested;
            dirty = true;
        }
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
            dirty |= DrawMonitorRect("Player 1", monitor.player[0]);
            dirty |= DrawMonitorRect("Player 2", monitor.player[1]);
            ImGui::EndDisabled();
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (!active_monitor.mirror_windows_enabled) {
            if (ImGui::Button("Apply and start outputs", ImVec2(210.0f, 0.0f))) {
                s_setup.dirty |= dirty;
                if (ApplySetupDraft()) {
                    AlphaRing::Render::D3d11::StartMirrorSplitWindows();
                    s_setup.monitor = AlphaRing::Render::D3d11::MonitorSplit();
                    dirty = false;
                }
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.17f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.22f, 0.20f, 1.0f));
            if (ImGui::Button("Stop output windows", ImVec2(190.0f, 0.0f)))
                AlphaRing::Render::D3d11::StopMirrorSplitWindows();
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::TextColored(kSuccessColor, "Active");
        }
        return dirty;
    }

    static bool DrawAdvancedPage() {
        bool dirty = false;
        ImGui::SeparatorText("Profiles");
        dirty |= ImGui::Checkbox("Use Player 1 profile for other players", &s_setup.use_player_one_profile);
        dirty |= ImGui::Checkbox("Override Player 1 profile", &s_setup.override_profile);
        return dirty;
    }

    static bool SetupReady(char* reason, size_t reason_size) {
        if (!s_setup.split_enabled) {
            std::snprintf(reason, reason_size, "Split screen is disabled");
            return false;
        }
        if (s_setup.player_count < 2) {
            std::snprintf(reason, reason_size, "Select at least 2 players");
            return false;
        }
        for (int player = 0; player < s_setup.player_count; ++player) {
            if (player == 0 && s_setup.player_one_uses_keyboard)
                continue;
            const int controller = s_setup.profiles[static_cast<size_t>(player)].controller_index;
            if (controller < 0 || controller >= kControllerCount) {
                std::snprintf(reason, reason_size, "Player %d needs an input", player + 1);
                return false;
            }
            if (!ControllerConnected(controller)) {
                std::snprintf(reason, reason_size, "Player %d controller is disconnected", player + 1);
                return false;
            }
        }
        std::snprintf(reason, reason_size, "Ready for local play");
        return true;
    }

    void RealContext() {
        if (!s_setup.initialized)
            RefreshSetupDraft();

        bool dirty = false;
        if (s_binding_player >= s_setup.player_count)
            s_binding_player = -1;

        char readiness[96];
        const bool ready = SetupReady(readiness, sizeof(readiness));
        ImGui::TextColored(ready ? kSuccessColor : kWarningColor, "%s", readiness);
        if (s_setup.dirty) {
            ImGui::SameLine();
            ImGui::TextDisabled("Changes not applied");
        }
        ImGui::Spacing();

        ImGui::BeginGroup();
        dirty |= ImGui::Checkbox("Enable split screen", &s_setup.split_enabled);
        ImGui::SameLine();
        ImGui::TextColored(
                s_setup.split_enabled ? kSuccessColor : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                "%s",
                s_setup.split_enabled ? "Enabled" : "Disabled"
        );
        ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 235.0f);
        const int previous_count = s_setup.player_count;
        if (DrawPlayerCount(&s_setup.player_count)) {
            if (s_setup.player_count < previous_count) {
                for (int player = s_setup.player_count; player < previous_count; ++player)
                    s_setup.profiles[static_cast<size_t>(player)].controller_index = kUnassignedController;
            }
            dirty = true;
        }

        ImGui::Spacing();
        if (ImGui::BeginTabBar("SplitSetupTabs")) {
            if (ImGui::BeginTabItem("Players")) {
                ImGui::Spacing();
                dirty |= DrawPlayersPage();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Displays (Experimental)")) {
                ImGui::Spacing();
                dirty |= DrawDisplaysPage();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced")) {
                ImGui::Spacing();
                dirty |= DrawAdvancedPage();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        s_setup.dirty |= dirty;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::BeginDisabled(!s_setup.dirty);
        if (ImGui::Button("Apply changes", ImVec2(150.0f, 0.0f)))
            ApplySetupDraft();
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(100.0f, 0.0f))) {
            RefreshSetupDraft();
            s_binding_player = -1;
        }
        ImGui::EndDisabled();
    }
}
