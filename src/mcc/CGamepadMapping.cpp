#include "CGamepadMapping.h"
#include "CGameEngine.h"

static std::array<const char*, 17> button_names {
        "Left Trigger","Right Trigger",
        "Dpad Up","Dpad Down","Dpad Left","Dpad Right",
        "Start","Back",
        "Left Thumb","Right Thumb",
        "Left Shoulder","Right Shoulder",
        "A","B","X","Y",
        "None"  // Unbound
};

static std::array<const char*, 66> action_names {
        "Jump",
        "Switch Grenades 1",
        "Action",
        "Reload Right Weapon",
        "Change Weapon",
        "Melee",
        "Toggle Flashlight",
        "Throw Grenade",
        "Use Right Weapon",
        "Crouch",
        "Player Zoom",
        nullptr,
        nullptr,
        "Swap/Reload Left Weapon",
        "Sprint",
        "Banshee Bomb",
        "Player Move Forward", // H1A
        "Player Move Backward", // H1A
        "Player Move Left", // H1A
        "Player Move Right", // H1A
        "Multiplayer Scoreboard",
        "Vehicle Function 2",
        "Vehicle Function 3",
        "Use Equipment",
        "Vehicle Function 1",
        "Editor Ascend",
        "Editor Descend",
        "Drop/Pickup",
        "Thrust",
        "Precision Mode",
        "Delete",
        "Object Options",
        "Tools",
        "Play/Edit",
        "Editor Zoom",
        "Rotate",
        "TOGGLE PANEL",
        "TOGGLE INTERFACE",
        "TOGGLE FIRST/THIRD PERSON",
        "CAMERA RESET",
        "JUMP FORWARD",
        "JUMP BACK",
        "PAUSE/RESUME PLAYBACK",
        "FAST FORWARD",
        "TOGGLE FREECAM",
        "BOOST",
        "THEATER PANNING",
        "THEATER ASCEND",
        "THEATER DESCEND",
        "Use Left Weapon",
        "Theater Zoom",
        "Toggle Rotation Axes",
        "Duplicate",
        "Lock",
        "Reset",
        "Select Next Grenades 2",
        "Select Previous Grenades 2",
        "Special Action",
        "Open Loadouts Menu",
        "Toggle Display Waypoint Markers",
        "Toggle Display Waypoint Markers Alternative",
        "Push to Talk", // not sure
        "Vehicle Ascend",
        "Vehicle Descend",
        "Select Previous Grenades",
        "Select Next Grenades",
};

const std::array<const char *, 17>* CGamepadMapping::ButtonNames() {return &button_names;}

const std::array<const char *, 66>* CGamepadMapping::ActionNames() {return &action_names;}

void CGamepadMapping::ResetToDefaults() {
    // Set all to None (unbound) first
    for (int i = 0; i < 66; i++) {
        actions[i] = None;
    }

    // Standard Xbox Halo controls
    actions[0]  = A;             // Jump
    actions[1]  = LeftShoulder;  // Switch Grenades
    actions[2]  = X;             // Action/Interact
    actions[3]  = RightShoulder; // Reload Right Weapon
    actions[4]  = Y;             // Change Weapon
    actions[5]  = B;             // Melee
    actions[6]  = DpadUp;        // Toggle Flashlight
    actions[7]  = LeftTrigger;   // Throw Grenade
    actions[8]  = RightTrigger;  // Use Right Weapon (Shoot)
    actions[9]  = LeftThumb;     // Crouch
    actions[10] = RightThumb;    // Player Zoom
    actions[20] = Back;          // Multiplayer Scoreboard
}

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "input/Input.h"
#include "mcc/settings/Settings.h"

