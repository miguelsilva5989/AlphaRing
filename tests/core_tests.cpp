#include "mcc/settings/ConfigValidation.h"
#include "render/d3d11/MonitorLayout.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
    void Check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(1);
        }
    }
}

int main() {
    using namespace MCC::Settings;
    Check(NormalizePlayerCount(-2) == 1, "player count lower bound");
    Check(NormalizePlayerCount(2) == 2, "player count preserves valid value");
    Check(NormalizePlayerCount(9) == 4, "player count upper bound");

    std::array<int, kMaximumPlayers> duplicate_assignments {0, 0, 9, -1};
    NormalizeControllerAssignments(false, duplicate_assignments);
    Check(duplicate_assignments == std::array<int, 4> {0, 4, 4, 4}, "duplicate controllers are removed");

    std::array<int, kMaximumPlayers> keyboard_assignments {0, 0, 1, 2};
    NormalizeControllerAssignments(true, keyboard_assignments);
    Check(keyboard_assignments == std::array<int, 4> {0, 0, 1, 2}, "keyboard player does not reserve XInput slot");

    using namespace AlphaRing::Render::D3d11;
    const RectI ultrawide {0, 0, 3440, 1440};
    const RectI standard {3440, 0, 2560, 1440};
    const RectI fitted = FitOutputToAspect(standard, ultrawide);
    Check(fitted.x == 3440 && fitted.y == 184, "secondary output is vertically centered");
    Check(fitted.w == 2560 && fitted.h == 1072, "secondary output preserves primary aspect");

    const SourceCrop native_player_two = CalculateSourceCrop(3440, 2880, 1, fitted);
    Check(native_player_two.left == 0 && native_player_two.top == 1440, "native player two starts in lower render target");
    Check(native_player_two.width == 3440 && native_player_two.height == 1440, "native output keeps the full player frame");

    const SourceCrop mirrored_player_one = CalculateSourceCrop(3440, 1440, 0, ultrawide);
    Check(mirrored_player_one.left == 860 && mirrored_player_one.width == 1720, "legacy mirror crop is deterministic");
    Check(mirrored_player_one.top == 0 && mirrored_player_one.height == 720, "legacy mirror uses the top player half");

    std::cout << "AlphaRing core tests passed\n";
    return 0;
}
