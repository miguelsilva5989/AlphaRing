#pragma once

#include <windows.h>

namespace AlphaRing::Runtime {
    enum class State {
        Dormant,
        Starting,
        Running,
        Failed,
        Stopping,
        Stopped,
    };

    DWORD WINAPI Bootstrap(void* module);
    void RequestStop();
    bool Shutdown();
    State GetState();
}
