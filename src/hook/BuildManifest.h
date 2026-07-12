#pragma once

#include <cstdint>

namespace AlphaRing::Compatibility {
    struct MccOffsets {
        std::int64_t game_engine;
        std::int64_t game_manager;
        std::int64_t device_manager;
        std::int64_t delta_time;
        std::int64_t is_in_game;
        std::int64_t game_global;
        std::int64_t index_by_xuid;
    };

    struct BuildManifest {
        const char* executable;
        const char* version;
        MccOffsets mcc;
    };

    const BuildManifest& CurrentBuild();
}
