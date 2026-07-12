#include "ConfigValidation.h"

#include <algorithm>

namespace MCC::Settings {
    int NormalizePlayerCount(int player_count) {
        return std::clamp(player_count, 1, kMaximumPlayers);
    }

    void NormalizeControllerAssignments(
            bool player_one_uses_keyboard,
            std::array<int, kMaximumPlayers>& assignments
    ) {
        bool used[kMaximumPlayers] {};
        for (int player = 0; player < kMaximumPlayers; ++player) {
            if (player == 0 && player_one_uses_keyboard)
                continue;

            int& controller = assignments[static_cast<size_t>(player)];
            if (controller < 0 || controller >= kMaximumPlayers || used[controller]) {
                controller = kUnassignedController;
                continue;
            }
            used[controller] = true;
        }
    }
}
