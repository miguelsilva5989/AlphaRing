#pragma once

#include <array>

namespace MCC::Settings {
    constexpr int kMaximumPlayers = 4;
    constexpr int kUnassignedController = 4;

    int NormalizePlayerCount(int player_count);
    void NormalizeControllerAssignments(
            bool player_one_uses_keyboard,
            std::array<int, kMaximumPlayers>& assignments
    );
}