// Detect which button is currently pressed on a controller
static int DetectPressedButton(int controllerIndex) {
    XINPUT_STATE state;
    if (!AlphaRing::Input::GetXInputGetState(controllerIndex, &state))
        return -1;

    // Check triggers first
    if (state.Gamepad.bLeftTrigger > 30) return CGamepadMapping::LeftTrigger;
    if (state.Gamepad.bRightTrigger > 30) return CGamepadMapping::RightTrigger;

    // Check buttons
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) return CGamepadMapping::DpadUp;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) return CGamepadMapping::DpadDown;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) return CGamepadMapping::DpadLeft;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) return CGamepadMapping::DpadRight;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) return CGamepadMapping::Start;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) return CGamepadMapping::Back;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) return CGamepadMapping::LeftThumb;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) return CGamepadMapping::RightThumb;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) return CGamepadMapping::LeftShoulder;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) return CGamepadMapping::RightShoulder;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) return CGamepadMapping::A;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) return CGamepadMapping::B;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) return CGamepadMapping::X;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) return CGamepadMapping::Y;

    return -1;
}

static bool ContainsInsensitive(const char* text, const char* filter) {
    if (!filter || !filter[0])
        return true;
    if (!text)
        return false;

    const std::string haystack(text);
    const std::string needle(filter);
    return std::search(
                   haystack.begin(),
                   haystack.end(),
                   needle.begin(),
                   needle.end(),
                   [](char left, char right) {
                       return std::tolower(static_cast<unsigned char>(left)) ==
                              std::tolower(static_cast<unsigned char>(right));
                   }
           ) != haystack.end();
}

static bool ActionInCategory(int action, int category) {
    if (category == 2)
        return true;
    if (category == 0)
        return action <= 24 || action == 49 || action >= 55;
    return action >= 25 && action <= 54 && action != 49;
}

