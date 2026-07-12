#include "Module.h"

#include <functional>

#include "tinyxml2.h"

#include "common.h"
#include "offset_mcc.h"
#include "mcc/CGameManager.h"
#include "render/d3d11/D3d11.h"

namespace MCC::Module {
    DefDetourFunction(void, __fastcall, module_load, module_info_t* info, int a2, __int64 a3) {
        ppOriginal_module_load(info, a2, a3);
        LOG_INFO(
                "MCC module load: title={} handle={:p}",
                info ? info->title : -1,
                info ? reinterpret_cast<void*>(info->hModule) : nullptr
        );
        if (info) {
            AlphaRing::Render::D3d11::NotifyMccModuleLoaded(info->title);
            auto module = GetSubModule(info->title);
            if (module && !module->load_module(info))
                LOG_ERROR("MCC module {} failed AlphaRing initialization", info->title);
        }
    }

    DefDetourFunction(__int64, __fastcall, module_unload, module_info_t* info) {
        const int title = info ? info->title : -1;
        LOG_INFO(
                "MCC module unload: title={} handle={:p}",
                title,
                info ? reinterpret_cast<void*>(info->hModule) : nullptr
        );
        if (info) {
            auto module = GetSubModule(info->title);
            if (module)
                module->unload_module();
        }
        const auto result = ppOriginal_module_unload(info);
        if (title >= 0)
            AlphaRing::Render::D3d11::NotifyMccModuleUnloaded(title);
        return result;
    }

    bool Initialize() {
        const bool result = AlphaRing::Hook::Detour({
            {OFFSET_MCC_PF_MODULELOAD, OFFSET_MCC_WS_PF_MODULELOAD,  module_load, (void **)&ppOriginal_module_load},
            {OFFSET_MCC_PF_MODULEUNLOAD, OFFSET_MCC_WS_PF_MODULEUNLOAD, module_unload, (void **)&ppOriginal_module_unload},
        });

        if (!result) {
            LOG_ERROR("MCC Module: failed to install load/unload hooks");
            return false;
        }

#if ALPHARING_DEVTOOLS
        if (!ReloadPatch("../../../alpha_ring/patch.xml"))
            LOG_WARNING("MCC Module: no compatible developer patch file was loaded");
#endif
        return true;
    }
}

bool MCC::Module::ReloadPatch(const char *xml_path) {
    FILE* file;
    wchar_t wbuffer[MAX_PATH];

    auto p_converter = [](const std::string& hex) {
        std::vector<__int8> bytes;

        for (unsigned int i = 0; i < hex.length(); i += 3) {
            std::string byteString = hex.substr(i, 2);
            auto byte = static_cast<__int8>(std::stoi(byteString, nullptr, 16));
            bytes.push_back(byte);
        }

        return bytes;
    };

    AlphaRing::Filesystem::GetDir(xml_path, wbuffer);

    if (_wfopen_s(&file, wbuffer, L"rb") != 0) return false;

    tinyxml2::XMLDocument doc;

    const auto load_result = doc.LoadFile(file);
    fclose(file);
    if (load_result != tinyxml2::XML_SUCCESS)
        return false;

    auto root = doc.FirstChildElement();

    if (root == nullptr) return false;

    for (int i = MODULE_HALO1; i < MODULE_MCC; ++i) {
        GetSubModule(i)->patches()->clear();
    }

    for (auto element = root->FirstChildElement(); element != nullptr; element = element->NextSiblingElement()) {
        auto p_module_name = element->Attribute("name");
        auto p_module_version = element->Attribute("version");

        if (p_module_name == nullptr || p_module_version == nullptr) return false;

        auto p_module = GetSubModule(p_module_name);

        if (p_module == nullptr) continue;

        if (strcmp(p_module_version, GAME_VERSION) != 0) continue;

        for (auto patch = element->FirstChildElement(); patch != nullptr; patch = patch->NextSiblingElement()) {
            auto p_patch_name = patch->Attribute("name");
            auto p_patch_desc = patch->Attribute("desc");
            auto p_patch_offset = patch->Attribute("offset");
            auto p_patch_data = patch->Attribute("aob");
            auto p_patch_enable = patch->Attribute("default");

            if (p_patch_name == nullptr || p_patch_offset == nullptr || p_patch_data == nullptr) continue;

            if (p_patch_desc == nullptr) p_patch_desc = "";

            auto patch_enable = false;

            if (p_patch_enable != nullptr && strcmp(p_patch_enable, "true") == 0)
                patch_enable = true;

            try {
                p_module->patches()->add(p_patch_name, p_patch_desc,
                          std::strtoull(p_patch_offset, nullptr, 16),
                          p_converter(p_patch_data),patch_enable);
            } catch (std::exception& e) {
                LOG_ERROR("Patch {}: {}", p_patch_name, e.what());
                continue;
            }
        }
    }

    return true;
}

