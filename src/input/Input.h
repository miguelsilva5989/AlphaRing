#pragma once

#include <windows.h>
#include <xinput.h>

namespace AlphaRing {
    namespace Input {
        bool Init();
        bool Shutdown();
        bool Update();
        bool GetXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState);
        void SetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
    };
}