void CGamepadMapping::ImGuiContext(int preferred_controller) {
    bool result = false;
    static CGamepadMapping* displayed_mapping = nullptr;
    static CGamepadMapping* binding_mapping = nullptr;
    static int binding_action = -1;
    static int binding_controller = 0;
    static bool binding_waiting_for_release = false;
    static int action_category = 0;
    static char action_filter[64] = {};
    static std::vector<std::string> profile_names;
    static int selected_profile = -1;
    static char new_profile_name[64] = "";
    static bool show_save_input = false;
    static bool needs_refresh = true;

    if (displayed_mapping != this) {
        displayed_mapping = this;
        binding_mapping = nullptr;
        binding_action = -1;
        if (preferred_controller >= 0 && preferred_controller < 4)
            binding_controller = preferred_controller;
    }

    if (needs_refresh) {
        profile_names = MCC::Settings::CustomMapping::GetProfileNames();
        needs_refresh = false;
        if (selected_profile >= (int)profile_names.size()) {
            selected_profile = profile_names.empty() ? -1 : 0;
        }
    }

    ImGui::SeparatorText("Mapping preset");
    const char* preview = (selected_profile >= 0 && selected_profile < (int)profile_names.size())
        ? profile_names[selected_profile].c_str()
        : "Select preset";

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("##CustomProfile", preview)) {
        for (int i = 0; i < (int)profile_names.size(); ++i) {
            const bool is_selected = selected_profile == i;
            if (ImGui::Selectable(profile_names[i].c_str(), is_selected)) {
                selected_profile = i;
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(selected_profile < 0 || selected_profile >= (int)profile_names.size());
    if (ImGui::Button("Load")) {
        if (MCC::Settings::CustomMapping::LoadProfile(profile_names[selected_profile], *this)) {
            result = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        MCC::Settings::CustomMapping::DeleteProfile(profile_names[selected_profile]);
        needs_refresh = true;
        selected_profile = -1;
    }
    ImGui::EndDisabled();

    if (!show_save_input) {
        if (ImGui::Button("Save new preset")) {
            show_save_input = true;
            new_profile_name[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset defaults")) {
            ResetToDefaults();
            result = true;
        }
    } else {
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##NewProfileName", "Preset name", new_profile_name, sizeof(new_profile_name));
        ImGui::SameLine();
        ImGui::BeginDisabled(new_profile_name[0] == '\0');
        if (ImGui::Button("Save")) {
            if (MCC::Settings::CustomMapping::SaveProfile(new_profile_name, *this)) {
                needs_refresh = true;
                show_save_input = false;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##SaveMapping")) {
            show_save_input = false;
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Button assignments");

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Listen on");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(210.0f);
    char controller_preview[48];
    XINPUT_STATE controller_state {};
    const bool controller_connected = AlphaRing::Input::GetXInputGetState(binding_controller, &controller_state);
    std::snprintf(
            controller_preview,
            sizeof(controller_preview),
            "Controller %d - %s",
            binding_controller + 1,
            controller_connected ? "connected" : "disconnected"
    );
    if (ImGui::BeginCombo("##BindController", controller_preview)) {
        for (int controller = 0; controller < 4; ++controller) {
            XINPUT_STATE state {};
            const bool connected = AlphaRing::Input::GetXInputGetState(controller, &state);
            char label[48];
            std::snprintf(label, sizeof(label), "Controller %d - %s", controller + 1, connected ? "connected" : "disconnected");
            if (ImGui::Selectable(label, binding_controller == controller))
                binding_controller = controller;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("##ActionCategory", &action_category, "Gameplay\0Editor + Theater\0All actions\0");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ActionFilter", "Filter actions", action_filter, sizeof(action_filter));

    if (binding_mapping == this && binding_action >= 0) {
        bool binding_completed = false;
        if (binding_waiting_for_release) {
            if (DetectPressedButton(binding_controller) < 0)
                binding_waiting_for_release = false;
        } else {
            const int pressed = DetectPressedButton(binding_controller);
            if (pressed >= 0) {
                actions[binding_action] = static_cast<CGamepadMapping::eButton>(pressed);
                binding_mapping = nullptr;
                binding_action = -1;
                result = true;
                binding_completed = true;
            }
        }

        if (!binding_completed) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.12f, 0.06f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.50f, 0.34f, 0.12f, 1.0f));
            ImGui::BeginChild("MappingBinding", ImVec2(0.0f, 52.0f), true);
            const char* binding_name = action_names[binding_action] ? action_names[binding_action] : "Unknown action";
            ImGui::TextColored(ImVec4(0.95f, 0.68f, 0.30f, 1.0f), "%s", binding_name);
            ImGui::SameLine();
            ImGui::TextDisabled(
                    "%s",
                    binding_waiting_for_release ? "Release controller buttons" : "Press a controller button"
            );
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 62.0f);
            if (ImGui::SmallButton("Cancel")) {
                binding_mapping = nullptr;
                binding_action = -1;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }
    }

    const ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_ScrollY |
                                        ImGuiTableFlags_SizingStretchProp;
    const float table_height = std::max(220.0f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginTable("MappingActions", 3, table_flags, ImVec2(0.0f, table_height))) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableHeadersRow();

        for (int action = 0; action < static_cast<int>(action_names.size()); ++action) {
            char unknown_name[32];
            const char* name = action_names[action];
            if (!name) {
                std::snprintf(unknown_name, sizeof(unknown_name), "Action %d", action);
                name = unknown_name;
            }
            if (!ActionInCategory(action, action_category) || !ContainsInsensitive(name, action_filter))
                continue;

            ImGui::PushID(action);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name);

            ImGui::TableSetColumnIndex(1);
            int value = actions[action] == CGamepadMapping::None ? 16 : static_cast<int>(actions[action]);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##button", &value, button_names.data(), button_names.size())) {
                actions[action] = value == 16 ? CGamepadMapping::None : static_cast<CGamepadMapping::eButton>(value);
                result = true;
            }

            ImGui::TableSetColumnIndex(2);
            const bool is_binding = binding_mapping == this && binding_action == action;
            ImGui::BeginDisabled((binding_mapping && !is_binding) || !controller_connected);
            if (ImGui::Button(is_binding ? "Listening" : "Detect", ImVec2(-1.0f, 0.0f)) && !is_binding) {
                binding_mapping = this;
                binding_action = action;
                binding_waiting_for_release = true;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (result) {
        auto* engine = GameEngine();
        if (engine)
            engine->load_setting();
    }
}