#include "imgui.h"
#include "global/Global.h"

namespace MCC::Module {
    void ContextPatch();
    void ContextEngine();

    void ImGuiContext() {
#if !ALPHARING_DEVTOOLS
        return;
#else
        static bool show_patch;
        static bool show_engine;

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Game Tools")) {
                ImGui::MenuItem("Session controls", nullptr, &show_engine);
                ImGui::MenuItem("Patches", nullptr, &show_patch);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (show_patch) {
            ImGui::SetNextWindowSize(ImVec2(680.0f, 560.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Patches", &show_patch, ImGuiWindowFlags_NoCollapse))
                ContextPatch();
            ImGui::End();
        }

        if (show_engine) {
            ImGui::SetNextWindowSize(ImVec2(560.0f, 540.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Game Session", &show_engine, ImGuiWindowFlags_NoCollapse))
                ContextEngine();
            ImGui::End();
        }
#endif
    }

    void ContextEngine() {
        auto p_engine = GameEngine();

        if (p_engine == nullptr) {
            ImGui::TextDisabled("No active game session");
            return;
        }

        ImGui::SeparatorText("Session");
        if (ImGui::Button("Load checkpoint", ImVec2(150.0f, 0.0f))) p_engine->load_checkpoint();
        ImGui::SameLine();
        if (ImGui::Button("New round", ImVec2(120.0f, 0.0f))) p_engine->new_round();
        ImGui::SameLine();
        if (ImGui::Button("Restart", ImVec2(110.0f, 0.0f))) p_engine->restart();

        if (ImGui::Button("Pause", ImVec2(150.0f, 0.0f))) p_engine->pause(true);
        ImGui::SameLine();
        if (ImGui::Button("Resume", ImVec2(120.0f, 0.0f))) p_engine->pause(false);

        if (ImGui::Button("Reload settings", ImVec2(150.0f, 0.0f))) p_engine->reload_setting();
        ImGui::SameLine();
        if (ImGui::Button("Apply settings", ImVec2(120.0f, 0.0f))) p_engine->load_setting();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.22f, 0.20f, 1.0f));
        if (ImGui::Button("Exit session", ImVec2(110.0f, 0.0f))) p_engine->exit();
        ImGui::PopStyleColor(2);

#pragma region HaloScript
        static char halo_script[1024] = {0};

        ImGui::SeparatorText("HaloScript");
        if (ImGui::InputTextMultiline("##HaloScript", halo_script + 4, sizeof(halo_script) - 4, ImVec2(-1.0f, 120.0f)))
            halo_script[1023] = '\0';

        if (ImGui::Button("Execute command")) {
            halo_script[0] = 'H';
            halo_script[1] = 'S';
            halo_script[2] = ':';
            halo_script[3] = ' ';
            halo_script[1023] = '\0';
            p_engine->execute_command(halo_script);
        }
#pragma endregion

#pragma region ChangeTeam
        ImGui::SeparatorText("Player team");
        static int player = 0;
        static int team = 0;
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("Player", &player, "Player 1\0Player 2\0Player 3\0Player 4");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("Team", &team, "Red\0Blue\0Green\0Orange\0Purple\0Gold\0Brown\0Pink");
        if (ImGui::Button("Apply team")) {
            auto xuid = CGameManager::get_xuid(player);
            if (xuid != 0) p_engine->change_team(xuid, team);
        }
#pragma endregion
    }

    void ContextPatch() {
        static int counter;
        auto p_print = [](CPatch* patch) {
            bool enabled = patch->enabled();

            ImGui::PushID(counter++);
            if (ImGui::Checkbox(patch->name(), &enabled)) patch->setState(enabled);
            ImGui::PopID();

            if (ImGui::IsItemHovered() && patch->have_desc())
                ImGui::SetTooltip("%s", patch->desc());
        };

        if (ImGui::Button("Reload patches")) ReloadPatch();
        ImGui::Spacing();

        if (!ImGui::BeginTabBar("patch")) return;

        counter = 0;

        for (int i = MODULE_HALO1; i < MODULE_MCC; ++i) {
            auto p_patches = GetSubModule((eModule)i)->patches();

            if (ImGui::BeginTabItem(cModuleName[i])) {
                ImGui::SeparatorText("Built-in patches");
                for (auto patch : p_patches->embed_patches())
                    p_print(patch);

                ImGui::SeparatorText("Loaded patches");
                for (auto patch : p_patches->patches())
                    p_print(patch);

                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }
}
