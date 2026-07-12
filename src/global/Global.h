#pragma once

#include <atomic>

#define DefGlobal(name) \
    struct name##_t;    \
    extern name##_t s_##name; \
    inline name##_t* name() {return &s_##name;} \
    struct name##_t

#define ImplGlobal(name) \
    name##_t s_##name;

namespace AlphaRing::Global {
    DefGlobal(Global) {
        bool wireframe;
        bool wireframe_model;
        bool wireframe_structure;
        bool show_imgui = true;
        // on menu
        bool show_imgui_mouse = true;
        bool pause_game_on_menu_shown = false;
        bool disable_input_on_menu_shown = true;
    };

    namespace Halo3 {
        DefGlobal(Physics) {
            bool enable_bump_possession;
        };

        DefGlobal(Render) {
            bool model;
            bool structure;
        };
    }

    namespace MCC {
        DefGlobal(Splitscreen) {
            std::atomic<bool> b_override {false};
            std::atomic<int> player_count {1};

            // player 0
            std::atomic<bool> b_player0_use_km {true};
            std::atomic<bool> b_override_profile {false};
            std::atomic<bool> b_use_player0_profile {true};
        };
    }
}
