#include "ImGui.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstdio>

#include "input/Input.h"
#include "global/Global.h"
#include "filesystem/Filesystem.h"

#include "../d3d11/D3d11.h"
#include "./game/mcc/CMCCContext.h"
#include "./game/halo3/CHalo3Context.h"

#include "mcc/mcc.h"
#include "mcc/CGameGlobal.h"

static ICContext* pages[7] {
        nullptr,
        nullptr,
        g_pHalo3Context,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
};

namespace AlphaRing::Render::ImGui {
    static bool initialized = false;

    static void ApplyTheme(float scale) {
        auto& style = ::ImGui::GetStyle();

        style.WindowPadding = ImVec2(14.0f, 12.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.CellPadding = ImVec2(8.0f, 6.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
        style.TouchExtraPadding = ImVec2(2.0f, 2.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 12.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 7.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        style.WindowTitleAlign = ImVec2(0.03f, 0.5f);
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.02f, 0.5f);
        style.SeparatorTextBorderSize = 1.0f;
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(8.0f, 5.0f);

        auto& colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.91f, 0.93f, 0.94f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.52f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.068f, 0.98f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.070f, 0.079f, 0.084f, 0.92f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.065f, 0.074f, 0.079f, 0.99f);
        colors[ImGuiCol_Border] = ImVec4(0.20f, 0.23f, 0.24f, 0.90f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.20f, 0.21f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.25f, 0.27f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.085f, 0.090f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.07f, 0.075f, 0.90f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.070f, 0.080f, 0.085f, 0.98f);

        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.06f, 0.065f, 0.85f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.28f, 0.29f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.31f, 0.36f, 0.37f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.38f, 0.44f, 0.45f, 1.00f);

        colors[ImGuiCol_CheckMark] = ImVec4(0.32f, 0.79f, 0.58f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.66f, 0.84f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.38f, 0.76f, 0.93f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.16f, 0.40f, 0.53f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.51f, 0.67f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.35f, 0.47f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.14f, 0.32f, 0.40f, 0.85f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.43f, 0.54f, 0.90f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.37f, 0.47f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.23f, 0.24f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.66f, 0.84f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.38f, 0.76f, 0.93f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.66f, 0.84f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.66f, 0.84f, 0.65f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.38f, 0.76f, 0.93f, 0.90f);
        colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.51f, 0.67f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.40f, 0.53f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.27f, 0.33f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.22f, 0.25f, 0.26f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.17f, 0.18f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.11f, 0.12f, 0.13f, 0.55f);

        style.ScaleAllSizes(scale);
    }

    bool Initialize() {
        if (initialized)
            return true;
        if (!Graphics()->hwnd || !Graphics()->pDevice || !Graphics()->pContext)
            return false;

        ::ImGui::CreateContext();
        if (!ImGui_ImplWin32_Init(Graphics()->hwnd)) {
            ::ImGui::DestroyContext();
            return false;
        }
        if (!ImGui_ImplDX11_Init(Graphics()->pDevice, Graphics()->pContext)) {
            ImGui_ImplWin32_Shutdown();
            ::ImGui::DestroyContext();
            return false;
        }
        ImGuiIO &io = ::ImGui::GetIO();

        // config
        io.MouseDrawCursor = true;
        io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange |
                         ImGuiConfigFlags_NavEnableKeyboard |
                         ImGuiConfigFlags_NavEnableGamepad;

        // Keep the path storage alive for ImGui's non-owning IniFilename pointer.
        static std::string ini_path;
        ini_path = AlphaRing::Filesystem::DataPath("imgui.ini").string();
        io.IniFilename = ini_path.c_str();
        ::ImGui::LoadIniSettingsFromDisk(io.IniFilename);

        const float scale = GetDpiForWindow(Graphics()->hwnd) * 1.0f / 96.0f;

        const char* font_path = R"(C:\Windows\Fonts\segoeui.ttf)";
        const char* fallback_font_path = R"(C:\Windows\Fonts\msyh.ttc)";

        io.Fonts->Clear();
        if (AlphaRing::Filesystem::Exist(font_path)) {
            io.Fonts->AddFontFromFileTTF(font_path, 17.0f * scale);
        } else if (AlphaRing::Filesystem::Exist(fallback_font_path)) {
            io.Fonts->AddFontFromFileTTF(fallback_font_path, 17.0f * scale, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        } else {
            ImFontConfig config;
            config.SizePixels = 17.0f * scale;
            io.Fonts->AddFontDefault(&config);
        }

        ApplyTheme(scale);

        initialized = true;
        return true;
    }

    void Shutdown() {
        if (!initialized)
            return;

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ::ImGui::DestroyContext();
        initialized = false;
    }

    void Render() {
        if (!initialized)
            return;

        AlphaRing::Input::Update();

        // Skip ImGui processing entirely when menu is hidden
        // This prevents ImGui from capturing input and interfering with game menus
        if (!AlphaRing::Global::Global()->show_imgui)
            return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ::ImGui::NewFrame();

        bool inGame = MCC::IsInGame();
        auto pGameGlobal = GameGlobal();

        if (!AlphaRing::Global::Global()->show_imgui_mouse)
            ::ImGui::SetMouseCursor(ImGuiMouseCursor_None);

        g_pMCCContext->render();


        if (inGame && pGameGlobal != nullptr) {
            // Bounds check: pages array has 7 elements (indices 0-6)
            if (pGameGlobal->current_game > 0 && pGameGlobal->current_game < 7)
            {
                auto context = pages[pGameGlobal->current_game];
                if (context != nullptr)
                    context->render();
            }
        }

        if (::ImGui::BeginMainMenuBar()) {
            char frame_rate[32];
            std::snprintf(frame_rate, sizeof(frame_rate), "%.1f fps", ::ImGui::GetIO().Framerate);
            const float right_padding = 12.0f;
            ::ImGui::SetCursorPosX(std::max(
                    ::ImGui::GetCursorPosX(),
                    ::ImGui::GetWindowWidth() - ::ImGui::CalcTextSize(frame_rate).x - right_padding
            ));
            ::ImGui::TextDisabled("%s", frame_rate);
            ::ImGui::EndMainMenuBar();
        }

        ::ImGui::Render();
        Graphics()->SetRenderTargetView();
        ImGui_ImplDX11_RenderDrawData(::ImGui::GetDrawData());
    }
}
