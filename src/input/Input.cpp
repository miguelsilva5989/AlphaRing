#include "Input.h"

#include "common.h"

#include "imgui.h"
#include "global/Global.h"

static HMODULE hModule;
static bool ownsModule = false;
static DWORD (WINAPI* g_pXInputGetState)(_In_  DWORD dwUserIndex, _Out_ XINPUT_STATE* pState) WIN_NOEXCEPT;
static DWORD (WINAPI* g_pXInputSetState)(_In_ DWORD dwUserIndex, _In_ XINPUT_VIBRATION* pVibration) WIN_NOEXCEPT;

namespace AlphaRing::Input {
    bool Init() {
        if ((hModule = GetModuleHandleA("XINPUT1_3.dll")) ||
            (hModule = GetModuleHandleA("XINPUT1_4.dll")) ||
            (hModule = GetModuleHandleA("XINPUT9_1_0.dll"))) {
            ownsModule = false;
        } else {
            hModule = LoadLibraryExA("XINPUT1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            ownsModule = hModule != nullptr;
        }

        if (!hModule) {
            LOG_WARNING("XInput is unavailable; keyboard and mouse input will still work");
            return true;
        }

        g_pXInputGetState = reinterpret_cast<decltype(g_pXInputGetState)>(GetProcAddress(hModule, "XInputGetState"));
        g_pXInputSetState = reinterpret_cast<decltype(g_pXInputSetState)>(GetProcAddress(hModule, "XInputSetState"));
        if (!g_pXInputGetState)
            LOG_WARNING("XInputGetState is unavailable; controller detection is disabled");

        return true;
    }

    bool Shutdown() {
        g_pXInputGetState = nullptr;
        g_pXInputSetState = nullptr;
        if (ownsModule && hModule)
            FreeLibrary(hModule);
        hModule = nullptr;
        ownsModule = false;
        return true;
    }

    bool GetXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!g_pXInputGetState || !pState) return false;
        memset(pState, 0, sizeof(XINPUT_STATE));
        return g_pXInputGetState(dwUserIndex, pState) == ERROR_SUCCESS;
    }

    void SetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
        if (!g_pXInputSetState) return;
        g_pXInputSetState(dwUserIndex, pVibration);
    }

    bool Update() {
        static bool b_toggled = false;
        XINPUT_STATE state {};

        auto& io = ImGui::GetIO();

        if (!GetXInputGetState(0, &state)) {
            io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
            io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
            io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
            io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, false);
            io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
            io.AddKeyEvent(ImGuiKey_GamepadDpadUp, false);
            io.AddKeyEvent(ImGuiKey_GamepadDpadDown, false);
            return false;
        }

        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_START && state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) {
            if (!b_toggled) {
                AlphaRing::Global::Global()->show_imgui = !AlphaRing::Global::Global()->show_imgui;
                b_toggled = true;
                return false;
            }
        } else {
            b_toggled = false;
        }

        const WORD buttons = state.Gamepad.wButtons;
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (buttons & XINPUT_GAMEPAD_A) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (buttons & XINPUT_GAMEPAD_B) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, (buttons & XINPUT_GAMEPAD_X) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp, (buttons & XINPUT_GAMEPAD_Y) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadL1, (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadR1, (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadStart, (buttons & XINPUT_GAMEPAD_START) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadBack, (buttons & XINPUT_GAMEPAD_BACK) != 0);

        const auto add_analog = [&io](ImGuiKey negative, ImGuiKey positive, SHORT value, SHORT dead_zone) {
            const float normalized = value < -dead_zone
                    ? static_cast<float>(-value - dead_zone) / (32768.0f - dead_zone)
                    : 0.0f;
            const float positive_value = value > dead_zone
                    ? static_cast<float>(value - dead_zone) / (32767.0f - dead_zone)
                    : 0.0f;
            io.AddKeyAnalogEvent(negative, normalized > 0.0f, normalized);
            io.AddKeyAnalogEvent(positive, positive_value > 0.0f, positive_value);
        };
        add_analog(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
                   state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        add_analog(ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp,
                   state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

        return true;
    }
}
