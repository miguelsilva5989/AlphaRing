#include "CMCCContext.h"

#include "global/Global.h"

#include "mcc/network/Network.h"
#include "mcc/splitscreen/Splitscreen.h"
#include "mcc/module/Module.h"

static bool show_about = false;

CMCCContext CMCCContext::instance;
ICContext* g_pMCCContext = &CMCCContext::instance;

void CMCCContext::render() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Alpha Ring")) {
            ImGui::MenuItem("Pause game while open", nullptr, &AlphaRing::Global::Global()->pause_game_on_menu_shown);
            ImGui::MenuItem("Block game input", nullptr, &AlphaRing::Global::Global()->disable_input_on_menu_shown);
            ImGui::MenuItem("Show cursor", nullptr, &AlphaRing::Global::Global()->show_imgui_mouse);
            ImGui::Separator();
            ImGui::MenuItem("Wireframe", nullptr, &AlphaRing::Global::Global()->wireframe);
            ImGui::Separator();
            ImGui::MenuItem("About", nullptr, &show_about);
            if (ImGui::MenuItem("Close menu"))
                AlphaRing::Global::Global()->show_imgui = false;
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    MCC::Module::ImGuiContext();

    MCC::Splitscreen::ImGuiContext();

    MCC::Network::ImGuiContext();

    if (show_about) {
        ImGui::SetNextWindowSize(ImVec2(360.0f, 150.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("About Alpha Ring", &show_about, ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("Alpha Ring");
        ImGui::TextDisabled("MCC local split-screen tools");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Created by WinterSquire");
        ImGui::TextUnformatted("Linux compatibility and monitor split build");
        ImGui::End();
    }
}
